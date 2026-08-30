// ==============================================================================
// Layer 2: DSP Processor - BreathingModulator (Seraphis Life Modulator)
// ==============================================================================
// An asymmetric inhale/exhale cycle -- deliberately NOT a sine. Rate 0.01-0.5 Hz,
// depth, and cycle-to-cycle period jitter ("irregularity") are the entire control
// surface; the breath SHAPE itself is hardcoded.
//
// Spec:  specs/seraphis-phase1-life-modulators/spec.md  (FR-021..FR-024)
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 2)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (all methods noexcept, no allocation, no
//   locks, no exceptions, no I/O; all state is fixed-size members)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr constants)
// - Principle IX: Layer 2 (includes Layer 0 + Layer 1 + stdlib only)
//
// ------------------------------------------------------------------------------
// SHAPE (FR-021) - fixed, physiologically-inspired, 40 % inhale / 60 % exhale
// ------------------------------------------------------------------------------
//   A phase phi in [0,1) advances at `rate` Hz. The unipolar shape y(phi) in
//   [0,1] is hardcoded with DIFFERENT curvature on the two limbs:
//
//       inhale  phi in [0, 0.4):   u = phi / 0.4
//                                  y = 0.5 * (1 - cos(pi * u^0.8))
//       exhale  phi in [0.4, 1):   v = (phi - 0.4) / 0.6
//                                  y = 0.5 * (1 + cos(pi * v^1.3))
//
//   y = 0 at phi = 0, y = 1 at phi = 0.4, y -> 0 as phi -> 1, so the shape is
//   value-continuous around the whole cycle. The rise occupies 40 % of the
//   period and the fall 60 % (rise != fall), and the two exponents warp the
//   limbs differently, so the spectrum carries strong 2f/3f energy that a
//   symmetric sine does not. Both facts are asserted by
//   BreathingModulator_ShapeAsymmetricAndNonSinusoidal.
//
//   Output = depth * (2y - 1), i.e. bipolar in [-depth, +depth] and therefore
//   inside [-1, +1] by construction for every setting (FR-023 / FR-006).
//
// ------------------------------------------------------------------------------
// IRREGULARITY (FR-024) - bounded, seeded, cycle-to-cycle period jitter
// ------------------------------------------------------------------------------
//   At each phase wrap a jitter factor is drawn:
//
//       jitter = 1 + irregularity * 0.5 * rng.nextFloat()      // rng in [-1,1]
//       jitter = max(jitter, kMinJitter)                       // positive period
//
//   and multiplies the NEXT cycle's phase increment. At irregularity = 0 no draw
//   happens at all and the jitter is exactly 1.0, so the period is exactly
//   constant -- which is what makes the SC-003(b) period prediction (period =
//   1 / rate) well posed. The kMinJitter floor is the spec's edge-case guard:
//   irregularity = 1 must never produce a zero or negative period. (At
//   irregularity = 1 the raw draw already lies in [0.5, 1.5], so the floor is a
//   belt-and-braces bound rather than an active clamp.)
//
//   The draw happens at a CYCLE BOUNDARY, which is wall-clock aligned, so the
//   same seed produces the same wall-clock jitter sequence at any sample rate.
//   That is why SC-005 uses option (a), like-for-like comparison, here.
//
// ------------------------------------------------------------------------------
// SC-002 - bounded per-sample slew
// ------------------------------------------------------------------------------
//   The peak slope of the shape is |dy/dphi| ~ 3.7 (attained mid-inhale), so
//   with output = depth * (2y - 1) and the fastest reachable breath
//   (kMaxRate = 0.5 Hz scaled by the largest jitter, 1.5 -> 0.75 Hz):
//
//       |d(out)/dt| <= 2 * 3.7 * 0.75 ~= 5.6 per second
//       per sample @48 kHz            ~= 1.2e-4  <<  2.0e-3  (SC-002 threshold)
//
//   The raw signal is therefore already far inside the bound; the light 20 ms
//   output smoother exists only so a block-decimated advance cannot introduce a
//   step at a block boundary.
//
// ------------------------------------------------------------------------------
// NON-FINITE HYGIENE
// ------------------------------------------------------------------------------
//   No std::isnan anywhere: macOS CI builds with -ffast-math, which breaks it.
//   Safety comes from construction instead - the phase accumulator is a double
//   wrapped into [0,1), std::pow is only ever called on a base in [0,1], the
//   shape is bounded in [0,1] analytically, the smoother target is clamped to
//   [-1,+1], and OnePoleSmoother::setTarget already sanitizes NaN/Inf
//   (smoother.h:170).
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief Slow, asymmetric, bounded breath cycle.
///
/// Implements the ModulationSource interface. Advanceable per sample via
/// process() or once per block via processBlock().
///
/// @par Output Range: [-1.0, +1.0] (bipolar, FIXED - it does not shrink with
///      the depth setting; depth scales the signal inside that range).
class BreathingModulator : public ModulationSource {
public:
    /// Slowest breath (FR-022): 100 s nominal period.
    static constexpr float kMinRate = 0.01f;
    /// Fastest breath (FR-022): 2 s nominal period.
    static constexpr float kMaxRate = 0.5f;
    static constexpr float kDefaultRate = 0.1f;
    static constexpr float kDefaultDepth = 1.0f;
    static constexpr float kDefaultIrregularity = 0.0f;

    /// Output smoothing time (ms). Block-boundary safety only - see the SC-002
    /// note above; the raw shape is already far inside the slew bound.
    static constexpr float kOutputSmoothMs = 20.0f;

    /// Fraction of the cycle spent inhaling (the remainder is the exhale).
    static constexpr float kInhaleFraction = 0.4f;
    /// Curvature warp of the inhale limb (< 1 -> fast start, slow finish).
    static constexpr float kInhaleExponent = 0.8f;
    /// Curvature warp of the exhale limb (> 1 -> slow start, long tail).
    static constexpr float kExhaleExponent = 1.3f;

    /// Half-width of the jitter factor at irregularity = 1.
    static constexpr float kJitterSpan = 0.5f;
    /// Hard floor on the jitter factor: guarantees a strictly positive period.
    static constexpr float kMinJitter = 0.1f;

    static constexpr std::uint32_t kDefaultBreathSeed = 0xB2EAu;

    BreathingModulator() noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle (FR-004)
    // -------------------------------------------------------------------------

    /// @brief Configure for a sample rate and initialise state.
    /// After this call getCurrentValue() is well defined without any advance.
    /// @param sampleRate Sample rate in Hz (floored at 1 Hz: a zero/negative
    ///        rate would make the phase increment non-finite, and advancePhase's
    ///        cycle-wrap loop would then never terminate)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        outputSmoother_.configure(kOutputSmoothMs, static_cast<float>(sampleRate_));
        initState();
    }

    /// @brief Rewind to the exact post-prepare state (RNG included).
    /// Keeps the configured sample rate; the same seed re-renders identically.
    void reset() noexcept {
        initState();
    }

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// @brief Set the RNG seed (FR-005). Plain non-virtual member:
    /// ModulationSource declares only getCurrentValue()/getSourceRange() as
    /// virtuals (modulation_source.h:37,41).
    /// @param seedValue Seed (0 is substituted by Xorshift32's default)
    void setSeed(std::uint32_t seedValue) noexcept {
        configuredSeed_ = seedValue;
        rng_.seed(seedValue);
    }

    /// @brief Breath rate in Hz, clamped to [kMinRate, kMaxRate] (FR-022).
    void setRate(float hz) noexcept {
        rate_ = std::clamp(hz, kMinRate, kMaxRate);
        updatePhaseIncrement();
    }

    /// @brief Output depth (FR-023). Scales the signal INSIDE the fixed range.
    /// @param normalized 0..1
    void setDepth(float normalized) noexcept {
        depth_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    /// @brief Cycle-to-cycle period jitter (FR-024). Takes effect at the next
    /// phase wrap; 0 means an exactly constant period.
    /// @param normalized 0..1
    void setIrregularity(float normalized) noexcept {
        irregularity_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    [[nodiscard]] float getRate() const noexcept { return rate_; }
    [[nodiscard]] float getDepth() const noexcept { return depth_; }
    [[nodiscard]] float getIrregularity() const noexcept { return irregularity_; }

    // -------------------------------------------------------------------------
    // Advance
    // -------------------------------------------------------------------------

    /// @brief Advance one sample.
    void process() noexcept {
        advancePhase(1.0);
        outputSmoother_.setTarget(shapeOutput());
        // OnePoleSmoother::process() is [[nodiscard]] (smoother.h:197); discard
        // exactly as RandomSource does (random_source.h:110) so the zero-warning
        // gate stays clean.
        static_cast<void>(outputSmoother_.process());
    }

    /// @brief Advance a whole block at control rate (FR-003).
    /// processBlock(0) is a no-op.
    /// @param numSamples Number of audio samples in this block
    void processBlock(size_t numSamples) noexcept {
        if (numSamples == 0) {
            return;
        }
        advancePhase(static_cast<double>(numSamples));
        outputSmoother_.setTarget(shapeOutput());
        outputSmoother_.advanceSamples(numSamples);
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

private:
    static constexpr double kPi = 3.14159265358979323846;

    void initState() noexcept {
        rng_.seed(configuredSeed_);
        phase_ = 0.0;
        cycleJitter_ = 1.0f;
        updatePhaseIncrement();
        outputSmoother_.snapTo(shapeOutput());
    }

    void updatePhaseIncrement() noexcept {
        phaseInc_ = (static_cast<double>(rate_) * static_cast<double>(cycleJitter_)) /
                    sampleRate_;
    }

    /// @brief Advance the phase by `numSamples` samples, drawing a fresh jitter
    /// factor at every cycle wrap. The loop (rather than a single subtraction)
    /// keeps arbitrarily large block advances correct. Its trip count is
    /// bounded by numSamples * kMaxRate * (1 + kJitterSpan) / sampleRate, which
    /// the 1 Hz prepare() floor keeps finite (< 1 per sample even there).
    void advancePhase(double numSamples) noexcept {
        phase_ += phaseInc_ * numSamples;
        while (phase_ >= 1.0) {
            phase_ -= 1.0;
            drawCycleJitter();
        }
    }

    void drawCycleJitter() noexcept {
        float jitter = 1.0f;
        if (irregularity_ > 0.0f) {
            jitter = 1.0f + (irregularity_ * kJitterSpan * rng_.nextFloat());
            jitter = std::max(jitter, kMinJitter);
        }
        cycleJitter_ = jitter;
        updatePhaseIncrement();
    }

    /// @brief Evaluate the hardcoded breath shape at the current phase and map
    /// it to the bipolar, depth-scaled output.
    [[nodiscard]] float shapeOutput() const noexcept {
        const double p = std::clamp(phase_, 0.0, 1.0);
        const double inhale = static_cast<double>(kInhaleFraction);

        double y = 0.0;
        if (p < inhale) {
            const double u = std::clamp(p / inhale, 0.0, 1.0);
            y = 0.5 * (1.0 - std::cos(kPi * std::pow(u, static_cast<double>(kInhaleExponent))));
        } else {
            const double v = std::clamp((p - inhale) / (1.0 - inhale), 0.0, 1.0);
            y = 0.5 * (1.0 + std::cos(kPi * std::pow(v, static_cast<double>(kExhaleExponent))));
        }

        const float bipolar = static_cast<float>((2.0 * y) - 1.0);
        return std::clamp(depth_ * bipolar, -1.0f, 1.0f);
    }

    double sampleRate_ = 44100.0;
    double phase_ = 0.0;
    double phaseInc_ = static_cast<double>(kDefaultRate) / 44100.0;

    float rate_ = kDefaultRate;
    float depth_ = kDefaultDepth;
    float irregularity_ = kDefaultIrregularity;
    float cycleJitter_ = 1.0f;

    std::uint32_t configuredSeed_ = kDefaultBreathSeed;
    Xorshift32 rng_{kDefaultBreathSeed};
    OnePoleSmoother outputSmoother_;
};

}  // namespace DSP
}  // namespace Krate
