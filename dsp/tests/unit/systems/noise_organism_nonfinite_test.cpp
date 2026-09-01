// =============================================================================
// Layer 3: System Tests - NoiseOrganism non-finite setter hygiene (SC-015)
//                              (specs/vorago-phase2-noise-organism)
// =============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase2-noise-organism/spec.md   (SC-015, FR-008)
//            specs/vorago-phase2-noise-organism/plan.md   (S1.2a - the normative
//                                                          neutral table)
//            specs/vorago-phase2-noise-organism/tasks.md  (T001 creates this TU,
//                                                          T019 lands this case)
//
// SCOPE OF THIS TU: SC-015 ONLY.
//
// THIS IS A SEPARATE TU BECAUSE OF ITS COMPILE FLAGS. It is the ONLY one of the
//   four Phase 2 TUs listed under "-fno-fast-math -fno-finite-math-only" in
//   dsp/tests/CMakeLists.txt; those flags must NOT be applied to the other
//   three (noise_organism_test.cpp and noise_organism_spectral_test.cpp stay out
//   so the FR-008 guards are proved in the /fp:fast + -ffast-math mode the
//   header actually ships in, and the perf TU stays out because -fno-fast-math
//   would move the figures its baselines are pinned to). Do not merge these
//   cases into the other three, and do not add the other three to that block.
//
// WHY THIS IS A REAL TRACE AND NOT A FORMALITY. std::clamp does NOT reject NaN:
//   with v = NaN both `v < lo` and `hi < v` are false, so v is returned
//   unchanged. A clamp-only setter therefore admits NaN into configuration
//   state, and the trace the header records as NORMATIVE
//   (noise_organism.h:1040-1090) is fatal:
//     setResonatorAnchor(slot, i, NaN) -> anchorHz[i] = NaN
//       -> driftedHz = clampFreq(NaN * exp2(..)) = NaN
//       -> ResonatorBank::setFrequency clamps with a bare std::clamp (:539)
//       -> BiquadCoefficients::calculate clamps with another (biquad.h:155)
//       -> NaN coefficients. Biquad::process resets only on a non-finite INPUT
//          SAMPLE (biquad.h:354), never on non-finite coefficients, so the
//          resonator emits NaN forever - and FR-074's output clamp is itself a
//          std::clamp and propagates it.
//   The blanket rule the header adopts is that every float-taking public setter
//   calls sanitise() as its FIRST statement, before any clamp
//   (noise_organism.h:1094-1097, detail::isFinite from core/db_utils.h:118).
//   This case is what holds that rule in place.
//
// NON-FINITE VALUES ARE BUILT FROM BIT PATTERNS THROUGH A VOLATILE SINK, never
//   from std::numeric_limits<float>::quiet_NaN() / infinity(): those fold to
//   FINITE GARBAGE on the macOS -ffast-math leg, and a test that injects a
//   finite number proves nothing. Likewise finiteness is read off the IEEE-754
//   EXPONENT FIELD, never with std::isnan / std::isinf, which fold away in the
//   same place. The idiom is the shipped one from
//   dsp/tests/unit/systems/seraphis_nonfinite_test.cpp:148-172.
// =============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/processors/resonator_bank.h>  // rt60ToQ, for the Q echo
#include <krate/dsp/systems/noise_organism.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using Catch::Approx;
using Krate::DSP::NoiseOrganism;
using Krate::DSP::NoiseOrganismModel;
using Krate::DSP::rt60ToQ;

namespace {

// =============================================================================
// Shared constants
// =============================================================================

constexpr double      kSr        = 48000.0;
constexpr std::size_t kOneSecond = 48000;
/// One whole control chunk - exactly one updateControl(), because controlPhase_
/// is 0 on a freshly prepared organism (noise_organism.h:427-436).
constexpr std::size_t kControlChunk = NoiseOrganism::kControlChunkSamples;

/// IEEE-754 binary32: exponent field all ones == Inf or NaN.
constexpr std::uint32_t kExponentMask = 0x7F800000u;

struct NonFinitePattern {
    const char*   name;
    std::uint32_t bits;
};

/// The three bit patterns, named once rather than spelled at the injection
/// sites.
constexpr std::array<NonFinitePattern, 3> kPatterns{{
    {"quiet NaN", 0x7FC00000u},
    {"+Inf", 0x7F800000u},
    {"-Inf", 0xFF800000u},
}};

/// binary64 twins, for prepare()'s `double sampleRate` argument. Same order.
constexpr std::array<std::uint64_t, 3> kPatterns64{{
    0x7FF8000000000000ULL,  // quiet NaN
    0x7FF0000000000000ULL,  // +Inf
    0xFFF0000000000000ULL,  // -Inf
}};

// =============================================================================
// Non-finite construction and classification
// =============================================================================

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant from being
/// folded back into the memcpy at compile time, which is how a -ffast-math build
/// turns an "infinity" literal into a finite number.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b            = bits;
    const std::uint32_t    materialized = b;
    float                  f            = 0.0f;
    std::memcpy(&f, &materialized, sizeof(f));
    return f;
}

/// @brief The binary64 twin of makeNonFinite, for prepare()'s sampleRate.
[[nodiscard]] double makeNonFiniteDouble(std::uint64_t bits) noexcept {
    volatile std::uint64_t b            = bits;
    const std::uint64_t    materialized = b;
    double                 d            = 0.0;
    std::memcpy(&d, &materialized, sizeof(d));
    return d;
}

/// @brief FR-008's finiteness test, on the exponent field.
///
/// Never std::isnan / std::isinf / std::isfinite: FR-008 forbids them
/// phase-wide, and this is integer arithmetic on the bit pattern, so it reads
/// correctly under any fast-math setting rather than only under this TU's.
[[nodiscard]] bool sampleIsFinite(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & kExponentMask) != kExponentMask;
}

// =============================================================================
// Fixtures
// =============================================================================

/// @brief The part (a) fixture: FR-016 defaults with the wander switch OFF.
///
/// setWanderEnabled(false) is what makes the applied-state echo a usable read
/// surface for the setters that have no direct getter. With wanderScale() == 0
/// (noise_organism.h:2196-2198) updateResonatorControl's drifted frequency
/// collapses to `clamp(anchor * exp2(0), ..)` == the anchor, its Q factor
/// collapses to exactly 1, and updateFilterControl's cutoff collapses to the
/// base cutoff - so getResonatorCurrentFrequency / getResonatorCurrentQ /
/// getFilterCurrentCutoff report the configured value rather than a
/// lane-dependent one.
void prepareDefaultFixture(NoiseOrganism& org) {
    org.prepare(kSr, NoiseOrganism::PrepareConfig{});
    org.setSeed(0xC0FFEEU);
    org.setWanderEnabled(false);
}

/// @brief Render `n` samples and return them.
[[nodiscard]] std::vector<float> render(NoiseOrganism& org, std::size_t n) {
    std::vector<float> out(n, 0.0f);
    org.processBlock(out.data(), out.size());
    return out;
}

/// @brief Run exactly one control step, so the applied-state echo is populated.
void advanceOneControlStep(NoiseOrganism& org) {
    std::vector<float> scratch(kControlChunk, 0.0f);
    org.processBlock(scratch.data(), scratch.size());
}

[[nodiscard]] double rmsDb(const std::vector<float>& x) {
    double sumSq = 0.0;
    for (const float s : x) {
        sumSq += static_cast<double>(s) * static_cast<double>(s);
    }
    const double count = x.empty() ? 1.0 : static_cast<double>(x.size());
    const double rms   = std::sqrt(sumSq / count);
    return 20.0 * std::log10(rms < 1.0e-12 ? 1.0e-12 : rms);
}

// =============================================================================
// Read surface capture (arm (c): "every OTHER getter unchanged")
// =============================================================================

/// @brief Every value the FR-015 read surface exposes, flattened in a fixed
/// order so two organisms can be compared element by element.
[[nodiscard]] std::vector<double> captureReadSurface(const NoiseOrganism& org) {
    std::vector<double> v;
    v.push_back(static_cast<double>(org.getNumSources()));
    v.push_back(org.isWanderEnabled() ? 1.0 : 0.0);
    v.push_back(static_cast<double>(org.getWanderRate()));
    v.push_back(static_cast<double>(org.getAllocatedBytes()));
    v.push_back(static_cast<double>(org.getClampEngagementCount()));
    for (std::size_t slot = 0; slot < NoiseOrganism::kMaxSources; ++slot) {
        v.push_back(static_cast<double>(static_cast<int>(org.getSourceModel(slot))));
        v.push_back(static_cast<double>(static_cast<int>(org.getSourceNoiseType(slot))));
        v.push_back(static_cast<double>(org.getSourceLevel(slot)));
        v.push_back(org.isSourceDormant(slot) ? 1.0 : 0.0);
        v.push_back(static_cast<double>(org.getSourceWakeAmount(slot)));
        v.push_back(static_cast<double>(org.getNumResonators(slot)));
        v.push_back(static_cast<double>(org.getNumCombs(slot)));
        v.push_back(static_cast<double>(org.getCombFundamental(slot)));
        v.push_back(static_cast<double>(org.getCombSpread(slot)));
        v.push_back(static_cast<double>(org.getCombFeedback(slot)));
        v.push_back(static_cast<double>(org.getDustDensity(slot)));
        v.push_back(static_cast<double>(org.getDustGrainMs(slot)));
        v.push_back(static_cast<double>(static_cast<int>(org.getDustCarrierColor(slot))));
        v.push_back(static_cast<double>(org.getSourceGain(slot)));
        v.push_back(static_cast<double>(org.getSourceRms(slot)));
        v.push_back(static_cast<double>(org.getFilterCurrentCutoff(slot)));
        for (std::size_t i = 0; i < NoiseOrganism::kMaxResonatorsPerSource; ++i) {
            v.push_back(static_cast<double>(org.getResonatorCurrentFrequency(slot, i)));
            v.push_back(static_cast<double>(org.getResonatorCurrentQ(slot, i)));
        }
        for (std::size_t i = 0; i < NoiseOrganism::kMaxCombsPerSource; ++i) {
            v.push_back(static_cast<double>(org.getCombCurrentDelayMs(slot, i)));
        }
    }
    return v;
}

/// @brief Drive EVERY float-taking public setter with one non-finite value.
///
/// Exhaustive against the header's normative table
/// (noise_organism.h:1063-1084): every row of that table appears here except
/// `prepare`'s two, which are prepare-time arguments and are injected separately
/// in part (d). A setter added to the class without a sanitise() call, and
/// without a row here, is exactly what this case exists to catch.
void injectEverySetter(NoiseOrganism& org, float nf) {
    for (std::size_t slot = 0; slot < NoiseOrganism::kMaxSources; ++slot) {
        org.setSourceLevel(slot, nf);
        for (std::size_t i = 0; i < NoiseOrganism::kMaxResonatorsPerSource; ++i) {
            org.setResonatorAnchor(slot, i, nf);
        }
        org.setResonatorDecay(slot, nf);
        org.setCombTuning(slot, nf, nf);
        org.setCombFeedback(slot, nf);
        org.setFilterBaseCutoff(slot, nf);
        org.setFilterBaseResonance(slot, nf);
        org.setDustGrainMs(slot, nf);
        org.setDustDensity(slot, nf);
        org.setResonatorWander(slot, nf, nf);
        org.setResonatorQWander(slot, nf);
        org.setFilterWander(slot, nf, nf);
        org.setFilterResonanceWander(slot, nf, nf);
        org.setCombWander(slot, nf, nf);
        org.setSourceBreathing(slot, nf, nf, nf);
        org.setSourceWake(slot, nf);
    }
    org.setWanderRate(nf);
}

}  // namespace

// =============================================================================
// SC-015 - non-finite setter inputs
// =============================================================================
// UNTAGGED, deliberately: this is a NaN/Inf sentinel, and CLAUDE.md's [long]
// convention forbids tagging those - they must stay in the per-push CI lane.
// =============================================================================

TEST_CASE("NoiseOrganism_NonFiniteSetterInputs", "[noise_organism]") {
    // -------------------------------------------------------------------------
    // (a) Each non-finite argument is replaced by the neutral named in the
    //     header's normative table, and read back through the read surface.
    //
    // Every sub-check writes a DISTINCT legal value first, then injects. Without
    // that pre-write most of these neutrals are also the FR-016 default, so a
    // setter that simply ignored the write would pass; with it, only an actual
    // substitution back to the neutral passes.
    // -------------------------------------------------------------------------
    for (std::size_t p = 0; p < kPatterns.size(); ++p) {
        CAPTURE(kPatterns[p].name);
        const float nf = makeNonFinite(kPatterns[p].bits);
        REQUIRE_FALSE(sampleIsFinite(nf));  // the injection is real, not folded

        // ---- setSourceLevel -> -12.0f ---------------------------------------
        {
            NoiseOrganism org;
            prepareDefaultFixture(org);
            org.setSourceLevel(0, -40.0f);
            REQUIRE(org.getSourceLevel(0) == Approx(-40.0f).margin(1.0e-5));
            org.setSourceLevel(0, nf);
            REQUIRE(org.getSourceLevel(0) == Approx(-12.0f).margin(1.0e-5));
        }

        // ---- setCombTuning -> 60.0f / 0.35f, guarded INDEPENDENTLY -----------
        {
            NoiseOrganism org;
            prepareDefaultFixture(org);
            org.setCombTuning(0, 120.0f, 0.8f);
            REQUIRE(org.getCombFundamental(0) == Approx(120.0f).margin(1.0e-4));
            REQUIRE(org.getCombSpread(0) == Approx(0.8f).margin(1.0e-5));

            // Non-finite fundamental, finite spread: the spread must SURVIVE.
            org.setCombTuning(0, nf, 0.6f);
            REQUIRE(org.getCombFundamental(0) == Approx(60.0f).margin(1.0e-4));
            REQUIRE(org.getCombSpread(0) == Approx(0.6f).margin(1.0e-5));

            // ...and the mirror image: finite fundamental, non-finite spread.
            org.setCombTuning(0, 90.0f, nf);
            REQUIRE(org.getCombFundamental(0) == Approx(90.0f).margin(1.0e-4));
            REQUIRE(org.getCombSpread(0) == Approx(0.35f).margin(1.0e-5));
        }

        // ---- setCombFeedback -> kDefaultCombFeedback -------------------------
        {
            NoiseOrganism org;
            prepareDefaultFixture(org);
            org.setCombFeedback(0, 0.2f);
            REQUIRE(org.getCombFeedback(0) == Approx(0.2f).margin(1.0e-5));
            org.setCombFeedback(0, nf);
            REQUIRE(org.getCombFeedback(0) ==
                    Approx(NoiseOrganism::kDefaultCombFeedback).margin(1.0e-5));
        }

        // ---- setDustDensity -> 100.0f, setDustGrainMs -> 40.0f ---------------
        {
            NoiseOrganism org;
            prepareDefaultFixture(org);
            org.setDustDensity(0, 5000.0f);
            REQUIRE(org.getDustDensity(0) == Approx(5000.0f).margin(1.0e-2));
            org.setDustDensity(0, nf);
            REQUIRE(org.getDustDensity(0) == Approx(100.0f).margin(1.0e-3));

            // At the restored 100 imp/s the FR-035 grain ceiling is 240 ms, so a
            // 120 ms request is NOT capped and the pre-write is observable.
            org.setDustGrainMs(0, 120.0f);
            REQUIRE(org.getDustGrainMs(0) == Approx(120.0f).margin(1.0e-3));
            org.setDustGrainMs(0, nf);
            REQUIRE(org.getDustGrainMs(0) == Approx(40.0f).margin(1.0e-3));
        }

        // ---- setWanderRate -> kDefaultWanderRateHz ---------------------------
        {
            NoiseOrganism org;
            prepareDefaultFixture(org);
            org.setWanderRate(1.0f);
            REQUIRE(org.getWanderRate() == Approx(1.0f).margin(1.0e-6));
            org.setWanderRate(nf);
            REQUIRE(org.getWanderRate() ==
                    Approx(NoiseOrganism::kDefaultWanderRateHz).margin(1.0e-6));
        }

        // ---- setSourceWake -> 1.0f -------------------------------------------
        {
            NoiseOrganism org;
            prepareDefaultFixture(org);
            org.setSourceWake(0, 0.25f);
            REQUIRE(org.getSourceWakeAmount(0) == Approx(0.25f).margin(1.0e-6));
            org.setSourceWake(0, nf);
            REQUIRE(org.getSourceWakeAmount(0) == Approx(1.0f).margin(1.0e-6));
        }

        // ---- setResonatorAnchor -> kDefaultAnchorHz[index] (70/140/260/500) ---
        // Read through the applied-state echo, which with the wander switch off
        // is the anchor itself. Both default-enabled resonators are covered, so
        // the PER-INDEX neutral is proved and not just index 0's.
        {
            NoiseOrganism org;
            prepareDefaultFixture(org);
            org.setResonatorAnchor(0, 0, 300.0f);
            org.setResonatorAnchor(0, 1, 900.0f);
            advanceOneControlStep(org);
            REQUIRE(org.getResonatorCurrentFrequency(0, 0) ==
                    Approx(300.0f).margin(1.0e-3));
            REQUIRE(org.getResonatorCurrentFrequency(0, 1) ==
                    Approx(900.0f).margin(1.0e-3));

            org.setResonatorAnchor(0, 0, nf);
            org.setResonatorAnchor(0, 1, nf);
            advanceOneControlStep(org);
            REQUIRE(org.getResonatorCurrentFrequency(0, 0) ==
                    Approx(70.0f).margin(1.0e-3));
            REQUIRE(org.getResonatorCurrentFrequency(0, 1) ==
                    Approx(140.0f).margin(1.0e-3));
        }

        // ---- setResonatorDecay -> 1.5f ---------------------------------------
        // Observed through the Q echo: with the wander switch off the Q lane
        // depth is 0, so the applied Q is exactly rt60ToQ(anchor, decay)
        // (resonator_bank.h:92-98) and the decay is fully recoverable from it.
        {
            NoiseOrganism org;
            prepareDefaultFixture(org);
            org.setResonatorDecay(0, 0.25f);
            advanceOneControlStep(org);
            REQUIRE(org.getResonatorCurrentQ(0, 0) ==
                    Approx(rt60ToQ(70.0f, 0.25f)).margin(1.0e-3));

            org.setResonatorDecay(0, nf);
            advanceOneControlStep(org);
            REQUIRE(org.getResonatorCurrentQ(0, 0) ==
                    Approx(rt60ToQ(70.0f, 1.5f)).margin(1.0e-3));
        }

        // ---- setFilterBaseCutoff -> 800.0f -----------------------------------
        {
            NoiseOrganism org;
            prepareDefaultFixture(org);
            org.setFilterBaseCutoff(0, 2500.0f);
            advanceOneControlStep(org);
            REQUIRE(org.getFilterCurrentCutoff(0) == Approx(2500.0f).margin(1.0e-2));

            org.setFilterBaseCutoff(0, nf);
            advanceOneControlStep(org);
            REQUIRE(org.getFilterCurrentCutoff(0) == Approx(800.0f).margin(1.0e-2));
        }
    }

    // -------------------------------------------------------------------------
    // (b) Every rendered sample is finite, on the exponent field.
    //
    // Two arms, because the two configurations reach different code:
    //   * the DEFAULT one, which is also what (c) compares against; and
    //   * a rich one - four slots, all four models, four resonators and four
    //     combs each, wander ON - so the dust pool, the inharmonic comb law and
    //     every lane are live while the poison lands.
    // -------------------------------------------------------------------------
    for (std::size_t p = 0; p < kPatterns.size(); ++p) {
        CAPTURE(kPatterns[p].name);
        const float nf = makeNonFinite(kPatterns[p].bits);

        {
            NoiseOrganism org;
            prepareDefaultFixture(org);
            injectEverySetter(org, nf);
            const std::vector<float> out = render(org, kOneSecond);
            for (std::size_t n = 0; n < out.size(); ++n) {
                if (!sampleIsFinite(out[n])) {
                    CAPTURE(n);
                    FAIL("default-configuration render went non-finite");
                }
            }
        }

        {
            NoiseOrganism                 org;
            NoiseOrganism::PrepareConfig  cfg{};
            cfg.numSources = NoiseOrganism::kMaxSources;
            org.prepare(kSr, cfg);
            org.setSeed(0x5EEDU);
            org.setSourceModel(0, NoiseOrganismModel::Direct);
            org.setSourceModel(1, NoiseOrganismModel::FilteredWind);
            org.setSourceModel(2, NoiseOrganismModel::GranularDust);
            org.setSourceModel(3, NoiseOrganismModel::MetallicHiss);
            for (std::size_t slot = 0; slot < NoiseOrganism::kMaxSources; ++slot) {
                org.setNumResonators(slot, NoiseOrganism::kMaxResonatorsPerSource);
                org.setNumCombs(slot, NoiseOrganism::kMaxCombsPerSource);
            }
            // Poison BEFORE and AFTER a render, so the injection is seen both by
            // a cold chain and by one whose resonators are ringing and whose
            // comb delay lines are full of past audio.
            injectEverySetter(org, nf);
            std::vector<float>       out  = render(org, kOneSecond);
            injectEverySetter(org, nf);
            const std::vector<float> out2 = render(org, kOneSecond);
            out.insert(out.end(), out2.begin(), out2.end());
            for (std::size_t n = 0; n < out.size(); ++n) {
                if (!sampleIsFinite(out[n])) {
                    CAPTURE(n);
                    FAIL("rich-configuration render went non-finite");
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // (c) A rejected value must not perturb state.
    //
    // Measured against an UNINJECTED reference built the same way: every neutral
    // in the normative table IS the FR-016 default, so an organism hit with
    // nothing but non-finite writes must stay indistinguishable from one that
    // was never written to at all - on the whole read surface and on the audio.
    // -------------------------------------------------------------------------
    {
        NoiseOrganism reference;
        prepareDefaultFixture(reference);
        const std::vector<double> referenceSurface = captureReadSurface(reference);
        const double              referenceDb      = rmsDb(render(reference, kOneSecond));

        for (std::size_t p = 0; p < kPatterns.size(); ++p) {
            CAPTURE(kPatterns[p].name);
            const float nf = makeNonFinite(kPatterns[p].bits);

            NoiseOrganism injected;
            prepareDefaultFixture(injected);
            const std::vector<double> beforeSurface = captureReadSurface(injected);
            REQUIRE(beforeSurface == referenceSurface);

            injectEverySetter(injected, nf);

            // Every getter unchanged from its pre-injection value - exactly, not
            // approximately: a rejected write re-stores the value that was
            // already there, through the same expression on the same inputs, so
            // any difference at all is a real perturbation.
            const std::vector<double> afterSurface = captureReadSurface(injected);
            REQUIRE(afterSurface.size() == beforeSurface.size());
            for (std::size_t i = 0; i < afterSurface.size(); ++i) {
                CAPTURE(i);
                CAPTURE(beforeSurface[i]);
                CAPTURE(afterSurface[i]);
                REQUIRE(afterSurface[i] == beforeSurface[i]);
            }

            // ...and the audio the organism goes on to make is the same audio.
            const double injectedDb = rmsDb(render(injected, kOneSecond));
            CAPTURE(referenceDb);
            CAPTURE(injectedDb);
            REQUIRE(std::abs(injectedDb - referenceDb) <= 0.5);
        }
    }

    // -------------------------------------------------------------------------
    // (d) prepare()'s two non-finite arguments: `double sampleRate` -> 48000.0
    //     (the double overload of sanitise, db_utils.h:125) and
    //     PrepareConfig::maxCombDelayMs -> 50.0f.
    //
    // Read back through getAllocatedBytes(), which is a function of BOTH: the
    // comb delay-line sizing is nextPowerOf2(trunc(sampleRate * maxCombDelayMs
    // / 1000) + 1) per comb (FR-096), so a non-finite sample rate or delay
    // length that reached the sizing would move the figure.
    // -------------------------------------------------------------------------
    {
        NoiseOrganism reference;
        reference.prepare(kSr, NoiseOrganism::PrepareConfig{});
        const std::size_t referenceBytes = reference.getAllocatedBytes();
        REQUIRE(referenceBytes > 0);

        for (std::size_t p = 0; p < kPatterns.size(); ++p) {
            CAPTURE(kPatterns[p].name);
            const float  nf  = makeNonFinite(kPatterns[p].bits);
            const double nfd = makeNonFiniteDouble(kPatterns64[p]);

            NoiseOrganism                injected;
            NoiseOrganism::PrepareConfig cfg{};
            cfg.maxCombDelayMs = nf;
            injected.prepare(nfd, cfg);
            REQUIRE(injected.isPrepared());
            REQUIRE(injected.getAllocatedBytes() == referenceBytes);

            const std::vector<float> out = render(injected, kOneSecond);
            for (std::size_t n = 0; n < out.size(); ++n) {
                if (!sampleIsFinite(out[n])) {
                    CAPTURE(n);
                    FAIL("render after a non-finite prepare() went non-finite");
                }
            }
        }
    }
}
