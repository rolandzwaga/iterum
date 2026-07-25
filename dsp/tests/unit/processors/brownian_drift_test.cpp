// ==============================================================================
// Layer 2: Processor Tests - BrownianDrift (Seraphis Life Modulator)
// ==============================================================================
// Spec:  specs/seraphis-phase1-life-modulators/spec.md
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 1)
// Covers: FR-001..FR-006, FR-011..FR-014, SC-001..SC-006.
//
// NOTE ON ALLOCATION TRACKING (single-owner rule):
//   This translation unit is the ONE owner of the global operator new/delete
//   replacements for the `dsp_processors_tests` binary. Before this file was
//   added, `rg "operator new" dsp/` returned nothing, so there is no ODR
//   collision. Sibling Seraphis modulator tests in this same binary MUST
//   include only <allocation_detector.h> and rely on the replacements defined
//   here -- including <allocation_operator_overrides.h> from a second TU is a
//   duplicate-symbol link error.
//
// Statistical thresholds are MEASURED tolerances (estimator standard error /
// across-seed realization spread), never bit-exact float goldens. The only
// exact-equality assertions are same-build determinism comparisons (SC-004),
// which compare one compiler's output against itself.
// ==============================================================================

#include <krate/dsp/processors/brownian_drift.h>

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>
#include <allocation_operator_overrides.h>

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
[[nodiscard]] std::vector<float> renderDecimated(BrownianDrift& drift,
                                                 double seconds,
                                                 double sampleRate,
                                                 size_t blockSize,
                                                 size_t blocksPerCapture) {
    const auto totalBlocks =
        static_cast<size_t>(seconds * sampleRate / static_cast<double>(blockSize));
    std::vector<float> out;
    out.reserve(totalBlocks / blocksPerCapture + 1);
    for (size_t b = 0; b < totalBlocks; ++b) {
        drift.processBlock(blockSize);
        if ((b % blocksPerCapture) == 0) {
            out.push_back(drift.getCurrentValue());
        }
    }
    return out;
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

/// Mean-detrended, r[0]-normalized autocorrelation using the BIASED estimator
/// (divide by n, not n-k). The biased form tapers with lag, which both damps
/// large-lag estimator noise and -- because the two sample rates in the SC-005
/// comparison cover the same wall-clock span -- applies the SAME taper factor
/// at both rates, so it cannot bias that comparison.
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

/// tau in seconds for a given smoothness, mirroring the header's
/// lerp(kTauMin, kTauMax, smoothness) mapping (used only to size renders).
[[nodiscard]] double tauSecondsFor(float smoothness) {
    return static_cast<double>(BrownianDrift::kTauMin) +
           static_cast<double>(smoothness) *
               (static_cast<double>(BrownianDrift::kTauMax) -
                static_cast<double>(BrownianDrift::kTauMin));
}

}  // namespace

// =============================================================================
// SC-001 / FR-013 - Boundedness at every parameter extreme
// =============================================================================

TEST_CASE("BrownianDrift_NeverExceedsRange", "[processors][brownian_drift][seraphis]") {
    const std::array<float, 2> smoothnessGrid{0.0f, 1.0f};
    const std::array<float, 2> depthGrid{0.0f, 1.0f};
    const std::array<float, 3> meanGrid{-1.0f, 0.0f, 1.0f};

    size_t violations = 0;
    size_t nonFinite = 0;

    for (float smoothness : smoothnessGrid) {
        for (float depth : depthGrid) {
            for (float mean : meanGrid) {
                BrownianDrift drift;
                drift.setSeed(2718u);
                drift.setSmoothness(smoothness);
                drift.setDepth(depth);
                drift.setMean(mean);
                drift.prepare(kSr48);

                const auto range = drift.getSourceRange();
                REQUIRE(range.first == -1.0f);
                REQUIRE(range.second == 1.0f);

                // Render horizon = 3x the longest configured period at this
                // setting (spec.md SC-001). The slowest configuration is
                // smoothness = 1, i.e. relaxation time kTauMax = 30 s, so the
                // horizon is 3 * 30 = 90 s (and hundreds of tau at the fastest
                // setting).
                const double seconds =
                    3.0 * static_cast<double>(BrownianDrift::kTauMax);
                const size_t blocks =
                    static_cast<size_t>(seconds * kSr48 / static_cast<double>(kBlock));
                for (size_t b = 0; b < blocks; ++b) {
                    drift.processBlock(kBlock);
                    const float v = drift.getCurrentValue();
                    if (!isFiniteValue(v)) ++nonFinite;
                    if (v < range.first || v > range.second) ++violations;
                }
            }
        }
    }

    REQUIRE(nonFinite == 0u);
    REQUIRE(violations == 0u);
}

// =============================================================================
// SC-002 - Bounded per-sample slew at the worst-case (max-slew) configuration
// =============================================================================

TEST_CASE("BrownianDrift_MaxSlewBounded", "[processors][brownian_drift][seraphis]") {
    // Worst case per spec.md:258 -- MINIMUM smoothness (least correlated, so the
    // largest per-control-step OU jump) at MAXIMUM depth (full-scale swing).
    BrownianDrift drift;
    drift.setSeed(31337u);
    drift.setSmoothness(0.0f);
    drift.setDepth(1.0f);
    drift.setMean(0.0f);
    drift.prepare(kSr48);

    float prev = drift.getCurrentValue();
    double maxDelta = 0.0;

    const size_t numSamples = static_cast<size_t>(5.0 * kSr48);  // 5 s per-sample
    for (size_t i = 0; i < numSamples; ++i) {
        drift.process();
        const float cur = drift.getCurrentValue();
        maxDelta = std::max(maxDelta,
                            std::abs(static_cast<double>(cur) - static_cast<double>(prev)));
        prev = cur;
    }

    // Threshold: 1e-3 of the source-range span (span = 2) = 2.0e-3 absolute.
    // Analytic bound (see the header's SC-002 proof): the output smoother is a
    // one-pole at kDriftOutputSmoothMs = 150 ms, so the per-sample change is
    // |current - target| * (1 - coeff) <= 2 * 6.94e-4 ~= 1.39e-3 at 48 kHz.
    // Measured on this implementation (g++ -O2): 4.53e-4.
    REQUIRE(maxDelta <= 2.0e-3);
}

// =============================================================================
// SC-003(a) - Decorrelation time tracks the smoothness control
// =============================================================================

TEST_CASE("BrownianDrift_AutocorrTimeTracksSmoothness",
          "[processors][brownian_drift][seraphis]") {
    constexpr size_t kBlocksPerCapture = 8;  // one capture per 4096 samples
    const double captureInterval =
        static_cast<double>(kBlock * kBlocksPerCapture) / kSr48;  // ~85.3 ms

    const std::array<float, 3> settings{0.1f, 0.5f, 0.9f};
    std::array<double, 3> crossingSeconds{};

    for (size_t i = 0; i < settings.size(); ++i) {
        BrownianDrift drift;
        drift.setSeed(9001u + static_cast<std::uint32_t>(i));
        drift.setSmoothness(settings[i]);
        drift.setDepth(1.0f);
        drift.setMean(0.0f);
        drift.prepare(kSr48);

        const double tau = tauSecondsFor(settings[i]);
        // 100x tau: far beyond the ">= 10x the longest tau" floor, giving ~100
        // independent samples so the large-lag autocorrelation estimate is
        // stable enough for the no-secondary-peak assertion below.
        const auto trace = renderDecimated(drift, 100.0 * tau, kSr48, kBlock, kBlocksPerCapture);
        REQUIRE(trace.size() > 100u);

        const auto maxLag = static_cast<size_t>(6.0 * tau / captureInterval) + 4;
        const auto r = normalizedAutocorr(trace, maxLag);
        const size_t crossing = firstOneOverECrossing(r);

        REQUIRE(crossing < r.size());  // a 1/e crossing exists
        REQUIRE(crossing > 4u);        // >> 1 capture: this is NOT white noise
        crossingSeconds[i] = static_cast<double>(crossing) * captureInterval;

        // NOT an LFO: once decorrelated the autocorrelation does not recover.
        // A periodic source would swing back toward +1 near its period.
        // Measured (g++ -O2): 0.213 / 0.128 / 0.137 for the three settings.
        double maxAfter = 0.0;
        for (size_t k = 2 * crossing; k < r.size(); ++k) {
            maxAfter = std::max(maxAfter, r[k]);
        }
        REQUIRE(maxAfter < 0.6);
    }

    // Strictly increasing decorrelation time with smoothness (FR-012).
    // Measured 1/e crossings (g++ -O2): 3.84 s / 12.29 s / 29.10 s for
    // smoothness 0.1 / 0.5 / 0.9 (nominal tau 3.18 / 15.10 / 27.02 s).
    REQUIRE(crossingSeconds[1] > crossingSeconds[0]);
    REQUIRE(crossingSeconds[2] > crossingSeconds[1]);
}

// =============================================================================
// SC-004 - Seeded determinism and reset() rewind
// =============================================================================

TEST_CASE("BrownianDrift_SeededDeterminism", "[processors][brownian_drift][seraphis]") {
    constexpr size_t kNumBlocks = 400;

    auto render = [&](std::uint32_t seed) {
        BrownianDrift drift;
        drift.setSeed(seed);
        drift.setSmoothness(0.2f);
        drift.setDepth(1.0f);
        drift.prepare(kSr48);
        std::vector<float> out;
        out.reserve(kNumBlocks);
        for (size_t b = 0; b < kNumBlocks; ++b) {
            drift.processBlock(kBlock);
            out.push_back(drift.getCurrentValue());
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

TEST_CASE("BrownianDrift_ResetRewindsToSeed", "[processors][brownian_drift][seraphis]") {
    constexpr size_t kNumBlocks = 400;

    BrownianDrift drift;
    drift.setSeed(4711u);
    drift.setSmoothness(0.3f);
    drift.setDepth(0.8f);
    drift.setMean(0.25f);
    drift.prepare(kSr48);

    std::vector<float> first;
    first.reserve(kNumBlocks);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        drift.processBlock(kBlock);
        first.push_back(drift.getCurrentValue());
    }

    drift.reset();

    std::vector<float> second;
    second.reserve(kNumBlocks);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        drift.processBlock(kBlock);
        second.push_back(drift.getCurrentValue());
    }

    REQUIRE(first == second);
}

// =============================================================================
// SC-005 (option b) - Sample-rate invariance, distributional across seeds
// =============================================================================

TEST_CASE("BrownianDrift_SampleRateInvariant", "[processors][brownian_drift][seraphis]") {
    // BrownianDrift draws its RNG on the sample-count control grid, so 44.1 kHz
    // and 96 kHz are different stochastic REALIZATIONS of the same process, not
    // one trajectory resampled. Per spec SC-005 option (b), compare
    // distributional statistics averaged over multiple seeds, with the
    // tolerance derived from the measured across-seed spread.
    constexpr size_t kNumSeeds = 8;
    constexpr double kSeconds = 120.0;
    constexpr float kSmoothness = 0.02f;  // tau ~ 0.796 s -> ~150 independent samples

    std::vector<double> rms44, rms96, tau44, tau96, mean44, mean96;

    auto measure = [&](double sampleRate, std::uint32_t seed,
                       double& rmsOut, double& tauOut, double& meanOut) {
        BrownianDrift drift;
        drift.setSeed(seed);
        drift.setSmoothness(kSmoothness);
        drift.setDepth(1.0f);
        drift.setMean(0.0f);
        drift.prepare(sampleRate);

        const auto trace = renderDecimated(drift, kSeconds, sampleRate, kBlock, 1u);
        REQUIRE(trace.size() > 1000u);

        rmsOut = rmsOf(trace);
        meanOut = meanOf(trace);

        const double captureInterval = static_cast<double>(kBlock) / sampleRate;
        const double tauNominal = tauSecondsFor(kSmoothness);
        const auto maxLag = static_cast<size_t>(6.0 * tauNominal / captureInterval) + 4;
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
    // two across-seed means). The two means are independent realizations, so the
    // SE of their difference is sqrt(2) x the SE of one mean -- hence the
    // sqrt(2) factor. The measured across-seed spread drives the bound; the
    // floor only applies when the realization variance is unusually small.
    const double kSeErrorFactor = 3.0 * std::sqrt(2.0) / std::sqrt(static_cast<double>(kNumSeeds));

    const double meanRms44 = meanOfD(rms44);
    const double meanRms96 = meanOfD(rms96);
    REQUIRE(meanRms44 > 0.05);

    // Measured on this implementation (g++ -O2, 8 seeds, 120 s):
    //   RMS 44.1 kHz = 0.4706, 96 kHz = 0.4613 -> relative difference 1.97 %,
    //   across-seed spread 2.31 % -> bound 5.0 % (floor). ~2.5x headroom.
    const double rmsSpread = stdDevOfD(rms44) / meanRms44;
    const double rmsTol = std::max(0.05, kSeErrorFactor * rmsSpread);
    REQUIRE(std::abs(meanRms44 - meanRms96) / meanRms44 <= rmsTol);

    // Measured: decorrelation time 44.1 kHz = 0.850 s, 96 kHz = 0.747 s
    //   -> relative difference 12.2 %, across-seed spread 13.7 % -> bound 20.5 %.
    // The 1/e crossing is quantized to the block grid, which differs between the
    // two rates, so this estimator is intrinsically noisier than RMS.
    const double meanTau44 = meanOfD(tau44);
    const double meanTau96 = meanOfD(tau96);
    REQUIRE(meanTau44 > 0.0);
    const double tauSpread = stdDevOfD(tau44) / meanTau44;
    const double tauTol = std::max(0.10, kSeErrorFactor * tauSpread);
    REQUIRE(std::abs(meanTau44 - meanTau96) / meanTau44 <= tauTol);

    // The source is ~zero-mean, so a RELATIVE mean comparison divides by ~0 and
    // is undefined. Compare absolutely, in source-range-span units (span = 2):
    // 0.10 absolute = 5 % of span. Measured absolute difference: 0.024.
    REQUIRE(std::abs(meanOfD(mean44) - meanOfD(mean96)) <= 0.10);
}

// =============================================================================
// SC-006 - Allocation-free steady state
// =============================================================================

TEST_CASE("BrownianDrift_NoAllocInProcess", "[processors][brownian_drift][seraphis]") {
    BrownianDrift drift;
    drift.setSeed(64u);
    drift.setSmoothness(0.4f);
    drift.setDepth(1.0f);
    drift.prepare(kSr48);
    drift.processBlock(kBlock);  // warm-up OUTSIDE the tracking scope

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    for (int i = 0; i < 500; ++i) {
        drift.processBlock(kBlock);
    }
    for (int i = 0; i < 4096; ++i) {
        drift.process();
    }
    const size_t allocations = detector.stopTracking();

    REQUIRE(allocations == 0u);
}

// =============================================================================
// FR-006 - getSourceRange() is fixed at polarity full scale
// =============================================================================

TEST_CASE("BrownianDrift_SourceRangeIndependentOfDepth",
          "[processors][brownian_drift][seraphis]") {
    BrownianDrift drift;
    drift.prepare(kSr48);

    for (float depth : {0.0f, 0.5f, 1.0f}) {
        drift.setDepth(depth);
        const auto range = drift.getSourceRange();
        REQUIRE(range.first == -1.0f);
        REQUIRE(range.second == 1.0f);
    }
}

// =============================================================================
// FR-004 - Output is well defined after prepare() and after reset()
// =============================================================================

TEST_CASE("BrownianDrift_OutputDefinedAfterPrepare",
          "[processors][brownian_drift][seraphis]") {
    BrownianDrift drift;
    drift.setSeed(11u);
    drift.setSmoothness(0.7f);
    drift.setDepth(0.6f);
    drift.setMean(-0.5f);
    drift.prepare(kSr48);

    const auto range = drift.getSourceRange();

    {
        const float v = drift.getCurrentValue();  // no intervening advance
        REQUIRE(isFiniteValue(v));
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    }

    for (int i = 0; i < 100; ++i) drift.processBlock(kBlock);
    drift.reset();

    {
        const float v = drift.getCurrentValue();  // no intervening advance
        REQUIRE(isFiniteValue(v));
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    }
}

// =============================================================================
// FR-011 - Mean reversion pulls the walk toward the configured mean
// =============================================================================

TEST_CASE("BrownianDrift_RevertsToConfiguredMean",
          "[processors][brownian_drift][seraphis]") {
    // depth 0.5 keeps depth*x roughly 3 sigma inside the +/-1 clamp, so the
    // long-run average is an unbiased estimate of depth*mean.
    constexpr float kDepth = 0.5f;
    constexpr float kSmoothness = 0.05f;  // tau ~ 1.69 s
    constexpr double kSeconds = 400.0;    // ~237 independent samples

    auto averageFor = [&](float mean) {
        BrownianDrift drift;
        drift.setSeed(24680u);
        drift.setSmoothness(kSmoothness);
        drift.setDepth(kDepth);
        drift.setMean(mean);
        drift.prepare(kSr48);
        return meanOf(renderDecimated(drift, kSeconds, kSr48, kBlock, 1u));
    };

    // Tolerance 0.08 ~= 5 standard errors of the mean
    // (sigma = kInternalStd * kDepth = 0.25, n_independent ~ 237 -> SE ~ 0.016).
    // Measured realization error on this implementation (g++ -O2): 0.025 for
    // both cases -> ~3x headroom. A stubbed setMean (fixed-0 OU target) would
    // land at ~0.0 for the first case, an error of 0.20 -- comfortably rejected.
    const double avgOffset = averageFor(0.4f);
    REQUIRE(std::abs(avgOffset - static_cast<double>(kDepth) * 0.4) <= 0.08);

    const double avgZero = averageFor(0.0f);
    REQUIRE(std::abs(avgZero) <= 0.08);
}

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("BrownianDrift_EdgeCases", "[processors][brownian_drift][seraphis]") {
    SECTION("degenerate sample rates neither hang nor poison the output") {
        // RT safety: prepare(0) made controlDtSeconds_ non-finite, which feeds
        // exp()/sqrt() in updateCoefficients() and the smoother configuration.
        // prepare() floors the sample rate at 1 Hz. Reaching the assertions at
        // all is half of what this section proves.
        for (double sr : {0.0, -48000.0, 0.25}) {
            BrownianDrift drift;
            drift.setSeed(1234u);
            drift.setSmoothness(0.0f);
            drift.setDepth(1.0f);
            drift.setMean(1.0f);
            drift.prepare(sr);

            bool finite = true;
            bool inRange = true;
            const auto observe = [&](float v) {
                if (!isFiniteValue(v)) finite = false;
                if (v < -1.0f || v > 1.0f) inRange = false;
            };

            for (int b = 0; b < 8; ++b) {
                drift.processBlock(kBlock);
                observe(drift.getCurrentValue());
            }
            for (int i = 0; i < 2000; ++i) {
                drift.process();
                observe(drift.getCurrentValue());
            }

            REQUIRE(finite);
            REQUIRE(inRange);
        }
    }

    SECTION("processBlock(0) is a no-op") {
        BrownianDrift withZeros;
        BrownianDrift reference;
        for (BrownianDrift* d : {&withZeros, &reference}) {
            d->setSeed(555u);
            d->setSmoothness(0.3f);
            d->setDepth(1.0f);
            d->prepare(kSr48);
        }

        for (int b = 0; b < 50; ++b) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
        }

        const float before = withZeros.getCurrentValue();
        for (int i = 0; i < 10; ++i) withZeros.processBlock(0);
        REQUIRE(withZeros.getCurrentValue() == before);

        // State (RNG stream, walk, smoother) is untouched as well.
        std::vector<float> a, b2;
        for (int i = 0; i < 50; ++i) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
            a.push_back(withZeros.getCurrentValue());
            b2.push_back(reference.getCurrentValue());
        }
        REQUIRE(a == b2);
    }

    SECTION("seed 0 is handled safely") {
        BrownianDrift drift;
        drift.setSeed(0u);  // Xorshift32 substitutes its default seed
        drift.setSmoothness(0.1f);
        drift.setDepth(1.0f);
        drift.prepare(kSr48);

        const auto trace = renderDecimated(drift, 30.0, kSr48, kBlock, 1u);
        REQUIRE(trace.size() > 100u);

        bool allFinite = true;
        bool inRange = true;
        for (float v : trace) {
            if (!isFiniteValue(v)) allFinite = false;
            if (v < -1.0f || v > 1.0f) inRange = false;
        }
        REQUIRE(allFinite);
        REQUIRE(inRange);
        // The walk actually moves (a zero-state xorshift would be stuck at 0).
        REQUIRE(rmsOf(trace) > 0.01);
    }
}
