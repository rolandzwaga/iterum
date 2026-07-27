#pragma once

// ==============================================================================
// Layer 3: Systems
// spectral_morph_engine.h - SpectralMorphEngine, the Seraphis travel engine
// ==============================================================================
// Spec: specs/seraphis-phase3-spectral-morph/spec.md  (FR-003 - FR-008, FR-041 -
//                                                      FR-047, FR-051, FR-052,
//                                                      FR-061 - FR-063, FR-070)
// Plan: specs/seraphis-phase3-spectral-morph/plan.md  (sections 5.1 - 5.7)
//
// Holds 2-4 SpectralState slots and TRAVELS between them along a scalar journey
// coordinate p in [0, numStates-1], rather than cross-fading them as one rigid
// object:
//
//   - the travel COORDINATE is driven by an owned SplineTrajectory or by an
//     externally supplied slow ramp, through one shared slew limiter (FR-061);
//   - the PER-PARTIAL completion is staggered by the bloom law, so low partials
//     arrive before high ones (FR-051);
//   - the result is perturbed by an owned EntropyProcessor as the LAST stage
//     (FR-070), and published through zero-copy pointer accessors (FR-008).
//
// It deliberately does NOT include systems/harmonic_cloud.h (FR-003, Non-Goals):
// the engine-to-cloud wiring is Phase 7's, and no production component in this
// phase includes both headers. That is a lint-visible property of this file.
//
// Constitution compliance:
// - Principle II (Real-Time Safety): every method noexcept; no allocation, no
//   locks, no exceptions, no I/O; all state is fixed-size member storage.
//   prepare() is the ONE method permitted configuration-rate work and is NOT
//   RT-safe by contract. setState(), setSeed() and reset() are RT-safe in the
//   allocation-free sense but are CONFIGURATION-TIME calls (see below).
// - Principle IX (Layers): Layer 3 -- includes Layer 0 and Layer 2 only.
//
// std::isnan / std::isinf are NOT usable here: the macOS leg builds -ffast-math,
// under which they are optimised away. detail::isNaN / detail::isInf
// (core/db_utils.h:54, :175) are the portable bit-pattern form.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/entropy_processor.h>
#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/processors/spline_trajectory.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Suppress MSVC C4324: structure was padded due to alignment specifier.
// Same idiom as harmonic_cloud.h:31-37 - the alignas(32) on the per-partial
// arrays is a deliberate locality choice, not an alignment assumption.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

namespace Krate::DSP {

/// @brief Travel between 2-4 SpectralStates under a bloom-staggered completion
/// law, with an owned EntropyProcessor applied last.
///
/// @par FR-086 — composition cadence
/// Reproduced verbatim in HarmonicCloud::setSpectralTarget's doc comment; this
/// header still does NOT include harmonic_cloud.h (FR-003, Non-Goals), so the
/// line references below are to that file.
/// @code
/// // A consumer driving HarmonicCloud from a SpectralMorphEngine MUST do so in slices of
/// // <= HarmonicCloud::kControlChunkSamples (= 64) samples, in this order:
/// //
/// //   for (each slice of <= 64 samples) {
/// //       engine.updateChunk(n);
/// //       cloud.setSpectralTarget(engine.getOutputRatios(),
/// //                               engine.getOutputAmplitudes(),
/// //                               engine.getOutputCount());
/// //       cloud.processStereoBlock(left + offset, right + offset, n);
/// //   }
/// //
/// // WHY A BOUND AND NOT A SUGGESTION: processStereoBlock restarts its internal 64-sample
/// // control grid on every call (harmonic_cloud.h:713-716) and setSpectralTarget only raises
/// // freqDirty_/ampDirty_, consumed at the head of the FIRST updateControl of that call
/// // (:1313-1321). A target supplied once per 512-sample host block is therefore frozen for
/// // all 8 internal chunks and the morph's effective resolution silently becomes the host
/// // block size.
/// @endcode
class SpectralMorphEngine {
public:
    // -------------------------------------------------------------------------
    // Section 5.1 constants
    // -------------------------------------------------------------------------

    static constexpr int kMinStates = 2;
    static constexpr int kMaxStates = 4;
    static constexpr std::size_t kStatePartials = SpectralState::kStatePartials; // 64

    static constexpr float kMaxBloomFraction = 0.6f;
    static constexpr float kMinTravelRate = 1.0f / 600.0f; ///< journeys per second
    static constexpr float kMaxTravelRate = 1.0f;
    static constexpr float kStateChangeFadeSec = 2.0f;

    // FR-041 fill
    static constexpr float kMaxFillGrowth = 2.0f;
    static constexpr float kMaxFillRatio = SpectralState::kMaxStateRatio; // 128
    static constexpr float kFillSpacingCents = 28.0f;
    static constexpr float kFillSpacingLog2 = kFillSpacingCents / 1200.0f;
    /// exp2(28 / 1200) == 1.0163049. detail::constexprExp is used because
    /// std::exp2 is not constexpr in C++20; the value is pinned by a test.
    static constexpr float kFillSpacingFactor =
        detail::constexprExp(kFillSpacingLog2 * detail::kLn2);

    /// FR-046 (aliases of the SINGLE definition, which EntropyProcessor owns).
    static constexpr float kMinRatioSpacingCents = EntropyProcessor::kMinRatioSpacingCents;
    static constexpr float kMinRatioSpacingLog2 = EntropyProcessor::kMinRatioSpacingLog2;

    /// Worst-case ratio the FR-041 fill can emit.
    ///
    /// kStatePartials (64), NOT kStatePartials - 1 (63): the worst case needs 63
    /// chained float multiplies to REACH the ceiling, so a mathematically tight
    /// ceiling can be missed by accumulated rounding (deviation D13). The 1.6 %
    /// of headroom this buys is what the ratios[0] = 128 corner spends.
    static constexpr float kMaxOutputRatio =
        kMaxFillRatio
        * detail::constexprExp(static_cast<float>(kStatePartials) * kFillSpacingLog2 * detail::kLn2);

    /// Cent span of the whole reachable output ratio range, 11392.0 cents.
    static constexpr float kOutputCentsSpan =
        1200.0f * detail::constexprLn(kMaxOutputRatio / SpectralState::kMinStateRatio) / detail::kLn2;

    static constexpr float kMaxAmpDeltaPerChunk = 0.025f;
    static constexpr float kMaxRatioDeltaCentsPerChunk = 125.0f;

    /// Which driver moves the travel coordinate. NESTED (deviation D8): the name
    /// `TravelMode` is generic enough that a namespace-scope enum would be an ODR
    /// and confusion hazard.
    enum class TravelMode : std::uint8_t { External = 0, Spline };

    /// Default RNG base seed, matching Xorshift32's own default (core/random.h:44).
    static constexpr std::uint32_t kDefaultMorphSeed = 1u;
    /// Distinct base salts so the two sub-components' streams cannot correlate.
    static constexpr std::size_t kEntropyBaseSalt = 0x1000;
    static constexpr std::size_t kSplineBaseSalt = 0x1001;

    // -------------------------------------------------------------------------
    // FR-044 contributor table -- a static_assert, NOT a comment.
    //
    // Every per-chunk amplitude and cent contributor is enumerated and summed
    // against the two published bounds at COMPILE TIME, with T = 64/48000 and
    // R = kMaxTravelRate * (kMaxStates - 1) = 3.0 journeys/s.
    //
    // If the amplitude assert ever fails to compile, the bug is
    // EntropyProcessor::kEntropyAmpSmoothMs, NOT the bound.
    // kMaxAmpDeltaPerChunk is NEVER raised: the spec defines it as the sum of
    // the enumerated contributors, so a contributor that grew is the defect.
    // -------------------------------------------------------------------------

    static constexpr float kFr044SampleRate = 48000.0f;
    static constexpr float kFr044ChunkSamples = 64.0f;
    static constexpr float kFr044ChunkSeconds = kFr044ChunkSamples / kFr044SampleRate;

    static constexpr float kFr044TravelAmp = (kMaxTravelRate * static_cast<float>(kMaxStates - 1))
                                             * kFr044ChunkSeconds / (1.0f - kMaxBloomFraction);
    static constexpr float kFr044StateAmp = kFr044ChunkSeconds / kStateChangeFadeSec;
    static constexpr float kFr044DeathAmp = kFr044ChunkSeconds / EntropyProcessor::kMinDeathFadeSec;

    // THE SMOOTHER'S OWN CONVENTION, ROUTED THROUGH THE SHARED HELPER SO IT
    // CANNOT DRIFT AGAIN: OnePoleSmoother's parameter is time-to-99 % and
    // coeff = exp(-5000/(ms*fs)) (primitives/smoother.h:91), so tau = ms/5000 s.
    // Writing `1 - exp(-T / (ms * 0.001))` treats ms as a time constant and
    // understates the per-chunk step by a factor of 4.91 at 150 ms (D12/D14).
    static constexpr float kFr044AmpOuStep = EntropyProcessor::onePoleChunkStep(
        EntropyProcessor::kEntropyAmpSmoothMs, kFr044ChunkSamples, kFr044SampleRate); // 8.8495e-3
    static constexpr float kFr044CentsOuStep = EntropyProcessor::onePoleChunkStep(
        EntropyProcessor::kEntropyCentsSmoothMs, kFr044ChunkSamples, kFr044SampleRate); // 4.3471e-2

    static constexpr float kFr044JitterAmp = EntropyProcessor::kMaxAmpJitter * 2.0f * kFr044AmpOuStep;

    static_assert(kFr044TravelAmp + kFr044StateAmp + kFr044DeathAmp + kFr044JitterAmp
                      <= kMaxAmpDeltaPerChunk,
                  "FR-044 amplitude budget");
    static_assert((kFr044TravelAmp + kFr044StateAmp) * kOutputCentsSpan
                          + EntropyProcessor::kMaxDecoherenceCents * 2.0f * kFr044CentsOuStep
                      <= kMaxRatioDeltaCentsPerChunk,
                  "FR-044 cents budget");

    /// The decoherence bank deliberately keeps BrownianDrift's OWN 150 ms so it
    /// stays bit-comparable to a stock BrownianDrift. Named here so the
    /// brownian_drift.h include is load-bearing rather than decorative: the two
    /// cent terms of the assert above are derived through it.
    static_assert(EntropyProcessor::kEntropyCentsSmoothMs == BrownianDrift::kDriftOutputSmoothMs,
                  "FR-044 cents budget is derived from BrownianDrift's own output smoothing time");

    // -------------------------------------------------------------------------
    // Lifecycle
    //
    // CONFIGURATION-TIME CALLS: prepare(), reset(), setSeed(), setState() and
    // setStateCount() are NOT to be called while the consumer is sounding.
    // reset() rewinds the travel position and every RNG stream; setSeed()
    // redraws all 64 scatter offsets (a step of up to 2 * kMaxScatterCents = 14
    // cents per partial in one chunk). They are named exemptions in FR-044's
    // continuity list. prepare() in particular is NOT RT-safe by contract, even
    // though it is declared noexcept and allocates nothing.
    // -------------------------------------------------------------------------

    /// @brief Load all four slots with makeFactoryState(SineStack) (FR-005).
    ///
    /// Not the default-constructed SpectralState, which is SILENT
    /// (numPartials == 0) and would make a forgotten setState an invisible mute
    /// with no criterion failing. All four slots identical also makes the
    /// default configuration the well-trodden "perfectly static output" corner.
    SpectralMorphEngine() noexcept {
        const SpectralState sine = makeFactoryState(SpectralStateId::SineStack);
        for (int slot = 0; slot < kMaxStates; ++slot) {
            loadSlot(static_cast<std::size_t>(slot), sine);
        }
        numStates_ = kMinStates;
        bloom_ = 0.0f;
        invCompletionPoint_.fill(1.0f);
        mode_ = TravelMode::External;
        travelRate_ = kMinTravelRate;
        entropy_.setEntropy(0.0f);
        reset();
    }

    /// @brief Re-derive every sample-rate-dependent coefficient and rewind.
    /// @param sampleRate Sample rate in Hz, floored at 1 Hz -- matching
    ///        brownian_drift.h:122 and spline_trajectory.h:137.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        invSampleRate_ = static_cast<float>(1.0 / sampleRate_);
        spline_.prepare(sampleRate_);
        entropy_.prepare(sampleRate_);
        reset();
        prepared_ = true;
    }

    /// @brief Rewind the travel and stochastic state to the post-prepare point.
    ///
    /// DEVIATION D15 -- reset() REWINDS, IT DOES NOT RECONFIGURE. The configured
    /// slots, numStates, bloom, travel mode, travel rate and entropy are left
    /// exactly as the caller set them, matching BrownianDrift::reset()
    /// (brownian_drift.h:133-135), which likewise keeps smoothness and depth.
    /// FR-005's default TABLE is scoped to "after default construction and after
    /// prepare() with no parameter call" -- the constructor performs that load.
    /// A wiping reset() would erase the patch on every Phase 7 voice allocation.
    void reset() noexcept {
        position_ = 0.0f;
        targetPosition_ = 0.0f;
        fadeX_ = 1.0f;
        repairCount_ = 0;
        limiterActiveChunks_ = 0;
        totalChunks_ = 0;
        departLogRatio_.fill(0.0f);
        departAmp_.fill(0.0f);
        spline_.setSeed(deriveStreamSeed(configuredSeed_, kSplineBaseSalt));
        spline_.reset();
        entropy_.setSeed(deriveStreamSeed(configuredSeed_, kEntropyBaseSalt));
        entropy_.reset();
        refreshOutputs();
    }

    /// @brief Set the base RNG seed (FR-006). CONFIGURATION-TIME.
    void setSeed(std::uint32_t seed) noexcept {
        configuredSeed_ = seed;
        spline_.setSeed(deriveStreamSeed(configuredSeed_, kSplineBaseSalt));
        entropy_.setSeed(deriveStreamSeed(configuredSeed_, kEntropyBaseSalt));
        refreshOutputs();
    }

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// @brief Assign a spectral identity to one slot (FR-042).
    ///
    /// Returns immediately, writing NOTHING, if `slot` is outside
    /// [0, kMaxStates) or `!isValidSpectralState(s)`. This is a STRICTER
    /// rejection set than HarmonicCloud::setSpectralTarget's, on purpose
    /// (FR-012 vs FR-081): it enforces amplitude <= 1, ratios inside
    /// [kMinStateRatio, kMaxStateRatio] and strict monotonicity.
    ///
    /// On acceptance the slot is stored SANITIZED (deviation D10): only the
    /// FR-041-filled log2(ratio) array, the amplitude array zeroed at
    /// i >= numPartials, and the count. tiltDbPerOct, inharmonicity and name are
    /// structurally incapable of reaching the audio path (FR-013).
    ///
    /// Assigning an IDENTICAL state is a no-op. Otherwise, if the slot currently
    /// contributes to the output, the FR-047 absorption fade is armed.
    void setState(int slot, const SpectralState& s) noexcept {
        if (slot < 0 || slot >= kMaxStates) {
            return;
        }
        if (!isValidSpectralState(s)) {
            return;
        }

        const auto index = static_cast<std::size_t>(slot);
        buildSanitized(s, scratchLog2_, scratchAmp_);
        if (slotNumPartials_[index] == s.numPartials && scratchLog2_ == slotLog2Ratio_[index]
            && scratchAmp_ == slotAmp_[index]) {
            return; // Identical -- no fade armed, isStateFadeActive() untouched.
        }

        slotLog2Ratio_[index] = scratchLog2_;
        slotAmp_[index] = scratchAmp_;
        slotNumPartials_[index] = s.numPartials;

        if (slotContributes(slot)) {
            armStateFade();
        }
    }

    /// @brief Set how many slots the journey spans, clamped to [2, 4] (FR-042).
    /// A call that does not change the count is a no-op.
    void setStateCount(int n) noexcept {
        const int clamped = std::clamp(n, kMinStates, kMaxStates);
        if (clamped == numStates_) {
            return;
        }
        numStates_ = clamped;
        const float ceiling = static_cast<float>(numStates_ - 1);
        position_ = std::clamp(position_, 0.0f, ceiling);
        targetPosition_ = std::clamp(targetPosition_, 0.0f, ceiling);
        armStateFade();
    }

    /// @brief Set the bloom amount, clamped to [0, 1] (FR-051).
    /// Non-finite input is REJECTED outright, leaving the previous value intact.
    void setBloom(float bloom) noexcept {
        if (detail::isNaN(bloom) || detail::isInf(bloom)) {
            return;
        }
        bloom_ = std::clamp(bloom, 0.0f, 1.0f);
        recomputeCompletionPoints();
    }

    /// @brief Forward the single entropy control to the owned processor (FR-070).
    void setEntropy(float e) noexcept { entropy_.setEntropy(e); }

    /// @brief Choose the travel driver (FR-062). The shared slew limiter's
    /// TARGET changes; its output does not, so the switch costs no continuity.
    void setTravelMode(TravelMode mode) noexcept { mode_ = mode; }

    /// @brief Set the External-mode travel target, clamped to [0, numStates-1].
    void setTargetPosition(float p) noexcept {
        if (detail::isNaN(p) || detail::isInf(p)) {
            return;
        }
        targetPosition_ = std::clamp(p, 0.0f, static_cast<float>(numStates_ - 1));
    }

    /// @brief Set the journey rate in journeys per second (FR-061), clamped to
    /// [kMinTravelRate, kMaxTravelRate]. The shared slew cap is
    /// `rate * (numStates - 1)` units/s, i.e. a JOURNEY-FRACTION rate.
    void setTravelRate(float journeysPerSecond) noexcept {
        if (detail::isNaN(journeysPerSecond) || detail::isInf(journeysPerSecond)) {
            return;
        }
        travelRate_ = std::clamp(journeysPerSecond, kMinTravelRate, kMaxTravelRate);
    }

    /// @brief Waypoint spacing of the owned SplineTrajectory, in seconds
    /// (spline_trajectory.h:165, which clamps to [kMinInterval, kMaxInterval]).
    /// CONFIGURATION-TIME. Changing it only changes the playhead speed, so the
    /// spline output stays continuous (spline_trajectory.h:161-163) and the
    /// shared slew limiter absorbs the change in target rate.
    ///
    /// EXPOSED FOR FR-063, WHICH IS OTHERWISE UNREACHABLE FROM THIS SURFACE.
    /// The waypoint-rotation path (spline_trajectory.h:262-269) only runs when a
    /// chunk is longer than one waypoint interval, and at the 2.0 s default
    /// (spline_trajectory.h:123) NO chunk length in SC-013's grid gets there --
    /// 65536 samples is 1.365 s at 48 kHz. At kMinInterval = 0.5 s the same chunk
    /// rotates 2.7 waypoints per call, which is what the criterion measures.
    ///
    /// NON-FINITE INPUT IS REJECTED HERE, not downstream (FR-007, FR-063).
    /// SplineTrajectory::setWaypointInterval clamps with std::clamp
    /// (spline_trajectory.h:166-169), and std::clamp returns a NaN unchanged --
    /// the NaN then reaches the playhead increment, getCurrentValue() and, through
    /// advanceTravel, position_, where the clamp at :699 cannot rescue it either.
    /// This is the FIRST point at which the value can be refused, so it is refused
    /// here. Rejecting leaves the previous interval bit-for-bit intact.
    void setWaypointInterval(double seconds) noexcept {
        if (!isFiniteDouble(seconds)) {
            return;
        }
        spline_.setWaypointInterval(seconds);
    }

    [[nodiscard]] double getWaypointInterval() const noexcept {
        return spline_.getWaypointInterval();
    }

    // -------------------------------------------------------------------------
    // Advance (FR-043)
    // -------------------------------------------------------------------------

    /// @brief Advance the journey by `numSamples` and rewrite the output arrays.
    ///
    /// The ONLY writer of the output arrays. numSamples == 0 is a no-op and
    /// leaves the state UNADVANCED (Edge Cases) -- use prepare()/reset(), which
    /// populate the arrays with no advance, for the post-configuration refresh.
    void updateChunk(std::size_t numSamples) noexcept {
        if (numSamples == 0) {
            return;
        }
        advanceTravel(numSamples);
        runPipeline(numSamples);
        entropy_.processChunk(outRatio_.data(), outAmp_.data(), outCount_, numSamples);
    }

    // -------------------------------------------------------------------------
    // FR-008 zero-copy output accessors
    //
    // Both pointers address STABLE member storage whose address never changes
    // over the instance's lifetime; only the contents move, and only inside
    // updateChunk(). That is what makes the FR-086 composition copy-free and
    // gives FR-085 lever 1 a stable comparand.
    // -------------------------------------------------------------------------

    [[nodiscard]] const float* getOutputRatios() const noexcept { return outRatio_.data(); }
    [[nodiscard]] const float* getOutputAmplitudes() const noexcept { return outAmp_.data(); }
    [[nodiscard]] std::size_t getOutputCount() const noexcept { return outCount_; }
    [[nodiscard]] const float* getCleanRatios() const noexcept { return cleanRatio_.data(); }
    [[nodiscard]] const float* getCleanAmplitudes() const noexcept { return cleanAmp_.data(); }

    // -------------------------------------------------------------------------
    // FR-008 scalar introspection. Out-of-range indices return a neutral value
    // rather than reading past the array.
    // -------------------------------------------------------------------------

    [[nodiscard]] float getTravelPosition() const noexcept { return position_; }

    [[nodiscard]] float getCompletionFraction(std::size_t i) const noexcept {
        return i < kStatePartials ? completion_[i] : 0.0f;
    }

    [[nodiscard]] float getBloom() const noexcept { return bloom_; }
    [[nodiscard]] float getTravelRate() const noexcept { return travelRate_; }
    [[nodiscard]] TravelMode getTravelMode() const noexcept { return mode_; }
    [[nodiscard]] int getStateCount() const noexcept { return numStates_; }
    [[nodiscard]] std::uint32_t getRepairEngagementCount() const noexcept { return repairCount_; }
    [[nodiscard]] std::uint64_t getLimiterActiveChunks() const noexcept {
        return limiterActiveChunks_;
    }
    [[nodiscard]] std::uint64_t getTotalChunks() const noexcept { return totalChunks_; }
    [[nodiscard]] bool isStateFadeActive() const noexcept { return fadeX_ < 1.0f; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief The owned processor, so SC-005/SC-006 reads need no forwarders.
    [[nodiscard]] const EntropyProcessor& entropy() const noexcept { return entropy_; }

    /// @brief True when no internal state value is NaN or Inf.
    [[nodiscard]] bool stateFinite() const noexcept {
        if (!isFiniteValue(position_) || !isFiniteValue(targetPosition_)
            || !isFiniteValue(travelRate_) || !isFiniteValue(bloom_) || !isFiniteValue(fadeX_)) {
            return false;
        }
        for (std::size_t i = 0; i < kStatePartials; ++i) {
            if (!isFiniteValue(outRatio_[i]) || !isFiniteValue(outAmp_[i])
                || !isFiniteValue(cleanRatio_[i]) || !isFiniteValue(cleanAmp_[i])
                || !isFiniteValue(logRatio_[i]) || !isFiniteValue(completion_[i])
                || !isFiniteValue(invCompletionPoint_[i]) || !isFiniteValue(departLogRatio_[i])
                || !isFiniteValue(departAmp_[i])) {
                return false;
            }
        }
        for (std::size_t s = 0; s < static_cast<std::size_t>(kMaxStates); ++s) {
            for (std::size_t i = 0; i < kStatePartials; ++i) {
                if (!isFiniteValue(slotLog2Ratio_[s][i]) || !isFiniteValue(slotAmp_[s][i])) {
                    return false;
                }
            }
        }
        return entropy_.stateFinite();
    }

private:
    using PartialArray = std::array<float, kStatePartials>;

    [[nodiscard]] static bool isFiniteValue(float v) noexcept {
        return !detail::isNaN(v) && !detail::isInf(v);
    }

    /// @brief The same bit-pattern idiom as detail::isNaN / detail::isInf
    ///        (core/db_utils.h:54, :175), at 64-bit width.
    ///
    /// Those helpers are float-only, and there are 800+ call sites passing floats,
    /// so a `double` overload beside them would silently change overload
    /// resolution across the whole library. Narrowing to float first is not an
    /// option either: 1e300 is a legal, finite request for a very long interval
    /// and would become +Inf, turning FR-007's documented CLAMP into a rejection.
    /// A double is non-finite exactly when its 11-bit exponent field is all ones.
    [[nodiscard]] static bool isFiniteDouble(double v) noexcept {
        constexpr std::uint64_t kExponentMask = 0x7FF0000000000000ULL;
        return (std::bit_cast<std::uint64_t>(v) & kExponentMask) != kExponentMask;
    }

    // -------------------------------------------------------------------------
    // Section 5.4 -- the FR-041 fill, and slot sanitisation (deviation D10)
    // -------------------------------------------------------------------------

    /// @brief Build one slot's sanitized arrays from a VALIDATED state.
    ///
    /// Deviation D9: with fewer than two authored ratios the geometric arm has
    /// no spacing to continue, so the rule is `j + 1` for EVERY j -- but the
    /// 28-cent floor still applies, which is what keeps the
    /// `numPartials = 1, ratios[0] = 128` corner monotone instead of emitting
    /// 128, 2, 3, ... Without it FR-046's repair would have to rescue every
    /// chunk of that configuration.
    static void buildSanitized(const SpectralState& s, PartialArray& outLog2,
                               PartialArray& outAmp) noexcept {
        PartialArray r{};
        const auto count = static_cast<std::size_t>(
            std::clamp(s.numPartials, 0, static_cast<int>(kStatePartials)));

        for (std::size_t i = 0; i < count; ++i) {
            r[i] = s.ratios[i];
        }

        for (std::size_t j = count; j < kStatePartials; ++j) {
            float grown = 0.0f;
            if (count >= 2) {
                // j >= 2 always holds here: the loop starts at j = count >= 2.
                const float g = std::clamp(r[j - 1] / r[j - 2], 1.0f, kMaxFillGrowth);
                grown = std::min(r[j - 1] * g, kMaxFillRatio);
            } else {
                grown = static_cast<float>(j + 1);
            }
            const float floorV =
                (j >= 1) ? r[j - 1] * kFillSpacingFactor : static_cast<float>(j + 1);
            r[j] = std::max(grown, floorV);
        }

        for (std::size_t i = 0; i < kStatePartials; ++i) {
            outLog2[i] = std::log2(r[i]);
            // FR-012 does NOT constrain amplitudes at i >= numPartials, so a
            // caller-supplied state may carry garbage there. Zeroing is required,
            // not defensive.
            outAmp[i] = (i < count) ? s.amplitudes[i] : 0.0f;
        }
    }

    /// @brief Constructor-time slot load: no identity check, no fade armed.
    void loadSlot(std::size_t index, const SpectralState& s) noexcept {
        buildSanitized(s, slotLog2Ratio_[index], slotAmp_[index]);
        slotNumPartials_[index] = s.numPartials;
    }

    /// @brief The segment index k = clamp(floor(p), 0, numStates - 1).
    [[nodiscard]] int currentSegment() const noexcept {
        return std::clamp(static_cast<int>(std::floor(position_)), 0, numStates_ - 1);
    }

    /// @brief True when `slot` is one of the two states bracketing the position.
    [[nodiscard]] bool slotContributes(int slot) const noexcept {
        const int k = currentSegment();
        const int b = std::min(k + 1, numStates_ - 1);
        return slot == k || slot == b;
    }

    /// @brief Arm the FR-047 absorption fade from the CURRENT clean arrays.
    ///
    /// A second qualifying call while the fade is still in flight re-snapshots
    /// from the current arrays, so the output stays continuous through any
    /// number of overlapping changes.
    void armStateFade() noexcept {
        departLogRatio_ = logRatio_;
        departAmp_ = cleanAmp_;
        fadeX_ = 0.0f;
    }

    // -------------------------------------------------------------------------
    // Section 5.5 -- the chunk pipeline. Everything stays in log2 until one exp2
    // (deviation D1): the spec states FR-041/046/047 in the linear ratio domain,
    // but log2 is strictly increasing, so max() commutes with it and a convex
    // combination is unchanged -- the same function, one third of the
    // transcendentals.
    // -------------------------------------------------------------------------

    void runPipeline(std::size_t numSamples) noexcept {
        recomputeCompletion();
        interpolate();
        repairSpacing();
        applyAbsorption(numSamples);
        for (std::size_t i = 0; i < kStatePartials; ++i) {
            cleanRatio_[i] = std::exp2(logRatio_[i]);
        }
        outRatio_ = cleanRatio_;
        outAmp_ = cleanAmp_;
    }

    /// @brief Run the pipeline WITHOUT advancing anything, then apply the entropy
    /// stages through FR-075's numSamples == 0 path.
    ///
    /// This is what makes the post-prepare output arrays well defined with no
    /// prior updateChunk (SC-002 clause 5).
    void refreshOutputs() noexcept {
        runPipeline(0);
        entropy_.processChunk(outRatio_.data(), outAmp_.data(), outCount_, 0);
    }

    /// @brief FR-051/FR-052. Recomputed per chunk; the per-partial completion
    /// POINTS are config-rate (setBloom).
    void recomputeCompletion() noexcept {
        const float u = position_ - std::floor(position_);
        for (std::size_t i = 0; i < kStatePartials; ++i) {
            completion_[i] = std::clamp(u * invCompletionPoint_[i], 0.0f, 1.0f);
        }
    }

    /// @brief The bloom law's per-partial completion points, 1 / e_n.
    ///
    /// The bloom == 0 branch is EXPLICIT rather than left to the arithmetic: at
    /// bloom 0 the multiply is by exactly 1.0f, so u_i == u BITWISE, which is a
    /// property of the code rather than of the FP mode (the macOS leg builds
    /// -ffast-math). e_n >= 1 - kMaxBloomFraction = 0.4, never near zero.
    void recomputeCompletionPoints() noexcept {
        if (bloom_ == 0.0f) {
            invCompletionPoint_.fill(1.0f);
            return;
        }
        constexpr float kSpan = static_cast<float>(kStatePartials - 1);
        for (std::size_t i = 0; i < kStatePartials; ++i) {
            const float lead = 1.0f - static_cast<float>(i) / kSpan;
            const float e = 1.0f - bloom_ * kMaxBloomFraction * lead;
            invCompletionPoint_[i] = 1.0f / e;
        }
    }

    /// @brief FR-041. Amplitudes interpolate linearly in magnitude
    /// (spectral_morph_filter.h:601); ratios interpolate geometrically, which in
    /// the log2 domain is the same lerp (clarification C-7).
    void interpolate() noexcept {
        const int k = currentSegment();
        const auto a = static_cast<std::size_t>(k);
        const auto b = static_cast<std::size_t>(std::min(k + 1, numStates_ - 1));

        outCount_ = static_cast<std::size_t>(std::max(slotNumPartials_[a], slotNumPartials_[b]));

        if (a == b) {
            // EXACT-ZERO FAST PATH for the degenerate segment reached at
            // position_ == numStates_ - 1, which is what makes that endpoint
            // well-defined. The branch is on the SLOT INDICES, so it fires only
            // there -- the FR-005 default (all four slots holding the same
            // state, numStates_ = 2) still runs the general path with a != b.
            //
            // A COPY, NOT A LERP, and the difference is observable: in float
            // `x * (1 - u) + x * u` is not bitwise `x`, so running the two-term
            // form here would make the endpoint approximate for no reason and
            // would spend 128 multiplies doing it.
            cleanAmp_ = slotAmp_[a];
            logRatio_ = slotLog2Ratio_[a];
            return;
        }

        for (std::size_t i = 0; i < kStatePartials; ++i) {
            const float uI = completion_[i];
            const float invU = 1.0f - uI;
            cleanAmp_[i] = slotAmp_[a][i] * invU + slotAmp_[b][i] * uI;
            logRatio_[i] = slotLog2Ratio_[a][i] * invU + slotLog2Ratio_[b][i] * uI;
        }
    }

    /// @brief FR-046. The 24-cent adjacent-spacing floor, additive in log2.
    void repairSpacing() noexcept {
        bool changed = false;
        for (std::size_t i = 1; i < kStatePartials; ++i) {
            const float floorLog = logRatio_[i - 1] + kMinRatioSpacingLog2;
            if (logRatio_[i] < floorLog) {
                logRatio_[i] = floorLog;
                changed = true;
            }
        }
        if (changed) {
            ++repairCount_;
        }
    }

    /// @brief FR-047. Ramp from the departed spectrum to the new one over
    /// kStateChangeFadeSec. Advances on chunk SECONDS, so it is sample-rate and
    /// chunk-length independent.
    void applyAbsorption(std::size_t numSamples) noexcept {
        if (fadeX_ >= 1.0f) {
            return; // Nothing in flight.
        }
        const float chunkSeconds = static_cast<float>(numSamples) * invSampleRate_;
        fadeX_ = std::min(1.0f, fadeX_ + chunkSeconds / kStateChangeFadeSec);
        const float inv = 1.0f - fadeX_;
        for (std::size_t i = 0; i < kStatePartials; ++i) {
            logRatio_[i] = departLogRatio_[i] * inv + logRatio_[i] * fadeX_;
            cleanAmp_[i] = departAmp_[i] * inv + cleanAmp_[i] * fadeX_;
        }
    }

    /// @brief FR-061/062/063. ONE position state and ONE slew limiter, shared by
    /// both modes -- which is exactly why a mode switch costs nothing: it changes
    /// the limiter's TARGET, never its output.
    void advanceTravel(std::size_t numSamples) noexcept {
        const float dt = static_cast<float>(numSamples) * invSampleRate_;

        float target = targetPosition_;
        if (mode_ == TravelMode::Spline) {
            spline_.processBlock(numSamples);
            const float s = spline_.getCurrentValue();
            // The kWaypointMax rescale is what makes p = 0 and p = numStates - 1
            // REACHABLE; the clamp is required because uniform Catmull-Rom
            // overshoots its control points (spline_trajectory.h:56-66).
            const float unit =
                std::clamp((s / SplineTrajectory::kWaypointMax + 1.0f) * 0.5f, 0.0f, 1.0f);
            target = unit * static_cast<float>(numStates_ - 1);
        }

        const float cap = travelRate_ * static_cast<float>(numStates_ - 1) * dt;
        const float delta = target - position_;
        ++totalChunks_;
        if (std::abs(delta) > cap) {
            position_ += (delta > 0.0f ? cap : -cap);
            ++limiterActiveChunks_;
        } else {
            position_ = target;
        }
        position_ = std::clamp(position_, 0.0f, static_cast<float>(numStates_ - 1));
    }

    // -------------------------------------------------------------------------
    // Section 5.3 state layout. SANITIZED per-slot arrays, never SpectralState
    // copies (deviation D10).
    // -------------------------------------------------------------------------

    std::array<PartialArray, static_cast<std::size_t>(kMaxStates)> slotLog2Ratio_{};
    std::array<PartialArray, static_cast<std::size_t>(kMaxStates)> slotAmp_{};
    std::array<int, static_cast<std::size_t>(kMaxStates)> slotNumPartials_{};
    int numStates_ = kMinStates;

    // Travel
    float position_ = 0.0f;
    float targetPosition_ = 0.0f;
    float travelRate_ = kMinTravelRate;
    TravelMode mode_ = TravelMode::External;
    SplineTrajectory spline_;
    std::uint64_t limiterActiveChunks_ = 0;
    std::uint64_t totalChunks_ = 0;

    // Bloom
    float bloom_ = 0.0f;
    alignas(32) PartialArray invCompletionPoint_{}; ///< 1 / e_n
    alignas(32) PartialArray completion_{};         ///< u_i (FR-008)

    // Working / output
    alignas(32) PartialArray logRatio_{};   ///< post-repair, post-absorption
    alignas(32) PartialArray cleanRatio_{}; ///< exp2(logRatio_)
    alignas(32) PartialArray cleanAmp_{};
    alignas(32) PartialArray outRatio_{};
    alignas(32) PartialArray outAmp_{};
    std::size_t outCount_ = kStatePartials;

    // FR-047 absorption
    alignas(32) PartialArray departLogRatio_{};
    alignas(32) PartialArray departAmp_{};
    float fadeX_ = 1.0f;

    // FR-042 identical-state detection scratch (configuration rate only)
    alignas(32) PartialArray scratchLog2_{};
    alignas(32) PartialArray scratchAmp_{};

    std::uint32_t repairCount_ = 0;
    EntropyProcessor entropy_;
    double sampleRate_ = 44100.0;
    float invSampleRate_ = 1.0f / 44100.0f;
    std::uint32_t configuredSeed_ = kDefaultMorphSeed;
    bool prepared_ = false;
};

} // namespace Krate::DSP

#ifdef _MSC_VER
#pragma warning(pop)
#endif
