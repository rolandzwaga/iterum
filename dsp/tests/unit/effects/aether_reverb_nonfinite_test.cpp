// ==============================================================================
// Layer 4: Effect Tests - AetherReverb, non-finite hygiene (SC-014)
//                                        (specs/seraphis-phase6-aether-space)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase6-aether-space/spec.md
//            specs/seraphis-phase6-aether-space/plan.md   (S1.2 item 2, S7.14,
//                                                          S8.6)
//            specs/seraphis-phase6-aether-space/tasks.md  (T001 creates this TU;
//                                                          T012 fills it)
//
// SCOPE OF THIS TU (plan S1.1's TU-ownership table): SC-014 - including the
//   FR-009/FR-008 setter-guard clause (every float setter driven with
//   bit-pattern NaN, +/-Inf and +/-1e9) and AetherReverb_BloomNoteApi.
//
// THIS IS A SEPARATE TU BECAUSE OF ITS COMPILE FLAGS. It is the ONLY one of the
//   five Phase 6 TUs listed under "-fno-fast-math -fno-finite-math-only" in
//   dsp/tests/CMakeLists.txt, and those flags must NOT be applied to the other
//   four. The main and spectral TUs are deliberately absent so the header's
//   ITERUM_NOINLINE guards are proved in the FP mode it actually ships in on the
//   macOS leg (plan R-5); the perf TU is absent because -fno-fast-math would
//   change the figures its baselines are pinned to.
//
// CONSTRUCTING NON-FINITE VALUES: never std::numeric_limits<float>::quiet_NaN()
//   or infinity(), and never std::isnan / std::isinf. Build the values from bit
//   patterns through a VOLATILE sink. Classify with Krate::DSP::detail::isNaN
//   (core/db_utils.h:54) and Krate::DSP::detail::isInf (:175).
//
// FAULT INJECTION: reaching AetherReverb's INTERNAL non-finite path needs the
//   KRATE_DSP_AETHER_TEST_HOOKS-gated injectNonFiniteStateForTest() - every
//   input path is sealed (FR-082, the setter contract, bloomNoteOn's clamp) and
//   FR-025 + FR-032 make the unfrozen loop structurally non-expansive. The macro
//   is defined TARGET-WIDE on dsp_effects_tests, never per-source.
//
// ALLOCATION DETECTION: include <allocation_detector.h> ONLY. The global
//   operator new/delete override (<allocation_operator_overrides.h>) has exactly
//   ONE owner per test image; a second include is a duplicate-symbol link error.
//   Without that owner in dsp_effects_tests the counter reads 0 unconditionally,
//   so the bracketing idiom below is written for correctness of FORM - it is the
//   ASan lane and the out-of-bounds-write clause that carry the teeth here.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <allocation_detector.h>
#include <render_fingerprint.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/effects/aether_reverb.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using Krate::DSP::AetherReverb;

// The macro is set with target_compile_definitions on dsp_effects_tests
// (dsp/tests/CMakeLists.txt:409). Failing the build here - rather than silently
// degenerating SC-014 clause 3 into a second measurement of clause 2 - is the
// point: without the hook, FR-083's detect -> emergencyClear -> counter branch
// is unreachable and the clause cannot be written at all.
#ifndef KRATE_DSP_AETHER_TEST_HOOKS
#error "dsp_effects_tests must define KRATE_DSP_AETHER_TEST_HOOKS target-wide (plan S1.2 item 4)"
#endif

// ------------------------------------------------------------------------------
// T001 smoke case: the FR-083 recovery counter exists, is zero on a freshly
// prepared engine, and this TU compiles under IEEE semantics.
// ------------------------------------------------------------------------------
TEST_CASE("AetherReverb_NonFiniteSmoke", "[effects][aether]") {
    AetherReverb r;
    r.prepare(48000.0, AetherReverb::PrepareConfig{});
    REQUIRE(r.isPrepared());
    REQUIRE(r.getNonFiniteRecoveryCount() == 0u);
}

// ==============================================================================
// T012 fixtures
// ==============================================================================

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;

/// One control chunk. Rendering in exactly this block size is what lets the
/// fault land on the hook's documented control-chunk-boundary precondition and
/// what makes the recovery point observable to +/- one chunk.
constexpr std::size_t kBlock = AetherReverb::kControlChunkSamples;  // 64

constexpr std::size_t kOneSecond = 48000;

/// Plan S8.6 clause 3: the fault is at t_f = 3.0 s of a 10 s render.
/// 3.0 s at 48 kHz is sample 144000 == 64 * 2250, exactly a chunk boundary.
constexpr std::size_t kFaultSample = 3u * kOneSecond;
constexpr std::size_t kFaultRenderSamples = 10u * kOneSecond;
static_assert((kFaultSample % kBlock) == 0u,
              "the hook's precondition is a control-chunk boundary");
static_assert((kFaultRenderSamples % kBlock) == 0u, "whole number of control chunks");

/// The three bit patterns. Kept as named constants rather than literals at the
/// injection sites so the pattern -> meaning mapping is stated exactly once.
constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// std::numeric_limits<float>::quiet_NaN() / infinity() fold to finite garbage
/// under -ffast-math. Although THIS TU carries -fno-fast-math, the construction
/// is written the portable way regardless: the values it produces are handed to
/// a header that the other four Phase 6 TUs compile WITH fast-math, and one
/// construction that is valid in both modes is the point.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b = bits;        // defeats constant folding
    const std::uint32_t materialized = b;   // the volatile READ is the sink
    float f = 0.0f;
    std::memcpy(&f, &materialized, sizeof(f));
    return f;
}

/// @brief FR-008's finiteness test as a COMPOSITION of the two Layer 0
///        exponent-field bit tests (core/db_utils.h:54, :175).
[[nodiscard]] bool sampleIsFinite(float value) noexcept {
    return !Krate::DSP::detail::isNaN(value) && !Krate::DSP::detail::isInf(value);
}

// ---- G-1 (plan S8.1) ---------------------------------------------------------

/// @brief G-1: 220 Hz + 2x .. 9x at 1/n, all sine, zero phase, peak 0.5.
[[nodiscard]] std::vector<float> makeHarmonicStack(std::size_t numSamples, double sampleRate) {
    std::vector<float> out(numSamples, 0.0f);
    double peak = 0.0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        double v = 0.0;
        for (std::size_t h = 1; h <= 9u; ++h) {
            const double f = 220.0 * static_cast<double>(h);
            v += std::sin(2.0 * kPiD * f * static_cast<double>(i) / sampleRate) /
                 static_cast<double>(h);
        }
        out[i] = static_cast<float>(v);
        peak = std::max(peak, std::abs(v));
    }
    if (peak > 0.0) {
        const auto g = static_cast<float>(0.5 / peak);
        for (auto& v : out) {
            v *= g;
        }
    }
    return out;
}

struct StereoRender {
    std::vector<float> l;
    std::vector<float> r;
    /// First sample index at which the engine reported it had finished
    /// recovering, resolved to one control chunk. 0 means "never entered
    /// recovery", which the fault render asserts against.
    std::size_t recoverySample = 0;
    bool sawRecovering = false;
};

/// @brief Plan S8.6's pinned SC-014 configuration.
///
/// P-1 (all three life-modulation depths at 0), P-3 (mix = 1, wet only), and the
/// clause-3 operating point: decay 4 s, damping 0.4, size 0.5, dimensionality
/// 0.35, every send 0 and spectral diffusion 0. P-2 is the PrepareConfig below
/// plus the getMaxSizeScale() assertion at the call sites.
void applyPinnedConfig(AetherReverb& engine) {
    engine.setSize(0.5f);
    engine.setDecaySeconds(4.0f);
    engine.setDamping(0.4f);
    engine.setDimensionality(0.35f);
    engine.setShimmerOctaveSend(0.0f);
    engine.setShimmerFifthSend(0.0f);
    engine.setBloomSend(0.0f);
    engine.setSpectralDiffusion(0.0f);
    // P-1
    engine.setSizeBreathDepth(0.0f);
    engine.setDimensionalityTideDepth(0.0f);
    engine.setModDepth(0.0f);
    // P-3
    engine.setMix(1.0f);
}

/// P-2: maxDelaySeconds = 0.5f, N = 8.
[[nodiscard]] AetherReverb::PrepareConfig pinnedPrepareConfig() {
    AetherReverb::PrepareConfig cfg;
    cfg.numChannels = 8;
    cfg.maxBlockSamples = kBlock;
    cfg.maxDelaySeconds = 0.5f;
    return cfg;
}

/// @brief Render @p input in kBlock-sized chunks, optionally injecting the
///        FR-083 internal fault at the kFaultSample chunk boundary.
///
/// The injection happens BETWEEN processStereoBlock() calls whose cumulative
/// sample count is 144000 - the hook's documented precondition - so the FR-083
/// sweep fires at the top of the next control step, before any sample of that
/// chunk is rendered.
[[nodiscard]] StereoRender renderBlocks(AetherReverb& engine, const std::vector<float>& input,
                                        bool injectFault) {
    StereoRender out;
    out.l.resize(input.size(), 0.0f);
    out.r.resize(input.size(), 0.0f);

    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);

    std::size_t done = 0;
    while (done < input.size()) {
        const std::size_t nb = std::min(kBlock, input.size() - done);

        if (injectFault && (done == kFaultSample)) {
            engine.injectNonFiniteStateForTest();
        }

        for (std::size_t k = 0; k < nb; ++k) {
            inL[k] = input[done + k];
            inR[k] = input[done + k];
        }
        engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), nb);
        for (std::size_t k = 0; k < nb; ++k) {
            out.l[done + k] = outL[k];
            out.r[done + k] = outR[k];
        }
        done += nb;

        if (engine.isRecovering()) {
            out.sawRecovering = true;
        } else if (out.sawRecovering && (out.recoverySample == 0u)) {
            out.recoverySample = done;
        }
    }
    return out;
}

/// @brief Mean-square level of both channels over [begin, end), in dB.
[[nodiscard]] double windowRmsDb(const StereoRender& r, std::size_t begin, std::size_t end) {
    double acc = 0.0;
    std::size_t n = 0;
    const std::size_t stop = std::min(end, r.l.size());
    for (std::size_t i = begin; i < stop; ++i) {
        acc += (static_cast<double>(r.l[i]) * static_cast<double>(r.l[i])) +
               (static_cast<double>(r.r[i]) * static_cast<double>(r.r[i]));
        n += 2u;
    }
    if (n == 0u) {
        return -300.0;
    }
    return 10.0 * std::log10(std::max(acc / static_cast<double>(n), 1e-300));
}

[[nodiscard]] std::size_t countNonFinite(const StereoRender& r) {
    std::size_t bad = 0;
    for (std::size_t i = 0; i < r.l.size(); ++i) {
        if (!sampleIsFinite(r.l[i]) || !sampleIsFinite(r.r[i])) {
            ++bad;
        }
    }
    return bad;
}

[[nodiscard]] float peakAbs(const StereoRender& r) {
    float p = 0.0f;
    for (std::size_t i = 0; i < r.l.size(); ++i) {
        p = std::max(p, std::abs(r.l[i]));
        p = std::max(p, std::abs(r.r[i]));
    }
    return p;
}

/// @brief Drive EVERY float setter in FR-009's table with @p value.
///
/// The list is the one plan S8.6 clause 4 enumerates, in the same order. All 17
/// of them: a setter added to FR-009's table without a line here is a setter
/// whose ITERUM_NOINLINE guard is never exercised with a non-finite argument.
void applyEverySetter(AetherReverb& engine, float value) {
    engine.setSize(value);
    engine.setDensity(value);
    engine.setDecaySeconds(value);
    engine.setDimensionality(value);
    engine.setDamping(value);
    engine.setPreDelayMs(value);
    engine.setModDepth(value);
    engine.setModSmoothness(value);
    engine.setShimmerOctaveSend(value);
    engine.setShimmerFifthSend(value);
    engine.setBloomSend(value);
    engine.setBloomDecay(value);
    engine.setSpectralDiffusion(value);
    engine.setSizeBreathDepth(value);
    engine.setDimensionalityTideDepth(value);
    engine.setWidth(value);
    engine.setMix(value);
}

/// @brief The +/-1e9 sub-case: alternating sign, setter by setter.
void applyEverySetterAlternating(AetherReverb& engine, float magnitude) {
    int i = 0;
    const auto v = [&i, magnitude]() {
        const float s = ((i++ % 2) == 0) ? 1.0f : -1.0f;
        return s * magnitude;
    };
    engine.setSize(v());
    engine.setDensity(v());
    engine.setDecaySeconds(v());
    engine.setDimensionality(v());
    engine.setDamping(v());
    engine.setPreDelayMs(v());
    engine.setModDepth(v());
    engine.setModSmoothness(v());
    engine.setShimmerOctaveSend(v());
    engine.setShimmerFifthSend(v());
    engine.setBloomSend(v());
    engine.setBloomDecay(v());
    engine.setSpectralDiffusion(v());
    engine.setSizeBreathDepth(v());
    engine.setDimensionalityTideDepth(v());
    engine.setWidth(v());
    engine.setMix(v());
}

}  // namespace

// ==============================================================================
// SC-014 - non-finite hygiene (plan S8.6)
// ==============================================================================
TEST_CASE("AetherReverb_NonFiniteHygiene", "[effects][aether]") {
    const std::vector<float> g1 = makeHarmonicStack(kFaultRenderSamples, kSampleRate);
    const auto cfg = pinnedPrepareConfig();

    // --------------------------------------------------------------------------
    // Clause 2 (FIRST, because it also establishes the FR-082 path): injecting
    // NON-FINITE INPUT must be silently replaced with 0.0f and must NOT count as
    // an FR-083 recovery. FR-082 is not FR-083.
    // --------------------------------------------------------------------------
    {
        AetherReverb engine;
        engine.prepare(kSampleRate, cfg);
        REQUIRE(engine.getMaxSizeScale() == 4.0f);  // P-2
        applyPinnedConfig(engine);

        std::vector<float> poisoned(2u * kOneSecond, 0.0f);
        for (std::size_t i = 0; i < poisoned.size(); ++i) {
            poisoned[i] = g1[i];
        }
        // Every 1000th sample is non-finite, cycling NaN / +Inf / -Inf, so both
        // the isNaN and the isInf legs of the guard are driven and no single
        // control chunk is left untouched for long.
        const std::uint32_t cycle[3] = {kQuietNaNBits, kPosInfBits, kNegInfBits};
        for (std::size_t i = 500; i < poisoned.size(); i += 1000u) {
            poisoned[i] = makeNonFinite(cycle[(i / 1000u) % 3u]);
        }

        const StereoRender out = renderBlocks(engine, poisoned, /*injectFault=*/false);

        CHECK(countNonFinite(out) == 0u);
        CHECK(engine.getNonFiniteRecoveryCount() == 0u);
        CHECK_FALSE(out.sawRecovering);
    }

    // --------------------------------------------------------------------------
    // Clauses 1 + 3: recovery from an INTERNAL fault, injected through the
    // KRATE_DSP_AETHER_TEST_HOOKS hook at t_f = 3.0 s of a 10 s render.
    // --------------------------------------------------------------------------
    {
        AetherReverb reference;
        reference.prepare(kSampleRate, cfg);
        REQUIRE(reference.getMaxSizeScale() == 4.0f);  // P-2
        applyPinnedConfig(reference);
        const StereoRender ref = renderBlocks(reference, g1, /*injectFault=*/false);

        AetherReverb subject;
        subject.prepare(kSampleRate, cfg);
        applyPinnedConfig(subject);
        REQUIRE(subject.getNonFiniteRecoveryCount() == 0u);
        const StereoRender sub = renderBlocks(subject, g1, /*injectFault=*/true);

        // Clause 1: no non-finite value ever reaches the output, at any point -
        // neither in the clean reference nor in the faulted subject.
        CHECK(countNonFinite(ref) == 0u);
        CHECK(countNonFinite(sub) == 0u);
        CHECK_FALSE(ref.sawRecovering);

        // Clause 3: exactly ONE recovery, and it terminated.
        REQUIRE(subject.getNonFiniteRecoveryCount() == 1u);
        REQUIRE(sub.sawRecovering);
        REQUIRE(sub.recoverySample > kFaultSample);
        REQUIRE(sub.recoverySample < (kFaultSample + kOneSecond));

        WARN("SC-014 clause 3: fault at " << kFaultSample << ", recovery point at "
                                          << sub.recoverySample << " ("
                                          << (1000.0 *
                                              static_cast<double>(sub.recoverySample -
                                                                  kFaultSample) /
                                              kSampleRate)
                                          << " ms after the fault)");

        // Five 100 ms windows ENDING at recovery + 0.6 .. 1.0 s. The reference is
        // read at the SAME ABSOLUTE indices: it never faulted, so its tail is
        // stationary and absolute alignment is the honest alignment.
        constexpr std::size_t kWindowSamples = kOneSecond / 10u;  // 100 ms
        constexpr std::size_t kNumWindows = 5;
        double diffDb[kNumWindows] = {};
        double subDb[kNumWindows] = {};
        double refDb[kNumWindows] = {};
        for (std::size_t w = 0; w < kNumWindows; ++w) {
            const std::size_t endOffset = ((6u + w) * kOneSecond) / 10u;  // 0.6 .. 1.0 s
            const std::size_t end = sub.recoverySample + endOffset;
            REQUIRE(end <= kFaultRenderSamples);
            const std::size_t begin = end - kWindowSamples;
            subDb[w] = windowRmsDb(sub, begin, end);
            refDb[w] = windowRmsDb(ref, begin, end);
            diffDb[w] = std::abs(subDb[w] - refDb[w]);
        }

        WARN("SC-014 clause 3 windows (dB subject / reference / |diff|): "
             << subDb[0] << " / " << refDb[0] << " / " << diffDb[0] << " | " << subDb[1] << " / "
             << refDb[1] << " / " << diffDb[1] << " | " << subDb[2] << " / " << refDb[2] << " / "
             << diffDb[2] << " | " << subDb[3] << " / " << refDb[3] << " / " << diffDb[3] << " | "
             << subDb[4] << " / " << refDb[4] << " / " << diffDb[4]);

        // (a) the last window is non-zero and within +/- 3 dB of the reference.
        CHECK(subDb[kNumWindows - 1] > -200.0);
        CHECK(diffDb[kNumWindows - 1] <= 3.0);

        // (b) the difference shrinks monotonically over THE FOUR WINDOWS
        //     PRECEDING the clause-(a) window - i.e. those ending at recovery +
        //     0.6/0.7/0.8/0.9 s, three transitions. The clause-(a) window itself
        //     is NOT part of the chain: spec.md:1982-1984 pairs "(a) ... the
        //     window ending at recovery + 1.0 s" with "(b) ... the four 100 ms
        //     windows preceding it", spec.md:1965-1966 budgets the render as
        //     "the four 100 ms convergence windows PLUS the 1.0 s measurement
        //     point", and plan.md:1672 / tasks.md:978 repeat "the four preceding
        //     100 ms windows" verbatim. Extending the chain onto window 4 would
        //     assert something no artifact asks for, and something an LTI
        //     rebuild cannot satisfy in general: the subject starts from a zeroed
        //     state at the recovery point, so the residual is the not-yet-built
        //     part of the steady state, which approaches zero as a DECAYING
        //     OSCILLATION rather than monotonically - |diff| in dB therefore
        //     crosses zero and comes back up on the far side of the crossing.
        //     (Measured here: 1.642 -> 1.100 -> 0.389 -> 0.010 dB across the four
        //     convergence windows, then 0.110 dB at the crossing's far side in
        //     the clause-(a) window. Both figures are far inside clause (a)'s
        //     3 dB bound and the recorded 1 dB convergence mark.) Clause (a)
        //     already bounds window 4 in absolute terms, which is the criterion
        //     of record for it.
        //     The slack is one hundredth of the clause-(a) bound: it absorbs the
        //     window-to-window ripple of a periodic excitation in a diffuse tail
        //     without admitting a non-converging implementation.
        constexpr double kMonotoneSlackDb = 0.05;
        constexpr std::size_t kNumConvergenceWindows = kNumWindows - 1u;  // the four preceding
        for (std::size_t w = 0; w + 1u < kNumConvergenceWindows; ++w) {
            INFO("window " << w << " -> " << (w + 1u) << ": " << diffDb[w] << " -> "
                           << diffDb[w + 1u]);
            CHECK(diffDb[w + 1u] <= (diffDb[w] + kMonotoneSlackDb));
        }

        // RECORDED, NOT THRESHOLDED: how the clause-(a) window sits relative to
        // the last convergence window. A genuinely diverging implementation would
        // show this growing while clause (a) still passed on a lucky window, so
        // the figure is printed on every run even though (b) does not span it.
        WARN("SC-014 clause 3 clause-(a) window vs last convergence window (|diff| dB): "
             << diffDb[kNumConvergenceWindows - 1u] << " -> " << diffDb[kNumWindows - 1u]);

        // RECORDED, NOT THRESHOLDED (plan S8.6 clause 3).
        std::size_t convergedWindow = kNumWindows;
        for (std::size_t w = 0; w < kNumWindows; ++w) {
            if (diffDb[w] <= 1.0) {
                convergedWindow = w;
                break;
            }
        }
        if (convergedWindow < kNumWindows) {
            WARN("SC-014 clause 3 MEASURED convergence (|diff| <= 1 dB) by "
                 << (600u + (100u * convergedWindow)) << " ms after the recovery point");
        } else {
            WARN("SC-014 clause 3 MEASURED convergence: |diff| still > 1 dB at "
                 "recovery + 1.0 s (" << diffDb[kNumWindows - 1] << " dB)");
        }
    }

    // --------------------------------------------------------------------------
    // Clause 4(a): NaN into EVERY setter must fall back to the FR-009 DEFAULT.
    // The fingerprint equality against an engine with NO SETTER EVER CALLED is
    // what distinguishes "fell back to the default" from "landed on a clamp
    // endpoint" - a clamp would change the render, a default cannot.
    // --------------------------------------------------------------------------
    const std::vector<float> g1OneSecond = makeHarmonicStack(kOneSecond, kSampleRate);
    StereoRender defaultsRender;
    {
        AetherReverb engine;
        engine.prepare(kSampleRate, cfg);
        defaultsRender = renderBlocks(engine, g1OneSecond, /*injectFault=*/false);
        REQUIRE(countNonFinite(defaultsRender) == 0u);
        REQUIRE(engine.getNonFiniteRecoveryCount() == 0u);
    }
    {
        AetherReverb engine;
        engine.prepare(kSampleRate, cfg);
        applyEverySetter(engine, makeNonFinite(kQuietNaNBits));
        const StereoRender out = renderBlocks(engine, g1OneSecond, /*injectFault=*/false);

        CHECK(countNonFinite(out) == 0u);
        CHECK(engine.getNonFiniteRecoveryCount() == 0u);

        const auto cl = Krate::DSP::TestUtils::compareFingerprints(
            Krate::DSP::TestUtils::fingerprintRender(out.l),
            Krate::DSP::TestUtils::fingerprintRender(defaultsRender.l));
        const auto cr = Krate::DSP::TestUtils::compareFingerprints(
            Krate::DSP::TestUtils::fingerprintRender(out.r),
            Krate::DSP::TestUtils::fingerprintRender(defaultsRender.r));
        INFO("L{metric=" << cl.worstMetricRelativeError << " sample=" << cl.worstSampleError << " "
                         << cl.detail << "}  R{metric=" << cr.worstMetricRelativeError
                         << " sample=" << cr.worstSampleError << " " << cr.detail << "}");
        CHECK(cl.withinTolerance());
        CHECK(cr.withinTolerance());
    }

    // --------------------------------------------------------------------------
    // Clause 4(b): +Inf, -Inf and +/-1e9 CLAMP to the range endpoints, so no
    // fingerprint equality is asserted - only that nothing non-finite escapes,
    // that FR-083 never fires, and that the engine stays bounded.
    // --------------------------------------------------------------------------
    {
        AetherReverb engine;
        engine.prepare(kSampleRate, cfg);
        applyEverySetter(engine, makeNonFinite(kPosInfBits));
        const StereoRender out = renderBlocks(engine, g1OneSecond, /*injectFault=*/false);
        INFO("+Inf sub-case peak=" << peakAbs(out));
        CHECK(countNonFinite(out) == 0u);
        CHECK(engine.getNonFiniteRecoveryCount() == 0u);
        CHECK(peakAbs(out) <= 4.0f);
    }
    {
        AetherReverb engine;
        engine.prepare(kSampleRate, cfg);
        applyEverySetter(engine, makeNonFinite(kNegInfBits));
        const StereoRender out = renderBlocks(engine, g1OneSecond, /*injectFault=*/false);
        INFO("-Inf sub-case peak=" << peakAbs(out));
        CHECK(countNonFinite(out) == 0u);
        CHECK(engine.getNonFiniteRecoveryCount() == 0u);
        CHECK(peakAbs(out) <= 4.0f);
    }
    {
        AetherReverb engine;
        engine.prepare(kSampleRate, cfg);
        applyEverySetterAlternating(engine, 1.0e9f);
        const StereoRender out = renderBlocks(engine, g1OneSecond, /*injectFault=*/false);
        INFO("+/-1e9 sub-case peak=" << peakAbs(out));
        CHECK(countNonFinite(out) == 0u);
        CHECK(engine.getNonFiniteRecoveryCount() == 0u);
        CHECK(peakAbs(out) <= 4.0f);
    }
}

// ==============================================================================
// FR-056's five normative guards / Edge cases 27-31 (plan S8.6 clause 5)
// ==============================================================================
TEST_CASE("AetherReverb_BloomNoteApi", "[effects][aether]") {
    const auto cfg = pinnedPrepareConfig();
    const float partials[4] = {220.0f, 440.0f, 660.0f, 880.0f};

    // --------------------------------------------------------------------------
    // (a) nullptr, count == 0, and a call before prepare() are all no-ops.
    //     Edge cases 27 and 31.
    // --------------------------------------------------------------------------
    {
        AetherReverb unprepared;
        REQUIRE_FALSE(unprepared.isPrepared());
        unprepared.bloomNoteOn(0, partials, 4);
        CHECK_FALSE(unprepared.isPrepared());
        CHECK(unprepared.getActiveBloomResonatorCount() == 0u);
        unprepared.bloomNoteOff(0);
        CHECK(unprepared.getActiveBloomResonatorCount() == 0u);
    }
    {
        AetherReverb engine;
        engine.prepare(kSampleRate, cfg);
        engine.bloomNoteOn(0, nullptr, 4);
        CHECK(engine.getActiveBloomResonatorCount() == 0u);
        engine.bloomNoteOn(0, partials, 0);
        CHECK(engine.getActiveBloomResonatorCount() == 0u);
    }

    // --------------------------------------------------------------------------
    // (b) count = 64 clamps to kMaxBloomResonators = 32 with NO out-of-bounds
    //     write. Run inside plan S8.2's BRACKETING AllocationScope idiom - the
    //     naive scope.getAllocationCount() form can never fail, because
    //     AllocationScope latches its count in its DESTRUCTOR
    //     (tests/test_helpers/allocation_detector.h:81-83).
    // --------------------------------------------------------------------------
    {
        AetherReverb engine;
        engine.prepare(kSampleRate, cfg);
        applyPinnedConfig(engine);
        engine.setBloomSend(1.0f);

        std::vector<float> many(64, 0.0f);
        for (std::size_t p = 0; p < many.size(); ++p) {
            many[p] = 110.0f * static_cast<float>(p + 1u);
        }

        // Warm-up so first-call runtime dispatch is not charged to the window.
        std::vector<float> in(kBlock, 0.0f);
        std::vector<float> outL(kBlock, 0.0f);
        std::vector<float> outR(kBlock, 0.0f);
        engine.processStereoBlock(in.data(), in.data(), outL.data(), outR.data(), kBlock);

        std::size_t allocs = 0;
        std::size_t activeAfter = 0;
        {
            [[maybe_unused]] const TestHelpers::AllocationScope scope;
            engine.bloomNoteOn(0, many.data(), many.size());
            activeAfter = engine.getActiveBloomResonatorCount();
            allocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        CHECK(allocs == 0u);
        CHECK(activeAfter == static_cast<std::size_t>(AetherReverb::kMaxBloomResonators));

        // (d, first half) Edge case 29: a repeat note-on for the SAME voiceId
        // replaces the set - the driven count must not double.
        engine.bloomNoteOn(0, many.data(), many.size());
        CHECK(engine.getActiveBloomResonatorCount() ==
              static_cast<std::size_t>(AetherReverb::kMaxBloomResonators));
    }

    // --------------------------------------------------------------------------
    // (c) hostile partials - NaN, +/-Inf, 0 Hz, a NEGATIVE frequency and
    //     0.9 * sr (above Nyquist) - must all be guarded and clamped BEFORE any
    //     coefficient maths, so nothing non-finite reaches the bank and FR-083
    //     never fires. Edge case 28.
    // --------------------------------------------------------------------------
    {
        AetherReverb engine;
        engine.prepare(kSampleRate, cfg);
        applyPinnedConfig(engine);
        engine.setBloomSend(1.0f);

        const float hostile[6] = {makeNonFinite(kQuietNaNBits),
                                  makeNonFinite(kPosInfBits),
                                  makeNonFinite(kNegInfBits),
                                  0.0f,
                                  -440.0f,
                                  static_cast<float>(0.9 * kSampleRate)};
        engine.bloomNoteOn(1, hostile, 6);
        CHECK(engine.getActiveBloomResonatorCount() == 6u);

        const std::vector<float> g1 = makeHarmonicStack(kOneSecond, kSampleRate);
        const StereoRender out = renderBlocks(engine, g1, /*injectFault=*/false);
        CHECK(countNonFinite(out) == 0u);
        CHECK(engine.getNonFiniteRecoveryCount() == 0u);
    }

    // --------------------------------------------------------------------------
    // (d, second half) bloomNoteOff for a voiceId that was never noted on is a
    //     no-op - it must not disturb the live voice.
    // --------------------------------------------------------------------------
    {
        AetherReverb engine;
        engine.prepare(kSampleRate, cfg);
        applyPinnedConfig(engine);
        engine.bloomNoteOn(0, partials, 4);
        REQUIRE(engine.getActiveBloomResonatorCount() == 4u);

        engine.bloomNoteOff(7);
        CHECK(engine.getActiveBloomResonatorCount() == 4u);

        engine.bloomNoteOff(0);
        CHECK(engine.getActiveBloomResonatorCount() == 0u);
    }
}
