// ==============================================================================
// Layer 2: DSP Processor - PerlinNoiseSource (Vorago Life Modulator)
// ==============================================================================
// Band-limited fractal-Brownian-motion (fBm) value noise built from 1-D Perlin
// gradient noise, evaluated at control rate and smoothed per sample. The
// "smooth but unpredictable" counterpart to BrownianDrift: where the OU walk is
// memoryless-plus-mean-reversion, Perlin noise is a deterministic function of
// POSITION, so the same seed/rate always produces the same trajectory shape and
// the spectral content is bounded by construction rather than by a filter.
//
// Spec:  specs/vorago-phase1-events-modulation/spec.md   (FR-012..FR-019)
// Plan:  specs/vorago-phase1-events-modulation/plan.md   (section 1)
// Tasks: specs/vorago-phase1-events-modulation/tasks.md  (T003)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (all methods noexcept, no allocation, no
//   locks, no exceptions, no I/O; all state is fixed-size members)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr constants)
// - Principle IX: Layer 2 (includes Layer 0 + Layer 1 + stdlib only)
//
// ------------------------------------------------------------------------------
// ALGORITHM (FR-012..FR-015)
// ------------------------------------------------------------------------------
//   position (in lattice cells) advances by rate * kControlRateInterval / sr on
//   every control step. One octave is classic 1-D Perlin gradient noise:
//
//       i0 = floor(x),  t = x - i0
//       s  = t^3 * (t * (6t - 15) + 10)             (Ken Perlin's smootherstep)
//       n  = (g0 * t) * (1 - s) + (g1 * (t - 1)) * s
//
//   with UNIT gradients g in {-1, +1} drawn from a STATELESS hash of the octave
//   seed and the lattice index (FR-012) - never from a running RNG stream, so
//   the value at a position does not depend on the path taken to reach it.
//
//   fBm sums kMaxOctaves-capped octaves at lacunarity 2 and persistence 0.5:
//
//       fbm(x) = ( SUM_{k<n} 0.5^k * noise_k(x * 2^k) ) / SUM_{k<n} 0.5^k
//
//   The divisor amplitudeSum_ = 2 - 2^(1-n) (1, 1.5, 1.75, 1.875) is what keeps
//   the sum inside [-1, +1] for ANY octave count.
//
// ------------------------------------------------------------------------------
// (1) FR-017 - why kGradientNormalize is exactly 2.0
// ------------------------------------------------------------------------------
//   With unit gradients the raw interpolant is bounded by |n(x)| <= 0.5. The
//   extremum is attained at t = 0.5 with OPPOSING gradients (g0 = +1,
//   g1 = -1): s(0.5) = 0.5, so
//
//       n = (1 * 0.5) * 0.5 + (-1 * (-0.5)) * 0.5 = 0.25 + 0.25 = 0.5
//
//   Multiplying by kGradientNormalize = 2.0 therefore maps ONE octave onto
//   exactly [-1, +1] - not approximately, and not by clamping. The terminal
//   std::clamp in evaluateFbm() is an inert net: it can never be the source of
//   the SC-001 bound (which is why SC-001 also asserts EXACT half-scaling
//   between depth 0.5 and depth 1.0 - a clamp cannot produce that).
//
// ------------------------------------------------------------------------------
// (2) The OnePoleSmoother completion snap is DOCUMENTED, not a defect
// ------------------------------------------------------------------------------
//   OnePoleSmoother snaps current_ to target_ whenever the residual falls below
//   kCompletionThreshold = 1e-4 (smoother.h:55, :199-201, :251-253).
//   - At kMaxRate with 4 octaves the target moves far enough every control step
//     that the residual never drops under 1e-4, so the snap never fires and the
//     5 ms one-pole shapes the output as intended (this is the regime SC-002
//     measures).
//   - At low rates the snap fires on essentially every control step and the
//     output degenerates to the raw 32-sample staircase. That staircase's step
//     is then <= 1e-4 by definition of the threshold, i.e. 20x INSIDE SC-002's
//     2.0e-3 per-sample slew budget. Harmless, and deliberately not worked
//     around.
//
// ------------------------------------------------------------------------------
// (3) FR-007 carve-out - the lattice index is NEVER wrapped
// ------------------------------------------------------------------------------
//   positionCells_ is unwrapped and monotonically increasing. Wrapping it (to a
//   period, or into a table of permutations) would make the trajectory an exact
//   repeat of itself, which would still pass every criterion in this phase while
//   being audibly wrong over long drones. kIndexBias below exists only so that
//   NEGATIVE lattice indices - unreachable today, but legal if a caller ever
//   drives position backwards - hash to a well-defined bucket instead of relying
//   on implementation-defined signed conversion.
//
// ------------------------------------------------------------------------------
// (4) FR-003 - why the output smoother is advanced PER SAMPLE, not in closed form
// ------------------------------------------------------------------------------
//   SC-004(a) requires a block-driven render to reproduce a pure process()
//   render within 1e-6 for ANY block partitioning. OnePoleSmoother offers a
//   closed-form advanceSamples(N) (smoother.h:243) for exactly this purpose, and
//   it is what BrownianDrift uses (brownian_drift.h:204) - but it CANNOT meet
//   that gate here, for two independent reasons, so this component advances the
//   smoother one process() call per sample and DECIMATES ONLY THE NOISE.
//
//   (i) The completion snap is applied at OPPOSITE ends of a sample by the two
//       paths. process() checks first and steps second: it snaps
//       current_ = target_ when the residual is ALREADY inside
//       kCompletionThreshold on entry (smoother.h:198-201) and never re-checks
//       the value it just produced. advanceSamples() steps first and checks
//       second: it early-RETURNS with current_ UNTOUCHED when it starts
//       complete (smoother.h:244) and snaps only AFTER the decay
//       (smoother.h:251-253). That is deliberate, load-bearing primitive
//       behaviour - two components transcribe it verbatim
//       (entropy_processor.h:595-608, atmosphere_engine.h:1439-1452) - so the
//       mismatch is reconciled in THIS consumer, never by editing the
//       primitive. Neither half is a corner case here: the entry one fires on
//       the very first control step (at position 0 the noise is exactly 0 for
//       every octave, because t = 0 kills both gradient terms, so the first
//       target is ~1e-6 - inside the threshold), and the exit one fires at
//       every turning point of the noise, where the target briefly stops moving
//       and the residual coasts down through 1e-4. MEASURED divergence of a
//       plain advanceSamples(N) block path from the per-sample render over the
//       SC-004(a) 60 s case: 1.98e-4, i.e. 198x the gate.
//
//   (ii) Even with that snap reconciled exactly - and it CAN be, by
//       advanceSamples(N-1) followed by one process(), whose snap predicates
//       then line up - the closed form is still only equal to the iterated form
//       to within float rounding. current_ and target_ are floats of order 1,
//       so every iterated step quantises the residual with an absolute error of
//       ~6e-8, which is ~1e-3 RELATIVE on a residual that is itself ~1e-4 near
//       the threshold. Both consequences were measured on the SC-004(a) case:
//       the pure-geometric drift alone reaches 3.6e-7 (only 2.8x under the
//       gate, before any -ffast-math pow on the macOS leg), and that same
//       rounding flips the SNAP predicate outright a few times per minute, each
//       flip costing a full 1.5e-4.
//
//   Advancing per sample makes the block path BIT-IDENTICAL to the per-sample
//   path (measured maxDiff = 0.0 at kMinRate, 1.0 and kMaxRate over 60 s), so
//   SC-004(a) holds with no tolerance budget spent at all. The one shortcut
//   taken is exact rather than approximate: when the smoother is already
//   complete at chunk entry, every process() call in that chunk would take
//   smoother.h:198-201's snap branch, so the chunk collapses to a single
//   snapToTarget().
//
//   What this costs is only the smoother: the EXPENSIVE part - the hashed
//   gradient-octave evaluations - stays decimated at kControlRateInterval, so
//   processBlock() remains O(control steps) in noise work and is O(numSamples)
//   only in one-pole steps (one compare plus one multiply-add each, the same
//   arithmetic the per-sample path performs anyway).
//
// ------------------------------------------------------------------------------
// NON-FINITE HYGIENE
// ------------------------------------------------------------------------------
//   No std::isnan/isinf anywhere: macOS CI builds with -ffast-math, which folds
//   them. Every float setter routes through sanitizeClamp(), which maps NaN to
//   the LOW bound via detail::isNaN (db_utils.h:99) - std::clamp on its own
//   PROPAGATES NaN. The noise itself is finite by construction: the gradients
//   are +/-1, t is in [0,1), and the sum is divided by a positive constant.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief Band-limited fBm Perlin-noise modulation source (1-4 octaves).
///
/// Implements the ModulationSource interface. Evaluated at control rate (every
/// kControlRateInterval samples) and smoothed per sample, so a block consumer
/// can advance it once per block via processBlock().
///
/// @par Output Range: [-1.0, +1.0] (bipolar, FIXED - it does not shrink with
///      the depth setting; depth scales the signal inside that range, FR-006).
class PerlinNoiseSource final : public ModulationSource {
public:
    /// Slowest travel speed, in lattice cells per second.
    static constexpr float kMinRate = 0.005f;
    /// Fastest travel speed, in lattice cells per second.
    static constexpr float kMaxRate = 5.0f;
    /// Fewest fBm octaves.
    static constexpr int kMinOctaves = 1;
    /// Most fBm octaves (also the number of independent gradient streams).
    static constexpr int kMaxOctaves = 4;
    /// Amplitude ratio between successive octaves.
    static constexpr float kPersistence = 0.5f;
    /// Frequency ratio between successive octaves (exact power of two).
    static constexpr float kLacunarity = 2.0f;
    /// Maps one unit-gradient octave onto exactly [-1, +1]. See note (1) above.
    static constexpr float kGradientNormalize = 2.0f;
    /// Output smoothing time (ms). See note (2) above.
    static constexpr float kOutputSmoothMs = 5.0f;
    /// Control-rate decimation, matching BrownianDrift (brownian_drift.h:105).
    static constexpr size_t kControlRateInterval = 32;

    static constexpr float kDefaultRate = 0.1f;
    static constexpr int kDefaultOctaves = 2;
    static constexpr float kDefaultDepth = 1.0f;
    static constexpr std::uint32_t kDefaultPerlinSeed = 0x9E37u;

    PerlinNoiseSource() noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle (FR-002)
    // -------------------------------------------------------------------------

    /// @brief Derive the control-step increment and initialise state.
    /// Full re-initialisation: calling it twice leaves no half-completed state.
    /// After this call getCurrentValue() is well defined without any advance.
    /// @param sampleRate Sample rate in Hz (floored at 1 Hz: a zero/negative
    ///        rate would make cellsPerControlStep_ non-finite)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        updateIncrement();
        outputSmoother_.configure(kOutputSmoothMs,
                                  static_cast<float>(sampleRate_));
        initState();
    }

    /// @brief Rewind to the exact post-prepare state.
    /// Keeps the configured sample rate; the same seed re-renders identically.
    void reset() noexcept {
        initState();
    }

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// @brief Set the gradient-hash seed (FR-014). Plain non-virtual member:
    /// ModulationSource declares only getCurrentValue()/getSourceRange() as
    /// virtuals (modulation_source.h:37,41).
    /// @param seedValue Seed (0 aliases Xorshift32's default, 2463534242)
    void setSeed(std::uint32_t seedValue) noexcept {
        configuredSeed_ = normalizeSeed(seedValue);
        octaveSeeds_ = deriveSeeds(configuredSeed_);
    }

    /// @brief Travel speed in lattice cells per second (FR-013).
    /// Changes the SPEED of travel, never the PLACE: positionCells_ is
    /// untouched, so a rate change cannot make the trajectory jump.
    /// @param cellsPerSecond Rate, clamped to [kMinRate, kMaxRate] (NaN -> min)
    void setRate(float cellsPerSecond) noexcept {
        rate_ = sanitizeClamp(cellsPerSecond, kMinRate, kMaxRate);
        updateIncrement();
    }

    /// @brief Number of fBm octaves (FR-015).
    /// @param numOctaves Clamped to [kMinOctaves, kMaxOctaves]
    void setOctaves(int numOctaves) noexcept {
        octaves_ = std::clamp(numOctaves, kMinOctaves, kMaxOctaves);
        amplitudeSum_ = amplitudeSumFor(octaves_);
    }

    /// @brief Output depth (FR-016). Scales the signal INSIDE the fixed range.
    /// @param normalized 0..1 (NaN -> 0)
    void setDepth(float normalized) noexcept {
        depth_ = sanitizeClamp(normalized, 0.0f, 1.0f);
    }

    [[nodiscard]] float getRate() const noexcept { return rate_; }
    [[nodiscard]] int getOctaves() const noexcept { return octaves_; }
    [[nodiscard]] float getDepth() const noexcept { return depth_; }

    /// @brief Current (unwrapped) position along the noise lattice, in cells.
    [[nodiscard]] double getPosition() const noexcept { return positionCells_; }

    // -------------------------------------------------------------------------
    // Advance
    // -------------------------------------------------------------------------

    /// @brief Advance one sample. Re-evaluates the noise on control boundaries.
    void process() noexcept {
        --samplesUntilControl_;
        if (samplesUntilControl_ <= 0) {
            samplesUntilControl_ = static_cast<int>(kControlRateInterval);
            advanceControlStep();
        }
        // OnePoleSmoother::process() is [[nodiscard]] (smoother.h:197); discard
        // exactly as BrownianDrift does (brownian_drift.h:187) so the
        // zero-warning gate stays clean.
        static_cast<void>(outputSmoother_.process());
    }

    /// @brief Advance a whole block, evaluating the noise at control rate (FR-003).
    /// BIT-IDENTICAL to numSamples process() calls, for any partitioning: the
    /// noise is decimated to kControlRateInterval, but the output smoother is
    /// advanced one process() step per sample - see note (4) in the banner for
    /// why the closed-form advanceSamples() cannot hold SC-004(a)'s 1e-6 gate.
    /// processBlock(0) is a no-op.
    /// @param numSamples Number of audio samples in this block
    void processBlock(size_t numSamples) noexcept {
        auto remaining = static_cast<int>(numSamples);
        while (remaining > 0) {
            if (samplesUntilControl_ <= 0) {
                samplesUntilControl_ = static_cast<int>(kControlRateInterval);
                advanceControlStep();
            }
            const int advance = std::min(remaining, samplesUntilControl_);
            samplesUntilControl_ -= advance;
            remaining -= advance;
            // Exactly `advance` OnePoleSmoother::process() steps - NOT
            // advanceSamples(advance), see note (4). The already-complete case
            // is collapsed to one snapToTarget(): every process() call in the
            // chunk would take the snap branch (smoother.h:198-201) and leave
            // current_ == target_, so the shortcut is exact, not approximate.
            // OnePoleSmoother::process() is [[nodiscard]] (smoother.h:197).
            if (outputSmoother_.isComplete()) {
                outputSmoother_.snapToTarget();
            } else {
                for (int i = 0; i < advance; ++i) {
                    static_cast<void>(outputSmoother_.process());
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // ModulationSource interface (FR-001)
    // -------------------------------------------------------------------------

    [[nodiscard]] float getCurrentValue() const noexcept override {
        return std::clamp(outputSmoother_.getCurrentValue(), -1.0f, 1.0f);
    }

    /// @brief Fixed at polarity full scale, independent of depth (FR-006).
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};
    }

    // -------------------------------------------------------------------------
    // Diagnostics (FR-015)
    // -------------------------------------------------------------------------

    /// @brief Raw value of one octave stream at the current position.
    ///
    /// Independent of the configured octave count, of amplitudeSum_, of depth_
    /// and of the output smoother: all kMaxOctaves gradient streams are always
    /// derived, so octave k's value is bit-identical no matter how many octaves
    /// are currently summed. Out-of-range indices return 0 (documented, not UB).
    /// @param octaveIndex Stream index in [0, kMaxOctaves)
    [[nodiscard]] float getOctaveValue(std::size_t octaveIndex) const noexcept {
        if (octaveIndex >= static_cast<std::size_t>(kMaxOctaves)) {
            return 0.0f;
        }
        return rawOctaveNoise(octaveIndex,
                              positionCells_ * octaveScale(octaveIndex));
    }

private:
    using OctaveSeeds = std::array<std::uint32_t, static_cast<std::size_t>(kMaxOctaves)>;

    /// Bias applied before hashing a lattice index so that negative indices are
    /// well defined rather than UB-by-omission. See note (3) in the banner.
    static constexpr std::int64_t kIndexBias = 1LL << 31;

    // -------------------------------------------------------------------------
    // Pure helpers
    // -------------------------------------------------------------------------

    /// NaN-aware clamp: std::clamp PROPAGATES NaN, which would let a NaN rate or
    /// depth through every bound check. NaN maps to the LOW bound.
    [[nodiscard]] static constexpr float sanitizeClamp(float value, float lo,
                                                       float hi) noexcept {
        if (detail::isNaN(value)) {
            return lo;
        }
        return (value < lo) ? lo : ((value > hi) ? hi : value);
    }

    /// Mirror Xorshift32's zero-seed substitution (random.h:44, :73-74) so that
    /// setSeed(0) aliases setSeed(2463534242) exactly, as it does for every
    /// other seeded source in the library.
    [[nodiscard]] static constexpr std::uint32_t normalizeSeed(
        std::uint32_t seedValue) noexcept {
        return Xorshift32{seedValue}.state();
    }

    /// Derive ALL kMaxOctaves stream seeds, always - independent of octaves_.
    [[nodiscard]] static constexpr OctaveSeeds deriveSeeds(
        std::uint32_t base) noexcept {
        OctaveSeeds seeds{};
        for (std::size_t k = 0; k < seeds.size(); ++k) {
            seeds[k] = deriveStreamSeed(base, k);
        }
        return seeds;
    }

    /// SUM_{k<n} kPersistence^k = 2 - 2^(1-n): 1, 1.5, 1.75, 1.875.
    [[nodiscard]] static constexpr double amplitudeSumFor(int numOctaves) noexcept {
        double sum = 0.0;
        double amp = 1.0;
        for (int k = 0; k < numOctaves; ++k) {
            sum += amp;
            amp *= static_cast<double>(kPersistence);
        }
        return sum;
    }

    /// Frequency multiplier of octave k. kLacunarity is exactly 2, so this is an
    /// exact power of two and the octave positions stay exactly representable.
    [[nodiscard]] static constexpr double octaveScale(std::size_t k) noexcept {
        return static_cast<double>(1ULL << k);
    }

    /// Stateless unit gradient at a lattice index (FR-012). NEVER a running
    /// stream: the value at a position must not depend on the path to it.
    [[nodiscard]] float gradientAt(std::size_t k, std::int64_t index) const noexcept {
        const std::size_t salt =
            static_cast<std::size_t>(static_cast<std::uint64_t>(index + kIndexBias));
        return (deriveStreamSeed(octaveSeeds_[k], salt) & 1u) != 0u ? 1.0f : -1.0f;
    }

    /// One octave of classic 1-D Perlin gradient noise, in [-1, +1] after the
    /// kGradientNormalize scaling. All intermediate math in double.
    [[nodiscard]] float rawOctaveNoise(std::size_t k, double x) const noexcept {
        const double fx = std::floor(x);
        const auto i0 = static_cast<std::int64_t>(fx);
        const double t = x - fx;
        const double s = t * t * t * (t * ((t * 6.0) - 15.0) + 10.0);
        const double g0 = static_cast<double>(gradientAt(k, i0));
        const double g1 = static_cast<double>(gradientAt(k, i0 + 1));
        const double n = ((g0 * t) * (1.0 - s)) + ((g1 * (t - 1.0)) * s);
        return static_cast<float>(static_cast<double>(kGradientNormalize) * n);
    }

    /// fBm over the configured octave count, depth-scaled. The terminal clamp is
    /// an inert net (see note (1)), never the source of the bound.
    [[nodiscard]] float evaluateFbm() const noexcept {
        double sum = 0.0;
        double amp = 1.0;
        for (int k = 0; k < octaves_; ++k) {
            const auto index = static_cast<std::size_t>(k);
            sum += amp * static_cast<double>(
                             rawOctaveNoise(index, positionCells_ * octaveScale(index)));
            amp *= static_cast<double>(kPersistence);
        }
        const double normalized = (sum / amplitudeSum_) * static_cast<double>(depth_);
        return std::clamp(static_cast<float>(normalized), -1.0f, 1.0f);
    }

    // -------------------------------------------------------------------------
    // State transitions
    // -------------------------------------------------------------------------

    void updateIncrement() noexcept {
        cellsPerControlStep_ = static_cast<double>(rate_) *
                               static_cast<double>(kControlRateInterval) / sampleRate_;
    }

    void initState() noexcept {
        octaveSeeds_ = deriveSeeds(configuredSeed_);
        positionCells_ = 0.0;
        samplesUntilControl_ = 0;
        outputSmoother_.snapTo(evaluateFbm());
    }

    void advanceControlStep() noexcept {
        positionCells_ += cellsPerControlStep_;
        outputSmoother_.setTarget(evaluateFbm());
    }

    // -------------------------------------------------------------------------
    // State (all fixed-size; no RNG object stored - the gradients are hashed)
    // -------------------------------------------------------------------------

    double sampleRate_ = 44100.0;
    double cellsPerControlStep_ = static_cast<double>(kDefaultRate) *
                                  static_cast<double>(kControlRateInterval) / 44100.0;
    double positionCells_ = 0.0;  ///< UNWRAPPED lattice position, in cells
    double amplitudeSum_ = amplitudeSumFor(kDefaultOctaves);

    float rate_ = kDefaultRate;
    float depth_ = kDefaultDepth;
    int octaves_ = kDefaultOctaves;

    int samplesUntilControl_ = 0;

    std::uint32_t configuredSeed_ = normalizeSeed(kDefaultPerlinSeed);
    OctaveSeeds octaveSeeds_ = deriveSeeds(normalizeSeed(kDefaultPerlinSeed));
    OnePoleSmoother outputSmoother_;
};

}  // namespace DSP
}  // namespace Krate
