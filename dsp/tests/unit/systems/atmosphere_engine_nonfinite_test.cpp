// ==============================================================================
// Layer 3: System Tests - AtmosphereEngine, non-finite hygiene (SC-014)
//                                        (specs/seraphis-phase5-atmosphere)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase5-atmosphere/spec.md
//            specs/seraphis-phase5-atmosphere/plan.md   (S13.2, S13.3, S15.7)
//            specs/seraphis-phase5-atmosphere/tasks.md  (T002 creates this TU;
//                                                        T015 fills it)
//
// SCOPE OF THIS TU (plan.md S15's TU-ownership table): SC-014 ONLY -
//   AtmosphereEngine_NonFiniteHygiene, whose three clauses are
//     (a) every output sample finite after a NaN/Inf injection on the input;
//     (b) the ring is PRESERVED - after the injection, silence still lets
//         grains reproduce the pre-injection audio (correlation >= 0.99 against
//         the same render without injection) and getCaptureCapacitySamples()
//         is unchanged;
//     (c) 0 ClickDetector detections across the injection window, using
//         SC-003's pinned config.
//   Plus the separate sub-case for FR-063's second half: with the engine's own
//   state forced non-finite, silence() fires, grains retire under the FR-007
//   ramp, the output returns to exact zero and STAYS exactly zero with
//   getActiveGrainCount() == 0 (the latch - no auto-resume) until reset().
//
// THIS IS A SEPARATE TU BECAUSE OF ITS COMPILE FLAGS. It is the ONLY one of the
//   four Phase 5 TUs listed under "-fno-fast-math -fno-finite-math-only" in
//   dsp/tests/CMakeLists.txt, and those flags must NOT be applied to the other
//   three. Do not merge these cases into the main TU and do not add the main or
//   perf TU to that list - see the comment at the registration site.
//
// SC-014's FOURTH CLAUSE IS NOT HERE, BY DESIGN. Clauses (a)-(c) above prove
//   the guards work in a TU compiled with -fno-fast-math, which is the one
//   configuration the shipped header will never be compiled in.
//   AtmosphereEngine_NonFiniteGuardSurvivesFastMath therefore lives in
//   dsp/tests/unit/systems/atmosphere_engine_test.cpp, which is deliberately
//   absent from the -fno-fast-math list, and repeats clause (a)'s injection and
//   clause (b)'s ring-preservation check there. That copy is the one with teeth.
//
// CONSTRUCTING NON-FINITE VALUES: never std::numeric_limits<float>::quiet_NaN()
//   or infinity(), and never std::isnan / std::isinf. Build the values from bit
//   patterns through a VOLATILE sink so the construction survives -ffast-math
//   identically in both TUs (plan S15.7). Classify with
//   Krate::DSP::detail::isNaN (core/db_utils.h:54-57) and
//   Krate::DSP::detail::isInf (:175-178).
//
// ALLOCATION DETECTION: this TU must NOT include
//   <allocation_operator_overrides.h> - the single owner in dsp_systems_tests is
//   dsp/tests/unit/systems/selectable_oscillator_test.cpp:388, and a second
//   include is a duplicate-symbol link error. <allocation_detector.h> only (and
//   this TU needs neither).
//
// ------------------------------------------------------------------------------
// O-1 RESOLVED: HOW THE INTERNAL NON-FINITE PATH IS REACHED, WITHOUT A HOOK
// (plan S18 O-1, plan S13.3, task T015)
// ------------------------------------------------------------------------------
//   Plan S15.7 leaves open how FR-063's INTERNAL half is constructed: the input
//   sanitiser (atmosphere_engine.h renderGrainChunk(), plan S9.1) substitutes
//   0.0f for every non-finite input sample BEFORE the ring write, so the ring is
//   unpoisonable from the input, and every setter in the control table sanitises
//   its argument (plan S7), so no non-finite value can be pushed in through the
//   API either. O-1 offers two resolutions and names route (i) - a legitimate
//   arithmetic route - as strongly preferred over route (ii)'s
//   #if defined(KRATE_TESTING) injection point.
//
//   ROUTE (i) EXISTS AND IS TAKEN HERE, SO NO HOOK WAS ADDED AND
//   atmosphere_engine.h IS NOT EDITED BY THIS TASK. The engine's own poison
//   detector is an ACCUMULATOR, not a per-sample classifier: plan S13.3 sums
//   `busPoisonAccum_ += busL_[i] + busR_[i]` per sample over a 64-sample control
//   chunk and calls isFinite() ONCE at the chunk boundary (atmosphere_engine.h
//   runControlStep() step 6). Plan S13.3 states the consequence explicitly:
//   "A busPoisonAccum_ that overflows to +/-Inf from finite values inside one
//   64-sample chunk would be a false positive; the bus is bounded by SC-008's
//   analysis at well under 4, so 64 samples cannot reach 3.4e38."
//
//   That bound is an analysis of the INPUT LEVELS SC-008 measures, not a clamp
//   in the code. Feeding the engine finite-but-enormous audio - kHugeSample =
//   5e37, a perfectly legal float that every setter and the input sanitiser pass
//   through untouched - makes the per-sample bus finite but drives the 64-sample
//   accumulator past FLT_MAX. The engine's own state (busPoisonAccum_) is then
//   non-finite while nothing non-finite was ever handed to it. That is exactly
//   the condition FR-063's internal half exists for, reached through the shipped
//   code path with no test hook, no friend declaration and no header edit.
//
//   IT IS ALSO A REAL HAZARD, not a contrivance: Phase 7 chains this engine
//   downstream of HarmonicCloud and ContinuousBody, and a resonator excursion is
//   how an upstream stage delivers huge-but-finite audio in practice.
//
//   THE MAGNITUDE IS CHOSEN SO THE RAMP STAYS OBSERVABLE. kHugeSample is set so
//   that the SUM OVER 64 SAMPLES overflows while every INDIVIDUAL bus sample
//   stays finite - see the arithmetic at the sub-case itself. Had individual
//   samples gone to +/-Inf, the FR-007 fade would be Inf * silenceGain == Inf at
//   every step and the "grains retire under the 10 ms ramp" half of the clause
//   would be unmeasurable.
//
// ------------------------------------------------------------------------------
// CLAUSE (c)'s DETECTOR SIGMA IS T009's MEASURED ONE, NOT THE 5.0f DEFAULT
// ------------------------------------------------------------------------------
//   "SC-003's pinned config" means the config T009 actually pinned. Every field
//   is verbatim - frameSize 512, hopSize 256, energyThresholdDb -60.0f,
//   mergeGap 5 - and detectionThreshold is the ONE field SC-003 authorises
//   moving. T009 moved it off 5.0f and recorded why, measured, in the banner of
//   dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp: at sigma 5 the
//   false-positive floor is 47 to 9146 detections in EVERY one of its 30 cells,
//   reference render included, because ClickDetector thresholds on
//   mean + sigma*stddev of |dy| WITHIN each 512-sample frame
//   (artifact_detection.h:186-193, :209-218) and a frame spanning a grain
//   fade-in has a |dy| distribution whose own maximum clears mean + 5*stddev
//   with no discontinuity present. Its measured zero points bracket this file's
//   grainSeconds = 0.5 at 6.5 .. 7.5, and kClickThresholdSigma = 14.0f is that
//   worst measurement plus the same cross-toolchain margin T009 carries.
//
//   WHAT IS NOT RELAXED: the 0-detection requirement itself, and the REFERENCE
//   GATE. The un-injected render is required to read 0 over the same window
//   before the injected render is judged, so a sigma that had drifted into
//   uselessness could not hide - it would make both counts 0 for the wrong
//   reason, which the non-vacuousness assertions on window energy catch.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include "artifact_detection.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/atmosphere_engine.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// =============================================================================
// Shared constants
// =============================================================================

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;

/// The three bit patterns plan S15.7 names. Kept as constants rather than
/// literals at the injection sites so the pattern -> meaning mapping is stated
/// exactly once.
constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

// =============================================================================
// Non-finite construction and classification
// =============================================================================

/// Build a non-finite float from its bit pattern through a volatile sink.
/// std::numeric_limits<float>::quiet_NaN() / infinity() fold to finite garbage
/// under -ffast-math, so they are never used - and although THIS TU carries
/// -fno-fast-math -fno-finite-math-only, the identical helper is used by the
/// fourth clause in the main TU, which does not. One construction, valid in
/// both, is the point.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b = bits;  // defeats constant folding
    const std::uint32_t materialized = b;  // the volatile READ is the sink
    float f = 0.0f;
    std::memcpy(&f, &materialized, sizeof(f));
    return f;
}

/// FR-008's finiteness test, as a COMPOSITION of the two existing Layer 0
/// exponent-field bit tests - Krate::DSP::detail::isNaN (core/db_utils.h:54-57)
/// and detail::isInf (:175-178). Never std::isnan / std::isinf: FR-008 forbids
/// them phase-wide, and the same source has to read correctly next to the main
/// TU's fast-math copy.
[[nodiscard]] bool sampleIsFinite(float value) noexcept {
    return !Krate::DSP::detail::isNaN(value) && !Krate::DSP::detail::isInf(value);
}

// =============================================================================
// The pinned input
// =============================================================================

constexpr int kNumPartials = 9;
constexpr double kFundamentalHz = 220.0;
constexpr double kTwoPiD = 6.283185307179586476925286766559;

/// Peak trim. sum(1/n, n=1..9) = 2.829 bounds |sum| absolutely; the truncated
/// sawtooth series actually peaks near 1.85, so 0.25 keeps the input below 0.47
/// with no buffer-wide normalisation pass (which cannot be done here: the
/// generator is called per block and must stay phase-continuous across them).
constexpr float kStackScale = 0.25f;

/// @brief 220 Hz fundamental plus partials 2x..9x at 1/n amplitude, all sine,
///        zero phase, both channels identical.
///
/// DELIBERATELY THE SAME GENERATOR T009 PINNED (the makeHarmonicStack in
/// atmosphere_engine_spectral_test.cpp), because clause (c) inherits T009's
/// MEASURED detector floor and a different input would invalidate that
/// measurement. Far from Gaussian by construction - its first difference is
/// dominated by one large jump per 220 Hz period - which is what keeps
/// ClickDetector's within-frame mean + sigma*stddev statistic usable at all.
///
/// The phase argument is formed from the ABSOLUTE sample index in double, never
/// by accumulating a per-sample increment, so a multi-block render carries no
/// phase drift and the two engines below see bit-identical input.
void fillHarmonicStack(float* outLeft, float* outRight, std::size_t numSamples,
                       std::size_t startSample, double sampleRate) noexcept {
    for (std::size_t i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(startSample + i) / sampleRate;
        double sum = 0.0;
        for (int n = 1; n <= kNumPartials; ++n) {
            const double nd = static_cast<double>(n);
            sum += std::sin(kTwoPiD * kFundamentalHz * nd * t) / nd;
        }
        const float value = static_cast<float>(sum) * kStackScale;
        outLeft[i] = value;
        outRight[i] = value;
    }
}

// =============================================================================
// Window metrics
// =============================================================================

/// Normalised cross-product over [first, last). No mean removal: both renders
/// are zero-mean audio, and the un-centred form is what makes a NON-FINITE
/// sample propagate into the result (so a buffer that failed clause (a) cannot
/// quietly pass clause (b)). Returns 0 for an empty or silent window, which
/// fails the >= 0.99 gate rather than passing it.
[[nodiscard]] double windowCorrelation(const std::vector<float>& a, const std::vector<float>& b,
                                       std::size_t first, std::size_t last) {
    if (last <= first || last > a.size() || last > b.size()) {
        return 0.0;
    }
    double sumAA = 0.0;
    double sumBB = 0.0;
    double sumAB = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const double x = static_cast<double>(a[i]);
        const double y = static_cast<double>(b[i]);
        sumAA += x * x;
        sumBB += y * y;
        sumAB += x * y;
    }
    const double denominator = std::sqrt(sumAA * sumBB);
    return (denominator > 0.0) ? (sumAB / denominator) : 0.0;
}

[[nodiscard]] double windowRms(const std::vector<float>& v, std::size_t first, std::size_t last) {
    if (last <= first || last > v.size()) {
        return 0.0;
    }
    double sumSquares = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const double x = static_cast<double>(v[i]);
        sumSquares += x * x;
    }
    return std::sqrt(sumSquares / static_cast<double>(last - first));
}

/// dBFS of an RMS, floored so a silent window is a finite number (a -inf cannot
/// even be formed in this component's vocabulary - see the file banner).
[[nodiscard]] double rmsToDb(double rms) {
    return 20.0 * std::log10(std::max(rms, 1e-12));
}

[[nodiscard]] float windowPeak(const std::vector<float>& v, std::size_t first, std::size_t last) {
    float peak = 0.0f;
    if (last <= first || last > v.size()) {
        return peak;
    }
    for (std::size_t i = first; i < last; ++i) {
        peak = std::max(peak, std::abs(v[i]));
    }
    return peak;
}

[[nodiscard]] bool allSamplesFinite(const std::vector<float>& v) {
    for (const float sample : v) {
        if (!sampleIsFinite(sample)) {
            return false;
        }
    }
    return true;
}

// =============================================================================
// Clause (c)'s detector
// =============================================================================

/// See the banner section on the sigma. Every other field is SC-003's verbatim
/// pinned value.
constexpr float kClickThresholdSigma = 14.0f;

/// Designated initialisers throughout - Clang rejects narrowing in brace
/// initialisation - and the field order is the declaration order at
/// artifact_detection.h:38-45.
[[nodiscard]] std::size_t countClicks(const std::vector<float>& buffer, std::size_t first,
                                      std::size_t last) {
    if (last <= first || last > buffer.size()) {
        return 0u;
    }
    Krate::DSP::TestUtils::ClickDetectorConfig cfg{.sampleRate = static_cast<float>(kSampleRate),
                                                  .frameSize = 512,
                                                  .hopSize = 256,
                                                  .detectionThreshold = kClickThresholdSigma,
                                                  .energyThresholdDb = -60.0f,
                                                  .mergeGap = 5};
    Krate::DSP::TestUtils::ClickDetector detector(cfg);
    detector.prepare();
    return detector.detect(buffer.data() + first, last - first).size();
}

}  // namespace

// =============================================================================
// SC-014 - AtmosphereEngine_NonFiniteHygiene
// =============================================================================
// Why SC-014 is needed at all (plan S15.7): SC-008 asserts finiteness only under
// FULL-SCALE WHITE NOISE, which is finite input - it can never reach FR-063's
// substitution path, and it can never observe whether the ring survived.

TEST_CASE("AtmosphereEngine_NonFiniteHygiene", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    SECTION("input injection: finite output, ring preserved, no clicks") {
        // --- Geometry. Stated in BLOCK INDICES so the schedule is a property of
        //     the test rather than of how fast the machine runs.
        constexpr std::size_t kWarmBlocks = 188;   // 96 256 samples = 2.005 s
        constexpr std::size_t kTailBlocks = 60;    // 30 720 samples = 0.640 s
        constexpr std::size_t kTotalBlocks = kWarmBlocks + 1 + kTailBlocks;
        constexpr std::size_t kInjectSample = kWarmBlocks * kBlock;  // 96 256

        // Clause (b)'s window: 47 blocks = 24 064 samples = 0.501 s, starting at
        // the injection. THE WINDOW LENGTH IS LOAD-BEARING, not a round number.
        // positionSeconds = 1.0 with positionSpread = 0, pitch 0, pitchSpread 0
        // and driftRangeSemitones 0 pins every grain's ratio at EXACTLY 1.0
        // (semitonesToRatio(0)), so the age moves by 1 - r == 0 per sample and
        // every grain reads at a CONSTANT age of 48 000 samples for its whole
        // life. Output at sample u therefore reproduces ring content written at
        // u - 48 000, and every sample of this window reads
        // [48 256, 72 320) - all of it written BEFORE the injection at 96 256.
        // "a window whose birth read age predates the injection", exactly.
        constexpr std::size_t kRingWindowBlocks = 47;
        constexpr std::size_t kRingWindowFirst = kInjectSample;
        constexpr std::size_t kRingWindowLast = kInjectSample + kRingWindowBlocks * kBlock;

        // Clause (c)'s window: +/- 24 blocks (0.256 s each side) around the
        // injection, so the injected samples sit in the middle of it. 48 frames
        // at frameSize 512 / hopSize 256.
        constexpr std::size_t kClickHalfBlocks = 24;
        constexpr std::size_t kClickFirst = kInjectSample - kClickHalfBlocks * kBlock;
        constexpr std::size_t kClickLast = kInjectSample + kClickHalfBlocks * kBlock;

        AtmosphereEngine::PrepareConfig config;
        config.captureSeconds = 8.0f;
        // Blur and freeze OFF, for SC-003's reason: this clause measures the
        // GRAIN path's response to a poisoned input sample. Routing the sum
        // through an STFT <-> OverlapAdd round-trip would fold a COLA question
        // into a criterion that is not about it, and would shift every sample by
        // the blur latency (RA-3), moving both windows off the material they are
        // computed to sit on.
        config.blurEnabled = false;
        config.freezeEnabled = false;

        // Two engines, identically prepared, identically seeded (the FR-009
        // default seed is 1 for both). AtmosphereEngine deletes copy
        // (atmosphere_engine.h:344-345), so these are two independent objects
        // configured by the same sequence of calls.
        AtmosphereEngine injected;
        AtmosphereEngine reference;
        for (AtmosphereEngine* engine : {&injected, &reference}) {
            engine->prepare(kSampleRate, config);
            engine->setDensity(8.0f);
            engine->setGrainSeconds(0.5f);
            engine->setPositionSeconds(1.0f);
            engine->setPositionSpread(0.0f);  // pins the read age - see above
            engine->setPitchSemitones(0.0f);
            engine->setPitchSpread(0.0f);
            engine->setDriftDepth(0.0f);
            engine->setDriftRangeSemitones(0.0f);
            engine->setPanSpread(0.5f);
            engine->setDecorrelation(0.0f);  // both channels read the same age
            engine->setLevel(1.0f);
        }

        const std::size_t capacityBefore = injected.getCaptureCapacitySamples();
        REQUIRE(capacityBefore > 0u);
        REQUIRE(reference.getCaptureCapacitySamples() == capacityBefore);

        std::vector<float> cleanLeft(kBlock, 0.0f);
        std::vector<float> cleanRight(kBlock, 0.0f);
        std::vector<float> dirtyLeft(kBlock, 0.0f);
        std::vector<float> dirtyRight(kBlock, 0.0f);
        std::vector<float> blockOutLeft(kBlock, 0.0f);
        std::vector<float> blockOutRight(kBlock, 0.0f);

        std::vector<float> injectedLeft;
        std::vector<float> injectedRight;
        std::vector<float> referenceLeft;
        std::vector<float> referenceRight;
        injectedLeft.reserve(kTotalBlocks * kBlock);
        injectedRight.reserve(kTotalBlocks * kBlock);
        referenceLeft.reserve(kTotalBlocks * kBlock);
        referenceRight.reserve(kTotalBlocks * kBlock);

        bool injectedInputWasNonFinite = false;

        for (std::size_t b = 0; b < kTotalBlocks; ++b) {
            // Signal up to and including the injection block, then SILENCE -
            // clause (b) asks for the ring to keep speaking after the input
            // stops. The engine is wet-only (FR-062), so the switch to silence
            // has no immediate effect on the output at all; only the material a
            // grain reads 1 s later would change, and that is outside both
            // windows.
            if (b <= kWarmBlocks) {
                fillHarmonicStack(cleanLeft.data(), cleanRight.data(), kBlock, b * kBlock,
                                  kSampleRate);
            } else {
                std::fill(cleanLeft.begin(), cleanLeft.end(), 0.0f);
                std::fill(cleanRight.begin(), cleanRight.end(), 0.0f);
            }

            const float* dirtyL = cleanLeft.data();
            const float* dirtyR = cleanRight.data();
            if (b == kWarmBlocks) {
                // NaN and BOTH infinities, on both channels, scattered across
                // several 64-sample control chunks - plus one contiguous run
                // covering a WHOLE chunk, so both the sanitiser's per-sample
                // substitution and its whole-chunk slow path are exercised.
                dirtyLeft = cleanLeft;
                dirtyRight = cleanRight;
                dirtyLeft[100] = makeNonFinite(kQuietNaNBits);
                dirtyRight[101] = makeNonFinite(kPosInfBits);
                dirtyLeft[200] = makeNonFinite(kNegInfBits);
                dirtyRight[200] = makeNonFinite(kNegInfBits);
                dirtyLeft[201] = makeNonFinite(kPosInfBits);
                dirtyRight[201] = makeNonFinite(kQuietNaNBits);
                for (std::size_t i = 320; i < 384; ++i) {  // one entire control chunk
                    dirtyLeft[i] = makeNonFinite(kQuietNaNBits);
                    dirtyRight[i] = makeNonFinite(kNegInfBits);
                }
                dirtyLeft[460] = makeNonFinite(kPosInfBits);
                dirtyRight[461] = makeNonFinite(kNegInfBits);
                for (std::size_t i = 0; i < kBlock; ++i) {
                    if (!sampleIsFinite(dirtyLeft[i]) || !sampleIsFinite(dirtyRight[i])) {
                        injectedInputWasNonFinite = true;
                    }
                }
                dirtyL = dirtyLeft.data();
                dirtyR = dirtyRight.data();
            }

            injected.processStereoBlock(dirtyL, dirtyR, blockOutLeft.data(), blockOutRight.data(),
                                        kBlock);
            injectedLeft.insert(injectedLeft.end(), blockOutLeft.begin(), blockOutLeft.end());
            injectedRight.insert(injectedRight.end(), blockOutRight.begin(), blockOutRight.end());

            reference.processStereoBlock(cleanLeft.data(), cleanRight.data(), blockOutLeft.data(),
                                         blockOutRight.data(), kBlock);
            referenceLeft.insert(referenceLeft.end(), blockOutLeft.begin(), blockOutLeft.end());
            referenceRight.insert(referenceRight.end(), blockOutRight.begin(), blockOutRight.end());
        }

        // --- Non-vacuousness FIRST. A silent engine, or one that never bore a
        //     grain, passes every clause below for the wrong reason.
        const std::uint64_t born = injected.getTotalGrainsBorn();
        const double referenceRms = windowRms(referenceLeft, kRingWindowFirst, kRingWindowLast);
        const double clickWindowRms = windowRms(injectedLeft, kClickFirst, kClickLast);
        CAPTURE(born, referenceRms, rmsToDb(referenceRms), clickWindowRms);
        REQUIRE(injectedInputWasNonFinite);
        REQUIRE(born > 0u);
        REQUIRE(reference.getTotalGrainsBorn() == born);
        REQUIRE(rmsToDb(referenceRms) > -60.0);
        // Clause (c)'s window must clear ClickDetector's own energy gate
        // (energyThresholdDb = -60.0f, artifact_detection.h) or the detector
        // skips the frame and reports 0 for no reason at all.
        REQUIRE(rmsToDb(clickWindowRms) > -60.0);

        // --- Clause (a): EVERY output sample finite, across the whole render.
        REQUIRE(allSamplesFinite(injectedLeft));
        REQUIRE(allSamplesFinite(injectedRight));
        REQUIRE(allSamplesFinite(referenceLeft));
        REQUIRE(allSamplesFinite(referenceRight));

        // --- Clause (b): THE RING IS PRESERVED. FR-063's input policy
        //     substitutes 0.0f and leaves the ring alone - it is NOT a
        //     silence-on-NaN policy - so grains born before the injection keep
        //     reproducing the pre-injection audio. Over a window that reads only
        //     material written before the injection the two renders agree.
        const double correlationLeft =
            windowCorrelation(injectedLeft, referenceLeft, kRingWindowFirst, kRingWindowLast);
        const double correlationRight =
            windowCorrelation(injectedRight, referenceRight, kRingWindowFirst, kRingWindowLast);
        CAPTURE(correlationLeft, correlationRight);
        REQUIRE(correlationLeft >= 0.99);
        REQUIRE(correlationRight >= 0.99);

        // ... and the ring itself did not move under the engine.
        REQUIRE(injected.getCaptureCapacitySamples() == capacityBefore);
        REQUIRE(reference.getCaptureCapacitySamples() == capacityBefore);

        // --- Clause (c): 0 detections across the injection window. The
        //     REFERENCE gate runs first: if the un-injected render does not read
        //     0 the sigma has drifted and the injected count means nothing.
        const std::size_t referenceClicksLeft = countClicks(referenceLeft, kClickFirst, kClickLast);
        const std::size_t referenceClicksRight =
            countClicks(referenceRight, kClickFirst, kClickLast);
        const std::size_t injectedClicksLeft = countClicks(injectedLeft, kClickFirst, kClickLast);
        const std::size_t injectedClicksRight = countClicks(injectedRight, kClickFirst, kClickLast);
        CAPTURE(kClickThresholdSigma, referenceClicksLeft, referenceClicksRight,
                injectedClicksLeft, injectedClicksRight);
        REQUIRE(referenceClicksLeft == 0u);
        REQUIRE(referenceClicksRight == 0u);
        REQUIRE(injectedClicksLeft == 0u);
        REQUIRE(injectedClicksRight == 0u);
    }

    SECTION("internal non-finite: silence() fires, and the latch does not resume") {
        // --- THE INTERNAL PATH, REACHED WITHOUT A TEST HOOK. Read the O-1
        //     section of the file banner first; the arithmetic it refers to is
        //     here.
        //
        //     kHugeSample = 5e37 is a legal finite float. It passes the input
        //     sanitiser untouched (each individual sample IS finite) and reaches
        //     the ring unchanged, so every grain reads +/-5e37.
        //
        //     PER-SAMPLE BUS (must stay FINITE, or the FR-007 fade below is
        //     unmeasurable): density 4 x grainSeconds 0.4 puts the mean
        //     concurrent count at 1.6, and panSpread = 0 fixes both pan gains at
        //     cos(pi/4) = 0.7071. A sample of the bus is therefore at most
        //     n * 0.7071 * 5e37 before the 1/sqrt(n) trim, which needs n >= 10
        //     concurrent grains to reach FLT_MAX - six times the mean, and the
        //     trip below happens within ~30 ms of the FIRST grain's birth.
        //
        //     THE 64-SAMPLE ACCUMULATOR (must OVERFLOW): plan S13.3 sums
        //     busL + busR per sample across the control chunk. One grain at
        //     envelope e contributes 2 * e * 0.7071 * 5e37 per sample, so the
        //     chunk sum passes FLT_MAX = 3.4e38 once e exceeds about 0.075 -
        //     roughly 7.5 % into a Hann window, i.e. ~30 ms into a 0.4 s grain.
        //     From there runControlStep() step 6's single isFinite() call sees a
        //     non-finite busPoisonAccum_ and fires silence().
        constexpr float kHugeSample = 5.0e37f;
        constexpr std::size_t kMaxBlocks = 400;  // 204 800 samples = 4.27 s
        const auto rampSamples =
            static_cast<std::size_t>(static_cast<double>(AtmosphereEngine::kSilenceRampMs) * 0.001 *
                                     kSampleRate);  // 480

        // The whole point of route (i): nothing non-finite is ever handed in.
        REQUIRE(sampleIsFinite(kHugeSample));

        AtmosphereEngine::PrepareConfig config;
        config.captureSeconds = 2.0f;
        config.blurEnabled = false;
        config.freezeEnabled = false;

        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        engine.setDensity(4.0f);
        engine.setGrainSeconds(0.4f);
        engine.setJitter(0.0f);
        engine.setPositionSeconds(0.2f);
        engine.setPositionSpread(0.0f);
        engine.setPitchSemitones(0.0f);
        engine.setPitchSpread(0.0f);
        engine.setDriftDepth(0.0f);
        engine.setDriftRangeSemitones(0.0f);
        engine.setPanSpread(0.0f);
        engine.setDecorrelation(0.0f);
        engine.setLevel(1.0f);

        const std::vector<float> hugeBlock(kBlock, kHugeSample);
        std::vector<float> blockOutLeft(kBlock, 0.0f);
        std::vector<float> blockOutRight(kBlock, 0.0f);

        std::vector<float> outLeft;
        std::vector<float> outRight;
        outLeft.reserve(kMaxBlocks * kBlock);
        outRight.reserve(kMaxBlocks * kBlock);

        std::size_t latchBlock = kMaxBlocks;
        for (std::size_t b = 0; b < kMaxBlocks; ++b) {
            engine.processStereoBlock(hugeBlock.data(), hugeBlock.data(), blockOutLeft.data(),
                                      blockOutRight.data(), kBlock);
            outLeft.insert(outLeft.end(), blockOutLeft.begin(), blockOutLeft.end());
            outRight.insert(outRight.end(), blockOutRight.begin(), blockOutRight.end());

            // The latch's signature, and nothing weaker: a WHOLE block of exact
            // zeros with no live grain, after grains have demonstrably existed.
            // An engine still running with grains in flight cannot produce one.
            const bool blockIsZero =
                std::all_of(blockOutLeft.begin(), blockOutLeft.end(),
                            [](float v) { return v == 0.0f; }) &&
                std::all_of(blockOutRight.begin(), blockOutRight.end(),
                            [](float v) { return v == 0.0f; });
            if (engine.getTotalGrainsBorn() > 0u && blockIsZero &&
                engine.getActiveGrainCount() == 0u) {
                latchBlock = b;
                break;
            }
        }

        const std::uint64_t bornAtLatch = engine.getTotalGrainsBorn();
        const std::uint64_t retiredAtLatch = engine.getTotalGrainsRetired();
        const std::uint64_t poolSkipAtLatch = engine.getSkippedTriggerCountPoolFull();
        const std::uint64_t coldSkipAtLatch = engine.getSkippedTriggerCountRingCold();
        CAPTURE(latchBlock, bornAtLatch, retiredAtLatch);

        // --- silence() FIRED, from the engine's own state.
        REQUIRE(latchBlock < kMaxBlocks);
        REQUIRE(bornAtLatch > 0u);
        REQUIRE(engine.getActiveGrainCount() == 0u);
        // latchNow() increments totalRetired_ BEFORE zeroing activeCount_, so
        // FR-072's identity holds through the latch. Every grain retired; none
        // was stolen or dropped.
        REQUIRE(retiredAtLatch == bornAtLatch);

        // --- GRAINS RETIRED UNDER THE 10 ms RAMP, not under a hard mute. The
        //     ramp is a per-output-sample multiply (plan S13.1), so the last
        //     rampSamples before the latch carry a gain sweeping 1 -> 0 on top
        //     of a grain envelope that is still slowly RISING. Comparing the
        //     head of that span against its tail therefore measures the ramp and
        //     essentially nothing else; a hard mute leaves both windows at the
        //     same magnitude.
        std::size_t lastNonZero = 0;
        bool anyNonZero = false;
        for (std::size_t i = 0; i < outLeft.size(); ++i) {
            if (outLeft[i] != 0.0f || outRight[i] != 0.0f) {
                lastNonZero = i;
                anyNonZero = true;
            }
        }
        REQUIRE(anyNonZero);
        const std::size_t latchSample = lastNonZero + 1;
        // 64 samples, not more: the late probe must sit where the ramp gain is
        // already small (<= 64/480 = 0.133), so the 0.5x bound below has margin
        // even if a second grain happened to be born inside the 10 ms ramp.
        constexpr std::size_t kRampProbeSamples = 64;
        REQUIRE(latchSample > rampSamples + kRampProbeSamples);

        const std::size_t earlyFirst = latchSample - rampSamples;
        const float earlyPeak = std::max(
            windowPeak(outLeft, earlyFirst, earlyFirst + kRampProbeSamples),
            windowPeak(outRight, earlyFirst, earlyFirst + kRampProbeSamples));
        const float latePeak =
            std::max(windowPeak(outLeft, latchSample - kRampProbeSamples, latchSample),
                     windowPeak(outRight, latchSample - kRampProbeSamples, latchSample));
        CAPTURE(latchSample, rampSamples, earlyPeak, latePeak);
        // Finite, or the comparison below is vacuous (anything < 0.5 * Inf).
        // This also records that the magnitude engineering in the section
        // preamble held: the per-sample bus never overflowed, only the
        // 64-sample accumulator did.
        REQUIRE(sampleIsFinite(earlyPeak));
        REQUIRE(sampleIsFinite(latePeak));
        REQUIRE(earlyPeak > 0.0f);
        REQUIRE(latePeak < 0.5f * earlyPeak);

        // --- THE LATCH: exactly 0.0f for the remainder of the render, with no
        //     counter moving. Fed NORMAL audio now, so an auto-resume would have
        //     everything it needs to restart - and must not.
        constexpr std::size_t kLatchedBlocks = 188;  // 2.005 s
        std::vector<float> normalLeft(kBlock, 0.0f);
        std::vector<float> normalRight(kBlock, 0.0f);
        bool latchedSpanIsExactlyZero = true;
        for (std::size_t b = 0; b < kLatchedBlocks; ++b) {
            fillHarmonicStack(normalLeft.data(), normalRight.data(), kBlock, b * kBlock,
                              kSampleRate);
            engine.processStereoBlock(normalLeft.data(), normalRight.data(), blockOutLeft.data(),
                                      blockOutRight.data(), kBlock);
            for (std::size_t i = 0; i < kBlock; ++i) {
                if (blockOutLeft[i] != 0.0f || blockOutRight[i] != 0.0f) {
                    latchedSpanIsExactlyZero = false;
                }
            }
        }
        REQUIRE(latchedSpanIsExactlyZero);
        REQUIRE(engine.getActiveGrainCount() == 0u);
        REQUIRE(engine.getTotalGrainsBorn() == bornAtLatch);
        REQUIRE(engine.getTotalGrainsRetired() == retiredAtLatch);
        REQUIRE(engine.getSkippedTriggerCountPoolFull() == poolSkipAtLatch);
        REQUIRE(engine.getSkippedTriggerCountRingCold() == coldSkipAtLatch);

        // --- reset() IS THE ONE RE-ENTRY (FR-006, FR-007). There is no
        //     resume(), and FR-063's internal path recovers the same way an
        //     explicit silence() does.
        engine.reset();
        REQUIRE(engine.getTotalGrainsBorn() == 0u);
        REQUIRE(engine.getTotalGrainsRetired() == 0u);

        // reset() also cleared the ring, so the engine speaks again only once it
        // has refilled past the birth read age (0.2 s here).
        constexpr std::size_t kRevivalBlocks = 282;  // 3.008 s
        std::vector<float> revivalLeft;
        std::vector<float> revivalRight;
        revivalLeft.reserve(kRevivalBlocks * kBlock);
        revivalRight.reserve(kRevivalBlocks * kBlock);
        for (std::size_t b = 0; b < kRevivalBlocks; ++b) {
            fillHarmonicStack(normalLeft.data(), normalRight.data(), kBlock, b * kBlock,
                              kSampleRate);
            engine.processStereoBlock(normalLeft.data(), normalRight.data(), blockOutLeft.data(),
                                      blockOutRight.data(), kBlock);
            revivalLeft.insert(revivalLeft.end(), blockOutLeft.begin(), blockOutLeft.end());
            revivalRight.insert(revivalRight.end(), blockOutRight.begin(), blockOutRight.end());
        }

        // Measured over the SECOND half only: the first half is the ring refill,
        // which is legitimately silent.
        const std::size_t revivalFirst = revivalLeft.size() / 2;
        const double revivalRms = windowRms(revivalLeft, revivalFirst, revivalLeft.size());
        CAPTURE(engine.getTotalGrainsBorn(), revivalRms, rmsToDb(revivalRms));
        REQUIRE(engine.getTotalGrainsBorn() > 0u);
        REQUIRE(engine.getActiveGrainCount() > 0u);
        REQUIRE(rmsToDb(revivalRms) > -60.0);
        REQUIRE(allSamplesFinite(revivalLeft));
        REQUIRE(allSamplesFinite(revivalRight));
    }
}
