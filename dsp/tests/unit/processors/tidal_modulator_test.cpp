// ==============================================================================
// Layer 2: Processor Tests - TidalModulator (Seraphis Life Modulator)
// ==============================================================================
// Spec:  specs/seraphis-phase1-life-modulators/spec.md
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 3)
// Covers: FR-001..FR-006, FR-031..FR-033, SC-001, SC-002, SC-003(b), SC-004,
//         SC-005, SC-006.
//
// NOTE ON ALLOCATION TRACKING (single-owner rule):
//   dsp/tests/unit/processors/brownian_drift_test.cpp is the ONE owner of the
//   global operator new/delete replacements for the `dsp_processors_tests`
//   binary. This file therefore includes ONLY <allocation_detector.h> and relies
//   on those replacements -- including <allocation_operator_overrides.h> from a
//   second translation unit is a duplicate-symbol link error.
//
// Statistical thresholds are MEASURED/derived tolerances (DFT bin width, detune
// pair separation), never bit-exact float goldens. The only exact-equality
// assertions are same-build determinism comparisons (SC-004), which compare one
// compiler's output against itself.
// ==============================================================================

#include <krate/dsp/processors/tidal_modulator.h>

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
constexpr size_t kBlock = 512;      // SC-001 / edge-case stepping granularity
constexpr size_t kLongBlock = 4096; // long (hours of wall clock) renders
constexpr double kPi = 3.14159265358979323846;

/// Finite check WITHOUT std::isnan: macOS CI builds with -ffast-math, which
/// folds std::isnan to false. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// A decimated capture of getCurrentValue() plus the wall-clock spacing between
/// captures, which every spectral helper below needs.
struct Trace {
    std::vector<float> samples;
    double captureInterval = 0.0;  ///< seconds between consecutive captures
};

/// Advance `seconds` of wall clock in `blockSize`-sample blocks, capturing
/// getCurrentValue() once every `captureEveryBlocks` blocks.
///
/// TidalModulator's fastest component is 1/30 Hz, so a ~1 s capture interval is
/// ~15x oversampled relative to Nyquist for the fastest layer.
[[nodiscard]] Trace renderTrace(TidalModulator& mod,
                                double seconds,
                                double sampleRate,
                                size_t blockSize,
                                size_t captureEveryBlocks) {
    Trace trace;
    trace.captureInterval = static_cast<double>(blockSize * captureEveryBlocks) / sampleRate;

    const auto totalBlocks =
        static_cast<size_t>(seconds * sampleRate / static_cast<double>(blockSize));
    trace.samples.reserve(totalBlocks / captureEveryBlocks + 1);

    for (size_t b = 0; b < totalBlocks; ++b) {
        mod.processBlock(blockSize);
        if ((b % captureEveryBlocks) == 0) {
            trace.samples.push_back(mod.getCurrentValue());
        }
    }
    return trace;
}

/// Number of blocks to skip between captures so the capture interval is ~1 s.
[[nodiscard]] size_t captureStrideForOneSecond(double sampleRate, size_t blockSize) {
    const auto stride =
        static_cast<size_t>(std::lround(sampleRate / static_cast<double>(blockSize)));
    return std::max<size_t>(stride, size_t{1});
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

/// Single-frequency DFT probe (a Goertzel-equivalent evaluated directly).
/// Returns the amplitude of a sinusoid at `freqHz`: 2*|X(f)|/N, so a pure sine
/// of amplitude A probed at its own frequency returns ~A.
[[nodiscard]] double dftAmplitude(const Trace& trace, double freqHz) {
    const size_t n = trace.samples.size();
    if (n == 0) return 0.0;

    double re = 0.0;
    double im = 0.0;
    const double omegaPerCapture = 2.0 * kPi * freqHz * trace.captureInterval;
    for (size_t i = 0; i < n; ++i) {
        const double ang = omegaPerCapture * static_cast<double>(i);
        const double x = static_cast<double>(trace.samples[i]);
        re += x * std::cos(ang);
        im -= x * std::sin(ang);
    }
    return 2.0 * std::sqrt((re * re) + (im * im)) / static_cast<double>(n);
}

/// Locate the dominant spectral period inside +/-`windowFraction` (in period
/// units) of `centrePeriod`, by scanning the DFT amplitude on a frequency grid
/// spaced at ~1/4 of the DFT bin width (1 / total render time).
[[nodiscard]] double dominantPeriodNear(const Trace& trace,
                                        double centrePeriod,
                                        double windowFraction) {
    const double totalTime =
        static_cast<double>(trace.samples.size()) * trace.captureInterval;
    const double fLo = 1.0 / (centrePeriod * (1.0 + windowFraction));
    const double fHi = 1.0 / (centrePeriod * (1.0 - windowFraction));

    const auto gridSteps =
        std::max(64, static_cast<int>((fHi - fLo) * totalTime * 4.0) + 1);

    double bestFreq = fLo;
    double bestAmplitude = -1.0;
    for (int i = 0; i <= gridSteps; ++i) {
        const double f =
            fLo + ((fHi - fLo) * static_cast<double>(i) / static_cast<double>(gridSteps));
        const double amplitude = dftAmplitude(trace, f);
        if (amplitude > bestAmplitude) {
            bestAmplitude = amplitude;
            bestFreq = f;
        }
    }
    return 1.0 / bestFreq;
}

/// The rate -> base-period mapping, written independently of the header:
/// P_base = lerp(600 s / sqrt(3) -> 30 s, rate01). The slow end is the 10 min
/// maximum divided by the LARGEST layer ratio, so the sqrt(3) layer - not the
/// base layer - is what lands on 600 s at rate 0 and no layer is ever clamped
/// (FR-032: the ratios must survive at every rate).
[[nodiscard]] double expectedBasePeriod(float rate01) {
    const double r = static_cast<double>(std::clamp(rate01, 0.0f, 1.0f));
    const double slowest = 600.0 / std::sqrt(3.0);
    return slowest + (r * (30.0 - slowest));
}

/// Longest layer period (seconds) at a rate setting: base period * sqrt(3).
[[nodiscard]] double expectedLongestPeriod(float rate01) {
    return expectedBasePeriod(rate01) * std::sqrt(3.0);
}

}  // namespace

// =============================================================================
// SC-001 - Boundedness at every parameter extreme
// =============================================================================

TEST_CASE("TidalModulator_NeverExceedsRange", "[processors][tidal_modulator][seraphis]") {
    // Extremes: rate 0 (P_base = 600/sqrt(3) = 346.4 s, so the sqrt(3) layer
    // sits on the 10 min maximum) and rate 1 (P_base = 30 s = the minimum),
    // each at depth 0 and depth 1.
    //
    // Render horizon = 3x the longest configured period at that setting
    // (spec.md SC-001): at rate 0 the slowest layer is kMaxPeriod = 600 s, so
    // the render is 1800 s = 30 min. At rate 1 the longest layer is
    // 30 * sqrt(3) = 51.96 s, so 200 s is ~3.8x.
    const std::array<float, 2> rateGrid{0.0f, 1.0f};
    const std::array<float, 2> depthGrid{0.0f, 1.0f};

    size_t violations = 0;
    size_t nonFinite = 0;

    for (float rate : rateGrid) {
        for (float depth : depthGrid) {
            TidalModulator tidal;
            tidal.setSeed(2718u);
            tidal.setRate(rate);
            tidal.setDepth(depth);
            tidal.prepare(kSr48);

            const auto range = tidal.getSourceRange();
            REQUIRE(range.first == -1.0f);
            REQUIRE(range.second == 1.0f);

            const double seconds = (rate <= 0.5f) ? 1800.0 : 200.0;
            const auto blocks =
                static_cast<size_t>(seconds * kSr48 / static_cast<double>(kBlock));
            for (size_t b = 0; b < blocks; ++b) {
                tidal.processBlock(kBlock);
                const float v = tidal.getCurrentValue();
                if (!isFiniteValue(v)) ++nonFinite;
                if (v < range.first || v > range.second) ++violations;
            }
        }
    }

    // Short per-sample window at the fastest setting: block-decimated sampling
    // could in principle step over an intra-block excursion, so also check every
    // sample for 10 s at the worst (fastest, full-depth) configuration.
    {
        TidalModulator tidal;
        tidal.setSeed(4242u);
        tidal.setRate(1.0f);
        tidal.setDepth(1.0f);
        tidal.prepare(kSr48);

        const auto samples = static_cast<size_t>(10.0 * kSr48);
        for (size_t i = 0; i < samples; ++i) {
            tidal.process();
            const float v = tidal.getCurrentValue();
            if (!isFiniteValue(v)) ++nonFinite;
            if (v < -1.0f || v > 1.0f) ++violations;
        }
    }

    REQUIRE(nonFinite == 0u);
    REQUIRE(violations == 0u);
}

// =============================================================================
// SC-001 / FR-033 - The analytic amplitude-sum bound (load bearing)
// =============================================================================

TEST_CASE("TidalModulator_BoundedUnderPhaseAlignment",
          "[processors][tidal_modulator][seraphis]") {
    // FR-033: boundedness is guaranteed ANALYTICALLY, not by the render.
    // The output is
    //     out = depth * sum_k w_k * kSinePairScale * (sin a_k + sin b_k)
    // so the sum of the absolute coefficients of the six sines is
    //     kNumLayers * kLayerWeight * kSinesPerLayer * kSinePairScale
    // and that must equal exactly 1 = the half-span of the [-1,+1] source range.
    // Worst-case fully-constructive alignment of all six sines therefore cannot
    // exceed the range, at any depth <= 1. Worst-case alignment of 30 s - 10 min
    // incommensurate periods is unreachable by any practical render, which is
    // why this identity -- not the render below -- is the guarantee.
    const double sumAbsCoefficients =
        static_cast<double>(TidalModulator::kNumLayers) *
        static_cast<double>(TidalModulator::kLayerWeight) *
        static_cast<double>(TidalModulator::kSinesPerLayer) *
        static_cast<double>(TidalModulator::kSinePairScale);
    REQUIRE(std::abs(sumAbsCoefficients - 1.0) <= 1e-6);

    REQUIRE(TidalModulator::kNumLayers == 3u);      // FR-031: exactly 3 layers
    REQUIRE(TidalModulator::kSinesPerLayer == 2u);  // FR-031: a beating sine pair
    REQUIRE(std::abs(static_cast<double>(TidalModulator::kLayerWeight) - (1.0 / 3.0)) <= 1e-6);

    // FR-032: the layer ratios are FIXED irrational constants (1, sqrt2, sqrt3),
    // hardcoded and NOT seed-drawn. Compared against independently computed
    // square roots, so a typo'd literal in the header is rejected.
    REQUIRE(std::abs(TidalModulator::kLayerRatios[0] - 1.0f) <= 1e-6f);
    REQUIRE(std::abs(TidalModulator::kLayerRatios[1] - std::sqrt(2.0f)) <= 1e-6f);
    REQUIRE(std::abs(TidalModulator::kLayerRatios[2] - std::sqrt(3.0f)) <= 1e-6f);

    // FR-032 (seed independence): two different seeds give identical layer
    // periods -- only the phase offsets are seeded.
    {
        TidalModulator a;
        TidalModulator b;
        a.setSeed(11u);
        b.setSeed(987654321u);
        a.setRate(0.8f);
        b.setRate(0.8f);
        a.prepare(kSr48);
        b.prepare(kSr48);
        for (size_t k = 0; k < TidalModulator::kNumLayers; ++k) {
            REQUIRE(a.getLayerPeriodSeconds(k) == b.getLayerPeriodSeconds(k));
        }
    }

    // Long-render sanity pass (NOT the proof): 3000 s = 100 base periods at the
    // fastest setting, where the three layers drift through the widest range of
    // relative phase.
    TidalModulator tidal;
    tidal.setSeed(1357u);
    tidal.setRate(1.0f);
    tidal.setDepth(1.0f);
    tidal.prepare(kSr48);

    const auto trace = renderTrace(tidal, 3000.0, kSr48, kLongBlock, 1u);
    REQUIRE(trace.samples.size() > 1000u);

    // Counters, not per-sample REQUIREs: 35 k Catch2 assertions per loop would
    // dominate the suite's runtime for no extra diagnostic value.
    double maxAbs = 0.0;
    size_t nonFinite = 0;
    for (float v : trace.samples) {
        if (!isFiniteValue(v)) ++nonFinite;
        maxAbs = std::max(maxAbs, std::abs(static_cast<double>(v)));
    }
    REQUIRE(nonFinite == 0u);
    REQUIRE(maxAbs <= 1.0);
    // The source is genuinely moving (a stuck-at-zero implementation would pass
    // the bound trivially). Six unit-coefficient sines summing to 1/6 each give
    // an RMS of ~0.289; 0.2 is a loose floor on the observed peak.
    REQUIRE(maxAbs > 0.2);
}

// =============================================================================
// SC-002 - Bounded per-sample slew at the worst-case (max-slew) configuration
// =============================================================================

TEST_CASE("TidalModulator_MaxSlewBounded", "[processors][tidal_modulator][seraphis]") {
    // Worst case per spec.md:259 -- SHORTEST period (30 s, i.e. rate = 1),
    // MAXIMUM depth, all three layers at full amplitude.
    TidalModulator tidal;
    tidal.setSeed(31337u);
    tidal.setRate(1.0f);
    tidal.setDepth(1.0f);
    tidal.prepare(kSr48);

    REQUIRE(tidal.getLayerPeriodSeconds(0) == TidalModulator::kMinPeriod);

    float prev = tidal.getCurrentValue();
    double maxDelta = 0.0;

    // >= one full 30 s base period, per-sample.
    const auto numSamples = static_cast<size_t>(30.0 * kSr48);
    for (size_t i = 0; i < numSamples; ++i) {
        tidal.process();
        const float cur = tidal.getCurrentValue();
        maxDelta = std::max(maxDelta,
                            std::abs(static_cast<double>(cur) - static_cast<double>(prev)));
        prev = cur;
    }

    // Threshold: 1e-3 of the source-range span (span = 2) = 2.0e-3 absolute.
    // Analytic bound (see the header's SC-002 justification):
    //   |d(out)/dt| <= depth * omega_max, omega_max = 2*pi/30 s ~= 0.209 rad/s
    //   -> per sample @48 kHz ~= 4.4e-6, which is ~450x inside the threshold.
    // The 20 ms output smoother only ever moves a fraction of an already-small
    // target step, so it cannot add a larger jump.
    REQUIRE(maxDelta <= 2.0e-3);
}

// =============================================================================
// SC-003(b) - Dominant period tracks the rate setting (quantitative)
// =============================================================================

TEST_CASE("TidalModulator_PeriodTracksSetting", "[processors][tidal_modulator][seraphis]") {
    // The spectrum is six equal-amplitude lines, so there is no single "loudest"
    // line; the quantity with a closed-form prediction is the location of the
    // FASTEST layer, whose period is exactly P_base (ratio 1.0). We locate it by
    // scanning the DFT amplitude in a +/-15 % window around the predicted
    // P_base -- narrow enough to exclude the sqrt(2)/sqrt(3) layers, which sit
    // 41 % and 73 % away in period at every rate (no layer is ever clamped).
    //
    // TOLERANCE (documented, not guessed). Each render is T = 40 * P_base, so
    //   * DFT bin width      = 1/T          -> +/- 2.5 % in period units at P_base
    //   * detune partner     = +2 % in frequency, 0.8 bins away, so it is NOT
    //                          resolved and pulls the merged peak to ~1.01*f0,
    //                          i.e. ~-1 % in period
    //   * scan grid          = 1/4 bin      -> +/- 0.6 %
    // Sum ~= 5.1 %; the assertion uses 6 %.
    const std::array<float, 3> rateGrid{0.2f, 0.5f, 1.0f};
    std::array<double, 3> measuredPeriod{};

    for (size_t i = 0; i < rateGrid.size(); ++i) {
        const double basePeriod = expectedBasePeriod(rateGrid[i]);

        TidalModulator tidal;
        tidal.setSeed(20260724u);
        tidal.setRate(rateGrid[i]);
        tidal.setDepth(1.0f);
        tidal.prepare(kSr48);

        const size_t stride = captureStrideForOneSecond(kSr48, kLongBlock);
        const auto trace = renderTrace(tidal, 40.0 * basePeriod, kSr48, kLongBlock, stride);
        REQUIRE(trace.samples.size() > 500u);

        measuredPeriod[i] = dominantPeriodNear(trace, basePeriod, 0.15);
        REQUIRE(std::abs(measuredPeriod[i] - basePeriod) / basePeriod <= 0.06);
    }

    // Monotone ordering: rate 0.2 -> 0.5 -> 1.0 maps to base periods
    // 283 s -> 188 s -> 30 s, so the measured period must strictly decrease.
    REQUIRE(measuredPeriod[0] > measuredPeriod[1]);
    REQUIRE(measuredPeriod[1] > measuredPeriod[2]);

    // FR-031/FR-032 - the sqrt(2) and sqrt(3) layers are actually present in the
    // rendered signal, not just declared as constants. At rate 1.0 no layer is
    // clamped, so the three predicted layer frequencies are all distinct and
    // resolvable. Off-frequency probes (periods 36 s and 47 s, both >= 7 DFT
    // bins from every line) measure the leakage floor.
    {
        TidalModulator tidal;
        tidal.setSeed(864231u);
        tidal.setRate(1.0f);
        tidal.setDepth(1.0f);
        tidal.prepare(kSr48);

        const size_t stride = captureStrideForOneSecond(kSr48, kLongBlock);
        const auto trace = renderTrace(tidal, 3600.0, kSr48, kLongBlock, stride);
        REQUIRE(trace.samples.size() > 2000u);

        const double p0 = 30.0;
        const double p1 = 30.0 * std::sqrt(2.0);
        const double p2 = 30.0 * std::sqrt(3.0);

        const double onLine0 = dftAmplitude(trace, 1.0 / p0);
        const double onLine1 = dftAmplitude(trace, 1.0 / p1);
        const double onLine2 = dftAmplitude(trace, 1.0 / p2);

        const double offA = dftAmplitude(trace, 1.0 / 36.0);
        const double offB = dftAmplitude(trace, 1.0 / 47.0);
        const double leakageFloor = std::max(offA, offB);

        // Each of the six sines carries amplitude
        // depth*kLayerWeight*kSinePairScale = 1/6 ~= 0.167; rectangular-window
        // leakage 7+ bins away is a few percent of that, so a 4x margin is
        // conservative while still rejecting a missing layer outright.
        REQUIRE(onLine0 > 4.0 * leakageFloor);
        REQUIRE(onLine1 > 4.0 * leakageFloor);
        REQUIRE(onLine2 > 4.0 * leakageFloor);
    }
}

// =============================================================================
// FR-032 - The layer ratios survive at EVERY rate (no clamp collapse)
// =============================================================================

TEST_CASE("TidalModulator_LayerRatiosHoldAtEveryRate",
          "[processors][tidal_modulator][seraphis]") {
    // Incommensurability is a property of the RUNNING configuration, not of the
    // kLayerRatios table. If the rate -> period mapping pushes a layer into the
    // [kMinPeriod, kMaxPeriod] clamp, two layers collapse onto one period and
    // the combined output degenerates - at the extreme into a 1:1:1 stack that
    // is a single exactly-repeating sine pair. Sweep the whole rate range and
    // assert the REALISED periods still stand in 1 : sqrt(2) : sqrt(3).
    const double ratio1 = std::sqrt(2.0);
    const double ratio2 = std::sqrt(3.0);

    for (int i = 0; i <= 20; ++i) {
        const float rate = static_cast<float>(i) / 20.0f;

        TidalModulator tidal;
        tidal.setSeed(4711u);
        tidal.setRate(rate);
        tidal.prepare(kSr48);

        const double p0 = static_cast<double>(tidal.getLayerPeriodSeconds(0));
        const double p1 = static_cast<double>(tidal.getLayerPeriodSeconds(1));
        const double p2 = static_cast<double>(tidal.getLayerPeriodSeconds(2));

        // FR-031: every layer period stays inside the declared 30 s - 10 min band.
        REQUIRE(p0 >= static_cast<double>(TidalModulator::kMinPeriod));
        REQUIRE(p2 <= static_cast<double>(TidalModulator::kMaxPeriod));

        // FR-032: and the ratios are intact. 1e-5 relative covers the float
        // round-trip through the period getter (and the last-ulp touch of the
        // inert range clamp at rate 0); a clamp collapse moves these by 41 % and
        // 73 %, so nothing subtle can hide under this tolerance.
        REQUIRE(std::abs((p1 / p0) - ratio1) / ratio1 <= 1e-5);
        REQUIRE(std::abs((p2 / p0) - ratio2) / ratio2 <= 1e-5);

        // The base period matches the independently written mapping.
        REQUIRE(std::abs(p0 - expectedBasePeriod(rate)) <= 1e-3);
    }

    // The band edges are actually reached: 30 s at the fast end (base layer),
    // 600 s at the slow end (sqrt(3) layer). Without both, "spanning 30 s to
    // 10 min" (FR-031) would be satisfied only on paper.
    {
        TidalModulator fast;
        fast.setRate(1.0f);
        fast.prepare(kSr48);
        REQUIRE(fast.getLayerPeriodSeconds(0) == TidalModulator::kMinPeriod);

        TidalModulator slow;
        slow.setRate(0.0f);
        slow.prepare(kSr48);
        REQUIRE(std::abs(static_cast<double>(slow.getLayerPeriodSeconds(2)) -
                         static_cast<double>(TidalModulator::kMaxPeriod)) <= 1e-3);
    }
}

// =============================================================================
// FR-032 - No exact repeat within the render horizon (bounded finite claim)
// =============================================================================

TEST_CASE("TidalModulator_NoExactRepeat", "[processors][tidal_modulator][seraphis]") {
    // Infinite-horizon non-repetition cannot be verified by any render; the
    // testable claim (FR-032) is that no length-W window of the output reappears
    // bit-identically at ANY lag inside the longest render used here.
    //
    // Swept across the rate range, NOT just the fastest setting: the slow end is
    // exactly where a period mapping can quietly collapse the three layers onto
    // one period, and a collapsed stack repeats. Each render spans >= 3x the
    // longest layer period at that rate (600 s * 3 = 1800 s at rate 0).
    constexpr size_t kWindow = 32;
    const std::array<float, 3> rateGrid{0.0f, 0.5f, 1.0f};

    for (float rate : rateGrid) {
        TidalModulator tidal;
        tidal.setSeed(777u);
        tidal.setRate(rate);
        tidal.setDepth(1.0f);
        tidal.prepare(kSr48);

        const double seconds =
            std::max(1200.0, 3.0 * expectedLongestPeriod(rate));

        // Captured every 4096 samples (85.3 ms).
        const auto trace = renderTrace(tidal, seconds, kSr48, kLongBlock, 1u);
        REQUIRE(trace.samples.size() > (4u * kWindow));

        const auto& x = trace.samples;

        // The reference window must actually vary, otherwise "no repeat" would
        // be a vacuous claim about a constant signal.
        bool referenceVaries = false;
        for (size_t i = 1; i < kWindow; ++i) {
            if (x[i] != x[0]) {
                referenceVaries = true;
                break;
            }
        }
        REQUIRE(referenceVaries);

        size_t exactRepeats = 0;
        for (size_t lag = 1; lag + kWindow <= x.size(); ++lag) {
            bool identical = true;
            for (size_t i = 0; i < kWindow; ++i) {
                if (x[i] != x[lag + i]) {
                    identical = false;
                    break;
                }
            }
            if (identical) ++exactRepeats;
        }
        REQUIRE(exactRepeats == 0u);
    }
}

// =============================================================================
// SC-004 - Seeded determinism and reset() rewind
// =============================================================================

TEST_CASE("TidalModulator_SeededDeterminism", "[processors][tidal_modulator][seraphis]") {
    constexpr size_t kNumBlocks = 400;

    auto render = [&](std::uint32_t seed) {
        TidalModulator tidal;
        tidal.setSeed(seed);
        tidal.setRate(1.0f);
        tidal.setDepth(1.0f);
        tidal.prepare(kSr48);
        std::vector<float> out;
        out.reserve(kNumBlocks);
        for (size_t b = 0; b < kNumBlocks; ++b) {
            tidal.processBlock(kBlock);
            out.push_back(tidal.getCurrentValue());
        }
        return out;
    };

    const auto a = render(1234u);
    const auto b = render(1234u);
    REQUIRE(a.size() == kNumBlocks);
    // Same build, same seed, same call sequence -> exact equality.
    REQUIRE(a == b);

    // The seed varies only the six initial sine phases; period/statistics are
    // seed independent (Clarifications Q3). The trajectory must still differ.
    const auto c = render(9999u);
    REQUIRE(c.size() == kNumBlocks);
    REQUIRE(a != c);
}

TEST_CASE("TidalModulator_ResetRewindsToSeed", "[processors][tidal_modulator][seraphis]") {
    constexpr size_t kNumBlocks = 400;

    TidalModulator tidal;
    tidal.setSeed(4711u);
    tidal.setRate(0.7f);
    tidal.setDepth(0.8f);
    tidal.prepare(kSr48);

    std::vector<float> first;
    first.reserve(kNumBlocks);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        tidal.processBlock(kBlock);
        first.push_back(tidal.getCurrentValue());
    }

    tidal.reset();

    std::vector<float> second;
    second.reserve(kNumBlocks);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        tidal.processBlock(kBlock);
        second.push_back(tidal.getCurrentValue());
    }

    REQUIRE(first == second);
}

// =============================================================================
// SC-005 (option a) - Sample-rate invariance, like-for-like
// =============================================================================

TEST_CASE("TidalModulator_SampleRateInvariant", "[processors][tidal_modulator][seraphis]") {
    // TidalModulator draws the RNG exactly six times, in prepare(), to seed the
    // initial phases -- the draw schedule does NOT depend on the sample rate. It
    // is therefore the SAME wall-clock trajectory at 44.1 and 96 kHz, sampled on
    // two different block grids (spec SC-005 option (a)), so statistics may be
    // compared like-for-like with a tight tolerance.
    constexpr double kSeconds = 1200.0;  // 40 base periods at rate 1.0

    auto measure = [&](double sampleRate) {
        TidalModulator tidal;
        tidal.setSeed(60606u);
        tidal.setRate(1.0f);
        tidal.setDepth(1.0f);
        tidal.prepare(sampleRate);

        const size_t stride = captureStrideForOneSecond(sampleRate, kLongBlock);
        return renderTrace(tidal, kSeconds, sampleRate, kLongBlock, stride);
    };

    const auto traceA = measure(44100.0);
    const auto traceB = measure(96000.0);
    REQUIRE(traceA.samples.size() > 500u);
    REQUIRE(traceB.samples.size() > 500u);

    const double rmsA = rmsOf(traceA.samples);
    const double rmsB = rmsOf(traceB.samples);
    // Six sines of amplitude 1/6 give an RMS of sqrt(6*(1/6)^2/2) ~= 0.289.
    REQUIRE(rmsA > 0.1);
    // Same continuous function, different sampling grid: the only difference is
    // grid quantization and the fractional block at the end of the render.
    REQUIRE(std::abs(rmsA - rmsB) / rmsA <= 0.05);

    // The source is ~zero mean, so a RELATIVE mean comparison divides by ~0 and
    // is undefined. Compare absolutely, in source-range-span units (span = 2):
    // 0.05 absolute = 2.5 % of the span.
    REQUIRE(std::abs(meanOf(traceA.samples) - meanOf(traceB.samples)) <= 0.05);

    // Dominant period, same estimator and same tolerance basis as SC-003(b).
    const double periodA = dominantPeriodNear(traceA, 30.0, 0.15);
    const double periodB = dominantPeriodNear(traceB, 30.0, 0.15);
    REQUIRE(std::abs(periodA - periodB) / periodA <= 0.06);
}

// =============================================================================
// SC-006 - Allocation-free steady state
// =============================================================================

TEST_CASE("TidalModulator_NoAllocInProcess", "[processors][tidal_modulator][seraphis]") {
    TidalModulator tidal;
    tidal.setSeed(64u);
    tidal.setRate(0.6f);
    tidal.setDepth(1.0f);
    tidal.prepare(kSr48);
    tidal.processBlock(kBlock);  // warm-up OUTSIDE the tracking scope

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    for (int i = 0; i < 500; ++i) {
        tidal.processBlock(kBlock);
    }
    for (int i = 0; i < 4096; ++i) {
        tidal.process();
    }
    const size_t allocations = detector.stopTracking();

    REQUIRE(allocations == 0u);
}

// =============================================================================
// FR-006 - getSourceRange() is fixed at polarity full scale
// =============================================================================

TEST_CASE("TidalModulator_SourceRangeIndependentOfDepth",
          "[processors][tidal_modulator][seraphis]") {
    TidalModulator tidal;
    tidal.prepare(kSr48);

    for (float depth : {0.0f, 0.5f, 1.0f}) {
        tidal.setDepth(depth);
        const auto range = tidal.getSourceRange();
        REQUIRE(range.first == -1.0f);
        REQUIRE(range.second == 1.0f);
    }
}

// =============================================================================
// FR-004 - Output is well defined after prepare() and after reset()
// =============================================================================

TEST_CASE("TidalModulator_OutputDefinedAfterPrepare",
          "[processors][tidal_modulator][seraphis]") {
    TidalModulator tidal;
    tidal.setSeed(11u);
    tidal.setRate(0.35f);
    tidal.setDepth(0.6f);
    tidal.prepare(kSr48);

    const auto range = tidal.getSourceRange();

    {
        const float v = tidal.getCurrentValue();  // no intervening advance
        REQUIRE(isFiniteValue(v));
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    }

    for (int i = 0; i < 100; ++i) tidal.processBlock(kBlock);
    tidal.reset();

    {
        const float v = tidal.getCurrentValue();  // no intervening advance
        REQUIRE(isFiniteValue(v));
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    }
}

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("TidalModulator_EdgeCases", "[processors][tidal_modulator][seraphis]") {
    SECTION("degenerate sample rates neither hang nor poison the output") {
        // RT safety: prepare(0) made every phase increment non-finite, so the
        // wrapped phase went NaN and the whole output with it. prepare() floors
        // the sample rate at 1 Hz. Reaching the assertions at all is half of
        // what this section proves.
        for (double sr : {0.0, -48000.0, 0.25}) {
            TidalModulator tidal;
            tidal.setSeed(1234u);
            tidal.setRate(1.0f);
            tidal.setDepth(1.0f);
            tidal.prepare(sr);

            bool finite = true;
            bool inRange = true;
            const auto observe = [&](float v) {
                if (!isFiniteValue(v)) finite = false;
                if (v < -1.0f || v > 1.0f) inRange = false;
            };

            for (int b = 0; b < 8; ++b) {
                tidal.processBlock(kBlock);
                observe(tidal.getCurrentValue());
            }
            for (int i = 0; i < 2000; ++i) {
                tidal.process();
                observe(tidal.getCurrentValue());
            }

            REQUIRE(finite);
            REQUIRE(inRange);
        }
    }

    SECTION("processBlock(0) is a no-op") {
        TidalModulator withZeros;
        TidalModulator reference;
        for (TidalModulator* m : {&withZeros, &reference}) {
            m->setSeed(555u);
            m->setRate(0.9f);
            m->setDepth(1.0f);
            m->prepare(kSr48);
        }

        for (int b = 0; b < 50; ++b) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
        }

        const float before = withZeros.getCurrentValue();
        for (int i = 0; i < 10; ++i) withZeros.processBlock(0);
        REQUIRE(withZeros.getCurrentValue() == before);

        // Phase and smoother state are untouched as well.
        std::vector<float> a;
        std::vector<float> b2;
        for (int i = 0; i < 50; ++i) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
            a.push_back(withZeros.getCurrentValue());
            b2.push_back(reference.getCurrentValue());
        }
        REQUIRE(a == b2);
    }

    SECTION("10 minute period stays continuous and bounded") {
        TidalModulator tidal;
        tidal.setSeed(90210u);
        tidal.setRate(0.0f);  // slowest layer sits on kMaxPeriod = 600 s
        tidal.setDepth(1.0f);
        tidal.prepare(kSr48);

        // The slowest layer reaches the 10 min maximum while the other two stay
        // strictly faster - they must NOT all pile onto kMaxPeriod (FR-032).
        REQUIRE(std::abs(static_cast<double>(tidal.getLayerPeriodSeconds(2)) -
                         static_cast<double>(TidalModulator::kMaxPeriod)) <= 1e-3);
        REQUIRE(tidal.getLayerPeriodSeconds(0) < tidal.getLayerPeriodSeconds(1));
        REQUIRE(tidal.getLayerPeriodSeconds(1) < tidal.getLayerPeriodSeconds(2));
        for (size_t k = 0; k < TidalModulator::kNumLayers; ++k) {
            REQUIRE(tidal.getLayerPeriodSeconds(k) >= TidalModulator::kMinPeriod);
            REQUIRE(tidal.getLayerPeriodSeconds(k) <= TidalModulator::kMaxPeriod);
        }

        // 1800 s = 3x the 10 min period, stepped in 512-sample blocks (10.67 ms).
        const auto blocks = static_cast<size_t>(1800.0 * kSr48 / static_cast<double>(kBlock));
        float prev = tidal.getCurrentValue();
        double maxDelta = 0.0;
        bool bounded = true;
        bool finite = true;

        for (size_t b = 0; b < blocks; ++b) {
            tidal.processBlock(kBlock);
            const float cur = tidal.getCurrentValue();
            if (!isFiniteValue(cur)) finite = false;
            if (cur < -1.0f || cur > 1.0f) bounded = false;
            maxDelta = std::max(maxDelta,
                                std::abs(static_cast<double>(cur) - static_cast<double>(prev)));
            prev = cur;
        }

        REQUIRE(finite);
        REQUIRE(bounded);
        // Continuity: at rate 0 the six sines sit at periods 346.4 / 489.9 /
        // 600 s (each with a +2 % detune partner), so the slope is at most
        // (1/6) * sum_i omega_i ~= 0.0139 /s, i.e. ~1.5e-4 per 10.67 ms block.
        // 1e-3 is a ~6.7x margin that still rejects any real discontinuity
        // (e.g. a phase wrap that loses the fraction).
        REQUIRE(maxDelta <= 1.0e-3);
    }

    SECTION("seed 0 is handled safely") {
        TidalModulator tidal;
        tidal.setSeed(0u);  // Xorshift32 substitutes its default seed
        tidal.setRate(1.0f);
        tidal.setDepth(1.0f);
        tidal.prepare(kSr48);

        const auto trace = renderTrace(tidal, 300.0, kSr48, kBlock, 1u);
        REQUIRE(trace.samples.size() > 1000u);

        bool allFinite = true;
        bool inRange = true;
        for (float v : trace.samples) {
            if (!isFiniteValue(v)) allFinite = false;
            if (v < -1.0f || v > 1.0f) inRange = false;
        }
        REQUIRE(allFinite);
        REQUIRE(inRange);
        REQUIRE(rmsOf(trace.samples) > 0.05);
    }
}
