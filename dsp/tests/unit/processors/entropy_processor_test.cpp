// ==============================================================================
// Layer 2: Processor Tests - EntropyProcessor (Seraphis Phase 3)
// ==============================================================================
// Spec:  specs/seraphis-phase3-spectral-morph/spec.md  (FR-005 - FR-008, FR-071 - FR-075)
// Plan:  specs/seraphis-phase3-spectral-morph/plan.md  (sections 4.1, 4.2, 4.3, 4.6, 4.8)
// Tasks: specs/seraphis-phase3-spectral-morph/tasks.md (T010, T011, T012, T013)
//
// This TU covers the STATIC surface of EntropyProcessor: the derived
// transcendental constants, the FR-074 headroom assert, the stage-weight ramps,
// and the configuration-time contract of prepare()/reset()/setSeed()/setEntropy()
// together with the FR-008 introspection getters -- plus (T011) the two
// lane-batched Ornstein-Uhlenbeck banks: their equivalence to a reference
// BrownianDrift, their exact discretisation, and the disjointness of the 4 x 64
// lane seeds -- plus (T012) the fixed-order stage application: which stages are
// shut at which entropy setting (SC-005) and the statistical signature of the
// stage-2 decoherence walk (SC-016) -- plus (T013) the stage-4 death/rebirth
// FSM: the e = 0.90 completion arm of SC-005 and the whole-grid boundedness of
// SC-006.
//
// NON-FINITE INPUTS ARE BUILT FROM BIT PATTERNS, NEVER FROM
// std::numeric_limits<float>::quiet_NaN() / infinity(): the macOS leg builds
// -ffast-math (-ffinite-math-only), under which the compiler assumes non-finite
// values do not exist and constant-folds those calls to finite garbage -- the
// "rejects non-finite" assertions would then silently be testing ordinary
// numbers. The volatile sink below forces a real non-finite bit pattern to
// exist at runtime regardless of FP mode.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/entropy_processor.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

using Krate::DSP::BrownianDrift;
using Krate::DSP::deriveStreamSeed;
using Krate::DSP::EntropyProcessor;
using Krate::DSP::kCompletionThreshold;

namespace {

/// The phase's pinned seed set, shared by every seeded criterion in this TU.
constexpr std::array<std::uint32_t, 8> kSeeds{1u, 7u, 13u, 29u, 101u, 257u, 1009u, 65537u};

/// A float built from its IEEE-754 bit pattern through a volatile sink.
/// See the -ffast-math note at the top of this file.
[[nodiscard]] float floatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t observed = sink;
    return std::bit_cast<float>(observed);
}

constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

/// First index past the end of every fixed per-partial array in the class.
constexpr std::size_t kPastEnd = EntropyProcessor::kPartials;

[[nodiscard]] bool withinRelative(float actual, float expected, float relTolerance) noexcept {
    return std::abs(actual - expected) <= relTolerance * std::abs(expected);
}

/// Finiteness by BIT PATTERN, never std::isnan / std::isinf: the macOS leg builds
/// -ffast-math (-ffinite-math-only), under which those calls are folded away and
/// the assertion silently stops testing anything. SC-006 names the requirement
/// explicitly ("Non-finite detection uses bit-pattern tests").
[[nodiscard]] bool isFiniteFloat(float v) noexcept {
    return !Krate::DSP::detail::isNaN(v) && !Krate::DSP::detail::isInf(v);
}

} // namespace

TEST_CASE("EntropyProcessor_ConstantsMatchTranscendentals", "[entropy][seraphis]") {
    SECTION("kMinRatioSpacingFactor is exp2(24/1200)") {
        // detail::constexprExp is a range-reduced Taylor series, so the constexpr
        // constant is pinned against the runtime transcendental it stands in for
        // (std::exp2 is not constexpr in C++20 -- plan section 0.1 trap 7).
        const float expected = std::exp2(24.0f / 1200.0f);
        REQUIRE(withinRelative(EntropyProcessor::kMinRatioSpacingFactor, expected, 1e-6f));
        // Published value, plan section 4.1.
        REQUIRE(withinRelative(EntropyProcessor::kMinRatioSpacingFactor, 1.0139595f, 1e-6f));
        REQUIRE(EntropyProcessor::kMinRatioSpacingLog2 == 24.0f / 1200.0f);
    }

    SECTION("FR-074: two neighbours cannot close the FR-046 spacing floor") {
        STATIC_REQUIRE(2.0f * (EntropyProcessor::kMaxDecoherenceCents +
                               EntropyProcessor::kMaxScatterCents) <
                       EntropyProcessor::kMinRatioSpacingCents); // 22.0 < 24.0
    }

    SECTION("onePoleChunkStep encodes smoother.h:91 exactly") {
        constexpr float kChunk = 64.0f;
        constexpr float kRate = 48000.0f;

        const float ampStep = EntropyProcessor::onePoleChunkStep(750.0f, kChunk, kRate);
        const float centsStep = EntropyProcessor::onePoleChunkStep(150.0f, kChunk, kRate);

        // Primary check, stated on the UN-CANCELLED quantity. `1 - exp(-x)` with
        // x ~ 1e-2 is a catastrophic-cancellation form: one ulp of the exp result
        // (~1.2e-7 relative) becomes ~6.7e-6 relative of the amplitude step and
        // ~1.3e-6 of the cents step. Asserting the SUBTRACTED value at 1e-6
        // relative would therefore demand bit-identical transcendentals from
        // MSVC, GCC and Apple Clang -- the trap dsp/CLAUDE.md documents. Comparing
        // exp against exp carries no such amplification and stays at 1e-6.
        REQUIRE(withinRelative(1.0f - ampStep, std::exp(-5000.0f * kChunk / (750.0f * kRate)),
                               1e-6f));
        REQUIRE(withinRelative(1.0f - centsStep, std::exp(-5000.0f * kChunk / (150.0f * kRate)),
                               1e-6f));

        // Secondary check: the FR-044 table rows (plan section 5.1) as published,
        // at the cancellation-aware band derived above (2e-5 relative covers ~3
        // ulp of the exp result on the amplitude row, and the published figures
        // are themselves quoted to only six significant digits). It is still four
        // orders sharper than the regression this clause exists to catch: feeding
        // kEntropyAmpSmoothMs = 150 instead of 750 (deviation D12) moves the
        // amplitude step from 8.85e-3 to 4.35e-2 -- a factor of 4.9.
        // Measured spread on g++ 13: 2.1e-7 (amp) and 4.3e-7 (cents).
        REQUIRE(withinRelative(ampStep, 8.84950e-3f, 2e-5f));
        REQUIRE(withinRelative(centsStep, 4.34712e-2f, 2e-5f));
    }

    SECTION("smoothing times are exactly BrownianDrift's, and 5x it (deviation D12)") {
        REQUIRE(EntropyProcessor::kEntropyCentsSmoothMs == BrownianDrift::kDriftOutputSmoothMs);
        REQUIRE(EntropyProcessor::kEntropyAmpSmoothMs ==
                5.0f * BrownianDrift::kDriftOutputSmoothMs);
        REQUIRE(EntropyProcessor::kEntropyCentsSmoothMs == 150.0f);
        REQUIRE(EntropyProcessor::kEntropyAmpSmoothMs == 750.0f);
    }

    SECTION("transcribed BrownianDrift walk bounds carry its values") {
        STATIC_REQUIRE(EntropyProcessor::kWalkLimit == 4.0f);       // brownian_drift.h:226
        STATIC_REQUIRE(EntropyProcessor::kDenormalFloor == 1e-20f); // brownian_drift.h:228
        STATIC_REQUIRE(EntropyProcessor::kPartials == std::size_t{64});
    }
}

TEST_CASE("EntropyProcessor_StageWeightsAreContinuous", "[entropy][seraphis]") {
    EntropyProcessor proc;
    proc.prepare(48000.0);

    SECTION("bounded, monotone and continuous on a 0.001 grid over [0,1]") {
        constexpr int kSteps = 1000;
        constexpr float kMaxStep = 0.005f;

        std::array<float, 4> previous{};
        for (int i = 0; i <= kSteps; ++i) {
            const float e = static_cast<float>(i) / static_cast<float>(kSteps);
            proc.setEntropy(e);
            for (int stage = 1; stage <= 4; ++stage) {
                const float w = proc.getStageWeight(stage);
                REQUIRE(w >= 0.0f);
                REQUIRE(w <= 1.0f);
                if (i > 0) {
                    const float prior = previous[static_cast<std::size_t>(stage - 1)];
                    REQUIRE(w >= prior);                      // monotone non-decreasing
                    REQUIRE(std::abs(w - prior) <= kMaxStep); // continuous
                }
                previous[static_cast<std::size_t>(stage - 1)] = w;
            }
        }
    }

    SECTION("every stage weight is exactly zero at entropy 0") {
        proc.setEntropy(0.0f);
        // Includes stage 1, whose interval STARTS at 0: (0 - 0) / 0.35 == 0.
        REQUIRE(proc.getStageWeight(1) == 0.0f);
        REQUIRE(proc.getStageWeight(2) == 0.0f);
        REQUIRE(proc.getStageWeight(3) == 0.0f);
        REQUIRE(proc.getStageWeight(4) == 0.0f);
    }

    SECTION("interval endpoints are exact") {
        proc.setEntropy(0.35f);
        REQUIRE(proc.getStageWeight(1) == 1.0f);

        proc.setEntropy(0.25f);
        REQUIRE(proc.getStageWeight(2) == 0.0f);
        proc.setEntropy(0.60f);
        REQUIRE(proc.getStageWeight(2) == 1.0f);

        proc.setEntropy(0.50f);
        REQUIRE(proc.getStageWeight(3) == 0.0f);
        proc.setEntropy(0.85f);
        REQUIRE(proc.getStageWeight(3) == 1.0f);

        proc.setEntropy(0.75f);
        REQUIRE(proc.getStageWeight(4) == 0.0f);
        proc.setEntropy(1.0f);
        REQUIRE(proc.getStageWeight(4) == 1.0f);
    }

    SECTION("out-of-range stage indices return zero") {
        proc.setEntropy(1.0f);
        REQUIRE(proc.getStageWeight(0) == 0.0f);
        REQUIRE(proc.getStageWeight(5) == 0.0f);
        REQUIRE(proc.getStageWeight(-1) == 0.0f);
        REQUIRE(proc.getStageWeight(std::numeric_limits<int>::max()) == 0.0f);
    }

    SECTION("setEntropy rejects non-finite input and clamps out-of-range input") {
        proc.setEntropy(0.42f);
        const float held = proc.getEntropy();
        REQUIRE(held == 0.42f);
        const float heldW3 = proc.getStageWeight(3);

        proc.setEntropy(floatFromBits(kQuietNaNBits));
        REQUIRE(proc.getEntropy() == held); // bitwise unchanged
        REQUIRE(proc.getStageWeight(3) == heldW3);

        proc.setEntropy(floatFromBits(kPosInfBits));
        REQUIRE(proc.getEntropy() == held);

        proc.setEntropy(floatFromBits(kNegInfBits));
        REQUIRE(proc.getEntropy() == held);

        proc.setEntropy(-1.0f);
        REQUIRE(proc.getEntropy() == 0.0f);

        proc.setEntropy(2.0f);
        REQUIRE(proc.getEntropy() == 1.0f);
    }
}

TEST_CASE("EntropyProcessor_IntrospectionIsBoundsChecked", "[entropy][seraphis]") {
    EntropyProcessor proc;
    proc.prepare(48000.0);

    SECTION("out-of-range partial indices return the neutral value, not memory") {
        REQUIRE(proc.getAmpJitterFactor(kPastEnd) == 0.0f);
        REQUIRE(proc.getDecoherenceCents(kPastEnd) == 0.0f);
        REQUIRE(proc.getAppliedScatterCents(kPastEnd) == 0.0f);
        REQUIRE(proc.getRawScatterDraw(kPastEnd) == 0.0f);
        REQUIRE(proc.getScatterRedrawCount(kPastEnd) == 0u);
        REQUIRE(proc.getLifePhase(kPastEnd) == EntropyProcessor::LifePhase::Alive);
        REQUIRE(proc.getLifeAmplitudeFactor(kPastEnd) == 0.0f);

        constexpr std::size_t kAbsurd = static_cast<std::size_t>(-1);
        REQUIRE(proc.getAmpJitterFactor(kAbsurd) == 0.0f);
        REQUIRE(proc.getDecoherenceCents(kAbsurd) == 0.0f);
        REQUIRE(proc.getAppliedScatterCents(kAbsurd) == 0.0f);
        REQUIRE(proc.getRawScatterDraw(kAbsurd) == 0.0f);
        REQUIRE(proc.getScatterRedrawCount(kAbsurd) == 0u);
        REQUIRE(proc.getLifePhase(kAbsurd) == EntropyProcessor::LifePhase::Alive);
        REQUIRE(proc.getLifeAmplitudeFactor(kAbsurd) == 0.0f);
    }

    SECTION("prepare leaves every partial alive, un-redrawn and finite") {
        REQUIRE(proc.stateFinite());
        for (std::size_t i = 0; i < EntropyProcessor::kPartials; ++i) {
            REQUIRE(proc.getLifePhase(i) == EntropyProcessor::LifePhase::Alive);
            REQUIRE(proc.getLifeAmplitudeFactor(i) == 1.0f);
            REQUIRE(proc.getScatterRedrawCount(i) == 0u);
            REQUIRE(proc.getRawScatterDraw(i) >= -1.0f);
            REQUIRE(proc.getRawScatterDraw(i) <= 1.0f);
        }
    }

    SECTION("reset keeps the configured parameters (BrownianDrift::reset semantics)") {
        proc.setEntropy(0.8f);
        const float weight = proc.getStageWeight(4);
        proc.reset();
        REQUIRE(proc.getEntropy() == 0.8f);
        REQUIRE(proc.getStageWeight(4) == weight);
        REQUIRE(proc.stateFinite());
    }

    SECTION("reset is deterministic and setSeed moves the scatter draws") {
        proc.reset();
        std::array<float, EntropyProcessor::kPartials> first{};
        for (std::size_t i = 0; i < EntropyProcessor::kPartials; ++i) {
            first[i] = proc.getRawScatterDraw(i);
        }
        proc.reset();
        for (std::size_t i = 0; i < EntropyProcessor::kPartials; ++i) {
            REQUIRE(proc.getRawScatterDraw(i) == first[i]); // bitwise reproducible
        }

        proc.setSeed(0xC0FFEEu);
        int differing = 0;
        for (std::size_t i = 0; i < EntropyProcessor::kPartials; ++i) {
            if (proc.getRawScatterDraw(i) != first[i]) {
                ++differing;
            }
        }
        REQUIRE(differing > 0);
    }
}

// =============================================================================
// T011 - the two lane-batched Ornstein-Uhlenbeck banks (FR-072)
// =============================================================================

TEST_CASE("EntropyProcessor_OuBankMatchesBrownianDrift", "[entropy][seraphis]") {
    constexpr double kSampleRate = 48000.0;
    // One chunk == one control step, so the schedule below is exactly
    // 90,000 control steps.
    constexpr std::size_t kControl = EntropyProcessor::kEntropyControlInterval; // 64
    constexpr std::size_t kNumChunks = 90000;                                   // 120 s @ 48 kHz
    static_assert(kControl * kNumChunks == 5760000u, "120 s at 48 kHz");
    static_assert(kControl == 2 * BrownianDrift::kControlRateInterval,
                  "the half-rate reference construction below needs EXACTLY a factor of two");

    // =========================================================================
    // ARM 1 WAS REPLACED, NOT DELETED (plan section 8 lever 5, consequence (b)).
    // =========================================================================
    // Lever 5 gave EntropyProcessor its own kEntropyControlInterval = 64, so a
    // lane no longer steps on the same grid as a stock BrownianDrift (32) and the
    // two are no longer directly stream-comparable. The plan's stated replacement
    // is an explicit-coefficient check at dt = 64/fs -- which Arm 2 below now
    // performs at exactly that dt.
    //
    // A coefficient check ALONE would be a weaker gate than what it replaced: it
    // says nothing about the lane-batched machinery (the three-draw Irwin-Hall
    // order, the walk clamp, the denormal floor, and the transcription of
    // OnePoleSmoother::advanceSamples with its isComplete skip and hard snap).
    // So the stream-equivalence arm is KEPT, in the one form that is still exact:
    //
    //   PREPARE THE REFERENCE AT HALF THE SAMPLE RATE AND DRIVE IT AT ITS OWN
    //   32-SAMPLE INTERVAL.
    //
    // Then its control dt is 32/(fs/2) = 64/fs -- the same REAL time step, and
    // bit-identical as a double (32/24000 and 64/48000 divide the same real value
    // from exactly representable operands, so IEEE correct rounding returns the
    // same float). The walks therefore run the identical AR(1) recurrence off the
    // identical seed streams in the identical draw order. The two output
    // smoothers agree in real arithmetic too -- coeff(fs/2) = coeff(fs)^2, so
    // coeff(fs/2)^32 = coeff(fs)^64 -- but are FORMED differently, which is
    // precisely the last-bit divergence the three-clause instrument below was
    // built to tolerate and to distinguish from a wrong recurrence.
    // =========================================================================

    SECTION("Arm 1: the decoherence bank reproduces a reference BrownianDrift") {
        // WHY THE COEFFICIENTS ARE DERIVED IN double (plan section 4.4).
        // The walk is an AR(1) recursion, so any difference in `a` or `g` is
        // re-applied at every one of the 90,000 control steps this arm drives.
        // Computing tau/a/g in float instead of double moves the coefficients in
        // the last bits and makes this row a coin flip across MSVC / GCC / Clang
        // -- the reason recorded verbatim at harmonic_cloud.h:1510-1515 and
        // carried into entropy_processor.h's updateBankCoefficients().
        //
        // The reference is configured with the CLASS CONSTANT
        // EntropyProcessor::kDecoherenceSmoothness, never a re-typed 0.26174f, so
        // both sides traverse the identical tau = kTauMin + s*(kTauMax - kTauMin)
        // mapping (brownian_drift.h:231-234). Writing tau = 8.0f directly gives
        // 7.99985 through that mapping and the two diverge in the last bits.
        //
        // ONLY THE DECOHERENCE BANK IS COMPARABLE. The amplitude bank smooths at
        // kEntropyAmpSmoothMs = 750 (deviation D12) and differs from a stock
        // BrownianDrift by construction. The lane-batching code under test --
        // advanceControlStepAllLanes / advanceSmootherAllLanes / advanceBank -- is
        // shared by both banks, so one bank exercises all of it.
        //
        // HOW EQUIVALENCE IS MEASURED, and why it is not a bare max-norm.
        // OnePoleSmoother::advanceSamples hard-snaps to target below
        // kCompletionThreshold = 1e-4 (smoother.h:251-253), which makes the
        // smoother a DISCONTINUOUS function of its input: when a last-bit
        // difference between the batched loop and the scalar reference straddles
        // that threshold, one side snaps and the other does not, and the pair
        // separates by up to the threshold itself until the 150 ms pole pulls them
        // back together. Last-bit differences are unavoidable between a 64-lane
        // loop and a scalar object under /fp:fast and -ffast-math -- Phase 2
        // MEASURED that merely adding a second reference bank to a TU moves the
        // batched loop's codegen and introduces 5.07e-7
        // (harmonic_cloud_test.cpp:1228-1285). A bare 1e-5 max-norm is therefore a
        // bit-exactness demand in disguise, of exactly the kind dsp/CLAUDE.md
        // forbids.
        //
        // The three clauses below are the Phase 2 instrument, unchanged:
        //   (1) the per-chunk MEAN over the 64 lanes stays within 1e-5. Every
        //       defect this gate exists to catch -- a wrong tau, a wrong seed
        //       derivation, a two-draw instead of three-draw Irwin-Hall, the naive
        //       closed-form smoother, a coeff^k table -- moves EVERY lane at once,
        //       so it moves the mean. A snap race moves one lane.
        //   (2) no single lane at any chunk exceeds twice the snap quantum. That is
        //       the structural bound on a snap race, not a free parameter.
        //   (3) chunks in which any lane exceeds 1e-5 are rarer than 2 %. This is
        //       what keeps the max-norm falsifiable: a systematically wrong
        //       recurrence breaches at essentially every chunk. Phase 2 measured
        //       0.57-0.68 % for the shipped transcription against ~98 % for the
        //       naive smoother.
        constexpr double kMeanTolerance = 1e-5;
        constexpr double kSnapRaceBound = 2.0 * static_cast<double>(kCompletionThreshold);
        constexpr double kMaxBreachingFraction = 0.02;

        constexpr std::uint32_t kBase = 29u; // kSeeds[3]
        static_assert(kSeeds[3] == kBase, "the base seed is one of the pinned set");

        EntropyProcessor proc;
        proc.setSeed(kBase);
        proc.prepare(kSampleRate);

        // Lane i of the decoherence bank draws from salt kPartials + i (plan
        // section 4.4's salt table); the reference is seeded from the same Layer 0
        // derivation, so the two share a stream rather than merely a distribution.
        // HALF the sample rate, driven at BrownianDrift's own control interval --
        // see the block above this SECTION. kReferenceControl samples at
        // kReferenceRate is the same real dt as kControl samples at kSampleRate.
        constexpr double kReferenceRate = kSampleRate / 2.0;
        constexpr std::size_t kReferenceControl = BrownianDrift::kControlRateInterval;
        static_assert(kReferenceControl * 2 == kControl);

        std::array<BrownianDrift, EntropyProcessor::kPartials> reference{};
        for (std::size_t i = 0; i < EntropyProcessor::kPartials; ++i) {
            reference[i].setSeed(deriveStreamSeed(kBase, EntropyProcessor::kPartials + i));
            reference[i].setSmoothness(EntropyProcessor::kDecoherenceSmoothness);
            reference[i].setDepth(1.0f);
            reference[i].prepare(kReferenceRate);
        }

        double worstLaneDiff = 0.0;
        double worstChunkMean = 0.0;
        double largestReference = 0.0;
        std::size_t breachingChunks = 0;

        // FR-075 makes a null pointer or count == 0 a WHOLE-CALL no-op that
        // advances nothing, so the banks cannot be driven with null arrays. Real
        // arrays are supplied and the processor is left at its default entropy of
        // 0, where processChunk's exact-zero fast path returns before applyStages
        // (entropy_processor.h:263-265) -- the two banks still advance, and the
        // arrays are never written, which is exactly the isolation this arm wants.
        REQUIRE(proc.getEntropy() == 0.0f);
        std::array<float, EntropyProcessor::kPartials> driverRatios{};
        std::array<float, EntropyProcessor::kPartials> driverAmps{};
        for (std::size_t i = 0; i < EntropyProcessor::kPartials; ++i) {
            driverRatios[i] = static_cast<float>(i + 1);
            driverAmps[i] = 1.0f / static_cast<float>(i + 1);
        }

        for (std::size_t chunk = 0; chunk < kNumChunks; ++chunk) {
            proc.processChunk(driverRatios.data(), driverAmps.data(),
                              EntropyProcessor::kPartials, kControl);

            double sum = 0.0;
            double chunkMax = 0.0;
            for (std::size_t i = 0; i < EntropyProcessor::kPartials; ++i) {
                reference[i].processBlock(kReferenceControl);
                const auto expected = static_cast<double>(reference[i].getCurrentValue());
                const auto actual = static_cast<double>(proc.getDecoherenceLaneValue(i));
                const double diff = std::abs(actual - expected);
                sum += diff;
                chunkMax = std::max(chunkMax, diff);
                largestReference = std::max(largestReference, std::abs(expected));
            }
            worstChunkMean =
                std::max(worstChunkMean, sum / static_cast<double>(EntropyProcessor::kPartials));
            worstLaneDiff = std::max(worstLaneDiff, chunkMax);
            if (chunkMax > kMeanTolerance) {
                ++breachingChunks;
            }
        }

        const double breachingFraction =
            static_cast<double>(breachingChunks) / static_cast<double>(kNumChunks);

        INFO("worst chunk mean = " << worstChunkMean << ", worst lane = " << worstLaneDiff
                                   << ", breaching fraction = " << breachingFraction);

        // Guard against the degenerate pass where both sides sat at zero.
        REQUIRE(largestReference > 0.1);
        REQUIRE(worstChunkMean <= kMeanTolerance);
        REQUIRE(worstLaneDiff <= kSnapRaceBound);
        REQUIRE(breachingFraction <= kMaxBreachingFraction);
    }

    SECTION("Arm 2: both banks store the exact OU discretisation") {
        EntropyProcessor proc;
        proc.prepare(kSampleRate);

        // THIS IS THE EXPLICIT-COEFFICIENT CHECK PLAN SECTION 8 LEVER 5 REQUIRES:
        // a == exp(-dt/tau) and g == kInternalStd*sqrt(1 - a^2) at
        // dt = kEntropyControlInterval / fs = 64/48000, at 1e-6 relative. It is
        // what proves lever 5 was an EXACT RE-DERIVATION from the doubled dt and
        // not an approximation -- the nominal taus (3.0 s, 8.0 s) are asserted
        // alongside, so a change to the control interval that forgot to re-derive
        // the coefficients fails here rather than drifting quietly.
        //
        // Recomputed in double, independently of the header, so a float
        // derivation inside updateBankCoefficients() fails this arm outright
        // rather than only destabilising Arm 1.
        const double controlDt = static_cast<double>(kControl) / kSampleRate;
        const auto tauMin = static_cast<double>(BrownianDrift::kTauMin);
        const auto tauMax = static_cast<double>(BrownianDrift::kTauMax);
        const auto internalStd = static_cast<double>(BrownianDrift::kInternalStd);

        auto checkBank = [&](float smoothness, double nominalTau, float storedA, float storedG) {
            const double tau = tauMin + (static_cast<double>(smoothness) * (tauMax - tauMin));
            REQUIRE(std::abs(tau - nominalTau) <= 1e-3);

            const double a = std::exp(-controlDt / tau);
            const double variance = 1.0 - (a * a);
            const double g = internalStd * std::sqrt(variance);

            REQUIRE(std::abs(static_cast<double>(storedA) - a) <= 1e-6 * std::abs(a));
            REQUIRE(std::abs(static_cast<double>(storedG) - g) <= 1e-6 * std::abs(g));
        };

        checkBank(EntropyProcessor::kAmpJitterSmoothness, 3.0, proc.getAmpJitterCoefficientA(),
                  proc.getAmpJitterCoefficientG());
        checkBank(EntropyProcessor::kDecoherenceSmoothness, 8.0,
                  proc.getDecoherenceCoefficientA(), proc.getDecoherenceCoefficientG());
    }

    SECTION("Arm 3: the 4 x 64 lane seeds are pairwise distinct and non-zero") {
        // The four disjoint salt ranges of plan section 4.4 -- amplitude jitter
        // [0, 64), decoherence [64, 128), static scatter [128, 192), lifecycle
        // [192, 256) -- are contiguous, so the cross product is salt in [0, 256).
        constexpr std::size_t kStreams = 4 * EntropyProcessor::kPartials;

        for (const std::uint32_t base : kSeeds) {
            INFO("base seed = " << base);
            std::array<std::uint32_t, kStreams> lanes{};
            for (std::size_t salt = 0; salt < kStreams; ++salt) {
                lanes[salt] = deriveStreamSeed(base, salt);
                // Zero is load-bearing: Xorshift32::seed() silently substitutes
                // its own default for 0 (core/random.h:73-75), so two lanes
                // hashing to 0 would collapse onto one stream.
                REQUIRE(lanes[salt] != 0u);
            }
            std::sort(lanes.begin(), lanes.end());
            REQUIRE(std::adjacent_find(lanes.begin(), lanes.end()) == lanes.end());
        }
    }
}

// =============================================================================
// T012 - the fixed-order stage application (FR-071, FR-072, FR-075)
// =============================================================================

namespace {

/// Number of partials driven by the T012 cases: the full bank.
constexpr std::size_t kCount = EntropyProcessor::kPartials;

/// The clean input spectrum, ratios n and amplitudes 1/n for n = 1..64.
///
/// It is rewritten before EVERY chunk. processChunk() perturbs IN PLACE, so
/// feeding back the previous chunk's output would compound 90,000 perturbations
/// into a product and measure something no caller ever sees; the engine hands the
/// entropy processor a freshly interpolated morph result on every chunk.
struct CleanSpectrum {
    std::array<float, kCount> ratios{};
    std::array<float, kCount> amplitudes{};

    void refill() noexcept {
        for (std::size_t i = 0; i < kCount; ++i) {
            const auto n = static_cast<float>(i + 1);
            ratios[i] = n;
            amplitudes[i] = 1.0f / n;
        }
    }
};

/// Cent deviation of a perturbed ratio from its clean input value n = index + 1.
[[nodiscard]] double deviationCents(float perturbed, std::size_t index) noexcept {
    const auto clean = static_cast<double>(index + 1);
    return 1200.0 * std::log2(static_cast<double>(perturbed) / clean);
}

/// Unbiased sample variance across a set of values.
[[nodiscard]] double sampleVariance(const double* values, std::size_t n) noexcept {
    double sum = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        sum += values[k];
    }
    const double mean = sum / static_cast<double>(n);
    double acc = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        const double d = values[k] - mean;
        acc += d * d;
    }
    return acc / static_cast<double>(n - 1);
}

/// Ordinary-least-squares slope of y on x, with the slope's own standard error.
struct LinearFit {
    double slope = 0.0;
    double slopeStdError = 0.0;
};

[[nodiscard]] LinearFit fitLine(const double* x, const double* y, std::size_t n) noexcept {
    double sx = 0.0;
    double sy = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        sx += x[k];
        sy += y[k];
    }
    const auto dn = static_cast<double>(n);
    const double meanX = sx / dn;
    const double meanY = sy / dn;

    double sxx = 0.0;
    double sxy = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        const double dx = x[k] - meanX;
        sxx += dx * dx;
        sxy += dx * (y[k] - meanY);
    }
    LinearFit fit;
    if (sxx <= 0.0 || n <= 2) {
        return fit;
    }
    fit.slope = sxy / sxx;
    const double intercept = meanY - (fit.slope * meanX);

    double sse = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        const double residual = y[k] - (intercept + (fit.slope * x[k]));
        sse += residual * residual;
    }
    const double residualVariance = sse / static_cast<double>(n - 2);
    fit.slopeStdError = std::sqrt(residualVariance / sxx);
    return fit;
}

} // namespace

TEST_CASE("EntropyProcessor_StagesEngageInOrder", "[entropy][seraphis]") {
    // SC-005. The four stages open at 0.00 / 0.25 / 0.50 / 0.75 (FR-071), so at a
    // given entropy the stages ABOVE it must be provably inert -- not "small", but
    // exactly neutral. Each arm below runs a clean input spectrum through the
    // processor for 2 s and reads the stage-specific introspection (FR-008).
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunkSamples = 64;
    constexpr std::size_t kNumChunks = 1500; // 2 s at 48 kHz

    // Assertions are ACCUMULATED into flags and checked once per arm rather than
    // asserted inside the 1500 x 64 loop: 96,000 REQUIREs per arm per seed would
    // dominate the suite's runtime for no extra coverage.
    for (const std::uint32_t seed : kSeeds) {
        INFO("seed = " << seed);

        // -- e = 0.10: stage 1 only -------------------------------------------
        {
            INFO("entropy = 0.10");
            EntropyProcessor proc;
            proc.setSeed(seed);
            proc.prepare(kSampleRate);
            proc.setEntropy(0.10f);

            CleanSpectrum spectrum;
            bool ratiosBitwiseClean = true;
            bool allAlive = true;
            bool allLifeUnity = true;
            bool anyJitter = false;

            for (std::size_t chunk = 0; chunk < kNumChunks; ++chunk) {
                spectrum.refill();
                proc.processChunk(spectrum.ratios.data(), spectrum.amplitudes.data(), kCount,
                                  kChunkSamples);
                for (std::size_t i = 0; i < kCount; ++i) {
                    // Stages 2 and 3 are both shut below e = 0.25 / 0.50, so the
                    // summed cent offset is exactly zero, centsToPitchRatio(0) is
                    // exactly 1.0f, and the ratio must come back BITWISE unchanged
                    // -- not merely close.
                    if (spectrum.ratios[i] != static_cast<float>(i + 1)) {
                        ratiosBitwiseClean = false;
                    }
                    if (proc.getLifePhase(i) != EntropyProcessor::LifePhase::Alive) {
                        allAlive = false;
                    }
                    if (proc.getLifeAmplitudeFactor(i) != 1.0f) {
                        allLifeUnity = false;
                    }
                    if (proc.getAmpJitterFactor(i) != 1.0f) {
                        anyJitter = true;
                    }
                }
            }

            REQUIRE(ratiosBitwiseClean);
            REQUIRE(allAlive);
            REQUIRE(allLifeUnity);
            REQUIRE(anyJitter); // stage 1 IS engaged: w1(0.10) = 0.2857
        }

        // -- e = 0.40: stages 1 and 2, stage 3 still shut ----------------------
        {
            INFO("entropy = 0.40");
            EntropyProcessor proc;
            proc.setSeed(seed);
            proc.prepare(kSampleRate);
            proc.setEntropy(0.40f);

            CleanSpectrum spectrum;
            bool anyRatioMoved = false;
            bool scatterAlwaysZero = true;
            bool noDeaths = true;

            for (std::size_t chunk = 0; chunk < kNumChunks; ++chunk) {
                spectrum.refill();
                proc.processChunk(spectrum.ratios.data(), spectrum.amplitudes.data(), kCount,
                                  kChunkSamples);
                for (std::size_t i = 0; i < kCount; ++i) {
                    if (spectrum.ratios[i] != static_cast<float>(i + 1)) {
                        anyRatioMoved = true;
                    }
                    // Stage 3 opens at 0.50: w3(0.40) is exactly 0, so the APPLIED
                    // scatter is exactly 0 even though the raw draw s_i is not.
                    if (proc.getAppliedScatterCents(i) != 0.0f) {
                        scatterAlwaysZero = false;
                    }
                    if (proc.getScatterRedrawCount(i) != 0u) {
                        noDeaths = false;
                    }
                }
            }

            REQUIRE(anyRatioMoved); // stage 2 IS engaged: w2(0.40) = 0.4286
            REQUIRE(scatterAlwaysZero);
            REQUIRE(noDeaths); // stage 4 opens at 0.75
        }

        // -- e = 0.65: stages 1, 2 and 3, stage 4 still shut -------------------
        {
            INFO("entropy = 0.65");
            EntropyProcessor proc;
            proc.setSeed(seed);
            proc.prepare(kSampleRate);
            proc.setEntropy(0.65f);

            CleanSpectrum spectrum;
            bool anyScatterApplied = false;
            bool noDeaths = true;

            for (std::size_t chunk = 0; chunk < kNumChunks; ++chunk) {
                spectrum.refill();
                proc.processChunk(spectrum.ratios.data(), spectrum.amplitudes.data(), kCount,
                                  kChunkSamples);
                for (std::size_t i = 0; i < kCount; ++i) {
                    if (proc.getAppliedScatterCents(i) != 0.0f) {
                        anyScatterApplied = true;
                    }
                    if (proc.getScatterRedrawCount(i) != 0u) {
                        noDeaths = false;
                    }
                }
            }

            REQUIRE(anyScatterApplied); // stage 3 IS engaged: w3(0.65) = 0.4286
            REQUIRE(noDeaths);
        }

        // -- e = 0.90: stage 4 engaged, a FULL lifecycle completes --------------
        {
            INFO("entropy = 0.90");
            // SC-005's death arm. w4(0.90) = (0.90 - 0.75) / 0.25 = 0.60, so each
            // partial's death rate is 0.60 * kMaxDeathRatePerSecond = 0.03 /s.
            //
            // DERIVATION OF THE RUN LENGTH (spec.md:1626-1629), which is why 60 s
            // and not a number picked until the test passed. The worst-case full
            // cycle is kMaxDeathFadeSec + kMaxDeadDwellSec + kMaxRebirthFadeSec =
            // 5.0 s, so every death that BEGINS before t = 55 s is guaranteed to
            // complete inside the run. Expected qualifying deaths per partial =
            // 0.03 * 55 = 1.65, so P(a given partial never dies) = exp(-1.65) =
            // 0.192 and P(none of the 64 dies) = 0.192^64 ~ 1e-46. The clause is
            // safe by ~46 orders of magnitude. IF kMaxDeathRatePerSecond IS EVER
            // LOWERED THIS ARITHMETIC MUST BE REDONE -- the run length may be
            // extended, the criterion may not be weakened.
            constexpr std::size_t kDeathChunks = 45000; // 60 s at 48 kHz
            static_assert(kDeathChunks * kChunkSamples == 2880000u, "60 s at 48 kHz");

            EntropyProcessor proc;
            proc.setSeed(seed);
            proc.prepare(kSampleRate);
            proc.setEntropy(0.90f);
            REQUIRE(std::abs(proc.getStageWeight(4) - 0.6f) <= 1e-6f);

            // Per-partial progress through Alive -> Dying -> Dead -> Reborn ->
            // Alive, advanced ONE STEP AT A TIME so that the FSM is observed to
            // traverse the phases IN ORDER rather than merely to have visited them.
            constexpr int kNotYetDying = 0;
            constexpr int kSeenDying = 1;
            constexpr int kSeenDead = 2;
            constexpr int kSeenReborn = 3;
            constexpr int kCycleComplete = 4;

            CleanSpectrum spectrum;
            std::array<int, kCount> progress{};
            std::array<float, kCount> drawBefore{};
            std::array<float, kCount> drawAfter{};
            for (std::size_t i = 0; i < kCount; ++i) {
                drawBefore[i] = proc.getRawScatterDraw(i);
            }

            for (std::size_t chunk = 0; chunk < kDeathChunks; ++chunk) {
                spectrum.refill();
                proc.processChunk(spectrum.ratios.data(), spectrum.amplitudes.data(), kCount,
                                  kChunkSamples);
                for (std::size_t i = 0; i < kCount; ++i) {
                    const EntropyProcessor::LifePhase phase = proc.getLifePhase(i);
                    switch (progress[i]) {
                    case kNotYetDying:
                        if (phase == EntropyProcessor::LifePhase::Dying) {
                            progress[i] = kSeenDying;
                        }
                        break;
                    case kSeenDying:
                        if (phase == EntropyProcessor::LifePhase::Dead) {
                            progress[i] = kSeenDead;
                        }
                        break;
                    case kSeenDead:
                        if (phase == EntropyProcessor::LifePhase::Reborn) {
                            progress[i] = kSeenReborn;
                        }
                        break;
                    case kSeenReborn:
                        if (phase == EntropyProcessor::LifePhase::Alive) {
                            progress[i] = kCycleComplete;
                            // Captured at COMPLETION, so a second death later in the
                            // run cannot retroactively supply the difference below.
                            drawAfter[i] = proc.getRawScatterDraw(i);
                        }
                        break;
                    default:
                        break;
                    }
                }
            }

            std::size_t completed = 0;
            std::size_t redrawn = 0;
            for (std::size_t i = 0; i < kCount; ++i) {
                if (progress[i] == kCycleComplete) {
                    ++completed;
                    if (drawAfter[i] != drawBefore[i]) {
                        ++redrawn;
                    }
                }
            }

            INFO("completed cycles = " << completed << ", of which redrawn = " << redrawn);
            REQUIRE(completed >= 1u);
            // FR-073 redraws s_i on EVERY death, so every completed cycle must have
            // moved its partial's static scatter draw -- not merely one of them. A
            // spurious equality would need two consecutive 32-bit draws to collide
            // (P = 2^-32 per cycle).
            REQUIRE(redrawn == completed);
        }
    }
}

// =============================================================================
// FR-073 deviation D5 -- the w_4 = 0 floor, and the bound on reaching it
// =============================================================================
// FR-073's `w_4 = 0` clause is RESTATED in spec.md as deviation D5: at w_4 = 0 no
// new death starts and every ALIVE partial has L_i exactly 1.0f, while a
// lifecycle already in flight runs to completion on the FR-073 ramps.
//
// THE DEVIATION EXISTS BECAUSE THE UNSCOPED FORM CONTRADICTS FR-044. Forcing a
// partial that is mid-Dead-window to Alive with L_i = 1.0f is a step of 1.0 in
// one chunk against kMaxAmpDeltaPerChunk = 0.025 -- 40x the bound -- and
// setEntropy(0) is legal at any moment. So the deviation is not free: it owes a
// BOUND, and this case is what turns that bound from prose into a gate.
//
// Three things are asserted, and the third is the one that would catch a
// lifecycle that simply froze instead of finishing:
//   1. within the pinned 5.0 s worst-case cycle after w_4 reaches 0, EVERY
//      partial is Alive with L_i BITWISE 1.0f;
//   2. no death starts after w_4 reaches 0 (the redraw counters stop moving);
//   3. every per-chunk step of L_i obeys the FR-073 ramp slope -- i.e. the
//      partials RAMP home rather than snapping, which is the whole reason the
//      literal form was given up. (L_i is NOT monotone across the settle: a
//      partial caught mid-`Dying` still falls to 0 first, which is exactly the
//      "runs to completion" half of the deviation.)
// =============================================================================

TEST_CASE("EntropyProcessor_LifecyclesSettleWhenStage4Closes", "[entropy][seraphis]") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunkSamples = 64;
    constexpr float kChunkSeconds = 64.0f / 48000.0f;

    /// FR-073's pinned worst-case cycle, DERIVED from the ramp constants rather
    /// than written as 5.0f, so lengthening any window updates the bound with it.
    constexpr float kWorstCaseCycleSec = EntropyProcessor::kMaxDeathFadeSec
                                         + EntropyProcessor::kMaxDeadDwellSec
                                         + EntropyProcessor::kMaxRebirthFadeSec;
    static_assert(kWorstCaseCycleSec == 5.0f, "FR-073's published worst-case full cycle");

    const auto settleChunks =
        static_cast<std::size_t>((kWorstCaseCycleSec / kChunkSeconds) + 1.0f);

    /// The steepest per-chunk step FR-073's ramps can produce: one chunk of
    /// seconds over the SHORTEST ramp window. DERIVED from the pinned constants,
    /// so shortening a window tightens this gate automatically. This is the
    /// contributor FR-044's amplitude row budgets as `chunkSeconds /
    /// kMinDeathFadeSec`, and it is 9.4x tighter than kMaxAmpDeltaPerChunk.
    constexpr float kMaxLifeStepPerChunk =
        kChunkSeconds / std::min(EntropyProcessor::kMinDeathFadeSec,
                                 EntropyProcessor::kMinRebirthFadeSec);
    /// Float slack on a value formed by a divide and a clamp; four orders below
    /// the bound itself, so it cannot hide a step.
    constexpr float kLifeStepSlack = 1e-6f;

    for (const std::uint32_t seed : kSeeds) {
        INFO("seed = " << seed);

        EntropyProcessor proc;
        proc.setSeed(seed);
        proc.prepare(kSampleRate);
        proc.setEntropy(0.90f); // w4 = 0.60: deaths ARE being started
        REQUIRE(std::abs(proc.getStageWeight(4) - 0.6f) <= 1e-6f);

        CleanSpectrum spectrum;

        // Run long enough that lifecycles are certainly in flight, then STOP at a
        // chunk where at least one partial is not Alive -- the corner the literal
        // form would have had to step through.
        constexpr std::size_t kMaxWindUpChunks = 45000; // 60 s at 48 kHz
        std::size_t inFlight = 0;
        for (std::size_t chunk = 0; chunk < kMaxWindUpChunks; ++chunk) {
            spectrum.refill();
            proc.processChunk(spectrum.ratios.data(), spectrum.amplitudes.data(), kCount,
                              kChunkSamples);
            inFlight = 0;
            for (std::size_t i = 0; i < kCount; ++i) {
                if (proc.getLifePhase(i) != EntropyProcessor::LifePhase::Alive) {
                    ++inFlight;
                }
            }
            if (inFlight > 0) {
                break;
            }
        }
        // Non-vacuity: with nothing in flight the settle below proves nothing.
        REQUIRE(inFlight > 0u);

        // w_4 -> 0. Entropy 0.5 is used rather than 0.0 deliberately: at exactly 0
        // processChunk takes its exact-zero fast path and never multiplies by L_i,
        // so a stuck lifecycle would be invisible in the OUTPUT. At 0.5 stage 4's
        // weight is still exactly 0 while the L_i multiply is live.
        proc.setEntropy(0.5f);
        REQUIRE(proc.getStageWeight(4) == 0.0f);

        std::array<float, kCount> previousLife{};
        std::array<EntropyProcessor::LifePhase, kCount> previousPhase{};
        for (std::size_t i = 0; i < kCount; ++i) {
            previousLife[i] = proc.getLifeAmplitudeFactor(i);
            previousPhase[i] = proc.getLifePhase(i);
        }

        int deltaBreaches = 0;
        int outOfRange = 0;
        int newDeaths = 0;
        float worstStep = 0.0f;
        for (std::size_t chunk = 0; chunk < settleChunks; ++chunk) {
            spectrum.refill();
            proc.processChunk(spectrum.ratios.data(), spectrum.amplitudes.data(), kCount,
                              kChunkSamples);
            for (std::size_t i = 0; i < kCount; ++i) {
                const float life = proc.getLifeAmplitudeFactor(i);
                // Negated-conjunction form so a NaN counts as out of range
                // (life < 0 || life > 1 is false for NaN).
                const bool lifeInRange = life >= 0.0f && life <= 1.0f;
                if (!lifeInRange) {
                    ++outOfRange;
                }
                const float step = std::abs(life - previousLife[i]);
                worstStep = std::max(worstStep, step);
                if (step > kMaxLifeStepPerChunk + kLifeStepSlack) {
                    ++deltaBreaches;
                }
                previousLife[i] = life;

                // A NEW death is an Alive -> Dying transition. Counting scatter
                // REDRAWS instead would count the in-flight lifecycle's own
                // Dying -> Dead redraw, which is the deviation working as
                // specified rather than a violation of it.
                const EntropyProcessor::LifePhase phase = proc.getLifePhase(i);
                if (previousPhase[i] == EntropyProcessor::LifePhase::Alive
                    && phase == EntropyProcessor::LifePhase::Dying) {
                    ++newDeaths;
                }
                previousPhase[i] = phase;
            }
        }

        int notSettled = 0;
        for (std::size_t i = 0; i < kCount; ++i) {
            if (proc.getLifePhase(i) != EntropyProcessor::LifePhase::Alive
                || proc.getLifeAmplitudeFactor(i) != 1.0f) {
                ++notSettled;
            }
        }

        INFO("in flight at close = " << inFlight << ", not settled after "
                                     << kWorstCaseCycleSec << " s = " << notSettled
                                     << ", new deaths = " << newDeaths << ", worst L_i step = "
                                     << worstStep << " against " << kMaxLifeStepPerChunk
                                     << ", delta breaches = " << deltaBreaches
                                     << ", out of range = " << outOfRange);
        // (1) BITWISE 1.0f, not "close to 1": the clause is stated as an exact
        //     assignment and the macOS leg builds -ffast-math.
        CHECK(notSettled == 0);
        // (2) w_4 = 0 starts no new death.
        CHECK(newDeaths == 0);
        // (3) They RAMPED home rather than stepping, which is what buys the
        //     deviation its keep.
        CHECK(deltaBreaches == 0);
        CHECK(outOfRange == 0);
    }
}

TEST_CASE("EntropyProcessor_PhaseDecoheres", "[entropy][seraphis]") {
    // SC-016. Stage 2 must decohere the partials WITHOUT walking any of them away:
    // the run mean of each partial's cent deviation stays bounded while the SPREAD
    // of accumulated phase error across partials grows without bound.
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunkSamples = 64;
    constexpr double kChunkSeconds = 64.0 / 48000.0;
    constexpr std::size_t kNumChunks = 90000; // 120 s at 48 kHz
    static_assert(kNumChunks * kChunkSamples == 5760000u, "120 s at 48 kHz");

    // The bound on every partial's RUN-MEAN cent deviation.
    //
    // DERIVATION (ARM A, entropy 0.45). w2(0.45) = (0.45 - 0.25) / 0.35 = 0.5714,
    // so the stage-2 term is 0.5714 * kMaxDecoherenceCents(4.0) * c_i, where c_i is
    // BrownianDrift's smoothed OU output with stationary sd kInternalStd = 0.5. The
    // per-sample sd is therefore sigma = 0.5714 * 4.0 * 0.5 = 1.143 cents. The
    // standard error of the RUN MEAN of an OU process of correlation time tau = 8 s
    // (kDecoherenceSmoothness maps to tau = 8 s) over T = 120 s is
    // sigma * sqrt(2 * tau / T) = 1.143 * sqrt(16 / 120) = 0.417 cents. 2.0 cents is
    // therefore 4.8 standard errors: P ~ 1.6e-6 per partial and ~8e-4 expected
    // exceedances over all 8 x 64 = 512 partials of ARM A.
    constexpr double kMeanRatioDriftCents = 2.0;

    SECTION("ARM A: entropy 0.45 -- bounded run mean, unbounded phase spread") {
        constexpr float kEntropy = 0.45f;
        constexpr std::size_t kSamplePoints = 40;
        constexpr std::size_t kSampleStride = kNumChunks / kSamplePoints; // 2250
        static_assert(kSampleStride * kSamplePoints == kNumChunks, "evenly spaced sample times");

        for (const std::uint32_t seed : kSeeds) {
            INFO("seed = " << seed);
            EntropyProcessor proc;
            proc.setSeed(seed);
            proc.prepare(kSampleRate);
            proc.setEntropy(kEntropy);

            // 0.45 is deliberately NOT 0.50: stage 3 must still be exactly shut, so
            // every cent of deviation measured below is stage 2's. Asserted BITWISE
            // so that moving the FR-071 boundaries fails here loudly rather than
            // quietly contaminating the statistics with a static scatter offset.
            REQUIRE(proc.getStageWeight(3) == 0.0f);

            CleanSpectrum spectrum;
            std::array<double, kCount> deviationSum{};
            std::array<double, kCount> phaseAccum{};
            std::array<double, kSamplePoints> sampleTimes{};
            std::array<double, kSamplePoints> phaseVariance{};
            std::size_t sampleIndex = 0;

            for (std::size_t chunk = 0; chunk < kNumChunks; ++chunk) {
                spectrum.refill();
                proc.processChunk(spectrum.ratios.data(), spectrum.amplitudes.data(), kCount,
                                  kChunkSamples);
                for (std::size_t i = 0; i < kCount; ++i) {
                    const double d = deviationCents(spectrum.ratios[i], i);
                    deviationSum[i] += d;
                    // Accumulated phase error is the time integral of the frequency
                    // deviation; in cents-seconds it is simply d * dt summed.
                    phaseAccum[i] += d * kChunkSeconds;
                }
                if (((chunk + 1) % kSampleStride) == 0) {
                    sampleTimes[sampleIndex] = static_cast<double>(chunk + 1) * kChunkSeconds;
                    phaseVariance[sampleIndex] = sampleVariance(phaseAccum.data(), kCount);
                    ++sampleIndex;
                }
            }
            REQUIRE(sampleIndex == kSamplePoints);

            // (a) + (c): every partial's run mean is within the bound, and the
            // COUNT of exceedances is 0 -- the same statement in both the "each"
            // and the "how many" form the spec uses.
            double worstRunMean = 0.0;
            std::size_t exceeding = 0;
            for (std::size_t i = 0; i < kCount; ++i) {
                const double runMean = deviationSum[i] / static_cast<double>(kNumChunks);
                worstRunMean = std::max(worstRunMean, std::abs(runMean));
                if (std::abs(runMean) > kMeanRatioDriftCents) {
                    ++exceeding;
                }
            }
            INFO("worst run mean = " << worstRunMean << " cents");
            REQUIRE(worstRunMean <= kMeanRatioDriftCents);
            REQUIRE(exceeding == 0u);

            // (b) The spread of accumulated phase error must GROW. Var[A(T)] of an
            // OU integral is asymptotically 2 * sigma^2 * tau * T, i.e. linear in
            // T, so a straight line through the 40 sampled variances must have a
            // slope that is positive and large against its own standard error. A
            // stage-2 implementation that merely offsets every partial by a
            // constant (or one that shares a single stream across lanes) produces a
            // FLAT variance curve and fails this clause while still passing (a).
            const LinearFit fit = fitLine(sampleTimes.data(), phaseVariance.data(), kSamplePoints);
            INFO("variance slope = " << fit.slope << " +/- " << fit.slopeStdError);
            REQUIRE(fit.slope > 0.0);
            REQUIRE(fit.slope > 5.0 * fit.slopeStdError);
        }
    }

    SECTION("ARM B: entropy 0.74 -- the static scatter dominates the run mean") {
        // 0.74, NOT the spec's 0.85 (deviation D17). At 0.85, w4 = 0.40 gives
        // 0.02 deaths/s, i.e. ~2.4 deaths per partial over 120 s, and FR-073
        // REDRAWS s_i on every death -- which turns the static offset into a
        // 2.4-step random walk, drops its sd from 4.04 to 2.33 cents and drops the
        // expected exceedance count to ~25 of 64. The spec's ">= 32" therefore
        // FAILS on a faithful implementation. 0.74 keeps w4 exactly 0, so the
        // offset is genuinely static and the arithmetic below is exact.
        constexpr float kEntropy = 0.74f;

        // DERIVATION OF THE GATES. At e = 0.74: w4 = 0 (stage 4 opens at 0.75);
        // w3 = (0.74 - 0.50) / 0.35 = 0.6857, so the static scatter offset is
        // 0.6857 * kMaxScatterCents(7.0) * s_i with s_i ~ U[-1, +1], i.e. an offset
        // ~ U[-4.80, +4.80]; w2 = 1.0 (saturated above 0.60), so the stage-2 term
        // is a zero-mean OU of sd 2.0 cents whose run-mean standard error over
        // 120 s is 2.0 * sqrt(16 / 120) = 0.73 cents. Hence
        //   P(|offset + noise| > 2.0) ~ 1 - 4.0 / 9.6 = 0.5833,
        // giving a mean of 37.33 exceedances of 64 with sd
        // sqrt(64 * 0.5833 * 0.4167) = 3.944.
        //   PER SEED: 24 is 3.38 sd below the mean, P(fail) ~ 3.6e-4 per seed.
        //   POOLED:   256 of 512 is 3.83 sd below the pooled mean of 298.7
        //             (sd sqrt(512 * 0.5833 * 0.4167) = 11.15).
        constexpr std::size_t kPerSeedGate = 24;
        constexpr std::size_t kPooledGate = 256;

        std::size_t pooledExceeding = 0;
        for (const std::uint32_t seed : kSeeds) {
            INFO("seed = " << seed);
            EntropyProcessor proc;
            proc.setSeed(seed);
            proc.prepare(kSampleRate);
            proc.setEntropy(kEntropy);

            // Stage 4 shut BITWISE: the whole derivation above rests on s_i never
            // being redrawn (FR-073), which is only true while w4 is exactly 0.
            REQUIRE(proc.getStageWeight(4) == 0.0f);
            REQUIRE(std::abs(proc.getStageWeight(3) - 0.6857f) <= 1e-4f);

            CleanSpectrum spectrum;
            std::array<double, kCount> deviationSum{};

            for (std::size_t chunk = 0; chunk < kNumChunks; ++chunk) {
                spectrum.refill();
                proc.processChunk(spectrum.ratios.data(), spectrum.amplitudes.data(), kCount,
                                  kChunkSamples);
                for (std::size_t i = 0; i < kCount; ++i) {
                    deviationSum[i] += deviationCents(spectrum.ratios[i], i);
                }
            }

            std::size_t exceeding = 0;
            for (std::size_t i = 0; i < kCount; ++i) {
                const double runMean = deviationSum[i] / static_cast<double>(kNumChunks);
                if (std::abs(runMean) > kMeanRatioDriftCents) {
                    ++exceeding;
                }
            }
            INFO("exceeding = " << exceeding << " of " << kCount);
            REQUIRE(exceeding >= kPerSeedGate);
            pooledExceeding += exceeding;
        }

        INFO("pooled exceeding = " << pooledExceeding << " of " << (kSeeds.size() * kCount));
        REQUIRE(pooledExceeding >= kPooledGate);
    }
}

// =============================================================================
// T013 - the stage-4 death/rebirth FSM (FR-073, FR-074)
// =============================================================================

TEST_CASE("EntropyProcessor_BoundedAtEverySetting", "[entropy][seraphis]") {
    // SC-006 / FR-074. The whole entropy range must be BOUNDED AND SMOOTH -- not
    // "usually small". 11 settings x 8 seeds x 60 s of 64-sample chunks at 48 kHz,
    // with a CLEAN input array re-fed every chunk (processChunk perturbs in place;
    // feeding its own output back would compound 45,000 perturbations into a
    // product no caller ever sees -- the engine hands over a freshly interpolated
    // morph result on every chunk).
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunkSamples = 64;
    constexpr std::size_t kNumChunks = 45000; // 60 s at 48 kHz
    static_assert(kNumChunks * kChunkSamples == 2880000u, "60 s at 48 kHz");
    constexpr int kNumSettings = 11; // 0.0, 0.1, ..., 1.0

    // FR-074's output bounds, DERIVED FROM THE CLASS CONSTANTS rather than typed
    // as 1.5 / 11.0, so that raising a cent magnitude fails here loudly instead of
    // quietly widening the criterion it is supposed to be measured against.
    constexpr float kMaxAmpGain = 1.0f + EntropyProcessor::kMaxAmpJitter;
    constexpr double kMaxDeviationCents =
        static_cast<double>(EntropyProcessor::kMaxDecoherenceCents) +
        static_cast<double>(EntropyProcessor::kMaxScatterCents);
    static_assert(kMaxAmpGain == 1.5f, "SC-006 clause 1 as published");
    static_assert(kMaxDeviationCents == 11.0, "SC-006 clause 2 as published");

    // The amplitude bound is ATTAINED, not approached: for e >= 0.35 the stage-1
    // weight is exactly 1.0f and the OU lane output is clamped to [-1, +1], so a
    // saturated lane produces the factor 1.5f EXACTLY and the output amplitude is
    // the correctly-rounded float product of the input amplitude and 1.5. Whether
    // that lands one ULP above or below a bound the TEST computes by its own route
    // is decided by float rounding alone -- and under /fp:fast (the flag this suite
    // is compiled with; see the CL command line for dsp_processors_tests) MSVC is
    // free to fold the test's `1.5f * (1.0f / n)` into a single `1.5f / n`
    // division, which rounds differently from the two-step product the processor
    // performs. That is a bit-exactness demand in disguise, exactly what
    // dsp/CLAUDE.md forbids, and it made this clause fire at e = 0.4 / seed 1 /
    // partial 50 on a value 1 ULP (9.7e-8 relative) over the bound.
    //
    // The clause is therefore evaluated in DOUBLE against the exact bound, with an
    // explicit rounding slack. MEASURED worst excess over the whole 11 x 8 grid:
    // 9.686e-08 relative (~1.6 ULP of float). The slack below is 10x that and four
    // orders of magnitude below any structural breach -- a mis-clamped stage weight
    // at e = 0.4 gives 1.571x (4.8 % over), a missing d-clamp gives more. The 1.5x
    // bound itself is NOT relaxed.
    constexpr double kMaxAmpGainExact = 1.0 + static_cast<double>(EntropyProcessor::kMaxAmpJitter);
    constexpr double kGainRoundingSlack = 1e-6;
    static_assert(kMaxAmpGainExact == static_cast<double>(kMaxAmpGain), "same bound, wider type");

    // FR-044's per-chunk deltas (spec.md:822), TRANSCRIBED rather than included:
    // they are SpectralMorphEngine's constants (Layer 3) and this is a Layer 2 TU,
    // which must not reach up a layer to name them.
    //
    // The entropy processor's own share of that budget, derived (FR-044's table
    // rows, spec.md:814-816): the amplitude-jitter OU can traverse at most
    // kMaxAmpJitter * 2 * (1 - exp(-T/tau)) = 8.85e-3 of the amplitude per chunk
    // and the death ramp a further T / kMinDeathFadeSec = 2.67e-3 of it, so with
    // an input amplitude of at most 1.0 and a jitter factor of at most 1.5 the
    // worst per-chunk amplitude move is 1.29e-2 -- half the 2.5e-2 budget. In
    // cents the decoherence OU contributes 0.071 of the 125.0. THIS CLAUSE
    // THEREFORE HAS ~2x AND ~1700x OF MARGIN BY CONSTRUCTION; a breach means the
    // FSM steps rather than ramps, which is exactly what it exists to catch.
    constexpr double kMaxAmpDeltaPerChunk = 0.025;
    constexpr double kMaxRatioDeltaCentsPerChunk = 125.0;

    for (int settingIndex = 0; settingIndex < kNumSettings; ++settingIndex) {
        const float entropy = static_cast<float>(settingIndex) / 10.0f;

        for (const std::uint32_t seed : kSeeds) {
            EntropyProcessor proc;
            proc.setSeed(seed);
            proc.prepare(kSampleRate);
            proc.setEntropy(entropy);

            CleanSpectrum spectrum;
            std::array<float, kCount> prevAmp{};
            std::array<double, kCount> prevDev{};
            std::array<float, kCount> prevLife{};
            std::array<std::uint32_t, kCount> prevRedraws{};
            bool havePrev = false;

            // Accumulated into flags and checked once per (setting, seed) run:
            // 45,000 x 64 REQUIREs per run x 88 runs would dominate the suite's
            // runtime without adding a single bit of coverage.
            bool allFinite = true;
            bool ampNonNegative = true;
            bool ampWithinGain = true;
            bool ratioPositive = true;
            bool ratioWithinBand = true;
            bool strictlyIncreasing = true;
            bool ampDeltaBounded = true;
            bool lifeDeltaBounded = true;
            bool ratioDeltaBounded = true;
            bool redrawsAtSilence = true;
            std::size_t observedRedraws = 0;

            double worstDeviation = 0.0;
            double worstGainExcess = 0.0;
            double worstAmpDelta = 0.0;
            double worstLifeDelta = 0.0;
            double worstRatioDelta = 0.0;

            for (std::size_t chunk = 0; chunk < kNumChunks; ++chunk) {
                spectrum.refill();
                proc.processChunk(spectrum.ratios.data(), spectrum.amplitudes.data(), kCount,
                                  kChunkSamples);

                for (std::size_t i = 0; i < kCount; ++i) {
                    const float amp = spectrum.amplitudes[i];
                    const float ratio = spectrum.ratios[i];
                    const float life = proc.getLifeAmplitudeFactor(i);
                    // CleanSpectrum's input amplitude, formed in double so the
                    // bound below is the exact real number 1.5/n and never a
                    // second float rounding the compiler may re-associate.
                    const double ampIn = 1.0 / static_cast<double>(i + 1);

                    if (!isFiniteFloat(amp) || !isFiniteFloat(ratio) || !isFiniteFloat(life)) {
                        allFinite = false;
                        continue; // every clause below is meaningless on a non-finite
                    }
                    if (amp < 0.0f) {
                        ampNonNegative = false;
                    }
                    const double gainExcess =
                        (static_cast<double>(amp) / (kMaxAmpGainExact * ampIn)) - 1.0;
                    worstGainExcess = std::max(worstGainExcess, gainExcess);
                    if (gainExcess > kGainRoundingSlack) {
                        ampWithinGain = false;
                    }
                    if (!(ratio > 0.0f)) {
                        ratioPositive = false;
                        continue; // deviationCents() would be -inf
                    }
                    if (i > 0 && ratio <= spectrum.ratios[i - 1]) {
                        strictlyIncreasing = false;
                    }

                    const double dev = deviationCents(ratio, i);
                    worstDeviation = std::max(worstDeviation, std::abs(dev));
                    if (std::abs(dev) > kMaxDeviationCents) {
                        ratioWithinBand = false;
                    }

                    // The complementary clause of SC-006, and the one that carries
                    // the inaudibility argument: the ratio DOES step when FR-073
                    // redraws s_i, and what makes that inaudible is that the
                    // partial contributes no energy at all when it happens.
                    // BITWISE == 0.0f, not "small".
                    const std::uint32_t redraws = proc.getScatterRedrawCount(i);
                    if (redraws != prevRedraws[i]) {
                        ++observedRedraws;
                        if (life != 0.0f) {
                            redrawsAtSilence = false;
                        }
                    }

                    if (havePrev) {
                        const double ampDelta =
                            std::abs(static_cast<double>(amp) - static_cast<double>(prevAmp[i]));
                        worstAmpDelta = std::max(worstAmpDelta, ampDelta);
                        if (ampDelta > kMaxAmpDeltaPerChunk) {
                            ampDeltaBounded = false;
                        }

                        // L_i ITSELF obeys the amplitude bound. This is the clause
                        // that proves death/rebirth RAMPS rather than steps: a
                        // literal force-to-Alive, or a missing linear fade, moves
                        // L_i by 1.0 in one chunk -- 40x the budget.
                        const double lifeDelta =
                            std::abs(static_cast<double>(life) - static_cast<double>(prevLife[i]));
                        worstLifeDelta = std::max(worstLifeDelta, lifeDelta);
                        if (lifeDelta > kMaxAmpDeltaPerChunk) {
                            lifeDeltaBounded = false;
                        }

                        // The ratio bound is asserted ONLY WHERE L_i > 0 (FR-074).
                        // The input array is identical on every chunk, so the
                        // difference of the two cent deviations IS the per-chunk
                        // change of the output ratio in cents.
                        if (life > 0.0f) {
                            const double ratioDelta = std::abs(dev - prevDev[i]);
                            worstRatioDelta = std::max(worstRatioDelta, ratioDelta);
                            if (ratioDelta > kMaxRatioDeltaCentsPerChunk) {
                                ratioDeltaBounded = false;
                            }
                        }
                    }

                    prevAmp[i] = amp;
                    prevDev[i] = dev;
                    prevLife[i] = life;
                    prevRedraws[i] = redraws;
                }
                havePrev = true;
            }

            INFO("entropy = " << entropy << ", seed = " << seed);
            INFO("worst deviation = " << worstDeviation << " cents, worst amp delta = "
                                      << worstAmpDelta << ", worst L_i delta = " << worstLifeDelta
                                      << ", worst ratio delta = " << worstRatioDelta
                                      << " cents, redraws = " << observedRedraws
                                      << ", worst gain excess = " << worstGainExcess);
            REQUIRE(allFinite);
            REQUIRE(ampNonNegative);
            REQUIRE(ampWithinGain);
            REQUIRE(ratioPositive);
            REQUIRE(ratioWithinBand);
            REQUIRE(strictlyIncreasing);
            REQUIRE(ampDeltaBounded);
            REQUIRE(lifeDeltaBounded);
            REQUIRE(ratioDeltaBounded);
            REQUIRE(redrawsAtSilence);
            REQUIRE(proc.stateFinite());

            // Guards against the two degenerate passes in which the clauses above
            // are all vacuously satisfied.
            if (entropy <= 0.75f) {
                // Stage 4 opens AT 0.75, so w4 is exactly 0 for every setting up to
                // and including it and no partial may die at all.
                REQUIRE(observedRedraws == 0u);
            } else {
                // At the lowest setting above the onset (0.8) w4 = 0.2, i.e. a death
                // rate of 0.01 /s: 0.01 * 60 * 64 = 38.4 expected deaths over the
                // run, so P(zero) = exp(-38.4) ~ 2e-17. If this fires, stage 4 is
                // not engaging and the redraw clause above proved nothing.
                REQUIRE(observedRedraws > 0u);
            }
        }
    }
}
