// ==============================================================================
// Layer 2: Processor Tests - SplineTrajectory (Seraphis Life Modulator)
// ==============================================================================
// Spec:  specs/seraphis-phase1-life-modulators/spec.md
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 5)
// Covers: FR-001..FR-006, FR-051..FR-054, SC-001, SC-002, SC-003(a), SC-004,
//         SC-005, SC-006.
//
// NOTE ON ALLOCATION TRACKING (single-owner rule):
//   The global operator new/delete replacements for the `dsp_processors_tests`
//   binary are owned by unit/processors/brownian_drift_test.cpp (see its header
//   comment). This TU therefore includes ONLY <allocation_detector.h> and
//   relies on those replacements -- pulling in
//   <allocation_operator_overrides.h> from a second TU is a duplicate-symbol
//   link error.
//
// Statistical thresholds are MEASURED tolerances (estimator standard error /
// across-seed realization spread), never bit-exact float goldens. The only
// exact-equality assertions are same-build determinism comparisons (SC-004),
// which compare one compiler's output against itself.
// ==============================================================================

#include <krate/dsp/processors/spline_trajectory.h>

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Krate::DSP;

namespace {

constexpr double kSr48 = 48000.0;
constexpr size_t kBlock = 512;

/// Finite check WITHOUT std::isnan: macOS CI builds with -ffast-math, which
/// folds std::isnan to false. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// Advance `seconds` of wall clock in `blockSize` blocks, capturing
/// getCurrentValue() once every `blocksPerCapture` blocks.
[[nodiscard]] std::vector<float> renderDecimated(SplineTrajectory& spline,
                                                 double seconds,
                                                 double sampleRate,
                                                 size_t blockSize,
                                                 size_t blocksPerCapture) {
    const auto totalBlocks =
        static_cast<size_t>(seconds * sampleRate / static_cast<double>(blockSize));
    std::vector<float> out;
    out.reserve(totalBlocks / blocksPerCapture + 1);
    for (size_t b = 0; b < totalBlocks; ++b) {
        spline.processBlock(blockSize);
        if ((b % blocksPerCapture) == 0) {
            out.push_back(spline.getCurrentValue());
        }
    }
    return out;
}

/// Blocks per capture such that roughly `targetPerInterval` captures land in one
/// waypoint interval. Keeps the autocorrelation estimate cheap while leaving the
/// 1/e crossing resolved to ~1 % of the interval.
[[nodiscard]] size_t blocksPerCaptureFor(double intervalSeconds,
                                         double sampleRate,
                                         size_t blockSize,
                                         double targetPerInterval) {
    const auto n = static_cast<size_t>(intervalSeconds * sampleRate /
                                       (targetPerInterval * static_cast<double>(blockSize)));
    return n < size_t{1} ? size_t{1} : n;
}

[[nodiscard]] double meanOf(const std::vector<float>& v) {
    if (v.empty()) return 0.0;
    double acc = 0.0;
    for (float x : v) acc += static_cast<double>(x);
    return acc / static_cast<double>(v.size());
}

[[nodiscard]] double rmsOf(const std::vector<float>& v) {
    if (v.empty()) return 0.0;
    double acc = 0.0;
    for (float x : v) {
        const double d = static_cast<double>(x);
        acc += d * d;
    }
    return std::sqrt(acc / static_cast<double>(v.size()));
}

[[nodiscard]] double meanOfD(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double acc = 0.0;
    for (double x : v) acc += x;
    return acc / static_cast<double>(v.size());
}

[[nodiscard]] double stdDevOfD(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    const double m = meanOfD(v);
    double acc = 0.0;
    for (double x : v) acc += (x - m) * (x - m);
    return std::sqrt(acc / static_cast<double>(v.size() - 1));
}

/// Mean-detrended, r[0]-normalized autocorrelation (biased estimator: divide by
/// n, not n-k). The taper damps large-lag estimator noise and applies equally to
/// both sample rates in the SC-005 comparison, so it cannot bias it.
[[nodiscard]] std::vector<double> normalizedAutocorr(const std::vector<float>& x,
                                                     size_t maxLag) {
    const size_t n = x.size();
    if (n < 2) return {};

    std::vector<double> r(std::min(maxLag, n - 1) + 1, 0.0);
    const double mean = meanOf(x);
    std::vector<double> y(n);
    for (size_t i = 0; i < n; ++i) y[i] = static_cast<double>(x[i]) - mean;

    for (size_t k = 0; k < r.size(); ++k) {
        double acc = 0.0;
        for (size_t i = 0; i + k < n; ++i) acc += y[i] * y[i + k];
        r[k] = acc / static_cast<double>(n);
    }
    if (r[0] > 0.0) {
        const double inv = 1.0 / r[0];
        for (double& v : r) v *= inv;
    }
    return r;
}

/// First lag at which the normalized autocorrelation drops below 1/e.
/// Returns r.size() when no crossing exists inside the computed lag window.
[[nodiscard]] size_t firstOneOverECrossing(const std::vector<double>& r) {
    const double threshold = 1.0 / std::exp(1.0);
    for (size_t k = 1; k < r.size(); ++k) {
        if (r[k] < threshold) return k;
    }
    return r.size();
}

}  // namespace

// =============================================================================
// SC-001 / FR-054 - Boundedness at every parameter extreme
// =============================================================================
//
// The bound is structural, not empirical: waypoints are drawn in
// [-kWaypointMax, +kWaypointMax] = [-0.8, 0.8], and the uniform Catmull-Rom
// Lebesgue constant is max_u sum_i |b_i(u)| = 1.25 (attained at u = 0.5:
// 0.0625 + 0.5625 + 0.5625 + 0.0625). Hence |q(u)| <= 1.25 * 0.8 = 1.0 for
// every waypoint sequence, and out = depth * q(u) with depth <= 1 stays inside
// [-1, +1] with no clamp doing any work.
// =============================================================================

TEST_CASE("SplineTrajectory_NeverExceedsRange", "[processors][spline_trajectory][seraphis]") {
    const std::array<double, 2> intervalGrid{
        static_cast<double>(SplineTrajectory::kMinInterval),   // 0.5 s
        static_cast<double>(SplineTrajectory::kMaxInterval)};  // 30 s
    const std::array<float, 2> depthGrid{0.0f, 1.0f};

    size_t violations = 0;
    size_t nonFinite = 0;

    for (double interval : intervalGrid) {
        for (float depth : depthGrid) {
            SplineTrajectory spline;
            spline.setSeed(2718u);
            spline.setWaypointInterval(interval);
            spline.setDepth(depth);
            spline.prepare(kSr48);

            const auto range = spline.getSourceRange();
            REQUIRE(range.first == -1.0f);
            REQUIRE(range.second == 1.0f);

            // Render horizon = 3x the longest configured period at this setting
            // (spec.md SC-001). The slowest configuration is the kMaxInterval
            // 30 s waypoint spacing, so the horizon is 3 * 30 = 90 s, rendered
            // PER SAMPLE at every extreme (>= 180 waypoints at the 0.5 s
            // spacing, 3 waypoint intervals at the 30 s spacing).
            const auto numSamples = static_cast<size_t>(
                3.0 * static_cast<double>(SplineTrajectory::kMaxInterval) * kSr48);
            for (size_t i = 0; i < numSamples; ++i) {
                spline.process();
                const float v = spline.getCurrentValue();
                if (!isFiniteValue(v)) ++nonFinite;
                if (v < range.first || v > range.second) ++violations;
            }
        }
    }

    REQUIRE(nonFinite == 0u);
    REQUIRE(violations == 0u);
}

// =============================================================================
// SC-002 - Bounded per-sample slew at the worst-case (max-slew) configuration
// =============================================================================

TEST_CASE("SplineTrajectory_MaxSlewBounded", "[processors][spline_trajectory][seraphis]") {
    // Worst case per spec.md:261 -- SHORTEST waypoint spacing (fastest playhead
    // advance) at MAXIMUM depth (full-scale swing).
    SplineTrajectory spline;
    spline.setSeed(31337u);
    spline.setWaypointInterval(static_cast<double>(SplineTrajectory::kMinInterval));
    spline.setDepth(1.0f);
    spline.prepare(kSr48);

    float prev = spline.getCurrentValue();
    double maxDelta = 0.0;

    // 5 s = 10 waypoint intervals at the 0.5 s spacing, so several joins and
    // several full segments are traversed per sample.
    const auto numSamples = static_cast<size_t>(5.0 * kSr48);
    for (size_t i = 0; i < numSamples; ++i) {
        spline.process();
        const float cur = spline.getCurrentValue();
        maxDelta = std::max(maxDelta,
                            std::abs(static_cast<double>(cur) - static_cast<double>(prev)));
        prev = cur;
    }

    // Threshold: 1e-3 of the source-range span (span = 2) = 2.0e-3 absolute.
    // Analytic bound (see the header): |dq/dt| <= (segment amplitude / interval)
    // * ~1.5 <= (2 / 0.5) * 1.5 = 6 per second, so per sample at 48 kHz the step
    // is <= 6/48000 = 1.25e-4, ~16x inside the threshold.
    REQUIRE(maxDelta <= 2.0e-3);
}

// =============================================================================
// FR-053 - C1 continuity across waypoint joins
// =============================================================================
//
// A first-difference (SC-002) metric cannot see a derivative kink: the value is
// continuous either way. The SECOND difference can. For a genuinely C1 curve the
// second difference is O(h^2 * q''), the same order on both sides of a join
// (Catmull-Rom is C1 but not C2, so q'' merely steps -- it does not spike). A
// C0-but-not-C1 implementation (wrong tangent, or a linear interp at the join)
// makes the slope jump by dS, producing a second difference of O(h * dS) -- one
// order LARGER in h, i.e. a spike at the join.
//
// The second difference is formed on a DECIMATED grid (stride kStride) rather
// than per sample: at 48 kHz with a 0.5 s spacing, du = 4.17e-5, so a per-sample
// second difference is O(du^2 * q'') ~ 5e-9 -- far below float quantization
// (~1e-7) and therefore pure noise. With h = kStride * du = 0.01 the curvature
// term is ~3e-4, roughly 2500x above the float noise floor, while a slope kink
// would land near 1e-2.
// =============================================================================

TEST_CASE("SplineTrajectory_C1AtWaypointJoins", "[processors][spline_trajectory][seraphis]") {
    constexpr double kInterval = 0.5;                                 // seconds
    constexpr size_t kSamplesPerSegment = 24000;                      // 0.5 s @ 48 kHz
    constexpr size_t kSegments = 5;
    constexpr size_t kNumSamples = kSamplesPerSegment * kSegments;    // 120000
    constexpr size_t kStride = 240;                                   // 100 points/segment
    constexpr size_t kPointsPerSegment = kSamplesPerSegment / kStride;

    SplineTrajectory spline;
    spline.setSeed(90210u);
    spline.setWaypointInterval(kInterval);
    spline.setDepth(1.0f);
    spline.prepare(kSr48);

    std::vector<float> out;
    out.reserve(kNumSamples);
    for (size_t i = 0; i < kNumSamples; ++i) {
        spline.process();
        out.push_back(spline.getCurrentValue());
    }

    const size_t numPoints = kNumSamples / kStride;
    std::vector<float> s(numPoints);
    for (size_t i = 0; i < numPoints; ++i) s[i] = out[i * kStride];

    double interiorMax = 0.0;
    double straddleMax = 0.0;

    for (size_t i = 2; i < numPoints; ++i) {
        const double d2 = static_cast<double>(s[i]) - (2.0 * static_cast<double>(s[i - 1])) +
                          static_cast<double>(s[i - 2]);

        // A triple straddles a join when a join index lies in [i-2, i]. The join
        // can land one sample either side of the nominal boundary (u_ crosses 1
        // after ~kSamplesPerSegment accumulated additions), which the 2-point
        // window absorbs.
        bool straddles = false;
        for (size_t j = 1; j * kPointsPerSegment < numPoints; ++j) {
            const size_t join = j * kPointsPerSegment;
            if (join <= i && join + 2 >= i) {
                straddles = true;
                break;
            }
        }

        if (straddles) {
            straddleMax = std::max(straddleMax, std::abs(d2));
        } else {
            interiorMax = std::max(interiorMax, std::abs(d2));
        }
    }

    // Sanity: real curvature is being measured, not float quantization noise
    // (which sits near 2e-7 at this amplitude).
    REQUIRE(interiorMax > 1.0e-5);

    // No join spike. Catmull-Rom's |q''| is largest at the segment ENDS, so a
    // correct implementation still lands the straddling triples near the global
    // maximum -- the ratio is ~1, not ~0. 5x leaves room for that while a C0
    // kink (order h*dS ~ 1e-2 against an interior ~1e-3) is rejected.
    REQUIRE(straddleMax <= 5.0 * interiorMax);
}

// =============================================================================
// SC-003(a) - Decorrelation time tracks the waypoint spacing
// =============================================================================

TEST_CASE("SplineTrajectory_AutocorrTimeTracksSpacing",
          "[processors][spline_trajectory][seraphis]") {
    const std::array<double, 3> intervals{1.0, 4.0, 16.0};
    std::array<double, 3> crossingSeconds{};

    for (size_t i = 0; i < intervals.size(); ++i) {
        const double interval = intervals[i];

        SplineTrajectory spline;
        spline.setSeed(9001u + static_cast<std::uint32_t>(i));
        spline.setWaypointInterval(interval);
        spline.setDepth(1.0f);
        spline.prepare(kSr48);

        // 100 x the interval: far beyond the ">= 10x the longest interval" floor,
        // giving ~100 independent waypoints so the large-lag autocorrelation is
        // stable enough for the no-periodic-peak assertion below.
        const double seconds = 100.0 * interval;
        const size_t blocksPerCapture = blocksPerCaptureFor(interval, kSr48, kBlock, 100.0);
        const double captureInterval =
            static_cast<double>(blocksPerCapture * kBlock) / kSr48;

        const auto trace = renderDecimated(spline, seconds, kSr48, kBlock, blocksPerCapture);
        REQUIRE(trace.size() > 1000u);

        const auto maxLag = static_cast<size_t>(6.0 * interval / captureInterval) + 4;
        const auto r = normalizedAutocorr(trace, maxLag);
        const size_t crossing = firstOneOverECrossing(r);

        REQUIRE(crossing < r.size());  // a 1/e crossing exists
        REQUIRE(crossing > 4u);        // >> 1 capture: this is NOT white noise
        crossingSeconds[i] = static_cast<double>(crossing) * captureInterval;

        // NOT an LFO: once decorrelated the autocorrelation does not recover
        // toward +1. Waypoints are i.i.d., so beyond ~1.5 intervals only
        // estimator noise (~1/sqrt(100) = 0.1) remains.
        double maxAfter = 0.0;
        for (size_t k = 2 * crossing; k < r.size(); ++k) {
            maxAfter = std::max(maxAfter, r[k]);
        }
        REQUIRE(maxAfter < 0.6);
    }

    // Strictly increasing decorrelation time with waypoint spacing (FR-052).
    REQUIRE(crossingSeconds[1] > crossingSeconds[0]);
    REQUIRE(crossingSeconds[2] > crossingSeconds[1]);
}

// =============================================================================
// SC-004 - Seeded determinism and reset() rewind
// =============================================================================

TEST_CASE("SplineTrajectory_SeededDeterminism", "[processors][spline_trajectory][seraphis]") {
    constexpr size_t kNumBlocks = 400;

    auto render = [&](std::uint32_t seed) {
        SplineTrajectory spline;
        spline.setSeed(seed);
        spline.setWaypointInterval(1.0);
        spline.setDepth(1.0f);
        spline.prepare(kSr48);
        std::vector<float> out;
        out.reserve(kNumBlocks);
        for (size_t b = 0; b < kNumBlocks; ++b) {
            spline.processBlock(kBlock);
            out.push_back(spline.getCurrentValue());
        }
        return out;
    };

    const auto a = render(1234u);
    const auto b = render(1234u);
    REQUIRE(a.size() == kNumBlocks);
    // Same build, same seed, same call sequence -> exact equality.
    REQUIRE(a == b);

    const auto c = render(9999u);
    REQUIRE(c.size() == kNumBlocks);
    REQUIRE(a != c);
}

TEST_CASE("SplineTrajectory_ResetRewindsToSeed", "[processors][spline_trajectory][seraphis]") {
    constexpr size_t kNumBlocks = 400;

    SplineTrajectory spline;
    spline.setSeed(4711u);
    spline.setWaypointInterval(0.75);
    spline.setDepth(0.8f);
    spline.prepare(kSr48);

    std::vector<float> first;
    first.reserve(kNumBlocks);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        spline.processBlock(kBlock);
        first.push_back(spline.getCurrentValue());
    }

    spline.reset();

    std::vector<float> second;
    second.reserve(kNumBlocks);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        spline.processBlock(kBlock);
        second.push_back(spline.getCurrentValue());
    }

    REQUIRE(first == second);
}

// =============================================================================
// SC-005 (option b) - Sample-rate invariance, distributional across seeds
// =============================================================================

TEST_CASE("SplineTrajectory_SampleRateInvariant", "[processors][spline_trajectory][seraphis]") {
    // Per plan section 0.3, SplineTrajectory uses SC-005 option (b): compare
    // distributional statistics averaged over >= 8 seeds, with the tolerance
    // derived from the measured across-seed realization spread rather than a
    // flat guess. (In practice the two rates track closely here because a
    // waypoint is drawn once per wall-clock interval regardless of sample rate;
    // option (b) is still the correct, conservative form of the check.)
    constexpr size_t kNumSeeds = 8;
    constexpr double kSeconds = 120.0;
    constexpr double kInterval = 2.0;  // ~60 independent waypoints per render

    std::vector<double> rms44, rms96, tau44, tau96, mean44, mean96;

    auto measure = [&](double sampleRate, std::uint32_t seed,
                       double& rmsOut, double& tauOut, double& meanOut) {
        SplineTrajectory spline;
        spline.setSeed(seed);
        spline.setWaypointInterval(kInterval);
        spline.setDepth(1.0f);
        spline.prepare(sampleRate);

        const size_t blocksPerCapture =
            blocksPerCaptureFor(kInterval, sampleRate, kBlock, 100.0);
        const double captureInterval =
            static_cast<double>(blocksPerCapture * kBlock) / sampleRate;

        const auto trace =
            renderDecimated(spline, kSeconds, sampleRate, kBlock, blocksPerCapture);
        REQUIRE(trace.size() > 1000u);

        rmsOut = rmsOf(trace);
        meanOut = meanOf(trace);

        const auto maxLag = static_cast<size_t>(4.0 * kInterval / captureInterval) + 4;
        const auto r = normalizedAutocorr(trace, maxLag);
        const size_t crossing = firstOneOverECrossing(r);
        REQUIRE(crossing < r.size());
        tauOut = static_cast<double>(crossing) * captureInterval;
    };

    for (size_t s = 0; s < kNumSeeds; ++s) {
        const auto seed = static_cast<std::uint32_t>(1000u + 137u * s);
        double rmsA = 0.0, tauA = 0.0, meanA = 0.0;
        double rmsB = 0.0, tauB = 0.0, meanB = 0.0;
        measure(44100.0, seed, rmsA, tauA, meanA);
        measure(96000.0, seed, rmsB, tauB, meanB);
        rms44.push_back(rmsA);
        tau44.push_back(tauA);
        mean44.push_back(meanA);
        rms96.push_back(rmsB);
        tau96.push_back(tauB);
        mean96.push_back(meanB);
    }

    // Tolerance = max(starting point, 3 standard errors of the DIFFERENCE of the
    // two across-seed means). The SE of the difference of two independent means
    // is sqrt(2) x the SE of one -- hence the sqrt(2) factor. The measured spread
    // drives the bound; the floor only applies when it is unusually small.
    const double kSeErrorFactor =
        3.0 * std::sqrt(2.0) / std::sqrt(static_cast<double>(kNumSeeds));

    const double meanRms44 = meanOfD(rms44);
    const double meanRms96 = meanOfD(rms96);
    REQUIRE(meanRms44 > 0.05);

    const double rmsSpread = stdDevOfD(rms44) / meanRms44;
    const double rmsTol = std::max(0.05, kSeErrorFactor * rmsSpread);
    REQUIRE(std::abs(meanRms44 - meanRms96) / meanRms44 <= rmsTol);

    const double meanTau44 = meanOfD(tau44);
    const double meanTau96 = meanOfD(tau96);
    REQUIRE(meanTau44 > 0.0);
    const double tauSpread = stdDevOfD(tau44) / meanTau44;
    const double tauTol = std::max(0.10, kSeErrorFactor * tauSpread);
    REQUIRE(std::abs(meanTau44 - meanTau96) / meanTau44 <= tauTol);

    // The source is ~zero-mean, so a RELATIVE mean comparison divides by ~0 and
    // is undefined. Compare ABSOLUTELY, in source-range-span units (span = 2):
    // 0.10 absolute = 5 % of span.
    REQUIRE(std::abs(meanOfD(mean44) - meanOfD(mean96)) <= 0.10);
}

// =============================================================================
// SC-006 - Allocation-free steady state
// =============================================================================

TEST_CASE("SplineTrajectory_NoAllocInProcess", "[processors][spline_trajectory][seraphis]") {
    SplineTrajectory spline;
    spline.setSeed(64u);
    spline.setWaypointInterval(1.5);
    spline.setDepth(1.0f);
    spline.prepare(kSr48);
    spline.processBlock(kBlock);  // warm-up OUTSIDE the tracking scope

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    for (int i = 0; i < 500; ++i) {
        spline.processBlock(kBlock);
    }
    for (int i = 0; i < 4096; ++i) {
        spline.process();
    }
    const size_t allocations = detector.stopTracking();

    REQUIRE(allocations == 0u);
}

// =============================================================================
// FR-006 - getSourceRange() is fixed at polarity full scale
// =============================================================================

TEST_CASE("SplineTrajectory_SourceRangeIndependentOfDepth",
          "[processors][spline_trajectory][seraphis]") {
    SplineTrajectory spline;
    spline.prepare(kSr48);

    for (float depth : {0.0f, 0.5f, 1.0f}) {
        spline.setDepth(depth);
        const auto range = spline.getSourceRange();
        REQUIRE(range.first == -1.0f);
        REQUIRE(range.second == 1.0f);
    }
}

// =============================================================================
// FR-004 - Output is well defined after prepare() and after reset()
// =============================================================================

TEST_CASE("SplineTrajectory_OutputDefinedAfterPrepare",
          "[processors][spline_trajectory][seraphis]") {
    SplineTrajectory spline;
    spline.setSeed(11u);
    spline.setWaypointInterval(3.0);
    spline.setDepth(0.6f);
    spline.prepare(kSr48);

    const auto range = spline.getSourceRange();

    {
        const float v = spline.getCurrentValue();  // no intervening advance
        REQUIRE(isFiniteValue(v));
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    }

    for (int i = 0; i < 100; ++i) spline.processBlock(kBlock);
    spline.reset();

    {
        const float v = spline.getCurrentValue();  // no intervening advance
        REQUIRE(isFiniteValue(v));
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    }
}

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("SplineTrajectory_EdgeCases", "[processors][spline_trajectory][seraphis]") {
    SECTION("degenerate sample rates neither hang nor poison the output") {
        // RT safety: prepare() floors the sample rate at 1 Hz so the playhead
        // increment stays finite. This section is the shared regression cover
        // that every life modulator now carries.
        for (double sr : {0.0, -48000.0, 0.25}) {
            SplineTrajectory spline;
            spline.setSeed(1234u);
            spline.setWaypointInterval(
                static_cast<double>(SplineTrajectory::kMinInterval));
            spline.setDepth(1.0f);
            spline.prepare(sr);

            bool finite = true;
            bool inRange = true;
            const auto observe = [&](float v) {
                if (!isFiniteValue(v)) finite = false;
                if (v < -1.0f || v > 1.0f) inRange = false;
            };

            for (int b = 0; b < 8; ++b) {
                spline.processBlock(kBlock);
                observe(spline.getCurrentValue());
            }
            for (int i = 0; i < 2000; ++i) {
                spline.process();
                observe(spline.getCurrentValue());
            }

            REQUIRE(finite);
            REQUIRE(inRange);
        }
    }

    SECTION("processBlock(0) is a no-op") {
        SplineTrajectory withZeros;
        SplineTrajectory reference;
        for (SplineTrajectory* t : {&withZeros, &reference}) {
            t->setSeed(555u);
            t->setWaypointInterval(1.0);
            t->setDepth(1.0f);
            t->prepare(kSr48);
        }

        for (int b = 0; b < 50; ++b) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
        }

        const float before = withZeros.getCurrentValue();
        for (int i = 0; i < 10; ++i) withZeros.processBlock(0);
        REQUIRE(withZeros.getCurrentValue() == before);

        // State (RNG stream, playhead, waypoint ring) is untouched as well.
        std::vector<float> a, b2;
        for (int i = 0; i < 50; ++i) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
            a.push_back(withZeros.getCurrentValue());
            b2.push_back(reference.getCurrentValue());
        }
        REQUIRE(a == b2);
    }

    SECTION("a block longer than the waypoint interval consumes multiple waypoints") {
        SplineTrajectory spline;
        spline.setSeed(777u);
        spline.setWaypointInterval(static_cast<double>(SplineTrajectory::kMinInterval));
        spline.setDepth(1.0f);
        spline.prepare(kSr48);

        // 0.5 s spacing @ 48 kHz = 24000 samples per waypoint. Each of these
        // blocks consumes ~4.2 waypoints; the last consumes ~417 in one call.
        // If the ring rotation did not loop, this would index past wp_[3].
        std::vector<float> values;
        for (int i = 0; i < 20; ++i) {
            spline.processBlock(100000u);
            values.push_back(spline.getCurrentValue());
        }
        spline.processBlock(10000000u);
        values.push_back(spline.getCurrentValue());

        bool allFinite = true;
        bool inRange = true;
        for (float v : values) {
            if (!isFiniteValue(v)) allFinite = false;
            if (v < -1.0f || v > 1.0f) inRange = false;
        }
        REQUIRE(allFinite);
        REQUIRE(inRange);
        // The trajectory actually moved (fresh waypoints were drawn).
        REQUIRE(rmsOf(values) > 0.01);
    }

    SECTION("waypoint interval is clamped to the documented range") {
        SplineTrajectory spline;
        spline.prepare(kSr48);

        spline.setWaypointInterval(0.001);
        REQUIRE(spline.getWaypointInterval() ==
                static_cast<double>(SplineTrajectory::kMinInterval));

        spline.setWaypointInterval(1000.0);
        REQUIRE(spline.getWaypointInterval() ==
                static_cast<double>(SplineTrajectory::kMaxInterval));
    }

    SECTION("seed 0 is handled safely") {
        SplineTrajectory spline;
        spline.setSeed(0u);  // Xorshift32 substitutes its default seed
        spline.setWaypointInterval(1.0);
        spline.setDepth(1.0f);
        spline.prepare(kSr48);

        const auto trace = renderDecimated(spline, 30.0, kSr48, kBlock, 1u);
        REQUIRE(trace.size() > 100u);

        bool allFinite = true;
        bool inRange = true;
        for (float v : trace) {
            if (!isFiniteValue(v)) allFinite = false;
            if (v < -1.0f || v > 1.0f) inRange = false;
        }
        REQUIRE(allFinite);
        REQUIRE(inRange);
        // The trajectory actually moves (a zero-state xorshift would be stuck).
        REQUIRE(rmsOf(trace) > 0.01);
    }
}
