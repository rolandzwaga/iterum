#pragma once

// ==============================================================================
// Layer 2: Processors
// entropy_processor.h - EntropyProcessor, the Seraphis four-stage entropy law
// ==============================================================================
// Spec: specs/seraphis-phase3-spectral-morph/spec.md  (FR-005 - FR-008,
//                                                      FR-071 - FR-075)
// Plan: specs/seraphis-phase3-spectral-morph/plan.md  (sections 4.1 - 4.8)
//
// A spectral TRANSFORM with an array-in / array-out contract: given the ratio
// and amplitude arrays of a morph result, it perturbs them in place by four
// stages that engage in order as the entropy control rises --
//
//   stage 1 (e in [0.00, 0.35])  per-partial amplitude jitter
//   stage 2 (e in [0.25, 0.60])  per-partial phase decoherence (ratio domain)
//   stage 3 (e in [0.50, 0.85])  static per-partial ratio scatter
//   stage 4 (e in [0.75, 1.00])  partial death / rebirth
//
// It deliberately does NOT derive from ModulationSource (which brownian_drift.h
// pulls in): it is not a scalar modulation source and must not appear in
// ModulationEngine's routing surface.
//
// Constitution compliance:
// - Principle II (Real-Time Safety): every method noexcept; no allocation, no
//   locks, no exceptions, no I/O; all state is fixed-size members (~5 KB).
//   prepare() is the ONE method permitted configuration-rate work and is NOT
//   RT-safe by contract.
// - Principle IX (Layers): Layer 2 -- includes Layer 0, Layer 1 and Layer 2
//   siblings only.
//
// std::isnan / std::isinf are NOT usable here: the macOS leg builds -ffast-math,
// under which they are optimised away. detail::isNaN / detail::isInf
// (core/db_utils.h:54, :175) are the portable bit-pattern form.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/spectral_state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

/// @brief Four-stage entropy law over a morph result's ratio/amplitude arrays.
///
/// Array-in / array-out; no audio, no scalar modulation output. Two lane-batched
/// Ornstein-Uhlenbeck banks (amplitude jitter, decoherence) plus a static scatter
/// draw and a per-partial death/rebirth FSM.
class EntropyProcessor {
public:
    /// Per-partial slot count. Equal to SpectralState::kStatePartials (64) by
    /// construction: the engine hands over its own fixed arrays.
    static constexpr std::size_t kPartials = SpectralState::kStatePartials; // 64

    // -------------------------------------------------------------------------
    // FR-071 stage ramps
    // -------------------------------------------------------------------------
    static constexpr float kStage1Lo = 0.00f, kStage1Hi = 0.35f; ///< amplitude jitter
    static constexpr float kStage2Lo = 0.25f, kStage2Hi = 0.60f; ///< phase decoherence
    static constexpr float kStage3Lo = 0.50f, kStage3Hi = 0.85f; ///< ratio scatter
    static constexpr float kStage4Lo = 0.75f, kStage4Hi = 1.00f; ///< death / rebirth

    // -------------------------------------------------------------------------
    // FR-072 magnitudes
    // -------------------------------------------------------------------------
    static constexpr float kMaxAmpJitter = 0.5f;
    static constexpr float kMaxDecoherenceCents = 4.0f;
    static constexpr float kMaxScatterCents = 7.0f;

    /// FR-046 spacing floor. OWNED HERE so FR-074's static_assert and the
    /// engine's repair share exactly one definition (the engine aliases these).
    static constexpr float kMinRatioSpacingCents = 24.0f;
    static constexpr float kMinRatioSpacingLog2 = kMinRatioSpacingCents / 1200.0f; // 0.02
    /// exp2(0.02) == 1.0139595. detail::constexprExp is used because std::exp2 is
    /// not constexpr in C++20; the value is pinned by a runtime equivalence test.
    static constexpr float kMinRatioSpacingFactor =
        detail::constexprExp(kMinRatioSpacingLog2 * detail::kLn2);

    static_assert(2.0f * (kMaxDecoherenceCents + kMaxScatterCents) < kMinRatioSpacingCents,
                  "FR-074: two neighbours must not be able to close the FR-046 spacing floor. "
                  "Any increase to the cent constants must be paid for by raising "
                  "kMinRatioSpacingCents - never by deleting this assert.");

    /// Transcribed from BrownianDrift's PRIVATE section (brownian_drift.h:221
    /// opens private:, kWalkLimit :226, kDenormalFloor :228) - they cannot be
    /// named from here. Same precedent and same values as
    /// HarmonicCloud::kDriftWalkLimit / kDriftDenormalFloor
    /// (systems/harmonic_cloud.h:156-157). brownian_drift.h is NOT modified.
    static constexpr float kWalkLimit = 4.0f;
    static constexpr float kDenormalFloor = 1e-20f;

    /// FR-072 OU bank configuration. tau is expressed through BrownianDrift's OWN
    /// smoothness mapping (tau = lerp(kTauMin, kTauMax, smoothness)) rather than
    /// as a 3.0 / 8.0 literal: writing tau = 8.0f directly gives 7.99985 through
    /// that mapping, so the mapping is the definition and a literal would be a
    /// silently different process. (Since lever 5 the banks step on their own
    /// kEntropyControlInterval, so they are no longer STREAM-comparable with a
    /// stock BrownianDrift -- the coefficients are checked explicitly instead.)
    static constexpr float kAmpJitterSmoothness = 0.09396f;   ///< tau = 3.0 s
    static constexpr float kDecoherenceSmoothness = 0.26174f; ///< tau = 8.0 s

    /// Output smoothing times, in OnePoleSmoother's OWN convention (time to 99 %,
    /// tau = ms/5000 s - primitives/smoother.h:86-91). See deviation D12.
    ///   decoherence: BrownianDrift's own value, kept so the two components share
    ///                one number rather than two that can drift apart.
    ///   amp jitter:  5x that value, i.e. tau = 0.150 s EXACTLY, which is the time
    ///                constant FR-044's amplitude row is derived with. At 150 ms
    ///                the true tau is 0.030 s, the per-chunk step is 4.347e-2 and
    ///                the FR-044 amplitude budget CANNOT be met.
    static constexpr float kEntropyCentsSmoothMs = BrownianDrift::kDriftOutputSmoothMs; // 150
    static constexpr float kEntropyAmpSmoothMs =
        5.0f * BrownianDrift::kDriftOutputSmoothMs; // 750

    /// OU control-step interval, in samples. OWNED HERE, deliberately NOT
    /// BrownianDrift::kControlRateInterval (= 32).
    ///
    /// PLAN SECTION 8 LEVER 5, SPENT. The dominant cost of this class is the two
    /// 64-lane banks' control steps: at 32 samples a 512-sample block runs 16 of
    /// them, each drawing three nextFloat() values on each of 64 lanes -- 6,144
    /// draws per block. At 64 samples that halves, and SC-010 clause 1's
    /// 16,000 ns/block budget needs it.
    ///
    /// IT IS AN EXACT RE-DERIVATION, NOT AN APPROXIMATION: updateBankCoefficients
    /// forms a = exp(-dt/tau) and g = kInternalStd*sqrt(1 - a^2) from THIS dt, so
    /// the AR(1) process has the same tau and the same stationary variance; only
    /// the sampling grid of the walk changed. It costs nothing musically either --
    /// the two taus are 3 s and 8 s, so a 1.33 ms grid at 48 kHz is still ~2,250x
    /// faster than the faster of them.
    ///
    /// CONSEQUENCE, recorded because it is not obvious: a lane is no longer
    /// step-comparable with a stock BrownianDrift, which steps on a 32-sample
    /// grid. EntropyProcessor_OuBankMatchesBrownianDrift's stream-equivalence arm
    /// was REPLACED (never deleted) by an explicit-coefficient check at this dt.
    static constexpr std::size_t kEntropyControlInterval = 64;

    /// Default RNG base seed, matching Xorshift32's own default (core/random.h:44).
    static constexpr std::uint32_t kDefaultEntropySeed = 1u;

    /// @brief Fractional approach of a OnePoleSmoother over `chunkSamples` samples,
    /// in the smoother's own convention (primitives/smoother.h:91).
    ///
    /// Public and constexpr because SpectralMorphEngine's FR-044 static_asserts are
    /// expressed through it and must not re-derive the exponent by hand.
    [[nodiscard]] static constexpr float onePoleChunkStep(float smoothTimeMs, float chunkSamples,
                                                          float sampleRate) noexcept {
        return 1.0f - detail::constexprExp(-5000.0f * chunkSamples / (smoothTimeMs * sampleRate));
    }

    // -------------------------------------------------------------------------
    // FR-073 lifecycle
    // -------------------------------------------------------------------------
    static constexpr float kMaxDeathRatePerSecond = 0.05f;
    static constexpr float kMinDeathFadeSec = 0.5f, kMaxDeathFadeSec = 2.0f;
    static constexpr float kMinDeadDwellSec = 0.2f, kMaxDeadDwellSec = 1.0f;
    static constexpr float kMinRebirthFadeSec = 0.5f, kMaxRebirthFadeSec = 2.0f;

    /// Per-partial lifecycle state (FR-073).
    enum class LifePhase : std::uint8_t { Alive = 0, Dying, Dead, Reborn };

    /// A default-constructed processor is already in the NEUTRAL lifecycle state.
    /// phase_ default-initialises to LifePhase::Alive (enumerator value 0), so
    /// life_ must default to 1.0f or the two disagree - and applyStages()
    /// multiplies amplitudes by life_, so leaving std::array<float>{}'s zero
    /// default in place would silently zero every amplitude handed to a processor
    /// that had not yet been prepare()d. std::array has no fill-value NSDMI form,
    /// hence the constructor body.
    EntropyProcessor() noexcept { life_.fill(1.0f); }

    // -------------------------------------------------------------------------
    // Lifecycle - CONFIGURATION-TIME CALLS (FR-005, FR-006)
    //
    // prepare(), reset() and setSeed() are NOT to be called while the consumer is
    // sounding: they re-seed the streams and rewind the walks, which steps the
    // stage outputs. prepare() in particular is NOT RT-safe by contract (it does
    // configuration-rate transcendental work), even though it is declared
    // noexcept and allocates nothing.
    // -------------------------------------------------------------------------

    /// @brief Derive the per-bank coefficients and rewind all stochastic state.
    /// @param sampleRate Sample rate in Hz, floored at 1 Hz (a zero/negative rate
    ///        would make the control-rate dt non-finite) - matching
    ///        brownian_drift.h:122 and spline_trajectory.h:137.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0;
        invSampleRate_ = static_cast<float>(1.0 / sampleRate_);
        updateBankCoefficients(jitter_, kAmpJitterSmoothness, kEntropyAmpSmoothMs);
        updateBankCoefficients(decohere_, kDecoherenceSmoothness, kEntropyCentsSmoothMs);
        reset();
    }

    /// @brief Rewind every stochastic stream and every lifecycle to the exact
    /// post-prepare state.
    ///
    /// The configured PARAMETERS (entropy, the stage weights, the base seed) are
    /// NOT touched, matching BrownianDrift::reset() (brownian_drift.h:133-135),
    /// which likewise keeps smoothness and depth.
    void reset() noexcept {
        clearDynamicState();
        reseedStreams();
    }

    /// @brief Set the base RNG seed (FR-006).
    ///
    /// Stores the seed and performs the same re-seed + scatter redraw that
    /// reset()'s once-per-seed half performs; the walks and lifecycles are left
    /// where they are, matching BrownianDrift::setSeed (brownian_drift.h:145-148).
    /// @param seed Base seed; every one of the 4 x 64 lane streams is derived from
    ///        it through deriveStreamSeed (core/random.h:102).
    void setSeed(std::uint32_t seed) noexcept {
        configuredSeed_ = seed;
        reseedStreams();
    }

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// @brief Set the entropy control and recompute the four stage weights.
    /// @param e Entropy in [0, 1]; out-of-range values are clamped and NaN/Inf are
    ///        REJECTED outright, leaving the previous value bit-for-bit intact
    ///        (FR-007).
    void setEntropy(float e) noexcept {
        if (detail::isNaN(e) || detail::isInf(e)) {
            return;
        }
        entropy_ = std::clamp(e, 0.0f, 1.0f);
        w1_ = stageWeight(entropy_, kStage1Lo, kStage1Hi);
        w2_ = stageWeight(entropy_, kStage2Lo, kStage2Hi);
        w3_ = stageWeight(entropy_, kStage3Lo, kStage3Hi);
        w4_ = stageWeight(entropy_, kStage4Lo, kStage4Hi);
    }

    // -------------------------------------------------------------------------
    // Advance (FR-075)
    // -------------------------------------------------------------------------

    /// @brief Advance every walk and lifecycle by numSamples, then apply all four
    /// stages IN PLACE.
    ///
    /// A NULL POINTER OR count == 0 IS A WHOLE-CALL NO-OP (FR-075): the stages are
    /// not applied AND nothing is advanced, so a rejected call cannot silently
    /// desynchronize the caller's time base -- the caller re-presents the same
    /// elapsed samples on its next successful call and the walks land exactly
    /// where an uninterrupted run would have put them. The guard is therefore the
    /// FIRST statement, before any advance. `count == 0` is a REACHABLE
    /// configuration: the engine passes max(A.numPartials, B.numPartials) and
    /// FR-012 permits numPartials == 0.
    ///
    /// Given an ACCEPTED call, the advance is unconditional in numSamples and
    /// independent of count: internal lane state after N advanced samples is a
    /// function of N alone, never of how N was partitioned into chunks.
    ///
    /// numSamples == 0 applies the stages WITHOUT advancing anything - which is
    /// what the engine's prepare()/reset() use to populate their output arrays
    /// with no advance (FR-005).
    ///
    /// @param ratios Ratio array, perturbed in place; null makes the call a no-op
    /// @param amplitudes Amplitude array, perturbed in place; null makes the call a no-op
    /// @param count Number of valid entries, clamped to kPartials; 0 makes the call a no-op
    /// @param numSamples Elapsed audio samples since the previous call
    void processChunk(float* ratios, float* amplitudes, std::size_t count,
                      std::size_t numSamples) noexcept {
        // FR-075 rejection, BEFORE the advance: a rejected call leaves every walk,
        // every smoother and every lifecycle exactly where it was.
        if (ratios == nullptr || amplitudes == nullptr || count == 0) {
            return;
        }
        if (numSamples > 0) {
            advanceBank(jitter_, numSamples);
            advanceBank(decohere_, numSamples);
            advanceLifecycles(static_cast<float>(numSamples) * invSampleRate_);
        }
        // EXACT-ZERO FAST PATH, written as an explicit branch rather than left to
        // the arithmetic. At entropy 0 all four stage weights are exactly 0 and
        // every factor is the identity, so applyStages() would spend 64 exp2 calls
        // per chunk to write its input back; and the branch makes the bitwise
        // pass-through a property of the CODE rather than of the FP mode, which
        // matters because the macOS leg builds -ffast-math.
        //
        // Consequence of the FR-073 FSM: this also skips the L_i multiply. That is
        // only observable for a lifecycle still in flight after entropy has been
        // driven to exactly 0 from above 0.75 - deviation D5's "a lifecycle
        // already in flight runs to completion" - and it is bounded by the 5.0 s
        // worst-case lifecycle.
        if (entropy_ == 0.0f) {
            return;
        }
        applyStages(ratios, amplitudes, std::min(count, kPartials));
    }

    // -------------------------------------------------------------------------
    // FR-008 introspection - a public contract, not #ifdef scaffolding.
    // Out-of-range indices return 0.0f / LifePhase::Alive rather than reading past
    // the array.
    // -------------------------------------------------------------------------

    [[nodiscard]] float getEntropy() const noexcept { return entropy_; }

    /// @param stage 1..4; anything else returns 0.0f.
    [[nodiscard]] float getStageWeight(int stage) const noexcept {
        switch (stage) {
        case 1:
            return w1_;
        case 2:
            return w2_;
        case 3:
            return w3_;
        case 4:
            return w4_;
        default:
            return 0.0f;
        }
    }

    /// @brief The stage-1 multiplier applied to amplitude i (neutral value 1.0f).
    [[nodiscard]] float getAmpJitterFactor(std::size_t i) const noexcept {
        if (i >= kPartials) {
            return 0.0f;
        }
        return 1.0f + (w1_ * kMaxAmpJitter * std::clamp(jitter_.smoothCur[i], -1.0f, 1.0f));
    }

    /// @brief The stage-2 cent offset applied to ratio i.
    [[nodiscard]] float getDecoherenceCents(std::size_t i) const noexcept {
        if (i >= kPartials) {
            return 0.0f;
        }
        return w2_ * kMaxDecoherenceCents * std::clamp(decohere_.smoothCur[i], -1.0f, 1.0f);
    }

    /// @brief The stage-3 cent offset actually applied to ratio i (w3 * 7 * s_i).
    [[nodiscard]] float getAppliedScatterCents(std::size_t i) const noexcept {
        if (i >= kPartials) {
            return 0.0f;
        }
        return w3_ * kMaxScatterCents * scatterDraw_[i];
    }

    /// @brief The raw scatter draw s_i in [-1, +1], independent of the stage weight.
    [[nodiscard]] float getRawScatterDraw(std::size_t i) const noexcept {
        if (i >= kPartials) {
            return 0.0f;
        }
        return scatterDraw_[i];
    }

    /// @brief How many times partial i's scatter draw has been redrawn (FR-073).
    [[nodiscard]] std::uint32_t getScatterRedrawCount(std::size_t i) const noexcept {
        if (i >= kPartials) {
            return 0u;
        }
        return scatterRedraws_[i];
    }

    [[nodiscard]] LifePhase getLifePhase(std::size_t i) const noexcept {
        if (i >= kPartials) {
            return LifePhase::Alive;
        }
        return phase_[i];
    }

    /// @brief The stage-4 amplitude factor L_i in [0, 1].
    [[nodiscard]] float getLifeAmplitudeFactor(std::size_t i) const noexcept {
        if (i >= kPartials) {
            return 0.0f;
        }
        return life_[i];
    }

    /// @brief The decoherence bank's smoothed lane output, in the exact form
    /// BrownianDrift publishes its own (`clamp(smoother.getCurrentValue(), -1, +1)`,
    /// brownian_drift.h:212-214).
    ///
    /// Exposed so the lane-batched bank can be gated against a reference
    /// BrownianDrift directly, rather than inferred back through
    /// getDecoherenceCents()'s stage weight (which is 0 for e < 0.25 and would make
    /// the equivalence gate depend on the entropy setting).
    [[nodiscard]] float getDecoherenceLaneValue(std::size_t i) const noexcept {
        if (i >= kPartials) {
            return 0.0f;
        }
        return std::clamp(decohere_.smoothCur[i], -1.0f, 1.0f);
    }

    /// @name OU coefficient introspection
    /// The AR(1) retention/innovation pair each bank derived in prepare(). They are
    /// a function of the class's own smoothness constants and the sample rate only,
    /// and are read by the exact-discretisation gate.
    /// @{
    [[nodiscard]] float getAmpJitterCoefficientA() const noexcept { return jitter_.a; }
    [[nodiscard]] float getAmpJitterCoefficientG() const noexcept { return jitter_.g; }
    [[nodiscard]] float getDecoherenceCoefficientA() const noexcept { return decohere_.a; }
    [[nodiscard]] float getDecoherenceCoefficientG() const noexcept { return decohere_.g; }
    /// @}

    /// @brief True when no internal state value is NaN or Inf.
    [[nodiscard]] bool stateFinite() const noexcept {
        if (!bankFinite(jitter_) || !bankFinite(decohere_)) {
            return false;
        }
        for (std::size_t i = 0; i < kPartials; ++i) {
            if (!isFiniteValue(scatterDraw_[i]) || !isFiniteValue(life_[i]) ||
                !isFiniteValue(phaseTimer_[i]) || !isFiniteValue(phaseLength_[i])) {
                return false;
            }
        }
        return isFiniteValue(entropy_) && isFiniteValue(w1_) && isFiniteValue(w2_) &&
               isFiniteValue(w3_) && isFiniteValue(w4_) && isFiniteValue(invSampleRate_);
    }

private:
    /// Xorshift32's only constructor is explicit (core/random.h:45), which makes
    /// std::array<Xorshift32, N>{} copy-initialisation and therefore ill-formed.
    /// Same workaround as HarmonicCloud::LaneRng (systems/harmonic_cloud.h:919-921).
    struct LaneRng {
        Xorshift32 rng{1};
    };

    /// One lane-batched Ornstein-Uhlenbeck bank; mirrors HarmonicCloud::DriftLanes
    /// (systems/harmonic_cloud.h:923-952).
    struct OuBank {
        alignas(32) std::array<float, kPartials> walk{};      ///< x_i
        alignas(32) std::array<float, kPartials> smoothCur{}; ///< one-pole current
        alignas(32) std::array<float, kPartials> smoothTgt{}; ///< one-pole target
        std::array<LaneRng, kPartials> rng{};
        float a = 0.0f;           ///< AR(1) retention coefficient
        float g = 0.0f;           ///< AR(1) innovation gain
        float smoothCoeff = 0.0f; ///< PER BANK: the two banks smooth at different times
        int samplesUntilControl = 0; ///< SHARED across the bank's 64 lanes
        int cachedPowN = 0;          ///< powf memo (harmonic_cloud.h:950-951)
        float cachedPowValue = 0.0f;
    };

    /// Disjoint salt ranges over one base seed, i.e. the 4 x 64 = 256-salt cross
    /// product SC-012 asserts pairwise distinct and non-zero.
    static constexpr std::size_t kJitterSaltBase = 0;
    static constexpr std::size_t kDecohereSaltBase = kPartials;
    static constexpr std::size_t kScatterSaltBase = 2 * kPartials;
    static constexpr std::size_t kLifeSaltBase = 3 * kPartials;

    /// FR-071: w_k(e) = clamp((e - lo_k) / (hi_k - lo_k), 0, 1). Continuous,
    /// monotone non-decreasing, and exactly 0 at e = 0 for all four stages -
    /// including stage 1, whose interval starts at 0.
    ///
    /// The denominator is divided, never pre-reciprocated: (hi - lo) / (hi - lo)
    /// is exactly 1.0f, while multiplying by a rounded 1/(hi - lo) is not.
    [[nodiscard]] static constexpr float stageWeight(float e, float lo, float hi) noexcept {
        return std::clamp((e - lo) / (hi - lo), 0.0f, 1.0f);
    }

    [[nodiscard]] static bool isFiniteValue(float v) noexcept {
        return !detail::isNaN(v) && !detail::isInf(v);
    }

    [[nodiscard]] static bool bankFinite(const OuBank& bank) noexcept {
        for (std::size_t i = 0; i < kPartials; ++i) {
            if (!isFiniteValue(bank.walk[i]) || !isFiniteValue(bank.smoothCur[i]) ||
                !isFiniteValue(bank.smoothTgt[i])) {
                return false;
            }
        }
        return isFiniteValue(bank.a) && isFiniteValue(bank.g) && isFiniteValue(bank.smoothCoeff);
    }

    static void clearBank(OuBank& bank) noexcept {
        bank.walk.fill(0.0f);
        bank.smoothCur.fill(0.0f);
        bank.smoothTgt.fill(0.0f);
        bank.samplesUntilControl = 0;
        bank.cachedPowN = 0;
        bank.cachedPowValue = 0.0f;
    }

    /// Zero every DYNAMIC value. The per-bank coefficients (a, g, smoothCoeff) are
    /// prepare()-derived and are deliberately left alone, and so are the scatter
    /// draws, which reseedStreams() owns.
    void clearDynamicState() noexcept {
        clearBank(jitter_);
        clearBank(decohere_);
        scatterRedraws_.fill(0u);
        phase_.fill(LifePhase::Alive);
        life_.fill(1.0f);
        phaseTimer_.fill(0.0f);
        phaseLength_.fill(0.0f);
    }

    /// Re-seed all 4 x 64 lane streams from the configured base seed and redraw the
    /// static scatter values in index order (FR-006). The scatter stream is drawn
    /// from a lane-local generator because a scatter value is only redrawn later by
    /// the lifecycle stream (FR-073), which owns its own lane RNG.
    ///
    /// This rewinds each lifeRng_ lane to the head of its stream, so the fixed draw
    /// order documented on advanceLifecycles() is reproduced exactly from here.
    void reseedStreams() noexcept {
        for (std::size_t i = 0; i < kPartials; ++i) {
            jitter_.rng[i].rng.seed(deriveStreamSeed(configuredSeed_, kJitterSaltBase + i));
            decohere_.rng[i].rng.seed(deriveStreamSeed(configuredSeed_, kDecohereSaltBase + i));
            Xorshift32 scatterRng{deriveStreamSeed(configuredSeed_, kScatterSaltBase + i)};
            scatterDraw_[i] = scatterRng.nextFloat();
            lifeRng_[i].rng.seed(deriveStreamSeed(configuredSeed_, kLifeSaltBase + i));
        }
    }

    // =========================================================================
    // The two lane-batched OU banks (FR-072, plan section 4.4)
    //
    // This is the LANE-BATCHED form (HarmonicCloud::DriftLanes,
    // systems/harmonic_cloud.h:923-952), not 128 BrownianDrift objects: the cloud
    // already proved the batched form equivalent, and 128 objects would carry 128
    // OnePoleSmoothers and 128 control counters for no benefit.
    // =========================================================================

    /// @brief Re-derive one bank's AR(1) coefficients and output-smoother
    ///        coefficient from its smoothness and smoothing time.
    ///
    /// EVERY INTERMEDIATE IS double; only the final a / g are narrowed to float.
    /// This is a literal transcription of BrownianDrift::updateCoefficients
    /// (processors/brownian_drift.h:230-240) via its Phase 2 copy
    /// HarmonicCloud::updateDriftCoefficients (systems/harmonic_cloud.h:1513-1525),
    /// INCLUDING the `variance > 0.0` guard.
    ///
    /// The double intermediates are not a stylistic preference. The walk is an
    /// AR(1) recursion, so a coefficient difference is re-applied at every one of
    /// the 90,000 control steps EntropyProcessor_OuBankMatchesBrownianDrift drives;
    /// computing tau/a/g in float would move the coefficients in the last bits and
    /// make that gate a coin flip across MSVC / GCC / Clang. The reason is recorded
    /// verbatim at harmonic_cloud.h:1505-1509.
    ///
    /// tau is DERIVED THROUGH THE SMOOTHNESS MAPPING, never written as a 3.0 / 8.0
    /// literal: that is what keeps a lane bit-comparable against a reference
    /// BrownianDrift configured with the same class constant. Writing tau = 8.0f
    /// gives 7.99985 through the mapping, and the two would diverge in the last
    /// bits.
    ///
    /// @param bank       Bank to configure
    /// @param smoothness Correlation control in [0, 1]; tau = lerp(0.2 s, 30 s)
    /// @param smoothMs   Output-smoother time to 99 % (primitives/smoother.h:86-91)
    void updateBankCoefficients(OuBank& bank, float smoothness, float smoothMs) noexcept {
        const double controlDt = static_cast<double>(kEntropyControlInterval) / sampleRate_;
        const double tau = static_cast<double>(BrownianDrift::kTauMin) +
                           (static_cast<double>(smoothness) *
                            (static_cast<double>(BrownianDrift::kTauMax) -
                             static_cast<double>(BrownianDrift::kTauMin)));
        const double a = std::exp(-controlDt / tau);
        bank.a = static_cast<float>(a);
        const double variance = 1.0 - (a * a);
        bank.g = static_cast<float>(static_cast<double>(BrownianDrift::kInternalStd) *
                                    std::sqrt(variance > 0.0 ? variance : 0.0));
        bank.smoothCoeff = calculateOnePolCoefficient(smoothMs, static_cast<float>(sampleRate_));
    }

    /// @brief One OU control step for every lane of a bank
    ///        (BrownianDrift::advanceControlStep, brownian_drift.h:253-270).
    ///
    /// The three nextFloat() draws are SEQUENCED into named locals: the operands of
    /// `+` are unsequenced in C++, so summing three calls inline would leave the
    /// draw order unspecified - and a lane whose draw order differs from
    /// BrownianDrift's is a DIFFERENT RANDOM STREAM, not a rounding difference.
    ///
    /// EVERY lane steps, not only the ones the current caller has partials for:
    /// FR-075 makes lane state a function of elapsed samples alone.
    static void advanceControlStepAllLanes(OuBank& bank) noexcept {
        for (std::size_t i = 0; i < kPartials; ++i) {
            const float z0 = bank.rng[i].rng.nextFloat();
            const float z1 = bank.rng[i].rng.nextFloat();
            const float z2 = bank.rng[i].rng.nextFloat();
            const float z = z0 + z1 + z2; // Irwin-Hall: zero-mean, unit-variance

            // BrownianDrift's mean_ is 0 for both banks, so its
            // `mean_ + a*(x_ - mean_) + g*z` (:262) reduces to this exactly.
            float x = (bank.a * bank.walk[i]) + (bank.g * z);
            x = std::clamp(x, -kWalkLimit, kWalkLimit);
            if (x < kDenormalFloor && x > -kDenormalFloor) {
                x = 0.0f;
            }
            bank.walk[i] = x;

            // BrownianDrift::outputTarget() is `clamp(depth_ * x_, -1, +1)` (:250);
            // depth is pinned at 1.0 for both banks (FR-072's table), so the
            // multiply is the identity and is not written out.
            bank.smoothTgt[i] = std::clamp(x, -1.0f, 1.0f);
        }
    }

    /// @brief Advance every lane's output one-pole by `numSamples`.
    ///
    /// THIS IS A TRANSCRIPTION OF OnePoleSmoother::advanceSamples
    /// (primitives/smoother.h:243-254), NOT the exponential identity. The naive
    /// `cur = tgt + (cur - tgt) * coeff^N` omits three observable operations:
    ///   1. the isComplete() early RETURN, which leaves current_ UNCHANGED - it
    ///      does not snap (smoother.h:244, :232-234);
    ///   2. detail::flushDenormal (:250);
    ///   3. a post-advance HARD SNAP to target below kCompletionThreshold (:251-253).
    /// (3) is a nonlinear, path-dependent step an order of magnitude larger than the
    /// equivalence gate's tolerance.
    ///
    /// coeff^N is formed by the SAME expression advanceSamples uses -
    /// `std::pow(coefficient_, static_cast<float>(numSamples))` (:248) - and NOT
    /// from a precomputed coeff^k table. That distinction is the equivalence itself:
    /// a `for (k) table[k] = std::pow(coeff, float(k))` loop is unrolled by /O2,
    /// which makes every exponent a compile-time constant, and under /fp:fast (and
    /// -ffast-math on the macOS leg) the compiler strength-reduces the
    /// constant-exponent pow into repeated multiplication - Phase 2 MEASURED 4 ULP
    /// of divergence from that (harmonic_cloud.h:1578-1588).
    ///
    /// It IS memoised on numSamples (OuBank::cachedPowN). That is not the rejected
    /// table: the memo is filled by this same call site with the same runtime
    /// numSamples, so no exponent ever becomes a compile-time constant and the float
    /// served is bit-for-bit the float the uncached form computed. What it removes is
    /// redundancy - the caller only ever advances to a control boundary, so
    /// numSamples is the same value on every call of a block.
    ///
    /// The coefficient is PER BANK: the two banks smooth at different times
    /// (kEntropyCentsSmoothMs = 150 vs kEntropyAmpSmoothMs = 750, deviation D12).
    ///
    /// @param bank       Bank to advance
    /// @param numSamples Samples to advance; bounded by kEntropyControlInterval,
    ///                   because the caller only advances to the next boundary.
    static void advanceSmootherAllLanes(OuBank& bank, int numSamples) noexcept {
        if (numSamples <= 0) { // advanceSamples(0) is a no-op (smoother.h:244)
            return;
        }
        if (bank.cachedPowN != numSamples) {
            bank.cachedPowValue =
                std::pow(bank.smoothCoeff, static_cast<float>(numSamples)); // smoother.h:248
            bank.cachedPowN = numSamples;
        }
        const float coeffN = bank.cachedPowValue;
        for (std::size_t i = 0; i < kPartials; ++i) {
            const float diff0 = bank.smoothCur[i] - bank.smoothTgt[i];
            if (std::abs(diff0) < kCompletionThreshold) {
                continue; // smoother.h:244 - SKIP this lane, do NOT snap it
            }
            bank.smoothCur[i] = bank.smoothTgt[i] + (diff0 * coeffN);      // :247-249
            bank.smoothCur[i] = detail::flushDenormal(bank.smoothCur[i]);  // :250
            if (std::abs(bank.smoothCur[i] - bank.smoothTgt[i]) < kCompletionThreshold) {
                bank.smoothCur[i] = bank.smoothTgt[i]; // :251-253
            }
        }
    }

    /// @brief Advance a bank by `numSamples`, structurally mirroring
    ///        BrownianDrift::processBlock (brownian_drift.h:194-206).
    ///
    /// `samplesUntilControl` is SHARED across the bank's 64 lanes - every lane is
    /// advanced by the same sample counts, so one counter is both sufficient and
    /// correct. It is also what makes the lane state after N advanced samples a
    /// function of N ALONE and never of how N was partitioned into chunks (FR-075).
    static void advanceBank(OuBank& bank, std::size_t numSamples) noexcept {
        auto remaining = static_cast<int>(numSamples);
        while (remaining > 0) {
            if (bank.samplesUntilControl <= 0) {
                bank.samplesUntilControl = static_cast<int>(kEntropyControlInterval);
                advanceControlStepAllLanes(bank);
            }
            const int advance = std::min(remaining, bank.samplesUntilControl);
            bank.samplesUntilControl -= advance;
            remaining -= advance;
            advanceSmootherAllLanes(bank, advance);
        }
    }

    // =========================================================================
    // Stage 4: the death / rebirth FSM (FR-073, plan section 4.7)
    // =========================================================================

    /// @brief Linear interpolation of a lifecycle window length, in seconds.
    ///
    /// Written as `lo + (hi - lo) * t`, the form the plan's FSM is stated in.
    /// Deliberately NOT std::lerp: this is the exact expression the drawn window
    /// lengths are defined by, and a lane's window length feeds the L_i ramp
    /// slope that FR-044's per-chunk bound is measured against.
    [[nodiscard]] static constexpr float lerpSeconds(float lo, float hi, float t) noexcept {
        return lo + ((hi - lo) * t);
    }

    /// @brief Advance every partial's Alive -> Dying -> Dead -> Reborn lifecycle
    ///        by `dt` seconds (FR-073).
    ///
    /// THE RNG DRAW ORDER IS FIXED AND PART OF THE CONTRACT. Per partial, from
    /// its own lifeRng_ stream, exactly in this order:
    ///   1. `nextUnipolar()` - the death coin, drawn on EVERY Alive chunk while
    ///      w4_ > 0 and on no other chunk. It is drawn whether or not the partial
    ///      dies, so the stream position is a function of how many Alive chunks
    ///      elapsed at w4_ > 0, never of the outcome sequence.
    ///   2. `nextUnipolar()` - the Dying window length, drawn ONLY on the chunk a
    ///      death starts.
    ///   3. `nextUnipolar()` - the Dead dwell length, on the chunk that enters Dead.
    ///   4. `nextFloat()`    - the FR-073 scatter redraw, immediately after (3) and
    ///      after life_[i] has been set to EXACTLY 0.0f.
    ///   5. `nextUnipolar()` - the Reborn window length, on the chunk that enters
    ///      Reborn.
    /// reset() and setSeed() re-seed this stream and must reproduce the order
    /// exactly; nothing else may draw from it.
    ///
    /// The redraw at (4) happens AFTER life_[i] is exactly 0.0f, which is what
    /// makes SC-006's "every redraw occurred at L_i == 0.0f, bitwise" true by
    /// construction rather than by timing luck. The dead dwell is at least
    /// kMinDeadDwellSec = 0.2 s (~150 further 64-sample chunks at 48 kHz), so the
    /// redraw is also strictly inside the Dead window in the ordinary sense.
    ///
    /// DEVIATION D5. FR-073 says "at w_4 = 0 every partial is Alive with L_i
    /// exactly 1.0f". Implemented as: w4_ == 0 STARTS NO NEW DEATHS and forces
    /// L_i = 1.0f for partials in Alive (the explicit assignment below, not an
    /// arithmetic consequence - the macOS leg builds -ffast-math); a lifecycle
    /// ALREADY IN FLIGHT RUNS TO COMPLETION, bounded by the 5.0 s worst-case
    /// cycle. A literal force-to-Alive would make setEntropy(0) during a Dead
    /// window a step of L_i from 0 to 1 in a single chunk - 40x
    /// kMaxAmpDeltaPerChunk - and SC-001 clause 1 exercises setEntropy mid-sweep.
    ///
    /// Each chunk takes exactly ONE branch: the phases do not fall through, so a
    /// window shorter than the chunk still costs one chunk per phase. At the
    /// pinned constants the shortest window (kMinDeadDwellSec = 0.2 s) is ~150
    /// chunks, so this is a non-issue at every legal chunk length.
    ///
    /// @param dt Elapsed seconds since the previous call (numSamples / sampleRate)
    void advanceLifecycles(float dt) noexcept {
        for (std::size_t i = 0; i < kPartials; ++i) {
            switch (phase_[i]) {
            case LifePhase::Alive:
                life_[i] = 1.0f; // EXPLICIT assignment, not arithmetic
                if (w4_ > 0.0f) {
                    const float p = w4_ * kMaxDeathRatePerSecond * dt;
                    if (lifeRng_[i].rng.nextUnipolar() < p) {
                        phaseLength_[i] = lerpSeconds(kMinDeathFadeSec, kMaxDeathFadeSec,
                                                      lifeRng_[i].rng.nextUnipolar());
                        phaseTimer_[i] = phaseLength_[i];
                        phase_[i] = LifePhase::Dying;
                    }
                }
                break;

            case LifePhase::Dying:
                phaseTimer_[i] -= dt;
                // Linear 1 -> 0. phaseLength_ is at least kMinDeathFadeSec = 0.5 s
                // by construction, so the division cannot be by zero.
                life_[i] = std::clamp(phaseTimer_[i] / phaseLength_[i], 0.0f, 1.0f);
                if (phaseTimer_[i] <= 0.0f) {
                    life_[i] = 0.0f; // EXACTLY 0, before anything else
                    phaseLength_[i] = lerpSeconds(kMinDeadDwellSec, kMaxDeadDwellSec,
                                                  lifeRng_[i].rng.nextUnipolar());
                    phaseTimer_[i] = phaseLength_[i];
                    phase_[i] = LifePhase::Dead;
                    scatterDraw_[i] = lifeRng_[i].rng.nextFloat(); // FR-073 redraw at L_i == 0
                    ++scatterRedraws_[i];
                }
                break;

            case LifePhase::Dead:
                life_[i] = 0.0f;
                phaseTimer_[i] -= dt;
                if (phaseTimer_[i] <= 0.0f) {
                    phaseLength_[i] = lerpSeconds(kMinRebirthFadeSec, kMaxRebirthFadeSec,
                                                  lifeRng_[i].rng.nextUnipolar());
                    phaseTimer_[i] = phaseLength_[i];
                    phase_[i] = LifePhase::Reborn;
                }
                break;

            case LifePhase::Reborn:
                phaseTimer_[i] -= dt;
                // Linear 0 -> 1.
                life_[i] = std::clamp(1.0f - (phaseTimer_[i] / phaseLength_[i]), 0.0f, 1.0f);
                if (phaseTimer_[i] <= 0.0f) {
                    life_[i] = 1.0f;
                    phase_[i] = LifePhase::Alive;
                }
                break;
            }
        }
    }

    // =========================================================================
    // The stage law (FR-072, plan section 4.6)
    // =========================================================================

    /// @brief Apply all four stages IN PLACE to the first `count` partials, in
    ///        FR-072's fixed order.
    ///
    /// The order is not an implementation detail. Stage 1's jitter multiplies the
    /// interpolated amplitude, stage 4's lifecycle factor then multiplies THAT --
    /// so a dying partial fades the jittered amplitude rather than the jitter
    /// fading a dead one back into audibility. Stage 1's factor is strictly
    /// positive by construction (w1 <= 1 and kMaxAmpJitter = 0.5 < 1, so the
    /// factor lies in [0.5, 1.5]), which is what keeps amplitudes non-negative
    /// without a clamp.
    ///
    /// DEVIATION D3. FR-072 writes stages 2 and 3 as two successive
    /// centsToPitchRatio() multiplications. Summing the two cent terms and
    /// converting ONCE is the same real number (f(x)*f(y) = f(x+y)); it differs
    /// only in float rounding (~1e-7 relative, i.e. ~1.7e-4 cents - four orders
    /// below kTargetRatioEpsilonCents = 0.05), it preserves FR-074's +/-11.0-cent
    /// bound exactly, and it halves the transcendental count in the hottest loop
    /// of the phase. getDecoherenceCents() and getAppliedScatterCents() keep the
    /// two terms separately readable for FR-008.
    ///
    /// @param ratios     Ratio array, at least `count` entries
    /// @param amplitudes Amplitude array, at least `count` entries
    /// @param count      Partial count, already clamped to kPartials by the caller
    void applyStages(float* ratios, float* amplitudes, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            // The banks publish their smoothed output exactly as BrownianDrift
            // does - clamped to [-1, +1] (brownian_drift.h:212-214).
            const float d = std::clamp(jitter_.smoothCur[i], -1.0f, 1.0f);
            const float c = std::clamp(decohere_.smoothCur[i], -1.0f, 1.0f);

            amplitudes[i] *= 1.0f + (w1_ * kMaxAmpJitter * d); // (a) stage 1
            amplitudes[i] *= life_[i];                         // (4) stage 4, L_i

            // (b) stage 2 + (c) stage 3, summed in cents and converted once.
            const float cents = (w2_ * kMaxDecoherenceCents * c) +
                                (w3_ * kMaxScatterCents * scatterDraw_[i]);
            // DEVIATION D4 / plan section 8 lever 4. centsToPitchRatioFast, not
            // centsToPitchRatio: FR-074 bounds this expression at
            // +-(kMaxDecoherenceCents + kMaxScatterCents) = +-11.0 cents, 4.5x
            // inside the polynomial's documented [-50, +50] window, where its
            // MEASURED worst case is 6.15e-08 relative (1.06e-4 cent) -- four
            // orders below kTargetRatioEpsilonCents = 0.05. It replaces an exp2
            // in the hottest loop of the phase, which SC-010 clause 1's budget
            // needs. cents == 0 still returns EXACTLY 1.0f, so the zero-entropy
            // pass-through stays bitwise (the macOS leg builds -ffast-math).
            ratios[i] *= centsToPitchRatioFast(cents);
        }
    }

    OuBank jitter_;   ///< bank (a), tau 3 s, amplitude jitter
    OuBank decohere_; ///< bank (b), tau 8 s, decoherence

    alignas(32) std::array<float, kPartials> scatterDraw_{}; ///< s_i in [-1, +1]
    std::array<std::uint32_t, kPartials> scatterRedraws_{};
    std::array<LaneRng, kPartials> lifeRng_{};
    std::array<LifePhase, kPartials> phase_{};
    alignas(32) std::array<float, kPartials> life_{};        ///< L_i
    alignas(32) std::array<float, kPartials> phaseTimer_{};  ///< seconds remaining
    alignas(32) std::array<float, kPartials> phaseLength_{}; ///< seconds, for the ramp

    float entropy_ = 0.0f;
    float w1_ = 0.0f, w2_ = 0.0f, w3_ = 0.0f, w4_ = 0.0f;
    double sampleRate_ = 44100.0;
    float invSampleRate_ = 1.0f / 44100.0f;
    std::uint32_t configuredSeed_ = kDefaultEntropySeed;
};

} // namespace Krate::DSP
