// ==============================================================================
// Layer 2: Processor Tests - PerlinNoiseSource (Vorago Phase 1)
// ==============================================================================
// Spec:  specs/vorago-phase1-events-modulation/spec.md
// Plan:  specs/vorago-phase1-events-modulation/plan.md   (section 1)
// Tasks: specs/vorago-phase1-events-modulation/tasks.md  (T002)
//
// Covers: FR-001, FR-002, FR-003, FR-006, FR-012, FR-015, FR-016, FR-017,
//         FR-018, FR-019 and SC-001..SC-005, SC-013 (Perlin half).
//
// NOTE ON ALLOCATION TRACKING (single-owner rule):
//   This TU includes <allocation_detector.h> ONLY. brownian_drift_test.cpp:27-28
//   is the single owner of <allocation_operator_overrides.h> in
//   dsp_processors_tests; a second include is a duplicate-symbol link error
//   (documented at life_modulators_perf_test.cpp:19-23).
//
// NOTE ON FINITENESS:
//   Krate::DSP::detail::isFinite (db_utils.h:118 float / :126 double) is used
//   everywhere. std::isnan/isinf/isfinite are BANNED by
//   tools/lint-nonfinite-symbols.js and are folded away by -ffast-math on the
//   macOS leg. Non-finite *inputs* for the setter-clamp cases are likewise built
//   from IEEE-754 bit patterns through a volatile sink, never from
//   std::numeric_limits (which fast-math folds to finite garbage).
//
// NOTE ON THRESHOLDS:
//   Every numeric bound below is a MEASURED tolerance or an analytic closed form
//   evaluated in the test. There is no checked-in float digest anywhere: the only
//   exact-equality comparisons are same-binary/same-run determinism comparisons
//   (SC-004), i.e. one build against itself.
//
// NOTE ON ASSERTION DENSITY:
//   The long corner renders below sweep up to ~1e8 control steps. A REQUIRE per
//   sample would make Catch2's assertion bookkeeping, not the DSP, the run time.
//   The per-sample clauses are therefore accumulated into worst-case reductions
//   (maxAbs / anyNonFinite / anyNonZero) inside the render loop and asserted once
//   per corner. The semantics are identical: the reduction fails iff at least one
//   sample violates the clause.
// ==============================================================================

#include <krate/dsp/processors/perlin_noise_source.h>

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

constexpr double kSr48 = 48000.0;
constexpr double kSr44 = 44100.0;
constexpr double kSr96 = 96000.0;
constexpr double kPi = 3.14159265358979323846;

/// The eight seeds used by every multi-seed clause in this TU.
constexpr std::array<std::uint32_t, 8> kSeeds = {1u,       7u,        0x9E37u, 12345u,
                                                 0xBEEFu,  424242u,   0x51E7u, 987654321u};

// -----------------------------------------------------------------------------
// Non-finite float construction that survives -ffast-math
// -----------------------------------------------------------------------------

/// Build a float from an IEEE-754 bit pattern through a volatile sink. A plain
/// std::numeric_limits<float>::infinity()/quiet_NaN() is constant-folded to
/// finite garbage under the macOS leg's -ffast-math.
[[nodiscard]] float floatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t local = sink;
    float value = 0.0f;
    std::memcpy(&value, &local, sizeof(value));
    return value;
}

// -----------------------------------------------------------------------------
// Source construction helper
// -----------------------------------------------------------------------------

/// Configure BEFORE prepare(), matching the component's re-init semantics
/// (FR-002: prepare() is a full re-initialisation).
void configureSource(PerlinNoiseSource& src, std::uint32_t seed, float rate, int octaves,
                     float depth, double sampleRate) noexcept {
    src.setSeed(seed);
    src.setRate(rate);
    src.setOctaves(octaves);
    src.setDepth(depth);
    src.prepare(sampleRate);
}

// -----------------------------------------------------------------------------
// Control-rate trajectory rendering
// -----------------------------------------------------------------------------

/// Render `numPoints` observations, advancing `decimation` control steps
/// (kControlRateInterval samples each) between captures.
/// Wall-clock covered = numPoints * decimation * kControlRateInterval / sampleRate.
[[nodiscard]] std::vector<double> renderControl(std::uint32_t seed, float rate, int octaves,
                                                std::size_t numPoints, std::size_t decimation,
                                                double sampleRate) {
    PerlinNoiseSource src;
    configureSource(src, seed, rate, octaves, 1.0f, sampleRate);

    std::vector<double> out;
    out.reserve(numPoints);
    for (std::size_t i = 0; i < numPoints; ++i) {
        for (std::size_t d = 0; d < decimation; ++d) {
            src.processBlock(PerlinNoiseSource::kControlRateInterval);
        }
        out.push_back(static_cast<double>(src.getCurrentValue()));
    }
    return out;
}

// -----------------------------------------------------------------------------
// Local double-precision radix-2 FFT (SC-003)
// -----------------------------------------------------------------------------
// Deliberately NOT Krate::DSP::FFT: fft.h:47 documents kMaxFFTSize = 8192 while
// prepare() validates only power-of-two, and the -129 / -84 dB reference figures
// SC-003(a) asserts sit near a float32 FFT's noise floor at 65 536 points. The
// twiddles are recomputed with cos/sin per butterfly rather than by the usual
// recurrence: the recurrence's accumulated error over len = 65 536 is of the same
// order as the -129 dB figure being measured.

void fftInPlace(std::vector<double>& re, std::vector<double>& im) {
    const std::size_t n = re.size();

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0u; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half = len / 2;
        for (std::size_t base = 0; base < n; base += len) {
            for (std::size_t k = 0; k < half; ++k) {
                const double ang =
                    -2.0 * kPi * static_cast<double>(k) / static_cast<double>(len);
                const double wr = std::cos(ang);
                const double wi = std::sin(ang);
                const double ur = re[base + k];
                const double ui = im[base + k];
                const double xr = re[base + k + half];
                const double xi = im[base + k + half];
                const double vr = xr * wr - xi * wi;
                const double vi = xr * wi + xi * wr;
                re[base + k] = ur + vr;
                im[base + k] = ui + vi;
                re[base + k + half] = ur - vr;
                im[base + k + half] = ui - vi;
            }
        }
    }
}

struct Spectrum {
    std::vector<double> power;  ///< one-sided, index 0 .. fftSize/2
    double binHz = 0.0;
};

/// Hann-windowed (mandatory per SC-003 - a rectangular window's leakage skirt
/// would put energy above the band edges that is an artefact of the transform),
/// zero-padded to `fftSize`.
[[nodiscard]] Spectrum computeSpectrum(const std::vector<double>& x, double sampleHz,
                                       std::size_t fftSize) {
    std::vector<double> re(fftSize, 0.0);
    std::vector<double> im(fftSize, 0.0);
    const std::size_t length = std::min(x.size(), fftSize);
    for (std::size_t i = 0; i < length; ++i) {
        const double w =
            0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i)
                                  / static_cast<double>(length - 1)));
        re[i] = x[i] * w;
    }
    fftInPlace(re, im);

    Spectrum s;
    s.binHz = sampleHz / static_cast<double>(fftSize);
    s.power.resize(fftSize / 2 + 1);
    for (std::size_t k = 0; k < s.power.size(); ++k) {
        s.power[k] = re[k] * re[k] + im[k] * im[k];
    }
    return s;
}

[[nodiscard]] double fracAbove(const Spectrum& s, double freqHz) {
    double total = 0.0;
    double above = 0.0;
    for (std::size_t k = 0; k < s.power.size(); ++k) {
        total += s.power[k];
        if (static_cast<double>(k) * s.binHz > freqHz) {
            above += s.power[k];
        }
    }
    return (total > 0.0) ? (above / total) : 0.0;
}

[[nodiscard]] double fracBelow(const Spectrum& s, double freqHz) {
    double total = 0.0;
    double below = 0.0;
    for (std::size_t k = 0; k < s.power.size(); ++k) {
        total += s.power[k];
        if (static_cast<double>(k) * s.binHz <= freqHz) {
            below += s.power[k];
        }
    }
    return (total > 0.0) ? (below / total) : 0.0;
}

/// Loudest bin strictly above `freqHz`, in dB relative to the spectrum peak.
[[nodiscard]] double peakRelativeDbAbove(const Spectrum& s, double freqHz) {
    double peak = 0.0;
    double maxAbove = 0.0;
    for (std::size_t k = 0; k < s.power.size(); ++k) {
        peak = std::max(peak, s.power[k]);
        if (static_cast<double>(k) * s.binHz > freqHz) {
            maxAbove = std::max(maxAbove, s.power[k]);
        }
    }
    if (peak <= 0.0) {
        return 0.0;
    }
    const double ratio = maxAbove / peak;
    return 10.0 * std::log10(ratio > 1e-300 ? ratio : 1e-300);
}

// -----------------------------------------------------------------------------
// Correlation helpers
// -----------------------------------------------------------------------------

[[nodiscard]] double meanOf(const std::vector<double>& x) {
    double sum = 0.0;
    for (const double v : x) {
        sum += v;
    }
    return x.empty() ? 0.0 : sum / static_cast<double>(x.size());
}

[[nodiscard]] double autocorrAtLag(const std::vector<double>& x, std::size_t lag) {
    if (x.size() <= lag) {
        return 0.0;
    }
    const double mean = meanOf(x);
    double den = 0.0;
    for (const double v : x) {
        const double d = v - mean;
        den += d * d;
    }
    double num = 0.0;
    for (std::size_t i = 0; i + lag < x.size(); ++i) {
        num += (x[i] - mean) * (x[i + lag] - mean);
    }
    return (den > 0.0) ? (num / den) : 0.0;
}

[[nodiscard]] double crossCorrZeroLag(const std::vector<double>& a,
                                      const std::vector<double>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n == 0) {
        return 0.0;
    }
    double meanA = 0.0;
    double meanB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        meanA += a[i];
        meanB += b[i];
    }
    meanA /= static_cast<double>(n);
    meanB /= static_cast<double>(n);

    double num = 0.0;
    double denA = 0.0;
    double denB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = a[i] - meanA;
        const double db = b[i] - meanB;
        num += da * db;
        denA += da * da;
        denB += db * db;
    }
    const double den = std::sqrt(denA * denB);
    return (den > 0.0) ? (num / den) : 0.0;
}

}  // namespace

// =============================================================================
// 1. FR-001 / FR-019 - shared ModulationSource contract and declared defaults
// =============================================================================

TEST_CASE("PerlinNoiseSource_SharedContract", "[processors][perlin][vorago]") {
    static_assert(std::is_base_of_v<Krate::DSP::ModulationSource, Krate::DSP::PerlinNoiseSource>,
                  "FR-001: PerlinNoiseSource must publicly derive from ModulationSource");

    PerlinNoiseSource src;

    // ---- FR-019: defaults are in force after DEFAULT CONSTRUCTION -----------
    REQUIRE(src.getRate() == PerlinNoiseSource::kDefaultRate);      // 0.1f
    REQUIRE(src.getOctaves() == PerlinNoiseSource::kDefaultOctaves);  // 2
    REQUIRE(src.getDepth() == PerlinNoiseSource::kDefaultDepth);    // 1.0f
    // n(0) = 0 exactly for every seed: at a lattice node both gradient terms
    // vanish (t = 0 => g0*t = 0, and the smootherstep weight s(0) = 0).
    REQUIRE(src.getCurrentValue() == 0.0f);

    // ---- FR-001: bind through a BASE HANDLE (virtual dispatch, not shadowing)
    ModulationSource& ms = src;
    REQUIRE(ms.getCurrentValue() == src.getCurrentValue());
    REQUIRE(ms.getSourceRange() == std::pair{-1.0f, 1.0f});

    // ---- FR-002/FR-019: same after prepare() with NO configuration call -----
    src.prepare(kSr48);
    REQUIRE(src.getRate() == PerlinNoiseSource::kDefaultRate);
    REQUIRE(src.getOctaves() == PerlinNoiseSource::kDefaultOctaves);
    REQUIRE(src.getDepth() == PerlinNoiseSource::kDefaultDepth);
    REQUIRE(src.getCurrentValue() == 0.0f);

    REQUIRE(ms.getCurrentValue() == src.getCurrentValue());
    REQUIRE(ms.getSourceRange() == std::pair{-1.0f, 1.0f});
}

// =============================================================================
// 2. SC-001 - boundedness is ANALYTIC, not supplied by the terminal clamp
// =============================================================================
// Corner grid: rate in {kMinRate, kMaxRate} x octaves in {1,2,3,4}
//              x depth in {0, 0.5, 1} x 8 seeds, 300 s each at 48 kHz.
//
// Driven with processBlock(kControlRateInterval), one capture per control step.
// The bound / finiteness / depth-scaling clauses are not per-sample-rate
// sensitive (the output is a 32-sample staircase through a one-pole, FR-003), so
// a per-sample render would be ~1.4e10 samples for no additional discrimination.

TEST_CASE("PerlinNoiseSource_NeverExceedsRange", "[processors][perlin][vorago]") {
    constexpr double kSeconds = 300.0;
    const std::size_t kControlSteps = static_cast<std::size_t>(
        kSeconds * kSr48 / static_cast<double>(PerlinNoiseSource::kControlRateInterval));

    constexpr std::array<float, 2> kRates = {PerlinNoiseSource::kMinRate,
                                             PerlinNoiseSource::kMaxRate};
    constexpr std::array<int, 4> kOctaveCounts = {1, 2, 3, 4};
    constexpr std::array<float, 3> kDepths = {0.0f, 0.5f, 1.0f};

    for (const float rate : kRates) {
        for (const int octaves : kOctaveCounts) {
            for (const std::uint32_t seed : kSeeds) {
                std::array<double, 3> peak = {0.0, 0.0, 0.0};

                for (std::size_t di = 0; di < kDepths.size(); ++di) {
                    const float depth = kDepths[di];

                    PerlinNoiseSource src;
                    configureSource(src, seed, rate, octaves, depth, kSr48);

                    // FR-006: the published range must NOT shrink with depth.
                    // Asserted at depth 0, 0.5 AND 1 inside the corner loop.
                    REQUIRE(src.getSourceRange() == std::pair{-1.0f, 1.0f});

                    double maxAbs = 0.0;
                    bool anyNonFinite = false;
                    bool anyNonZero = false;

                    for (std::size_t i = 0; i < kControlSteps; ++i) {
                        src.processBlock(PerlinNoiseSource::kControlRateInterval);
                        const float v = src.getCurrentValue();
                        if (!detail::isFinite(v)) {
                            anyNonFinite = true;
                        }
                        const double av = std::abs(static_cast<double>(v));
                        maxAbs = std::max(maxAbs, av);
                        if (v != 0.0f) {
                            anyNonZero = true;
                        }
                    }

                    REQUIRE_FALSE(anyNonFinite);
                    REQUIRE(maxAbs <= 1.0);

                    // FR-016: depth 0 is exactly 0 at EVERY sample, not "small".
                    if (depth == 0.0f) {
                        REQUIRE_FALSE(anyNonZero);
                    }

                    peak[di] = maxAbs;
                }

                // ---- Non-tautology proof (SC-001) ---------------------------
                // Asserted only where the excursion is statistically forced,
                // i.e. rate >= 0.1 Hz (>= 30 lattice cells in 300 s). At
                // kMinRate a 300 s render advances 1.5 cells and the peak is
                // seed-dependent (0.26-0.36) - a guaranteed intermittent
                // failure. The bound / finiteness / depth-scaling clauses above
                // still run at EVERY corner, kMinRate included.
                if (rate >= 0.1f) {
                    REQUIRE(peak[2] > 0.5);  // depth 1.0 really moves
                    // Exact half-scaling is impossible if a clamp were engaging.
                    REQUIRE(std::abs(peak[1] / peak[2] - 0.5) < 1e-4);
                }
            }
        }
    }
}

// =============================================================================
// 3. SC-002 - bounded derivative (max per-sample slew), PER-SAMPLE at 48 kHz
// =============================================================================
// Never accelerated: the quantity under test is the per-sample delta of the
// smoothed staircase, which processBlock() skips over by construction.
//
// This case catches UNDER-smoothing only. The over-smoothing half of FR-018 is
// carried by SC-003 clause (c)'s absolute floor
// (PerlinNoiseSource_SpectralRolloff), which a 20 ms smoother fails.

TEST_CASE("PerlinNoiseSource_MaxSlewBounded", "[processors][perlin][vorago]") {
    constexpr double kSeconds = 120.0;
    const std::size_t kNumSamples = static_cast<std::size_t>(kSeconds * kSr48);
    constexpr std::uint32_t kPinnedSeed = 0x9E37u;

    // The prediction is computed HERE with 5.0 as a LITERAL, deliberately not
    // PerlinNoiseSource::kOutputSmoothMs: using the class constant would make
    // the band self-referential (a wrong smoother time would move the band with
    // the measurement and the case could never fail).
    const double kMaxSlope = 2.7;  // kGradientNormalize 2.0 x per-cell raw max 1.35
    const double alpha = 1.0 - std::exp(-5000.0 / (5.0 * 48000.0));    // 2.0618e-2
    const double gain = alpha / (1.0 - std::pow(1.0 - alpha, 32.0));   // 4.2373e-2

    const auto measureMaxSlew = [&](int octaves) {
        PerlinNoiseSource src;
        configureSource(src, kPinnedSeed, PerlinNoiseSource::kMaxRate, octaves, 1.0f, kSr48);

        float prev = src.getCurrentValue();
        double measured = 0.0;
        for (std::size_t i = 0; i < kNumSamples; ++i) {
            src.process();
            const float v = src.getCurrentValue();
            measured = std::max(measured, std::abs(static_cast<double>(v) -
                                                   static_cast<double>(prev)));
            prev = v;
        }
        return measured;
    };

    SECTION("octaves = 4 (worst case)") {
        const double fbm4 = 32.0 / 15.0;  // = 2.1333 = Sum(a_k*l_k)/Sum(a_k), n = 4
        const double predicted = gain * kMaxSlope * fbm4 * 5.0 * 32.0 / 48000.0;  // 8.118e-4
        const double measured = measureMaxSlew(4);

        // Two-sided band: an unbounded implementation fails the upper edge, an
        // over-smoothed or frozen one fails the lower edge.
        REQUIRE(measured <= predicted);
        REQUIRE(measured >= 0.5 * predicted);
        // Sanity: the closed form itself must sit inside the roadmap-level
        // 1.0e-3-of-range-span (= 2.0e-3 bipolar) slew budget.
        REQUIRE(predicted <= 2.0e-3);
    }

    SECTION("octaves = 1") {
        const double fbm1 = 1.0;
        const double predicted = gain * kMaxSlope * fbm1 * 5.0 * 32.0 / 48000.0;  // 3.81e-4
        const double measured = measureMaxSlew(1);

        REQUIRE(measured <= predicted);
        REQUIRE(measured >= 0.5 * predicted);
        REQUIRE(predicted <= 2.0e-3);
    }
}

// =============================================================================
// 4. SC-003 - spectral rolloff ("band-limited by construction")
// =============================================================================

TEST_CASE("PerlinNoiseSource_SpectralRolloff", "[processors][perlin][vorago]") {
    constexpr std::size_t kFftSize = 65536;
    constexpr std::uint32_t kPinnedSeed = 0x9E37u;

    SECTION("clauses (a)(b)(d) at rate = 0.1 Hz") {
        // 600 s at 48 kHz => 900 000 control steps (1500 Hz); decimate by 15 to
        // 100 Hz => 60 000 points. The fastest content at this rate is
        // 0.1 * 2^3 = 0.8 Hz, 60x below the resulting 50 Hz Nyquist, so no
        // anti-alias filter is needed. Zero-pad to 65 536 => 1.526 mHz bins.
        constexpr double kRate = 0.1;
        constexpr double kGridHz = 100.0;
        constexpr std::size_t kPoints = 60000;
        constexpr std::size_t kDecimate = 15;

        std::array<double, 4> fracAbove4x = {0.0, 0.0, 0.0, 0.0};
        std::vector<double> traj1;

        for (int n = 1; n <= 4; ++n) {
            const std::vector<double> traj = renderControl(
                kPinnedSeed, static_cast<float>(kRate), n, kPoints, kDecimate, kSr48);
            if (n == 1) {
                traj1 = traj;
            }
            const Spectrum spec = computeSpectrum(traj, kGridHz, kFftSize);

            // ---- (a) band limitation, asserted at EVERY octave count --------
            // Measured: fraction below 8*rate = 1.0000 / 1.0000 / 0.99999 / 0.99979
            REQUIRE(fracBelow(spec, 8.0 * kRate) >= 0.99);
            // Measured loudest bin above 32*rate: -129 / -114 / -97 / -84 dB.
            REQUIRE(peakRelativeDbAbove(spec, 32.0 * kRate) <= -30.0);

            fracAbove4x[static_cast<std::size_t>(n - 1)] = fracAbove(spec, 4.0 * kRate);
        }

        // ---- (b) roughness monotonicity ------------------------------------
        // Measured: 7e-9 -> 3.0e-5 -> 9.0e-4 -> 8.55e-3
        REQUIRE(fracAbove4x[1] > fracAbove4x[0]);
        REQUIRE(fracAbove4x[2] > fracAbove4x[1]);
        REQUIRE(fracAbove4x[3] > fracAbove4x[2]);
        // n = 4 fraction is >= 10x the n = 2 fraction (measured ratio ~285x).
        REQUIRE(fracAbove4x[3] >= 10.0 * fracAbove4x[1]);

        // ---- (d) not white, not a sine (n = 1) ------------------------------
        // Lag 0.1/rate = 1 s = 100 samples at the 100 Hz grid. White noise gives
        // ~0 here; measured 0.936. Lag-1 is deliberately NOT used: at 100 Hz it
        // is ~0.99999 for ANY smooth signal and cannot fail.
        const std::size_t lagTenth =
            static_cast<std::size_t>(std::lround(0.1 / kRate * kGridHz));
        REQUIRE(autocorrAtLag(traj1, lagTenth) > 0.7);
        // Lag 2/rate = 20 s = 2000 samples. A sine would hold near 1;
        // measured 0.152.
        const std::size_t lagTwoCells =
            static_cast<std::size_t>(std::lround(2.0 / kRate * kGridHz));
        REQUIRE(std::abs(autocorrAtLag(traj1, lagTwoCells)) < 0.35);
    }

    SECTION("clause (c) at rate = kMaxRate (5 Hz)") {
        // The stride MUST change here and nowhere else: the top octave is 40 Hz
        // and the band edge 32*rate is 160 Hz, both at/above a 100 Hz grid's
        // 50 Hz Nyquist. Use the UNDECIMATED 1500 Hz control trajectory, first
        // 65 536 points (43.7 s), Hann-windowed, no zero-padding => 22.9 mHz
        // bins, 750 Hz Nyquist.
        const double kRate = static_cast<double>(PerlinNoiseSource::kMaxRate);
        const double kGridHz = kSr48 / static_cast<double>(
                                          PerlinNoiseSource::kControlRateInterval);  // 1500 Hz
        constexpr std::size_t kPoints = kFftSize;

        std::array<double, 4> fracAbove4x = {0.0, 0.0, 0.0, 0.0};
        double fracAbove8xAtN4 = 0.0;

        for (int n = 1; n <= 4; ++n) {
            const std::vector<double> traj = renderControl(
                kPinnedSeed, static_cast<float>(kRate), n, kPoints, std::size_t{1}, kSr48);
            const Spectrum spec = computeSpectrum(traj, kGridHz, kFftSize);
            fracAbove4x[static_cast<std::size_t>(n - 1)] = fracAbove(spec, 4.0 * kRate);
            if (n == 4) {
                fracAbove8xAtN4 = fracAbove(spec, 8.0 * kRate);
            }
        }

        REQUIRE(fracAbove4x[1] > fracAbove4x[0]);
        REQUIRE(fracAbove4x[2] > fracAbove4x[1]);
        REQUIRE(fracAbove4x[3] > fracAbove4x[2]);

        // ---- ABSOLUTE FLOOR -------------------------------------------------
        // Monotonicity alone cannot fail: it is preserved by ANY low-pass -
        // measured monotonic at 1, 5, 20 AND 100 ms output smoothing. The floor
        // is what gives FR-018's smoother sizing teeth.
        //   8*rate = 40 Hz, n = 4:
        //     kOutputSmoothMs =  5 ms -> 1.706e-4 .. 2.012e-4 across 8 seeds
        //     kOutputSmoothMs = 20 ms -> 7.941e-5 .. 9.588e-5 across 8 seeds
        //   The floor sits 1.31x below the worst 5 ms seed and 1.36x above the
        //   worst 20 ms seed. (T004 injects 20 ms and REQUIRES this go red.)
        REQUIRE(fracAbove8xAtN4 >= 1.30e-4);
    }
}

// =============================================================================
// 5. SC-004 - seeded determinism and position independence (FR-012 / FR-015)
// =============================================================================

TEST_CASE("PerlinNoiseSource_SeededDeterminism", "[processors][perlin][vorago]") {
    constexpr std::size_t kBlock = 512;
    constexpr std::size_t kCapturedBlocks = 400;

    const auto renderBlocks = [&](std::uint32_t seed) {
        PerlinNoiseSource src;
        configureSource(src, seed, 0.5f, 3, 1.0f, kSr48);
        std::vector<float> out;
        out.reserve(kCapturedBlocks);
        for (std::size_t i = 0; i < kCapturedBlocks; ++i) {
            src.processBlock(kBlock);
            out.push_back(src.getCurrentValue());
        }
        return out;
    };

    SECTION("same seed is bit-identical, different seeds differ, reset() rewinds") {
        const std::vector<float> a = renderBlocks(4242u);
        const std::vector<float> b = renderBlocks(4242u);
        REQUIRE(a == b);

        const std::vector<float> c = renderBlocks(4243u);
        REQUIRE(a != c);

        // reset() reproduces run 1 exactly (FR-002 / FR-005).
        PerlinNoiseSource src;
        configureSource(src, 4242u, 0.5f, 3, 1.0f, kSr48);
        std::vector<float> first;
        first.reserve(kCapturedBlocks);
        for (std::size_t i = 0; i < kCapturedBlocks; ++i) {
            src.processBlock(kBlock);
            first.push_back(src.getCurrentValue());
        }
        src.reset();
        std::vector<float> second;
        second.reserve(kCapturedBlocks);
        for (std::size_t i = 0; i < kCapturedBlocks; ++i) {
            src.processBlock(kBlock);
            second.push_back(src.getCurrentValue());
        }
        REQUIRE(first == second);
        REQUIRE(first == a);
    }

    SECTION("(a) non-aligned block sequence matches a pure process() render") {
        // Two multiples of 32 would NOT discriminate: per FR-003 the control
        // counter is instance state, so control steps land at identical ABSOLUTE
        // sample indices for any such partitioning and a stream-consuming
        // implementation would draw the same values in the same order and pass.
        // {37, 1, 64, 512} varies the control-step PHASE against block edges.
        constexpr std::array<std::size_t, 4> kSequence = {37, 1, 64, 512};
        const std::size_t kTotalSamples = static_cast<std::size_t>(60.0 * kSr48);

        PerlinNoiseSource blocked;
        configureSource(blocked, 0x51E7u, 1.0f, 4, 1.0f, kSr48);

        std::vector<float> blockedValues;
        std::vector<std::size_t> boundaries;
        std::size_t consumed = 0;
        for (std::size_t si = 0; consumed < kTotalSamples; ++si) {
            const std::size_t blockSize = kSequence[si % kSequence.size()];
            if (consumed + blockSize > kTotalSamples) {
                break;
            }
            blocked.processBlock(blockSize);
            consumed += blockSize;
            blockedValues.push_back(blocked.getCurrentValue());
            boundaries.push_back(consumed);
        }
        REQUIRE(boundaries.size() > 1000u);

        PerlinNoiseSource perSample;
        configureSource(perSample, 0x51E7u, 1.0f, 4, 1.0f, kSr48);

        std::size_t position = 0;
        double maxDiff = 0.0;
        for (std::size_t i = 0; i < boundaries.size(); ++i) {
            while (position < boundaries[i]) {
                perSample.process();
                ++position;
            }
            const double d = std::abs(static_cast<double>(perSample.getCurrentValue()) -
                                      static_cast<double>(blockedValues[i]));
            maxDiff = std::max(maxDiff, d);
        }
        REQUIRE(maxDiff <= 1e-6);
    }

    SECTION("(c) octave-stream identity across octave counts (FR-015)") {
        // Same seed, both advanced to the same control-step count. The OUTPUTS
        // are deliberately NOT compared: they legitimately differ by the
        // 1/Sum(a_k) normalisation (1.0 at n = 1 vs 1.875 at n = 4).
        PerlinNoiseSource p1;
        PerlinNoiseSource p4;
        configureSource(p1, 0xBEEFu, 0.25f, 2, 1.0f, kSr48);
        configureSource(p4, 0xBEEFu, 0.25f, 2, 1.0f, kSr48);

        for (std::size_t i = 0; i < 5000; ++i) {
            p1.processBlock(PerlinNoiseSource::kControlRateInterval);
            p4.processBlock(PerlinNoiseSource::kControlRateInterval);
        }
        REQUIRE(p1.getPosition() == p4.getPosition());

        p1.setOctaves(1);
        p4.setOctaves(4);
        REQUIRE(p1.getOctaves() == 1);
        REQUIRE(p4.getOctaves() == 4);

        // Bit-exact: octave seeds are derived for ALL kMaxOctaves streams
        // regardless of the configured octave count.
        REQUIRE(p1.getOctaveValue(0) == p4.getOctaveValue(0));

        // Non-vacuity: the top stream is queryable on the ONE-octave instance
        // too, and matches. A "derive only octaves_ streams" implementation
        // returns 0 (or garbage) from p1 here while still passing the line
        // above, so this is what pins FR-015's always-derive rule.
        REQUIRE(p1.getOctaveValue(3) == p4.getOctaveValue(3));
        REQUIRE(p4.getOctaveValue(3) != 0.0f);

        // Out-of-range index is a documented 0, not UB.
        REQUIRE(p1.getOctaveValue(static_cast<std::size_t>(PerlinNoiseSource::kMaxOctaves)) ==
                0.0f);

        // Still identical after a further advance in lock-step.
        p1.processBlock(PerlinNoiseSource::kControlRateInterval);
        p4.processBlock(PerlinNoiseSource::kControlRateInterval);
        REQUIRE(p1.getOctaveValue(0) == p4.getOctaveValue(0));
    }
}

// =============================================================================
// 6. SC-005 - sample-rate invariance, PER-SAMPLE, never accelerated
// =============================================================================
// rate = kDefaultRate (0.1 Hz), octaves in {1, 4}, depth = 1, same seed.
// 120 s wall clock at 44 100 and 96 000; the 44.1 k run is linearly resampled
// onto the 96 k grid.
//
// NOT run at kMaxRate: the same 0.73 ms control-grid offset (32/44100)
// contributes ~2e-2 there, 10x the tolerance, and the criterion would fail on a
// CORRECT implementation. A fast-corner check would have to compare at aligned
// control-step boundaries, not per audio sample.

TEST_CASE("PerlinNoiseSource_SampleRateInvariant", "[processors][perlin][vorago]") {
    constexpr double kSeconds = 120.0;
    constexpr std::uint32_t kSeed = 0x9E37u;

    for (const int octaves : {1, 4}) {
        const std::size_t n44 = static_cast<std::size_t>(kSeconds * kSr44);
        const std::size_t n96 = static_cast<std::size_t>(kSeconds * kSr96);

        // ---- 44.1 kHz run, captured in full -------------------------------
        std::vector<float> run44;
        run44.reserve(n44);
        double sumSq44 = 0.0;
        double sum44 = 0.0;
        {
            PerlinNoiseSource src;
            configureSource(src, kSeed, PerlinNoiseSource::kDefaultRate, octaves, 1.0f, kSr44);
            for (std::size_t i = 0; i < n44; ++i) {
                src.process();
                const float v = src.getCurrentValue();
                run44.push_back(v);
                sumSq44 += static_cast<double>(v) * static_cast<double>(v);
                sum44 += static_cast<double>(v);
            }
        }
        const double rms44 = std::sqrt(sumSq44 / static_cast<double>(n44));
        const double mean44 = sum44 / static_cast<double>(n44);

        // ---- 96 kHz run, streamed against the resampled 44.1 kHz reference --
        double maxAbsDiff = 0.0;
        double sumSq96 = 0.0;
        double sum96 = 0.0;
        {
            PerlinNoiseSource src;
            configureSource(src, kSeed, PerlinNoiseSource::kDefaultRate, octaves, 1.0f, kSr96);
            for (std::size_t j = 0; j < n96; ++j) {
                src.process();
                const float v = src.getCurrentValue();
                sumSq96 += static_cast<double>(v) * static_cast<double>(v);
                sum96 += static_cast<double>(v);

                const double pos = static_cast<double>(j) * (kSr44 / kSr96);
                const double base = std::floor(pos);
                const std::size_t i0 = static_cast<std::size_t>(base);
                if (i0 + 1 >= run44.size()) {
                    continue;
                }
                const double frac = pos - base;
                const double ref = static_cast<double>(run44[i0]) * (1.0 - frac) +
                                   static_cast<double>(run44[i0 + 1]) * frac;
                maxAbsDiff = std::max(maxAbsDiff, std::abs(static_cast<double>(v) - ref));
            }
        }
        const double rms96 = std::sqrt(sumSq96 / static_cast<double>(n96));
        const double mean96 = sum96 / static_cast<double>(n96);

        // Measured 1.705e-4 at n = 1, 2.972e-4 at n = 4 (bound = 1e-3 of the
        // 2.0-wide range span).
        REQUIRE(maxAbsDiff <= 2.0e-3);
        // Measured RMS deviation 0.000 % / 0.001 %.
        REQUIRE(rms96 > 0.0);
        REQUIRE(std::abs(rms44 / rms96 - 1.0) <= 0.02);
        // ABSOLUTE, never relative-to-mean: the source is ~zero-mean and a
        // relative comparison divides by ~0. Measured 1.208e-6 / 6.080e-6.
        REQUIRE(std::abs(mean44 - mean96) <= 2.0e-4);
    }
}

// =============================================================================
// 7. SC-013 (Perlin half) - zero allocations in the advance path
// =============================================================================

TEST_CASE("PerlinNoiseSource_NoAllocInProcess", "[processors][perlin][vorago]") {
    constexpr std::size_t kBlock512 = 512;
    constexpr std::size_t kOneSecondAt48k = 48000;

    PerlinNoiseSource src;
    configureSource(src, 0x9E37u, 1.0f, 4, 1.0f, kSr48);

    // Warm-up OUTSIDE the tracked scope: prepare() and any first-touch work must
    // not be attributed to the steady state under test.
    src.processBlock(kBlock512);
    src.process();

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    for (int i = 0; i < 500; ++i) {
        src.processBlock(kBlock512);
    }
    for (int i = 0; i < 4096; ++i) {
        src.process();
    }
    for (int i = 0; i < 40; ++i) {
        src.processBlock(kOneSecondAt48k);
    }
    const std::size_t allocations = detector.stopTracking();

    REQUIRE(allocations == 0u);
    REQUIRE(detail::isFinite(src.getCurrentValue()));
}

// =============================================================================
// 8. FR-002 / FR-019 / Edge Cases
// =============================================================================

TEST_CASE("PerlinNoiseSource_EdgeCases", "[processors][perlin][vorago]") {
    SECTION("processBlock(0) is a no-op") {
        PerlinNoiseSource src;
        configureSource(src, 0x51E7u, 1.0f, 3, 1.0f, kSr48);
        src.processBlock(1234);

        const double posBefore = src.getPosition();
        const float valBefore = src.getCurrentValue();
        src.processBlock(0);
        REQUIRE(src.getPosition() == posBefore);
        REQUIRE(src.getCurrentValue() == valBefore);
    }

    SECTION("processBlock(10'000'000) resolves in one call") {
        PerlinNoiseSource src;
        configureSource(src, 0x51E7u, 1.0f, 4, 1.0f, kSr48);
        src.processBlock(10'000'000);
        const float v = src.getCurrentValue();
        REQUIRE(detail::isFinite(v));
        REQUIRE(std::abs(v) <= 1.0f);
    }

    SECTION("advance before prepare() does not crash and stays finite") {
        PerlinNoiseSource src;  // NO prepare()
        src.process();
        src.processBlock(512);
        src.processBlock(PerlinNoiseSource::kControlRateInterval);
        const float v = src.getCurrentValue();
        REQUIRE(detail::isFinite(v));
        REQUIRE(std::abs(v) <= 1.0f);
    }

    SECTION("prepare() twice leaves no half-completed state") {
        PerlinNoiseSource twice;
        twice.setSeed(0xBEEFu);
        twice.setRate(0.75f);
        twice.setOctaves(3);
        twice.prepare(kSr48);
        twice.processBlock(9999);  // dirty the state
        twice.prepare(kSr48);      // full re-initialisation (FR-002)

        PerlinNoiseSource fresh;
        configureSource(fresh, 0xBEEFu, 0.75f, 3, 1.0f, kSr48);

        REQUIRE(detail::isFinite(twice.getCurrentValue()));
        REQUIRE(twice.getPosition() == fresh.getPosition());
        REQUIRE(twice.getCurrentValue() == fresh.getCurrentValue());

        for (std::size_t i = 0; i < 2000; ++i) {
            twice.processBlock(PerlinNoiseSource::kControlRateInterval);
            fresh.processBlock(PerlinNoiseSource::kControlRateInterval);
        }
        REQUIRE(twice.getCurrentValue() == fresh.getCurrentValue());
    }

    SECTION("setRate clamps, NaN maps to kMinRate") {
        const float infF = floatFromBits(0x7F800000u);
        const float negInfF = floatFromBits(0xFF800000u);
        const float nanF = floatFromBits(0x7FC00000u);

        PerlinNoiseSource src;
        src.prepare(kSr48);

        src.setRate(0.0f);
        REQUIRE(src.getRate() == PerlinNoiseSource::kMinRate);
        src.setRate(1e9f);
        REQUIRE(src.getRate() == PerlinNoiseSource::kMaxRate);
        src.setRate(-1.0f);
        REQUIRE(src.getRate() == PerlinNoiseSource::kMinRate);
        src.setRate(infF);
        REQUIRE(src.getRate() == PerlinNoiseSource::kMaxRate);
        src.setRate(negInfF);
        REQUIRE(src.getRate() == PerlinNoiseSource::kMinRate);
        // std::clamp PROPAGATES NaN - the component must route float setters
        // through a NaN-aware sanitising clamp that maps NaN to the low bound.
        src.setRate(nanF);
        REQUIRE(src.getRate() == PerlinNoiseSource::kMinRate);

        // A NaN rate must not have poisoned the trajectory either.
        src.setRate(1.0f);
        src.processBlock(48000);
        REQUIRE(detail::isFinite(src.getCurrentValue()));

        // setDepth is a float setter too - same NaN rule, low bound = 0.
        src.setDepth(nanF);
        REQUIRE(src.getDepth() == 0.0f);
        src.setDepth(infF);
        REQUIRE(src.getDepth() == 1.0f);
        src.setDepth(negInfF);
        REQUIRE(src.getDepth() == 0.0f);
    }

    SECTION("setOctaves clamps to [1, 4]") {
        PerlinNoiseSource src;
        src.prepare(kSr48);
        src.setOctaves(0);
        REQUIRE(src.getOctaves() == PerlinNoiseSource::kMinOctaves);
        src.setOctaves(-7);
        REQUIRE(src.getOctaves() == PerlinNoiseSource::kMinOctaves);
        src.setOctaves(99);
        REQUIRE(src.getOctaves() == PerlinNoiseSource::kMaxOctaves);
    }

    SECTION("setDepth(0) is exactly 0 for all time") {
        PerlinNoiseSource src;
        configureSource(src, 0x51E7u, PerlinNoiseSource::kMaxRate, 4, 0.0f, kSr48);
        bool anyNonZero = false;
        for (std::size_t i = 0; i < 200000; ++i) {
            src.processBlock(PerlinNoiseSource::kControlRateInterval);
            if (src.getCurrentValue() != 0.0f) {
                anyNonZero = true;
            }
        }
        REQUIRE_FALSE(anyNonZero);
    }

    SECTION("prepare(0) / prepare(-1) hit the 1 Hz floor and stay finite") {
        for (const double sr : {0.0, -1.0}) {
            PerlinNoiseSource src;
            configureSource(src, 0x9E37u, 1.0f, 4, 1.0f, sr);
            src.processBlock(4096);
            src.process();
            const float v = src.getCurrentValue();
            REQUIRE(detail::isFinite(v));
            REQUIRE(std::abs(v) <= 1.0f);
        }
    }

    SECTION("setSeed(0) aliases the Xorshift32 default seed") {
        // random.h:73-74 substitutes kDefaultSeed (random.h:85 = 2463534242u)
        // for a zero seed. The alias is DOCUMENTED behaviour, not an error.
        const auto render = [](std::uint32_t seed) {
            PerlinNoiseSource src;
            configureSource(src, seed, 1.0f, 4, 1.0f, kSr48);
            std::vector<float> out;
            out.reserve(400);
            for (std::size_t i = 0; i < 400; ++i) {
                src.processBlock(512);
                out.push_back(src.getCurrentValue());
            }
            return out;
        };
        REQUIRE(render(0u) == render(2463534242u));
    }

    SECTION("adjacent seeds are uncorrelated") {
        // 300 s at kMaxRate = 1500 lattice cells, so the sample cross-correlation
        // of two independent trajectories has standard error ~0.026 and the 0.2
        // bound is ~8 sigma away. (At kDefaultRate the same window covers only
        // 30 cells, standard error ~0.18, and this clause would be a coin flip.)
        constexpr std::size_t kPoints = static_cast<std::size_t>(
            300.0 * 48000.0 / 32.0);

        std::vector<double> previous;
        for (std::uint32_t seed = 1u; seed <= 8u; ++seed) {
            std::vector<double> current = renderControl(
                seed, PerlinNoiseSource::kMaxRate, 2, kPoints, std::size_t{1}, kSr48);
            if (!previous.empty()) {
                REQUIRE(std::abs(crossCorrZeroLag(previous, current)) < 0.2);
            }
            previous = std::move(current);
        }
    }
}
