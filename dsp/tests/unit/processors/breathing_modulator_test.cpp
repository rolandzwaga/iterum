// ==============================================================================
// Layer 2: Processor Tests - BreathingModulator (Seraphis Life Modulator)
// ==============================================================================
// Spec:  specs/seraphis-phase1-life-modulators/spec.md
// Plan:  specs/seraphis-phase1-life-modulators/plan.md  (section 2)
// Covers: FR-001..FR-006, FR-021..FR-024, SC-001, SC-002, SC-003(b),
//         SC-004, SC-005, SC-006.
//
// NOTE ON ALLOCATION TRACKING (single-owner rule):
//   brownian_drift_test.cpp is the ONE owner of the global operator new/delete
//   replacements for the `dsp_processors_tests` binary. This TU therefore
//   includes only <allocation_detector.h> and relies on those replacements --
//   including <allocation_operator_overrides.h> from a second TU in the same
//   binary is a duplicate-symbol link error.
//
// Statistical thresholds are MEASURED / analytically derived tolerances, never
// bit-exact float goldens. The only exact-equality assertions are same-build
// determinism comparisons (SC-004), which compare one compiler against itself.
// ==============================================================================

#include <krate/dsp/processors/breathing_modulator.h>

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
constexpr double kPiTest = 3.14159265358979323846;

/// Finite check WITHOUT std::isnan: macOS CI builds with -ffast-math, which
/// folds std::isnan to false. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// Per-sample render: advances process() `numSamples` times, capturing the
/// output after every advance. Used wherever per-sample resolution matters
/// (SC-002 slew, shape analysis, cycle-period measurement).
[[nodiscard]] std::vector<float> renderPerSample(BreathingModulator& mod, size_t numSamples) {
    std::vector<float> out;
    out.reserve(numSamples);
    for (size_t i = 0; i < numSamples; ++i) {
        mod.process();
        out.push_back(mod.getCurrentValue());
    }
    return out;
}

/// Control-rate render: advances `seconds` of wall clock in `blockSize` blocks,
/// capturing getCurrentValue() once every `blocksPerCapture` blocks.
[[nodiscard]] std::vector<float> renderDecimated(BreathingModulator& mod,
                                                 double seconds,
                                                 double sampleRate,
                                                 size_t blockSize,
                                                 size_t blocksPerCapture) {
    const auto totalBlocks =
        static_cast<size_t>(seconds * sampleRate / static_cast<double>(blockSize));
    std::vector<float> out;
    out.reserve((totalBlocks / blocksPerCapture) + 1);
    for (size_t b = 0; b < totalBlocks; ++b) {
        mod.processBlock(blockSize);
        if ((b % blocksPerCapture) == 0) {
            out.push_back(mod.getCurrentValue());
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

/// Mean-detrended, r[0]-normalized autocorrelation (biased estimator: divide by
/// n, not n-k). The biased form tapers with lag, which damps large-lag estimator
/// noise; because both sample rates in the SC-005 comparison cover the same
/// wall-clock span and capture count, the taper is identical at both rates.
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

/// Lag of the first autocorrelation MAXIMUM after the estimate first goes
/// negative -- i.e. the dominant period of a periodic source (SC-003(b): the
/// 1/e DECAY metric is undefined for a periodic signal, so period tracking is
/// the correct form). Returns 0 when no negative excursion exists inside the
/// computed lag window, which the callers reject.
[[nodiscard]] size_t firstAutocorrPeakLag(const std::vector<double>& r) {
    size_t k = 1;
    while (k < r.size() && r[k] >= 0.0) ++k;
    if (k >= r.size()) return 0;

    size_t best = 0;
    double bestVal = -2.0;
    for (size_t i = k; i < r.size(); ++i) {
        if (r[i] > bestVal) {
            bestVal = r[i];
            best = i;
        }
    }
    return best;
}

/// Single-bin DFT magnitude (normalized by N). Used instead of a full FFT
/// because only three bins are needed; the analysis window holds an exact
/// integer number of cycles, so leakage is negligible.
[[nodiscard]] double dftMagnitude(const std::vector<double>& x, size_t bin) {
    if (x.empty()) return 0.0;
    const double n = static_cast<double>(x.size());
    double re = 0.0;
    double im = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double ang =
            2.0 * kPiTest * static_cast<double>(bin) * static_cast<double>(i) / n;
        re += x[i] * std::cos(ang);
        im -= x[i] * std::sin(ang);
    }
    return std::sqrt((re * re) + (im * im)) / n;
}

/// Times (seconds) of upward zero crossings of a per-sample render, with linear
/// sub-sample interpolation. The breath waveform crosses 0 exactly once upward
/// per cycle (at phase ~0.168 on the inhale ramp, a steep part of the shape),
/// so successive crossing intervals ARE the cycle periods.
[[nodiscard]] std::vector<double> upwardCrossingTimes(const std::vector<float>& x,
                                                      double sampleRate) {
    std::vector<double> times;
    for (size_t i = 1; i < x.size(); ++i) {
        const double prev = static_cast<double>(x[i - 1]);
        const double cur = static_cast<double>(x[i]);
        if (prev < 0.0 && cur >= 0.0) {
            const double denom = prev - cur;
            const double frac = (denom != 0.0) ? (prev / denom) : 0.0;
            times.push_back((static_cast<double>(i - 1) + frac) / sampleRate);
        }
    }
    return times;
}

[[nodiscard]] std::vector<double> diffs(const std::vector<double>& t) {
    std::vector<double> d;
    for (size_t i = 1; i < t.size(); ++i) d.push_back(t[i] - t[i - 1]);
    return d;
}

}  // namespace

// =============================================================================
// SC-001 / FR-006 - Boundedness at every parameter extreme
// =============================================================================

TEST_CASE("BreathingModulator_NeverExceedsRange",
          "[processors][breathing_modulator][seraphis]") {
    const std::array<float, 2> rateGrid{0.01f, 0.5f};
    const std::array<float, 2> depthGrid{0.0f, 1.0f};
    const std::array<float, 2> irregularityGrid{0.0f, 1.0f};

    // Horizon: >= 3x the longest configured period. At kMinRate = 0.01 Hz the
    // nominal period is 100 s; at irregularity 1 the jitter factor bottoms out
    // at 0.5, so the slowest achievable period is 200 s -> 600 s render.
    constexpr double kSeconds = 600.0;

    size_t violations = 0;
    size_t nonFinite = 0;

    for (float rate : rateGrid) {
        for (float depth : depthGrid) {
            for (float irregularity : irregularityGrid) {
                BreathingModulator mod;
                mod.setSeed(2718u);
                mod.setRate(rate);
                mod.setDepth(depth);
                mod.setIrregularity(irregularity);
                mod.prepare(kSr48);

                const auto range = mod.getSourceRange();
                REQUIRE(range.first == -1.0f);
                REQUIRE(range.second == 1.0f);

                const auto blocks = static_cast<size_t>(
                    kSeconds * kSr48 / static_cast<double>(kBlock));
                for (size_t b = 0; b < blocks; ++b) {
                    mod.processBlock(kBlock);
                    const float v = mod.getCurrentValue();
                    if (!isFiniteValue(v)) ++nonFinite;
                    if (v < range.first || v > range.second) ++violations;
                }
            }
        }
    }

    REQUIRE(nonFinite == 0u);
    REQUIRE(violations == 0u);

    // The zero-violation counts above would also be reported by a broken
    // implementation whose values are only in range BECAUSE getCurrentValue()
    // clamps. Prove the clamp is inert rather than trusting it: depth enters the
    // signal path as a pure multiplier (raw = depth * bipolar, bipolar in
    // [-1,1]), so halving the depth must halve the excursion. If the depth-1
    // render were being clamped, its peak would sit pinned at 1.0 while twice
    // the depth-0.5 peak reported the larger true value.
    //
    // Peaks (not per-sample proportionality) are the right comparison: the
    // output smoother snaps to target below an ABSOLUTE completion threshold
    // (smoother.h:199), which is not a linear operation, but its lag vanishes at
    // the extrema where the target's slope is zero.
    {
        const auto peakOf = [](float depth) {
            BreathingModulator mod;
            mod.setSeed(90210u);
            mod.setRate(BreathingModulator::kMaxRate);
            mod.setDepth(depth);
            mod.setIrregularity(1.0f);  // jitter draws do not depend on depth
            mod.prepare(kSr48);

            double peak = 0.0;
            const auto numSamples = static_cast<size_t>(30.0 * kSr48);
            for (size_t i = 0; i < numSamples; ++i) {
                mod.process();
                peak = std::max(peak,
                                std::abs(static_cast<double>(mod.getCurrentValue())));
            }
            return peak;
        };

        const double peakFull = peakOf(1.0f);
        const double peakHalf = peakOf(0.5f);

        // The full-depth render really does run up against the bound, so the
        // comparison below is made exactly where a clamp would bite.
        REQUIRE(peakFull > 0.99);
        REQUIRE(peakFull <= 1.0);
        // 1 % of full scale: the smoother's peak attenuation at these rates is
        // ~1e-4 and applies equally to both renders, so this only rejects real
        // clamping.
        REQUIRE(std::abs(peakFull - (2.0 * peakHalf)) <= 0.01);
    }
}

// =============================================================================
// SC-002 - Bounded per-sample slew at the worst-case (max-slew) configuration
// =============================================================================

TEST_CASE("BreathingModulator_MaxSlewBounded",
          "[processors][breathing_modulator][seraphis]") {
    // Worst case per spec.md:258 -- MAXIMUM rate (0.5 Hz), MAXIMUM depth,
    // MAXIMUM irregularity (which can shorten a cycle by up to 1.5x, i.e. an
    // effective 0.75 Hz breath).
    BreathingModulator mod;
    mod.setSeed(31337u);
    mod.setRate(BreathingModulator::kMaxRate);
    mod.setDepth(1.0f);
    mod.setIrregularity(1.0f);
    mod.prepare(kSr48);

    float prev = mod.getCurrentValue();
    double maxDelta = 0.0;

    // 10 s = at least 2 full cycles even at the slowest jittered period (4 s).
    const auto numSamples = static_cast<size_t>(10.0 * kSr48);
    for (size_t i = 0; i < numSamples; ++i) {
        mod.process();
        const float cur = mod.getCurrentValue();
        maxDelta = std::max(
            maxDelta, std::abs(static_cast<double>(cur) - static_cast<double>(prev)));
        prev = cur;
    }

    // Threshold: 1e-3 of the source-range span (span = 2) = 2.0e-3 absolute.
    // Analytic bound (see the header's SC-002 note): the breath shape's peak
    // slope is |dy/dphi| ~ 3.7, so |d(out)/dt| <= 2 * 3.7 * 0.75 Hz ~= 5.6 /s,
    // i.e. ~1.2e-4 per sample at 48 kHz. Measured on this implementation
    // (g++ -O2): 1.11e-4 -- 18x of headroom under the bound.
    REQUIRE(maxDelta <= 2.0e-3);
}

// =============================================================================
// SC-003(b) / FR-022 - Dominant period matches 1/rate
// =============================================================================

TEST_CASE("BreathingModulator_PeriodMatchesRate",
          "[processors][breathing_modulator][seraphis]") {
    constexpr size_t kBlocksPerCapture = 8;  // one capture per 4096 samples
    const double captureInterval =
        static_cast<double>(kBlock * kBlocksPerCapture) / kSr48;  // ~85.3 ms

    const std::array<float, 3> rates{0.05f, 0.1f, 0.2f};
    std::array<double, 3> measuredPeriods{};

    for (size_t i = 0; i < rates.size(); ++i) {
        const double nominalPeriod = 1.0 / static_cast<double>(rates[i]);

        BreathingModulator mod;
        mod.setSeed(4242u);
        mod.setRate(rates[i]);
        mod.setDepth(1.0f);
        mod.setIrregularity(0.0f);  // exactly periodic (FR-024)
        mod.prepare(kSr48);

        // 12 full cycles: long relative to the structure being measured, so the
        // biased-estimator taper shifts the peak by << one capture interval.
        const auto trace =
            renderDecimated(mod, 12.0 * nominalPeriod, kSr48, kBlock, kBlocksPerCapture);
        REQUIRE(trace.size() > 200u);

        const auto maxLag =
            static_cast<size_t>(1.6 * nominalPeriod / captureInterval) + 4;
        const auto r = normalizedAutocorr(trace, maxLag);
        const size_t lag = firstAutocorrPeakLag(r);
        REQUIRE(lag > 0u);

        measuredPeriods[i] = static_cast<double>(lag) * captureInterval;

        // Documented tolerance band: the estimator's period resolution is one
        // capture interval (85.3 ms = 0.43 % of the slowest 20 s period), and
        // the equivalent FFT bin width at this render length is 1/(12*P), i.e.
        // 1/12 = 8.3 % in frequency. 5 % of the period sits between the two and
        // is 20x tighter than the 100 % spacing between the three settings, so
        // the check discriminates the settings rather than merely passing.
        // Measured relative error (g++ -O2): 0.59 % / 0.16 % / 1.01 % for
        // 0.05 / 0.1 / 0.2 Hz.
        REQUIRE(std::abs(measuredPeriods[i] - nominalPeriod) <= 0.05 * nominalPeriod);
    }

    // Monotone ordering across settings (SC-003(b)).
    REQUIRE(measuredPeriods[0] > measuredPeriods[1]);
    REQUIRE(measuredPeriods[1] > measuredPeriods[2]);
}

// =============================================================================
// FR-021 - Asymmetric (40/60) and non-sinusoidal breath shape
// =============================================================================

TEST_CASE("BreathingModulator_ShapeAsymmetricAndNonSinusoidal",
          "[processors][breathing_modulator][seraphis]") {
    constexpr float kRate = 0.5f;
    const auto periodSamples = static_cast<size_t>(kSr48 / static_cast<double>(kRate));

    BreathingModulator mod;
    mod.setSeed(777u);
    mod.setRate(kRate);
    mod.setDepth(1.0f);
    mod.setIrregularity(0.0f);  // exactly periodic
    mod.prepare(kSr48);

    // 5 cycles; cycle 0 is discarded so nothing depends on the initial state.
    const auto trace = renderPerSample(mod, 5u * periodSamples);
    REQUIRE(trace.size() == 5u * periodSamples);

    // ---- (a) rise duration != fall duration, ratio ~ 40/60 -----------------
    // The extrema are FLAT (the shape is quadratic-ish at both turning points)
    // and the output smoother lags, so a plain argmin/argmax lands anywhere in
    // the plateau. Mark each turning point by the moment the signal ENTERS a
    // narrow band around it instead: the entry instant sits on the steep side of
    // the plateau and is stable. Measured (g++ -O2) at both 44.1 and 48 kHz:
    // rise = 0.3993, fall = 0.6007 of the period.
    const size_t windowStart = periodSamples;
    const size_t windowEnd = windowStart + (3u * periodSamples);

    float minVal = trace[windowStart];
    float maxVal = trace[windowStart];
    for (size_t i = windowStart; i < windowEnd; ++i) {
        minVal = std::min(minVal, trace[i]);
        maxVal = std::max(maxVal, trace[i]);
    }
    REQUIRE(maxVal - minVal > 1.5f);  // the breath actually swings

    const float loBand = minVal + 0.002f;
    const float hiBand = maxVal - 0.002f;

    size_t troughIn = 0;   // first entry into the trough band
    size_t peakIn = 0;     // first entry into the peak band after that
    size_t nextTroughIn = 0;
    for (size_t i = windowStart; i < windowEnd; ++i) {
        if (troughIn == 0) {
            if (trace[i] <= loBand && trace[i - 1] > loBand) troughIn = i;
        } else if (peakIn == 0) {
            if (trace[i] >= hiBand && trace[i - 1] < hiBand) peakIn = i;
        } else if (nextTroughIn == 0) {
            if (trace[i] <= loBand && trace[i - 1] > loBand) {
                nextTroughIn = i;
                break;
            }
        }
    }
    REQUIRE(troughIn > 0u);
    REQUIRE(peakIn > troughIn);
    REQUIRE(nextTroughIn > peakIn);

    const double riseSamples = static_cast<double>(peakIn - troughIn);
    const double fallSamples = static_cast<double>(nextTroughIn - peakIn);
    const double cycleSamples = riseSamples + fallSamples;
    const double riseFraction = riseSamples / cycleSamples;
    const double fallFraction = fallSamples / cycleSamples;

    // Hardcoded split is 0.4 inhale / 0.6 exhale (plan section 2.1). The +/-0.05
    // band absorbs the band-entry bias (measured: 0.0007 of the period).
    REQUIRE(riseFraction > 0.35);
    REQUIRE(riseFraction < 0.45);
    REQUIRE(fallFraction > 0.55);
    REQUIRE(fallFraction < 0.65);
    REQUIRE(fallFraction > riseFraction);  // rise != fall (FR-021)

    // ---- (b) significant energy at 2f and 3f -------------------------------
    // Analysis window: exactly 4 cycles starting at cycle 1, decimated 32x
    // (the breath is a 0.5 Hz signal, so 1.5 kHz is grossly oversampled).
    // With 4 whole cycles in the window the fundamental lands on bin 4.
    constexpr size_t kDecim = 32;
    std::vector<double> analysis;
    analysis.reserve((4u * periodSamples) / kDecim);
    for (size_t i = windowStart; i < windowStart + (4u * periodSamples); i += kDecim) {
        analysis.push_back(static_cast<double>(trace[i]));
    }
    REQUIRE(analysis.size() > 1000u);

    const double mag1 = dftMagnitude(analysis, 4);
    const double mag2 = dftMagnitude(analysis, 8);
    const double mag3 = dftMagnitude(analysis, 12);
    REQUIRE(mag1 > 0.1);

    // A pure sine leaves these bins at the numerical floor (~1e-5 relative).
    // The 40/60 duty asymmetry ALONE (ignoring the 0.8/1.3 curvature
    // exponents) accounts for |c2|/|c1| = 11.2 % and |c3|/|c1| = 2.2 %,
    // computed analytically from the piecewise phase warp. Measured on this
    // implementation (g++ -O2): mag1 = 0.4916, 2f = 15.1 %, 3f = 2.34 %.
    // These thresholds sit 7x below the measured figures, so they reject a
    // symmetric sine by three orders of magnitude with ample margin.
    REQUIRE(mag2 / mag1 > 0.02);
    REQUIRE(mag3 / mag1 > 0.003);
}

// =============================================================================
// FR-024 - Irregularity jitters the cycle period; 0 means exactly constant
// =============================================================================

TEST_CASE("BreathingModulator_IrregularityJittersPeriod",
          "[processors][breathing_modulator][seraphis]") {
    constexpr float kRate = 0.5f;
    const double nominalPeriod = 1.0 / static_cast<double>(kRate);

    // ---- irregularity = 0: successive periods are identical ----------------
    {
        BreathingModulator mod;
        mod.setSeed(1001u);
        mod.setRate(kRate);
        mod.setDepth(1.0f);
        mod.setIrregularity(0.0f);
        mod.prepare(kSr48);

        const auto trace = renderPerSample(mod, static_cast<size_t>(22.0 * kSr48));
        const auto periods = diffs(upwardCrossingTimes(trace, kSr48));
        REQUIRE(periods.size() >= 8u);

        double lo = periods[0];
        double hi = periods[0];
        for (double p : periods) {
            lo = std::min(lo, p);
            hi = std::max(hi, p);
            REQUIRE(p > 0.0);
            REQUIRE(std::abs(p - nominalPeriod) <= 0.02);  // 1 % of the period
        }
        REQUIRE((hi - lo) <= 0.02);
    }

    // ---- irregularity = 0.7: periods vary, stay bounded and positive -------
    {
        constexpr float kIrregularity = 0.7f;
        BreathingModulator mod;
        mod.setSeed(1001u);
        mod.setRate(kRate);
        mod.setDepth(1.0f);
        mod.setIrregularity(kIrregularity);
        mod.prepare(kSr48);

        const auto trace = renderPerSample(mod, static_cast<size_t>(65.0 * kSr48));
        const auto periods = diffs(upwardCrossingTimes(trace, kSr48));
        REQUIRE(periods.size() >= 15u);

        // The jitter factor is 1 + irregularity*0.5*U(-1,1), so it lies in
        // [1 - 0.35, 1 + 0.35] and the period (nominal / jitter) lies in
        // [P/1.35, P/0.65]. A measured crossing-to-crossing interval is a
        // convex mixture of two consecutive cycles' periods, so it obeys the
        // SAME bounds. 0.05 s of slack covers crossing interpolation.
        const double loBound =
            nominalPeriod / (1.0 + (0.5 * static_cast<double>(kIrregularity))) - 0.05;
        const double hiBound =
            nominalPeriod / (1.0 - (0.5 * static_cast<double>(kIrregularity))) + 0.05;

        double lo = periods[0];
        double hi = periods[0];
        for (double p : periods) {
            REQUIRE(p > 0.0);  // never a zero/negative period (spec edge case)
            REQUIRE(p >= loBound);
            REQUIRE(p <= hiBound);
            lo = std::min(lo, p);
            hi = std::max(hi, p);
        }

        // Periods actually VARY: a stubbed jitter (always 1.0) collapses this
        // spread to ~0 (the irregularity = 0 block above measures exactly that,
        // 0.000000 s). Measured here (g++ -O2, 30 cycles): periods span
        // 1.5654..2.8102 s, a spread of 1.245 s against a 0.1 s requirement.
        REQUIRE((hi - lo) > 0.05 * nominalPeriod);
    }
}

// =============================================================================
// SC-004 - Seeded determinism and reset() rewind
// =============================================================================

TEST_CASE("BreathingModulator_SeededDeterminism",
          "[processors][breathing_modulator][seraphis]") {
    constexpr size_t kNumBlocks = 4000;

    auto render = [&](std::uint32_t seed) {
        BreathingModulator mod;
        mod.setSeed(seed);
        mod.setRate(0.5f);
        mod.setDepth(1.0f);
        mod.setIrregularity(0.8f);
        mod.prepare(kSr48);
        std::vector<float> out;
        out.reserve(kNumBlocks);
        for (size_t b = 0; b < kNumBlocks; ++b) {
            mod.processBlock(kBlock);
            out.push_back(mod.getCurrentValue());
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

TEST_CASE("BreathingModulator_ResetRewindsToSeed",
          "[processors][breathing_modulator][seraphis]") {
    constexpr size_t kNumBlocks = 4000;

    BreathingModulator mod;
    mod.setSeed(4711u);
    mod.setRate(0.4f);
    mod.setDepth(0.8f);
    mod.setIrregularity(0.6f);
    mod.prepare(kSr48);

    std::vector<float> first;
    first.reserve(kNumBlocks);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        mod.processBlock(kBlock);
        first.push_back(mod.getCurrentValue());
    }

    mod.reset();

    std::vector<float> second;
    second.reserve(kNumBlocks);
    for (size_t b = 0; b < kNumBlocks; ++b) {
        mod.processBlock(kBlock);
        second.push_back(mod.getCurrentValue());
    }

    REQUIRE(first == second);
}

// =============================================================================
// SC-005 (option a) - Sample-rate invariance, like-for-like
// =============================================================================

TEST_CASE("BreathingModulator_SampleRateInvariant",
          "[processors][breathing_modulator][seraphis]") {
    // BreathingModulator's only RNG draw happens at a cycle boundary, which is
    // wall-clock aligned, so 44.1 kHz and 96 kHz produce the SAME wall-clock
    // trajectory (spec SC-005 option (a)) rather than two realizations.
    // Both renders capture on a 10 ms grid (blockSize = sampleRate / 100), so
    // the two traces are directly comparable sample-for-sample in time.
    constexpr double kSeconds = 100.0;
    constexpr float kRate = 0.2f;  // 5 s nominal period -> 20 cycles

    struct Stats {
        double rms;
        double mean;
        double period;
    };

    auto measure = [&](double sampleRate, float irregularity) {
        const auto blockSize = static_cast<size_t>(sampleRate / 100.0);  // 10 ms
        const double captureInterval = static_cast<double>(blockSize) / sampleRate;

        BreathingModulator mod;
        mod.setSeed(5150u);
        mod.setRate(kRate);
        mod.setDepth(1.0f);
        mod.setIrregularity(irregularity);
        mod.prepare(sampleRate);

        const auto trace = renderDecimated(mod, kSeconds, sampleRate, blockSize, 1u);
        REQUIRE(trace.size() > 9000u);

        const auto maxLag =
            static_cast<size_t>(1.6 / (static_cast<double>(kRate) * captureInterval)) + 4;
        const auto r = normalizedAutocorr(trace, maxLag);
        const size_t lag = firstAutocorrPeakLag(r);
        REQUIRE(lag > 0u);

        return Stats{.rms = rmsOf(trace),
                     .mean = meanOf(trace),
                     .period = static_cast<double>(lag) * captureInterval};
    };

    // --- deterministic breath (irregularity = 0) ---------------------------
    {
        const Stats a = measure(44100.0, 0.0f);
        const Stats b = measure(96000.0, 0.0f);

        REQUIRE(a.rms > 0.1);
        // Tolerance: the two renders differ only by phase-accumulator rounding
        // and the 10 ms capture grid alignment, so 2 % is generous. Measured
        // (g++ -O2): RMS 0.71794 at both rates, period 4.980 s at both.
        REQUIRE(std::abs(a.rms - b.rms) / a.rms <= 0.02);
        // The source is ~zero-mean, so a RELATIVE mean comparison divides by
        // ~0 and is undefined. Compare absolutely in source-range-span units
        // (span = 2): 0.05 absolute = 2.5 % of span.
        REQUIRE(std::abs(a.mean - b.mean) <= 0.05);
        // Period estimate is quantized to the (identical, 10 ms) capture grid.
        REQUIRE(std::abs(a.period - b.period) / a.period <= 0.05);
    }

    // --- jittered breath: the RNG draw schedule must be rate-independent ----
    {
        const Stats a = measure(44100.0, 0.5f);
        const Stats b = measure(96000.0, 0.5f);

        REQUIRE(a.rms > 0.1);
        // If the jitter draw were tied to a sample-count grid instead of the
        // cycle boundary, the two rates would draw a different number of times
        // and this would drift well past 2 %. Measured (g++ -O2): RMS 0.71793
        // and mean 0.13785 at BOTH rates -- the trajectories coincide.
        REQUIRE(std::abs(a.rms - b.rms) / a.rms <= 0.02);
        REQUIRE(std::abs(a.mean - b.mean) <= 0.05);
    }
}

// =============================================================================
// SC-006 - Allocation-free steady state
// =============================================================================

TEST_CASE("BreathingModulator_NoAllocInProcess",
          "[processors][breathing_modulator][seraphis]") {
    BreathingModulator mod;
    mod.setSeed(64u);
    mod.setRate(0.3f);
    mod.setDepth(1.0f);
    mod.setIrregularity(0.5f);
    mod.prepare(kSr48);
    mod.processBlock(kBlock);  // warm-up OUTSIDE the tracking scope

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    for (int i = 0; i < 500; ++i) {
        mod.processBlock(kBlock);
    }
    for (int i = 0; i < 4096; ++i) {
        mod.process();
    }
    const size_t allocations = detector.stopTracking();

    REQUIRE(allocations == 0u);
}

// =============================================================================
// FR-006 - getSourceRange() is fixed at polarity full scale
// =============================================================================

TEST_CASE("BreathingModulator_SourceRangeIndependentOfDepth",
          "[processors][breathing_modulator][seraphis]") {
    BreathingModulator mod;
    mod.prepare(kSr48);

    for (float depth : {0.0f, 0.5f, 1.0f}) {
        mod.setDepth(depth);
        const auto range = mod.getSourceRange();
        REQUIRE(range.first == -1.0f);
        REQUIRE(range.second == 1.0f);
    }
}

// =============================================================================
// FR-004 - Output is well defined after prepare() and after reset()
// =============================================================================

TEST_CASE("BreathingModulator_OutputDefinedAfterPrepare",
          "[processors][breathing_modulator][seraphis]") {
    BreathingModulator mod;
    mod.setSeed(11u);
    mod.setRate(0.25f);
    mod.setDepth(0.6f);
    mod.setIrregularity(0.4f);
    mod.prepare(kSr48);

    const auto range = mod.getSourceRange();

    {
        const float v = mod.getCurrentValue();  // no intervening advance
        REQUIRE(isFiniteValue(v));
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    }

    for (int i = 0; i < 1000; ++i) mod.processBlock(kBlock);
    mod.reset();

    {
        const float v = mod.getCurrentValue();  // no intervening advance
        REQUIRE(isFiniteValue(v));
        REQUIRE(v >= range.first);
        REQUIRE(v <= range.second);
    }
}

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("BreathingModulator_EdgeCases",
          "[processors][breathing_modulator][seraphis]") {
    SECTION("degenerate sample rates neither hang nor poison the output") {
        // RT safety (the worst failure class there is): prepare(0) made
        // phaseInc_ non-finite, and advancePhase's `while (phase_ >= 1.0)`
        // cycle-wrap loop then never terminated - an audio-thread hang, not
        // merely a wrong value. prepare() floors the sample rate at 1 Hz, which
        // also bounds that loop's trip count. Reaching the assertions at all is
        // half of what this section proves.
        for (double sr : {0.0, -48000.0, 0.25}) {
            BreathingModulator mod;
            mod.setSeed(1234u);
            mod.setRate(BreathingModulator::kMaxRate);
            mod.setDepth(1.0f);
            mod.setIrregularity(1.0f);
            mod.prepare(sr);

            bool finite = true;
            bool inRange = true;
            const auto observe = [&](float v) {
                if (!isFiniteValue(v)) finite = false;
                if (v < -1.0f || v > 1.0f) inRange = false;
            };

            for (int b = 0; b < 8; ++b) {
                mod.processBlock(kBlock);
                observe(mod.getCurrentValue());
            }
            for (int i = 0; i < 2000; ++i) {
                mod.process();
                observe(mod.getCurrentValue());
            }

            REQUIRE(finite);
            REQUIRE(inRange);
        }
    }

    SECTION("processBlock(0) is a no-op") {
        BreathingModulator withZeros;
        BreathingModulator reference;
        for (BreathingModulator* m : {&withZeros, &reference}) {
            m->setSeed(555u);
            m->setRate(0.4f);
            m->setDepth(1.0f);
            m->setIrregularity(0.5f);
            m->prepare(kSr48);
        }

        for (int b = 0; b < 200; ++b) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
        }

        const float before = withZeros.getCurrentValue();
        for (int i = 0; i < 10; ++i) withZeros.processBlock(0);
        REQUIRE(withZeros.getCurrentValue() == before);

        // Phase, jitter, RNG stream and smoother are all untouched as well.
        std::vector<float> a;
        std::vector<float> b2;
        for (int i = 0; i < 200; ++i) {
            withZeros.processBlock(kBlock);
            reference.processBlock(kBlock);
            a.push_back(withZeros.getCurrentValue());
            b2.push_back(reference.getCurrentValue());
        }
        REQUIRE(a == b2);
    }

    SECTION("irregularity = 1 never yields a non-positive period") {
        BreathingModulator mod;
        mod.setSeed(8675309u);
        mod.setRate(BreathingModulator::kMaxRate);  // 2 s nominal period
        mod.setDepth(1.0f);
        mod.setIrregularity(1.0f);
        mod.prepare(kSr48);

        const auto trace = renderPerSample(mod, static_cast<size_t>(60.0 * kSr48));
        const auto periods = diffs(upwardCrossingTimes(trace, kSr48));
        REQUIRE(periods.size() >= 10u);

        // jitter = 1 + 0.5*U(-1,1) in [0.5, 1.5] (the kMinJitter = 0.1 floor is
        // never reached at this setting), so every period is in [1.33 s, 4.0 s].
        for (double p : periods) {
            REQUIRE(p > 0.0);
            REQUIRE(p <= 4.2);
        }
    }

    SECTION("rate is clamped to [kMinRate, kMaxRate]") {
        const float minRate = BreathingModulator::kMinRate;
        const float maxRate = BreathingModulator::kMaxRate;

        BreathingModulator mod;
        mod.setRate(-5.0f);
        REQUIRE(mod.getRate() == minRate);
        mod.setRate(100.0f);
        REQUIRE(mod.getRate() == maxRate);
    }

    SECTION("seed 0 is handled safely") {
        BreathingModulator mod;
        mod.setSeed(0u);  // Xorshift32 substitutes its default seed
        mod.setRate(0.5f);
        mod.setDepth(1.0f);
        mod.setIrregularity(1.0f);
        mod.prepare(kSr48);

        const auto trace = renderDecimated(mod, 60.0, kSr48, kBlock, 1u);
        REQUIRE(trace.size() > 1000u);

        bool allFinite = true;
        bool inRange = true;
        for (float v : trace) {
            if (!isFiniteValue(v)) allFinite = false;
            if (v < -1.0f || v > 1.0f) inRange = false;
        }
        REQUIRE(allFinite);
        REQUIRE(inRange);
        REQUIRE(rmsOf(trace) > 0.1);  // the breath actually moves
    }
}
