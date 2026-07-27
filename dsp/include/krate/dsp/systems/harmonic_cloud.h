#pragma once

// ==============================================================================
// Layer 3: Systems - HarmonicCloud
// ==============================================================================
// Seraphis Phase 2: a 64-partial additive cloud in which every partial is an
// individual living entity with its own drift, stereo position and envelope.
//
// Spec: specs/seraphis-phase2-harmonic-cloud/spec.md
// Plan: specs/seraphis-phase2-harmonic-cloud/plan.md
//
// Distinction from Innexus: Innexus *analyses* existing sounds into partials;
// this component *generates* partial worlds from parameters. No analysis
// pipeline, no HarmonicFrame dependency — only the SIMD MCF kernel is shared.
// ==============================================================================

#include <krate/dsp/core/crossfade_utils.h>  // L0 equalPowerGains
#include <krate/dsp/core/db_utils.h>         // L0 detail::isNaN/isInf/flushDenormal
#include <krate/dsp/core/math_constants.h>   // L0 kPi/kTwoPi/kHalfPi
#include <krate/dsp/core/pitch_utils.h>      // L0 semitonesToRatio
#include <krate/dsp/core/random.h>           // L0 Xorshift32
#include <krate/dsp/primitives/smoother.h>   // L1 OnePoleSmoother, calculateOnePolCoefficient
#include <krate/dsp/processors/harmonic_oscillator_bank_simd.h>  // L2 processMcfBatchSIMD

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>  // std::memcmp - FR-085 lever 1's bit-identical array skip

// Suppress MSVC C4324: structure was padded due to alignment specifier.
// Same idiom as harmonic_oscillator_bank.h:45-49 — the alignas(32) on the SoA
// arrays is a deliberate locality choice, not an alignment assumption.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

namespace Krate::DSP {

namespace detail {

/// @brief `log2(n)` for the 1-based partial numbers `n = 1 .. 64` (SC-007).
///
/// `HarmonicCloud`'s three config-rate power laws — FR-081's `pow(n, 1+g*r)`,
/// FR-041's `pow(n, -p)` and FR-061's `pow(10, tiltDb*log2(n)/20)` — all raise the
/// SAME fixed set of 64 integers to a per-recompute exponent, so `log2(n)` is loop
/// invariant across every call the component will ever make. Hoisting it here turns
/// each law into one `exp2` and removes FR-061's per-partial `log2` outright.
///
/// It is a namespace-scope `inline const` rather than a member array so the 64
/// floats are shared by every voice's cloud instead of duplicated per instance,
/// and rather than a function-local static so the reads carry no thread-safe-init
/// guard. Declared before the class because an inline member function body cannot
/// see a namespace-scope name introduced after the class.
inline const std::array<float, 64> kHarmonicCloudLog2N = [] {
    std::array<float, 64> table{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        table[i] = std::log2(static_cast<float>(i + 1));
    }
    return table;
}();

/// @brief FR-033 cents -> frequency multiplier, `2^(cents/1200)`, on the BOUNDED
///        drift domain `|cents| <= HarmonicCloud::kMaxDriftCents` (50).
///
/// SC-007's hot path calls this once per partial per control chunk — 64 x 8 = 512
/// calls per 512-sample block — and `semitonesToRatio` (`pitch_utils.h:25`,
/// `std::pow(2.0f, s/12.0f)`) is a general-domain library call. MEASURED on the dev
/// machine (Windows 11, i9-13900HX, MSVC Release, 2026-07-26) by swapping only this
/// expression, everything else held fixed, best-of-7 x 2000 blocks, static /
/// automated ns per 512-sample block:
///   `semitonesToRatio(cents/100)` : 32,486 / 35,853
///   `std::exp2(cents/1200)`       : 29,879 / 32,878
///   this polynomial               : 28,803 / 33,638
/// (repeat runs of the polynomial: 28,146-29,943 static / 31,049-33,638 automated,
/// so the two faster rows overlap inside the run-to-run spread of the 7 x 2000
/// shape). The polynomial is kept because it is the only one of the three with no
/// library call on the hot path at all, and because the optimisation set it belongs
/// to measures 31,281-32,027 / 33,257-34,184 without it against 25,651-26,810 /
/// 27,383-28,643 with it — same code, same machine, under the 25 x 500 trial shape
/// the perf TU settled on (see harmonic_cloud_perf_test.cpp's trial-shape comment).
///
/// WHY A POLYNOMIAL IS EXACT ENOUGH HERE AND fastExp WAS NOT (`fast_math.h:13`
/// records that general-domain polynomial exps were *slower* than MSVC's): this one
/// is not general-domain. `driftCents_` is clamped to `[0, kMaxDriftCents]`
/// (`setDriftDepthCents`), `driftAmount_[i] <= 1` (FR-022, `reseed()`) and the lane
/// read is clamped to `[-1, +1]`, so `|cents| <= 50` and
/// `|u| = |cents * ln2 / 1200| <= 0.0289` — always. Over that interval the degree-4
/// Taylor series of `e^u` truncates at `u^5/120 <= 1.7e-10` relative, well under
/// float epsilon, so what is left is Horner rounding.
///
/// The bound is not taken on trust — `HarmonicCloud_CentsToRatioMatchesExp2`
/// (`harmonic_cloud_test.cpp`) sweeps the whole `[-50, +50]` cent domain at 20,001
/// points against a DOUBLE-precision `std::exp2(cents/1200)`. MEASURED worst case:
/// **6.15e-08 relative, at 29.25 cents — 1.06e-4 cent of pitch error**, against
/// SC-001's 0.1 cent bound (a ~940x margin). The same case runs the realistic
/// defect — the same polynomial expanded in `cents/1200` with the `ln(2)` base
/// conversion dropped — through the identical measurement and it lands at
/// 1.29e-02, so the tolerance is not vacuous.
/// `cents == 0` returns EXACTLY 1.0f (`u = 0`, so Horner yields 1), which is what
/// leaves SC-001/SC-002/SC-003/SC-004 measuring the undetuned law unchanged.
///
/// @param cents Detune in cents; accurate on `[-50, +50]`, degrading outside it
///
/// PROMOTED TO LAYER 0 (Phase 3 §8 lever 4). The body now lives at
/// `core/pitch_utils.h` as `centsToPitchRatioFast`, so this and Phase 3's entropy
/// stage share ONE definition instead of two copies that can drift apart. This
/// name is kept — it is the identifier the Phase 2 accuracy case, the drift lanes
/// and the whole FR-031 documentation trail refer to — in the same shape as
/// FR-006's `deriveSeed` forward.
[[nodiscard]] inline float centsToDriftRatio(float cents) noexcept {
    return centsToPitchRatioFast(cents);
}

}  // namespace detail

/// @brief A 64-partial additive "harmonic cloud" with per-partial life.
///
/// Renders stereo audio through the shared SIMD Modified-Coupled-Form kernel
/// `processMcfBatchSIMD` (`processors/harmonic_oscillator_bank_simd.h:33-46`)
/// over Seraphis-owned SoA arrays laid out to that kernel's parameter contract.
///
/// @par Layer: 3 (systems/). Dependencies: Layer 0/1/2 + stdlib only.
/// @par Real-Time Safety: everything except prepare() is noexcept, allocation-free, lock-free.
class HarmonicCloud {
public:
    // =========================================================================
    // Constants (plan §1.2) — ALL class-scoped
    // =========================================================================

    /// Fixed partial capacity (FR-012, Clarifications OQ-1).
    /// CLASS-scoped on purpose: `Krate::DSP::kMaxPartials = 96` already exists at
    /// namespace scope in `processors/harmonic_types.h:21`, and a namespace-scope
    /// redeclaration here is a hard redefinition error the moment both headers
    /// land in one translation unit (plan §0.1).
    static constexpr std::size_t kMaxPartials = 64;

    static_assert(detail::kHarmonicCloudLog2N.size() == kMaxPartials,
                  "the shared log2(n) table must cover exactly the partial capacity");

    /// Control-rate chunk length in samples (FR-032, Clarifications Q7).
    static constexpr std::size_t kControlChunkSamples = 64;

    /// Drift lane control-step interval, mirrors `BrownianDrift::kControlRateInterval`
    /// (`processors/brownian_drift.h:105`).
    static constexpr int kDriftControlInterval = 32;

    /// Ornstein-Uhlenbeck time-constant range (`brownian_drift.h:97,99`).
    static constexpr float kDriftTauMin = 0.2f;
    static constexpr float kDriftTauMax = 30.0f;

    /// OU internal standard deviation (`brownian_drift.h:101`).
    static constexpr float kDriftInternalStd = 0.5f;

    /// Drift output one-pole smoothing time (`brownian_drift.h:103`).
    static constexpr float kDriftOutputSmoothMs = 150.0f;

    /// Random-walk hard bound and denormal floor (`brownian_drift.h:226,228`).
    static constexpr float kDriftWalkLimit = 4.0f;
    static constexpr float kDriftDenormalFloor = 1e-20f;

    /// Kernel amplitude smoothing time (FR-014, `harmonic_oscillator_bank.h:84`).
    static constexpr float kAmpSmoothTimeSec = 0.002f;

    /// Anti-alias fade band start as a fraction of Nyquist (FR-015, `…bank.h:87`).
    static constexpr float kAntiAliasFadeStart = 0.8f;

    /// MCF epsilon hard bound (FR-015, `…bank.h:1050`).
    static constexpr float kMaxEpsilon = 1.99f;

    /// Output safety clamp (FR-006, `…bank.h:90`).
    static constexpr float kOutputClamp = 2.0f;

    /// Normalizer target RMS and gain ceiling (FR-017, `…bank.h:94,97`).
    static constexpr float kTargetOscRms = 0.5f;
    static constexpr float kMaxNormGain = 20.0f;

    /// Normalizer gain smoothing time (FR-017).
    static constexpr float kNormGainSmoothMs = 20.0f;

    /// Fundamental range in Hz (FR-013).
    static constexpr float kMinFundamentalHz = 20.0f;
    static constexpr float kMaxFundamentalHz = 4000.0f;

    /// Pitch-jump crossfade length (FR-013, `…bank.h:81`).
    static constexpr float kCrossfadeTimeSec = 0.003f;

    /// Inharmonicity ceiling (FR-052, `additive_oscillator.h:339`).
    static constexpr float kMaxInharmonicity = 0.1f;

    /// Spectral tilt range in dB/octave (FR-062, `spectral_tilt.h:98-101`).
    static constexpr float kMinTiltDbPerOct = -12.0f;
    static constexpr float kMaxTiltDbPerOct = 12.0f;

    /// Spectral-gravity exponent half-range (FR-081): ratio exponent is 1 + kGravityExponentRange*g.
    static constexpr float kGravityExponentRange = 0.1f;

    /// Richness rolloff exponent endpoints (FR-041(b)): p(r) = 3.0 - 2.5*r.
    static constexpr float kRichnessMinExponent = 3.0f;
    static constexpr float kRichnessMaxExponent = 0.5f;

    /// Mutation re-weighting depth at Mutation = 1 (FR-071): w in [0.25, 1.75].
    static constexpr float kMaxMutationDepth = 0.75f;

    /// Fixed smoothness of the mutation drift bank (FR-072) -> tau ~ 15.1 s.
    static constexpr float kMutationSmoothness = 0.5f;

    /// Per-partial drift-amount index exponent (FR-022).
    static constexpr float kDriftIndexExponent = 1.0f;

    /// Documented maximum drift depth in cents (FR-033).
    static constexpr float kMaxDriftCents = 50.0f;

    /// Per-partial envelope time bounds (FR-023). 50 ms is 25x kAmpSmoothTimeSec,
    /// clearing FR-023's ">= 20x the smoother time constant" rule with margin.
    static constexpr float kMinAttackSec = 0.05f;
    static constexpr float kMaxAttackSec = 30.0f;
    static constexpr float kMinDecaySec = 0.05f;
    static constexpr float kMaxDecaySec = 60.0f;

    /// Documented maximum per-partial envelope offset (FR-023).
    static constexpr float kMaxEnvOffsetSec = 2.0f;

    /// Quiescence threshold, -100 dBFS (FR-016).
    static constexpr float kQuiescenceAmplitude = 1.0e-5f;

    /// Tail / masked-partial silence threshold (FR-043, `…bank.h:756`).
    static constexpr float kTailSilenceThreshold = 1.0e-8f;

    /// Default cloud seed. Plan §1.3:185 references this constant but §1.2 does not
    /// tabulate a value; 1u is chosen to match `Xorshift32`'s own default seed
    /// argument (`core/random.h:44`) so an unseeded cloud and an unseeded
    /// `Xorshift32` start from the same documented place.
    static constexpr std::uint32_t kDefaultCloudSeed = 1u;

    /// FR-085 lever 3 — the per-slot "has this partial actually moved?" epsilons
    /// `setSpectralTarget` compares an incoming target against (plan §6.1).
    ///
    /// 0.05 cent is four orders of magnitude below the smallest perturbation
    /// Phase 3 produces and ~2000x below SC-001's 0.1-cent pitch bound, so a slot
    /// the mask leaves undirtied is inaudibly stationary rather than merely
    /// slow-moving.
    ///
    /// DEVIATION D4: FR-085 specifies the compare "in the precomputed log domain",
    /// i.e. `|log2 r_new - log2 r_old| > cents/1200`. It is implemented as the
    /// equivalent RELATIVE test `|r_new - r_old| > r_old * kTargetRatioRelEpsilon`
    /// because a per-slot `log2` per chunk costs strictly more than the `exp2` the
    /// lever exists to save. The two agree to first order over the whole reachable
    /// ratio range.
    ///
    /// `detail::constexprExp` rather than `std::exp2`, which is not constexpr in
    /// C++20 (`core/db_utils.h:123`, `detail::kLn2` at `:67`).
    static constexpr float kTargetRatioEpsilonCents = 0.05f;
    static constexpr float kTargetRatioRelEpsilon =
        detail::constexprExp(kTargetRatioEpsilonCents / 1200.0f * detail::kLn2) - 1.0f;  // 2.887e-5
    static constexpr float kTargetAmpEpsilon = 1e-5f;

    // NOTE: `kCompletionThreshold` is deliberately NOT redeclared here. The drift
    // lanes use the shared namespace-scope `Krate::DSP::kCompletionThreshold`
    // (`primitives/smoother.h:55`) so they snap on exactly the value
    // `OnePoleSmoother::advanceSamples` snaps on (plan §1.2, §4.5).

    // =========================================================================
    // Construction
    // =========================================================================

    HarmonicCloud() noexcept = default;
    HarmonicCloud(const HarmonicCloud&) = delete;
    HarmonicCloud& operator=(const HarmonicCloud&) = delete;
    HarmonicCloud(HarmonicCloud&&) noexcept = default;
    HarmonicCloud& operator=(HarmonicCloud&&) noexcept = default;

    // =========================================================================
    // Lifecycle (FR-003) — plan §5
    // =========================================================================

    /// @brief Re-derive every sample-rate-dependent coefficient and reset.
    /// @param sampleRate Sample rate in Hz (values <= 1 are clamped to 1)
    /// @note NOT real-time safe — the only non-RT method on this class.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 1.0) ? sampleRate : 1.0;
        const float sr = static_cast<float>(sampleRate_);

        nyquist_ = sr * 0.5f;
        invSampleRate_ = 1.0f / sr;
        fadeStart_ = kAntiAliasFadeStart * nyquist_;
        invFadeRange_ = 1.0f / (nyquist_ - fadeStart_);
        ampSmoothCoeff_ = 1.0f - std::exp(-1.0f / (kAmpSmoothTimeSec * sr));  // …bank.h:136
        crossfadeLengthSamples_ =
            std::max(1, static_cast<int>(kCrossfadeTimeSec * sr));            // …bank.h:185
        crossfadeThresholdRatio_ = semitonesToRatio(1.0f);                    // …bank.h:190
        normGain_.configure(kNormGainSmoothMs, sr);

        driftSmoothCoeff_ =
            calculateOnePolCoefficient(kDriftOutputSmoothMs, sr);  // smoother.h:77-93, :163

        // Both banks' AR(1) coefficients depend on the sample rate through
        // dt = kDriftControlInterval / fs, so both are re-derived here — the
        // mutation bank at its FIXED kMutationSmoothness (FR-072), which no setter
        // ever touches, and the detune bank at the current cloud control (FR-035).
        // setDriftSmoothness() early-returns on an unchanged value, so at the
        // default smoothness this is the ONLY place the detune bank is configured.
        updateDriftCoefficients(detuneLanes_, driftSmoothness_);
        updateDriftCoefficients(mutationLanes_, kMutationSmoothness);

        reset();
        prepared_ = true;
    }

    /// @brief Silence all partial state without changing configuration (RT-safe).
    void reset() noexcept {
        currentAmplitude_.fill(0.0f);
        targetAmplitude_.fill(0.0f);
        envValue_.fill(0.0f);
        detuneMultiplier_.fill(1.0f);
        envStage_.fill(kEnvStageIdle);
        gate_ = false;

        resetDriftLanes(detuneLanes_);
        resetDriftLanes(mutationLanes_);

        crossfadeRemaining_ = 0;
        lastOutL_ = 0.0f;
        lastOutR_ = 0.0f;
        crossfadeOldL_ = 0.0f;
        crossfadeOldR_ = 0.0f;
        driftReadCount_ = 0;

        positionOverridden_.fill(false);
        masked_.fill(false);

        reseed();

        // FR-085 lever 3, and this ORDER is load-bearing (plan §6.1). The two
        // recomputes below are called DIRECTLY and UNCONDITIONALLY — they never
        // go through the dirty flags — but with a spectral target active their
        // per-slot guard skips any slot whose mask bit is clear. With the masks
        // left at zero both loops would iterate 64 times and write NOTHING.
        // That is not a corner case: prepare() recomputes nyquist_/invSampleRate_
        // and THEN calls reset(), so a sample-rate change on a target-active
        // cloud would leave every epsilon_[i] derived from the OLD rate — every
        // partial rendering at the wrong pitch, and recalculateAntiAliasing()
        // below computing fade/correction from that stale epsilon.
        // Marking everything dirty here makes a reset() the full recompute it
        // was before the amendment; the two flags are cleared again below.
        markFreqDirty();
        markAmpDirty();

        recalculateFrequencies(false);  // reset(): the orbit is being re-established
        recalculateAmplitudes();

        // reset() has just zeroed every currentAmplitude_, so no FR-043 tail
        // survives: retire the high-water mark recalculateAmplitudes() carried over
        // from the previous configuration instead of paying for it until the first
        // chunk's retireFadedTail() notices.
        tailHighWater_ = activeCount_;
        kernelCount_ = activeCount_;

        recalculatePan();
        recalculateAntiAliasing();
        freqDirty_ = false;
        ampDirty_ = false;

        // A reset render starts at the correct level instead of sliding into it
        // over kNormGainSmoothMs (plan §5). One of exactly TWO places snapTo may
        // appear — the other is the quiescent branch of noteOn(), for the same
        // reason and under the same "nothing is sounding" precondition.
        normGain_.snapTo(currentNormGainTarget());
        gainSmoothed_ = normGain_.getCurrentValue();
    }

    // =========================================================================
    // Pitch (FR-013)
    // =========================================================================

    /// @brief Set the cloud fundamental. Clamped to [20, 4000] Hz.
    /// @note A pitch jump larger than one semitone arms a 3 ms output crossfade
    ///       (FR-013, `…bank.h:388-396` + `:782-788`). Unlike the reference, the
    ///       pre-jump level is snapshotted per channel rather than as a mono
    ///       `(L+R)/2` (plan D3), so the stereo image does not collapse for 3 ms.
    void setFundamentalHz(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            return;
        }
        const float v = std::clamp(hz, kMinFundamentalHz, kMaxFundamentalHz);
        if (v == fundamentalHz_) {
            return;
        }
        // Plan §4.8: the FR-013 pitch-jump crossfade is armed at CALL TIME because it
        // reads lastOutL_/lastOutR_; only the epsilon recompute is deferred to the
        // next chunk boundary.
        const float oldHz = fundamentalHz_;
        const float ratio = (v > oldHz) ? (v / oldHz) : (oldHz / v);
        if (oldHz > 0.0f && ratio > crossfadeThresholdRatio_) {
            crossfadeOldL_ = lastOutL_;
            crossfadeOldR_ = lastOutR_;
            crossfadeRemaining_ = crossfadeLengthSamples_;
        }
        fundamentalHz_ = v;
        markFreqDirty();  // recomputing epsilon must NOT touch sinState_/cosState_
    }

    [[nodiscard]] float getFundamentalHz() const noexcept { return fundamentalHz_; }

    // =========================================================================
    // Five macros
    // =========================================================================

    /// @brief Richness: active partial count plus amplitude rolloff shape. [0, 1] (FR-041).
    void setRichness(float r) noexcept {
        if (detail::isNaN(r) || detail::isInf(r)) {
            return;
        }
        const float v = std::clamp(r, 0.0f, 1.0f);
        if (v == richness_) {
            return;
        }
        richness_ = v;
        markFreqDirty();  // N(r) may move
        markAmpDirty();   // and the rolloff exponent with it
    }

    /// @brief Inharmonicity B in the piano/bell law sqrt(1 + B*n^2). [0, 0.1] (FR-051/052).
    void setInharmonicity(float B) noexcept {
        if (detail::isNaN(B) || detail::isInf(B)) {
            return;
        }
        const float v = std::clamp(B, 0.0f, kMaxInharmonicity);
        if (v == inharmonicity_) {
            return;
        }
        inharmonicity_ = v;
        markFreqDirty();
    }

    /// @brief Spectral tilt in dB/octave. [-12, +12] (FR-061/062).
    void setSpectralTiltDb(float dbPerOct) noexcept {
        if (detail::isNaN(dbPerOct) || detail::isInf(dbPerOct)) {
            return;
        }
        const float v = std::clamp(dbPerOct, kMinTiltDbPerOct, kMaxTiltDbPerOct);
        if (v == tiltDb_) {
            return;
        }
        tiltDb_ = v;
        markAmpDirty();
    }

    /// @brief Mutation: slow random re-weighting of partial amplitudes. [0, 1] (FR-071).
    void setMutation(float m) noexcept {
        if (detail::isNaN(m) || detail::isInf(m)) {
            return;
        }
        const float v = std::clamp(m, 0.0f, 1.0f);
        if (v == mutationAmount_) {
            return;
        }
        // Applied per chunk from the mutation lane bank — no config-rate recompute.
        mutationAmount_ = v;
    }

    /// @brief Spectral gravity: pulls partial ratios toward/away from the harmonic grid. [-1, +1] (FR-081).
    ///
    /// `ratio_g(n) = pow(n, 1 + g * kGravityExponentRange)`: g = 0 is exactly the
    /// integer grid, g > 0 stretches, g < 0 compresses, and `ratio_g(1) == 1` at
    /// every setting so the fundamental never moves. It composes with
    /// Inharmonicity in the fixed FR-083 order — the warp first, the
    /// `sqrt(1 + B*n^2)` stretch second — so no gravity setting cancels a non-zero
    /// B (spec Assumption 4: a recorded deviation from roadmap line 153, not a
    /// defect; re-harmonizing an arbitrary spectrum belongs to Phase 3).
    ///
    /// @note FR-084: this only raises the frequency dirty flag, and the recompute
    ///       it schedules writes `frequencyHz_`/`epsilon_` without touching
    ///       `sinState_`/`cosState_`, so a gravity change is phase-continuous by
    ///       construction — the same mechanism FR-053 relies on.
    void setSpectralGravity(float g) noexcept {
        if (detail::isNaN(g) || detail::isInf(g)) {
            return;
        }
        const float v = std::clamp(g, -1.0f, 1.0f);
        if (v == gravity_) {
            return;
        }
        gravity_ = v;
        markFreqDirty();
    }

    [[nodiscard]] float getRichness() const noexcept { return richness_; }
    [[nodiscard]] float getInharmonicity() const noexcept { return inharmonicity_; }
    [[nodiscard]] float getSpectralTiltDb() const noexcept { return tiltDb_; }
    [[nodiscard]] float getMutation() const noexcept { return mutationAmount_; }
    [[nodiscard]] float getSpectralGravity() const noexcept { return gravity_; }

    // =========================================================================
    // Drift (FR-033 / FR-035)
    // =========================================================================

    /// @brief Per-partial detune drift depth in cents. [0, 50]. Detune bank ONLY (FR-035).
    void setDriftDepthCents(float cents) noexcept {
        if (detail::isNaN(cents) || detail::isInf(cents)) {
            return;
        }
        const float v = std::clamp(cents, 0.0f, kMaxDriftCents);
        if (v == driftCents_) {
            return;
        }
        driftCents_ = v;
    }

    /// @brief Detune drift smoothness. [0, 1]. Detune bank ONLY (FR-035).
    void setDriftSmoothness(float s) noexcept {
        if (detail::isNaN(s) || detail::isInf(s)) {
            return;
        }
        const float v = std::clamp(s, 0.0f, 1.0f);
        if (v == driftSmoothness_) {
            return;
        }
        driftSmoothness_ = v;
        // Plan §4.5: the detune bank's AR(1) coefficients are re-derived from this
        // value; the mutation bank keeps kMutationSmoothness and is untouched.
        updateDriftCoefficients(detuneLanes_, driftSmoothness_);
    }

    [[nodiscard]] float getDriftDepthCents() const noexcept { return driftCents_; }
    [[nodiscard]] float getDriftSmoothness() const noexcept { return driftSmoothness_; }

    // =========================================================================
    // Stereo (FR-021 / FR-093)
    // =========================================================================

    /// @brief Stereo spread of the seeded per-partial position scatter. [0, 1].
    void setStereoSpread(float spread) noexcept {
        if (detail::isNaN(spread) || detail::isInf(spread)) {
            return;
        }
        const float v = std::clamp(spread, 0.0f, 1.0f);
        if (v == stereoSpread_) {
            return;
        }
        stereoSpread_ = v;
        // FR-008: a setPartialPosition override lasts until the next spread change.
        positionOverridden_.fill(false);
        recalculatePan();
    }

    [[nodiscard]] float getStereoSpread() const noexcept { return stereoSpread_; }

    // =========================================================================
    // Per-partial envelope (FR-023)
    // =========================================================================

    /// @brief Envelope attack time (time-to-100 %, not a time constant). [0.05, 30] s.
    void setAttackTimeSec(float seconds) noexcept {
        if (detail::isNaN(seconds) || detail::isInf(seconds)) {
            return;
        }
        const float v = std::clamp(seconds, kMinAttackSec, kMaxAttackSec);
        if (v == attackSec_) {
            return;
        }
        attackSec_ = v;
    }

    /// @brief Envelope decay time. [0.05, 60] s.
    void setDecayTimeSec(float seconds) noexcept {
        if (detail::isNaN(seconds) || detail::isInf(seconds)) {
            return;
        }
        const float v = std::clamp(seconds, kMinDecaySec, kMaxDecaySec);
        if (v == decaySec_) {
            return;
        }
        decaySec_ = v;
    }

    /// @brief Per-partial envelope offset spread. [0, 1], scaling kMaxEnvOffsetSec.
    void setEnvelopeOffsetSpread(float spread) noexcept {
        if (detail::isNaN(spread) || detail::isInf(spread)) {
            return;
        }
        const float v = std::clamp(spread, 0.0f, 1.0f);
        if (v == offsetSpread_) {
            return;
        }
        offsetSpread_ = v;
        // Plan §4.4: re-scale the STORED per-seed draws. A re-draw here would
        // re-shuffle which partial starts first, making the onset order a property
        // of the spread history rather than of the seed.
        applyEnvelopeOffsetSpread();
    }

    [[nodiscard]] float getAttackTimeSec() const noexcept { return attackSec_; }
    [[nodiscard]] float getDecayTimeSec() const noexcept { return decaySec_; }
    [[nodiscard]] float getEnvelopeOffsetSpread() const noexcept { return offsetSpread_; }

    /// @brief Gate on. Re-opens every envelope FROM its current value (never from 0),
    ///        and redraws phases only when the cloud was quiescent (FR-016, plan §4.7).
    ///
    /// Quiescent means gate off AND every kernel-visible partial's
    /// `currentAmplitude` below `kQuiescenceAmplitude` (1.0e-5f, i.e. -100 dBFS).
    /// A note-on arriving while the cloud is still sounding therefore KEEPS every
    /// partial's phase and merely re-opens the envelope, which is what makes a
    /// sounding retrigger click-free by construction (SC-006's retrigger clause).
    ///
    /// Repeated `noteOn()` with no intervening `noteOff()` is idempotent apart from
    /// that envelope re-open: a partial already holding at 1.0 is put back into
    /// Attack and clamps straight back to Hold on the next chunk, so nothing steps.
    ///
    /// Two renders with the same seed but different retrigger TIMING legitimately
    /// differ — the quiescent branch consumes phase draws. Determinism is
    /// unaffected because SC-009 pins the call sequence, not just the seed.
    ///
    /// @par Why a quiescent note-on also snaps the FR-017 normalizer
    /// Every setter defers its recompute to the next chunk (plan §4.8) and step 3 of
    /// `updateControl` multiplies by `gainSmoothed_`, the value cached at the END of
    /// the PREVIOUS chunk. Configure the cloud while it is silent and the first
    /// chunk therefore pairs the NEW `baseAmplitude_` set with the gain belonging to
    /// the OLD one, after which `normGain_` slides to the correct value over
    /// `kNormGainSmoothMs`. Measured on this component (T005 step 1, seed
    /// 0x5E3A0003, `setRichness(1)` + `setSpectralTiltDb(+12)` from the shipped
    /// defaults): the first 40 ms render pinned FR-006's clamp at exactly 2.000 and
    /// the SC-018 onset peak was 2.000 for **every** one of 32 seeds, against a
    /// steady-state peak of 1.082. Snapping here removes it entirely — measured
    /// onset peak 1.081, i.e. the steady-state value.
    ///
    /// Doing it *only* while quiescent is what keeps it click-free by construction,
    /// the same argument FR-016 uses for the phase redraw: `currentAmplitude_` is
    /// untouched and it is what multiplies the oscillator, so the rendered sample is
    /// continuous across the snap — only the target the FR-014 smoother chases moves.
    /// A **sounding** retrigger must NOT snap (the gain would step), which is why
    /// this sits inside the quiescence branch and not beside `gate_ = true`.
    void noteOn() noexcept {
        if (isQuiescent()) {
            redrawPhases();

            // Flush the deferred config-rate recomputes first — snapping to a target
            // derived from a stale `baseAmplitude_` would just move the bug.
            if (freqDirty_) {
                recalculateFrequencies(false);  // redrawPhases() just ran: nothing to preserve
                freqDirty_ = false;
                markAmpDirty();  // a frequency recompute invalidates EVERY amplitude slot
            }
            if (ampDirty_) {
                recalculateAmplitudes();
                ampDirty_ = false;
            }

            // Same two statements reset() ends with, for the same reason (plan §5):
            // start at the correct level instead of sliding into it.
            normGain_.snapTo(currentNormGainTarget());
            gainSmoothed_ = normGain_.getCurrentValue();
        }
        gate_ = true;
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            envStage_[i] = kEnvStageAttack;
        }
    }

    /// @brief Gate off. Every non-idle envelope enters Release from its current value.
    void noteOff() noexcept {
        gate_ = false;
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            if (envStage_[i] != kEnvStageIdle) {
                envStage_[i] = kEnvStageRelease;
            }
        }
    }

    // =========================================================================
    // Determinism (FR-005)
    // =========================================================================

    /// @brief Derive one lane's stream seed from the cloud seed and a salt.
    ///
    /// PUBLIC because two gates in this phase are unwritable without it: the
    /// seed-distinctness gate asserts the 128 derived streams are pairwise
    /// distinct and non-zero, and the drift-lane equivalence gate has to build a
    /// reference `BrownianDrift` on `deriveSeed(seed, i)`. It is a pure function
    /// of its arguments, so exposing it adds no state and no coupling.
    ///
    /// The explicit non-zero substitution is load-bearing, not belt-and-braces:
    /// `Xorshift32::seed()` silently replaces 0 with its own default
    /// (`core/random.h:72-74`), so two lanes that both hashed to 0 would COLLAPSE
    /// ONTO THE SAME STREAM. `deriveSeed` must therefore never hand a lane 0 in
    /// the first place rather than rely on that substitution to fix collisions.
    ///
    /// @param base Cloud seed
    /// @param salt Lane discriminator (detune bank uses `i`, mutation bank `i + kMaxPartials`)
    /// @return A non-zero 32-bit stream seed (lowbias32 finaliser)
    [[nodiscard]] static constexpr std::uint32_t deriveSeed(std::uint32_t base,
                                                            std::size_t salt) noexcept {
        return deriveStreamSeed(base, salt);  // FR-006: the hash moved to Layer 0 (core/random.h).
    }

    /// @brief Set the cloud seed and redraw all once-per-seed state (plan §4.6).
    /// @note A re-seed also clears every `setPartialPosition` override: FR-021
    ///       scopes an override to "the next spread change, re-seed or reset()".
    void setSeed(std::uint32_t seed) noexcept {
        configuredSeed_ = seed;
        positionOverridden_.fill(false);
        reseed();
        recalculatePan();  // positionScatter_ moved
    }

    [[nodiscard]] std::uint32_t getSeed() const noexcept { return configuredSeed_; }

    // =========================================================================
    // Spectral-target injection (FR-081 – FR-086) — Seraphis Phase 3, plan §6
    // =========================================================================

    /// @brief Override the parametric partial ratios and amplitudes with an
    ///        externally supplied spectrum (FR-081).
    ///
    /// STRICTLY ADDITIVE: everything this enables is inert while no target has
    /// been supplied, so an untargeted cloud renders exactly what Phase 2 shipped.
    ///
    /// FR-082: the supplied ratio REPLACES the `pow(n, 1 + g*range)` grid law, but
    /// Spectral Gravity still applies as a WARP FACTOR on top of it, and
    /// Inharmonicity's `sqrt(1 + B*n^2)` stretch is untouched — the FR-083
    /// composition order is unchanged.
    /// FR-083: the supplied amplitude replaces the Richness ROLLOFF only. Tilt,
    /// the FR-041 active count N(r), the FR-043 tail and the FR-017 normalizer all
    /// still apply, so entropy stays level-neutral by construction.
    /// FR-084: nothing here touches `sinState_`/`cosState_`, so a target change is
    /// phase-continuous, and the amplitude change is chased by the same FR-014
    /// smoother — it cannot click. The next chunk's `recalculateFrequencies()`
    /// does SCALE both by one positive factor, which is a level correction and not
    /// a phase step — see `preserveOrbitEnergy()` for why it is required and why
    /// it is ~1e-4 per chunk at the FR-086 cadence.
    ///
    /// @par FR-086 — composition cadence
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
    ///
    /// @par Rejection (FR-081, AUTHORITATIVE for this entry point)
    /// Rejected WHOLESALE — nothing is written, not even the slots that passed —
    /// on a null pointer, `count == 0`, `count > kMaxPartials`, any NaN/Inf, any
    /// `ratios[i] <= 0.0f` (which rejects `-0.0f`, whose zero frequency would
    /// collapse the partial) or any `amplitudes[i] < 0.0f` (which ACCEPTS
    /// `-0.0f`). Non-monotone ratios, ratios outside
    /// `[SpectralState::kMinStateRatio, kMaxStateRatio]` and amplitudes above 1
    /// are all ACCEPTED — that is the point of the surface. FR-012's authored-state
    /// validity governs `SpectralState`, not this live post-entropy array, and the
    /// two diverge on purpose.
    ///
    /// @param ratios     `count` partial ratios relative to the fundamental
    /// @param amplitudes `count` linear amplitudes; may exceed 1
    /// @param count      Number of supplied partials, `1 .. kMaxPartials`
    void setSpectralTarget(const float* ratios, const float* amplitudes,
                           std::size_t count) noexcept {
        if (ratios == nullptr || amplitudes == nullptr || count == 0 || count > kMaxPartials) {
            return;
        }

        // ---------------------------------------------------------------------
        // FR-085 LEVER 1 - THE WHOLE-ARRAY SKIP, and it is the FIRST thing this
        // function does.
        // ---------------------------------------------------------------------
        // "The recompute is skipped when the supplied arrays are bit-identical to
        // the stored ones" (FR-085), and SC-010 clause 3 times exactly this path:
        // an unchanged target must cost no more than 10 % over the no-target
        // cloud. Without this early-out the unchanged case still pays the full
        // per-partial validation scan plus 128 epsilon compares and 128 stores,
        // which MEASURED 12.1 % on the reference machine - i.e. the clause fails
        // on cost alone even though not one slot is marked dirty.
        //
        // WHY RETURNING HERE CANNOT LOSE A DIRTY BIT. The mask below compares the
        // supplied value against committedRatio_/committedAmp_ (deviation D14).
        // committedRatio_[i] is only ever assigned targetRatio_[i], and only for
        // slots a recompute actually consumed - so between two bit-identical
        // calls the committed value can only move TOWARDS the supplied one (to it
        // exactly, or not at all). The mask this call would have produced is
        // therefore a SUBSET of the one the previous call produced, and every bit
        // of that one is either still standing in freqSlotDirty_/ampSlotDirty_
        // (they are sticky until a recompute clears them) or has already been
        // consumed by the recompute that zeroed the distance. Skipping is exact,
        // not approximate.
        //
        // Validation is skipped with it, which is also exact: the stored arrays
        // were validated when they were accepted, and these are bit-identical to
        // them. hasTarget_ gates the whole thing, so the FIRST call always runs
        // the full path.
        if (hasTarget_ && count == targetCount_) {
            const std::size_t bytes = count * sizeof(float);
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - float arrays,
            // and FR-085 states the skip in terms of BIT-identical arrays.
            if (std::memcmp(ratios, targetRatio_.data(), bytes) == 0
                // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison)
                && std::memcmp(amplitudes, targetAmp_.data(), bytes) == 0) {
                return;
            }
        }

        for (std::size_t i = 0; i < count; ++i) {
            if (detail::isNaN(ratios[i]) || detail::isInf(ratios[i])
                || detail::isNaN(amplitudes[i]) || detail::isInf(amplitudes[i])
                || ratios[i] <= 0.0f || amplitudes[i] < 0.0f) {
                return;  // wholesale rejection, nothing written
            }
        }

        std::uint64_t fMask = 0;
        std::uint64_t aMask = 0;
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            const float r = (i < count) ? ratios[i] : static_cast<float>(i + 1);
            const float a = (i < count) ? amplitudes[i] : 0.0f;
            // COMPARE AGAINST THE COMMITTED VALUE — the one the last recompute
            // actually consumed — NEVER against targetRatio_/targetAmp_, which
            // this loop is about to overwrite. Deviation D14: with the stored
            // target as baseline, the baseline advances with the input while the
            // recompute is skipped, so sub-epsilon per-chunk motion accumulates
            // FOREVER and never trips the threshold. At the FR-005 default travel
            // rate a 64-sample chunk moves partial 24 of the SineStack->Bell pair
            // by 0.0061 cent — permanently under kTargetRatioEpsilonCents — so
            // most partials would freeze at their start frequency for the whole
            // journey and the phase's central feature would silently not render.
            if (!hasTarget_
                || std::abs(r - committedRatio_[i]) > committedRatio_[i] * kTargetRatioRelEpsilon) {
                fMask |= (std::uint64_t{1} << i);
            }
            if (!hasTarget_ || std::abs(a - committedAmp_[i]) > kTargetAmpEpsilon) {
                aMask |= (std::uint64_t{1} << i);
            }
            targetRatio_[i] = r;  // latest supplied value; always stored
            targetAmp_[i] = a;
        }
        targetCount_ = count;  // FR-085 lever 1's comparand length
        hasTarget_ = true;
        if (fMask != 0) {
            freqDirty_ = true;
            freqSlotDirty_ |= fMask;
        }
        if (aMask != 0) {
            ampDirty_ = true;
            ampSlotDirty_ |= aMask;
        }
    }

    /// @brief Return to the parametric ratio and amplitude laws (FR-084).
    /// @note Goes through the same dirty-flag path and the same FR-014 amplitude
    ///       smoother as every other configuration change, so it cannot click.
    void clearSpectralTarget() noexcept {
        hasTarget_ = false;
        markFreqDirty();
        markAmpDirty();
    }

    [[nodiscard]] bool hasSpectralTarget() const noexcept { return hasTarget_; }

    // =========================================================================
    // Render (FR-004) — plan §4.1
    // =========================================================================

    /// @brief Render `numSamples` of stereo audio into two separate buffers.
    /// @param leftOutput  Left channel destination (null is rejected without writing)
    /// @param rightOutput Right channel destination (null is rejected without writing)
    /// @param numSamples  Sample count; 0 is a no-op
    void processStereoBlock(float* leftOutput, float* rightOutput,
                            std::size_t numSamples) noexcept {
        // FR-004, Edge Cases: null buffers rejected without writing; 0 is a no-op.
        if (leftOutput == nullptr || rightOutput == nullptr || numSamples == 0) {
            return;
        }

        // Edge Cases: processing before prepare() outputs silence rather than
        // reading uninitialized coefficients (…bank.h:809-815).
        if (!prepared_) {
            std::fill_n(leftOutput, numSamples, 0.0f);
            std::fill_n(rightOutput, numSamples, 0.0f);
            return;
        }

        // Quiescent early-out (plan §4.1): the lanes keep free-running so
        // life-modulation never stops, and driftReadCount_ advances identically,
        // so a silent render and a sounding render of the same length leave
        // identical lane state (SC-015 clauses 2a/2b/2c).
        if (isQuiescent()) {
            advanceDriftLanes(detuneLanes_, numSamples);
            advanceDriftLanes(mutationLanes_, numSamples);
            driftReadCount_ += (numSamples + kControlChunkSamples - 1) / kControlChunkSamples;
            std::fill_n(leftOutput, numSamples, 0.0f);
            std::fill_n(rightOutput, numSamples, 0.0f);
            return;
        }

        // Chunked render (plan §4.1). Nothing here is keyed to the caller's block
        // size, so the render is block-size-invariant up to the 64-sample control
        // grid: a 16384-sample block is 256 chunks, not one frozen control frame.
        std::size_t done = 0;
        while (done < numSamples) {
            const std::size_t chunk = std::min(kControlChunkSamples, numSamples - done);
            updateControl(chunk);

            for (std::size_t s = 0; s < chunk; ++s) {
                // The kernel ACCUMULATES into these (`sumL += outSumL`,
                // `harmonic_oscillator_bank_simd.cpp:182-183`), so both MUST be zeroed
                // every sample — carrying them over gives a silently ramping DC output.
                float sl = 0.0f;
                float sr = 0.0f;

                processMcfBatchSIMD(sinState_.data(), cosState_.data(), epsilon_.data(),
                                    detuneMultiplier_.data(), currentAmplitude_.data(),
                                    targetAmplitude_.data(), antiAliasGain_.data(),
                                    panLeft_.data(), panRight_.data(), ampSmoothCoeff_,
                                    kernelCount_, sl, sr);

                if (crossfadeRemaining_ > 0) {  // FR-013, `…bank.h:782-788`
                    const float p = static_cast<float>(crossfadeRemaining_)
                                    / static_cast<float>(crossfadeLengthSamples_);
                    sl = crossfadeOldL_ * p + sl * (1.0f - p);
                    sr = crossfadeOldR_ * p + sr * (1.0f - p);
                    --crossfadeRemaining_;
                }

                leftOutput[done + s] = std::clamp(sl, -kOutputClamp, kOutputClamp);   // FR-006
                rightOutput[done + s] = std::clamp(sr, -kOutputClamp, kOutputClamp);  // FR-006
            }

            lastOutL_ = leftOutput[done + chunk - 1];
            lastOutR_ = rightOutput[done + chunk - 1];
            done += chunk;
        }
    }

    // =========================================================================
    // FR-008 test/introspection surface (public contract, not #ifdef scaffolding)
    // =========================================================================

    /// @brief Active partial count N(r) — explicit state, not a spectral measurement.
    [[nodiscard]] std::size_t getActivePartialCount() const noexcept {
        return static_cast<std::size_t>(activeCount_);
    }

    /// @brief Undetuned synthesized frequency of partial `i` in Hz (FR-083).
    [[nodiscard]] float getPartialFrequencyHz(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? frequencyHz_[i] : 0.0f;
    }

    [[nodiscard]] float getPartialCurrentAmplitude(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? currentAmplitude_[i] : 0.0f;
    }

    [[nodiscard]] float getPartialTargetAmplitude(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? targetAmplitude_[i] : 0.0f;
    }

    /// @brief Post-Richness/post-tilt/post-normalization target, excluding the
    ///        mutation weight and the envelope (SC-013, SC-016 reference).
    [[nodiscard]] float getPartialUnmutatedTargetAmplitude(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? unmutatedTarget_[i] : 0.0f;
    }

    [[nodiscard]] float getPartialAntiAliasGain(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? antiAliasGain_[i] : 0.0f;
    }

    [[nodiscard]] float getPartialPanLeft(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? panLeft_[i] : 0.0f;
    }

    [[nodiscard]] float getPartialPanRight(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? panRight_[i] : 0.0f;
    }

    /// @brief Stereo position of partial `i` in [-1, +1].
    [[nodiscard]] float getPartialPosition(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? panPosition_[i] : 0.0f;
    }

    /// @brief Current drift detune of partial `i` as a frequency MULTIPLIER.
    [[nodiscard]] float getPartialDriftDetune(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? detuneMultiplier_[i] : 0.0f;
    }

    /// @brief Partial `i`'s FR-023 attack offset in seconds — the amount its
    ///        time-to-100 % exceeds the cloud-level attack time.
    ///
    /// This accessor and its decay twin exist for the same reason as the other
    /// additions to FR-008's surface: SC-013 clauses 3 and 4 are phrased against
    /// "the documented attack time plus **its own offset**" and
    /// "`decayTime + that partial's decay offset`". The offsets are seeded draws
    /// scaled by the spread control (plan §4.4), so nothing outside the component
    /// can know them, and a criterion forced to fall back on the WORST-case offset
    /// would pass an implementation that handed every partial the same offset —
    /// exactly the defect clause 3 exists to reject.
    [[nodiscard]] float getPartialAttackOffsetSec(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? attackOffsetSec_[i] : 0.0f;
    }

    /// @brief Partial `i`'s FR-023 decay offset in seconds (see the attack twin).
    [[nodiscard]] float getPartialDecayOffsetSec(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? decayOffsetSec_[i] : 0.0f;
    }

    [[nodiscard]] float getPartialSinState(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? sinState_[i] : 0.0f;
    }

    [[nodiscard]] float getPartialCosState(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? cosState_[i] : 0.0f;
    }

    /// @brief Raw detune lane value, in exactly the shape of
    ///        `BrownianDrift::getCurrentValue()` (`brownian_drift.h:212-214`).
    [[nodiscard]] float getDriftLaneValue(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? std::clamp(detuneLanes_.smoothCur[i], -1.0f, 1.0f) : 0.0f;
    }

    /// @brief Raw mutation lane value, same shape as getDriftLaneValue().
    [[nodiscard]] float getMutationLaneValue(std::size_t i) const noexcept {
        return (i < kMaxPartials) ? std::clamp(mutationLanes_.smoothCur[i], -1.0f, 1.0f) : 0.0f;
    }

    /// @brief Cloud-side drift reads PER PARTIAL (one per control chunk), SC-015 clause 2(b).
    [[nodiscard]] std::uint64_t getDriftReadCount() const noexcept { return driftReadCount_; }

    /// @brief True when the gate is off and every kernel-visible partial has decayed
    ///        below kQuiescenceAmplitude — the render early-out and the Phase-7
    ///        voice-retirement signal.
    [[nodiscard]] bool isQuiescent() const noexcept {
        if (gate_) {
            return false;
        }
        for (int i = 0; i < kernelCount_; ++i) {
            if (currentAmplitude_[static_cast<std::size_t>(i)] >= kQuiescenceAmplitude) {
                return false;
            }
        }
        return true;
    }

    /// @brief Bit-pattern finiteness test over the MCF state (`…bank.h:622-633`).
    /// @note Uses detail::isNaN/isInf, never std::isnan — the macOS leg is -ffast-math.
    [[nodiscard]] bool stateFinite() const noexcept {
        for (int i = 0; i < kernelCount_; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            if (detail::isNaN(sinState_[idx]) || detail::isInf(sinState_[idx]) ||
                detail::isNaN(cosState_[idx]) || detail::isInf(cosState_[idx])) {
                return false;
            }
        }
        return true;
    }

    /// @brief Place one partial at an exact stereo position, overriding the FR-021
    ///        seeded scatter until the next spread change, re-seed or reset().
    /// @param index    Partial index; out of range is a no-op
    /// @param position Stereo position, clamped to [-1, +1]
    void setPartialPosition(std::size_t index, float position) noexcept {
        if (index >= kMaxPartials) {
            return;
        }
        if (detail::isNaN(position) || detail::isInf(position)) {
            return;
        }
        panPosition_[index] = std::clamp(position, -1.0f, 1.0f);
        positionOverridden_[index] = true;
        updatePanGains(index);
    }

    /// @brief Mask a partial in or out. `active == false` forces its target amplitude
    ///        to zero at the END of the amplitude chain, so FR-014's smoother still
    ///        applies and masking cannot click.
    void setPartialMask(std::size_t index, bool active) noexcept {
        if (index >= kMaxPartials) {
            return;
        }
        masked_[index] = !active;
    }

    /// @brief Mask every partial except `index`. Out-of-range index is a no-op.
    void soloPartial(std::size_t index) noexcept {
        if (index >= kMaxPartials) {
            return;
        }
        masked_.fill(true);
        masked_[index] = false;
    }

    /// @brief Clear every mask.
    void clearPartialMask() noexcept { masked_.fill(false); }

private:
    // =========================================================================
    // Envelope stages (plan §1.3: 0 Idle, 1 Attack, 2 Hold, 3 Release)
    // =========================================================================
    static constexpr std::uint8_t kEnvStageIdle = 0;
    static constexpr std::uint8_t kEnvStageAttack = 1;
    static constexpr std::uint8_t kEnvStageHold = 2;
    static constexpr std::uint8_t kEnvStageRelease = 3;

    // =========================================================================
    // Drift lanes (plan §1.3, §4.5) — SoA, no BrownianDrift objects
    // =========================================================================

    /// Wrapper so `std::array<..., kMaxPartials>{}` can be value-initialised:
    /// `Xorshift32`'s only constructor is explicit (`core/random.h:44`), which makes
    /// `std::array<Xorshift32, N>{}` copy-initialisation and therefore ill-formed
    /// (plan §0.1 trap 3). This holds the REAL Layer-0 RNG — never a hand-copied
    /// xorshift, which would silently desynchronise from BrownianDrift's streams.
    struct LaneRng {
        Xorshift32 rng{1};
    };

    struct DriftLanes {
        alignas(32) std::array<float, kMaxPartials> walk{};       ///< x_i
        alignas(32) std::array<float, kMaxPartials> smoothCur{};  ///< one-pole current
        alignas(32) std::array<float, kMaxPartials> smoothTgt{};  ///< one-pole target
        std::array<LaneRng, kMaxPartials> rng{};
        float a = 0.0f;                ///< AR(1) retention coefficient
        float g = 0.0f;                ///< AR(1) innovation gain
        float depth = 1.0f;            ///< BrownianDrift::setDepth semantics
        int samplesUntilControl = 0;   ///< SHARED across all lanes of the bank

        /// Memo of `std::pow(driftSmoothCoeff_, float(cachedPowN))` (plan §4.5,
        /// SC-007). `advanceSmootherAllLanes` is entered twice per 64-sample chunk
        /// and `numSamples` is 32 both times, so without a memo a 512-sample block
        /// pays 32 `powf` calls to recompute one value. The memo returns THE SAME
        /// FLOAT: it is filled by the same `std::pow(coefficient, (float)numSamples)`
        /// call site with the same runtime operands, so it is not the precomputed
        /// coeff^k TABLE that comment warns about — that table's hazard is the
        /// compile-time-constant exponent /fp:fast strength-reduces into repeated
        /// multiplication, and there is no constant exponent here.
        /// `cachedPowN <= 0` means "empty"; `reset()` clears it and
        /// `driftSmoothCoeff_` only ever moves in `prepare()`, which calls `reset()`.
        int cachedPowN = 0;
        float cachedPowValue = 0.0f;
    };

    // =========================================================================
    // Seeding helpers (plan §4.6) — `deriveSeed` is PUBLIC, see above
    // =========================================================================

    /// @brief Redraw every partial's MCF phase from the dedicated phase stream
    ///        (FR-016). Shape from `…bank.h:288-290`, with a seeded phase.
    ///
    /// Distribution: each partial's initial phase is drawn independently and
    /// uniformly from [0, 2*pi) — `phaseRng_.nextUnipolar()` is uniform on [0, 1]
    /// (`core/random.h:66`) scaled by `kTwoPi` — so no two partials start coherent
    /// and a fresh note never begins with an impulse-like in-phase transient.
    /// The draws come from a stream SEPARATE from `configRng_`, so a retrigger
    /// redraws phases without perturbing any once-per-seed state.
    ///
    /// @note `sinState_`/`cosState_` are SEEDED here, never zeroed: a zeroed MCF
    ///       state is a fixed point of the recurrence and would render silence.
    void redrawPhases() noexcept {
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            const float phase = phaseRng_.nextUnipolar() * kTwoPi;
            sinState_[i] = std::sin(phase);
            cosState_[i] = std::cos(phase);
        }
    }

    /// @brief Rewind both seeded streams and redraw all once-per-seed state.
    ///
    /// THE DRAW ORDER BELOW IS FIXED AND DOCUMENTED (plan §4.6), because `reset()`
    /// must reproduce every one of these draws exactly. All of them come off
    /// `configRng_` in this sequence, so inserting, removing or reordering a loop
    /// re-shuffles every subsequent partial's state and silently breaks SC-009's
    /// reset-reproducibility clause.
    ///
    /// `redrawPhases()` deliberately draws from a SEPARATE stream (`phaseRng_`),
    /// which is what lets a retrigger redraw phases without perturbing any of the
    /// once-per-seed draws above it (FR-016, plan §4.7).
    void reseed() noexcept {
        configRng_.seed(configuredSeed_);
        phaseRng_.seed(deriveSeed(configuredSeed_, 0xF0F0u));

        // FR-021: s_i ~ U[-1, +1]. `nextFloat()` is ALREADY bipolar
        // (`core/random.h:58`) — remapping it here would halve the stereo field.
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            positionScatter_[i] = configRng_.nextFloat();
        }

        // FR-022: amount_i = (n / kMaxPartials)^kDriftIndexExponent * u_i, with
        // 1-based n and u_i ~ U[0.5, 1.0]. The denominator is the FIXED capacity,
        // never activeCount_, so changing Richness cannot re-scale an existing
        // partial's drift. Both factors are <= 1, so amount_i <= 1 — which is what
        // makes driftCents_ a true upper bound over all partials (SC-015 clause 3a)
        // rather than merely a scale factor. The [0.5, 1.0] floor on u_i keeps
        // every partial alive: no partial is silently inert.
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            const float u = 0.5f + 0.5f * configRng_.nextUnipolar();
            const float indexTerm = static_cast<float>(i + 1) / static_cast<float>(kMaxPartials);
            driftAmount_[i] = std::pow(indexTerm, kDriftIndexExponent) * u;
        }

        // FR-023: the raw envelope-offset draws, stored UNSCALED. The
        // `offsetSpread_` factor is re-applied by applyEnvelopeOffsetSpread() on
        // every spread change instead of being baked in here, so the ORDERING of
        // partial onsets stays a property of the seed and a spread change never
        // forces a re-draw (plan §4.4).
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            attackOffsetDraw_[i] = configRng_.nextUnipolar();
        }
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            decayOffsetDraw_[i] = configRng_.nextUnipolar();
        }

        // 128 independent lane streams. The salt separates the two banks, so the
        // detune and mutation lanes of the same partial never share a stream
        // (plan §4.5's configuration table).
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            detuneLanes_.rng[i].rng.seed(deriveSeed(configuredSeed_, i));
        }
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            mutationLanes_.rng[i].rng.seed(deriveSeed(configuredSeed_, i + kMaxPartials));
        }

        applyEnvelopeOffsetSpread();
        redrawPhases();
    }

    /// @brief Re-apply the FR-023 offset spread to the stored per-seed draws.
    /// @note Scaling the stored draws rather than re-drawing is what keeps the
    ///       order of partial onsets a seed property across a spread change.
    void applyEnvelopeOffsetSpread() noexcept {
        const float scale = offsetSpread_ * kMaxEnvOffsetSec;
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            attackOffsetSec_[i] = scale * attackOffsetDraw_[i];
            decayOffsetSec_[i] = scale * decayOffsetDraw_[i];
        }
    }

    // =========================================================================
    // Configuration-rate recomputes (plan §2, §3, §4.3) — filled by later tasks
    // =========================================================================

    /// @brief Schedule a frequency recompute of EVERY partial (plan §6.2).
    /// A parametric change invalidates every slot, so the per-slot mask goes to
    /// all-ones; only `setSpectralTarget` ever sets a partial mask.
    void markFreqDirty() noexcept {
        freqDirty_ = true;
        freqSlotDirty_ = ~std::uint64_t{0};
    }

    /// @brief Schedule an amplitude recompute of EVERY partial (plan §6.2).
    void markAmpDirty() noexcept {
        ampDirty_ = true;
        ampSlotDirty_ = ~std::uint64_t{0};
    }

    /// @brief FR-083 combined frequency law + epsilon (plan §2). CONFIG RATE ONLY.
    ///
    /// For 1-based partial number n:
    /// @code
    ///   ratio_g(n) = pow(n, 1 + gravity * kGravityExponentRange)   // FR-081
    ///   stretch(n) = sqrt(1 + inharmonicity * n^2)                 // FR-051
    ///   f_n        = f0 * ratio_g(n) * stretch(n)                  // FR-083, THIS order
    /// @endcode
    /// `ratio_g(1) == 1` exactly for every gravity setting (`pow(1, x) == 1`), so the
    /// fundamental never moves. This never touches sinState_/cosState_, so a
    /// frequency change is phase-continuous by construction (FR-034).
    /// @param preserveOrbit True only on the per-chunk path, where the partial is
    ///        already sounding on an established orbit whose energy must survive
    ///        the epsilon rewrite. False from `reset()` and from `noteOn()`'s
    ///        flush, where the orbit is being (re)established and there is nothing
    ///        to preserve — `noteOn()` calls `redrawPhases()` FIRST (`:632`), so a
    ///        rescale there would act on a brand-new draw against an epsilon
    ///        derived from a stale configuration and would break SC-014 clause 2's
    ///        identity render (measured: worst sample error 1.7e-3 against the
    ///        1.0e-4 fingerprint tolerance, at `r = 0, g = -1, tilt = -12`).
    void recalculateFrequencies(bool preserveOrbit) noexcept {
        const float exponent = 1.0f + gravity_ * kGravityExponentRange;
        // FR-081 says the g = 0 grid is EXACTLY the integers, so take that branch
        // exactly — the same identity-branch idiom tiltGain() copies from
        // `additive_oscillator.h:481-489`. It is load-bearing rather than merely
        // fast: the macOS leg builds -ffast-math, under which `pow(n, 1.0f)` may be
        // rewritten as `exp2(1.0f * log2(n))` and hand back 31.999998 for n = 32.
        // SC-004 clause 1 asserts the grid at g = 0; every other criterion that
        // renders at the default gravity (SC-001, SC-002, SC-003) rests on it too.
        //
        // The non-identity branch is `exp2(exponent * log2(n))` off the shared
        // detail::kHarmonicCloudLog2N table rather than `std::pow(n, exponent)`
        // (SC-007). That is the SAME rewrite the comment above
        // warns -ffast-math performs — which is precisely why the `gravityIsZero`
        // identity branch above it is load-bearing and must stay: it is what keeps
        // SC-004 clause 1's g = 0 grid exactly integral. Off that branch the
        // rewrite's error is the table's own float rounding, ~2e-8 relative
        // (4e-5 cent), against SC-002/SC-004's 1-cent tolerance.
        const bool gravityIsZero = (gravity_ == 0.0f);
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            // FR-085 lever 3: with a target active, recompute only the slots
            // setSpectralTarget marked as having actually MOVED. INERT while
            // hasTarget_ is false, so the untargeted loop is the shipped loop.
            if (hasTarget_ && (freqSlotDirty_ & (std::uint64_t{1} << i)) == 0) {
                continue;
            }
            const float n = static_cast<float>(i + 1);
            committedRatio_[i] = targetRatio_[i];  // this slot IS being recomputed now
            float ratioG = 0.0f;  // both arms below assign; initialised for clang-tidy
            if (hasTarget_ && targetRatio_[i] != n) {
                // FR-082: the branch is scoped to the WARP FACTOR ALONE — the
                // supplied ratio replaces the grid law, gravity still warps it.
                const float warp =
                    gravityIsZero
                        ? 1.0f
                        : std::exp2(gravity_ * kGravityExponentRange
                                    * detail::kHarmonicCloudLog2N[i]);
                ratioG = targetRatio_[i] * warp;
            } else {
                // No target, or the FR-082 identity guard: fall back to the
                // UNMODIFIED parametric law INCLUDING its own gravityIsZero
                // branch. Falling back to the std::exp2 arm alone would evaluate
                // exp2(1.0f * log2N[i]) — exactly the rewrite the comment above
                // warns hands back 31.999998 for n = 32 under -ffast-math,
                // destroying the bit-exactness that branch exists to protect and
                // doing it invisibly to a fingerprint tolerance.
                ratioG = gravityIsZero ? n : std::exp2(exponent * detail::kHarmonicCloudLog2N[i]);
            }
            const float stretch = std::sqrt(1.0f + inharmonicity_ * n * n);  // additive_oscillator.h:472
            const float f = fundamentalHz_ * ratioG * stretch;
            frequencyHz_[i] = f;
            const float epsOld = epsilon_[i];
            const float epsNew = std::clamp(2.0f * std::sin(kPi * f * invSampleRate_),
                                            -kMaxEpsilon, kMaxEpsilon);  // …bank.h:1054-1055
            epsilon_[i] = epsNew;
            if (preserveOrbit) {
                preserveOrbitEnergy(i, epsOld, epsNew);
            }
        }
        freqSlotDirty_ = 0;
    }

    /// @brief Hold the MCF orbit energy fixed across an epsilon change (SC-009).
    ///
    /// SPEC GAP, RECORDED HERE DELIBERATELY: no FR covers this. It is the fix for a
    /// defect SC-009 clauses 1 and 2 caught on the `Bell -> Breath` pair, and it
    /// belongs with FR-084's phase-continuity guarantee, which is silent about the
    /// orbit's AMPLITUDE. See the plan's deviation table.
    ///
    /// THE PROBLEM THIS SOLVES IS A LEVEL ERROR, NOT A PHASE ERROR. The kernel's
    /// recurrence (`harmonic_oscillator_bank_simd.cpp:103-105`)
    /// `s' = s + eps*c`, `c' = c - eps*s'` conserves
    /// @code
    ///   E = s^2 + c^2 + eps*s*c
    /// @endcode
    /// exactly — for FIXED `eps`. The orbit's peak `|s|` is `sqrt(E)/cos(w/2)`, and
    /// FR-015's correction factor is exactly `cos(w/2)`
    /// (`updateAntiAliasGain`, `:1436-1451`), so the RENDERED peak of a partial is
    /// `targetAmplitude * fade * sqrt(E)`. `E == 1` is therefore the condition for a
    /// partial to render at the amplitude it was asked for, and Phase 2's measured
    /// +1.60 / −2.55 dB per-partial spread at partial 64
    /// (`harmonic_cloud_spectral_test.cpp:104-125`) is precisely `E`'s FR-016 phase
    /// draw: `E = 1 + (eps/2)*sin(4*pi*phase)` right after `redrawPhases()`.
    ///
    /// Rewriting `eps` at a fixed state moves `E` by `(eps_new - eps_old)*s*c`, and
    /// `|s|` grows like `1/cos(w/2)` — 10x at the `kMaxEpsilon` clamp. A partial
    /// swept through the near-Nyquist region therefore accumulates a LARGE,
    /// one-directional energy error which stays with it after it comes back down,
    /// and it is inaudible while it happens because FR-015 has already faded that
    /// partial out. Measured on the SC-009 `Bell -> Breath` journey before this
    /// function existed: per-partial render-gain errors of +11.3 dB and −12.7 dB
    /// after the sweep, a whole-render RMS 3.8 dB above the FR-017 target and a peak
    /// pinned at `kOutputClamp`. Rescaling `s` and `c` by `sqrt(E_old / E_new)`
    /// restores the invariant the recurrence itself cannot.
    ///
    /// @par Why this does not contradict FR-034 / FR-084
    /// Both say a frequency change must not STEP the waveform. This scales `s` and
    /// `c` by one positive factor rather than rotating them, so the phase is
    /// untouched; the sample moves by `|scale - 1|`, which over the FR-086 cadence
    /// is ~1e-4 per chunk (0.001 dB) because `eps` moves by ~3e-5 per chunk at the
    /// travel rates FR-061 admits. A `setState` swap is absorbed over
    /// `kStateChangeFadeSec` by FR-047, so no caller can present a large jump.
    ///
    /// @par Why it is gated on `hasTarget_`
    /// Non-Goals forbid any change to Phase 2 behaviour when the injection surface
    /// is unused, and SC-014 is the standing gate on that. Off the injection path
    /// `epsilon_` only moves on a configuration-time setter, never thousands of
    /// times per second, so the accumulation this corrects is not reachable there.
    /// Same "inert until a target is supplied" rule as FR-085's three levers.
    /// With an IDENTITY target (SC-014 clause 2) `epsNew == epsOld` bitwise, so this
    /// is a bitwise no-op there too, which is what keeps that clause satisfiable.
    ///
    /// @param index  Partial index, < kMaxPartials
    /// @param epsOld The epsilon the current state's energy was accumulated under
    /// @param epsNew The epsilon just written to `epsilon_[index]`
    void preserveOrbitEnergy(std::size_t index, float epsOld, float epsNew) noexcept {
        if (!hasTarget_ || epsNew == epsOld) {
            return;
        }
        const float s = sinState_[index];
        const float c = cosState_[index];
        const float quad = s * s + c * c;
        const float cross = s * c;
        const float eOld = quad + epsOld * cross;
        const float eNew = quad + epsNew * cross;
        // `E` is positive definite for |eps| < 2 (discriminant eps^2 - 4 < 0), so the
        // only way either is non-positive is the quiescent all-zero state before the
        // first noteOn() — where there is no orbit to preserve.
        if (eOld <= 0.0f || eNew <= 0.0f) {
            return;
        }
        const float scale = std::sqrt(eOld / eNew);
        sinState_[index] = s * scale;
        cosState_[index] = c * scale;
    }

    /// @brief FR-061 per-partial tilt gain for the 1-based partial number `n`.
    /// @note The identity branch is copied from `additive_oscillator.h:481-489`:
    ///       tilt 0 dB/oct and the fundamental are exactly unity, never
    ///       `pow(10, 0)`, so tilt cannot perturb the reference partial.
    /// @param index 0-based partial index (`n = index + 1`), < kMaxPartials
    /// @note `pow(10, tiltDb*log2(n)/20)` is evaluated as
    ///       `exp2(tiltDb*log2(n) * log2(10)/20)` off the shared log2 table — the
    ///       same value by the identity `10^y == 2^(y*log2(10))`, with one `exp2`
    ///       in place of a `log2` and a `pow` (SC-007). The identity branch is
    ///       untouched, so tilt 0 and the fundamental are still EXACTLY unity.
    [[nodiscard]] float tiltGain(std::size_t index) const noexcept {
        if (tiltDb_ == 0.0f || index == 0) {
            return 1.0f;
        }
        constexpr float kLog2TenOver20 = 3.32192809488736235f / 20.0f;
        return std::exp2(tiltDb_ * detail::kHarmonicCloudLog2N[index] * kLog2TenOver20);
    }

    /// @brief FR-041 x FR-061 base amplitudes + FR-017 normalizer target (plan §3).
    ///
    /// THE AMPLITUDE COMPOSITION ORDER IS FIXED AND IS ITSELF A REQUIREMENT
    /// (FR-017). Nothing may be folded into a different stage:
    /// @code
    ///   a_i               = richnessRolloff(i) * tiltGain(i)                    // config rate
    ///   gainTarget        = min(kTargetOscRms / sqrt(sum_{i<N} a_i^2 * 0.5),
    ///                           kMaxNormGain)                                   // config rate
    ///                       -> normGain_.setTarget(gainTarget)   // LAST statement here
    ///   gainSmoothed      = normGain_.getCurrentValue()                         // per chunk
    ///   unmutatedTarget_i = gainSmoothed * a_i                                  // per chunk
    ///   targetAmplitude_i = unmutatedTarget_i * w_i * env_i                     // per chunk
    ///   currentAmplitude_i <- kernel, += ampSmoothCoeff * (target_i * aa_i - current_i)
    /// @endcode
    ///
    /// The normalizer's input is the `a_i` set ONLY. The mutation weights `w_i`
    /// (FR-071), the per-partial drift and the per-partial envelope are
    /// deliberately OUTSIDE it: recomputing the gain from mutated amplitudes
    /// would cancel exactly the +-3 dB level movement SC-016 exists to observe.
    /// Being one scalar shared by every partial, it also cannot bend the measured
    /// tilt slope (SC-003) or any partial ratio.
    void recalculateAmplitudes() noexcept {
        // FR-041(a): N(r) = clamp(round(64^r), 1, 64). The active count is EXPLICIT
        // state — it is what getActivePartialCount() returns and what the kernel
        // receives as its numPartials, so inactive partials cost no CPU.
        const float rounded = std::round(std::pow(static_cast<float>(kMaxPartials), richness_));
        activeCount_ = std::clamp(static_cast<int>(rounded), 1, static_cast<int>(kMaxPartials));

        // FR-041(b): p(r) linear from kRichnessMinExponent (3.0) at r = 0 to
        // kRichnessMaxExponent (0.5) at r = 1, i.e. p(r) = 3.0 - 2.5*r.
        const float exponent =
            kRichnessMinExponent + (kRichnessMaxExponent - kRichnessMinExponent) * richness_;

        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            if (static_cast<int>(i) >= activeCount_) {
                baseAmplitude_[i] = 0.0f;  // a_n = 0 for n > N(r)
                continue;
            }
            // FR-085 lever 3, and it MUST sit after the zeroing above so a
            // Richness reduction still silences a slot the mask left undirtied.
            // INERT while hasTarget_ is false.
            if (hasTarget_ && (ampSlotDirty_ & (std::uint64_t{1} << i)) == 0) {
                continue;
            }
            committedAmp_[i] = targetAmp_[i];  // this slot IS being recomputed now
            // FR-041 x FR-061. `pow(n, -p)` is `exp2(-p * log2(n))` off the shared
            // table (SC-007) — the identity, to within the table's ~2e-8 rounding,
            // which is 2e-7 dB against SC-003's 0.5 dB per-partial bound. n = 1 is
            // exact either way: log2(1) is 0, so exp2(0) is 1.0f.
            //
            // FR-083: a supplied amplitude replaces the Richness ROLLOFF ONLY —
            // tilt still multiplies it. Richness's rolloff exponent therefore has
            // no effect while a target is active, deliberately (C-3): multiplying
            // a state's own shape by n^(-p) would erase exactly the timbral
            // distinction between the factory states.
            baseAmplitude_[i] =
                hasTarget_ ? targetAmp_[i] * tiltGain(i)
                           : std::exp2(-exponent * detail::kHarmonicCloudLog2N[i]) * tiltGain(i);
        }

        // FR-043: a partial dropped from the active count keeps being handed to the
        // kernel until its smoothed amplitude has faded, because its target is now
        // zero (baseAmplitude_ above) and the kernel's uniform ampSmoothCoeff fades
        // it INSIDE the SIMD path. Truncation would click; this cannot.
        tailHighWater_ = std::max(tailHighWater_, activeCount_);
        kernelCount_ = std::max(activeCount_, tailHighWater_);

        // FR-085 lever 3: consume the mask. Placed HERE rather than at the end of
        // the function because FR-017 below must remain the last statement.
        // Without it the mask saturates after a few chunks (setSpectralTarget
        // accumulates with |=) and the amplitude half of the lever is dead code.
        ampSlotDirty_ = 0;

        // FR-017. THIS MUST BE THE LAST STATEMENT OF THIS FUNCTION. reset() calls
        // normGain_.snapTo(...), after which OnePoleSmoother::advanceSamples()
        // early-returns on isComplete() (`smoother.h:244`) FOREVER unless something
        // moves the target. Omitting this line silently disables FR-017: no NaN, no
        // clip, just a level frozen at its reset value for the life of the instance,
        // surfacing as SC-016/SC-006 failures no assertion names.
        normGain_.setTarget(currentNormGainTarget());
    }

    /// @brief FR-043 tail retirement — checked ONCE PER CHUNK, never per sample.
    ///
    /// Once every departing partial in `[activeCount_, tailHighWater_)` has faded
    /// below kTailSilenceThreshold (the reference's 1e-8 idiom, `…bank.h:756`), the
    /// high-water mark drops back to the active count and those lanes stop costing
    /// kernel time.
    void retireFadedTail() noexcept {
        if (tailHighWater_ <= activeCount_) {
            return;
        }
        for (int i = activeCount_; i < tailHighWater_; ++i) {
            if (std::abs(currentAmplitude_[static_cast<std::size_t>(i)]) >= kTailSilenceThreshold) {
                return;
            }
        }
        tailHighWater_ = activeCount_;
        kernelCount_ = activeCount_;
    }

    /// @brief FR-021/FR-091 per-partial pan positions and gains (plan §4.3).
    void recalculatePan() noexcept {
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            if (!positionOverridden_[i]) {
                panPosition_[i] = stereoSpread_ * positionScatter_[i];
            }
            updatePanGains(i);
        }
    }

    /// @brief FR-015 fade band x MCF magnitude correction (plan §2). PER CHUNK,
    ///        because it depends on the drifting detune.
    ///
    /// The MCF correction is computed WITHOUT a cos call, via the identity
    /// `cos(pi*f/fs) = sqrt(1 - (eps/2)^2)` for `eps = 2*sin(pi*f/fs)`.
    ///
    /// @note This is deliberately NOT identical to the reference's
    ///       `cos(pi*f*detune/fs)` (`…bank.h:1073,1078`) — `detune*sin(theta) !=
    ///       sin(detune*theta)`, so the two agree only at `detune == 1`. The form
    ///       here corrects for the orbit the kernel ACTUALLY synthesizes, whose
    ///       half-angle is `asin(epsEff/2)` after the kernel's own +-1.99 clamp
    ///       (`…_simd.cpp:103`). Recorded as plan D5. Measured divergence at
    ///       fs = 48 kHz and the maximum 50 cents of drift: 0.005 dB at 7040 Hz,
    ///       2.09 dB at 19000 Hz.
    void recalculateAntiAliasing() noexcept {
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            updateAntiAliasGain(i);
        }
    }

    /// @brief One partial's FR-015 gain, on its CURRENT detune (plan §2, §4.2).
    /// @param index Partial index, < kMaxPartials
    /// @note Must be called AFTER `detuneMultiplier_[index]` is written for the
    ///       chunk, never before: both factors below are evaluated on the detuned
    ///       frequency, which is the only frequency the kernel actually synthesizes.
    void updateAntiAliasGain(std::size_t index) noexcept {
        const float epsEff = epsilon_[index] * detuneMultiplier_[index];  // the kernel's own product
        const float q = 0.5f * epsEff;
        const float oneMinusQSq = 1.0f - q * q;
        const float corr = (oneMinusQSq > 0.0f) ? std::sqrt(oneMinusQSq) : 0.0f;

        const float fEff = frequencyHz_[index] * detuneMultiplier_[index];
        float fade = 1.0f;
        if (fEff >= nyquist_) {
            fade = 0.0f;
        } else if (fEff > fadeStart_) {
            fade = (nyquist_ - fEff) * invFadeRange_;
        }

        antiAliasGain_[index] = fade * corr;
    }

    /// @brief Advance one partial's linear AR envelope by `dt` seconds (plan §4.4).
    ///
    /// FR-023's shape in full, and the whole of it: a constant-slope rise to 1 over
    /// `attackSec_ + attackOffsetSec_[i]`, a hold at 1 while the gate is on, and a
    /// constant-slope fall to 0 over `decaySec_ + decayOffsetSec_[i]` after
    /// gate-off. **Decay IS the release** — there is no separate release stage, no
    /// sustain level, no multi-stage segments, no velocity or key scaling and no
    /// exponential variant (Assumption 2 fixes the scope at exactly
    /// {attack, decay, offset spread, gate}).
    ///
    /// The attack time is a **time-to-100 %**, not a time constant, which is what
    /// makes SC-013 clause 3's ">= 95 % of target by `attack + offset`" reachable at
    /// all: the FR-014 smoother lags a linear ramp by one time constant, so the
    /// observed amplitude at `t = attackSec_i` is `1 - kAmpSmoothTimeSec/attackSec_i`
    /// — 0.960 at the `kMinAttackSec` floor of 50 ms, which is 25x the smoother's
    /// 2 ms and clears FR-023's ">= 20x" rule with margin (20x would land on exactly
    /// 0.95).
    ///
    /// Both divisors are >= kMinAttackSec / kMinDecaySec (0.05 s) because the
    /// setters clamp and the offsets are non-negative, so neither division can be
    /// by zero.
    ///
    /// A release entered mid-attack falls from wherever the envelope stands, and a
    /// note-on during a release re-opens from wherever it stands (`noteOn` never
    /// writes `envValue_`), so no stage transition steps the value — which is what
    /// keeps retrigger and note-off click-free by construction rather than by
    /// masking. The value is clamped into [0, 1] at every transition.
    ///
    /// @param index Partial index, < kMaxPartials
    /// @param dt    Chunk length in seconds
    /// @return The partial's envelope value in [0, 1] for this chunk
    [[nodiscard]] float advanceEnvelope(std::size_t index, float dt) noexcept {
        switch (envStage_[index]) {
            case kEnvStageAttack: {
                const float attackSec = attackSec_ + attackOffsetSec_[index];
                envValue_[index] += dt / attackSec;
                if (envValue_[index] >= 1.0f) {
                    envValue_[index] = 1.0f;
                    envStage_[index] = kEnvStageHold;
                }
                break;
            }
            case kEnvStageHold:
                envValue_[index] = 1.0f;
                break;
            case kEnvStageRelease: {
                const float decaySec = decaySec_ + decayOffsetSec_[index];
                envValue_[index] -= dt / decaySec;
                if (envValue_[index] <= 0.0f) {
                    envValue_[index] = 0.0f;
                    envStage_[index] = kEnvStageIdle;
                }
                break;
            }
            case kEnvStageIdle:
            default:
                envValue_[index] = 0.0f;
                break;
        }
        return envValue_[index];
    }

    /// @brief Once-per-chunk control pass (plan §4.2). The order is load-bearing.
    /// @param numSamples Chunk length in samples (<= kControlChunkSamples)
    void updateControl(std::size_t numSamples) noexcept {
        // Step 0: consume the config-rate dirty flags — at most one recompute of each
        // per chunk, no matter how many setters ran since the last chunk.
        if (freqDirty_) {
            recalculateFrequencies(true);
            freqDirty_ = false;
            markAmpDirty();  // a frequency recompute invalidates EVERY amplitude slot
        }
        if (ampDirty_) {
            recalculateAmplitudes();
            ampDirty_ = false;
        }

        // Step 1 (plan §4.2): both lane banks advance by exactly this chunk's
        // length, at their own 32-sample OU control rate (FR-032). The mutation
        // bank advances whatever the drift controls are set to — including a drift
        // depth of 0, which must not disable Mutation (FR-072).
        advanceDriftLanes(detuneLanes_, numSamples);
        advanceDriftLanes(mutationLanes_, numSamples);

        // Step 2 (plan §4.2): the drift read count is PER PARTIAL — one per chunk.
        // The quiescent early-out advances it identically, so a silent render and a
        // sounding render of the same length agree.
        ++driftReadCount_;

        // Step 3 (plan §4.2): per-partial detune and amplitude targets, reading each
        // lane's smoothed value EXACTLY ONCE per chunk and holding the resulting
        // multiplier constant across the chunk (FR-032). `gainSmoothed_` is the
        // value step 4 cached at the END of the PREVIOUS chunk, which is what makes
        // the normalizer a one-chunk-latency scalar rather than a per-partial cost.
        //
        // FR-023's per-partial envelope is evaluated HERE — by the cloud, once per
        // control chunk, and written into `targetAmplitude_[]`. It cannot live
        // downstream: the kernel's `ampSmoothCoeff` is a SINGLE SCALAR shared by
        // every partial (`harmonic_oscillator_bank_simd.h:43`), a uniform
        // de-zippering stage (FR-014) that cannot express per-partial timing at all.
        const float chunkSeconds = static_cast<float>(numSamples) * invSampleRate_;

        for (std::size_t i = 0; i < static_cast<std::size_t>(kernelCount_); ++i) {
            // FR-033. The clamp is `BrownianDrift::getCurrentValue()`'s own shape
            // (`brownian_drift.h:212-214`) — the source range is fixed at [-1, +1]
            // regardless of depth, so the cents bound is applied HERE by the cloud
            // and never by re-reading the source range. Since driftAmount_[i] <= 1
            // (FR-022), driftCents_ is a true upper bound over all partials.
            const float d = std::clamp(detuneLanes_.smoothCur[i], -1.0f, 1.0f);
            const float cents = driftCents_ * driftAmount_[i] * d;
            detuneMultiplier_[i] = detail::centsToDriftRatio(cents);

            // FR-034: `epsilon_` is deliberately NOT recomputed here. The kernel
            // forms `eps * detune` itself (`…_simd.cpp:103`), so the MCF state is
            // never stepped discontinuously and a detune change is phase-continuous
            // by construction — there is nothing for a crossfade to hide. Only the
            // FR-015 gain, which is evaluated on the DETUNED frequency, follows.
            updateAntiAliasGain(i);

            unmutatedTarget_[i] = gainSmoothed_ * baseAmplitude_[i];

            // FR-071/FR-072: the mutation weight comes from the SECOND lane bank,
            // read here on exactly the chunk cadence the detune read above uses, and
            // never from `d`. The two banks carry different derived seeds
            // (`deriveSeed(seed, i)` against `deriveSeed(seed, i + kMaxPartials)`),
            // the mutation bank's depth is pinned at 1 and its smoothness is fixed at
            // kMutationSmoothness — so drift depth 0 leaves Mutation fully alive and
            // a drift-smoothness change cannot touch it (SC-016's two independence
            // configurations). The clamp is `BrownianDrift::getCurrentValue()`'s own
            // shape, and with |dm| <= 1 and kMaxMutationDepth = 0.75 it is what makes
            // FR-073's [0.25, 1.75] bound exact rather than statistical.
            const float dm = std::clamp(mutationLanes_.smoothCur[i], -1.0f, 1.0f);

            // The `<= 0` branch is EXPLICIT, not an optimisation. SC-016 asserts the
            // weight is EXACTLY 1.0f at Mutation 0 — an assertion on the bits, not a
            // tolerance — and the general expression only delivers that as a
            // consequence of how the compiler contracts and rounds it (the macOS leg
            // builds -ffast-math, which is free to reassociate and to fuse the two
            // multiplies into an FMA). Taking the branch puts the guarantee in the
            // code instead of in the arithmetic, on every toolchain.
            const float w = (mutationAmount_ <= 0.0f)
                                ? 1.0f
                                : 1.0f + mutationAmount_ * kMaxMutationDepth * dm;

            // FR-023: the linear AR envelope, advanced by exactly this chunk's
            // duration. It is the LAST factor of the chain and, like `w`, sits
            // downstream of the FR-017 normalizer, whose input stays the un-mutated,
            // un-enveloped `a_i` set — so `getPartialUnmutatedTargetAmplitude(i) *
            // getPartialAntiAliasGain(i)` stays the fixed steady-state level SC-013's
            // four clauses are measured against even while `env` is moving.
            const float env = advanceEnvelope(i, chunkSeconds);

            // FR-008: masking is applied at the very END of the chain, so a masked or
            // soloed-out partial is faded by FR-014's smoother instead of being
            // switched off — solo cannot click.
            //
            // FR-071: `w` multiplies DOWNSTREAM of the FR-017 normalizer — its input
            // stays the un-mutated `a_i` set (see recalculateAmplitudes) — so the
            // level movement mutation creates survives to the output instead of being
            // cancelled by a gain recomputed from the mutated amplitudes.
            targetAmplitude_[i] = masked_[i] ? 0.0f : unmutatedTarget_[i] * w * env;
        }

        // Step 4 (plan §4.2): ONE advance for the whole chunk — not one per partial —
        // then cache the value the next chunk multiplies by. FR-017's target itself
        // is set at config rate, in step 0's recalculateAmplitudes().
        normGain_.advanceSamples(numSamples);
        gainSmoothed_ = normGain_.getCurrentValue();

        // Step 5 (plan §4.2): FR-043 tail retirement, then the §4.9 denormal guard.
        retireFadedTail();

        // Plan §4.9 / R7. THE GUARD GOES ON `currentAmplitude_`, WHICH IS THE STATE
        // THAT DECAYS — never on `targetAmplitude_`.
        //
        // Flushing `targetAmplitude_` would not prevent the denormal: the kernel's
        // amplitude-smoothing recurrence's state IS `currentAmplitude_`, and the
        // kernel never flushes it (`vAmp = hn::MulAdd(vCoeff, vDiff, vAmp)`,
        // `harmonic_oscillator_bank_simd.cpp:93`; scalar tail
        // `currentAmplitude[i] += ampSmoothCoeff * (target - currentAmplitude[i])`,
        // `:120`). Forcing a target to exactly 0 is in fact the CONDITION that
        // walks `currentAmplitude_` through the denormal range: at
        // kAmpSmoothTimeSec = 0.002 the per-sample retention is ~0.9896 at 48 kHz,
        // so ~8.4 k samples (~175 ms) after a target reaches 0 the state is
        // denormal and stays denormal for thousands of samples more.
        //
        // `retireFadedTail()` above only ever inspects `[activeCount_,
        // tailHighWater_)`. The two families that actually park denormals sit
        // INSIDE `[0, kernelCount_)` and are therefore invisible to it:
        //   - masked / soloed-out partials (target forced to 0 with the gate open);
        //   - every partial once noteOff() has completed its release
        //     (Idle => env 0 => target 0).
        // Hence the sweep below is over `kernelCount_`, i.e. everything the kernel
        // is actually handed, and uses the same 1e-8 idiom the reference applies to
        // its own tail (`harmonic_oscillator_bank.h:756`), generalised.
        for (std::size_t i = 0; i < static_cast<std::size_t>(kernelCount_); ++i) {
            if (targetAmplitude_[i] == 0.0f && currentAmplitude_[i] < kTailSilenceThreshold) {
                currentAmplitude_[i] = 0.0f;
            }
        }
    }

    /// @brief FR-017 normalizer target for the current base amplitudes (plan §3).
    ///
    /// Expected RMS of the summed partials, following the verified pattern at
    /// `harmonic_oscillator_bank.h:338-357`: a sinusoid of amplitude `a` has mean
    /// square `a^2/2`, so the expected summed RMS is `sqrt(sum a_i^2 * 0.5)`.
    [[nodiscard]] float currentNormGainTarget() const noexcept {
        float sumSquares = 0.0f;
        for (int i = 0; i < activeCount_; ++i) {
            const float a = baseAmplitude_[static_cast<std::size_t>(i)];
            sumSquares += a * a;
        }
        const float expectedRms = std::sqrt(sumSquares * 0.5f);
        if (expectedRms <= 0.0f) {
            return kMaxNormGain;  // unreachable while a_1 == 1, but never divide by 0
        }
        return std::min(kTargetOscRms / expectedRms, kMaxNormGain);
    }

    /// @brief FR-091's mandatory bipolar -> [0,1] remap before equalPowerGains,
    ///        which does NOT clamp its argument (`core/crossfade_utils.h:41`).
    ///
    /// The remap is not optional and not cosmetic. `equalPowerGains` is a
    /// CROSSFADE helper whose domain is [0, 1]; handing it an FR-021 position
    /// directly gives `panRight = sin(-pi/2) = -1` at position -1 — a full-level,
    /// phase-inverted right channel that still satisfies `L^2 + R^2 = 1` and would
    /// therefore pass an equal-power check. SC-012 clauses 2-4 exist for that bug.
    void updatePanGains(std::size_t i) noexcept {
        const float p01 = std::clamp((panPosition_[i] + 1.0f) * 0.5f, 0.0f, 1.0f);
        equalPowerGains(p01, panLeft_[i], panRight_[i]);

        // FR-091 admits NO polarity inversion, and the Layer 0 helper's own
        // endpoint violates that by ~4.4e-8: `kHalfPi` is `float(pi) / 2`, which
        // overshoots the true pi/2 by 4.37e-8 (`core/math_constants.h:28,36`), so
        // `cos(1.0f * kHalfPi)` returns -4.37e-8 rather than 0 and a hard-panned
        // partial comes back with a vanishing inverted channel. Flooring at zero
        // costs nothing audible — the clamped magnitude is ~145 dB below the
        // opposite channel, and `L^2 + R^2` is unchanged to within 1e-15 — while
        // letting SC-012 clause 2 assert the sign EXACTLY. A clause phrased as
        // "non-negative within a tolerance" would have to admit a tolerance
        // wider than nothing, and the bug it exists to reject produces -1.0.
        panLeft_[i] = std::max(panLeft_[i], 0.0f);
        panRight_[i] = std::max(panRight_[i], 0.0f);
    }

    // =========================================================================
    // Drift lane machinery (plan §4.5) — an SoA transposition of BrownianDrift
    // =========================================================================
    // FR-031 (as amended) requires the lanes' recurrence, coefficients, clamps and
    // 150 ms output smoother to BE `BrownianDrift`'s, not merely to resemble them.
    // The lanes are not instances of that class because 128 of them measured
    // 44,402 ns per 512-sample block against SC-007's own 35,533 ns baseline gate,
    // while this transposition costs 9,426 ns (plan §6.1, §6.4). The equivalence is
    // therefore MEASURED, by `HarmonicCloud_DriftLaneMatchesBrownianDrift`, which
    // drives a real `BrownianDrift` and a lane from the same seed, smoothness and
    // sample rate through an identical chunk schedule for 60 s on both banks. Any
    // edit below that changes a value sequence lands there.
    // =========================================================================

    /// @brief Re-derive a bank's AR(1) coefficients from its smoothness (plan §4.5).
    ///
    /// The exact discretisation of `dX = (1/tau)(0 - X)dt + sigma dW` over the
    /// fixed step `dt = kDriftControlInterval / fs`, transcribed from
    /// `BrownianDrift::updateCoefficients` (`brownian_drift.h:230-240`) — INCLUDING
    /// its double-precision intermediates. Computing tau/a/g in float instead would
    /// move the coefficients in the last bits and put
    /// `HarmonicCloud_DriftLaneMatchesBrownianDrift` near its 1e-5 tolerance for no
    /// reason: the walk is an AR(1) recursion, so a coefficient difference is
    /// re-applied at every one of the 90,000 control steps that test drives.
    ///
    /// @param bank       Lane bank to configure
    /// @param smoothness Correlation control in [0, 1]; tau = lerp(0.2 s, 30 s)
    void updateDriftCoefficients(DriftLanes& bank, float smoothness) noexcept {
        const double controlDtSeconds =
            static_cast<double>(kDriftControlInterval) / sampleRate_;
        const double tau = static_cast<double>(kDriftTauMin) +
                           static_cast<double>(smoothness) *
                               (static_cast<double>(kDriftTauMax) -
                                static_cast<double>(kDriftTauMin));
        const double a = std::exp(-controlDtSeconds / tau);
        bank.a = static_cast<float>(a);
        const double variance = 1.0 - (a * a);
        bank.g = static_cast<float>(static_cast<double>(kDriftInternalStd) *
                                    std::sqrt(variance > 0.0 ? variance : 0.0));
    }

    /// @brief One OU control step for every lane of a bank
    ///        (`BrownianDrift::advanceControlStep`, `brownian_drift.h:253-270`).
    ///
    /// The three `nextFloat()` draws are SEQUENCED into named locals: the operands
    /// of `+` are unsequenced in C++, so summing three calls inline would leave the
    /// draw order unspecified — and a lane whose draw order differs from
    /// `BrownianDrift`'s is a different random stream, not a rounding difference.
    ///
    /// Every lane of the bank steps, not just the `activeCount_` ones: a partial
    /// that Richness re-activates must find its walk where it would have been had
    /// it never gone quiet, not frozen at the value it held when it left.
    void advanceControlStepAllLanes(DriftLanes& bank) noexcept {
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            const float z0 = bank.rng[i].rng.nextFloat();
            const float z1 = bank.rng[i].rng.nextFloat();
            const float z2 = bank.rng[i].rng.nextFloat();
            const float z = z0 + z1 + z2;  // Irwin-Hall: zero-mean, unit-variance

            float x = (bank.a * bank.walk[i]) + (bank.g * z);  // mean is 0
            x = std::clamp(x, -kDriftWalkLimit, kDriftWalkLimit);
            if (x < kDriftDenormalFloor && x > -kDriftDenormalFloor) {
                x = 0.0f;
            }
            bank.walk[i] = x;

            // `BrownianDrift::outputTarget()` (`brownian_drift.h:249-251`) fed to
            // `OnePoleSmoother::setTarget`, which for a finite argument is a plain
            // assignment (`smoother.h:170-181`) — the clamp already guarantees finite.
            bank.smoothTgt[i] = std::clamp(bank.depth * x, -1.0f, 1.0f);
        }
    }

    /// @brief Advance every lane's 150 ms output one-pole by `numSamples`.
    ///
    /// THIS IS A TRANSCRIPTION OF `OnePoleSmoother::advanceSamples`
    /// (`smoother.h:243-254`), NOT the exponential identity. The naive
    /// `cur = tgt + (cur - tgt) * coeff^k` omits three operations that the real
    /// smoother performs, all three of them observable:
    ///   1. the `isComplete()` early RETURN, which leaves `current_` UNCHANGED —
    ///      it does not snap (`smoother.h:244`, :232-234);
    ///   2. `detail::flushDenormal` (:250);
    ///   3. a post-advance HARD SNAP to target below kCompletionThreshold (:251-253).
    /// (3) is a nonlinear, path-dependent step an order of magnitude larger than
    /// the equivalence gate's 1e-5 tolerance, and a 150 ms pole chasing a target
    /// that reverses every few control steps crosses 1e-4 constantly: the naive
    /// form measures up to 1.64e-4 of divergence, this one measures 0.000e+00.
    /// Writing (3) as a PRE-multiply snap is a different function again and
    /// reintroduces the divergence at exactly the points the gate samples.
    ///
    /// `coeff^N` is formed by the SAME expression `advanceSamples` uses —
    /// `std::pow(coefficient_, static_cast<float>(numSamples))` (`smoother.h:248`)
    /// — and NOT from a precomputed coeff^k table. That distinction is not a style
    /// choice, it is the equivalence itself. A `for (k) table[k] = std::pow(coeff,
    /// float(k))` loop is unrolled by /O2, which makes every exponent a compile-time
    /// constant, and under /fp:fast (this repo's MSVC setting, and -ffast-math on the
    /// macOS leg) the compiler then strength-reduces the constant-exponent `pow` into
    /// repeated multiplication. MEASURED: that table's entry for N = 32 is
    /// 0x1.f4bf56p-1 against powf's 0x1.f4bf5ep-1 — 4 ULP, which the 150 ms pole's
    /// 1/(1-coeff) = 1440-fold accumulation and the kCompletionThreshold snap below
    /// turn into 1.02e-4 of divergence from BrownianDrift, first observable at chunk 0
    /// and breaching this phase's 1e-5 equivalence gate by chunk 1372. The call is
    /// hoisted OUT of the 64-lane loop, so it costs 2 powf per bank per chunk.
    ///
    /// It is additionally MEMOISED on `numSamples` (`DriftLanes::cachedPowN`, see
    /// there). That is NOT the coeff^k table this paragraph rejects and the
    /// difference is exactly the one the paragraph turns on: the memo is filled by
    /// this same call site with the same runtime `numSamples`, so no exponent ever
    /// becomes a compile-time constant and there is nothing for /fp:fast to
    /// strength-reduce. `powf` is a pure function and `driftSmoothCoeff_` only moves
    /// in `prepare()` (which calls `reset()`, which clears the memo), so the float
    /// served is bit-for-bit the float the uncached form computed and the value
    /// sequence is unchanged by construction; `HarmonicCloud_DriftLaneMatchesBrownianDrift`
    /// is the standing check that it stays that way.
    ///
    /// What the memo removes is redundancy, not work that mattered: the caller only
    /// ever advances to a control boundary, so `numSamples` is 32 on every one of
    /// the 32 calls a 512-sample block makes, and the uncached form recomputed one
    /// value 32 times. MEASURED on the QUIESCENT path — which is the two lane banks
    /// and nothing else — 13,972 -> 12,421 ns/block on back-to-back builds. That
    /// 1,551 ns is 32 powf at ~48 ns, i.e. the measurement and the arithmetic agree,
    /// which is what makes it credible against that path's ~1,000 ns run-to-run
    /// spread.
    ///
    /// @param bank       Lane bank to advance
    /// @param numSamples Samples to advance; bounded by kDriftControlInterval,
    ///                   because the caller only ever advances to the next control
    ///                   boundary.
    void advanceSmootherAllLanes(DriftLanes& bank, int numSamples) noexcept {
        if (numSamples <= 0) {  // `advanceSamples(0)` is a no-op (smoother.h:244)
            return;
        }
        if (bank.cachedPowN != numSamples) {
            bank.cachedPowValue =
                std::pow(driftSmoothCoeff_, static_cast<float>(numSamples));  // smoother.h:248
            bank.cachedPowN = numSamples;
        }
        const float coeffN = bank.cachedPowValue;
        for (std::size_t i = 0; i < kMaxPartials; ++i) {
            const float diff0 = bank.smoothCur[i] - bank.smoothTgt[i];
            if (std::abs(diff0) < kCompletionThreshold) {
                continue;  // smoother.h:244 — SKIP this lane, do NOT snap it
            }
            bank.smoothCur[i] = bank.smoothTgt[i] + diff0 * coeffN;  // :247-249
            bank.smoothCur[i] = detail::flushDenormal(bank.smoothCur[i]);  // :250
            if (std::abs(bank.smoothCur[i] - bank.smoothTgt[i]) < kCompletionThreshold) {
                bank.smoothCur[i] = bank.smoothTgt[i];  // :251-253
            }
        }
    }

    /// @brief Advance a lane bank by `numSamples`, structurally mirroring
    ///        `BrownianDrift::processBlock` (`brownian_drift.h:194-206`).
    ///
    /// `samplesUntilControl` is SHARED across the bank's 64 lanes — every lane is
    /// advanced by the same sample counts, so one counter is both sufficient and
    /// correct. It is also what makes the lane state after N advanced samples a
    /// function of N alone and not of how N was partitioned (SC-015 clause 2a).
    ///
    /// A 64-sample control chunk therefore performs TWO internal OU steps at the
    /// 32-sample control-rate interval, not one: FR-032 decimates the cloud's READ
    /// of the lane, never the walk's own step rate, which is what keeps the
    /// autocorrelation time a property of seconds rather than of buffer length.
    void advanceDriftLanes(DriftLanes& bank, std::size_t numSamples) noexcept {
        auto remaining = static_cast<int>(numSamples);
        while (remaining > 0) {
            if (bank.samplesUntilControl <= 0) {
                bank.samplesUntilControl = kDriftControlInterval;
                advanceControlStepAllLanes(bank);
            }
            const int advance = std::min(remaining, bank.samplesUntilControl);
            bank.samplesUntilControl -= advance;
            remaining -= advance;
            advanceSmootherAllLanes(bank, advance);
        }
    }

    /// @brief Zero a lane bank's state without touching its coefficients or seeds.
    static void resetDriftLanes(DriftLanes& bank) noexcept {
        bank.walk.fill(0.0f);
        bank.smoothCur.fill(0.0f);
        bank.smoothTgt.fill(0.0f);
        bank.samplesUntilControl = 0;
        bank.cachedPowN = 0;  // driftSmoothCoeff_ may have moved in prepare()
        bank.cachedPowValue = 0.0f;
    }

    // =========================================================================
    // State (plan §1.3) — SoA, alignas(32) for LOCALITY only. The kernel uses
    // hn::LoadU / hn::StoreU, so nothing here may assume alignment.
    // =========================================================================

    // --- kernel-facing SoA (exact parameter contract of processMcfBatchSIMD) ---
    alignas(32) std::array<float, kMaxPartials> sinState_{};          ///< FR-016: seeded, NOT zeroed
    alignas(32) std::array<float, kMaxPartials> cosState_{};
    alignas(32) std::array<float, kMaxPartials> epsilon_{};           ///< 2*sin(pi*f_i/fs), clamped +-1.99
    alignas(32) std::array<float, kMaxPartials> detuneMultiplier_{};  ///< per-chunk, from drift
    alignas(32) std::array<float, kMaxPartials> currentAmplitude_{};  ///< kernel-owned
    alignas(32) std::array<float, kMaxPartials> targetAmplitude_{};   ///< gain*a_i*w_i*env_i
    alignas(32) std::array<float, kMaxPartials> antiAliasGain_{};     ///< fade x MCF correction
    alignas(32) std::array<float, kMaxPartials> panLeft_{};
    alignas(32) std::array<float, kMaxPartials> panRight_{};

    // --- per-partial living state (FR-021..FR-024) ---
    alignas(32) std::array<float, kMaxPartials> frequencyHz_{};     ///< FR-083 law, undetuned
    alignas(32) std::array<float, kMaxPartials> baseAmplitude_{};   ///< a_i = rolloff*tilt
    alignas(32) std::array<float, kMaxPartials> unmutatedTarget_{}; ///< gainSmoothed*a_i
    alignas(32) std::array<float, kMaxPartials> panPosition_{};     ///< FR-021, [-1,+1]
    alignas(32) std::array<float, kMaxPartials> positionScatter_{}; ///< s_i ~ U[-1,1], once per seed
    std::array<bool, kMaxPartials> positionOverridden_{};           ///< FR-008 setPartialPosition
    alignas(32) std::array<float, kMaxPartials> driftAmount_{};     ///< amount_i (FR-022)
    alignas(32) std::array<float, kMaxPartials> attackOffsetSec_{}; ///< FR-023
    alignas(32) std::array<float, kMaxPartials> decayOffsetSec_{};
    alignas(32) std::array<float, kMaxPartials> attackOffsetDraw_{}; ///< oa_i ~ U[0,1], per seed
    alignas(32) std::array<float, kMaxPartials> decayOffsetDraw_{};  ///< od_i ~ U[0,1], per seed
    alignas(32) std::array<float, kMaxPartials> envValue_{};        ///< [0,1]
    std::array<std::uint8_t, kMaxPartials> envStage_{};             ///< Idle/Attack/Hold/Release
    std::array<bool, kMaxPartials> masked_{};                       ///< FR-008 solo/mask

    // --- FR-081 spectral-target injection (Phase 3, plan §6.1) ---
    alignas(32) std::array<float, kMaxPartials> targetRatio_{};     ///< latest supplied
    alignas(32) std::array<float, kMaxPartials> targetAmp_{};
    /// The values the LAST RECOMPUTE ACTUALLY CONSUMED. The dirty test in
    /// setSpectralTarget compares against THESE, never against
    /// targetRatio_/targetAmp_ (deviation D14). Written only inside
    /// recalculateFrequencies()/recalculateAmplitudes(), and only for slots those
    /// functions actually recomputed, so the comparison baseline can never drift
    /// away from the values the audio path is using. reset() marks every slot
    /// dirty, so they re-synchronise there and need no separate clearing.
    alignas(32) std::array<float, kMaxPartials> committedRatio_{};
    alignas(32) std::array<float, kMaxPartials> committedAmp_{};
    std::uint64_t freqSlotDirty_ = 0;  ///< FR-085 lever 3, one bit per partial
    std::uint64_t ampSlotDirty_ = 0;
    /// Length of the array pair that produced targetRatio_/targetAmp_. FR-085
    /// lever 1's skip requires the SAME count as well as the same bytes: slots at
    /// or above `count` are filled by this class (ratio i+1, amplitude 0), so two
    /// calls with different counts can agree on their first `count` floats and
    /// still describe different spectra.
    std::size_t targetCount_ = 0;
    /// NOT cleared by reset() — a spectral target is CONFIGURATION, like Richness.
    bool hasTarget_ = false;

    // --- two drift lane banks (plan §4.5) ---
    DriftLanes detuneLanes_{};    ///< FR-031 bank
    DriftLanes mutationLanes_{};  ///< FR-072 bank
    /// The 150 ms output-smoother coefficient shared by both banks, from the single
    /// source of truth (`smoother.h:77-93`) so the lanes hold exactly the float
    /// `OnePoleSmoother::configure` would have stored.
    float driftSmoothCoeff_ = 0.0f;

    // --- cloud-level scalars ---
    double sampleRate_ = 44100.0;
    float nyquist_ = 0.0f;
    float invSampleRate_ = 0.0f;
    float fadeStart_ = 0.0f;
    float invFadeRange_ = 0.0f;
    float ampSmoothCoeff_ = 0.0f;
    int activeCount_ = 1;
    int kernelCount_ = 1;   ///< >= activeCount_ (tail high-water, FR-043)
    int tailHighWater_ = 1;
    bool freqDirty_ = false;
    bool ampDirty_ = false;
    OnePoleSmoother normGain_;    ///< FR-017, ONE instance
    float gainSmoothed_ = 1.0f;   ///< normGain_ value cached by the PREVIOUS chunk
    Xorshift32 configRng_{1};   ///< per-seed draws, fixed order (plan §4.6)
    Xorshift32 phaseRng_{1};    ///< FR-016 phase redraws only
    std::uint32_t configuredSeed_ = kDefaultCloudSeed;
    bool prepared_ = false;
    bool gate_ = false;
    float lastOutL_ = 0.0f;
    float lastOutR_ = 0.0f;
    float crossfadeOldL_ = 0.0f;
    float crossfadeOldR_ = 0.0f;
    int crossfadeRemaining_ = 0;
    int crossfadeLengthSamples_ = 1;
    float crossfadeThresholdRatio_ = 1.0f;
    std::uint64_t driftReadCount_ = 0;  ///< SC-015 clause 2(b) test hook

    // --- parameter shadows (FR-003 defaults: neutral everywhere but f0/richness) ---
    float fundamentalHz_ = 220.0f;
    /// Full Richness by default: N(1) = 64, so every partial index is active
    /// before any setter runs. Two consequences depend on it. (1) FR-008's
    /// solo/mask facility — which is also SC-001's and SC-002's measurement tool —
    /// works on any index without first raising Richness; at r = 0.5 only 8
    /// partials exist and `soloPartial(63)` would render silence. (2) It preserves
    /// the behaviour the pre-T004 interim chain shipped (`activeCount_ =
    /// kMaxPartials`), which those two criteria were written against. Nothing in
    /// the criteria depends on the default being anything else: every Richness
    /// criterion sets the value explicitly.
    float richness_ = 1.0f;
    float inharmonicity_ = 0.0f;
    float tiltDb_ = 0.0f;
    float mutationAmount_ = 0.0f;
    float gravity_ = 0.0f;
    float driftCents_ = 0.0f;
    float driftSmoothness_ = 0.5f;
    float stereoSpread_ = 0.0f;
    float attackSec_ = 0.05f;
    float decaySec_ = 0.5f;
    float offsetSpread_ = 0.0f;
};

}  // namespace Krate::DSP

#ifdef _MSC_VER
#pragma warning(pop)
#endif
