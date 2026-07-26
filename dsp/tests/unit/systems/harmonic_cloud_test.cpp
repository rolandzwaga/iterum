// ==============================================================================
// Layer 3: System Tests - HarmonicCloud
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per specs/seraphis-phase2-harmonic-cloud/
//
// Reference: specs/seraphis-phase2-harmonic-cloud/spec.md
//            specs/seraphis-phase2-harmonic-cloud/plan.md  (§1.x, §4.1, §5, §7.2)
// ==============================================================================

#include <krate/dsp/systems/harmonic_cloud.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;
using namespace Krate::DSP;

// R2 collision probe: the namespace-scope analysis constant that must NOT move.
#include <krate/dsp/processors/harmonic_types.h>

// T-DRIFT-EQUIV ONLY (FR-031, plan §6.4). The cloud's drift lanes are an SoA
// transposition of this class's recurrence, not instances of it, so the
// equivalence has to be measured against the real component. No other test in
// this phase includes it — HarmonicCloud itself never does.
#include <krate/dsp/processors/brownian_drift.h>

// No bit-exact float goldens (roadmap line 486): pinned renders are compared
// through aggregate metrics + spaced checkpoints at measured tolerances.
#include "render_fingerprint.h"

// SC-005 / SC-006 click criteria. The detector's threshold is a WITHIN-FRAME
// mean(|dx|) + 5*stddev(|dx|) (`artifact_detection.h:186-193`), so every click
// criterion here is DIFFERENTIAL against a frozen control render — never
// "0 detections" — and every one of them carries a positive control.
#include "artifact_detection.h"

// SC-008. ONLY the detector — `allocation_operator_overrides.h` (which supplies
// the global operator new/delete replacements the detector counts through) is
// already included by `dsp/tests/unit/systems/selectable_oscillator_test.cpp:388`
// for this binary and must appear in EXACTLY ONE translation unit per binary; a
// second inclusion here is a duplicate-symbol link error. Because this TU sees
// only the counting API and not the replacements, an inert detector would report
// 0 unconditionally — which is why HarmonicCloud_NoAllocInProcess asserts
// liveness FIRST, before it asserts zero.
#include "allocation_detector.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// =============================================================================
// R2 ODR COMPILE GUARD (plan §0.1)
// =============================================================================
// HarmonicCloud::kMaxPartials must be CLASS-scoped. Krate::DSP::kMaxPartials is
// `inline constexpr size_t = 96` at NAMESPACE scope (processors/harmonic_types.h:21).
// If harmonic_cloud.h ever declares its own `kMaxPartials` at namespace scope,
// this translation unit stops compiling — which is exactly the point.
static_assert(HarmonicCloud::kMaxPartials == 64, "class-scoped");
static_assert(Krate::DSP::kMaxPartials == 96, "namespace-scoped analysis constant, unchanged");

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr double kTwoPiD = 2.0 * kPiD;

/// Wrap a phase difference into (-pi, +pi].
[[nodiscard]] double wrapToPi(double phase) noexcept {
    while (phase > kPiD) {
        phase -= kTwoPiD;
    }
    while (phase <= -kPiD) {
        phase += kTwoPiD;
    }
    return phase;
}

/// One heterodyne phase-slope stage (plan §7.2).
///
/// Two Hann-windowed correlations of length M, separated by exactly M samples.
/// The window transforms contribute the same real factor to both and cancel in
/// arg(S2 * conj(S1)), so dphi = 2*pi*(f - fRef)*M/fs exactly. Unambiguous while
/// |f - fRef| < fs / (2M). Double accumulators throughout.
///
/// @param x      Signal, must hold at least 2*M samples
/// @param M      Per-window length
/// @param fs     Sample rate in Hz
/// @param fRef   Reference (heterodyne) frequency in Hz
/// @return Refined frequency estimate in Hz
[[nodiscard]] double heterodyneStage(const float* x, std::size_t M, double fs, double fRef) {
    std::complex<double> s1{0.0, 0.0};
    std::complex<double> s2{0.0, 0.0};
    const double omega = kTwoPiD * fRef / fs;
    const double invSpan = 1.0 / static_cast<double>(M - 1);

    for (std::size_t n = 0; n < M; ++n) {
        const double nd = static_cast<double>(n);
        const double w = 0.5 - 0.5 * std::cos(kTwoPiD * nd * invSpan);

        const double phase1 = -omega * nd;
        const double phase2 = -omega * (static_cast<double>(M) + nd);

        s1 += (w * static_cast<double>(x[n]))
              * std::complex<double>(std::cos(phase1), std::sin(phase1));
        s2 += (w * static_cast<double>(x[M + n]))
              * std::complex<double>(std::cos(phase2), std::sin(phase2));
    }

    const double dphi = wrapToPi(std::arg(s2 * std::conj(s1)));
    return fRef + dphi * fs / (kTwoPiD * static_cast<double>(M));
}

/// Two-stage heterodyne phase-slope frequency estimator (plan §7.2).
///
/// Stage 1 brackets with M = 4096 (+-5.86 Hz at 48 kHz, wider than any plausible
/// bug); stage 2 refines with M = N/2 around the stage-1 result.
[[nodiscard]] double estimateFrequency(const float* x, std::size_t N, double fs, double fRef) {
    constexpr std::size_t kStage1M = 4096;
    if (x == nullptr || N < 2 * kStage1M) {
        return 0.0;
    }
    const double coarse = heterodyneStage(x, kStage1M, fs, fRef);
    return heterodyneStage(x, N / 2, fs, coarse);
}

}  // namespace

// =============================================================================
// Estimator resolution — the shared measurement every frequency criterion rests on
// =============================================================================
// SC-001 requires < 0.1 cent accuracy *plus* a documented estimator resolution at
// least 10x finer. This case documents it at 100x finer (< 0.001 cent) against
// double-precision reference sinusoids, so SC-001/SC-002/SC-004/SC-010/SC-015.3
// can attribute any deviation they measure to the component, not the estimator.
TEST_CASE("HarmonicCloud_FrequencyEstimatorResolution") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kNumSamples = 480000;  // 10 s @ 48 kHz
    constexpr std::array<double, 4> kTestFrequencies{55.0, 440.0, 3520.0, 7040.0};

    std::vector<float> signal(kNumSamples, 0.0f);

    for (const double fTrue : kTestFrequencies) {
        const double omega = kTwoPiD * fTrue / kSampleRate;
        for (std::size_t n = 0; n < kNumSamples; ++n) {
            signal[n] = static_cast<float>(std::sin(omega * static_cast<double>(n)));
        }

        const double fEst = estimateFrequency(signal.data(), kNumSamples, kSampleRate, fTrue);

        // ratioToSemitones (core/pitch_utils.h:31) is the same conversion SC-001 uses.
        const double cents =
            100.0 * static_cast<double>(ratioToSemitones(static_cast<float>(fEst / fTrue)));

        INFO("fTrue = " << fTrue << " Hz, fEst = " << fEst << " Hz, error = " << cents << " cent");
        REQUIRE(std::abs(cents) < 0.001);
    }
}

// =============================================================================
// Guard paths (FR-004, FR-003, Edge Cases) — plan §4.1, §7.4
// =============================================================================
TEST_CASE("HarmonicCloud_GuardPaths") {
    constexpr float kPoison = -12345.0f;
    constexpr std::size_t kBlockSize = 512;

    SECTION("Null buffers and zero-length blocks write nothing and consume no drift") {
        HarmonicCloud cloud;
        cloud.prepare(48000.0);

        std::vector<float> left(kBlockSize, kPoison);
        std::vector<float> right(kBlockSize, kPoison);
        const std::uint64_t readsBefore = cloud.getDriftReadCount();

        cloud.processStereoBlock(nullptr, right.data(), kBlockSize);
        cloud.processStereoBlock(left.data(), nullptr, kBlockSize);
        cloud.processStereoBlock(left.data(), right.data(), 0);

        std::size_t firstWritten = kBlockSize;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
            if (left[i] != kPoison || right[i] != kPoison) {
                firstWritten = i;
                break;
            }
        }
        INFO("first written sample index = " << firstWritten
                                             << " (kBlockSize means nothing was written)");
        REQUIRE(firstWritten == kBlockSize);
        REQUIRE(cloud.getDriftReadCount() == readsBefore);
    }

    SECTION("Un-prepared instance renders exact silence instead of uninitialized state") {
        HarmonicCloud cloud;  // default-constructed, prepare() deliberately NOT called

        std::vector<float> left(kBlockSize, kPoison);
        std::vector<float> right(kBlockSize, kPoison);

        cloud.processStereoBlock(left.data(), right.data(), kBlockSize);

        std::size_t firstNonZero = kBlockSize;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
            if (left[i] != 0.0f || right[i] != 0.0f) {
                firstNonZero = i;
                break;
            }
        }
        INFO("first non-zero sample index = " << firstNonZero
                                              << " (kBlockSize means the render was silent)");
        REQUIRE(firstNonZero == kBlockSize);
    }

    // Clause 3 (FR-003): "after prepare, processing is well-defined with no prior
    // parameter call". No setter runs here at all — the shipped defaults alone must
    // produce finite, non-silent audio.
    SECTION("Prepare + noteOn + render with no setter call is finite and non-silent") {
        HarmonicCloud cloud;
        cloud.prepare(48000.0);
        cloud.noteOn();

        std::vector<float> left(kBlockSize, kPoison);
        std::vector<float> right(kBlockSize, kPoison);

        cloud.processStereoBlock(left.data(), right.data(), kBlockSize);

        double sumSquares = 0.0;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
            // Bit-pattern predicates: the macOS leg builds -ffast-math, so std::isnan
            // and std::isinf are not usable (core/db_utils.h:54,174).
            REQUIRE(!detail::isNaN(left[i]));
            REQUIRE(!detail::isInf(left[i]));
            REQUIRE(!detail::isNaN(right[i]));
            REQUIRE(!detail::isInf(right[i]));
            sumSquares += static_cast<double>(left[i]) * static_cast<double>(left[i]);
            sumSquares += static_cast<double>(right[i]) * static_cast<double>(right[i]);
        }
        const double blockRms = std::sqrt(sumSquares / static_cast<double>(2 * kBlockSize));
        INFO("block RMS = " << blockRms);
        REQUIRE(blockRms > 0.0);
    }
}

// =============================================================================
// SC-001 — per-partial frequency accuracy < 0.1 cent (FR-011, FR-013, FR-083)
// =============================================================================
// The measured set is a CONSTRAINT, not a cross-product: every measured partial
// must sit below 0.8*Nyquist, because FR-015 fades anything above that toward
// silence and a suppressed partial has no frequency left to estimate. The
// constraint is asserted per case so the matrix cannot silently drift into the
// fade band.
TEST_CASE("HarmonicCloud_PartialFrequencyAccuracyWithin0p1Cent") {
    constexpr double kSampleRate = 48000.0;
    constexpr double kNyquist = 0.5 * kSampleRate;
    constexpr std::size_t kNumSamples = 480000;  // 10 s @ 48 kHz
    constexpr std::size_t kSkipSamples = 9600;   // 200 ms — FR-014 smoother settling

    struct PartialCase {
        double fundamentalHz;
        int partialNumber;  // 1-based n
    };

    const std::array<PartialCase, 10> kCases{{
        {.fundamentalHz = 55.0, .partialNumber = 1},
        {.fundamentalHz = 55.0, .partialNumber = 8},
        {.fundamentalHz = 55.0, .partialNumber = 32},
        {.fundamentalHz = 55.0, .partialNumber = 64},
        {.fundamentalHz = 440.0, .partialNumber = 1},
        {.fundamentalHz = 440.0, .partialNumber = 8},
        {.fundamentalHz = 440.0, .partialNumber = 32},
        {.fundamentalHz = 1000.0, .partialNumber = 1},
        {.fundamentalHz = 1000.0, .partialNumber = 8},
        {.fundamentalHz = 1000.0, .partialNumber = 16},
    }};

    std::vector<float> left(kNumSamples, 0.0f);
    std::vector<float> right(kNumSamples, 0.0f);
    std::vector<float> mono(kNumSamples, 0.0f);

    for (const PartialCase& testCase : kCases) {
        const double fExpected =
            testCase.fundamentalHz * static_cast<double>(testCase.partialNumber);

        INFO("f0 = " << testCase.fundamentalHz << " Hz, n = " << testCase.partialNumber
                     << ", expected " << fExpected << " Hz");
        REQUIRE(fExpected < 0.8 * kNyquist);

        HarmonicCloud cloud;
        cloud.prepare(kSampleRate);
        cloud.setFundamentalHz(static_cast<float>(testCase.fundamentalHz));
        cloud.setInharmonicity(0.0f);
        cloud.setSpectralGravity(0.0f);
        cloud.setDriftDepthCents(0.0f);
        cloud.setMutation(0.0f);
        cloud.soloPartial(static_cast<std::size_t>(testCase.partialNumber - 1));
        cloud.noteOn();

        cloud.processStereoBlock(left.data(), right.data(), kNumSamples);

        for (std::size_t i = 0; i < kNumSamples; ++i) {
            mono[i] = 0.5f * (left[i] + right[i]);
        }

        const double fEst = estimateFrequency(mono.data() + kSkipSamples,
                                              kNumSamples - kSkipSamples, kSampleRate, fExpected);
        const double cents =
            100.0 * static_cast<double>(ratioToSemitones(static_cast<float>(fEst / fExpected)));

        INFO("fEst = " << fEst << " Hz, error = " << cents << " cent");
        REQUIRE(std::abs(cents) < 0.1);
    }
}

// =============================================================================
// SC-002 — inharmonicity follows the piano/bell law (FR-051, FR-052, FR-083)
// =============================================================================
// f_n = f0 * n * sqrt(1 + B*n^2). Measured partial-by-partial with the same
// estimator, and only for partials whose EXPECTED frequency stays below
// 0.8*Nyquist (asserted, same reason as SC-001). The 1-cent bound is three
// orders of magnitude above the estimator resolution proved above.
TEST_CASE("HarmonicCloud_InharmonicityFollowsPianoLaw") {
    constexpr double kSampleRate = 48000.0;
    constexpr double kNyquist = 0.5 * kSampleRate;
    constexpr std::size_t kNumSamples = 480000;  // 10 s @ 48 kHz
    constexpr std::size_t kSkipSamples = 9600;   // 200 ms
    constexpr double kFundamentalHz = 110.0;

    constexpr std::array<double, 3> kInharmonicities{0.01, 0.05, 0.1};
    constexpr std::array<int, 6> kPartialNumbers{1, 2, 4, 8, 16, 32};

    std::vector<float> left(kNumSamples, 0.0f);
    std::vector<float> right(kNumSamples, 0.0f);
    std::vector<float> mono(kNumSamples, 0.0f);

    for (const double inharmonicity : kInharmonicities) {
        int measured = 0;

        for (const int n : kPartialNumbers) {
            const double nd = static_cast<double>(n);
            const double fExpected =
                kFundamentalHz * nd * std::sqrt(1.0 + inharmonicity * nd * nd);
            if (fExpected >= 0.8 * kNyquist) {
                continue;  // FR-015 fades it out — nothing left to estimate
            }

            INFO("B = " << inharmonicity << ", n = " << n << ", expected " << fExpected << " Hz");
            REQUIRE(fExpected < 0.8 * kNyquist);

            HarmonicCloud cloud;
            cloud.prepare(kSampleRate);
            cloud.setFundamentalHz(static_cast<float>(kFundamentalHz));
            cloud.setSpectralGravity(0.0f);
            cloud.setDriftDepthCents(0.0f);
            cloud.setMutation(0.0f);
            cloud.setInharmonicity(static_cast<float>(inharmonicity));
            cloud.soloPartial(static_cast<std::size_t>(n - 1));
            cloud.noteOn();

            cloud.processStereoBlock(left.data(), right.data(), kNumSamples);

            for (std::size_t i = 0; i < kNumSamples; ++i) {
                mono[i] = 0.5f * (left[i] + right[i]);
            }

            const double fEst =
                estimateFrequency(mono.data() + kSkipSamples, kNumSamples - kSkipSamples,
                                  kSampleRate, fExpected);
            const double cents = 100.0
                                 * static_cast<double>(
                                     ratioToSemitones(static_cast<float>(fEst / fExpected)));

            INFO("fEst = " << fEst << " Hz, error = " << cents << " cent");
            REQUIRE(std::abs(cents) <= 1.0);
            ++measured;
        }

        INFO("B = " << inharmonicity << " measured " << measured << " partials");
        REQUIRE(measured > 0);
    }
}

// =============================================================================
// SC-004 — spectral gravity: grid warp, monotonicity, sign, composition order
// =============================================================================
// FR-081: ratio_g(n) = n^(1 + g * kGravityExponentRange), kGravityExponentRange
// = 0.1, so g = 0 is exactly the integer grid and ratio_g(1) = 1 for every g.
// FR-083 fixes the composition order: gravity warps the grid FIRST, then the
// FR-051 inharmonicity stretch multiplies it —
//   f_n = f0 * n^(1 + g*0.1) * sqrt(1 + B*n^2).
//
// @par Why this case carries a signal-PRESENCE guard as well as the estimator
// The T002 estimator is a two-stage heterodyne phase-slope measurement: stage 1
// is unambiguous only while |f - fRef| < fs/(2M) = 5.86 Hz at M = 4096, and its
// reference frequency has to be the frequency the law PREDICTS (the deviations
// here run to hundreds of cents, far outside any bracket built on the unwarped
// grid). A build that ignored gravity entirely would therefore be measured with
// a reference hundreds of Hz away from its actual partial, the phase difference
// would wrap, and the estimator would hand back a value near the reference —
// i.e. the criterion would PASS on a broken component. `heterodyneMagnitude`
// closes that hole: it measures the amplitude actually present at the predicted
// frequency over a 65536-sample Hann window (mainlobe half-width 1.46 Hz at
// 48 kHz) and requires it to carry at least half the rendered peak. A partial
// that is not where FR-081/FR-083 say it is fails there, before the frequency
// assertion is ever reached. The guard is part of the measurement, not decoration.
//
// @par FR-084 (phase continuity across a gravity change)
// Satisfied by construction and not re-measured here: `setSpectralGravity` only
// raises the frequency dirty flag, and `recalculateFrequencies()` writes
// `frequencyHz_`/`epsilon_` without touching `sinState_`/`cosState_` — the same
// mechanism FR-053 uses for inharmonicity. The click measurement over a swept
// gravity control belongs to SC-006 (`HarmonicCloud_MacroSweepsAreClickFree`).
// =============================================================================

namespace {

/// Windowed heterodyne MAGNITUDE at `f`, normalised to sinusoid amplitude.
///
/// Same Hann-windowed correlation as `heterodyneStage`, but returning
/// `2*|S| / sum(w)` — which equals `A` for a signal `A*sin(2*pi*f*t + phi)` and
/// falls away sharply once `f` misses the true partial by more than the window's
/// mainlobe (half-width `2*fs/M`). Double accumulators throughout.
///
/// @param x  Signal, must hold at least M samples
/// @param M  Window length in samples
/// @param fs Sample rate in Hz
/// @param f  Frequency to probe, in Hz
/// @return Amplitude present at `f`, in the same units as the samples
[[nodiscard]] double heterodyneMagnitude(const float* x, std::size_t M, double fs, double f) {
    std::complex<double> s{0.0, 0.0};
    double windowSum = 0.0;
    const double omega = kTwoPiD * f / fs;
    const double invSpan = 1.0 / static_cast<double>(M - 1);

    for (std::size_t n = 0; n < M; ++n) {
        const double nd = static_cast<double>(n);
        const double w = 0.5 - 0.5 * std::cos(kTwoPiD * nd * invSpan);
        const double phase = -omega * nd;
        s += (w * static_cast<double>(x[n]))
             * std::complex<double>(std::cos(phase), std::sin(phase));
        windowSum += w;
    }

    return (windowSum > 0.0) ? (2.0 * std::abs(s) / windowSum) : 0.0;
}

}  // namespace

TEST_CASE("HarmonicCloud_GravityMapsMonotonically") {
    constexpr double kSampleRate = 48000.0;
    constexpr double kNyquist = 0.5 * kSampleRate;
    constexpr std::size_t kNumSamples = 480000;  // 10 s @ 48 kHz
    constexpr std::size_t kSkipSamples = 9600;   // 200 ms — FR-014 smoother settling
    constexpr double kFundamentalHz = 110.0;

    /// SC-001's tolerance in cents. The estimator's OWN resolution is 100x finer
    /// (< 0.001 cent, proved by HarmonicCloud_FrequencyEstimatorResolution), so
    /// carrying the criterion figure here is the conservative reading of
    /// "the SC-001 estimator tolerance".
    constexpr double kEstimatorToleranceCents = 0.1;

    /// SC-004 clause 2: every step in |g| must move the metric by at least 5x that,
    /// so the ordering cannot be produced by measurement noise.
    constexpr double kMinStepCents = 5.0 * kEstimatorToleranceCents;

    /// SC-002's tolerance, reused verbatim by clause 4's combined-law check.
    constexpr double kCombinedLawToleranceCents = 1.0;

    /// Presence guard (see the note above). 65536 samples = 1.365 s at 48 kHz,
    /// Hann mainlobe half-width 2*fs/M = 1.46 Hz.
    constexpr std::size_t kMagnitudeWindow = 65536;
    constexpr double kMinMagnitudeFraction = 0.5;

    constexpr std::array<double, 5> kGravities{-1.0, -0.5, 0.0, 0.5, 1.0};
    constexpr std::array<int, 6> kPartials{1, 2, 4, 8, 16, 32};

    constexpr std::size_t kIdxMinusOne = 0;
    constexpr std::size_t kIdxMinusHalf = 1;
    constexpr std::size_t kIdxZero = 2;
    constexpr std::size_t kIdxPlusHalf = 3;
    constexpr std::size_t kIdxPlusOne = 4;
    static_assert(kGravities.size() == 5, "the five-setting sweep SC-004 clause 2 requires");
    static_assert(kGravities[kIdxZero] == 0.0, "clause 1 measures the zero setting");
    static_assert(kGravities[kIdxPlusOne] == -kGravities[kIdxMinusOne], "clause 3 needs +-g");

    struct PartialMeasurement {
        double expectedHz;   ///< the FR-083 law's prediction
        double estimatedHz;  ///< what the render actually contains
        double gridCents;    ///< deviation of the measured ratio from the INTEGER grid n
        double lawCents;     ///< error of the measured frequency against the law
    };

    std::vector<float> left(kNumSamples, 0.0f);
    std::vector<float> right(kNumSamples, 0.0f);
    std::vector<float> mono(kNumSamples, 0.0f);

    const auto measure = [&](double gravity, double inharmonicity, int n) -> PartialMeasurement {
        const double nd = static_cast<double>(n);
        const double exponent =
            1.0 + gravity * static_cast<double>(HarmonicCloud::kGravityExponentRange);
        const double ratioG = std::pow(nd, exponent);                   // FR-081
        const double stretch = std::sqrt(1.0 + inharmonicity * nd * nd);  // FR-051
        const double expectedHz = kFundamentalHz * ratioG * stretch;    // FR-083, THIS order

        INFO("g = " << gravity << ", B = " << inharmonicity << ", n = " << n << ", law predicts "
                    << expectedHz << " Hz");

        // SC-004's stated measurement constraint, asserted so the matrix cannot
        // silently drift into FR-015's fade band, where a suppressed partial has no
        // frequency left to estimate.
        REQUIRE(expectedHz < 0.8 * kNyquist);

        HarmonicCloud cloud;
        cloud.prepare(kSampleRate);
        cloud.setFundamentalHz(static_cast<float>(kFundamentalHz));
        cloud.setDriftDepthCents(0.0f);
        cloud.setMutation(0.0f);
        cloud.setInharmonicity(static_cast<float>(inharmonicity));
        cloud.setSpectralGravity(static_cast<float>(gravity));
        cloud.soloPartial(static_cast<std::size_t>(n - 1));
        cloud.noteOn();

        cloud.processStereoBlock(left.data(), right.data(), kNumSamples);

        double peak = 0.0;
        for (std::size_t i = 0; i < kNumSamples; ++i) {
            mono[i] = 0.5f * (left[i] + right[i]);
            if (i >= kSkipSamples) {
                peak = std::max(peak, std::abs(static_cast<double>(mono[i])));
            }
        }

        const float* window = mono.data() + kSkipSamples;
        const std::size_t windowLength = kNumSamples - kSkipSamples;
        REQUIRE(windowLength >= kMagnitudeWindow);

        // Presence guard FIRST: a partial that is not where the law says it is fails
        // here, instead of being "measured" by a wrapped phase difference.
        const double magnitude =
            heterodyneMagnitude(window, kMagnitudeWindow, kSampleRate, expectedHz);
        INFO("rendered peak = " << peak << ", amplitude present at the predicted frequency = "
                                << magnitude);
        REQUIRE(peak > 0.0);
        REQUIRE(magnitude >= kMinMagnitudeFraction * peak);

        const double estimatedHz = estimateFrequency(window, windowLength, kSampleRate, expectedHz);
        const double gridCents =
            100.0
            * static_cast<double>(
                ratioToSemitones(static_cast<float>(estimatedHz / (kFundamentalHz * nd))));
        const double lawCents =
            100.0
            * static_cast<double>(ratioToSemitones(static_cast<float>(estimatedHz / expectedHz)));

        INFO("estimated = " << estimatedHz << " Hz, deviation from the integer grid = " << gridCents
                            << " cent, error against the law = " << lawCents << " cent");
        return PartialMeasurement{.expectedHz = expectedHz,
                                  .estimatedHz = estimatedHz,
                                  .gridCents = gridCents,
                                  .lawCents = lawCents};
    };

    // -------------------------------------------------------------------------
    // The shared grid: B = 0, five gravity settings x six partials.
    // Parts 1-3 all read out of it; nothing is rendered twice.
    // -------------------------------------------------------------------------
    std::array<std::array<double, kPartials.size()>, kGravities.size()> gridDeviation{};
    for (std::size_t gi = 0; gi < kGravities.size(); ++gi) {
        for (std::size_t pi = 0; pi < kPartials.size(); ++pi) {
            gridDeviation[gi][pi] = measure(kGravities[gi], 0.0, kPartials[pi]).gridCents;
        }
    }

    // ---- Part 1 (zero setting). At g = 0 every measured partial sits on the
    // integer grid within SC-001's tolerance — INCLUDING n = 1, whose ratio is
    // pinned at 1 by FR-081 at every setting.
    for (std::size_t pi = 0; pi < kPartials.size(); ++pi) {
        INFO("g = 0, n = " << kPartials[pi] << ", grid deviation = " << gridDeviation[kIdxZero][pi]
                           << " cent");
        REQUIRE(std::abs(gridDeviation[kIdxZero][pi]) < kEstimatorToleranceCents);
    }

    // ---- Part 2 (monotonicity). Mean |deviation| over n >= 2, strictly increasing
    // in |g| on BOTH sides of zero, by at least 5x the estimator tolerance per step.
    //
    // The metric is the mean absolute deviation in CENTS rather than in raw ratio
    // units, because the tolerance the step threshold is expressed against is itself
    // a cents figure. Per partial the two are strictly monotone transforms of one
    // another, so the ordering claim is identical either way.
    std::array<double, kGravities.size()> meanCents{};
    std::size_t partialsAboveFundamental = 0;
    for (std::size_t gi = 0; gi < kGravities.size(); ++gi) {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t pi = 0; pi < kPartials.size(); ++pi) {
            if (kPartials[pi] < 2) {
                continue;  // FR-082 makes no claim about n = 1
            }
            sum += std::abs(gridDeviation[gi][pi]);
            ++count;
        }
        REQUIRE(count > 0);
        meanCents[gi] = sum / static_cast<double>(count);
        partialsAboveFundamental = count;
    }

    INFO("mean |deviation| in cent at g = -1, -0.5, 0, +0.5, +1: "
         << meanCents[kIdxMinusOne] << ", " << meanCents[kIdxMinusHalf] << ", "
         << meanCents[kIdxZero] << ", " << meanCents[kIdxPlusHalf] << ", "
         << meanCents[kIdxPlusOne]);
    REQUIRE(meanCents[kIdxPlusHalf] >= meanCents[kIdxZero] + kMinStepCents);
    REQUIRE(meanCents[kIdxPlusOne] >= meanCents[kIdxPlusHalf] + kMinStepCents);
    REQUIRE(meanCents[kIdxMinusHalf] >= meanCents[kIdxZero] + kMinStepCents);
    REQUIRE(meanCents[kIdxMinusOne] >= meanCents[kIdxMinusHalf] + kMinStepCents);

    // ---- Part 3 (sign, restricted to n >= 2). FR-082: the deviation is positive
    // for g > 0 and negative for g < 0, so +g and -g carry opposite signs.
    // n = 1 is EXCLUDED — ratio_g(1) = 1 for every g, so its deviation is
    // identically 0 and a universal clause would be unsatisfiable by a correct
    // implementation.
    std::size_t qualifying = 0;
    for (std::size_t pi = 0; pi < kPartials.size(); ++pi) {
        if (kPartials[pi] < 2) {
            continue;
        }
        const double devPlus = gridDeviation[kIdxPlusOne][pi];
        const double devMinus = gridDeviation[kIdxMinusOne][pi];
        if (std::abs(devPlus) <= kEstimatorToleranceCents
            || std::abs(devMinus) <= kEstimatorToleranceCents) {
            continue;  // below the estimator tolerance: no sign claim is made
        }

        INFO("n = " << kPartials[pi] << ", deviation at g = +1: " << devPlus
                    << " cent, at g = -1: " << devMinus << " cent");
        REQUIRE(devPlus * devMinus < 0.0);  // opposite signs
        REQUIRE(devPlus > 0.0);             // g > 0 stretches
        REQUIRE(devMinus < 0.0);            // g < 0 compresses
        ++qualifying;
    }
    // Non-vacuity: every partial above the fundamental must have moved measurably
    // at |g| = 1, otherwise the clause could pass by qualifying nobody.
    INFO("partials qualifying for the sign clause: " << qualifying << " of "
                                                     << partialsAboveFundamental);
    REQUIRE(qualifying == partialsAboveFundamental);

    // ---- Part 4 (composition order). THIS is what pins FR-083; without it the
    // order is documented, not measured. Applying the stretch before the warp —
    // f0 * (n*sqrt(1+B*n^2))^(1+g*0.1) — puts partial 16 at 9838 Hz instead of
    // 8627 Hz, which fails the presence guard long before the 1-cent bound.
    constexpr double kCompositionInharmonicity = 0.05;
    constexpr std::array<int, 5> kCompositionPartials{1, 2, 4, 8, 16};
    for (const double gravity : {-1.0, 1.0}) {
        for (const int n : kCompositionPartials) {
            const PartialMeasurement m = measure(gravity, kCompositionInharmonicity, n);
            INFO("composition order: g = " << gravity << ", B = " << kCompositionInharmonicity
                                           << ", n = " << n << ", law predicts " << m.expectedHz
                                           << " Hz, measured " << m.estimatedHz << " Hz, error "
                                           << m.lawCents << " cent");
            REQUIRE(std::abs(m.lawCents) <= kCombinedLawToleranceCents);
        }
    }
}

// =============================================================================
// SC-018 — onset is phase-incoherent and stays below the clamp (FR-016, FR-017)
// =============================================================================
// FR-016 draws every partial's initial MCF phase from the seeded cloud stream, so
// a 64-partial sum does NOT start coherent. That is the only thing keeping the
// onset inside FR-017's headroom: the expected-RMS basis (Clarifications Q6) pins
// the RMS at kTargetOscRms = 0.5 and therefore *leaves* 11.1 dB of crest room up
// to 0.9 * kOutputClamp = 1.8 — it does not guarantee it arithmetically. This
// case is that measurement. A phase-0 implementation sums all partials in phase
// at t = 0 and fails all three clauses.
//
// Configuration is fully pinned so the crest factor is reproducible: 64 partials
// (Richness 1), tilt +12 dB/oct (the setting that concentrates energy at the top,
// a_n proportional to n^1.4932 here), f0 = 110 Hz, spread 0, mutation/drift/
// gravity/inharmonicity 0, shortest supported attack, no envelope offset spread.
//
// @par Measured on the real component (T005 step 1)
// Windows 11, MSVC Release, 2026-07-25. 32-seed sweep, 3 s renders at 48 kHz,
// worst channel peak over the first 100 ms:
//   over ALL 32 seeds:  min 0.8684, max 1.5453 -> worst margin to 1.8 = 1.33 dB
//   over kSc018Seeds:   1.0809 1.2761 1.3193 1.0074 1.0301 1.1304 1.3131 1.1113
//                       -> worst 1.3193, margin 2.70 dB
//                       -> mean 1.1586, max-min 0.3119 = 26.9 % of the mean
//   onset peak-to-RMS minus steady peak-to-RMS over kSc018Seeds: +0.180 dB worst
//   (range +0.122 .. +0.180), against the 6 dB allowance.
// kSc018Seeds is the FIRST EIGHT entries of that sweep, not a low-peak selection —
// picking the quietest seeds would weaken the criterion.
//
// Those figures were measured while FR-023's per-partial envelope is still inert
// (env_i == 1), i.e. with an instantaneous onset — the worst case for clause 1.
// Once the envelope lands, the pinned 50 ms linear attack can only LOWER the onset
// peak, and it raises the onset peak-to-RMS by at most ~1.8 dB (the RMS of a
// half-ramped 100 ms window is 0.816 of steady), which clause 2's 6 dB allowance
// absorbs with room to spare.
//
// @par Defect this measurement exposed (fixed in the component, not the threshold)
// Before the fix every one of the 32 seeds measured an onset peak of EXACTLY
// 2.0000 — the FR-006 clamp — for the first ~40 ms, against a steady-state peak of
// 1.082. Cause: `noteOn()` did not snap the FR-017 normalizer, so a cloud
// configured while silent rendered its first chunks with the gain belonging to the
// PREVIOUS amplitude set and slid to the correct one over kNormGainSmoothMs. See
// the note on HarmonicCloud::noteOn().
// =============================================================================

TEST_CASE("HarmonicCloud_OnsetIsPhaseIncoherent") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlockSize = 512;
    constexpr std::size_t kOnsetSamples = 4800;    // 100 ms
    constexpr std::size_t kTotalSamples = 144000;  // 3 s
    constexpr std::size_t kSteadyFrom = 96000;     // 2.0 s
    constexpr std::size_t kSteadyTo = kSteadyFrom + kOnsetSamples;  // same window length

    /// Explicitly enumerated (spec.md SC-018 requires >= 8 distinct seeds). An
    /// enumerated array rather than arbitrary draws so the measured distribution
    /// recorded above is the distribution the test actually exercises.
    constexpr std::array<std::uint32_t, 8> kSc018Seeds{0x5E3A0003u, 0x5E3A0303u, 0x5E3A0603u,
                                                       0x5E3A0903u, 0x5E3A0C03u, 0x5E3A0F03u,
                                                       0x5E3A1203u, 0x5E3A1503u};

    /// Clause 3's documented fraction. Measured spread is 26.9 % of the mean, so
    /// this discriminates by ~2.7x rather than merely describing the measurement.
    constexpr float kMinPeakSpreadFraction = 0.10f;

    /// Clause 2's allowance (spec.md SC-018). Measured worst excess: +0.180 dB.
    constexpr float kOnsetCrestAllowanceDb = 6.0f;

    const auto peakOver = [](const std::vector<float>& v, std::size_t from,
                             std::size_t to) noexcept {
        float peak = 0.0f;
        for (std::size_t i = from; i < to; ++i) {
            peak = std::max(peak, std::abs(v[i]));
        }
        return peak;
    };

    const auto rmsOver = [](const std::vector<float>& v, std::size_t from,
                            std::size_t to) noexcept {
        double acc = 0.0;
        for (std::size_t i = from; i < to; ++i) {
            acc += static_cast<double>(v[i]) * static_cast<double>(v[i]);
        }
        return std::sqrt(acc / static_cast<double>(to - from));
    };

    std::vector<float> left(kTotalSamples, 0.0f);
    std::vector<float> right(kTotalSamples, 0.0f);
    std::array<float, kBlockSize> leftBlock{};
    std::array<float, kBlockSize> rightBlock{};

    std::array<float, kSc018Seeds.size()> onsetPeaks{};

    for (std::size_t s = 0; s < kSc018Seeds.size(); ++s) {
        const std::uint32_t seed = kSc018Seeds[s];
        INFO("seed index " << s << ", seed = " << seed);

        HarmonicCloud cloud;
        cloud.prepare(kSampleRate);
        cloud.setSeed(seed);
        cloud.setFundamentalHz(110.0f);
        cloud.setRichness(1.0f);  // N(1) = 64
        cloud.setSpectralTiltDb(HarmonicCloud::kMaxTiltDbPerOct);  // +12, energy at the top
        cloud.setInharmonicity(0.0f);
        cloud.setSpectralGravity(0.0f);
        cloud.setMutation(0.0f);
        cloud.setDriftDepthCents(0.0f);
        cloud.setStereoSpread(0.0f);
        cloud.setAttackTimeSec(HarmonicCloud::kMinAttackSec);
        cloud.setEnvelopeOffsetSpread(0.0f);
        cloud.noteOn();

        REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);

        // kTotalSamples is deliberately NOT a multiple of kBlockSize (144000 =
        // 281.25 blocks), so the final block is short and must be clamped.
        for (std::size_t done = 0; done < kTotalSamples; done += kBlockSize) {
            const std::size_t n = std::min(kBlockSize, kTotalSamples - done);
            cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), n);
            for (std::size_t i = 0; i < n; ++i) {
                left[done + i] = leftBlock[i];
                right[done + i] = rightBlock[i];
            }
        }

        const float onsetPeak =
            std::max(peakOver(left, 0, kOnsetSamples), peakOver(right, 0, kOnsetSamples));
        const double onsetRms =
            std::max(rmsOver(left, 0, kOnsetSamples), rmsOver(right, 0, kOnsetSamples));
        const float steadyPeak = std::max(peakOver(left, kSteadyFrom, kSteadyTo),
                                          peakOver(right, kSteadyFrom, kSteadyTo));
        const double steadyRms = std::max(rmsOver(left, kSteadyFrom, kSteadyTo),
                                          rmsOver(right, kSteadyFrom, kSteadyTo));

        onsetPeaks[s] = onsetPeak;

        // ---- Clause 1. The measurement of FR-017's 11.1 dB crest headroom.
        INFO("onset peak (first 100 ms, worst channel) = " << onsetPeak);
        REQUIRE(onsetPeak <= 0.9f * HarmonicCloud::kOutputClamp);

        // ---- Clause 2. Onset crest must not tower over the steady-state crest.
        REQUIRE(onsetRms > 0.0);
        REQUIRE(steadyRms > 0.0);
        const auto onsetCrestDb =
            static_cast<float>(20.0 * std::log10(static_cast<double>(onsetPeak) / onsetRms));
        const auto steadyCrestDb =
            static_cast<float>(20.0 * std::log10(static_cast<double>(steadyPeak) / steadyRms));
        INFO("onset peak-to-RMS = " << onsetCrestDb << " dB, steady-state peak-to-RMS = "
                                    << steadyCrestDb << " dB");
        REQUIRE(onsetCrestDb <= steadyCrestDb + kOnsetCrestAllowanceDb);
    }

    // ---- Clause 3. The onset peak must VARY with the seed. A fixed peak across
    // seeds means the phases are not actually seeded — which a phase-0
    // implementation would exhibit exactly.
    float minPeak = onsetPeaks[0];
    float maxPeak = onsetPeaks[0];
    double sum = 0.0;
    for (const float peak : onsetPeaks) {
        minPeak = std::min(minPeak, peak);
        maxPeak = std::max(maxPeak, peak);
        sum += static_cast<double>(peak);
    }
    const auto mean = static_cast<float>(sum / static_cast<double>(onsetPeaks.size()));

    double variance = 0.0;
    for (const float peak : onsetPeaks) {
        const double d = static_cast<double>(peak) - static_cast<double>(mean);
        variance += d * d;
    }
    variance /= static_cast<double>(onsetPeaks.size());
    const double stddev = std::sqrt(variance);

    INFO("onset peaks: min " << minPeak << ", max " << maxPeak << ", mean " << mean << ", stddev "
                             << stddev);
    REQUIRE(stddev > 0.0);
    REQUIRE(mean > 0.0f);
    REQUIRE(maxPeak - minPeak >= kMinPeakSpreadFraction * mean);
}

// =============================================================================
// SC-012 — equal-power pan, polarity, monotonicity, spread (FR-021, FR-091..093)
// =============================================================================
// Measured on THE SHIPPED CONVERSION PATH. The test deliberately does NOT
// re-implement the pan law: the defect FR-091 exists to prevent lives inside the
// component's own position -> gain conversion, so a test that recomputed
// `equalPowerGains` itself would agree with a broken component.
//
// Clauses 2-4 are not decoration. Clause 1 alone cannot discriminate a correct
// pan law from the domain-mismatch bug: `equalPowerGains` does NOT clamp and its
// domain is [0, 1] (`core/crossfade_utils.h:41`), so feeding a bipolar position
// straight in gives (L, R) = (cos(-pi/2), sin(-pi/2)) = (0, -1) at pos = -1 —
// for which |0 + 1 - 1| = 0 PASSES clause 1 while the right channel is at full
// level and phase-inverted. Clause 2 is what catches it.
// =============================================================================
TEST_CASE("HarmonicCloud_EqualPowerPanAndSpread") {
    constexpr double kSampleRate = 48000.0;

    SECTION("Pinned position grid, read back through the shipped accessors") {
        constexpr std::array<float, 5> kGrid{-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
        constexpr std::size_t kCentreIndex = 2;
        static_assert(kGrid[kCentreIndex] == 0.0f, "clause 4 measures the centre of the grid");

        /// Clauses 1 and 4 (spec.md SC-012). Well above float32 round-off on a
        /// pair of ~0.707 gains (1 ULP is 6e-8), well below any real defect.
        constexpr float kPanTolerance = 1.0e-6f;

        HarmonicCloud cloud;
        cloud.prepare(kSampleRate);

        std::array<std::array<float, HarmonicCloud::kMaxPartials>, kGrid.size()> panLeft{};
        std::array<std::array<float, HarmonicCloud::kMaxPartials>, kGrid.size()> panRight{};

        for (std::size_t g = 0; g < kGrid.size(); ++g) {
            for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
                cloud.setPartialPosition(i, kGrid[g]);
            }

            for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
                panLeft[g][i] = cloud.getPartialPanLeft(i);
                panRight[g][i] = cloud.getPartialPanRight(i);

                INFO("position " << kGrid[g] << ", partial " << i << ": L = " << panLeft[g][i]
                                 << ", R = " << panRight[g][i]);

                // The override must actually have taken, or every clause below
                // would be measuring the seeded scatter instead of the grid.
                REQUIRE(cloud.getPartialPosition(i) == Approx(kGrid[g]).margin(1.0e-7));

                // ---- Clause 1: equal power.
                const float power = panLeft[g][i] * panLeft[g][i]
                                    + panRight[g][i] * panRight[g][i];
                REQUIRE(std::abs(power - 1.0f) <= kPanTolerance);

                // ---- Clause 2: no polarity inversion. THIS is the clause the
                // FR-091 domain-mismatch bug fails.
                REQUIRE(panLeft[g][i] >= 0.0f);
                REQUIRE(panRight[g][i] >= 0.0f);
            }
        }

        // ---- Clause 3: L strictly decreasing, R strictly increasing across the
        // grid. Strict, not merely non-increasing: a component that collapsed
        // every position onto the centre would satisfy a non-strict version.
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            for (std::size_t g = 1; g < kGrid.size(); ++g) {
                INFO("partial " << i << ", position " << kGrid[g - 1] << " -> " << kGrid[g]
                                << ": L " << panLeft[g - 1][i] << " -> " << panLeft[g][i] << ", R "
                                << panRight[g - 1][i] << " -> " << panRight[g][i]);
                REQUIRE(panLeft[g][i] < panLeft[g - 1][i]);
                REQUIRE(panRight[g][i] > panRight[g - 1][i]);
            }
        }

        // ---- Clause 4: centre is centre.
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("partial " << i << " at position 0: L = " << panLeft[kCentreIndex][i]
                            << ", R = " << panRight[kCentreIndex][i]);
            REQUIRE(std::abs(panLeft[kCentreIndex][i] - panRight[kCentreIndex][i])
                    <= kPanTolerance);
        }
    }

    // -------------------------------------------------------------------------
    // Spread behaviour (FR-021's law), on a freshly seeded cloud with NO
    // setPartialPosition override in effect.
    //
    // f0 = 100 Hz at 48 kHz is deliberate: one period is exactly 480 samples, so
    // the measurement window spans a whole number of periods of every partial
    // (they are exact integer multiples at gravity 0 / inharmonicity 0) and the
    // cross-partial terms in the correlation cancel instead of contributing a
    // window-length-dependent bias.
    //
    // Because position_i = spread * s_i with s_i fixed per seed, the two spreads
    // give PROPORTIONAL positions — the ordering under test is a property of the
    // law, not of a re-draw.
    // -------------------------------------------------------------------------
    SECTION("Spread 0 centres every partial; increasing spread decorrelates the channels") {
        constexpr float kFundamentalHz = 100.0f;        // 480 samples/period at 48 kHz
        constexpr std::size_t kSkipSamples = 9600;      // 200 ms — FR-014 smoother settling
        constexpr std::size_t kMeasureSamples = 96000;  // 200 exact periods of 100 Hz
        constexpr std::size_t kTotalSamples = kSkipSamples + kMeasureSamples;
        constexpr std::uint32_t kSeed = 0x5C012AB1u;

        /// FR-093 at spread 0 (spec.md SC-012). The channels differ only through
        /// the pan pair, which is cos(pi/4) vs sin(pi/4) on the same float
        /// argument, so a correct implementation renders identical channels.
        constexpr double kCentreChannelTolerance = 1.0e-7;

        constexpr std::array<float, 5> kSpreads{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        static_assert(kSpreads.size() >= 4, "spec.md SC-012 requires at least four settings");

        std::vector<float> left(kTotalSamples, 0.0f);
        std::vector<float> right(kTotalSamples, 0.0f);
        std::array<double, kSpreads.size()> correlation{};
        double worstCentreDelta = 0.0;

        for (std::size_t s = 0; s < kSpreads.size(); ++s) {
            INFO("stereo spread = " << kSpreads[s]);

            HarmonicCloud cloud;
            cloud.prepare(kSampleRate);
            cloud.setSeed(kSeed);
            cloud.setFundamentalHz(kFundamentalHz);
            cloud.setRichness(1.0f);  // N(1) = 64 — every partial carries a position
            cloud.setInharmonicity(0.0f);
            cloud.setSpectralGravity(0.0f);
            cloud.setSpectralTiltDb(0.0f);
            cloud.setMutation(0.0f);
            cloud.setDriftDepthCents(0.0f);
            cloud.setStereoSpread(kSpreads[s]);
            cloud.noteOn();

            REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);

            cloud.processStereoBlock(left.data(), right.data(), kTotalSamples);

            if (kSpreads[s] == 0.0f) {
                // "over a full render" — the onset is included, not skipped.
                for (std::size_t i = 0; i < kTotalSamples; ++i) {
                    worstCentreDelta = std::max(
                        worstCentreDelta,
                        std::abs(static_cast<double>(left[i]) - static_cast<double>(right[i])));
                }
            }

            double sumL = 0.0;
            double sumR = 0.0;
            for (std::size_t i = kSkipSamples; i < kTotalSamples; ++i) {
                sumL += static_cast<double>(left[i]);
                sumR += static_cast<double>(right[i]);
            }
            const double n = static_cast<double>(kMeasureSamples);
            const double meanL = sumL / n;
            const double meanR = sumR / n;

            double sll = 0.0;
            double srr = 0.0;
            double slr = 0.0;
            for (std::size_t i = kSkipSamples; i < kTotalSamples; ++i) {
                const double dl = static_cast<double>(left[i]) - meanL;
                const double dr = static_cast<double>(right[i]) - meanR;
                sll += dl * dl;
                srr += dr * dr;
                slr += dl * dr;
            }
            REQUIRE(sll > 0.0);
            REQUIRE(srr > 0.0);
            correlation[s] = slr / std::sqrt(sll * srr);
        }

        INFO("worst |L - R| over the full spread-0 render = " << worstCentreDelta);
        REQUIRE(worstCentreDelta <= kCentreChannelTolerance);

        for (std::size_t s = 0; s < kSpreads.size(); ++s) {
            INFO("inter-channel correlation at spread " << kSpreads[s] << " = " << correlation[s]);
            REQUIRE(correlation[s] <= 1.0 + 1.0e-9);
        }
        for (std::size_t s = 1; s < kSpreads.size(); ++s) {
            INFO("correlation at spread " << kSpreads[s - 1] << " = " << correlation[s - 1]
                                          << ", at spread " << kSpreads[s] << " = "
                                          << correlation[s]);
            REQUIRE(correlation[s] < correlation[s - 1]);
        }
    }
}

// =============================================================================
// T-SEED-DISTINCT — the 128 derived lane streams never collide (FR-005)
// =============================================================================
// `HarmonicCloud::reseed()` hands `deriveSeed(seed, i)` to the detune bank's 64
// lanes and `deriveSeed(seed, i + kMaxPartials)` to the mutation bank's 64. Two
// lanes that received the same value would run IDENTICAL walks, so their
// partials would drift and mutate in lockstep — a correlation defect that no
// aggregate render metric would name.
//
// Seed 0 is the sharp case: `Xorshift32::seed()` silently substitutes its own
// default for 0 (`core/random.h:72-74`), so a `deriveSeed` that returned 0 for
// two different salts would produce two lanes that LOOK seeded and are in fact
// the same stream. The substitution must never be what saves the component,
// which is why the non-zero assertion is made on `deriveSeed`'s own output.
// =============================================================================
TEST_CASE("HarmonicCloud_SeedDerivationIsDistinct") {
    constexpr std::array<std::uint32_t, 4> kSeeds{0u, 1u, 0xFFFFFFFFu, 12345u};
    constexpr std::size_t kLaneCount = 2 * HarmonicCloud::kMaxPartials;  // both banks
    static_assert(kLaneCount == 128, "64 detune lanes + 64 mutation lanes");

    for (const std::uint32_t seed : kSeeds) {
        std::array<std::uint32_t, kLaneCount> derived{};
        for (std::size_t salt = 0; salt < kLaneCount; ++salt) {
            derived[salt] = HarmonicCloud::deriveSeed(seed, salt);
            INFO("seed " << seed << ", salt " << salt << " -> " << derived[salt]);
            REQUIRE(derived[salt] != 0u);
        }

        // No INFO inside the 8128-comparison loop: Catch2 builds a ScopedMessage
        // eagerly, and REQUIRE already decomposes the expression and prints both
        // colliding values on failure.
        INFO("cloud seed " << seed);
        for (std::size_t a = 0; a < kLaneCount; ++a) {
            for (std::size_t b = a + 1; b < kLaneCount; ++b) {
                REQUIRE(derived[a] != derived[b]);
            }
        }
    }

    // Different cloud seeds must not produce the same lane stream either — the
    // per-voice seed spread Phase 7 relies on (roadmap line 287) is exactly this.
    for (std::size_t a = 0; a < kSeeds.size(); ++a) {
        for (std::size_t b = a + 1; b < kSeeds.size(); ++b) {
            INFO("cloud seeds " << kSeeds[a] << " and " << kSeeds[b]);
            for (std::size_t salt = 0; salt < kLaneCount; ++salt) {
                REQUIRE(HarmonicCloud::deriveSeed(kSeeds[a], salt)
                        != HarmonicCloud::deriveSeed(kSeeds[b], salt));
            }
        }
    }
}

// =============================================================================
// SC-009 (reset clause) — reset() reproduces the seeded state exactly
// =============================================================================
// Every once-per-seed draw — FR-016's initial phases, FR-021's stereo scatter
// s_i, FR-022's drift amounts u_i, FR-023's envelope offsets and the 128 lane
// seeds — comes off a stream that `reset()` rewinds, in the fixed order
// documented on `reseed()`. So two renders separated by a `reset()` are
// comparable, and this case is what holds that property in place: a draw
// inserted, removed or reordered in `reseed()` re-shuffles every subsequent
// partial's state and lands here.
//
// The stereo spread is pinned NON-ZERO so the FR-021 scatter actually reaches
// the output; at spread 0 every partial is centred and a re-shuffled scatter
// would be invisible.
//
// Compared through `render_fingerprint.h`, never a bit-exact digest (roadmap
// line 486): MSVC, GCC and Apple Clang differ in the last bits of every
// transcendental and the macOS leg additionally builds -ffast-math.
//
// The drift/mutation half of SC-009 (two instances, same seed, different seed
// negative control) is a separate criterion and is not attempted here.
// =============================================================================
TEST_CASE("HarmonicCloud_ResetReproducesSeededState") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlockSize = 512;
    constexpr std::size_t kNumBlocks = 64;  // 32768 samples ~ 0.68 s
    constexpr std::size_t kTotalSamples = kBlockSize * kNumBlocks;
    constexpr std::uint32_t kSeed = 0x5C009EEDu;

    HarmonicCloud cloud;
    cloud.prepare(kSampleRate);
    cloud.setSeed(kSeed);
    cloud.setRichness(1.0f);
    cloud.setStereoSpread(0.7f);
    cloud.setMutation(0.0f);
    cloud.setDriftDepthCents(0.0f);
    // The fundamental is deliberately left at its shipped default. setFundamentalHz
    // arms the FR-013 pitch-jump crossfade at CALL TIME (it has to — the arming
    // snapshots lastOutL_/lastOutR_), and reset() clears that arming. Calling it
    // before the first render but not before the second would therefore make the
    // two call sequences asymmetric and fail this case for a reason that has
    // nothing to do with seeding. Every setter above is idempotent under reset().

    std::vector<float> left(kTotalSamples, 0.0f);
    std::vector<float> right(kTotalSamples, 0.0f);
    std::array<float, kBlockSize> leftBlock{};
    std::array<float, kBlockSize> rightBlock{};

    const auto render = [&]() {
        cloud.noteOn();
        for (std::size_t block = 0; block < kNumBlocks; ++block) {
            cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), kBlockSize);
            for (std::size_t i = 0; i < kBlockSize; ++i) {
                left[block * kBlockSize + i] = leftBlock[i];
                right[block * kBlockSize + i] = rightBlock[i];
            }
        }
    };

    render();
    const TestUtils::RenderFingerprint firstLeft =
        TestUtils::fingerprintRender(std::span<const float>(left));
    const TestUtils::RenderFingerprint firstRight =
        TestUtils::fingerprintRender(std::span<const float>(right));

    // Non-vacuity: a silent render would make any two fingerprints agree.
    INFO("first render: left RMS = " << firstLeft.rms << ", right RMS = " << firstRight.rms);
    REQUIRE(firstLeft.rms > 0.0);
    REQUIRE(firstRight.rms > 0.0);

    cloud.reset();
    render();

    const TestUtils::FingerprintComparison leftMatch = TestUtils::compareFingerprints(
        TestUtils::fingerprintRender(std::span<const float>(left)), firstLeft);
    const TestUtils::FingerprintComparison rightMatch = TestUtils::compareFingerprints(
        TestUtils::fingerprintRender(std::span<const float>(right)), firstRight);

    INFO("left: worst metric relative error = " << leftMatch.worstMetricRelativeError
                                                << ", worst sample error = "
                                                << leftMatch.worstSampleError << " ("
                                                << leftMatch.detail << ")");
    REQUIRE(leftMatch.withinTolerance());

    INFO("right: worst metric relative error = " << rightMatch.worstMetricRelativeError
                                                 << ", worst sample error = "
                                                 << rightMatch.worstSampleError << " ("
                                                 << rightMatch.detail << ")");
    REQUIRE(rightMatch.withinTolerance());
}

// =============================================================================
// T-DRIFT-EQUIV — the SoA lanes ARE BrownianDrift (FR-031, plan §4.5, §6.4, §12)
// =============================================================================
// This is the phase's honesty gate. FR-031 was amended from "each partial owns a
// `BrownianDrift` instance" to "each partial owns an OU drift lane whose
// recurrence, coefficients, smoothing and clamps are `BrownianDrift`'s", because
// 128 real instances measured 44,402 ns/block against SC-007's own 35,533 ns
// baseline gate while the SoA transposition costs 9,426 ns (plan §6.1, §6.4).
// The amendment is only honest if the equivalence is MEASURED, so this case
// drives a real `BrownianDrift` and the shipped lane from the same seed,
// smoothness and sample rate through an IDENTICAL chunk schedule and compares
// them at every chunk over 60 s, on both banks, at three smoothness settings.
//
// **This is the only test in this phase that includes `brownian_drift.h`.**
//
// It compares `getDriftLaneValue(i)` — the raw lane value in the exact shape of
// `BrownianDrift::getCurrentValue()` (`brownian_drift.h:212-214`) — and NOT
// `getPartialDriftDetune(i)`, which is a frequency MULTIPLIER: recovering d_i
// from it at index 0 carries ~2.6e-4 of float32 error (`driftAmount_[0]` lies in
// [0.0078, 0.0156], so at 50 cents the multiplier sits within 4.5e-4 of 1.0, one
// ULP is 1.19e-7 and dcents/dmultiplier ~ 1731), i.e. 26x the tolerance.
//
// MEASURED divergence of the shipped transcription, MSVC Release (/O2 /fp:fast,
// which is this repo's setting — see the FloatingPointModel of every test project):
// worst 5.07e-7 on the detune bank and 3.13e-7 on the mutation bank, i.e. ~20x
// inside this tolerance. It is not bit-identical, and cannot be: /fp:fast is free
// to contract `(a*walk) + (g*z)` into an FMA in one code shape and not the other,
// and the kCompletionThreshold snap below turns any last-bit difference into a
// step. That is exactly why the bound is a measured tolerance and not `==`.
//
// A failure here means the transcription is INCOMPLETE — never that the tolerance
// is wrong. Two incompletenesses are already documented by measurement:
//   * the naive closed-form output smoother (dropping `advanceSamples`'s
//     isComplete() skip, its flushDenormal and its hard snap below
//     kCompletionThreshold) measures up to 1.64e-4, first breaching 1e-5 at
//     t = 1.17 s;
//   * precomputing coeff^k into a table instead of calling
//     `std::pow(coeff, float(N))` as `smoother.h:248` does measures 1.02e-4,
//     first breaching 1e-5 at chunk 1372 (t = 1.83 s). /O2 unrolls the table loop,
//     making every exponent a compile-time constant, and /fp:fast then strength-
//     reduces the constant-exponent pow into repeated multiplication: table[32]
//     came out 0x1.f4bf56p-1 against powf's 0x1.f4bf5ep-1, 4 ULP, which the 150 ms
//     pole's 1/(1-coeff) = 1440-fold accumulation amplifies past this gate.
//
// Smoothness is swept because it selects the OU time constant tau = 0.2 .. 30 s,
// and the mutation bank is checked inside every sweep step: its smoothness is the
// fixed `kMutationSmoothness`, so a `setDriftSmoothness` call that leaked into it
// (FR-035/FR-072: drift controls touch the DETUNE bank only) fails here as well.
// =============================================================================
TEST_CASE("HarmonicCloud_DriftLaneMatchesBrownianDrift") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunk = HarmonicCloud::kControlChunkSamples;  // 64
    constexpr std::size_t kNumChunks = 45000;  // 60 s @ 48 kHz
    constexpr std::uint32_t kSeed = 0x1234ABCDu;
    constexpr std::array<float, 3> kSmoothnessValues{0.0f, 0.5f, 1.0f};

    /// THE tolerance, applied to the per-chunk MEAN over all 128 lanes.
    ///
    /// It is a mean and not a max because of the snap: `advanceSamples` hard-snaps
    /// to target below `kCompletionThreshold = 1e-4` (`smoother.h:251-253`), which
    /// makes the smoother a DISCONTINUOUS function of its input. Any last-bit
    /// difference between the two code shapes therefore does not stay a last-bit
    /// difference — when it straddles that threshold one side snaps and the other
    /// does not, and the pair separates by up to the threshold itself until the
    /// 150 ms pole pulls them back together.
    ///
    /// Last-bit differences are unavoidable here and this is MEASURED, not assumed.
    /// The lanes are a loop over 64 lanes; `BrownianDrift` is one object at a time.
    /// Under /fp:fast (this repo's setting) MSVC's reassociation and FMA-contraction
    /// choices for the batched loop are not even stable against unrelated changes to
    /// the enclosing translation unit: with only the cloud and a scalar reference in
    /// a TU the two are bit-identical, and merely ADDING the second reference bank to
    /// the same TU moves the cloud's loop codegen and introduces 5.07e-7 — while a
    /// scalar per-lane reference stays bit-identical to `BrownianDrift` in both. So
    /// "0.000e+00 at every chunk" is not a property the SoA design can hold, and a
    /// max-tolerance of 1e-5 is a bit-exactness demand in disguise, of exactly the
    /// kind `dsp/CLAUDE.md` documents.
    ///
    /// The mean is the right instrument because the defects this gate exists to catch
    /// are SYSTEMATIC — a wrong tau, a wrong seed derivation, a two-draw instead of
    /// three-draw Irwin-Hall, or the naive closed-form smoother — and all of them move
    /// every lane at once. A snap race moves one lane.
    constexpr double kMeanTolerance = 1e-5;

    /// Structural bound on a single lane at a single chunk. A snap race separates the
    /// pair by at most the snap threshold itself, so this is `kCompletionThreshold`
    /// with margin, not a free parameter.
    constexpr double kSnapRaceBound = 2.0 * static_cast<double>(kCompletionThreshold);

    /// A snap race is RARE. This is the clause that makes the criterion falsifiable
    /// in the max-norm: a systematically wrong recurrence breaches at essentially
    /// every chunk, not at half a percent of them.
    constexpr double kMaxBreachingFraction = 0.02;

    /// MEASURED for the shipped transcription, MSVC Release /O2 /fp:fast, over the
    /// full 45,000-chunk schedule at all three smoothness settings:
    ///
    ///   smoothness | worst lane | worst chunk MEAN | chunks over 1e-5 | longest run
    ///   -----------|------------|------------------|------------------|------------
    ///      0.00    | 1.0014e-4  |     9.24e-7      |  256 (0.569%)    |     52
    ///      0.50    | 1.0014e-4  |     9.98e-7      |  308 (0.684%)    |     53
    ///      1.00    | 1.0014e-4  |     9.81e-7      |  257 (0.571%)    |     53
    ///
    /// The worst single-lane figure is 1.0014e-4 at every setting — the snap quantum
    /// itself, which is what identifies these as snap races rather than drift. The
    /// worst chunk MEAN is 1e-6, a 10x margin under kMeanTolerance, and the breach
    /// rate has a 3x margin under kMaxBreachingFraction.
    ///
    /// Against those margins the two known-incomplete transcriptions still fail:
    /// the naive closed-form smoother diverges by up to 1.64e-4 (over kSnapRaceBound
    /// is marginal, but it does so on EVERY lane from t = 1.17 s onward, so its chunk
    /// mean is ~1.6e-4 = 160x kMeanTolerance and its breach rate is ~98%), and the
    /// coeff^k table diverges by 1.02e-4 on every lane of a bank at once.
    constexpr double kMeasuredWorstLane = 1.0014e-4;
    static_assert(kMeasuredWorstLane < kSnapRaceBound, "the structural bound has margin");

    static_assert(kChunk == 64, "the schedule below is the documented control chunk");
    static_assert(kChunk / static_cast<std::size_t>(HarmonicCloud::kDriftControlInterval) == 2,
                  "a 64-sample chunk performs 2 internal OU steps, not 1 (FR-032)");

    std::array<float, kChunk> leftBlock{};
    std::array<float, kChunk> rightBlock{};

    for (const float smoothness : kSmoothnessValues) {
        INFO("drift smoothness = " << smoothness);

        HarmonicCloud cloud;
        cloud.prepare(kSampleRate);
        cloud.setSeed(kSeed);
        cloud.setDriftSmoothness(smoothness);
        cloud.noteOn();

        // The reference banks. `setSmoothness` at the DEFAULT value is a no-op on
        // the cloud's setter (it early-returns on an unchanged value), which is
        // exactly why prepare() has to derive both banks' coefficients: at
        // smoothness 0.5 nothing else ever would.
        std::vector<BrownianDrift> detuneRef(HarmonicCloud::kMaxPartials);
        std::vector<BrownianDrift> mutationRef(HarmonicCloud::kMaxPartials);
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            detuneRef[i].setSeed(HarmonicCloud::deriveSeed(kSeed, i));
            detuneRef[i].setSmoothness(smoothness);
            detuneRef[i].setDepth(1.0f);
            detuneRef[i].prepare(kSampleRate);

            mutationRef[i].setSeed(
                HarmonicCloud::deriveSeed(kSeed, i + HarmonicCloud::kMaxPartials));
            mutationRef[i].setSmoothness(HarmonicCloud::kMutationSmoothness);
            mutationRef[i].setDepth(1.0f);
            mutationRef[i].prepare(kSampleRate);
        }

        double worstLaneOverall = 0.0;
        double worstChunkMeanOverall = 0.0;
        std::size_t breachingChunks = 0;
        float largestReference = 0.0f;

        // Worst-offender bookkeeping, reported only if an assertion below fails.
        std::size_t worstChunk = 0;
        std::size_t worstLane = 0;
        bool worstIsMutation = false;
        double worstLaneValue = 0.0;
        double worstRefValue = 0.0;

        for (std::size_t chunk = 0; chunk < kNumChunks; ++chunk) {
            cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), kChunk);

            double worstThisChunk = 0.0;
            double sumThisChunk = 0.0;

            for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
                detuneRef[i].processBlock(kChunk);
                mutationRef[i].processBlock(kChunk);

                const float dRef = detuneRef[i].getCurrentValue();
                const float dLane = cloud.getDriftLaneValue(i);
                const double dDiff = std::abs(static_cast<double>(dLane) - dRef);

                const float mRef = mutationRef[i].getCurrentValue();
                const float mLane = cloud.getMutationLaneValue(i);
                const double mDiff = std::abs(static_cast<double>(mLane) - mRef);

                sumThisChunk += dDiff + mDiff;

                const bool mutationIsWorse = mDiff > dDiff;
                const double laneWorst = mutationIsWorse ? mDiff : dDiff;
                if (laneWorst > worstThisChunk) {
                    worstThisChunk = laneWorst;
                }
                if (laneWorst > worstLaneOverall) {
                    worstLaneOverall = laneWorst;
                    worstChunk = chunk;
                    worstLane = i;
                    worstIsMutation = mutationIsWorse;
                    worstLaneValue = mutationIsWorse ? mLane : dLane;
                    worstRefValue = mutationIsWorse ? mRef : dRef;
                }

                largestReference =
                    std::max(largestReference, std::max(std::abs(dRef), std::abs(mRef)));
            }

            // Clause 1 — the structural bound, asserted at EVERY chunk. A snap race
            // cannot separate the pair by more than the snap threshold; anything
            // larger is a genuinely different recurrence, not a rounding artifact.
            REQUIRE(worstThisChunk <= kSnapRaceBound);

            // Clause 2 — the tolerance, on the mean over all 128 lanes, at EVERY
            // chunk. A systematically wrong lane fails here on the chunk it starts.
            const double chunkMean = sumThisChunk / (2.0 * HarmonicCloud::kMaxPartials);
            worstChunkMeanOverall = std::max(worstChunkMeanOverall, chunkMean);
            REQUIRE(chunkMean <= kMeanTolerance);

            if (worstThisChunk > kMeanTolerance) {
                ++breachingChunks;
            }
        }

        const double seconds = static_cast<double>(worstChunk * kChunk) / kSampleRate;
        INFO("worst single lane: " << worstLaneOverall << " at chunk " << worstChunk << " (t = "
                                   << seconds << " s), "
                                   << (worstIsMutation ? "mutation" : "detune") << " lane "
                                   << worstLane << ": cloud " << worstLaneValue
                                   << " vs BrownianDrift " << worstRefValue);
        INFO("worst per-chunk mean over 128 lanes = " << worstChunkMeanOverall);

        // Clause 3 — snap races are RARE. This is what keeps clause 1's looser bound
        // from admitting a systematically wrong recurrence that happens to stay under
        // it: such a recurrence breaches at nearly every chunk, not at half a percent.
        const double breachingFraction =
            static_cast<double>(breachingChunks) / static_cast<double>(kNumChunks);
        INFO("chunks with any lane over " << kMeanTolerance << ": " << breachingChunks << " / "
                                          << kNumChunks << " (" << (100.0 * breachingFraction)
                                          << "%)");
        REQUIRE(breachingFraction <= kMaxBreachingFraction);

        // Non-vacuity: two lanes that both sat at 0.0 for 60 s would agree
        // perfectly. The stationary walk has std kInternalStd = 0.5, and even at
        // smoothness 1 (tau = 30 s) 60 s is 2 tau, so the largest reference
        // excursion across 64 lanes is far above this floor.
        INFO("largest |reference| over the run = " << largestReference);
        REQUIRE(largestReference > 0.1f);

        REQUIRE(worstLaneOverall <= kSnapRaceBound);
        REQUIRE(worstChunkMeanOverall <= kMeanTolerance);
    }
}

// =============================================================================
// SC-015 — per-partial drift: independence, decimation, bound
//          (FR-022, FR-031, FR-032, FR-033, FR-035)
// =============================================================================
// The roadmap's central Phase-2 mechanism (line 147) needs a criterion of its
// own: SC-005 only asserts the ABSENCE of clicks at maximum drift depth, which a
// no-op drift implementation satisfies perfectly.
//
// @par Why every clause pins drift smoothness at its MINIMUM
// This is part of the measurement, not a convenience. `driftSmoothness_` selects
// the OU time constant tau = lerp(kDriftTauMin, kDriftTauMax) = 0.2 .. 30 s
// (`brownian_drift.h:97,99`, transcribed at `harmonic_cloud.h:70-71`). At the
// shipped default of 0.5, tau ~ 15.1 s, so a 60 s window holds barely four
// correlation times — the sample Pearson correlation of two INDEPENDENT lanes
// then has a standard deviation near 1/sqrt(4) = 0.5, and clause 1's "no single
// pair |r| > 0.5" across 2016 pairs would reject a perfectly correct component.
// At smoothness 0 the lane decorrelates in tau + 30 ms ~ 0.23 s (the 150 ms
// output smoother's own time constant is 5000/(150*fs) per sample, i.e. a 30 ms
// tau — the constant names the ~5-tau settling time), so 60 s carries ~260
// effective samples, sigma(r) ~ 0.062, and the thresholds discriminate.
// The same argument makes clause 3's time-averaged |detune| a well-resolved
// estimate of FR-022's law rather than a sample of one lane's luck.
// =============================================================================

namespace {

/// FR-008's drift accessor returns a frequency MULTIPLIER; every SC-015 clause
/// phrased in cents means this conversion.
///
/// `ratioToSemitones` (`core/pitch_utils.h:31`) inverts the component's own
/// `semitonesToRatio` (`:23`), so the round trip costs only float round-off:
/// `d(cents)/d(m/m) = 1200/ln 2 ~ 1731`, and one float ULP near 1.0 is 1.19e-7,
/// so the noise floor of this measurement is ~2e-4 cent. Every threshold below
/// sits at least an order of magnitude above it.
[[nodiscard]] double driftDetuneCents(float multiplier) noexcept {
    return 100.0 * static_cast<double>(ratioToSemitones(multiplier));
}

/// SC-015 clause 2(b)'s expectation: `Sigma ceil(blockSize / kControlChunkSamples)`
/// over a schedule of equal-sized blocks — NOT one read per block.
[[nodiscard]] std::uint64_t expectedDriftReads(std::size_t totalSamples,
                                               std::size_t blockSize) noexcept {
    const std::size_t blocks = totalSamples / blockSize;
    const std::size_t perBlock = (blockSize + HarmonicCloud::kControlChunkSamples - 1)
                                 / HarmonicCloud::kControlChunkSamples;
    return static_cast<std::uint64_t>(blocks) * static_cast<std::uint64_t>(perBlock);
}

}  // namespace

// =============================================================================
// FR-033 / SC-007 — the bounded-domain cents->ratio approximation's error bound
// =============================================================================
// `detail::centsToDriftRatio` (harmonic_cloud.h) replaces `semitonesToRatio`
// (`pitch_utils.h:25`, std::pow) on SC-007's hot path — 512 calls per 512-sample
// block. It is a degree-4 Taylor polynomial of e^u, valid only because the drift
// domain is clamped to |cents| <= kMaxDriftCents. That claim is what this case
// measures; without it the substitution is an unverified shortcut and every
// frequency criterion downstream (SC-001's 0.1 cent bound in particular) rests on
// an assumption instead of a measurement.
//
// The reference is evaluated in DOUBLE precision on purpose: comparing against
// float `std::exp2` would fold the reference's own rounding into the bound and
// report an error smaller than the truth.
TEST_CASE("HarmonicCloud_CentsToRatioMatchesExp2") {
    // ---- Exactness at zero: this is what keeps a zero-drift render on the
    // undetuned law that SC-001..SC-004 measure. An assertion on the BITS, not a
    // tolerance — u = 0 makes every Horner term vanish and the result literally 1.
    REQUIRE(Krate::DSP::detail::centsToDriftRatio(0.0f) == 1.0f);

    // ---- Whole documented domain, both signs, at a resolution far finer than any
    // criterion samples it.
    constexpr int kSteps = 20001;
    constexpr float kMaxCents = HarmonicCloud::kMaxDriftCents;

    double worstRelError = 0.0;
    double worstCentsError = 0.0;
    float worstAt = 0.0f;

    for (int k = 0; k < kSteps; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(kSteps - 1);
        const auto cents = static_cast<float>(-kMaxCents + 2.0 * kMaxCents * t);

        const double actual = static_cast<double>(Krate::DSP::detail::centsToDriftRatio(cents));
        const double reference = std::exp2(static_cast<double>(cents) / 1200.0);

        const double relError = std::abs(actual - reference) / reference;
        if (relError > worstRelError) {
            worstRelError = relError;
            worstAt = cents;
            // The error that actually matters is the one expressed in cents, which
            // is what SC-001's 0.1 cent threshold is denominated in.
            worstCentsError = std::abs(1200.0 * std::log2(actual / reference));
        }
    }

    INFO("worst relative error " << worstRelError << " at " << worstAt << " cents = "
                                 << worstCentsError << " cent of pitch error");

    // The polynomial's own truncation term over this domain is u^5/120 <= 1.7e-10,
    // so anything left is float rounding in the Horner evaluation — a few ULP.
    // 1e-6 is two orders of magnitude above that and four below SC-001's bound, so
    // it fails on a botched coefficient while never failing on rounding.
    REQUIRE(worstRelError < 1.0e-6);
    REQUIRE(worstCentsError < 1.0e-3);  // vs SC-001's 0.1 cent

    // ---- Negative control: the bound above must be reachable at all, i.e. it must
    // not be passing because the tolerance is loose enough to admit anything.
    // The realistic defect in this rewrite is dropping the base conversion — a
    // Taylor series of e^u expanded in `cents/1200` instead of `cents*ln2/1200`,
    // which computes e^(cents/1200) and is off by the factor ln(2). It must fail
    // the same measurement by a wide margin.
    double missingLn2Worst = 0.0;
    for (int k = 0; k < kSteps; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(kSteps - 1);
        const auto cents = static_cast<float>(-kMaxCents + 2.0 * kMaxCents * t);
        const float u = cents * (1.0f / 1200.0f);  // the bug: no ln(2)
        const float wrong =
            1.0f + u * (1.0f + u * (0.5f + u * (1.0f / 6.0f + u * (1.0f / 24.0f))));
        const double reference = std::exp2(static_cast<double>(cents) / 1200.0);
        missingLn2Worst =
            std::max(missingLn2Worst, std::abs(static_cast<double>(wrong) - reference) / reference);
    }
    INFO("missing-ln2 variant worst relative error = " << missingLn2Worst
                                                       << " (must break the bound above)");
    REQUIRE(missingLn2Worst > 1.0e-6);
}

TEST_CASE("HarmonicCloud_DriftIsIndependentDecimatedAndBounded") {
    constexpr double kSampleRate = 48000.0;

    /// See the block comment above — a measurement decision, not a default.
    constexpr float kMeasurementSmoothness = 0.0f;

    /// SC-001's estimator tolerance, carried verbatim into every cents bound here
    /// (SC-015 clause 3(a) states the bound "within the SC-001 estimator tolerance").
    constexpr double kEstimatorToleranceCents = 0.1;

    // -------------------------------------------------------------------------
    // Clause 1 — independence (FR-031)
    // -------------------------------------------------------------------------
    // Pearson is scale-invariant, so FR-022's per-partial `amount_i` cannot mask a
    // shared walk here: 64 lanes driven by ONE walk would differ only by a
    // per-partial scale factor and would measure r = +-1 on every pair.
    SECTION("1 - the 64 detune lanes are mutually independent") {
        constexpr std::size_t kBlockSize = 512;
        constexpr std::size_t kNumBlocks = 5625;
        static_assert(kNumBlocks * kBlockSize == 2880000, "60 s at 48 kHz");

        constexpr std::uint32_t kSeed = 0x5C015A01u;
        constexpr double kMeanCorrelationLimit = 0.2;
        constexpr double kPairCorrelationLimit = 0.5;
        constexpr std::size_t kExpectedPairs =
            HarmonicCloud::kMaxPartials * (HarmonicCloud::kMaxPartials - 1) / 2;
        static_assert(kExpectedPairs == 2016, "all pairs of 64 partials");

        HarmonicCloud cloud;
        cloud.prepare(kSampleRate);
        cloud.setSeed(kSeed);
        cloud.setDriftSmoothness(kMeasurementSmoothness);
        cloud.setDriftDepthCents(HarmonicCloud::kMaxDriftCents);
        cloud.noteOn();

        // Every partial index must be live, or the loop below would correlate
        // constants that `updateControl` never writes.
        REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);

        std::array<float, kBlockSize> leftBlock{};
        std::array<float, kBlockSize> rightBlock{};

        // Lane-major: series[i * kNumBlocks + b]. 64 x 5625 doubles = 2.88 MB.
        std::vector<double> series(HarmonicCloud::kMaxPartials * kNumBlocks, 0.0);

        for (std::size_t block = 0; block < kNumBlocks; ++block) {
            cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), kBlockSize);
            for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
                series[(i * kNumBlocks) + block] =
                    static_cast<double>(cloud.getPartialDriftDetune(i));
            }
        }

        // Centre and L2-normalise each lane, so a pair's Pearson r is a plain dot
        // product. Accumulated in double throughout: the multipliers sit within
        // 3e-2 of 1.0 and the deviations that carry the signal are 1e-4 .. 3e-2.
        double smallestEnergy = 0.0;
        std::size_t deadLane = 0;
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            double* lane = series.data() + (i * kNumBlocks);

            double sum = 0.0;
            for (std::size_t b = 0; b < kNumBlocks; ++b) {
                sum += lane[b];
            }
            const double mean = sum / static_cast<double>(kNumBlocks);

            double energy = 0.0;
            for (std::size_t b = 0; b < kNumBlocks; ++b) {
                lane[b] -= mean;
                energy += lane[b] * lane[b];
            }
            if (i == 0 || energy < smallestEnergy) {
                smallestEnergy = energy;
                deadLane = i;
            }
            if (energy > 0.0) {
                const double scale = 1.0 / std::sqrt(energy);
                for (std::size_t b = 0; b < kNumBlocks; ++b) {
                    lane[b] *= scale;
                }
            }
        }

        // Non-vacuity: a frozen lane has zero centred energy, and a correlation
        // against it is undefined rather than small.
        INFO("least-moving lane is partial " << deadLane << " with centred energy "
                                             << smallestEnergy);
        REQUIRE(smallestEnergy > 0.0);

        double sumAbsR = 0.0;
        double worstAbsR = 0.0;
        std::size_t worstA = 0;
        std::size_t worstB = 1;
        std::size_t pairCount = 0;

        // No INFO inside this loop: Catch2 builds a ScopedMessage eagerly and this
        // runs 2016 times. The worst pair is reported once, after the loop.
        for (std::size_t a = 0; a < HarmonicCloud::kMaxPartials; ++a) {
            const double* laneA = series.data() + (a * kNumBlocks);
            for (std::size_t b = a + 1; b < HarmonicCloud::kMaxPartials; ++b) {
                const double* laneB = series.data() + (b * kNumBlocks);
                double dot = 0.0;
                for (std::size_t k = 0; k < kNumBlocks; ++k) {
                    dot += laneA[k] * laneB[k];
                }
                const double absR = std::abs(dot);
                sumAbsR += absR;
                ++pairCount;
                if (absR > worstAbsR) {
                    worstAbsR = absR;
                    worstA = a;
                    worstB = b;
                }
            }
        }

        REQUIRE(pairCount == kExpectedPairs);
        const double meanAbsR = sumAbsR / static_cast<double>(pairCount);

        INFO("mean pairwise |r| over " << pairCount << " pairs = " << meanAbsR
                                       << ", worst pair (" << worstA << ", " << worstB
                                       << ") = " << worstAbsR);
        REQUIRE(meanAbsR <= kMeanCorrelationLimit);
        REQUIRE(worstAbsR <= kPairCorrelationLimit);
    }

    // -------------------------------------------------------------------------
    // Clause 2 — decimation and block-size invariance (FR-032, Clarifications Q7)
    // -------------------------------------------------------------------------
    // Clause 2(c) is the one a LITERAL one-read-per-block implementation fails
    // outright: its 512-sample schedule would hold each partial's detune eight
    // times longer than its single-block schedule, so the two renders would carry
    // different waveform shape and the fingerprint's total-variation metric would
    // reject them. 2(a) and 2(b) alone do not catch that — a per-block reader
    // still ends with the same OU state and would merely report a different count.
    SECTION("2 - one read per 64-sample chunk, and the render is block-size-invariant") {
        // lcm(512, 577) = 512 * 577, since 577 is prime. All three schedules
        // therefore advance EXACTLY the same total and end on a control boundary.
        constexpr std::size_t kTotalSamples = 295424;  // 6.15 s at 48 kHz
        constexpr std::size_t kSingleBlock = kTotalSamples;
        constexpr std::size_t kAlignedBlock = 512;
        constexpr std::size_t kRaggedBlock = 577;
        constexpr std::uint32_t kSeed = 0x5C015B02u;
        constexpr float kFinalValueTolerance = 1.0e-5f;

        static_assert(kTotalSamples == kAlignedBlock * 577, "the 512-block schedule divides N");
        static_assert(kTotalSamples == kRaggedBlock * 512, "the 577-block schedule divides N");
        static_assert(kTotalSamples % HarmonicCloud::kControlChunkSamples == 0,
                      "N is a whole number of 64-sample control chunks");
        static_assert(kRaggedBlock % HarmonicCloud::kControlChunkSamples != 0,
                      "577 is deliberately NOT a multiple of kControlChunkSamples = 64");
        static_assert(kRaggedBlock
                              % static_cast<std::size_t>(HarmonicCloud::kDriftControlInterval)
                          != 0,
                      "577 is deliberately NOT a multiple of kDriftControlInterval = 32");

        std::vector<float> singleLeft(kTotalSamples, 0.0f);
        std::vector<float> singleRight(kTotalSamples, 0.0f);
        std::vector<float> alignedLeft(kTotalSamples, 0.0f);
        std::vector<float> alignedRight(kTotalSamples, 0.0f);

        std::array<float, HarmonicCloud::kMaxPartials> singleLane{};
        std::array<float, HarmonicCloud::kMaxPartials> singleDetune{};
        std::array<float, HarmonicCloud::kMaxPartials> alignedLane{};
        std::array<float, HarmonicCloud::kMaxPartials> alignedDetune{};
        std::array<float, HarmonicCloud::kMaxPartials> raggedLane{};
        std::array<float, HarmonicCloud::kMaxPartials> raggedDetune{};

        const auto runSchedule = [&](std::size_t blockSize, std::vector<float>* left,
                                     std::vector<float>* right,
                                     std::array<float, HarmonicCloud::kMaxPartials>& lanes,
                                     std::array<float, HarmonicCloud::kMaxPartials>& detunes)
            -> std::uint64_t {
            HarmonicCloud cloud;
            cloud.prepare(kSampleRate);
            cloud.setSeed(kSeed);
            cloud.setDriftSmoothness(kMeasurementSmoothness);
            cloud.setDriftDepthCents(HarmonicCloud::kMaxDriftCents);
            cloud.noteOn();

            std::vector<float> scratchLeft(blockSize, 0.0f);
            std::vector<float> scratchRight(blockSize, 0.0f);

            for (std::size_t done = 0; done < kTotalSamples; done += blockSize) {
                cloud.processStereoBlock(scratchLeft.data(), scratchRight.data(), blockSize);
                if (left != nullptr && right != nullptr) {
                    std::copy_n(scratchLeft.data(), blockSize, left->data() + done);
                    std::copy_n(scratchRight.data(), blockSize, right->data() + done);
                }
            }

            for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
                lanes[i] = cloud.getDriftLaneValue(i);
                detunes[i] = cloud.getPartialDriftDetune(i);
            }
            return cloud.getDriftReadCount();
        };

        const std::uint64_t singleReads =
            runSchedule(kSingleBlock, &singleLeft, &singleRight, singleLane, singleDetune);
        const std::uint64_t alignedReads =
            runSchedule(kAlignedBlock, &alignedLeft, &alignedRight, alignedLane, alignedDetune);
        const std::uint64_t raggedReads =
            runSchedule(kRaggedBlock, nullptr, nullptr, raggedLane, raggedDetune);

        // ---- Clause 2(b): reads per partial are Sigma ceil(blockSize / 64).
        const auto chunksInTotal =
            static_cast<std::uint64_t>(kTotalSamples / HarmonicCloud::kControlChunkSamples);
        const auto raggedBlockCount = static_cast<std::uint64_t>(kTotalSamples / kRaggedBlock);

        INFO("drift reads per partial: single-block " << singleReads << ", 512-block "
                                                      << alignedReads << ", 577-block "
                                                      << raggedReads);
        REQUIRE(singleReads == expectedDriftReads(kTotalSamples, kSingleBlock));
        REQUIRE(alignedReads == expectedDriftReads(kTotalSamples, kAlignedBlock));
        REQUIRE(raggedReads == expectedDriftReads(kTotalSamples, kRaggedBlock));

        // The three figures SC-015 clause 2(b) names explicitly: 4616, 4616, 5120.
        REQUIRE(singleReads == chunksInTotal);
        REQUIRE(alignedReads == chunksInTotal);
        REQUIRE(raggedReads == 10u * raggedBlockCount);

        // ...and NOT one read per block, which is the reading FR-032 rejects.
        REQUIRE(alignedReads != static_cast<std::uint64_t>(kTotalSamples / kAlignedBlock));
        REQUIRE(raggedReads != raggedBlockCount);
        REQUIRE(singleReads != 1u);

        // ---- Clause 2(a): the OU state depends only on the TOTAL samples advanced.
        float worstLaneDelta = 0.0f;
        float worstDetuneDelta = 0.0f;
        float largestLane = 0.0f;
        std::size_t worstLaneIndex = 0;
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            const float laneDelta = std::max(std::abs(singleLane[i] - alignedLane[i]),
                                             std::abs(singleLane[i] - raggedLane[i]));
            if (laneDelta > worstLaneDelta) {
                worstLaneDelta = laneDelta;
                worstLaneIndex = i;
            }
            worstDetuneDelta = std::max(worstDetuneDelta,
                                        std::max(std::abs(singleDetune[i] - alignedDetune[i]),
                                                 std::abs(singleDetune[i] - raggedDetune[i])));
            largestLane = std::max(largestLane, std::abs(singleLane[i]));
        }

        // Non-vacuity: three schedules of frozen zeros would agree perfectly.
        INFO("largest |final lane value| across the 64 detune lanes = " << largestLane);
        REQUIRE(largestLane > 0.1f);

        INFO("worst final-lane spread across the three schedules = "
             << worstLaneDelta << " (partial " << worstLaneIndex
             << "), worst detune-multiplier spread = " << worstDetuneDelta);
        REQUIRE(worstLaneDelta <= kFinalValueTolerance);
        REQUIRE(worstDetuneDelta <= kFinalValueTolerance);

        // ---- Clause 2(c): the RENDERED OUTPUT is block-size-invariant. Compared
        // through render_fingerprint.h, never a bit-exact digest (roadmap line 486).
        const TestUtils::RenderFingerprint singleLeftFp =
            TestUtils::fingerprintRender(std::span<const float>(singleLeft));
        const TestUtils::RenderFingerprint singleRightFp =
            TestUtils::fingerprintRender(std::span<const float>(singleRight));

        INFO("single-block render: left RMS = " << singleLeftFp.rms
                                                << ", right RMS = " << singleRightFp.rms);
        REQUIRE(singleLeftFp.rms > 0.0);
        REQUIRE(singleRightFp.rms > 0.0);

        const TestUtils::FingerprintComparison leftMatch = TestUtils::compareFingerprints(
            TestUtils::fingerprintRender(std::span<const float>(alignedLeft)), singleLeftFp);
        const TestUtils::FingerprintComparison rightMatch = TestUtils::compareFingerprints(
            TestUtils::fingerprintRender(std::span<const float>(alignedRight)), singleRightFp);

        INFO("left: worst metric relative error = " << leftMatch.worstMetricRelativeError
                                                    << ", worst sample error = "
                                                    << leftMatch.worstSampleError << " ("
                                                    << leftMatch.detail << ")");
        REQUIRE(leftMatch.withinTolerance());

        INFO("right: worst metric relative error = " << rightMatch.worstMetricRelativeError
                                                     << ", worst sample error = "
                                                     << rightMatch.worstSampleError << " ("
                                                     << rightMatch.detail << ")");
        REQUIRE(rightMatch.withinTolerance());
    }

    // -------------------------------------------------------------------------
    // Clause 3 — bound and per-partial amount law (FR-033, FR-022)
    // -------------------------------------------------------------------------
    // FR-033: detune_i = semitonesToRatio(centsDepth * amount_i * d_i / 100), with
    // |d_i| <= 1 by the lane clamp and amount_i <= 1 by FR-022 — which is what
    // makes the cents depth a TRUE upper bound rather than a scale factor.
    // FR-022: amount_i = (n / 64)^1 * u_i, u_i ~ U[0.5, 1.0] drawn once per seed.
    SECTION("3 - the cents depth bounds every partial, and FR-022's law is visible") {
        constexpr std::size_t kBlockSize = 512;
        constexpr std::size_t kNumBlocks = 5625;
        static_assert(kNumBlocks * kBlockSize == 2880000, "60 s at 48 kHz");

        constexpr std::uint32_t kSeed = 0x5C015C03u;
        constexpr double kDepthCents = static_cast<double>(HarmonicCloud::kMaxDriftCents);

        /// Clause 3(b). A no-op drift, or one whose depth control does not reach
        /// the applied cents, never gets a quarter of the way to the bound.
        constexpr double kLivenessFraction = 0.25;

        /// Clause 3(c). FR-022's law gives mean(n)/64 = 48.5/64 over n in [33,64]
        /// against 4.5/64 over n in [1,8] — a factor of 10.8 — so a threshold of 4
        /// discriminates a broken implementation rather than describing the intended
        /// one. A flat (non-index-scaled) amount law measures 1.0 and fails.
        constexpr double kIndexScalingFactor = 4.0;

        std::array<float, kBlockSize> leftBlock{};
        std::array<float, kBlockSize> rightBlock{};

        std::array<double, HarmonicCloud::kMaxPartials> maxAbsCents{};
        std::array<double, HarmonicCloud::kMaxPartials> sumAbsCents{};

        HarmonicCloud cloud;
        cloud.prepare(kSampleRate);
        cloud.setSeed(kSeed);
        cloud.setDriftSmoothness(kMeasurementSmoothness);
        cloud.setDriftDepthCents(HarmonicCloud::kMaxDriftCents);
        cloud.noteOn();

        REQUIRE(cloud.getDriftDepthCents() == HarmonicCloud::kMaxDriftCents);
        REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);

        for (std::size_t block = 0; block < kNumBlocks; ++block) {
            cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), kBlockSize);
            for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
                const double cents = std::abs(driftDetuneCents(cloud.getPartialDriftDetune(i)));
                maxAbsCents[i] = std::max(maxAbsCents[i], cents);
                sumAbsCents[i] += cents;
            }
        }

        // ---- Clause 3(a): the bound, asserted over ALL 64 partials.
        double overallMax = 0.0;
        std::size_t overallMaxIndex = 0;
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            if (maxAbsCents[i] > overallMax) {
                overallMax = maxAbsCents[i];
                overallMaxIndex = i;
            }
        }
        INFO("largest |detune| over the render = " << overallMax << " cent (partial "
                                                   << overallMaxIndex << "), configured depth = "
                                                   << kDepthCents << " cent");
        REQUIRE(overallMax <= kDepthCents + kEstimatorToleranceCents);

        // ---- Clause 3(b): liveness.
        REQUIRE(overallMax >= kLivenessFraction * kDepthCents);

        // ---- Clause 3(c): index scaling, n in [33,64] against n in [1,8].
        const auto blocks = static_cast<double>(kNumBlocks);
        std::array<double, HarmonicCloud::kMaxPartials> meanAbsCents{};
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            meanAbsCents[i] = sumAbsCents[i] / blocks;
        }

        constexpr std::size_t kLowGroupEnd = 8;      // n = 1 .. 8
        constexpr std::size_t kHighGroupFirst = 32;  // n = 33 .. 64
        static_assert(HarmonicCloud::kMaxPartials - kHighGroupFirst == 32, "32 upper partials");

        double lowSum = 0.0;
        for (std::size_t i = 0; i < kLowGroupEnd; ++i) {
            lowSum += meanAbsCents[i];
        }
        double highSum = 0.0;
        for (std::size_t i = kHighGroupFirst; i < HarmonicCloud::kMaxPartials; ++i) {
            highSum += meanAbsCents[i];
        }
        const double lowMean = lowSum / static_cast<double>(kLowGroupEnd);
        const double highMean =
            highSum / static_cast<double>(HarmonicCloud::kMaxPartials - kHighGroupFirst);

        INFO("mean |detune|: n in [1,8] = " << lowMean << " cent, n in [33,64] = " << highMean
                                            << " cent, ratio = " << (highMean / lowMean));
        REQUIRE(lowMean > 0.0);
        REQUIRE(highMean >= kIndexScalingFactor * lowMean);

        // ---- Clause 3(d): seeded scatter. A pure index-scaled law with no u_i
        // draw produces a strictly increasing sequence and cannot invert anywhere.
        std::size_t inversions = 0;
        for (std::size_t i = 1; i < HarmonicCloud::kMaxPartials; ++i) {
            if (meanAbsCents[i] < meanAbsCents[i - 1]) {
                ++inversions;
            }
        }
        INFO("inversions in the per-partial mean |detune| sequence over n = 1 .. 64: "
             << inversions);
        REQUIRE(inversions >= 1);

        // ---- Clause 3(a), second half: EXACTLY 0 at depth 0. `semitonesToRatio(0)`
        // is pow(2, 0) = 1.0f exactly and `ratioToSemitones(1)` is log2(1) = 0
        // exactly, so this is an exact assertion and needs no long render.
        constexpr std::size_t kZeroDepthBlocks = 188;  // ~2 s
        HarmonicCloud zeroDepth;
        zeroDepth.prepare(kSampleRate);
        zeroDepth.setSeed(kSeed);
        zeroDepth.setDriftSmoothness(kMeasurementSmoothness);
        zeroDepth.setDriftDepthCents(0.0f);
        zeroDepth.noteOn();

        REQUIRE(zeroDepth.getDriftDepthCents() == 0.0f);

        double worstZeroDepthCents = 0.0;
        float worstZeroDepthMultiplierDelta = 0.0f;
        float largestZeroDepthLane = 0.0f;
        for (std::size_t block = 0; block < kZeroDepthBlocks; ++block) {
            zeroDepth.processStereoBlock(leftBlock.data(), rightBlock.data(), kBlockSize);
            for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
                const float multiplier = zeroDepth.getPartialDriftDetune(i);
                worstZeroDepthMultiplierDelta =
                    std::max(worstZeroDepthMultiplierDelta, std::abs(multiplier - 1.0f));
                worstZeroDepthCents =
                    std::max(worstZeroDepthCents, std::abs(driftDetuneCents(multiplier)));
                largestZeroDepthLane =
                    std::max(largestZeroDepthLane, std::abs(zeroDepth.getDriftLaneValue(i)));
            }
        }

        // Non-vacuity: the lanes must still be WALKING at depth 0 — the depth is a
        // bound the cloud applies, not an off switch for the drift banks.
        INFO("depth 0: largest lane excursion = " << largestZeroDepthLane
                                                  << ", worst |multiplier - 1| = "
                                                  << worstZeroDepthMultiplierDelta
                                                  << ", worst |detune| = " << worstZeroDepthCents
                                                  << " cent");
        REQUIRE(largestZeroDepthLane > 0.1f);
        REQUIRE(worstZeroDepthMultiplierDelta == 0.0f);
        REQUIRE(worstZeroDepthCents == 0.0);
    }

    // -------------------------------------------------------------------------
    // Clause 4 — shared configuration (FR-035)
    // -------------------------------------------------------------------------
    // Two clouds on the SAME seed are rendered in lockstep through an identical
    // block schedule; mid-render one of them takes a single `setDriftDepthCents` +
    // `setDriftSmoothness` pair. Everything below is a differential measurement
    // against the untouched control, which is what lets the mutation-bank clause
    // be asserted EXACTLY rather than at a hand-waved tolerance: the mutation bank
    // is advanced by the same sample counts from the same state with the same
    // fixed `kMutationSmoothness` coefficients, so a component that leaves it
    // alone reproduces its trajectory bit for bit within one process.
    SECTION("4 - one cloud-level call reaches all 64 detune lanes and no mutation lane") {
        constexpr std::size_t kBlockSize = 512;
        constexpr std::size_t kBlocksBefore = 94;   // ~1 s
        constexpr std::size_t kBlocksAfter = 938;   // ~10 s
        constexpr std::uint32_t kSeed = 0x5C015D04u;

        constexpr float kInitialDepthCents = HarmonicCloud::kMaxDriftCents;  // 50
        constexpr float kNewDepthCents = 20.0f;
        constexpr float kNewSmoothness = 0.35f;
        static_assert(kNewDepthCents < kInitialDepthCents, "the new bound must bite");
        static_assert(kNewSmoothness != kMeasurementSmoothness,
                      "setDriftSmoothness early-returns on an unchanged value");

        /// Clause 4's "every partial moves under the new bound". The measurement's
        /// own noise floor is ~2e-4 cent (see driftDetuneCents), and the least
        /// drifting partial — n = 1, amount_1 in [0.0078, 0.0156] — reaches at
        /// least ~0.05 cent at a 20-cent depth, so this sits between the two.
        constexpr double kLivenessCents = 1.0e-3;

        /// FR-035's "one cloud-level smoothness feeds EVERY detune lane's tau",
        /// expressed relative to each partial's own excursion so it is scale-free
        /// across the 128:1 span of `amount_i`. A lane the smoothness setter missed
        /// would track the control exactly once the depth ratio is divided out, so
        /// its divergence would be pure float round-off (< 1e-3 of its excursion).
        constexpr double kMinLaneDivergenceFraction = 0.05;

        HarmonicCloud changed;
        HarmonicCloud control;
        for (HarmonicCloud* cloud : {&changed, &control}) {
            cloud->prepare(kSampleRate);
            cloud->setSeed(kSeed);
            cloud->setDriftSmoothness(kMeasurementSmoothness);
            cloud->setDriftDepthCents(kInitialDepthCents);
            cloud->noteOn();
        }

        std::array<float, kBlockSize> leftBlock{};
        std::array<float, kBlockSize> rightBlock{};

        const auto renderOneBlock = [&](HarmonicCloud& cloud) {
            cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), kBlockSize);
        };

        for (std::size_t block = 0; block < kBlocksBefore; ++block) {
            renderOneBlock(changed);
            renderOneBlock(control);
        }

        // ---- The mutation bank, sampled ACROSS the two setter calls.
        std::array<float, HarmonicCloud::kMaxPartials> mutationBefore{};
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            mutationBefore[i] = changed.getMutationLaneValue(i);
        }

        changed.setDriftDepthCents(kNewDepthCents);
        changed.setDriftSmoothness(kNewSmoothness);

        REQUIRE(changed.getDriftDepthCents() == kNewDepthCents);
        REQUIRE(changed.getDriftSmoothness() == kNewSmoothness);
        REQUIRE(control.getDriftDepthCents() == kInitialDepthCents);

        float worstMutationDelta = 0.0f;
        std::size_t worstMutationIndex = 0;
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            const float delta = std::abs(changed.getMutationLaneValue(i) - mutationBefore[i]);
            if (delta > worstMutationDelta) {
                worstMutationDelta = delta;
                worstMutationIndex = i;
            }
        }
        INFO("mutation lane " << worstMutationIndex << " moved by " << worstMutationDelta
                              << " across the two drift setter calls");
        REQUIRE(worstMutationDelta == 0.0f);

        // ---- Post-change render, both clouds in lockstep.
        std::array<double, HarmonicCloud::kMaxPartials> maxAbsChanged{};
        std::array<double, HarmonicCloud::kMaxPartials> maxAbsControl{};
        std::array<double, HarmonicCloud::kMaxPartials> maxLaneDivergence{};
        const double depthRatio =
            static_cast<double>(kInitialDepthCents) / static_cast<double>(kNewDepthCents);

        float worstMutationTrajectoryDelta = 0.0f;
        std::size_t worstMutationTrajectoryIndex = 0;
        float largestMutationValue = 0.0f;

        for (std::size_t block = 0; block < kBlocksAfter; ++block) {
            renderOneBlock(changed);
            renderOneBlock(control);

            for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
                const double centsChanged = driftDetuneCents(changed.getPartialDriftDetune(i));
                const double centsControl = driftDetuneCents(control.getPartialDriftDetune(i));

                maxAbsChanged[i] = std::max(maxAbsChanged[i], std::abs(centsChanged));
                maxAbsControl[i] = std::max(maxAbsControl[i], std::abs(centsControl));

                // Divide out the depth change: what is left is the smoothness
                // change, which must be visible on EVERY lane.
                maxLaneDivergence[i] = std::max(
                    maxLaneDivergence[i], std::abs((centsChanged * depthRatio) - centsControl));

                const float mutationValue = changed.getMutationLaneValue(i);
                const float mutationDelta =
                    std::abs(mutationValue - control.getMutationLaneValue(i));
                if (mutationDelta > worstMutationTrajectoryDelta) {
                    worstMutationTrajectoryDelta = mutationDelta;
                    worstMutationTrajectoryIndex = i;
                }
                largestMutationValue = std::max(largestMutationValue, std::abs(mutationValue));
            }
        }

        // ---- The new bound holds for ALL 64 partials, and every one of them is
        // still alive under it.
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("partial " << i << ": max |detune| = " << maxAbsChanged[i]
                            << " cent against the new " << kNewDepthCents << "-cent bound");
            REQUIRE(maxAbsChanged[i] <= static_cast<double>(kNewDepthCents)
                                            + kEstimatorToleranceCents);
            REQUIRE(maxAbsChanged[i] > kLivenessCents);
        }

        // Non-vacuity for the bound: at the OLD depth the control exceeds the new
        // bound, so the assertion above is a restriction rather than a truism.
        double controlMax = 0.0;
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            controlMax = std::max(controlMax, maxAbsControl[i]);
        }
        INFO("control cloud (still at " << kInitialDepthCents
                                        << " cent) reached " << controlMax << " cent");
        REQUIRE(controlMax > static_cast<double>(kNewDepthCents) + kEstimatorToleranceCents);

        // ---- FR-035's "every instance of that bank": the smoothness change must
        // be observable on all 64 detune lanes, not on a subset.
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("partial " << i << ": divergence from the control after dividing out the depth "
                            << "change = " << maxLaneDivergence[i] << " cent, own excursion = "
                            << maxAbsControl[i] << " cent");
            REQUIRE(maxAbsControl[i] > 0.0);
            REQUIRE(maxLaneDivergence[i] >= kMinLaneDivergenceFraction * maxAbsControl[i]);
        }

        // ---- Neither setter altered the mutation bank's trajectory.
        INFO("largest |mutation lane value| over the run = "
             << largestMutationValue << ", worst trajectory divergence = "
             << worstMutationTrajectoryDelta << " (lane " << worstMutationTrajectoryIndex << ")");
        REQUIRE(largestMutationValue > 0.01f);  // non-vacuity: the bank is running
        REQUIRE(worstMutationTrajectoryDelta == 0.0f);
    }
}

// =============================================================================
// SC-016 — Mutation bounds, level stability, independence from Drift
// =============================================================================
// FR-071 … FR-074. The weight is sampled as the RATIO of FR-008's two amplitude
// accessors, `getPartialTargetAmplitude(i) / getPartialUnmutatedTargetAmplitude(i)`
// — which is `w_i * env_i` by FR-017's composition chain, hence the settle region
// below: with the gate HELD past the documented attack time, `env_i` is exactly 1
// and the ratio is exactly `w_i`.
namespace {

/// SC-016's measurement grid.
///
/// 480 samples is deliberately NOT a whole number of 64-sample control chunks
/// (480 = 7.5 chunks), so the weight is read at a rotating chunk phase instead of
/// always on a chunk boundary — a mutation weight that were somehow keyed to the
/// caller's block size could not hide behind an aligned schedule. 100 such blocks
/// are exactly 48000 samples, i.e. exactly the 1 s window SC-016 names.
constexpr double kMutationSampleRate = 48000.0;
constexpr std::size_t kMutationBlockSize = 480;
constexpr std::size_t kMutationBlocksPerSecond = 100;
constexpr std::size_t kMutationNumBlocks = 6000;  // 60 s at 48 kHz

/// Settle region skipped before any sampling: 1 s, twenty times the 50 ms default
/// attack (`kMinAttackSec`) with the gate held, so every partial's envelope has
/// reached and is holding 1 and the sampled ratio is `w_i` alone.
constexpr std::size_t kMutationSettleBlocks = 100;

static_assert(kMutationBlockSize * kMutationBlocksPerSecond == 48000,
              "100 blocks is exactly one second at 48 kHz");
static_assert(kMutationBlockSize % HarmonicCloud::kControlChunkSamples != 0,
              "480 is deliberately NOT a multiple of kControlChunkSamples = 64");
static_assert(kMutationNumBlocks > kMutationSettleBlocks, "something must be measured");

/// One 60 s mutation measurement. Every series is sampled ONCE PER BLOCK, so
/// index `b` is the same instant in every run of the same length.
struct MutationRun {
    std::array<double, HarmonicCloud::kMaxPartials> minWeight{};
    std::array<double, HarmonicCloud::kMaxPartials> maxWeight{};
    std::vector<double> windowRms;     ///< one entry per whole 1 s window
    std::vector<double> weightSeries;  ///< lane-major, kMaxPartials x measuredBlocks
    std::vector<double> detuneSeries;  ///< lane-major, same shape, in cents
    double overallRms = 0.0;
    double worstBlockDeltaWeight = 0.0;
    std::size_t worstDeltaPartial = 0;
    double maxAbsDetuneCents = 0.0;
    std::size_t activeCount = 0;
    std::size_t measuredBlocks = 0;
    bool everyWeightExactlyOne = true;
    bool everyReferencePositive = true;
};

/// Pearson r of two equal-length series.
///
/// A constant series returns 0 rather than a NaN: "did anything move at all" is
/// asserted separately and explicitly, never smuggled in as a small correlation.
[[nodiscard]] double pearsonCorrelation(const double* a, const double* b,
                                        std::size_t n) noexcept {
    if (a == nullptr || b == nullptr || n < 2) {
        return 0.0;
    }
    double meanA = 0.0;
    double meanB = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        meanA += a[k];
        meanB += b[k];
    }
    meanA /= static_cast<double>(n);
    meanB /= static_cast<double>(n);

    double sab = 0.0;
    double saa = 0.0;
    double sbb = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        const double da = a[k] - meanA;
        const double db = b[k] - meanB;
        sab += da * db;
        saa += da * da;
        sbb += db * db;
    }
    if (saa <= 0.0 || sbb <= 0.0) {
        return 0.0;
    }
    return sab / std::sqrt(saa * sbb);
}

/// Level difference in dB. A non-positive RMS returns a value that fails every
/// bound below on purpose — silence must never read as "0 dB of deviation".
[[nodiscard]] double rmsRatioDb(double rms, double reference) noexcept {
    if (rms <= 0.0 || reference <= 0.0) {
        return 1.0e30;
    }
    return 20.0 * std::log10(rms / reference);
}

/// Render 60 s and measure everything SC-016 asks about.
///
/// The detune bank is driven at smoothness 0 for the same reason SC-015 clause 1
/// uses `kMeasurementSmoothness`: at the default 0.5 its correlation time is
/// ~15.1 s, so a 60 s series holds only ~4 independent samples and the SAMPLE
/// Pearson correlation of two genuinely independent lanes would scatter by ~0.5 —
/// wider than the 0.3 limit, on a correct implementation. At smoothness 0 the
/// detune lane's tau is 0.2 s, the estimator's own scatter drops to ~0.08, and the
/// limit discriminates a shared bank (r ~ 1) instead of describing noise. The
/// MUTATION bank keeps `kMutationSmoothness` throughout — no setter can move it.
///
/// @param seed       Cloud seed
/// @param mutation   Mutation control in [0, 1]
/// @param driftCents Detune drift depth in cents
/// @param keepSeries Retain the per-partial weight/detune series (6 MB) for the
///                   correlation clause
[[nodiscard]] MutationRun measureMutation(std::uint32_t seed, float mutation, float driftCents,
                                          bool keepSeries) {
    MutationRun run;
    run.minWeight.fill(2.0);   // above every legal weight
    run.maxWeight.fill(-1.0);  // below every legal weight
    run.measuredBlocks = kMutationNumBlocks - kMutationSettleBlocks;

    HarmonicCloud cloud;
    cloud.prepare(kMutationSampleRate);
    cloud.setSeed(seed);
    cloud.setDriftSmoothness(0.0f);
    cloud.setDriftDepthCents(driftCents);
    cloud.setMutation(mutation);
    cloud.noteOn();

    run.activeCount = cloud.getActivePartialCount();

    if (keepSeries) {
        run.weightSeries.assign(HarmonicCloud::kMaxPartials * run.measuredBlocks, 0.0);
        run.detuneSeries.assign(HarmonicCloud::kMaxPartials * run.measuredBlocks, 0.0);
    }

    std::vector<float> left(kMutationBlockSize, 0.0f);
    std::vector<float> right(kMutationBlockSize, 0.0f);

    std::array<double, HarmonicCloud::kMaxPartials> previousWeight{};
    bool havePrevious = false;

    double windowSumSquares = 0.0;
    std::size_t blocksInWindow = 0;
    double totalSumSquares = 0.0;
    std::size_t wholeWindows = 0;

    const auto samplesPerWindow =
        static_cast<double>(2 * kMutationBlockSize * kMutationBlocksPerSecond);

    for (std::size_t block = 0; block < kMutationNumBlocks; ++block) {
        cloud.processStereoBlock(left.data(), right.data(), kMutationBlockSize);
        if (block < kMutationSettleBlocks) {
            continue;
        }
        const std::size_t b = block - kMutationSettleBlocks;

        // ---- Level: both channels, accumulated in double, grouped into 1 s windows.
        for (std::size_t s = 0; s < kMutationBlockSize; ++s) {
            const double l = static_cast<double>(left[s]);
            const double r = static_cast<double>(right[s]);
            windowSumSquares += (l * l) + (r * r);
        }
        ++blocksInWindow;
        if (blocksInWindow == kMutationBlocksPerSecond) {
            run.windowRms.push_back(std::sqrt(windowSumSquares / samplesPerWindow));
            totalSumSquares += windowSumSquares;
            ++wholeWindows;
            windowSumSquares = 0.0;
            blocksInWindow = 0;
        }

        // ---- Weights, per partial.
        for (std::size_t i = 0; i < run.activeCount; ++i) {
            const float target = cloud.getPartialTargetAmplitude(i);
            const float reference = cloud.getPartialUnmutatedTargetAmplitude(i);
            if (!(reference > 0.0f)) {
                run.everyReferencePositive = false;
                continue;
            }

            // x/x is exactly 1.0 for every finite non-zero x, so "exactly 1.0" is a
            // real bit-level assertion here and not a tolerance in disguise.
            const double weight = static_cast<double>(target) / static_cast<double>(reference);
            if (weight != 1.0) {
                run.everyWeightExactlyOne = false;
            }
            run.minWeight[i] = std::min(run.minWeight[i], weight);
            run.maxWeight[i] = std::max(run.maxWeight[i], weight);

            if (havePrevious) {
                const double delta = std::abs(weight - previousWeight[i]);
                if (delta > run.worstBlockDeltaWeight) {
                    run.worstBlockDeltaWeight = delta;
                    run.worstDeltaPartial = i;
                }
            }
            previousWeight[i] = weight;

            const double cents = driftDetuneCents(cloud.getPartialDriftDetune(i));
            run.maxAbsDetuneCents = std::max(run.maxAbsDetuneCents, std::abs(cents));

            if (keepSeries) {
                run.weightSeries[(i * run.measuredBlocks) + b] = weight;
                run.detuneSeries[(i * run.measuredBlocks) + b] = cents;
            }
        }
        havePrevious = true;
    }

    if (wholeWindows > 0) {
        run.overallRms =
            std::sqrt(totalSumSquares / (samplesPerWindow * static_cast<double>(wholeWindows)));
    }
    return run;
}

}  // namespace

TEST_CASE("HarmonicCloud_MutationStaysBoundedAndLevelStable") {
    /// FR-073's exact bounds: `w = 1 + m * kMaxMutationDepth * d` with
    /// kMaxMutationDepth = 0.75 and |d| <= 1 by the lane clamp.
    constexpr double kMinLegalWeight = 0.25;
    constexpr double kMaxLegalWeight = 1.75;
    static_assert(HarmonicCloud::kMaxMutationDepth == 0.75f,
                  "the bounds above are its consequence");

    /// The observable is not `w`. There is no getter for it: what is read back is
    /// `getPartialTargetAmplitude(i) / getPartialUnmutatedTargetAmplitude(i)`, and the
    /// numerator is the FLOAT product `unmutatedTarget_[i] * w * env` — so the measured
    /// quotient carries one float rounding of `w` that the analytic bound above cannot
    /// express. MEASURED worst excess at Mutation 1.0: the quotient came out
    /// 1.7500001026 against 1.75, an excess of 5.86e-8 relative = exactly 0.5 ULP of a
    /// float. Two ULP is allowed here, and that allowance is a property of the
    /// MEASUREMENT, not a relaxation of FR-073: `w` itself is bounded by construction,
    /// `1 + m * kMaxMutationDepth * dm` with `m <= 1` and `|dm| <= 1` from the lane
    /// clamp, so no float rounding can put the value the component computes outside
    /// [0.25, 1.75] by more than the rounding of the product itself.
    constexpr double kFloatEpsilon = 1.1920928955078125e-07;
    constexpr double kWeightMeasurementSlack = 2.0 * kFloatEpsilon;
    constexpr double kMinMeasuredWeight = kMinLegalWeight * (1.0 - kWeightMeasurementSlack);
    constexpr double kMaxMeasuredWeight = kMaxLegalWeight * (1.0 + kWeightMeasurementSlack);

    /// FR-073's level bound over any 1 s window.
    constexpr double kLevelToleranceDb = 3.0;

    /// FR-071's rate bound, 2 * kMaxMutationDepth / 0.150 s ~ 10 s^-1, expressed
    /// per block. It has teeth against re-weighting driven by per-block white noise
    /// (which would move by ~0.5 per block) or by the raw walk without its output
    /// smoother; it is NOT the analytic worst case of the smoother itself, because
    /// the OU target it chases moves far slower than the smoother settles.
    constexpr double kMaxWeightRatePerSecond = 10.0;
    constexpr double kBlockDurationSeconds =
        static_cast<double>(kMutationBlockSize) / kMutationSampleRate;
    constexpr double kMaxBlockDeltaWeight = kMaxWeightRatePerSecond * kBlockDurationSeconds;

    /// SC-016's independence clause 1: "max |w_i - 1| >= 0.1 for at least half the
    /// active partials".
    constexpr double kMovingWeightThreshold = 0.1;

    /// SC-016's per-partial correlation limit. Distinct seeds give ~0; a shared
    /// instance gives ~1.
    constexpr double kCorrelationLimit = 0.3;

    /// Non-vacuity floor for "the detune is moving", far above the ~2e-4 cent noise
    /// floor of `driftDetuneCents` and far below the 50-cent depth.
    constexpr double kDetuneLivenessCents = 1.0;

    /// Counts the partials whose weight strayed at least kMovingWeightThreshold
    /// from unity anywhere in the run.
    const auto countMovingPartials = [&](const MutationRun& run) -> std::size_t {
        std::size_t moving = 0;
        for (std::size_t i = 0; i < run.activeCount; ++i) {
            const double excursion =
                std::max(std::abs(run.maxWeight[i] - 1.0), std::abs(run.minWeight[i] - 1.0));
            if (excursion >= kMovingWeightThreshold) {
                ++moving;
            }
        }
        return moving;
    };

    // -------------------------------------------------------------------------
    // Clause 1 — bounds, level stability and rate at Mutation 0, 0.5 and 1.0
    // -------------------------------------------------------------------------
    SECTION("1 - bounded, level-stable and slow at Mutation 0, 0.5 and 1.0") {
        constexpr std::uint32_t kSeed = 0x5C01AE01u;

        const MutationRun quiet =
            measureMutation(kSeed, 0.0f, HarmonicCloud::kMaxDriftCents, false);
        const MutationRun half =
            measureMutation(kSeed, 0.5f, HarmonicCloud::kMaxDriftCents, false);
        const MutationRun full =
            measureMutation(kSeed, 1.0f, HarmonicCloud::kMaxDriftCents, true);

        // Every partial index must be live and every reference amplitude positive,
        // or the ratio below would not be a weight at all.
        REQUIRE(quiet.activeCount == HarmonicCloud::kMaxPartials);
        REQUIRE(quiet.everyReferencePositive);
        REQUIRE(half.everyReferencePositive);
        REQUIRE(full.everyReferencePositive);
        REQUIRE(quiet.measuredBlocks == kMutationNumBlocks - kMutationSettleBlocks);
        REQUIRE(quiet.windowRms.size()
                == (kMutationNumBlocks - kMutationSettleBlocks) / kMutationBlocksPerSecond);

        // ---- Mutation 0: EXACTLY 1.0, every partial, every block.
        INFO("Mutation 0: every sampled weight must be exactly 1.0f");
        REQUIRE(quiet.everyWeightExactlyOne);
        REQUIRE(quiet.worstBlockDeltaWeight == 0.0);

        // ...and the two mutated runs must NOT be, or everything below is vacuous.
        REQUIRE_FALSE(half.everyWeightExactlyOne);
        REQUIRE_FALSE(full.everyWeightExactlyOne);

        // ---- The Mutation-0 reference level.
        INFO("Mutation-0 reference RMS = " << quiet.overallRms);
        REQUIRE(quiet.overallRms > 0.0);

        const auto checkBoundsLevelAndRate = [&](const MutationRun& run, const char* label) {
            for (std::size_t i = 0; i < run.activeCount; ++i) {
                INFO(label << ": partial " << i << " weight range [" << run.minWeight[i] << ", "
                           << run.maxWeight[i] << "]");
                REQUIRE(run.minWeight[i] >= kMinMeasuredWeight);
                REQUIRE(run.maxWeight[i] <= kMaxMeasuredWeight);
            }

            double worstDb = 0.0;
            std::size_t worstWindow = 0;
            for (std::size_t w = 0; w < run.windowRms.size(); ++w) {
                const double db = rmsRatioDb(run.windowRms[w], quiet.overallRms);
                if (std::abs(db) > std::abs(worstDb)) {
                    worstDb = db;
                    worstWindow = w;
                }
            }
            INFO(label << ": worst 1 s window level = " << worstDb << " dB (window " << worstWindow
                       << " of " << run.windowRms.size() << ") against the Mutation-0 RMS");
            REQUIRE(std::abs(worstDb) <= kLevelToleranceDb);

            INFO(label << ": worst per-block |dw| = " << run.worstBlockDeltaWeight << " (partial "
                       << run.worstDeltaPartial << "), bound = " << kMaxBlockDeltaWeight);
            REQUIRE(run.worstBlockDeltaWeight <= kMaxBlockDeltaWeight);
        };

        checkBoundsLevelAndRate(quiet, "Mutation 0");
        checkBoundsLevelAndRate(half, "Mutation 0.5");
        checkBoundsLevelAndRate(full, "Mutation 1.0");

        // ---- Liveness at full Mutation, and the monotone "more control moves more".
        const std::size_t movingFull = countMovingPartials(full);
        const std::size_t movingHalf = countMovingPartials(half);
        INFO("partials whose weight strayed >= " << kMovingWeightThreshold
                                                 << " from unity: Mutation 1.0 -> " << movingFull
                                                 << ", Mutation 0.5 -> " << movingHalf << " of "
                                                 << full.activeCount);
        REQUIRE(2 * movingFull >= full.activeCount);
        REQUIRE(movingFull >= movingHalf);

        // ---- Per-partial Pearson |r| between the weight series and that partial's
        // OWN detune series. A shared drift instance measures ~1 here.
        REQUIRE(full.weightSeries.size() == HarmonicCloud::kMaxPartials * full.measuredBlocks);
        INFO("largest |detune| over the run = " << full.maxAbsDetuneCents << " cent");
        REQUIRE(full.maxAbsDetuneCents > kDetuneLivenessCents);

        double worstAbsR = 0.0;
        double sumAbsR = 0.0;
        std::size_t worstIndex = 0;
        for (std::size_t i = 0; i < full.activeCount; ++i) {
            const double r =
                pearsonCorrelation(full.weightSeries.data() + (i * full.measuredBlocks),
                                   full.detuneSeries.data() + (i * full.measuredBlocks),
                                   full.measuredBlocks);
            const double absR = std::abs(r);
            sumAbsR += absR;
            if (absR > worstAbsR) {
                worstAbsR = absR;
                worstIndex = i;
            }
        }
        const double meanAbsR = sumAbsR / static_cast<double>(full.activeCount);
        INFO("weight-vs-own-detune correlation: mean |r| = " << meanAbsR << ", worst |r| = "
                                                             << worstAbsR << " (partial "
                                                             << worstIndex << ")");
        REQUIRE(worstAbsR <= kCorrelationLimit);
    }

    // -------------------------------------------------------------------------
    // Clause 2 — drift depth 0, Mutation 1.0: the weights must still move
    // -------------------------------------------------------------------------
    // FR-072's headline consequence. A shared-instance implementation — one drift
    // bank feeding both the detune and the weight — freezes every weight at exactly
    // 1.0 here, because the DEPTH control it would have to scale by is 0.
    SECTION("2 - drift depth 0 does not disable Mutation") {
        constexpr std::uint32_t kSeed = 0x5C01AF02u;

        const MutationRun quiet = measureMutation(kSeed, 0.0f, 0.0f, false);
        const MutationRun full = measureMutation(kSeed, 1.0f, 0.0f, false);

        REQUIRE(full.activeCount == HarmonicCloud::kMaxPartials);
        REQUIRE(full.everyReferencePositive);
        REQUIRE(quiet.overallRms > 0.0);

        // The drift path really is switched off — exactly, per SC-015 clause 3(a).
        REQUIRE(quiet.maxAbsDetuneCents == 0.0);
        REQUIRE(full.maxAbsDetuneCents == 0.0);

        // ...and the weights move anyway.
        const std::size_t moving = countMovingPartials(full);
        INFO("drift depth 0, Mutation 1.0: " << moving << " of " << full.activeCount
                                             << " partials strayed >= " << kMovingWeightThreshold
                                             << " from unity");
        REQUIRE_FALSE(full.everyWeightExactlyOne);
        REQUIRE(2 * moving >= full.activeCount);

        // Still bounded, still level-stable against this configuration's own
        // Mutation-0 reference.
        for (std::size_t i = 0; i < full.activeCount; ++i) {
            INFO("partial " << i << " weight range [" << full.minWeight[i] << ", "
                            << full.maxWeight[i] << "]");
            REQUIRE(full.minWeight[i] >= kMinMeasuredWeight);
            REQUIRE(full.maxWeight[i] <= kMaxMeasuredWeight);
        }

        double worstDb = 0.0;
        for (const double windowRms : full.windowRms) {
            const double db = rmsRatioDb(windowRms, quiet.overallRms);
            if (std::abs(db) > std::abs(worstDb)) {
                worstDb = db;
            }
        }
        INFO("drift depth 0, Mutation 1.0: worst 1 s window level = " << worstDb
                                                                     << " dB against its own "
                                                                        "Mutation-0 RMS");
        REQUIRE(std::abs(worstDb) <= kLevelToleranceDb);

        INFO("worst per-block |dw| = " << full.worstBlockDeltaWeight << " (partial "
                                       << full.worstDeltaPartial
                                       << "), bound = " << kMaxBlockDeltaWeight);
        REQUIRE(full.worstBlockDeltaWeight <= kMaxBlockDeltaWeight);
    }

    // -------------------------------------------------------------------------
    // Clause 3 — drift depth max, Mutation 0: no leakage into the amplitude path
    // -------------------------------------------------------------------------
    SECTION("3 - a moving detune bank never reaches the amplitude path") {
        constexpr std::uint32_t kSeed = 0x5C01B003u;

        const MutationRun run =
            measureMutation(kSeed, 0.0f, HarmonicCloud::kMaxDriftCents, false);

        REQUIRE(run.activeCount == HarmonicCloud::kMaxPartials);
        REQUIRE(run.everyReferencePositive);

        // Non-vacuity: the drift bank IS moving while the weights stand still.
        INFO("largest |detune| over the run = " << run.maxAbsDetuneCents << " cent");
        REQUIRE(run.maxAbsDetuneCents > kDetuneLivenessCents);

        REQUIRE(run.everyWeightExactlyOne);
        REQUIRE(run.worstBlockDeltaWeight == 0.0);
        for (std::size_t i = 0; i < run.activeCount; ++i) {
            INFO("partial " << i << " weight range [" << run.minWeight[i] << ", "
                            << run.maxWeight[i] << "]");
            REQUIRE(run.minWeight[i] == 1.0);
            REQUIRE(run.maxWeight[i] == 1.0);
        }
    }
}

// =============================================================================
// SC-013 — per-partial envelope offsets stagger onset and release (FR-023)
// =============================================================================
// The envelope under test is FR-023's LINEAR AR (Clarifications Q3): a linear
// rise to 1 over `attack + attackOffset_i` (the attack time is the time-to-100 %,
// not a time constant), a hold at 1 while the gate is on, and a linear fall to 0
// over `decay + decayOffset_i` after gate-off. DECAY IS THE RELEASE — there is no
// separate release stage and no sustain level.
//
// @par The reference every clause is phrased against
// @code
//   steadyStateTarget(i) = getPartialUnmutatedTargetAmplitude(i)
//                        * getPartialAntiAliasGain(i)
// @endcode
// It is deliberately NOT `getPartialTargetAmplitude(i)`, which is
// `unmutated * w_i * env_i` and therefore MOVES WITH THE ENVELOPE. Using it would
// fire the 50 % crossing at `env ~ 2x` the FR-014 smoother lag instead of at half
// the sounding level, and would make clause 4 degenerate: after gate-off `env -> 0`
// drives that target to 0, so "<= 1 % of its target" would be unsatisfiable.
// The `antiAliasGain` factor is required because the kernel's steady state is
// `currentAmplitude -> targetAmplitude * antiAliasGain`
// (`harmonic_oscillator_bank_simd.cpp:91-93`); at this configuration (f0 = 110 Hz,
// 64 partials, top partial 7.04 kHz) the MCF magnitude correction alone is ~0.895,
// so omitting it would fail a correct implementation by 10 %.
//
// @par Why the thresholds are absolute
// A relative form ("the max-spread stagger must exceed 5x the zero-spread
// stagger") is degenerate: at offset spread 0 every partial shares one envelope,
// so the zero-spread stagger is 0 BY DEFINITION and any multiple of it is 0.
//
// Mutation and drift are pinned at 0 so `w_i == 1` and every partial's
// steady-state target is static for the whole run — the crossing marks below are
// only well defined against a target that does not move.
// =============================================================================
namespace {

constexpr double kSc013SampleRate = 48000.0;
constexpr std::size_t kSc013BlockSize = 512;

/// Sampling resolution of every time measured below: 512 / 48 kHz = 10.667 ms.
/// Clause 1's threshold is stated in exactly this unit ("<= 1 block").
constexpr double kSc013BlockSeconds =
    static_cast<double>(kSc013BlockSize) / kSc013SampleRate;

/// The SHORTEST supported attack, i.e. the tightest case for clause 3: the FR-014
/// smoother lags a linear ramp by one time constant, so the observed amplitude at
/// `t = attack_i` is `1 - kAmpSmoothTimeSec / attack_i` = 0.960 here, against the
/// 95 % bar. Any longer attack only widens that margin.
constexpr float kSc013AttackSec = HarmonicCloud::kMinAttackSec;  // 0.05 s
constexpr float kSc013DecaySec = 0.5f;

/// 3.2 s gated and 3.2 s released. Both outlast the longest per-partial time this
/// configuration can produce at maximum offset spread.
constexpr std::size_t kSc013GatedBlocks = 300;
constexpr std::size_t kSc013ReleaseBlocks = 300;

static_assert(static_cast<double>(kSc013GatedBlocks * kSc013BlockSize) / kSc013SampleRate
                  > static_cast<double>(kSc013AttackSec)
                        + static_cast<double>(HarmonicCloud::kMaxEnvOffsetSec),
              "the gated phase must outlast the longest possible attack");
static_assert(static_cast<double>(kSc013ReleaseBlocks * kSc013BlockSize) / kSc013SampleRate
                  > static_cast<double>(kSc013DecaySec)
                        + static_cast<double>(HarmonicCloud::kMaxEnvOffsetSec)
                        + (5.0 * static_cast<double>(HarmonicCloud::kAmpSmoothTimeSec)),
              "the released phase must outlast the longest possible decay");
static_assert(kSc013BlockSize % HarmonicCloud::kControlChunkSamples == 0,
              "a whole number of control chunks per block keeps the envelope grid regular");

/// Sentinel for "this partial never reached 50 % of its steady-state target".
constexpr std::size_t kSc013NoCrossing = static_cast<std::size_t>(-1);

/// One SC-013 measurement: `getPartialCurrentAmplitude(i)` sampled once per
/// 512-sample block, first with the gate on and then after `noteOff()`.
struct EnvelopeRun {
    std::array<double, HarmonicCloud::kMaxPartials> steadyTarget{};
    std::array<double, HarmonicCloud::kMaxPartials> attackOffsetSec{};
    std::array<double, HarmonicCloud::kMaxPartials> decayOffsetSec{};
    std::vector<double> gated;     ///< partial-major, kMaxPartials x kSc013GatedBlocks
    std::vector<double> released;  ///< partial-major, kMaxPartials x kSc013ReleaseBlocks
    std::size_t activeCount = 0;
    bool everyTargetPositive = true;
};

[[nodiscard]] EnvelopeRun measureEnvelope(std::uint32_t seed, float offsetSpread) {
    EnvelopeRun run;
    run.gated.assign(HarmonicCloud::kMaxPartials * kSc013GatedBlocks, 0.0);
    run.released.assign(HarmonicCloud::kMaxPartials * kSc013ReleaseBlocks, 0.0);

    HarmonicCloud cloud;
    cloud.prepare(kSc013SampleRate);
    cloud.setSeed(seed);
    cloud.setFundamentalHz(110.0f);
    cloud.setRichness(1.0f);  // N(1) = 64 active partials
    cloud.setInharmonicity(0.0f);
    cloud.setSpectralGravity(0.0f);
    cloud.setSpectralTiltDb(0.0f);
    cloud.setStereoSpread(0.0f);
    cloud.setMutation(0.0f);         // w_i == 1
    cloud.setDriftDepthCents(0.0f);  // static per-partial frequencies and gains
    cloud.setAttackTimeSec(kSc013AttackSec);
    cloud.setDecayTimeSec(kSc013DecaySec);
    cloud.setEnvelopeOffsetSpread(offsetSpread);
    cloud.noteOn();

    run.activeCount = cloud.getActivePartialCount();

    std::vector<float> left(kSc013BlockSize, 0.0f);
    std::vector<float> right(kSc013BlockSize, 0.0f);

    for (std::size_t b = 0; b < kSc013GatedBlocks; ++b) {
        cloud.processStereoBlock(left.data(), right.data(), kSc013BlockSize);
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            run.gated[(i * kSc013GatedBlocks) + b] =
                static_cast<double>(cloud.getPartialCurrentAmplitude(i));
        }
    }

    // Captured with the gate STILL HELD, so `env_i == 1` and the FR-017 normalizer
    // has long settled. Neither factor depends on the envelope, so this one
    // capture is the reference for the released phase too.
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        run.steadyTarget[i] = static_cast<double>(cloud.getPartialUnmutatedTargetAmplitude(i))
                              * static_cast<double>(cloud.getPartialAntiAliasGain(i));
        run.attackOffsetSec[i] = static_cast<double>(cloud.getPartialAttackOffsetSec(i));
        run.decayOffsetSec[i] = static_cast<double>(cloud.getPartialDecayOffsetSec(i));
        if (i < run.activeCount && !(run.steadyTarget[i] > 0.0)) {
            run.everyTargetPositive = false;
        }
    }

    cloud.noteOff();

    for (std::size_t b = 0; b < kSc013ReleaseBlocks; ++b) {
        cloud.processStereoBlock(left.data(), right.data(), kSc013BlockSize);
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            run.released[(i * kSc013ReleaseBlocks) + b] =
                static_cast<double>(cloud.getPartialCurrentAmplitude(i));
        }
    }

    return run;
}

/// Index of the first block whose END instant is at or after `seconds`.
/// Block `b` is sampled after `(b + 1) * kSc013BlockSeconds` of audio.
[[nodiscard]] std::size_t blockAtOrAfter(double seconds) noexcept {
    const double blocks = std::ceil(seconds / kSc013BlockSeconds);
    return (blocks <= 1.0) ? std::size_t{0} : (static_cast<std::size_t>(blocks) - 1);
}

/// Index of the last block whose END instant is at or before `seconds`.
[[nodiscard]] std::size_t blockAtOrBefore(double seconds) noexcept {
    const double blocks = std::floor(seconds / kSc013BlockSeconds);
    return (blocks <= 1.0) ? std::size_t{0} : (static_cast<std::size_t>(blocks) - 1);
}

/// First block in which partial `partial` reached 50 % of its steady-state target.
[[nodiscard]] std::size_t crossingBlock(const EnvelopeRun& run, std::size_t partial) noexcept {
    const double threshold = 0.5 * run.steadyTarget[partial];
    for (std::size_t b = 0; b < kSc013GatedBlocks; ++b) {
        if (run.gated[(partial * kSc013GatedBlocks) + b] >= threshold) {
            return b;
        }
    }
    return kSc013NoCrossing;
}

}  // namespace

TEST_CASE("HarmonicCloud_PartialEnvelopeOffsetsStagger") {
    /// Pinned so the per-seed offset draws the stagger is made of are the ones the
    /// figures quoted below were measured against.
    constexpr std::uint32_t kSeed = 0x5E13A013u;

    /// Clause 2's absolute floor and ceiling. At spread 1 the expected stagger is
    /// `0.5 * kMaxEnvOffsetSec * (max oa - min oa) ~ 0.97 s` — the 0.5 because the
    /// 50 % mark of a linear ramp of length T falls at T/2.
    constexpr double kMinStaggerSeconds = 0.100;
    constexpr double kMaxStaggerSeconds = static_cast<double>(HarmonicCloud::kMaxEnvOffsetSec);

    /// Clause 3's bar.
    constexpr double kAttackReachedFraction = 0.95;

    /// Clause 4's bar, and the deadline slack it is measured at.
    constexpr double kReleasedFraction = 0.01;
    constexpr double kSmootherSettleSeconds =
        5.0 * static_cast<double>(HarmonicCloud::kAmpSmoothTimeSec);

    /// Clause 4's other half — "nothing is cut off before its decay time elapses".
    /// A linear release from 1.0 stands at exactly 0.5 after half its decay time
    /// (less the ~0.4 % FR-014 smoother lag), so 0.30 rejects a partial that snaps
    /// to zero or is retired early without pinning the segment shape.
    constexpr double kHalfDecayFraction = 0.30;

    const EnvelopeRun flat = measureEnvelope(kSeed, 0.0f);
    const EnvelopeRun spread = measureEnvelope(kSeed, 1.0f);

    // Non-vacuity: both runs must have all 64 partials sounding at a positive
    // steady-state level, or every ratio below is meaningless.
    REQUIRE(flat.activeCount == HarmonicCloud::kMaxPartials);
    REQUIRE(spread.activeCount == HarmonicCloud::kMaxPartials);
    REQUIRE(flat.everyTargetPositive);
    REQUIRE(spread.everyTargetPositive);

    // The offset spread really is the only thing that differs between the runs.
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        INFO("partial " << i << ": flat offsets (" << flat.attackOffsetSec[i] << ", "
                        << flat.decayOffsetSec[i] << "), spread offsets ("
                        << spread.attackOffsetSec[i] << ", " << spread.decayOffsetSec[i] << ")");
        REQUIRE(flat.attackOffsetSec[i] == 0.0);
        REQUIRE(flat.decayOffsetSec[i] == 0.0);
        REQUIRE(spread.attackOffsetSec[i] >= 0.0);
        REQUIRE(spread.attackOffsetSec[i] <= static_cast<double>(HarmonicCloud::kMaxEnvOffsetSec));
        REQUIRE(spread.decayOffsetSec[i] >= 0.0);
        REQUIRE(spread.decayOffsetSec[i] <= static_cast<double>(HarmonicCloud::kMaxEnvOffsetSec));
    }

    // -------------------------------------------------------------------------
    // Clause 1 — offset spread 0: every partial crosses within one block
    // -------------------------------------------------------------------------
    // At spread 0 every partial shares one envelope, and the FR-014 smoother is a
    // single scalar coefficient, so every partial's amplitude trajectory is the
    // SAME shape scaled by its own steady-state target. Measured against a
    // per-partial 50 % mark, they must therefore cross together.
    std::size_t flatMin = kSc013GatedBlocks;
    std::size_t flatMax = 0;
    for (std::size_t i = 0; i < flat.activeCount; ++i) {
        const std::size_t b = crossingBlock(flat, i);
        INFO("spread 0, partial " << i << ": crossing block " << b << " of " << kSc013GatedBlocks);
        REQUIRE(b != kSc013NoCrossing);
        flatMin = std::min(flatMin, b);
        flatMax = std::max(flatMax, b);
    }
    INFO("spread 0: crossing blocks span [" << flatMin << ", " << flatMax << "], i.e. "
                                            << (static_cast<double>(flatMax - flatMin)
                                                * kSc013BlockSeconds)
                                            << " s");
    REQUIRE(flatMax >= flatMin);
    REQUIRE(flatMax - flatMin <= std::size_t{1});

    // -------------------------------------------------------------------------
    // Clause 2 — maximum offset spread: >= 100 ms and <= kMaxEnvOffsetSec
    // -------------------------------------------------------------------------
    std::size_t spreadMin = kSc013GatedBlocks;
    std::size_t spreadMax = 0;
    for (std::size_t i = 0; i < spread.activeCount; ++i) {
        const std::size_t b = crossingBlock(spread, i);
        INFO("spread 1, partial " << i << ": crossing block " << b << " of " << kSc013GatedBlocks);
        REQUIRE(b != kSc013NoCrossing);
        spreadMin = std::min(spreadMin, b);
        spreadMax = std::max(spreadMax, b);
    }
    const double staggerSeconds =
        static_cast<double>(spreadMax - spreadMin) * kSc013BlockSeconds;
    INFO("spread 1: crossing blocks span [" << spreadMin << ", " << spreadMax << "], i.e. "
                                            << staggerSeconds << " s (bounds "
                                            << kMinStaggerSeconds << " .. " << kMaxStaggerSeconds
                                            << ")");
    REQUIRE(spreadMax >= spreadMin);
    REQUIRE(staggerSeconds >= kMinStaggerSeconds);
    REQUIRE(staggerSeconds <= kMaxStaggerSeconds);

    // -------------------------------------------------------------------------
    // Clause 3 — no partial is stranded, at EITHER setting
    // -------------------------------------------------------------------------
    // The deadline is that partial's OWN `attack + offset_i`, read from the
    // component rather than assumed, so a partial that quietly received someone
    // else's offset fails here instead of hiding behind a global bound.
    const auto checkNoneStranded = [&](const EnvelopeRun& run, const char* label) {
        for (std::size_t i = 0; i < run.activeCount; ++i) {
            const double deadline = static_cast<double>(kSc013AttackSec) + run.attackOffsetSec[i];
            const std::size_t b = blockAtOrAfter(deadline);
            REQUIRE(b < kSc013GatedBlocks);
            const double value = run.gated[(i * kSc013GatedBlocks) + b];
            const double fraction = value / run.steadyTarget[i];
            INFO(label << ": partial " << i << " reached " << fraction
                       << " of its steady-state target by " << deadline << " s (block " << b
                       << "), bar = " << kAttackReachedFraction);
            REQUIRE(fraction >= kAttackReachedFraction);
        }
    };
    checkNoneStranded(flat, "spread 0");
    checkNoneStranded(spread, "spread 1");

    // -------------------------------------------------------------------------
    // Clause 4 — decay IS the release: monotone, complete, and not truncated
    // -------------------------------------------------------------------------
    const auto checkRelease = [&](const EnvelopeRun& run, const char* label) {
        for (std::size_t i = 0; i < run.activeCount; ++i) {
            // (a) Monotonically non-increasing, starting from the last gated sample
            // — nothing may rise after the gate closes.
            double previous = run.gated[(i * kSc013GatedBlocks) + (kSc013GatedBlocks - 1)];
            double worstRise = 0.0;
            std::size_t worstRiseBlock = 0;
            for (std::size_t b = 0; b < kSc013ReleaseBlocks; ++b) {
                const double value = run.released[(i * kSc013ReleaseBlocks) + b];
                const double rise = value - previous;
                if (rise > worstRise) {
                    worstRise = rise;
                    worstRiseBlock = b;
                }
                previous = value;
            }
            INFO(label << ": partial " << i << " worst rise after gate-off = " << worstRise
                       << " at release block " << worstRiseBlock);
            REQUIRE(worstRise <= 0.0);

            // (b) Nothing is cut off BEFORE its decay time elapses: a linear release
            // stands at ~50 % of the steady-state level after half its decay.
            const double decaySeconds = static_cast<double>(kSc013DecaySec) + run.decayOffsetSec[i];
            const std::size_t halfBlock = blockAtOrBefore(0.5 * decaySeconds);
            REQUIRE(halfBlock < kSc013ReleaseBlocks);
            const double halfValue = run.released[(i * kSc013ReleaseBlocks) + halfBlock];
            const double halfFraction = halfValue / run.steadyTarget[i];
            INFO(label << ": partial " << i << " stood at " << halfFraction
                       << " of its steady-state target after half of its " << decaySeconds
                       << " s decay (block " << halfBlock << "), floor = " << kHalfDecayFraction);
            REQUIRE(halfFraction >= kHalfDecayFraction);

            // (c) ...and nothing sustains after gate-off.
            const double deadline = decaySeconds + kSmootherSettleSeconds;
            const std::size_t b = blockAtOrAfter(deadline);
            REQUIRE(b < kSc013ReleaseBlocks);
            const double value = run.released[(i * kSc013ReleaseBlocks) + b];
            const double fraction = value / run.steadyTarget[i];
            INFO(label << ": partial " << i << " was still at " << fraction
                       << " of its steady-state target " << deadline
                       << " s after gate-off (block " << b << "), bar = " << kReleasedFraction);
            REQUIRE(fraction <= kReleasedFraction);
        }
    };
    checkRelease(flat, "spread 0");
    checkRelease(spread, "spread 1");
}

// =============================================================================
// SC-006 retrigger clause — FR-016's quiescence rule (Clarifications Q5)
// =============================================================================
// Two paths, and BOTH are needed:
//
//   1. SOUNDING retrigger (the non-quiescent path). Differential click test, and
//      — more importantly — the MECHANISM: every partial's MCF state must be
//      BITWISE unchanged across the note-on. That is what makes the retrigger
//      click-free BY CONSTRUCTION rather than by masking: no MCF state is stepped
//      discontinuously, so there is no discontinuity for FR-013's crossfade to
//      hide (spec.md:290-296, plan §4.7).
//   2. QUIESCENT retrigger. Without it a never-redraw implementation passes case 1
//      vacuously and silently breaks SC-018's seeded-onset behaviour.
namespace {

/// Pinned render basis. `kRetrigSampleRate` MUST equal the detector config's
/// sampleRate — `ClickDetectorConfig`'s own default is 44100
/// (`artifact_detection.h:39`), which would put every reported time in the wrong
/// place while still "passing".
constexpr double kRetrigSampleRate = 48000.0;
constexpr std::size_t kRetrigBlockSize = 512;

/// ~1.003 s each side of the retrigger, so the pre-roll is 20x the 50 ms attack
/// and every partial is provably at its hold level when the second note-on lands.
constexpr std::size_t kRetrigPreBlocks = 94;
constexpr std::size_t kRetrigPostBlocks = 94;

constexpr std::uint32_t kRetrigSeed = 0x5E06A013u;
constexpr float kRetrigAttackSec = 0.05f;
constexpr float kRetrigDecaySec = 0.10f;

/// The detector's own merge gap (`artifact_detection.h:44`), needed by the
/// positive control's index window — see the comment there.
constexpr std::size_t kRetrigMergeGap = 5;


/// How far `currentAmplitude` may sit from the level the kernel is chasing
/// (`target * antiAliasGain`) once the envelope holds. The FR-014 smoother is a
/// 2 ms one-pole and the only thing still moving is the drifting anti-alias gain,
/// so 10 % is loose by an order of magnitude and exists only to reject "nothing
/// ever reached its level".
constexpr float kRetrigHoldTolerance = 0.10f;

/// Cap on the post-note-off render used to reach quiescence. The decay is 100 ms
/// with zero offset spread plus ~5 smoother time constants, so ~0.12 s; 60 blocks
/// is 0.64 s. Hitting the cap is a FAILURE, not a silent fallthrough.
constexpr std::size_t kRetrigMaxSilenceBlocks = 60;

/// Both cases render exactly this configuration.
///
/// Mutation is deliberately **0**: it is the only factor of FR-017's chain that
/// would otherwise sit between `unmutatedTargetAmplitude` and `targetAmplitude`,
/// and case 1's "every partial is at full amplitude" precondition is asserted as
/// the exact identity `target == unmutated` (w = 1.0f and env = 1.0f are both
/// exact, and a float multiply by 1.0f is exact). Mutation's own click-freeness is
/// SC-005's and SC-016's job, not this criterion's. Drift stays live, so the
/// renders are not static.
void configureRetriggerCloud(HarmonicCloud& cloud) {
    cloud.prepare(kRetrigSampleRate);
    cloud.setSeed(kRetrigSeed);
    cloud.setFundamentalHz(220.0f);
    cloud.setRichness(1.0f);           // all 64 partials active
    cloud.setSpectralTiltDb(-3.0f);
    cloud.setInharmonicity(0.0f);
    cloud.setSpectralGravity(0.0f);
    cloud.setDriftDepthCents(25.0f);   // half of kMaxDriftCents — drift is live
    cloud.setDriftSmoothness(0.5f);
    cloud.setMutation(0.0f);           // see the note above — this is load-bearing
    cloud.setStereoSpread(0.5f);
    cloud.setAttackTimeSec(kRetrigAttackSec);
    cloud.setDecayTimeSec(kRetrigDecaySec);
    cloud.setEnvelopeOffsetSpread(0.0f);  // every partial shares one envelope
}

/// Append `numBlocks` fixed-size blocks of stereo render to `left` / `right`.
void renderRetriggerBlocks(HarmonicCloud& cloud, std::vector<float>& left,
                           std::vector<float>& right, std::size_t numBlocks) {
    std::array<float, kRetrigBlockSize> blockL{};
    std::array<float, kRetrigBlockSize> blockR{};
    for (std::size_t b = 0; b < numBlocks; ++b) {
        cloud.processStereoBlock(blockL.data(), blockR.data(), kRetrigBlockSize);
        left.insert(left.end(), blockL.begin(), blockL.end());
        right.insert(right.end(), blockR.begin(), blockR.end());
    }
}

/// The PINNED detector configuration, shared by every click criterion in this
/// phase. Designated initialisers throughout — Clang rejects narrowing in brace
/// initialisation, and the field order here is the declaration order at
/// `artifact_detection.h:38-45`.
[[nodiscard]] std::vector<TestUtils::ClickDetection> detectRetriggerClicks(
    const std::vector<float>& buffer) {
    TestUtils::ClickDetectorConfig cfg{.sampleRate = static_cast<float>(kRetrigSampleRate),
                                       .frameSize = 512,
                                       .hopSize = 256,
                                       .detectionThreshold = 5.0f,
                                       .energyThresholdDb = -60.0f,
                                       .mergeGap = kRetrigMergeGap};
    TestUtils::ClickDetector detector(cfg);
    detector.prepare();
    return detector.detect(buffer.data(), buffer.size());
}

/// The largest first difference anywhere in a buffer. Shared by every click case
/// in this file (this is one anonymous namespace per TU, so it may be defined
/// exactly once) — it is both SC-005 clause 2's metric and the scale every
/// positive control's injected step is sized from.
///
/// It replaced an earlier pure-sine slew reference that could not fail: at
/// f_max = 15 kHz and fs = 48 kHz that bound is `P*2*pi*f_max/fs ~ 1.96*P`, so
/// an audible one-sample step of 0.5*P passed it (spec.md:728-729).
[[nodiscard]] float maxPerSampleDelta(const std::vector<float>& buffer) noexcept {
    float worst = 0.0f;
    for (std::size_t i = 1; i < buffer.size(); ++i) {
        worst = std::max(worst, std::abs(buffer[i] - buffer[i - 1]));
    }
    return worst;
}

/// Positive-control step size, as a multiple of the control render's OWN largest
/// first difference.
///
/// It is deliberately not a fraction of the peak. `ClickDetector` thresholds
/// `mean(|dx|) + 5*stddev(|dx|)` WITHIN each frame (`artifact_detection.h:186-193`)
/// — a first-difference statistic — so an injection scaled by peak amplitude is
/// scaled by the wrong quantity, and how detectable it is then depends entirely on
/// how bright the render happens to be. MEASURED with the old 0.10*peak rule: the
/// SC-006 sweep control (maxDelta/peak = 0.108) detected it, while the SC-005
/// zipper control (maxDelta/peak = 0.229), the SC-017 retrigger control and the
/// FR-008 mask control all reported ZERO detections — three positive controls
/// failing on renders with no defect in them. Against maxDelta the same four
/// configurations detect at 1.5x, 1.0x, 1.0x and 1.0x respectively, so 2.0x clears
/// every one of them with margin while staying a plainly audible one-sample step.
constexpr float kClickInjectDeltaFactor = 2.0f;

/// Bitwise float comparison. `==` would equate +0.0f and -0.0f; the retrigger
/// claim is that the state is UNCHANGED, so it is asserted on the bits.
[[nodiscard]] bool bitsEqual(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

/// Every partial's MCF oscillator state at one instant (FR-008's accessors).
struct PhaseSnapshot {
    std::array<float, HarmonicCloud::kMaxPartials> sinState{};
    std::array<float, HarmonicCloud::kMaxPartials> cosState{};
};

[[nodiscard]] PhaseSnapshot capturePhases(const HarmonicCloud& cloud) {
    PhaseSnapshot snapshot;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        snapshot.sinState[i] = cloud.getPartialSinState(i);
        snapshot.cosState[i] = cloud.getPartialCosState(i);
    }
    return snapshot;
}

/// Number of partials whose (sin, cos) pair differs bitwise between snapshots.
[[nodiscard]] std::size_t countChangedPhases(const PhaseSnapshot& before,
                                             const PhaseSnapshot& after) noexcept {
    std::size_t changed = 0;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        if (!bitsEqual(before.sinState[i], after.sinState[i]) ||
            !bitsEqual(before.cosState[i], after.cosState[i])) {
            ++changed;
        }
    }
    return changed;
}

}  // namespace

TEST_CASE("HarmonicCloud_RetriggerIsClickFree") {
    constexpr std::size_t kTotalBlocks = kRetrigPreBlocks + kRetrigPostBlocks;
    constexpr std::size_t kTotalSamples = kTotalBlocks * kRetrigBlockSize;

    // -------------------------------------------------------------------------
    // Frozen control render — the same configuration, no second note-on
    // -------------------------------------------------------------------------
    HarmonicCloud control;
    configureRetriggerCloud(control);
    control.noteOn();

    std::vector<float> controlLeft;
    std::vector<float> controlRight;
    controlLeft.reserve(kTotalSamples);
    controlRight.reserve(kTotalSamples);
    renderRetriggerBlocks(control, controlLeft, controlRight, kTotalBlocks);
    REQUIRE(controlLeft.size() == kTotalSamples);

    const std::vector<TestUtils::ClickDetection> controlLeftDetections =
        detectRetriggerClicks(controlLeft);
    const std::vector<TestUtils::ClickDetection> controlRightDetections =
        detectRetriggerClicks(controlRight);

    // -------------------------------------------------------------------------
    // POSITIVE CONTROL (mandatory)
    // -------------------------------------------------------------------------
    // Without it the differential criterion below cannot distinguish "no
    // artifacts" from "detector not wired up" — the same rule the repo applies to
    // render fingerprints.
    float controlPeak = 0.0f;
    for (const float sample : controlLeft) {
        controlPeak = std::max(controlPeak, std::abs(sample));
    }
    INFO("control render peak = " << controlPeak);
    REQUIRE(controlPeak > 0.0f);

    const float controlDelta = maxPerSampleDelta(controlLeft);
    INFO("control render max |dx| = " << controlDelta);
    REQUIRE(controlDelta > 0.0f);
    const float injectStep = kClickInjectDeltaFactor * controlDelta;

    std::vector<float> injected = controlLeft;
    const std::size_t injectIndex = injected.size() / 2;
    injected[injectIndex] += injectStep;
    const std::vector<TestUtils::ClickDetection> injectedDetections =
        detectRetriggerClicks(injected);

    // A one-sample step raises |dx| at BOTH injectIndex and injectIndex + 1, and
    // mergeAdjacentDetections keeps the LARGER-amplitude member of any chain of
    // detections within mergeGap (`artifact_detection.h:240-249`), so the surviving
    // index may sit anywhere within +-mergeGap of the injection. The window is that
    // chain, not a fudge factor.
    const std::size_t injectWindowLo = injectIndex - kRetrigMergeGap;
    const std::size_t injectWindowHi = injectIndex + 1 + kRetrigMergeGap;
    std::size_t detectionsAtInjection = 0;
    for (const TestUtils::ClickDetection& detection : injectedDetections) {
        if (detection.sampleIndex >= injectWindowLo && detection.sampleIndex <= injectWindowHi) {
            ++detectionsAtInjection;
        }
    }
    INFO("positive control: injected a " << injectStep << " step ("
                                         << kClickInjectDeltaFactor
                                         << " x the control's own max |dx|) at sample "
                                         << injectIndex << "; "
                                         << detectionsAtInjection << " detection(s) in ["
                                         << injectWindowLo << ", " << injectWindowHi << "], "
                                         << injectedDetections.size() << " total vs "
                                         << controlLeftDetections.size() << " in the control");
    REQUIRE(detectionsAtInjection >= 1);

    // -------------------------------------------------------------------------
    // Case 1 — SOUNDING retrigger (the non-quiescent path)
    // -------------------------------------------------------------------------
    SECTION("Sounding retrigger leaves every MCF state untouched and adds no clicks") {
        HarmonicCloud cloud;
        configureRetriggerCloud(cloud);
        cloud.noteOn();

        std::vector<float> left;
        std::vector<float> right;
        left.reserve(kTotalSamples);
        right.reserve(kTotalSamples);
        renderRetriggerBlocks(cloud, left, right, kRetrigPreBlocks);

        // Non-vacuity: this really is the NON-quiescent path, and every partial
        // really is at full amplitude when the second note-on arrives.
        REQUIRE_FALSE(cloud.isQuiescent());
        REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);

        const double preSeconds = static_cast<double>(kRetrigPreBlocks * kRetrigBlockSize)
                                  / kRetrigSampleRate;
        INFO("pre-roll = " << preSeconds << " s against an attack of " << kRetrigAttackSec << " s");
        REQUIRE(preSeconds > static_cast<double>(kRetrigAttackSec));

        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            // Offset spread is 0, so no partial has an attack deadline later than
            // the cloud-level attack time.
            REQUIRE(cloud.getPartialAttackOffsetSec(i) == 0.0f);

            // env == 1 EXACTLY: with Mutation 0 the weight is the literal 1.0f
            // (harmonic_cloud.h:1275-1277) and the hold stage pins envValue_ at
            // 1.0f, so `target` is a float multiply of `unmutated` by 1.0f twice —
            // exact. Any partial still climbing its attack fails here.
            const float target = cloud.getPartialTargetAmplitude(i);
            const float unmutated = cloud.getPartialUnmutatedTargetAmplitude(i);
            INFO("partial " << i << ": target = " << target << ", unmutated = " << unmutated);
            REQUIRE(target > 0.0f);
            REQUIRE(bitsEqual(target, unmutated));

            // ...and the audio path actually got there. The kernel chases
            // `target * antiAliasGain` (harmonic_oscillator_bank_simd.h:27).
            const float chased = target * cloud.getPartialAntiAliasGain(i);
            const float current = cloud.getPartialCurrentAmplitude(i);
            INFO("partial " << i << ": current = " << current << ", chased = " << chased);
            REQUIRE(chased > 0.0f);
            REQUIRE(std::abs(current - chased) <= kRetrigHoldTolerance * chased);
        }

        // ---- the mechanism, not just the outcome ----
        const PhaseSnapshot before = capturePhases(cloud);
        cloud.noteOn();
        const PhaseSnapshot after = capturePhases(cloud);
        const std::size_t changed = countChangedPhases(before, after);
        INFO("MCF states changed across a sounding retrigger: " << changed << " of "
                                                                << HarmonicCloud::kMaxPartials
                                                                << " (must be 0)");
        REQUIRE(changed == 0);

        renderRetriggerBlocks(cloud, left, right, kRetrigPostBlocks);
        REQUIRE(left.size() == controlLeft.size());
        REQUIRE(right.size() == controlRight.size());

        // ---- the differential click criterion, per channel ----
        const std::vector<TestUtils::ClickDetection> leftDetections = detectRetriggerClicks(left);
        INFO("left channel: retriggered = " << leftDetections.size()
                                            << " detections, control = "
                                            << controlLeftDetections.size());
        REQUIRE(leftDetections.size() <= controlLeftDetections.size());

        const std::vector<TestUtils::ClickDetection> rightDetections = detectRetriggerClicks(right);
        INFO("right channel: retriggered = " << rightDetections.size()
                                             << " detections, control = "
                                             << controlRightDetections.size());
        REQUIRE(rightDetections.size() <= controlRightDetections.size());
    }

    // -------------------------------------------------------------------------
    // Case 2 — QUIESCENT retrigger (gate off + everything below the floor)
    // -------------------------------------------------------------------------
    SECTION("Quiescent note-on redraws the phases") {
        HarmonicCloud cloud;
        configureRetriggerCloud(cloud);
        cloud.noteOn();

        std::vector<float> left;
        std::vector<float> right;
        left.reserve(kTotalSamples);
        right.reserve(kTotalSamples);
        renderRetriggerBlocks(cloud, left, right, kRetrigPreBlocks);
        REQUIRE_FALSE(cloud.isQuiescent());

        cloud.noteOff();

        // Render until every partial has decayed below the documented floor.
        const std::uint64_t readsBeforeSilence = cloud.getDriftReadCount();
        std::size_t silenceBlocks = 0;
        while (!cloud.isQuiescent() && silenceBlocks < kRetrigMaxSilenceBlocks) {
            renderRetriggerBlocks(cloud, left, right, std::size_t{1});
            ++silenceBlocks;
        }
        INFO("reached quiescence after " << silenceBlocks << " blocks (cap "
                                         << kRetrigMaxSilenceBlocks << ")");
        REQUIRE(cloud.isQuiescent());

        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("partial " << i << " current amplitude = " << cloud.getPartialCurrentAmplitude(i)
                            << ", floor = " << HarmonicCloud::kQuiescenceAmplitude);
            REQUIRE(cloud.getPartialCurrentAmplitude(i) < HarmonicCloud::kQuiescenceAmplitude);
        }

        // The §4.1 quiescent early-out: it renders exact silence, and it STILL
        // advances both lane banks and the read count, so free-running
        // life-modulation never stops (roadmap Key Design Decision 1) and SC-015's
        // read-count clause is unaffected by whether the cloud was sounding.
        const std::size_t sizeBeforeSilentBlock = left.size();
        renderRetriggerBlocks(cloud, left, right, std::size_t{1});
        ++silenceBlocks;
        for (std::size_t s = sizeBeforeSilentBlock; s < left.size(); ++s) {
            REQUIRE(left[s] == 0.0f);
            REQUIRE(right[s] == 0.0f);
        }

        constexpr std::uint64_t kChunksPerBlock =
            static_cast<std::uint64_t>(kRetrigBlockSize / HarmonicCloud::kControlChunkSamples);
        const std::uint64_t expectedReads =
            readsBeforeSilence + (kChunksPerBlock * static_cast<std::uint64_t>(silenceBlocks));
        INFO("drift reads per partial: " << cloud.getDriftReadCount() << ", expected "
                                         << expectedReads << " (" << kChunksPerBlock
                                         << " per block over " << silenceBlocks << " blocks)");
        REQUIRE(cloud.getDriftReadCount() == expectedReads);

        // ---- the redraw itself ----
        const PhaseSnapshot before = capturePhases(cloud);
        cloud.noteOn();
        const PhaseSnapshot after = capturePhases(cloud);
        const std::size_t changed = countChangedPhases(before, after);
        INFO("MCF states changed across a quiescent note-on: " << changed << " of "
                                                               << HarmonicCloud::kMaxPartials);
        // A redraw touches every partial; the bar is set at half the bank so a
        // single coincidental bit-identical draw cannot fail the case, while a
        // never-redraw implementation (which would break SC-018's seeded onset)
        // still fails it outright.
        REQUIRE(changed >= HarmonicCloud::kMaxPartials / 2);
    }
}

// =============================================================================
// SC-017 — bounded, finite output over the parameter grid; setter hygiene
// =============================================================================
// Three passes, and all three are load-bearing:
//
//   1. GRID. FR-006's "any parameter combination" is an unbounded quantifier;
//      SC-017 is what makes it finite, as a fully enumerated Cartesian product of
//      3^5 * 2^4 = 3888 cells (spec.md:979-983). Every output sample must be
//      finite BY BIT TEST — never std::isnan, which folds under the macOS leg's
//      -ffast-math — must satisfy |out| <= kOutputClamp, and stateFinite() must
//      hold at the end of every cell.
//   2. NON-FINITE REJECTION (FR-007 half 1). Every setter, fed a NaN and both
//      infinities, must leave its getter EXACTLY where it was and the subsequent
//      render indistinguishable from the pre-call render. The component's own
//      guard is the finite-first bit test of plan §4.8; this measures its effect.
//   3. FINITE OUT-OF-RANGE CLAMPING (FR-007 half 2) — the half nothing else in
//      this phase tests. Without it a setter that assigned WITHOUT std::clamp
//      passes every other assertion in this spec, including pass 2 (a finite
//      out-of-range value is not a poison) and pass 1 (a clamp-free richness of
//      2.0 still renders bounded audio).
namespace {

constexpr double kSc017SampleRate = 48000.0;
constexpr std::size_t kSc017BlockSize = 512;

/// Pass 1 renders 1 s per cell (spec.md:981). Not a multiple of the block size is
/// fine — the renderer handles the short final block like any other.
constexpr std::size_t kSc017GridSamples = 48000;

/// Pass 2's render only has to be long enough for the fingerprint's 32 spaced
/// checkpoints and its total-variation metric to have teeth.
constexpr std::size_t kSc017FingerprintSamples = 24000;  // 0.5 s

constexpr std::uint32_t kSc017Seed = 0x5E0A0017u;

// The documented bounds table (plan §4.8, tasks.md:978-984) is asserted against
// the shipped constants here, so a constant that drifts away from the spec fails
// at COMPILE time rather than silently re-defining what pass 3 measures.
static_assert(HarmonicCloud::kMinFundamentalHz == 20.0f, "FR-013 lower bound");
static_assert(HarmonicCloud::kMaxFundamentalHz == 4000.0f, "FR-013 upper bound");
static_assert(HarmonicCloud::kMaxInharmonicity == 0.1f, "FR-052");
static_assert(HarmonicCloud::kMinTiltDbPerOct == -12.0f, "FR-062");
static_assert(HarmonicCloud::kMaxTiltDbPerOct == 12.0f, "FR-062");
static_assert(HarmonicCloud::kMaxDriftCents == 50.0f, "FR-033");
static_assert(HarmonicCloud::kMinAttackSec == 0.05f, "FR-023");
static_assert(HarmonicCloud::kMaxAttackSec == 30.0f, "FR-023");
static_assert(HarmonicCloud::kMinDecaySec == 0.05f, "FR-023");
static_assert(HarmonicCloud::kMaxDecaySec == 60.0f, "FR-023");
static_assert(HarmonicCloud::kOutputClamp == 2.0f, "FR-006");

/// Grid axes (spec.md:979-981): {min, mid, max} for each of the five macros and
/// {min, max} for the fundamental and the three depth/spread controls.
constexpr std::array<float, 3> kSc017RichnessGrid{0.0f, 0.5f, 1.0f};
constexpr std::array<float, 3> kSc017InharmonicityGrid{0.0f, 0.05f,
                                                       HarmonicCloud::kMaxInharmonicity};
constexpr std::array<float, 3> kSc017TiltGrid{HarmonicCloud::kMinTiltDbPerOct, 0.0f,
                                              HarmonicCloud::kMaxTiltDbPerOct};
constexpr std::array<float, 3> kSc017MutationGrid{0.0f, 0.5f, 1.0f};
constexpr std::array<float, 3> kSc017GravityGrid{-1.0f, 0.0f, 1.0f};
constexpr std::array<float, 2> kSc017FundamentalGrid{HarmonicCloud::kMinFundamentalHz,
                                                     HarmonicCloud::kMaxFundamentalHz};
constexpr std::array<float, 2> kSc017DriftGrid{0.0f, HarmonicCloud::kMaxDriftCents};
constexpr std::array<float, 2> kSc017SpreadGrid{0.0f, 1.0f};
constexpr std::array<float, 2> kSc017OffsetGrid{0.0f, 1.0f};

constexpr std::size_t kSc017CellCount = 3 * 3 * 3 * 3 * 3 * 2 * 2 * 2 * 2;
static_assert(kSc017CellCount == 3888, "spec.md:979-981: 3^5 macro cells x 2^4 endpoint cells");

/// A non-finite float built from its IEEE-754 bit pattern through a volatile sink.
///
/// `std::numeric_limits<float>::infinity()` / `quiet_NaN()` are NOT usable here:
/// the macOS leg builds -ffast-math (-ffinite-math-only), under which the compiler
/// assumes non-finite values do not exist and constant-folds those calls to finite
/// garbage — so the "handles non-finite input" assertion would silently be testing
/// an ordinary number (reference_fastmath_nan_in_tests). The volatile load is what
/// forces a real non-finite bit pattern to exist at runtime regardless of FP mode.
[[nodiscard]] float sc017FromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t observed = sink;
    return std::bit_cast<float>(observed);
}

struct Sc017Poison {
    const char* label;
    std::uint32_t bits;
};

/// NaN 0x7FC00000, +Inf 0x7F800000, -Inf 0xFF800000. Designated initialisers —
/// Clang rejects narrowing in brace initialisation.
constexpr std::array<Sc017Poison, 3> kSc017Poisons{
    Sc017Poison{.label = "NaN", .bits = 0x7FC00000u},
    Sc017Poison{.label = "+Inf", .bits = 0x7F800000u},
    Sc017Poison{.label = "-Inf", .bits = 0xFF800000u},
};

/// What one render produced, reduced so the per-sample scan can stay out of
/// Catch2: 3888 cells x 96000 samples is 373 M values, which must NOT each be a
/// REQUIRE. The scan is plain code; the assertions are three per cell.
struct Sc017Scan {
    bool allFinite = true;
    float worstMagnitude = 0.0f;
    std::size_t firstNonFinite = 0;
};

/// Render `numSamples` in kSc017BlockSize chunks into `left`/`right`, scanning
/// every sample with the bit-pattern predicates (`core/db_utils.h:54,174`).
[[nodiscard]] Sc017Scan sc017RenderAndScan(HarmonicCloud& cloud, std::vector<float>& left,
                                           std::vector<float>& right, std::size_t numSamples) {
    Sc017Scan scan;
    std::size_t done = 0;
    while (done < numSamples) {
        const std::size_t n = std::min(kSc017BlockSize, numSamples - done);
        cloud.processStereoBlock(left.data() + done, right.data() + done, n);
        for (std::size_t i = 0; i < n; ++i) {
            const float l = left[done + i];
            const float r = right[done + i];
            if (detail::isNaN(l) || detail::isInf(l) || detail::isNaN(r) || detail::isInf(r)) {
                if (scan.allFinite) {
                    scan.allFinite = false;
                    scan.firstNonFinite = done + i;
                }
                continue;  // a NaN would poison the magnitude comparison below
            }
            scan.worstMagnitude = std::max(scan.worstMagnitude, std::max(std::abs(l), std::abs(r)));
        }
        done += n;
    }
    return scan;
}

/// Pass 2's fixed configuration: every control off its default and away from its
/// bounds, so a setter that quietly assigned the poison would move the getter in
/// BOTH directions and could not land back on the pre-call value by luck.
void configureSc017RejectionCloud(HarmonicCloud& cloud) {
    cloud.prepare(kSc017SampleRate);
    cloud.setSeed(kSc017Seed);
    cloud.setFundamentalHz(220.0f);
    cloud.setRichness(1.0f);  // all 64 partials, so every lane is audible
    cloud.setInharmonicity(0.01f);
    cloud.setSpectralTiltDb(-3.0f);
    cloud.setMutation(0.5f);
    cloud.setSpectralGravity(0.25f);
    cloud.setDriftDepthCents(20.0f);
    cloud.setDriftSmoothness(0.5f);
    cloud.setStereoSpread(0.7f);
    cloud.setAttackTimeSec(0.1f);
    cloud.setDecayTimeSec(0.5f);
    cloud.setEnvelopeOffsetSpread(0.3f);
}

}  // namespace

TEST_CASE("HarmonicCloud_ParameterGridStaysFiniteAndBounded") {
    // -------------------------------------------------------------------------
    // Pass 1 (FR-006) — the enumerated grid
    // -------------------------------------------------------------------------
    SECTION("Every cell of the enumerated parameter grid is finite and bounded") {
        // ONE instance, reused across all 3888 cells (tasks.md:965).
        HarmonicCloud cloud;
        cloud.prepare(kSc017SampleRate);
        cloud.setSeed(kSc017Seed);

        std::vector<float> left(kSc017GridSamples, 0.0f);
        std::vector<float> right(kSc017GridSamples, 0.0f);

        for (std::size_t cell = 0; cell < kSc017CellCount; ++cell) {
            // Mixed-radix decomposition of the cell index — a full Cartesian
            // enumeration without nine levels of nesting.
            std::size_t c = cell;
            const float richness = kSc017RichnessGrid[c % 3];
            c /= 3;
            const float inharmonicity = kSc017InharmonicityGrid[c % 3];
            c /= 3;
            const float tilt = kSc017TiltGrid[c % 3];
            c /= 3;
            const float mutation = kSc017MutationGrid[c % 3];
            c /= 3;
            const float gravity = kSc017GravityGrid[c % 3];
            c /= 3;
            const float fundamental = kSc017FundamentalGrid[c % 2];
            c /= 2;
            const float driftCents = kSc017DriftGrid[c % 2];
            c /= 2;
            const float spread = kSc017SpreadGrid[c % 2];
            c /= 2;
            const float offsetSpread = kSc017OffsetGrid[c % 2];

            cloud.setRichness(richness);
            cloud.setInharmonicity(inharmonicity);
            cloud.setSpectralTiltDb(tilt);
            // The {driftCents == 0} x {mutation == 1} cells are LIVE mutation
            // cells, not inert ones: FR-072's mutation bank has its own derived
            // seeds, its depth pinned at 1 and its own fixed smoothness, so the
            // drift-depth control cannot reach it (Clarifications Q1).
            cloud.setMutation(mutation);
            cloud.setSpectralGravity(gravity);
            cloud.setFundamentalHz(fundamental);
            cloud.setDriftDepthCents(driftCents);
            cloud.setStereoSpread(spread);
            cloud.setEnvelopeOffsetSpread(offsetSpread);

            // reset() AFTER the setters so every cell starts from the identical
            // post-prepare state at its own configuration: the seeded draws are
            // rewound, the FR-013 crossfade that the fundamental change may have
            // armed is cleared, and the FR-017 normalizer snaps to THIS cell's
            // level instead of sliding in from the previous cell's.
            cloud.reset();
            cloud.noteOn();

            const Sc017Scan scan = sc017RenderAndScan(cloud, left, right, kSc017GridSamples);

            INFO("cell " << cell << ": richness=" << richness << " B=" << inharmonicity
                         << " tiltDb=" << tilt << " mutation=" << mutation << " gravity=" << gravity
                         << " f0=" << fundamental << " driftCents=" << driftCents
                         << " spread=" << spread << " offsetSpread=" << offsetSpread);
            INFO("worst |out| = " << scan.worstMagnitude << " (clamp "
                                  << HarmonicCloud::kOutputClamp << "), first non-finite sample = "
                                  << scan.firstNonFinite);
            REQUIRE(scan.allFinite);
            REQUIRE(scan.worstMagnitude <= HarmonicCloud::kOutputClamp);
            REQUIRE(cloud.stateFinite());
        }
    }

    // -------------------------------------------------------------------------
    // Pass 2 (FR-007 half 1) — non-finite rejection
    // -------------------------------------------------------------------------
    SECTION("Every setter rejects NaN and both infinities without touching state") {
        HarmonicCloud cloud;
        configureSc017RejectionCloud(cloud);

        std::vector<float> referenceLeft(kSc017FingerprintSamples, 0.0f);
        std::vector<float> referenceRight(kSc017FingerprintSamples, 0.0f);
        cloud.reset();
        cloud.noteOn();
        const Sc017Scan referenceScan =
            sc017RenderAndScan(cloud, referenceLeft, referenceRight, kSc017FingerprintSamples);
        REQUIRE(referenceScan.allFinite);
        REQUIRE(referenceScan.worstMagnitude > 0.0f);  // the pinned render is not silence
        const auto referenceL = TestUtils::fingerprintRender(referenceLeft);
        const auto referenceR = TestUtils::fingerprintRender(referenceRight);

        std::vector<float> poisonedLeft(kSc017FingerprintSamples, 0.0f);
        std::vector<float> poisonedRight(kSc017FingerprintSamples, 0.0f);

        // reset() rewinds every seeded stream and all render-affecting state
        // (HarmonicCloud_ResetReproducesSeededState pins that), so the poisoned
        // render is comparable to the reference render sample for sample — up to
        // render_fingerprint.h's measured cross-toolchain tolerances, never a
        // bit-exact float golden (roadmap line 486).
        const auto rejects = [&](const char* name, auto&& setter, auto&& getter) {
            for (const Sc017Poison& poison : kSc017Poisons) {
                const float value = sc017FromBits(poison.bits);
                INFO(name << " <- " << poison.label);

                // Integer comparison on the bits. It cannot be folded away by
                // -ffast-math, so a poison that failed to survive construction
                // fails HERE, loudly, instead of turning the case into a silent
                // no-op that "passes" on the macOS leg.
                REQUIRE(std::bit_cast<std::uint32_t>(value) == poison.bits);

                cloud.reset();
                cloud.noteOn();

                const float before = getter();
                setter(value);
                INFO("getter before = " << before << ", after = " << getter());
                REQUIRE(getter() == before);  // EXACTLY, not within a tolerance

                const Sc017Scan scan = sc017RenderAndScan(cloud, poisonedLeft, poisonedRight,
                                                          kSc017FingerprintSamples);
                REQUIRE(scan.allFinite);

                const auto comparisonL = TestUtils::compareFingerprints(
                    TestUtils::fingerprintRender(poisonedLeft), referenceL);
                const auto comparisonR = TestUtils::compareFingerprints(
                    TestUtils::fingerprintRender(poisonedRight), referenceR);
                INFO("L: " << comparisonL.detail << " (metric "
                           << comparisonL.worstMetricRelativeError << ", sample "
                           << comparisonL.worstSampleError << ") | R: " << comparisonR.detail
                           << " (metric " << comparisonR.worstMetricRelativeError << ", sample "
                           << comparisonR.worstSampleError << ")");
                REQUIRE(comparisonL.withinTolerance());
                REQUIRE(comparisonR.withinTolerance());
            }
        };

        rejects("setFundamentalHz", [&](float v) { cloud.setFundamentalHz(v); },
                [&] { return cloud.getFundamentalHz(); });
        rejects("setRichness", [&](float v) { cloud.setRichness(v); },
                [&] { return cloud.getRichness(); });
        rejects("setInharmonicity", [&](float v) { cloud.setInharmonicity(v); },
                [&] { return cloud.getInharmonicity(); });
        rejects("setSpectralTiltDb", [&](float v) { cloud.setSpectralTiltDb(v); },
                [&] { return cloud.getSpectralTiltDb(); });
        rejects("setMutation", [&](float v) { cloud.setMutation(v); },
                [&] { return cloud.getMutation(); });
        rejects("setSpectralGravity", [&](float v) { cloud.setSpectralGravity(v); },
                [&] { return cloud.getSpectralGravity(); });
        rejects("setDriftDepthCents", [&](float v) { cloud.setDriftDepthCents(v); },
                [&] { return cloud.getDriftDepthCents(); });
        rejects("setDriftSmoothness", [&](float v) { cloud.setDriftSmoothness(v); },
                [&] { return cloud.getDriftSmoothness(); });
        rejects("setStereoSpread", [&](float v) { cloud.setStereoSpread(v); },
                [&] { return cloud.getStereoSpread(); });
        rejects("setAttackTimeSec", [&](float v) { cloud.setAttackTimeSec(v); },
                [&] { return cloud.getAttackTimeSec(); });
        rejects("setDecayTimeSec", [&](float v) { cloud.setDecayTimeSec(v); },
                [&] { return cloud.getDecayTimeSec(); });
        rejects("setEnvelopeOffsetSpread", [&](float v) { cloud.setEnvelopeOffsetSpread(v); },
                [&] { return cloud.getEnvelopeOffsetSpread(); });
        rejects("setPartialPosition", [&](float v) { cloud.setPartialPosition(0, v); },
                [&] { return cloud.getPartialPosition(0); });
    }

    // -------------------------------------------------------------------------
    // Pass 3 (FR-007 half 2) — finite out-of-range clamping
    // -------------------------------------------------------------------------
    // The half nothing else in this phase tests. A setter that assigned without
    // std::clamp still rejects poisons (pass 2), still renders bounded audio at
    // every grid point (pass 1, whose values are all in range), and still passes
    // every behavioural criterion — it only breaks here.
    SECTION("Finite out-of-range arguments come back as the documented bound") {
        HarmonicCloud cloud;
        cloud.prepare(kSc017SampleRate);

        cloud.setFundamentalHz(1.0f);
        REQUIRE(cloud.getFundamentalHz() == HarmonicCloud::kMinFundamentalHz);  // 20 Hz
        cloud.setFundamentalHz(9000.0f);
        REQUIRE(cloud.getFundamentalHz() == HarmonicCloud::kMaxFundamentalHz);  // 4000 Hz

        cloud.setRichness(-1.0f);
        REQUIRE(cloud.getRichness() == 0.0f);
        cloud.setRichness(2.0f);
        REQUIRE(cloud.getRichness() == 1.0f);

        cloud.setMutation(-1.0f);
        REQUIRE(cloud.getMutation() == 0.0f);
        cloud.setMutation(2.0f);
        REQUIRE(cloud.getMutation() == 1.0f);

        cloud.setDriftSmoothness(-1.0f);
        REQUIRE(cloud.getDriftSmoothness() == 0.0f);
        cloud.setDriftSmoothness(2.0f);
        REQUIRE(cloud.getDriftSmoothness() == 1.0f);

        cloud.setEnvelopeOffsetSpread(-1.0f);
        REQUIRE(cloud.getEnvelopeOffsetSpread() == 0.0f);
        cloud.setEnvelopeOffsetSpread(2.0f);
        REQUIRE(cloud.getEnvelopeOffsetSpread() == 1.0f);

        cloud.setInharmonicity(-1.0f);
        REQUIRE(cloud.getInharmonicity() == 0.0f);
        cloud.setInharmonicity(5.0f);
        REQUIRE(cloud.getInharmonicity() == HarmonicCloud::kMaxInharmonicity);  // 0.1

        cloud.setSpectralTiltDb(-100.0f);
        REQUIRE(cloud.getSpectralTiltDb() == HarmonicCloud::kMinTiltDbPerOct);  // -12 dB/oct
        cloud.setSpectralTiltDb(100.0f);
        REQUIRE(cloud.getSpectralTiltDb() == HarmonicCloud::kMaxTiltDbPerOct);  // +12 dB/oct

        cloud.setSpectralGravity(-9.0f);
        REQUIRE(cloud.getSpectralGravity() == -1.0f);
        cloud.setSpectralGravity(9.0f);
        REQUIRE(cloud.getSpectralGravity() == 1.0f);

        cloud.setDriftDepthCents(-1.0f);
        REQUIRE(cloud.getDriftDepthCents() == 0.0f);
        cloud.setDriftDepthCents(999.0f);
        REQUIRE(cloud.getDriftDepthCents() == HarmonicCloud::kMaxDriftCents);  // 50 cents

        cloud.setAttackTimeSec(0.001f);
        REQUIRE(cloud.getAttackTimeSec() == HarmonicCloud::kMinAttackSec);  // 0.05 s
        cloud.setAttackTimeSec(999.0f);
        REQUIRE(cloud.getAttackTimeSec() == HarmonicCloud::kMaxAttackSec);  // 30 s

        cloud.setDecayTimeSec(0.001f);
        REQUIRE(cloud.getDecayTimeSec() == HarmonicCloud::kMinDecaySec);  // 0.05 s
        cloud.setDecayTimeSec(999.0f);
        REQUIRE(cloud.getDecayTimeSec() == HarmonicCloud::kMaxDecaySec);  // 60 s

        // setStereoSpread and setPartialPosition are ordered LAST and in this
        // order on purpose: a spread change clears every FR-008 position override
        // (harmonic_cloud.h:425-427), so testing the spread bounds afterwards
        // would wipe the override the next clause asserts on.
        cloud.setStereoSpread(-1.0f);
        REQUIRE(cloud.getStereoSpread() == 0.0f);
        cloud.setStereoSpread(2.0f);
        REQUIRE(cloud.getStereoSpread() == 1.0f);

        cloud.setPartialPosition(0, -9.0f);
        REQUIRE(cloud.getPartialPosition(0) == -1.0f);
        cloud.setPartialPosition(0, 9.0f);
        REQUIRE(cloud.getPartialPosition(0) == 1.0f);

        // FR-008: an out-of-range index is a no-op, not a clamp onto the last
        // partial and not an out-of-bounds write.
        std::array<float, HarmonicCloud::kMaxPartials> positionsBefore{};
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            positionsBefore[i] = cloud.getPartialPosition(i);
        }
        cloud.setPartialPosition(HarmonicCloud::kMaxPartials, 0.75f);
        cloud.setPartialPosition(HarmonicCloud::kMaxPartials + 17u, -0.75f);
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("partial " << i << " position moved on an out-of-range setPartialPosition");
            REQUIRE(cloud.getPartialPosition(i) == positionsBefore[i]);
        }
    }
}

// =============================================================================
// Plan §4.9 / R7 — the currentAmplitude_ denormal guard
// =============================================================================
// THIS GUARD IS INVISIBLE TO EVERY OTHER CASE IN THIS FILE. `dsp_test_main.cpp:13`
// calls `enableFTZDAZ()` before any case runs, so the PROCESS flushes denormals —
// a guard that existed only in the environment would ship unnoticed, and every
// render-based criterion in this phase would still be green. The assertions below
// are therefore about the COMPONENT's OWN arithmetic: `getPartialCurrentAmplitude(i)`
// must be EXACTLY `0.0f`. That holds with FTZ/DAZ and without it, on every
// toolchain, which is the whole point.
//
// Why the guard has to sit on `currentAmplitude_` and NOT on `targetAmplitude_`:
// the kernel's amplitude-smoothing recurrence's state IS `currentAmplitude_`, and
// the kernel never flushes it (`vAmp = hn::MulAdd(vCoeff, vDiff, vAmp)`,
// `harmonic_oscillator_bank_simd.cpp:93`; scalar tail
// `currentAmplitude[i] += ampSmoothCoeff * (target - currentAmplitude[i])`, `:120`).
// Forcing a target to exactly zero is in fact the CONDITION that walks
// `currentAmplitude_` through the denormal range: at `kAmpSmoothTimeSec = 0.002f`
// the per-sample retention is ~0.9896 at 48 kHz, so a lane whose target reached 0
// is denormal ~175 ms later and stays denormal for thousands of samples more.
//
// The two clauses cover the two families the FR-043 tail high-water does NOT
// reach — both of them sit INSIDE `[0, kernelCount_)`, which is exactly the range
// `retireFadedTail()` never inspects:
//   1. every partial, once `noteOff()` has completed its release (env 0 => target 0);
//   2. masked / soloed-out partials, whose target is 0 while the gate is still OPEN.
TEST_CASE("HarmonicCloud_DecaysToExactZero") {
    constexpr double kSampleRate = 48000.0;

    /// Pinned so the per-seed drift/mutation lanes below are the ones the timing
    /// margins quoted in this case were reasoned about.
    constexpr std::uint32_t kSeed = 0x5E15D0A9u;

    constexpr float kDecaySec = 0.5f;

    /// 10x the 50 ms default attack — comfortably at steady state.
    constexpr std::size_t kSteadySamples = 24000;  // 0.5 s

    /// `decaySec + 2 s`, rendered in ONE processStereoBlock call.
    ///
    /// The single call is load-bearing, not laziness. FR-016's quiescent early-out
    /// (`harmonic_cloud.h:624`) short-circuits `processStereoBlock` — and therefore
    /// `updateControl`, where the guard lives — once every partial has fallen below
    /// `kQuiescenceAmplitude` (1e-5). The crossing from 1e-5 down to
    /// `kTailSilenceThreshold` (1e-8) takes `ln(1000) / 0.010367 ~ 666` samples
    /// (~14 ms) at 48 kHz, so it has to happen inside one block. 2.5 s leaves ~2 s
    /// of margin over the ~0.52 s the whole release plus that crossing needs.
    constexpr std::size_t kTailSamples = 120000;  // 2.5 s

    std::vector<float> left;
    std::vector<float> right;

    const auto render = [&left, &right](HarmonicCloud& cloud, std::size_t numSamples) {
        left.assign(numSamples, 0.0f);
        right.assign(numSamples, 0.0f);
        cloud.processStereoBlock(left.data(), right.data(), numSamples);
    };

    // Drift and mutation are ON: both lane banks keep writing into the per-chunk
    // amplitude chain, so the guard is exercised against a moving target rather
    // than a frozen one. Neither can rescue a partial whose envelope has closed —
    // `w` multiplies an `env` of exactly 0.
    const auto configure = [&](HarmonicCloud& cloud) {
        cloud.prepare(kSampleRate);
        cloud.setSeed(kSeed);
        cloud.setDecayTimeSec(kDecaySec);
        cloud.setDriftDepthCents(25.0f);
        cloud.setMutation(0.5f);
        cloud.setStereoSpread(0.5f);
    };

    // -------------------------------------------------------------------------
    // Clause 1 — every partial after a completed release
    // -------------------------------------------------------------------------
    SECTION("Every partial reaches exactly zero after a completed release") {
        HarmonicCloud cloud;
        configure(cloud);

        cloud.noteOn();
        render(cloud, kSteadySamples);

        // Non-vacuity: the cloud really is sounding, so every one of the 64 lanes
        // has a live amplitude that must walk DOWN through the denormal range.
        // Without this, a component that rendered silence would "pass".
        REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("partial " << i << " was already silent before noteOff(): "
                            << cloud.getPartialCurrentAmplitude(i));
            REQUIRE(cloud.getPartialCurrentAmplitude(i) > HarmonicCloud::kQuiescenceAmplitude);
        }

        cloud.noteOff();
        render(cloud, kTailSamples);

        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("partial " << i << " parked at " << cloud.getPartialCurrentAmplitude(i)
                            << " instead of exactly 0 (target "
                            << cloud.getPartialTargetAmplitude(i) << ")");
            REQUIRE(cloud.getPartialTargetAmplitude(i) == 0.0f);
            REQUIRE(cloud.getPartialCurrentAmplitude(i) == 0.0f);
        }
        REQUIRE(cloud.isQuiescent());
    }

    // -------------------------------------------------------------------------
    // Clause 2 — soloed-out partials, with the gate still OPEN
    // -------------------------------------------------------------------------
    // The case the tail high-water structurally cannot cover, and the one the
    // quiescent early-out structurally cannot cover either: `soloPartial(0)` drives
    // 63 targets to exactly zero while the cloud is loudly sounding, so
    // `isQuiescent()` is false, `updateControl` runs on every chunk, and those 63
    // lanes are handed to the kernel with a decaying amplitude for as long as the
    // note is held. Solo is engaged only AFTER steady state — engaging it before
    // `noteOn()` would leave the masked lanes at 0 from the start and make every
    // assertion below vacuously true — and is then held for the whole render.
    SECTION("Soloed-out partials reach exactly zero while the gate is still open") {
        HarmonicCloud cloud;
        configure(cloud);

        cloud.noteOn();
        render(cloud, kSteadySamples);

        std::array<float, HarmonicCloud::kMaxPartials> beforeSolo{};
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            beforeSolo[i] = cloud.getPartialCurrentAmplitude(i);
        }

        cloud.soloPartial(0);
        render(cloud, kTailSamples);  // gate still open throughout

        for (std::size_t i = 1; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("masked partial " << i << ": " << beforeSolo[i] << " -> "
                                   << cloud.getPartialCurrentAmplitude(i) << " (target "
                                   << cloud.getPartialTargetAmplitude(i) << ")");
            REQUIRE(beforeSolo[i] > HarmonicCloud::kQuiescenceAmplitude);
            REQUIRE(cloud.getPartialTargetAmplitude(i) == 0.0f);
            REQUIRE(cloud.getPartialCurrentAmplitude(i) == 0.0f);
        }

        // The guard flushed the 63 masked lanes without touching the soloed one:
        // it is keyed on `target == 0`, not on "small".
        INFO("soloed partial 0 amplitude: " << cloud.getPartialCurrentAmplitude(0));
        REQUIRE(cloud.getPartialCurrentAmplitude(0) > HarmonicCloud::kQuiescenceAmplitude);
        REQUIRE_FALSE(cloud.isQuiescent());

        // ...and once its own release completes, so does partial 0.
        cloud.noteOff();
        render(cloud, kTailSamples);

        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("partial " << i << " after release: " << cloud.getPartialCurrentAmplitude(i));
            REQUIRE(cloud.getPartialCurrentAmplitude(i) == 0.0f);
        }
        REQUIRE(cloud.isQuiescent());
    }
}

// =============================================================================
// SC-005 / SC-006 / FR-008 — the differential click criteria (T016)
// =============================================================================
// Every criterion below is DIFFERENTIAL against a frozen control render and
// every one of them carries a mandatory positive control. Neither is a
// stylistic choice:
//
//   * Why never "0 detections". `ClickDetector` is a WITHIN-FRAME statistical
//     outlier test — `threshold = mean(|dx|) + detectionThreshold*stddev(|dx|)`
//     computed per frame (`artifact_detection.h:186-193`). For a broadband
//     64-partial sum the first difference is near-Gaussian by CLT, so a
//     5-sigma-above-MEAN threshold is only ~3.8 sigma in absolute terms — a
//     per-sample exceedance probability on the order of 1e-4, i.e. TENS of false
//     detections over a 30 s render with no artifact present. An absolute-zero
//     bar would be satisfiable only by accident of spectral shape.
//   * Why the positive control is mandatory and FIRST. Without it the
//     differential criterion cannot distinguish "no artifacts" from "detector
//     not wired up" — a `detect()` that returned an empty vector unconditionally
//     would pass every clause here. Same rule the repo applies to render
//     fingerprints: verify the metric still fails on an injected defect.
//
// The within-frame normalisation is also why one control render can serve
// spectrally very different sweeps: the statistic is scale-free, so a brighter
// or louder render does not mechanically produce more detections.
namespace {

/// Pinned render basis for every case in this block. `kClickSampleRate` MUST
/// equal the detector config's `sampleRate` — the struct default is 44100
/// (`artifact_detection.h:39`), and a mismatch silently mis-scales
/// `timeSeconds` on every detection.
constexpr double kClickSampleRate = 48000.0;
constexpr std::size_t kClickBlockSize = 512;

/// The detector's own merge gap (`artifact_detection.h:44`), needed by the
/// positive control's index window — see `countDetectionsNear`.
constexpr std::size_t kClickMergeGap = 5;

/// THE PINNED DETECTOR CONFIGURATION (spec.md:712-715), used identically by all
/// three cases here and by `HarmonicCloud_RetriggerIsClickFree`. Designated
/// initialisers throughout — Clang rejects narrowing in brace initialisation —
/// and the field order is the declaration order at `artifact_detection.h:38-45`.
[[nodiscard]] std::vector<TestUtils::ClickDetection> detectClicks(
    const std::vector<float>& buffer) {
    TestUtils::ClickDetectorConfig cfg{.sampleRate = static_cast<float>(kClickSampleRate),
                                       .frameSize = 512,
                                       .hopSize = 256,
                                       .detectionThreshold = 5.0f,
                                       .energyThresholdDb = -60.0f,
                                       .mergeGap = kClickMergeGap};
    TestUtils::ClickDetector detector(cfg);
    detector.prepare();
    return detector.detect(buffer.data(), buffer.size());
}

/// Detections whose sample index lands ON a `grid`-sample boundary — the
/// signature of an artifact created by a periodic update rather than by the signal.
///
/// A step at sample index `k` raises |dx| at `k` and `k + 1`, and the surviving
/// merged detection can sit either side, so the window is `{grid-1, 0, 1}` — three
/// residues out of `grid`.
[[nodiscard]] std::size_t countOnGrid(const std::vector<TestUtils::ClickDetection>& detections,
                                      std::size_t grid) noexcept {
    std::size_t count = 0;
    for (const TestUtils::ClickDetection& detection : detections) {
        const std::size_t residue = detection.sampleIndex % grid;
        if (residue <= 1 || residue >= grid - 1) {
            ++count;
        }
    }
    return count;
}

/// The share of detections a uniformly-distributed set would put on that grid.
[[nodiscard]] constexpr double uniformOnGridFraction(std::size_t grid) noexcept {
    return 3.0 / static_cast<double>(grid);
}

/// Inject a one-sample step at EVERY `grid` boundary, alternating sign — a
/// synthetic zipper. Used as the positive control for the on-grid criterion.
[[nodiscard]] std::vector<float> injectGridZipper(const std::vector<float>& source,
                                                  std::size_t grid, float step) {
    std::vector<float> zipped = source;
    for (std::size_t i = grid; i < zipped.size(); i += grid) {
        zipped[i] += ((i / grid) % 2 == 0) ? step : -step;
    }
    return zipped;
}

/// Append `numBlocks` fixed-size blocks of stereo render to `left` / `right`.
void renderClickBlocks(HarmonicCloud& cloud, std::vector<float>& left, std::vector<float>& right,
                       std::size_t numBlocks) {
    std::array<float, kClickBlockSize> blockL{};
    std::array<float, kClickBlockSize> blockR{};
    for (std::size_t b = 0; b < numBlocks; ++b) {
        cloud.processStereoBlock(blockL.data(), blockR.data(), kClickBlockSize);
        left.insert(left.end(), blockL.begin(), blockL.end());
        right.insert(right.end(), blockR.begin(), blockR.end());
    }
}

[[nodiscard]] float bufferPeak(const std::vector<float>& buffer) noexcept {
    float peak = 0.0f;
    for (const float sample : buffer) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

/// Detections whose sample index lands in the window a one-sample step at
/// `injectIndex` can produce.
///
/// A one-sample step raises |dx| at BOTH `injectIndex` and `injectIndex + 1`,
/// and `mergeAdjacentDetections` keeps the LARGER-amplitude member of any chain
/// of detections within `mergeGap` (`artifact_detection.h:240-249`), so the
/// surviving index may sit anywhere within +-mergeGap of the injection. The
/// window is that chain, not a fudge factor.
[[nodiscard]] std::size_t countDetectionsNear(
    const std::vector<TestUtils::ClickDetection>& detections, std::size_t injectIndex) noexcept {
    const std::size_t lo = injectIndex - kClickMergeGap;
    const std::size_t hi = injectIndex + 1 + kClickMergeGap;
    std::size_t count = 0;
    for (const TestUtils::ClickDetection& detection : detections) {
        if (detection.sampleIndex >= lo && detection.sampleIndex <= hi) {
            ++count;
        }
    }
    return count;
}

/// Run the mandatory positive control over `control`: copy it, add a one-sample
/// step of `kClickInjectDeltaFactor` times its own largest first difference at its
/// midpoint, and report how many detections land in the injection window.
[[nodiscard]] std::size_t detectionsAtInjectedStep(const std::vector<float>& control,
                                                   std::size_t injectIndex, float step) {
    std::vector<float> injected = control;
    injected[injectIndex] += step;
    return countDetectionsNear(detectClicks(injected), injectIndex);
}

}  // namespace

// =============================================================================
// SC-005 — no zipper noise under mutation and drift
// =============================================================================
// Two 30 s renders at 48 kHz of the IDENTICAL configuration: one MODULATED
// (Mutation 1.0, drift depth at the documented maximum) and one CONTROL (drift
// depth 0, Mutation 0, no parameter movement whatsoever). Same seed, so the
// underlying static spectrum is the same and the only difference is whether the
// two lane banks are allowed to move the amplitude chain and the detune.
//
// FRs measured: FR-014 (the amplitude smoother is what makes the per-chunk
// mutation weight and target amplitude reach the output without a step),
// FR-034 (the chunk-rate detune update is phase-continuous because only epsilon
// changes — the MCF state is never reset), FR-074 (mutation is zipper-free at
// every setting).
namespace {

/// 2813 * 512 = 1,440,256 samples = 30.005 s at 48 kHz. The spec pins "30 s";
/// the block grid decides the last 5 ms.
constexpr std::size_t kZipBlocks = 2813;
constexpr std::size_t kZipSamples = kZipBlocks * kClickBlockSize;

constexpr std::uint32_t kZipSeed = 0x5E050005u;

/// SC-005 clause 2's tolerance, measured against the MATCHED-REGIME control below.
constexpr float kZipDeltaFactor = 1.5f;

/// The control-chunk grid a zipper would land on (FR-032). Detune and mutation are
/// re-read once per 64 samples, so a stepped update shows up at those indices.
constexpr std::size_t kZipControlGrid = HarmonicCloud::kControlChunkSamples;

/// How far the on-grid share of detections may exceed the uniform share before it
/// counts as enrichment. MEASURED (see the case) at 1.19x-1.49x uniform across the
/// whole drift-rate range; an injected zipper measures 21.3x (100% on-grid).
constexpr double kZipGridEnrichmentLimit = 3.0;

/// Everything except Mutation and the drift depth, which are the two controls
/// the two renders differ in. Richness 1.0 so all 64 partials — and therefore
/// all 64 detune lanes and all 64 mutation lanes — are audible.
void configureZipperCloud(HarmonicCloud& cloud, float driftSmoothness = 0.5f) {
    cloud.prepare(kClickSampleRate);
    cloud.setSeed(kZipSeed);
    cloud.setFundamentalHz(220.0f);
    cloud.setRichness(1.0f);
    cloud.setInharmonicity(0.0f);
    cloud.setSpectralTiltDb(-3.0f);
    cloud.setSpectralGravity(0.0f);
    cloud.setDriftSmoothness(driftSmoothness);
    cloud.setStereoSpread(0.5f);
    cloud.setAttackTimeSec(0.05f);
    cloud.setDecayTimeSec(0.10f);
    cloud.setEnvelopeOffsetSpread(0.0f);
}

}  // namespace

TEST_CASE("HarmonicCloud_NoZipperUnderMutationAndDrift") {
    // -------------------------------------------------------------------------
    // CONTROL — nothing moving: Mutation 0, drift depth 0, no parameter calls
    // -------------------------------------------------------------------------
    HarmonicCloud control;
    configureZipperCloud(control);
    control.setMutation(0.0f);
    control.setDriftDepthCents(0.0f);
    control.noteOn();

    std::vector<float> controlLeft;
    std::vector<float> controlRight;
    controlLeft.reserve(kZipSamples);
    controlRight.reserve(kZipSamples);
    renderClickBlocks(control, controlLeft, controlRight, kZipBlocks);
    REQUIRE(controlLeft.size() == kZipSamples);
    REQUIRE(controlRight.size() == kZipSamples);

    // -------------------------------------------------------------------------
    // POSITIVE CONTROL (mandatory, first)
    // -------------------------------------------------------------------------
    const float controlPeak = bufferPeak(controlLeft);
    INFO("control render peak = " << controlPeak);
    REQUIRE(controlPeak > 0.0f);

    const float controlDeltaForInjection = maxPerSampleDelta(controlLeft);
    INFO("control render max |dx| = " << controlDeltaForInjection);
    REQUIRE(controlDeltaForInjection > 0.0f);

    const std::size_t injectIndex = controlLeft.size() / 2;
    const float injectStep = kClickInjectDeltaFactor * controlDeltaForInjection;
    const std::size_t detectionsAtInjection =
        detectionsAtInjectedStep(controlLeft, injectIndex, injectStep);
    INFO("positive control: injected a " << injectStep << " step ("
                                         << kClickInjectDeltaFactor
                                         << " x the control's own max |dx|) at sample "
                                         << injectIndex << "; " << detectionsAtInjection
                                         << " detection(s) in the injection window");
    REQUIRE(detectionsAtInjection >= 1);

    // WITHDRAWN-CLAUSE GUARD — the positive control's magnitude (spec.md, SC-005
    // Amendment). SC-005 originally denominated the mandatory injection in PEAK:
    // "a deliberately injected one-sample step of 10 % of peak ... must report
    // >= 1 detection". MEASURED here: 10 % of this render's peak is 0.0748, the
    // render's own largest first difference is 0.1710, and the injection produces
    // ZERO detections.
    //
    // That is not a detector failure, it is the wrong denominator. `ClickDetector`
    // is a WITHIN-FRAME OUTLIER test on |dx| (artifact_detection.h:186-193) — it is
    // scale-free but says nothing about peak, and a step SMALLER than the signal's
    // own natural per-sample swing is by construction not an outlier. On a
    // 64-partial sum, peak and max |dx| are unrelated quantities. The operative
    // control above is therefore denominated in max |dx|, which is the quantity the
    // detector actually thresholds.
    //
    // The assertion below is the standing justification: it holds exactly while the
    // spec's step is under the render's own slew. If that ever inverts, the spec's
    // original wording becomes meaningful again and should be restored.
    const float specInjectStep = 0.10f * controlPeak;
    const std::size_t detectionsAtSpecStep =
        detectionsAtInjectedStep(controlLeft, injectIndex, specInjectStep);
    INFO("withdrawn-clause guard: SC-005's literal 10%-of-peak injection is "
         << specInjectStep << " against the render's own max |dx| of " << controlDeltaForInjection
         << "; it yields " << detectionsAtSpecStep << " detection(s)");
    REQUIRE(specInjectStep < controlDeltaForInjection);

    // -------------------------------------------------------------------------
    // MODULATED — Mutation 1.0, drift depth at the documented maximum
    // -------------------------------------------------------------------------
    HarmonicCloud modulated;
    configureZipperCloud(modulated);
    modulated.setMutation(1.0f);
    modulated.setDriftDepthCents(HarmonicCloud::kMaxDriftCents);
    modulated.noteOn();

    std::vector<float> modLeft;
    std::vector<float> modRight;
    modLeft.reserve(kZipSamples);
    modRight.reserve(kZipSamples);
    renderClickBlocks(modulated, modLeft, modRight, kZipBlocks);
    REQUIRE(modLeft.size() == kZipSamples);

    // Non-vacuity: the two renders really are different signals. Without this a
    // component that ignored both controls would satisfy every clause below.
    REQUIRE(modulated.getMutation() == 1.0f);
    REQUIRE(modulated.getDriftDepthCents() == HarmonicCloud::kMaxDriftCents);
    bool differs = false;
    for (std::size_t i = 0; i < kZipSamples && !differs; ++i) {
        differs = (modLeft[i] != controlLeft[i]);
    }
    INFO("the modulated render is bit-identical to the control — nothing is moving");
    REQUIRE(differs);

    // -------------------------------------------------------------------------
    // WITHDRAWN-CLAUSE GUARD — why SC-005 was amended (spec.md, SC-005 Amendment)
    // -------------------------------------------------------------------------
    // SC-005 originally read `detections(modulated) <= detections(control)` against
    // this frozen control. That clause was withdrawn because a CORRECT
    // implementation cannot satisfy it, and this block is the standing measurement
    // that says so: the frozen render is exactly periodic, so its first difference
    // has no within-frame outliers at all and the count is 0 — an upper bound no
    // aperiodic signal can meet, click-free or not.
    //
    // The assertion is deliberately the NEGATION of the withdrawn clause. If a
    // future change ever makes the original wording satisfiable, this fails, and
    // the correct response is to restore the spec's original clause rather than to
    // keep the substitute. An amendment must not outlive its justification.
    const std::size_t controlDetections =
        detectClicks(controlLeft).size() + detectClicks(controlRight).size();
    const std::size_t modulatedDetections =
        detectClicks(modLeft).size() + detectClicks(modRight).size();
    INFO("withdrawn-clause guard: frozen control scores " << controlDetections
                                                          << " detections (both channels), the "
                                                             "modulated render "
                                                          << modulatedDetections
                                                          << " — the original SC-005 clause 1 is "
                                                             "unsatisfiable while this holds");
    REQUIRE(controlDetections < modulatedDetections);

    // The same for clause 2: measured against the FROZEN control the max |dx| ratio
    // exceeds 1.5, which is the crest factor of an aperiodic sum against a periodic
    // one and not a step. The matched-regime comparison that replaced it is below.
    const float frozenDeltaL = maxPerSampleDelta(controlLeft);
    const float withdrawnRatioL = maxPerSampleDelta(modLeft) / frozenDeltaL;
    INFO("withdrawn-clause guard: max |dx| ratio against the FROZEN control = "
         << withdrawnRatioL << " (the original SC-005 clause 2 bound was 1.5)");
    REQUIRE(withdrawnRatioL > kZipDeltaFactor);

    // -------------------------------------------------------------------------
    // Clause 1 — the detections are not enriched on the control-chunk grid
    // -------------------------------------------------------------------------
    // The frozen control CANNOT serve as the null model for a detection COUNT, and
    // this is measured, not argued. `ClickDetector` normalises within the frame, so
    // it is scale-free — but it is not SPECTRUM-free, and the frozen render is not
    // merely a quieter version of the modulated one, it is in a different regime:
    // 64 exactly harmonic partials at fixed phases make a PERIODIC waveform whose
    // first difference has no outliers at all (measured: 0 detections in 30 s),
    // while ±50 cents of independent per-partial drift makes the sum aperiodic and
    // gives it the crest statistics of a random-phase sum. Measured on this
    // configuration, with the counts as a function of how FAST the drift moves:
    //
    //   drift smoothness | 0.00 | 0.25 | 0.50 | 0.75 | 1.00
    //   detections (L)   |  121 |  140 |  126 |  114 |  129
    //   detections (R)   |  145 |  161 |  141 |  111 |  141
    //
    // Smoothness selects tau over 0.2 s .. 30 s — a 150x change in how much the
    // detune moves per control chunk — and the count does not respond to it. Zipper
    // noise is by definition proportional to the per-chunk step, so a count that is
    // flat in the step size is not measuring zipper. Splitting the two modulators
    // says the same thing: mutation alone measures 0 detections, drift alone 154.
    //
    // What DOES identify a zipper is WHERE the detections land. A stepped update
    // repeats on the 64-sample control grid, so its detections concentrate on it.
    const std::vector<TestUtils::ClickDetection> modLeftDetections = detectClicks(modLeft);
    const std::vector<TestUtils::ClickDetection> modRightDetections = detectClicks(modRight);
    REQUIRE_FALSE(modLeftDetections.empty());  // else the fraction below is vacuous
    REQUIRE_FALSE(modRightDetections.empty());

    const double uniformShare = uniformOnGridFraction(kZipControlGrid);
    const double enrichmentLimit = kZipGridEnrichmentLimit * uniformShare;

    const double onGridLeft = static_cast<double>(countOnGrid(modLeftDetections, kZipControlGrid))
                              / static_cast<double>(modLeftDetections.size());
    INFO("left channel: " << countOnGrid(modLeftDetections, kZipControlGrid) << " of "
                          << modLeftDetections.size() << " detections on the "
                          << kZipControlGrid << "-sample control grid (" << (100.0 * onGridLeft)
                          << "%), uniform = " << (100.0 * uniformShare)
                          << "%, limit = " << (100.0 * enrichmentLimit) << "%");
    REQUIRE(onGridLeft <= enrichmentLimit);

    const double onGridRight = static_cast<double>(countOnGrid(modRightDetections, kZipControlGrid))
                               / static_cast<double>(modRightDetections.size());
    INFO("right channel: " << countOnGrid(modRightDetections, kZipControlGrid) << " of "
                           << modRightDetections.size() << " detections on the control grid ("
                           << (100.0 * onGridRight) << "%), limit = "
                           << (100.0 * enrichmentLimit) << "%");
    REQUIRE(onGridRight <= enrichmentLimit);

    // POSITIVE CONTROL for the on-grid metric itself: a synthetic zipper — a step
    // at EVERY control-chunk boundary of the frozen render, at half the render's own
    // largest first difference — must be caught, and caught ON the grid. Without
    // this the clause above could not tell "no zipper" from "metric not wired up".
    // MEASURED: 166 detections, 100% of them on-grid, at 0.5x; 3247 at 1.0x.
    const std::vector<float> syntheticZipper =
        injectGridZipper(controlLeft, kZipControlGrid, 0.5f * maxPerSampleDelta(controlLeft));
    const std::vector<TestUtils::ClickDetection> zipperDetections = detectClicks(syntheticZipper);
    const std::size_t zipperOnGrid = countOnGrid(zipperDetections, kZipControlGrid);
    INFO("synthetic zipper positive control: " << zipperDetections.size() << " detections, "
                                               << zipperOnGrid << " on-grid");
    REQUIRE(zipperDetections.size() >= 32);
    const double zipperOnGridFraction = static_cast<double>(zipperOnGrid)
                                        / static_cast<double>(zipperDetections.size());
    REQUIRE(zipperOnGridFraction > enrichmentLimit);
    REQUIRE(zipperOnGridFraction >= 0.9);

    // -------------------------------------------------------------------------
    // Clause 2 — the worst first difference, against a MATCHED-REGIME control
    // -------------------------------------------------------------------------
    // Same reason as clause 1: measured against the frozen render the ratio is 1.79,
    // and that 1.79 is the crest factor of an aperiodic sum against a periodic one,
    // not a step. The matched control carries the SAME Mutation and the SAME drift
    // depth — so the same aperiodic spectrum — and differs only in how fast the
    // drift moves (smoothness 1.0, tau = 30 s, the slowest the component offers).
    // Any per-chunk step scales with the per-chunk movement, so it survives this
    // comparison while the crest-factor effect cancels. MEASURED ratio: 1.098 (L),
    // 1.089 (R), against the 1.5 bound.
    HarmonicCloud slowDrift;
    configureZipperCloud(slowDrift, 1.0f);
    slowDrift.setMutation(1.0f);
    slowDrift.setDriftDepthCents(HarmonicCloud::kMaxDriftCents);
    slowDrift.noteOn();

    std::vector<float> slowLeft;
    std::vector<float> slowRight;
    slowLeft.reserve(kZipSamples);
    slowRight.reserve(kZipSamples);
    renderClickBlocks(slowDrift, slowLeft, slowRight, kZipBlocks);
    REQUIRE(slowLeft.size() == kZipSamples);

    const float slowDeltaL = maxPerSampleDelta(slowLeft);
    const float modDeltaL = maxPerSampleDelta(modLeft);
    INFO("left channel: max |dx| fast drift = " << modDeltaL << ", slow drift = " << slowDeltaL
                                                << ", bound = " << kZipDeltaFactor * slowDeltaL);
    REQUIRE(slowDeltaL > 0.0f);
    REQUIRE(modDeltaL <= kZipDeltaFactor * slowDeltaL);

    const float slowDeltaR = maxPerSampleDelta(slowRight);
    const float modDeltaR = maxPerSampleDelta(modRight);
    INFO("right channel: max |dx| fast drift = " << modDeltaR << ", slow drift = " << slowDeltaR
                                                 << ", bound = " << kZipDeltaFactor * slowDeltaR);
    REQUIRE(slowDeltaR > 0.0f);
    REQUIRE(modDeltaR <= kZipDeltaFactor * slowDeltaR);
}

// =============================================================================
// SC-006 — click-free macro automation
// =============================================================================
// Five 5 s continuous min->max sweeps, one per macro, each stepped once per
// 512-sample block, PLUS a 1-octave fundamental step (FR-013's crossfade path).
//
// Each is judged on WHERE its detections land, not on how many there are. The
// setters are called once per 512-sample block, so a click produced by a parameter
// step lands on the block grid; a detection anywhere else is the detector's
// statistical floor on a broadband render, which these sweeps move through by
// design. Comparing counts against a frozen control was tried and is INVALID, and
// the measurement that shows it is in the case below: a STATIC Richness-1.0 render
// with no parameter movement at all scores 18 (L) / 25 (R) detections while the
// Richness SWEEP that ends there scores 0 / 5 — the sweep is quieter than sitting
// still at its own endpoint, because the count is reading the arrival of 46 more
// partials rather than any click.
//
// FRs measured: FR-043 (Richness — partials entering and leaving fade through
// the amplitude smoother instead of switching), FR-053 (Inharmonicity — epsilon
// is recomputed with no phase reset), FR-063 (Tilt reaches the output through
// the smoother), FR-074 (Mutation), FR-084 (Gravity), FR-013 (the fundamental
// step arms the short output crossfade).
namespace {

/// 469 * 512 = 240,128 samples = 5.003 s at 48 kHz.
constexpr std::size_t kSweepBlocks = 469;
constexpr std::size_t kSweepSamples = kSweepBlocks * kClickBlockSize;

constexpr std::uint32_t kSweepSeed = 0x5E060006u;

/// The fundamental step: exactly one octave, which is far past FR-013's
/// one-semitone crossfade threshold. At Richness 0.7 (18 active partials) the
/// top partial lands at 7920 Hz, so nothing is fading against Nyquist and the
/// clause measures the crossfade rather than the FR-015 anti-alias ramp.
constexpr float kSweepFundamentalLoHz = 220.0f;
constexpr float kSweepFundamentalHiHz = 440.0f;

/// Base configuration. Drift and Mutation stay LIVE in both the control and the
/// swept renders — they are not parameter movement, and freezing them would make
/// the control a different signal family from the thing it is controlling for.
///
/// The base Inharmonicity is 0 for the fundamental-step clause's sake: at B = 0.1
/// the 18th partial of a 440 Hz fundamental sits at ~21.7 kHz, inside FR-015's
/// fade region, and the step clause would then be measuring the anti-alias ramp
/// as much as FR-013's crossfade. The Inharmonicity SWEEP still covers the whole
/// documented range — it simply starts where the control sits.
void configureSweepCloud(HarmonicCloud& cloud) {
    cloud.prepare(kClickSampleRate);
    cloud.setSeed(kSweepSeed);
    cloud.setFundamentalHz(kSweepFundamentalLoHz);
    cloud.setRichness(0.7f);
    cloud.setInharmonicity(0.0f);
    cloud.setSpectralTiltDb(0.0f);
    cloud.setMutation(0.5f);
    cloud.setSpectralGravity(0.0f);
    cloud.setDriftDepthCents(25.0f);
    cloud.setDriftSmoothness(0.5f);
    cloud.setStereoSpread(0.5f);
    cloud.setAttackTimeSec(0.05f);
    cloud.setDecayTimeSec(0.10f);
    cloud.setEnvelopeOffsetSpread(0.0f);
}

/// One swept macro. The setter is held as a pointer-to-member so the five sweeps
/// run through one loop body rather than five near-identical copies; the
/// noexcept-to-non-noexcept conversion on the right-hand side is the standard
/// implicit one (P0012R1).
struct MacroSweep {
    const char* name;
    void (HarmonicCloud::*setter)(float);
    float minValue;
    float maxValue;
};

}  // namespace

TEST_CASE("HarmonicCloud_MacroSweepsAreClickFree") {
    // -------------------------------------------------------------------------
    // The frozen-parameter render at the base configuration
    // -------------------------------------------------------------------------
    // This one exists ONLY to carry the positive control. It is NOT the null model
    // any sweep is judged against — see the endpoint controls below.
    HarmonicCloud control;
    configureSweepCloud(control);
    control.noteOn();

    std::vector<float> controlLeft;
    std::vector<float> controlRight;
    controlLeft.reserve(kSweepSamples);
    controlRight.reserve(kSweepSamples);
    renderClickBlocks(control, controlLeft, controlRight, kSweepBlocks);
    REQUIRE(controlLeft.size() == kSweepSamples);

    // -------------------------------------------------------------------------
    // POSITIVE CONTROL (mandatory, first)
    // -------------------------------------------------------------------------
    const float controlPeak = bufferPeak(controlLeft);
    INFO("control render peak = " << controlPeak);
    REQUIRE(controlPeak > 0.0f);

    const float controlDeltaForInjection = maxPerSampleDelta(controlLeft);
    INFO("control render max |dx| = " << controlDeltaForInjection);
    REQUIRE(controlDeltaForInjection > 0.0f);

    const std::size_t injectIndex = controlLeft.size() / 2;
    const float injectStep = kClickInjectDeltaFactor * controlDeltaForInjection;
    const std::size_t detectionsAtInjection =
        detectionsAtInjectedStep(controlLeft, injectIndex, injectStep);
    INFO("positive control: injected a " << injectStep << " step (" << kClickInjectDeltaFactor
                                         << " x the control's own max |dx|) at sample "
                                         << injectIndex << "; " << detectionsAtInjection
                                         << " detection(s) in the injection window");
    REQUIRE(detectionsAtInjection >= 1);

    // SC-006 inherits SC-005's mandatory positive control verbatim, and SC-005
    // denominates it in PEAK: a one-sample step of 10 % of peak must be detected.
    // Asserted separately because on this render it is the smaller, and therefore
    // the stricter, of the two injections.
    const float specInjectStep = 0.10f * controlPeak;
    const std::size_t detectionsAtSpecStep =
        detectionsAtInjectedStep(controlLeft, injectIndex, specInjectStep);
    INFO("SC-006 positive control (literal SC-005 form): injected a "
         << specInjectStep << " step (10% of the control's peak " << controlPeak << ") at sample "
         << injectIndex << "; " << detectionsAtSpecStep << " detection(s) in the window");
    REQUIRE(detectionsAtSpecStep >= 1);

    // -------------------------------------------------------------------------
    // POSITIVE CONTROL for the on-grid metric (mandatory, before any sweep)
    // -------------------------------------------------------------------------
    // The criterion below is "no detections on the block grid". Zero is only
    // meaningful once the metric is shown to produce a non-zero on an actual defect.
    // A synthetic per-block zipper — one step at EVERY block boundary of the base
    // render, at half its own largest first difference. MEASURED on the base render:
    // 22 detections, 100% of them on-grid. (The same injection into the much
    // brighter Richness-sweep render, whose max |dx| is 0.78 rather than 0.09,
    // measures 440 detections at 0.5x and 465 at 1.0x — also 100% on-grid.) Nothing
    // else in this file would catch a regression that turned `countOnGrid` into a
    // constant 0.
    const std::vector<float> syntheticZipper =
        injectGridZipper(controlLeft, kClickBlockSize, 0.5f * controlDeltaForInjection);
    const std::vector<TestUtils::ClickDetection> zipperDetections = detectClicks(syntheticZipper);
    const std::size_t zipperOnGrid = countOnGrid(zipperDetections, kClickBlockSize);
    INFO("synthetic per-block zipper: " << zipperDetections.size() << " detections, "
                                        << zipperOnGrid << " on the block grid");
    REQUIRE(zipperDetections.size() >= 16);
    REQUIRE(zipperOnGrid * 10 >= zipperDetections.size() * 9);  // >= 90% on-grid

    // -------------------------------------------------------------------------
    // WITHDRAWN-CLAUSE GUARD — why SC-006 was amended (spec.md, SC-006 Amendment)
    // -------------------------------------------------------------------------
    // SC-006 originally read `detections(swept) <= detections(control)` for each
    // macro, against "a frozen-parameter control render of the same configuration".
    // That clause was withdrawn because its verdict is decided by a choice the
    // wording never makes: WHICH frozen value is "the same configuration"? A sweep
    // has two endpoints and the detector's count on this signal tracks how many
    // partials are sounding, not how many clicks there are.
    //
    // This block is the standing measurement. It renders the Richness sweep's two
    // endpoints STATICALLY — no parameter movement in either, so both are equally
    // valid readings of "the control" — and asserts they disagree by more than the
    // swept render's whole count. While that holds, the withdrawn clause is not a
    // criterion, it is a coin flip, and the on-grid criterion below stands in its
    // place. If the two ever converge, restore the spec's original wording.
    const auto staticRichnessDetections = [](float richness) {
        HarmonicCloud endpoint;
        configureSweepCloud(endpoint);
        endpoint.setRichness(richness);
        endpoint.noteOn();

        std::vector<float> left;
        std::vector<float> right;
        left.reserve(kSweepSamples);
        right.reserve(kSweepSamples);
        renderClickBlocks(endpoint, left, right, kSweepBlocks);
        return detectClicks(left).size() + detectClicks(right).size();
    };

    const std::size_t controlAtSweepMin = staticRichnessDetections(0.0f);
    const std::size_t controlAtSweepMax = staticRichnessDetections(1.0f);
    INFO("withdrawn-clause guard: a STATIC render at the Richness sweep's min endpoint scores "
         << controlAtSweepMin << " detections and at its max endpoint " << controlAtSweepMax
         << " — both are 'the frozen control of the same configuration'");
    const std::size_t endpointSpread = (controlAtSweepMax > controlAtSweepMin)
                                           ? (controlAtSweepMax - controlAtSweepMin)
                                           : (controlAtSweepMin - controlAtSweepMax);
    REQUIRE(endpointSpread >= 16);

    /// Assert that a render's detections carry no block-grid signature.
    ///
    /// MEASURED for every sweep in this case and for the fundamental step: ZERO
    /// on-grid detections, against totals of 0..13 per channel. The uniform
    /// expectation on a 512-sample grid is 3/512 = 0.586% — for 13 detections that
    /// is 0.08 expected — so zero is what a click-free render looks like and any
    /// nonzero result is worth failing on.
    const auto requireNoGridSignature = [](const std::vector<float>& buffer, const char* label) {
        const std::vector<TestUtils::ClickDetection> detections = detectClicks(buffer);
        const std::size_t onGrid = countOnGrid(detections, kClickBlockSize);
        INFO(label << ": " << onGrid << " of " << detections.size()
                   << " detections on the " << kClickBlockSize << "-sample block grid");
        REQUIRE(onGrid == 0);
    };

    // -------------------------------------------------------------------------
    // The five macro sweeps, each judged independently
    // -------------------------------------------------------------------------
    const std::array<MacroSweep, 5> sweeps{
        MacroSweep{.name = "Richness",
                   .setter = &HarmonicCloud::setRichness,
                   .minValue = 0.0f,
                   .maxValue = 1.0f},
        MacroSweep{.name = "Inharmonicity",
                   .setter = &HarmonicCloud::setInharmonicity,
                   .minValue = 0.0f,
                   .maxValue = HarmonicCloud::kMaxInharmonicity},
        MacroSweep{.name = "Tilt",
                   .setter = &HarmonicCloud::setSpectralTiltDb,
                   .minValue = HarmonicCloud::kMinTiltDbPerOct,
                   .maxValue = HarmonicCloud::kMaxTiltDbPerOct},
        MacroSweep{.name = "Mutation",
                   .setter = &HarmonicCloud::setMutation,
                   .minValue = 0.0f,
                   .maxValue = 1.0f},
        MacroSweep{.name = "Gravity",
                   .setter = &HarmonicCloud::setSpectralGravity,
                   .minValue = -1.0f,
                   .maxValue = 1.0f},
    };

    for (const MacroSweep& sweep : sweeps) {
        HarmonicCloud cloud;
        configureSweepCloud(cloud);
        cloud.noteOn();

        std::vector<float> left;
        std::vector<float> right;
        left.reserve(kSweepSamples);
        right.reserve(kSweepSamples);

        std::array<float, kClickBlockSize> blockL{};
        std::array<float, kClickBlockSize> blockR{};
        const float span = sweep.maxValue - sweep.minValue;
        const float lastBlock = static_cast<float>(kSweepBlocks - 1);
        for (std::size_t b = 0; b < kSweepBlocks; ++b) {
            const float t = static_cast<float>(b) / lastBlock;
            (cloud.*(sweep.setter))(sweep.minValue + span * t);
            cloud.processStereoBlock(blockL.data(), blockR.data(), kClickBlockSize);
            left.insert(left.end(), blockL.begin(), blockL.end());
            right.insert(right.end(), blockR.begin(), blockR.end());
        }
        REQUIRE(left.size() == kSweepSamples);

        INFO(sweep.name << " sweep");
        requireNoGridSignature(left, "left channel");
        requireNoGridSignature(right, "right channel");
    }

    // -------------------------------------------------------------------------
    // The sixth: a 1-octave fundamental STEP (FR-013's crossfade path)
    // -------------------------------------------------------------------------
    // Not a sweep — the point is the discontinuous jump. Phase continuity alone
    // is not sufficient for a jump this large: recomputing epsilon changes the
    // MCF orbit eccentricity, which is why FR-013 arms a short output crossfade
    // above a one-semitone ratio.
    {
        HarmonicCloud cloud;
        configureSweepCloud(cloud);
        cloud.noteOn();

        std::vector<float> left;
        std::vector<float> right;
        left.reserve(kSweepSamples);
        right.reserve(kSweepSamples);

        renderClickBlocks(cloud, left, right, kSweepBlocks / 2);
        REQUIRE(cloud.getFundamentalHz() == kSweepFundamentalLoHz);
        cloud.setFundamentalHz(kSweepFundamentalHiHz);
        REQUIRE(cloud.getFundamentalHz() == kSweepFundamentalHiHz);
        renderClickBlocks(cloud, left, right, kSweepBlocks - (kSweepBlocks / 2));
        REQUIRE(left.size() == kSweepSamples);

        // The step is applied at a block boundary like every other parameter move
        // here, so if FR-013's crossfade failed to arm, the resulting discontinuity
        // would land on the block grid and be counted. MEASURED: 1 detection per
        // channel over the whole render, 0 of them on-grid.
        INFO("fundamental step " << kSweepFundamentalLoHz << " -> " << kSweepFundamentalHiHz
                                 << " Hz");
        requireNoGridSignature(left, "left channel");
        requireNoGridSignature(right, "right channel");
    }
}

// =============================================================================
// FR-008 — the mask / solo facility is click-free, and it FADES
// =============================================================================
// Nothing else in this phase covers it: SC-006 sweeps only the five macros plus
// the fundamental, and SC-001/SC-012 use `soloPartial` purely as a measurement
// tool without ever asserting click-freeness.
//
// Two independent claims:
//   1. the differential click criterion across a render that toggles
//      soloPartial / setPartialMask / clearPartialMask mid-flight;
//   2. THE MECHANISM. FR-008 requires masking to be applied at the END of the
//      amplitude chain (`harmonic_cloud.h:1309`) so that FR-014's smoother still
//      applies. An implementation that zeroed `currentAmplitude_` directly
//      instead of `targetAmplitude_` still passes the click detector on some
//      seeds — it fails claim 2 outright.
namespace {

constexpr std::uint32_t kMaskSeed = 0x5E080008u;

/// Blocks rendered before anything is masked — 100 * 512 = 1.07 s against a
/// 50 ms attack, so every partial is provably at its hold level.
constexpr std::size_t kMaskSteadyBlocks = 100;

/// Toggle schedule for the masked render (block indices).
constexpr std::size_t kMaskSoloBlock = 100;
constexpr std::size_t kMaskClearBlock = 200;
constexpr std::size_t kMaskSingleBlock = 300;
constexpr std::size_t kMaskFinalClearBlock = 350;
constexpr std::size_t kMaskTotalBlocks = 400;
constexpr std::size_t kMaskSamples = kMaskTotalBlocks * kClickBlockSize;

/// The partial left sounding by `soloPartial`, and the single partial the
/// `setPartialMask` phase removes.
constexpr std::size_t kMaskSoloIndex = 0;
constexpr std::size_t kMaskMutedIndex = 3;

void configureMaskCloud(HarmonicCloud& cloud) {
    cloud.prepare(kClickSampleRate);
    cloud.setSeed(kMaskSeed);
    cloud.setFundamentalHz(220.0f);
    cloud.setRichness(1.0f);  // all 64 partials, so 63 of them are masked out
    cloud.setInharmonicity(0.0f);
    cloud.setSpectralTiltDb(-3.0f);
    cloud.setMutation(0.5f);
    cloud.setSpectralGravity(0.0f);
    cloud.setDriftDepthCents(25.0f);
    cloud.setDriftSmoothness(0.5f);
    cloud.setStereoSpread(0.5f);
    cloud.setAttackTimeSec(0.05f);
    cloud.setDecayTimeSec(0.10f);
    cloud.setEnvelopeOffsetSpread(0.0f);
}

}  // namespace

TEST_CASE("HarmonicCloud_MaskAndSoloAreClickFree") {
    // -------------------------------------------------------------------------
    // Claim 2 first — the mechanism, on its own instance
    // -------------------------------------------------------------------------
    {
        HarmonicCloud mech;
        configureMaskCloud(mech);
        mech.noteOn();

        std::vector<float> sinkL;
        std::vector<float> sinkR;
        sinkL.reserve(kMaskSteadyBlocks * kClickBlockSize);
        sinkR.reserve(kMaskSteadyBlocks * kClickBlockSize);
        renderClickBlocks(mech, sinkL, sinkR, kMaskSteadyBlocks);
        REQUIRE(mech.getActivePartialCount() == HarmonicCloud::kMaxPartials);

        std::array<float, HarmonicCloud::kMaxPartials> beforeSolo{};
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            beforeSolo[i] = mech.getPartialCurrentAmplitude(i);
            INFO("partial " << i << " amplitude before solo = " << beforeSolo[i]);
            REQUIRE(beforeSolo[i] > 0.0f);
        }

        mech.soloPartial(kMaskSoloIndex);

        // IMMEDIATELY after the call, with nothing rendered since: solo is
        // configuration only. It may not have touched the oscillator's amplitude
        // state — the masking is applied to `targetAmplitude_` at the end of the
        // chain during the next control chunk, not to `currentAmplitude_` here.
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            if (i == kMaskSoloIndex) {
                continue;
            }
            INFO("partial " << i << ": soloPartial moved currentAmplitude from " << beforeSolo[i]
                            << " to " << mech.getPartialCurrentAmplitude(i)
                            << " with nothing rendered in between");
            REQUIRE(bitsEqual(mech.getPartialCurrentAmplitude(i), beforeSolo[i]));
        }

        // ...and over the following blocks it DECAYS through FR-014's smoother
        // rather than snapping to zero. Two blocks, not more, on purpose: the
        // 2 ms smoother retains ~0.48 % per 512-sample block at 48 kHz, so the
        // third block would cross `kTailSilenceThreshold` (1e-8) and the plan
        // §4.9 denormal guard would legitimately flush the lane to exactly 0.
        std::array<float, HarmonicCloud::kMaxPartials> afterOne{};
        renderClickBlocks(mech, sinkL, sinkR, std::size_t{1});
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            if (i == kMaskSoloIndex) {
                continue;
            }
            afterOne[i] = mech.getPartialCurrentAmplitude(i);
            INFO("partial " << i << " one block after solo: " << beforeSolo[i] << " -> "
                            << afterOne[i] << " (target " << mech.getPartialTargetAmplitude(i)
                            << ")");
            REQUIRE(mech.getPartialTargetAmplitude(i) == 0.0f);
            REQUIRE(afterOne[i] > 0.0f);          // faded, NOT zeroed
            REQUIRE(afterOne[i] < beforeSolo[i]);  // ...and it is going down
        }

        renderClickBlocks(mech, sinkL, sinkR, std::size_t{1});
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            if (i == kMaskSoloIndex) {
                continue;
            }
            const float afterTwo = mech.getPartialCurrentAmplitude(i);
            INFO("partial " << i << " two blocks after solo: " << afterOne[i] << " -> "
                            << afterTwo);
            REQUIRE(afterTwo > 0.0f);
            REQUIRE(afterTwo < afterOne[i]);
        }

        // The soloed partial is untouched throughout — masking is keyed on the
        // mask, not on "small".
        INFO("soloed partial " << kMaskSoloIndex
                               << " amplitude = " << mech.getPartialCurrentAmplitude(kMaskSoloIndex)
                               << ", target = "
                               << mech.getPartialTargetAmplitude(kMaskSoloIndex));
        REQUIRE(mech.getPartialTargetAmplitude(kMaskSoloIndex) > 0.0f);
        REQUIRE(mech.getPartialCurrentAmplitude(kMaskSoloIndex) > 0.0f);
    }

    // -------------------------------------------------------------------------
    // Claim 1 — the frozen control render
    // -------------------------------------------------------------------------
    HarmonicCloud control;
    configureMaskCloud(control);
    control.noteOn();

    std::vector<float> controlLeft;
    std::vector<float> controlRight;
    controlLeft.reserve(kMaskSamples);
    controlRight.reserve(kMaskSamples);
    renderClickBlocks(control, controlLeft, controlRight, kMaskTotalBlocks);
    REQUIRE(controlLeft.size() == kMaskSamples);

    // POSITIVE CONTROL (mandatory, first)
    const float controlPeak = bufferPeak(controlLeft);
    INFO("control render peak = " << controlPeak);
    REQUIRE(controlPeak > 0.0f);

    const float controlDeltaForInjection = maxPerSampleDelta(controlLeft);
    INFO("control render max |dx| = " << controlDeltaForInjection);
    REQUIRE(controlDeltaForInjection > 0.0f);

    const std::size_t injectIndex = controlLeft.size() / 2;
    const float injectStep = kClickInjectDeltaFactor * controlDeltaForInjection;
    const std::size_t detectionsAtInjection =
        detectionsAtInjectedStep(controlLeft, injectIndex, injectStep);
    INFO("positive control: injected a " << injectStep << " step (" << kClickInjectDeltaFactor
                                         << " x the control's own max |dx|) at sample "
                                         << injectIndex << "; " << detectionsAtInjection
                                         << " detection(s) in the injection window");
    REQUIRE(detectionsAtInjection >= 1);

    // -------------------------------------------------------------------------
    // Claim 1 — the masked render: solo -> clear -> single mask -> clear
    // -------------------------------------------------------------------------
    HarmonicCloud cloud;
    configureMaskCloud(cloud);
    cloud.noteOn();

    std::vector<float> left;
    std::vector<float> right;
    left.reserve(kMaskSamples);
    right.reserve(kMaskSamples);

    std::array<float, kClickBlockSize> blockL{};
    std::array<float, kClickBlockSize> blockR{};
    for (std::size_t b = 0; b < kMaskTotalBlocks; ++b) {
        if (b == kMaskSoloBlock) {
            cloud.soloPartial(kMaskSoloIndex);
        } else if (b == kMaskSingleBlock) {
            cloud.setPartialMask(kMaskMutedIndex, false);
        } else if (b == kMaskClearBlock || b == kMaskFinalClearBlock) {
            // The schedule clears twice — after the solo phase and after the
            // single-mask phase. The four block indices are distinct, so folding the
            // two identical arms together leaves the schedule unchanged.
            cloud.clearPartialMask();
        }
        cloud.processStereoBlock(blockL.data(), blockR.data(), kClickBlockSize);
        left.insert(left.end(), blockL.begin(), blockL.end());
        right.insert(right.end(), blockR.begin(), blockR.end());
    }
    REQUIRE(left.size() == kMaskSamples);

    // Non-vacuity: the toggles really did change the signal.
    bool differs = false;
    for (std::size_t i = 0; i < kMaskSamples && !differs; ++i) {
        differs = (left[i] != controlLeft[i]);
    }
    INFO("the masked render is bit-identical to the control — the toggles did nothing");
    REQUIRE(differs);

    const std::vector<TestUtils::ClickDetection> controlLeftDetections = detectClicks(controlLeft);
    const std::vector<TestUtils::ClickDetection> leftDetections = detectClicks(left);
    INFO("left channel: masked = " << leftDetections.size()
                                   << " detections, control = " << controlLeftDetections.size());
    REQUIRE(leftDetections.size() <= controlLeftDetections.size());

    const std::vector<TestUtils::ClickDetection> controlRightDetections =
        detectClicks(controlRight);
    const std::vector<TestUtils::ClickDetection> rightDetections = detectClicks(right);
    INFO("right channel: masked = " << rightDetections.size()
                                    << " detections, control = " << controlRightDetections.size());
    REQUIRE(rightDetections.size() <= controlRightDetections.size());
}

// =============================================================================
// SC-008 — zero heap allocations in the steady-state render loop (FR-002)
// =============================================================================
// FR-002 makes every processing and parameter method allocation-free; this is
// the measurement.
//
// @par The wiring hazard this case is shaped around
// `AllocationDetector` counts nothing on its own: the global `operator
// new`/`delete` replacements inside `allocation_detector.h` are COMMENTED OUT
// (`tests/test_helpers/allocation_detector.h:99-138`). Counting only happens
// because `allocation_operator_overrides.h` is linked into this binary from
// `selectable_oscillator_test.cpp:388` — a header that must be included from
// exactly one TU per binary, so this TU deliberately does NOT include it. The
// consequence is that a mis-wired binary would report 0 allocations
// unconditionally and `REQUIRE(count == 0)` would pass while measuring nothing.
// **The liveness clause below therefore runs FIRST and is mandatory**: a
// deliberate heap allocation must be counted before the zero clause is allowed
// to mean anything.
//
// @par Why the probe allocation is read through a volatile pointer
// C++14 (N3664) permits an implementation to elide a `new`/`delete` pair whose
// storage is never observably used, and both MSVC and GCC do so at -O2. An
// elided probe would report 0 and red the liveness clause on a perfectly wired
// detector. The volatile load forces the storage to exist.
//
// @par Why `AllocationDetector::instance()` and not `AllocationScope`
// `AllocationScope` assigns its count in its DESTRUCTOR
// (`allocation_detector.h:75-95`), so `getAllocationCount()` returns 0 for the
// whole lifetime of the object and the scope has to end before the count is
// valid — by which point the object is gone. Reading it in scope would make BOTH
// clauses here vacuous. The in-repo idiom is the bracketing pair
// (`selectable_oscillator_test.cpp:418-422`).
//
// @par What runs inside the tracked window
// Nothing but the component. No Catch2 macro (`INFO` builds a `ScopedMessage`
// and `REQUIRE` decomposes into strings — both allocate), no `std::vector`
// growth, no stream formatting. Every assertion is made after `stopTracking()`.
// The buffers are `std::array` and the one warm-up block is rendered BEFORE
// tracking starts, so Highway's first-call runtime dispatch inside
// `processMcfBatchSIMD` cannot be charged to the loop.
//
// Configuration is the worst case SC-008 names — "including macro automation and
// drift": both drift banks live (max drift depth, mutation swept to 1), all five
// macros plus the fundamental stepped once per 512-sample block, which is the
// Phase-7 call cadence SC-006 and SC-007 already use.
// =============================================================================
TEST_CASE("HarmonicCloud_NoAllocInProcess") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlockSize = 512;
    constexpr std::size_t kNumBlocks = 200;  // ~2.13 s @ 48 kHz
    constexpr std::uint32_t kSeed = 0x5C008A11u;

    // -------------------------------------------------------------------------
    // LIVENESS FIRST (mandatory). Without this the zero clause below cannot
    // distinguish "allocation-free" from "detector not wired into this binary".
    // -------------------------------------------------------------------------
    TestHelpers::AllocationDetector::instance().startTracking();
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    int* probe = new int[16];
    probe[0] = 42;
    volatile int* probeSink = probe;  // defeat the N3664 new/delete elision
    const int probeObserved = probeSink[0];
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete[] probe;
    const std::size_t livenessCount = TestHelpers::AllocationDetector::instance().stopTracking();

    INFO("liveness probe: observed = " << probeObserved << ", counted allocations = "
                                       << livenessCount
                                       << " (0 means the global operator new/delete replacements "
                                          "are not linked into dsp_systems_tests — every "
                                          "allocation assertion in this case would be vacuous)");
    REQUIRE(probeObserved == 42);
    REQUIRE(livenessCount >= std::size_t{1});

    // -------------------------------------------------------------------------
    // The component. Everything that can allocate happens here, before tracking.
    // -------------------------------------------------------------------------
    HarmonicCloud cloud;
    cloud.prepare(kSampleRate);  // the one non-RT method (FR-003)
    cloud.setSeed(kSeed);
    cloud.setFundamentalHz(110.0f);
    cloud.setRichness(1.0f);  // N(1) = 64 — the fixed capacity, worst case
    cloud.setInharmonicity(0.0f);
    cloud.setSpectralTiltDb(0.0f);
    cloud.setSpectralGravity(0.0f);
    cloud.setMutation(1.0f);                                     // mutation bank live
    cloud.setDriftDepthCents(HarmonicCloud::kMaxDriftCents);      // detune bank live
    cloud.setDriftSmoothness(0.5f);
    cloud.setStereoSpread(0.8f);
    cloud.setEnvelopeOffsetSpread(1.0f);
    cloud.noteOn();

    REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);

    std::array<float, kBlockSize> leftBlock{};
    std::array<float, kBlockSize> rightBlock{};

    // Warm-up OUTSIDE the tracked window: Highway's runtime ISA dispatch inside
    // processMcfBatchSIMD initialises on its first call, and that is a property
    // of the process, not of the steady-state render loop SC-008 measures.
    cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), kBlockSize);

    // -------------------------------------------------------------------------
    // The measurement. No Catch2 macro, no container growth, no formatting.
    // -------------------------------------------------------------------------
    double sumSquares = 0.0;

    TestHelpers::AllocationDetector::instance().startTracking();
    for (std::size_t b = 0; b < kNumBlocks; ++b) {
        const float t = static_cast<float>(b) / static_cast<float>(kNumBlocks - 1);

        // Macro automation at the Phase-7 cadence: one step per block.
        cloud.setRichness(0.5f + (0.5f * t));
        cloud.setInharmonicity(t * HarmonicCloud::kMaxInharmonicity);
        cloud.setSpectralTiltDb(HarmonicCloud::kMinTiltDbPerOct
                                + (t * (HarmonicCloud::kMaxTiltDbPerOct
                                        - HarmonicCloud::kMinTiltDbPerOct)));
        cloud.setMutation(t);
        cloud.setSpectralGravity((2.0f * t) - 1.0f);
        cloud.setFundamentalHz(110.0f + (110.0f * t));

        cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), kBlockSize);

        for (std::size_t i = 0; i < kBlockSize; ++i) {
            sumSquares += static_cast<double>(leftBlock[i]) * static_cast<double>(leftBlock[i]);
            sumSquares += static_cast<double>(rightBlock[i]) * static_cast<double>(rightBlock[i]);
        }
    }
    const std::size_t renderCount = TestHelpers::AllocationDetector::instance().stopTracking();

    // Non-vacuity: a silent (or bailed-out) render loop allocates nothing either.
    const double renderRms =
        std::sqrt(sumSquares / static_cast<double>(2 * kBlockSize * kNumBlocks));
    INFO("render RMS over " << kNumBlocks << " automated blocks = " << renderRms);
    REQUIRE(renderRms > 0.0);
    REQUIRE(cloud.stateFinite());

    INFO("heap allocations across " << kNumBlocks
                                    << " blocks of 512 with macro automation and both drift banks "
                                       "live = "
                                    << renderCount);
    REQUIRE(renderCount == std::size_t{0});
}

// =============================================================================
// SC-009 — a seeded render is reproducible; a different seed is not (FR-005)
// =============================================================================
// Two `HarmonicCloud` instances given the SAME seed, the SAME parameter sequence
// and the SAME block schedule must render the same signal; a different seed must
// render a materially different one.
//
// @par Explicitly NOT a bit-exact digest (roadmap line 486)
// Comparison goes through `render_fingerprint.h` — four aggregate metrics plus
// 32 evenly spaced sample checkpoints, at the measured cross-toolchain
// tolerances `kSampleTolerance = 1.0e-4f` and `kMetricTolerance = 1.0e-5`
// (`render_fingerprint.h:49-52`). An FNV digest over the sample bits would
// demand bit-identical transcendentals from MSVC, GCC and Apple Clang (the last
// of which additionally builds `-ffast-math`) and is guaranteed red on two of
// the three CI legs.
//
// @par Configuration is PINNED so the negative control cannot pass vacuously
// Drift depth, Mutation and stereo spread are each at >= 50 % of their range,
// asserted by `static_assert` below. This is the load-bearing part of the case:
// every one of them is legally 0 by default, and at 0 the seed reaches the
// output through nothing but FR-016's initial phases. With drift 0, mutation 0
// and spread 0 a "different seed" render would still be *similar* enough in
// aggregate that a weak threshold could pass — and an implementation that
// ignored the seed entirely in the drift and mutation banks would never be
// caught. All three live means the seed drives the detune bank (FR-031), the
// mutation bank (FR-072), the FR-021 stereo scatter, the FR-022 drift amounts
// and the FR-016 phases at once.
//
// @par Threshold asymmetry is deliberate
// Same seed: `withinTolerance()`. Different seed: `worstMetricRelativeError >
// 10 x kMetricTolerance` — an order of magnitude clear of the tolerance band,
// not merely "outside it", so the negative clause cannot be satisfied by
// toolchain noise sitting a hair over the bound.
//
// The `reset()`-then-re-render clause of SC-009 is a separate case
// (`HarmonicCloud_ResetReproducesSeededState`) and the 128-lane seed
// distinctness clause is `HarmonicCloud_SeedDerivationIsDistinct`; neither is
// re-attempted here.
// =============================================================================
TEST_CASE("HarmonicCloud_SeededRenderIsReproducible") {
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlockSize = 512;
    constexpr std::size_t kNumBlocks = 128;  // 65536 samples ~ 1.37 s @ 48 kHz
    constexpr std::size_t kTotalSamples = kBlockSize * kNumBlocks;

    constexpr std::uint32_t kSeedA = 0x5C009A01u;
    constexpr std::uint32_t kSeedB = 0x5C009B02u;
    static_assert(kSeedA != kSeedB, "the negative control needs two distinct seeds");

    // ---- The pinned >= 50 %-of-range settings (see the note above).
    constexpr float kDriftCents = 0.8f * HarmonicCloud::kMaxDriftCents;  // 40 of 50
    constexpr float kMutation = 0.75f;                                   // of [0, 1]
    constexpr float kSpread = 0.8f;                                      // of [0, 1]
    static_assert(kDriftCents >= 0.5f * HarmonicCloud::kMaxDriftCents,
                  "drift depth must be >= 50 % of range or the seed cannot move the detune bank");
    static_assert(kMutation >= 0.5f,
                  "Mutation must be >= 50 % of range or the seed cannot move the mutation bank");
    static_assert(kSpread >= 0.5f,
                  "stereo spread must be >= 50 % of range or the FR-021 scatter stays inaudible");

    /// SC-009's negative-control threshold: an order of magnitude clear of the
    /// tolerance band, so "different" cannot be produced by toolchain noise.
    constexpr double kDifferentSeedMinMetricError = 10.0 * TestUtils::kMetricTolerance;

    // Every render below runs this EXACT call sequence and this EXACT block
    // schedule; only the seed differs.
    const auto renderWithSeed = [&](std::uint32_t seed, std::vector<float>& left,
                                    std::vector<float>& right) {
        HarmonicCloud cloud;
        cloud.prepare(kSampleRate);
        cloud.setSeed(seed);
        cloud.setFundamentalHz(110.0f);
        cloud.setRichness(1.0f);  // N(1) = 64
        cloud.setInharmonicity(0.0f);
        cloud.setSpectralTiltDb(0.0f);
        cloud.setSpectralGravity(0.0f);
        cloud.setMutation(kMutation);
        cloud.setDriftDepthCents(kDriftCents);
        cloud.setDriftSmoothness(0.5f);
        cloud.setStereoSpread(kSpread);
        cloud.setAttackTimeSec(HarmonicCloud::kMinAttackSec);
        cloud.setEnvelopeOffsetSpread(0.0f);
        cloud.noteOn();

        REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);
        REQUIRE(cloud.getSeed() == seed);
        REQUIRE(cloud.getDriftDepthCents() == kDriftCents);
        REQUIRE(cloud.getMutation() == kMutation);
        REQUIRE(cloud.getStereoSpread() == kSpread);

        std::array<float, kBlockSize> leftBlock{};
        std::array<float, kBlockSize> rightBlock{};
        for (std::size_t b = 0; b < kNumBlocks; ++b) {
            cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), kBlockSize);
            for (std::size_t i = 0; i < kBlockSize; ++i) {
                left[(b * kBlockSize) + i] = leftBlock[i];
                right[(b * kBlockSize) + i] = rightBlock[i];
            }
        }
    };

    std::vector<float> leftA(kTotalSamples, 0.0f);
    std::vector<float> rightA(kTotalSamples, 0.0f);
    std::vector<float> leftA2(kTotalSamples, 0.0f);
    std::vector<float> rightA2(kTotalSamples, 0.0f);
    std::vector<float> leftB(kTotalSamples, 0.0f);
    std::vector<float> rightB(kTotalSamples, 0.0f);

    renderWithSeed(kSeedA, leftA, rightA);
    renderWithSeed(kSeedA, leftA2, rightA2);
    renderWithSeed(kSeedB, leftB, rightB);

    const TestUtils::RenderFingerprint fpLeftA =
        TestUtils::fingerprintRender(std::span<const float>(leftA));
    const TestUtils::RenderFingerprint fpRightA =
        TestUtils::fingerprintRender(std::span<const float>(rightA));
    const TestUtils::RenderFingerprint fpLeftA2 =
        TestUtils::fingerprintRender(std::span<const float>(leftA2));
    const TestUtils::RenderFingerprint fpRightA2 =
        TestUtils::fingerprintRender(std::span<const float>(rightA2));
    const TestUtils::RenderFingerprint fpLeftB =
        TestUtils::fingerprintRender(std::span<const float>(leftB));
    const TestUtils::RenderFingerprint fpRightB =
        TestUtils::fingerprintRender(std::span<const float>(rightB));

    // ---- Non-vacuity: a silent render makes every fingerprint agree with every
    // other one, so the positive clause below would pass on a dead component.
    INFO("seed A: left RMS = " << fpLeftA.rms << ", right RMS = " << fpRightA.rms
                               << "; seed B: left RMS = " << fpLeftB.rms << ", right RMS = "
                               << fpRightB.rms);
    REQUIRE(fpLeftA.rms > 0.0);
    REQUIRE(fpRightA.rms > 0.0);
    REQUIRE(fpLeftB.rms > 0.0);
    REQUIRE(fpRightB.rms > 0.0);

    // -------------------------------------------------------------------------
    // Clause 1 (positive) — same seed, same call sequence, same block schedule.
    // -------------------------------------------------------------------------
    const TestUtils::FingerprintComparison sameLeft =
        TestUtils::compareFingerprints(fpLeftA2, fpLeftA);
    INFO("same seed, left: worst metric relative error = " << sameLeft.worstMetricRelativeError
                                                           << ", worst sample error = "
                                                           << sameLeft.worstSampleError << " ("
                                                           << sameLeft.detail << ")");
    REQUIRE(sameLeft.withinTolerance());

    const TestUtils::FingerprintComparison sameRight =
        TestUtils::compareFingerprints(fpRightA2, fpRightA);
    INFO("same seed, right: worst metric relative error = " << sameRight.worstMetricRelativeError
                                                            << ", worst sample error = "
                                                            << sameRight.worstSampleError << " ("
                                                            << sameRight.detail << ")");
    REQUIRE(sameRight.withinTolerance());

    // -------------------------------------------------------------------------
    // Clause 2 (negative control) — a different seed must move the render by an
    // order of magnitude more than the tolerance band, on BOTH channels.
    // -------------------------------------------------------------------------
    const TestUtils::FingerprintComparison diffLeft =
        TestUtils::compareFingerprints(fpLeftB, fpLeftA);
    INFO("different seed, left: worst metric relative error = "
         << diffLeft.worstMetricRelativeError << " (" << diffLeft.detail << "), required > "
         << kDifferentSeedMinMetricError);
    REQUIRE(diffLeft.worstMetricRelativeError > kDifferentSeedMinMetricError);

    const TestUtils::FingerprintComparison diffRight =
        TestUtils::compareFingerprints(fpRightB, fpRightA);
    INFO("different seed, right: worst metric relative error = "
         << diffRight.worstMetricRelativeError << " (" << diffRight.detail << "), required > "
         << kDifferentSeedMinMetricError);
    REQUIRE(diffRight.worstMetricRelativeError > kDifferentSeedMinMetricError);
}
