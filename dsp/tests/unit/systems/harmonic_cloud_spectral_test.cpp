// ==============================================================================
// Layer 3: System Tests - HarmonicCloud (FFT-heavy spectral criteria)
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per specs/seraphis-phase2-harmonic-cloud/
//
// Reference: specs/seraphis-phase2-harmonic-cloud/spec.md   (SC-003, SC-010,
//            SC-011, SC-014)
//            specs/seraphis-phase2-harmonic-cloud/plan.md   (§3, §7.1, §7.3, §7.4)
//
// This TU is kept separate from harmonic_cloud_test.cpp because every case here
// builds 65536-point transforms and dominates wall time (plan §7.1).
//
// Populated by T004 (SC-014), T005 (SC-003), T007 (SC-011) and T018 (SC-010).
// ==============================================================================

#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>
#include <spectral_analysis.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;
using namespace Krate::DSP;

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// =============================================================================
// SC-014 pinned configuration (spec.md SC-014, plan §7.4)
// =============================================================================

constexpr double kSampleRate = 48000.0;
constexpr float kSampleRateF = 48000.0f;
constexpr float kF0Hz = 110.0f;

/// 65536-point analysis: 0.7324 Hz per bin, so the 110 Hz partial spacing is
/// 150.2 bins and the main lobes never overlap. `FFT::prepare` validates only
/// power-of-two (`fft.h:151`); `kMaxFFTSize = 8192` (`fft.h:47`) is documentary
/// and pffft handles 2^16 — hence the mandatory `isPrepared()` assertion below
/// (plan §7.2), so a future tightening of that bound fails loudly instead of
/// silently analysing a zero-size spectrum.
constexpr std::size_t kFftSize = 65536;

/// 4-term Blackman-Harris main-lobe half-width in bins. Summing POWER over the
/// whole main lobe removes scalloping loss (the peak-bin magnitude of a
/// Blackman-Harris-windowed tone varies by up to ~0.8 dB with the tone's
/// fractional bin offset, which alone would consume the 0.5 dB budget), and the
/// -92 dB sidelobes put inter-partial leakage ~150 bins away far below the
/// measurement floor.
constexpr std::size_t kLobeHalfWidth = 4;

/// 4.0 s render (SC-014 requires >= 4 s); the last kFftSize samples are analysed
/// so every smoother (FR-014 at 2 ms, FR-017's normalizer at 20 ms) has settled.
constexpr std::size_t kRenderSamples = 192000;

constexpr std::size_t kRenderBlock = 512;

/// Pinned seed. FR-016 draws each partial's initial MCF phase from the seeded
/// stream, and the orbit gain below is phase-dependent, so an unpinned seed makes
/// the measured per-partial levels (and the onset peak) a coin flip. Modelled
/// worst channel peak at this seed across the five Richness settings: 0.50, 0.56,
/// 0.60, 0.67, 0.92 — all far below `kOutputClamp = 2.0`.
constexpr std::uint32_t kSc014Seed = 0x5E3A0014u;

constexpr float kFloorDb = -200.0f;

/// Audibility floor: -60 dB relative to the strongest partial (FR-042, SC-014).
constexpr float kAudibilityFloorDb = -60.0f;

/// Above-fundamental energy fraction floor, so the r = 0 value stays finite
/// (SC-014 metric (b)).
constexpr float kEnergyFractionFloorDb = -80.0f;

[[nodiscard]] float powerToDb(float power) noexcept {
    constexpr float kTiny = 1.0e-30f;
    return (power > kTiny) ? 10.0f * std::log10(power) : kFloorDb;
}

// =============================================================================
// Per-setting measurement
// =============================================================================

struct RichnessMeasurement {
    std::size_t activeCount = 0;
    std::array<float, HarmonicCloud::kMaxPartials> powerLin{};   ///< main-lobe power sum
    std::array<float, HarmonicCloud::kMaxPartials> levelDb{};    ///< 10*log10(powerLin)
    std::array<float, HarmonicCloud::kMaxPartials> renderGainDb{};  ///< aa x MCF orbit gain, dB
    std::size_t countAboveFloor = 0;
    float energyFractionDb = kEnergyFractionFloorDb;
    float peak = 0.0f;
};

/// @brief Render one Richness setting and measure its spectrum.
///
/// @par Why the measured level needs the `renderGainDb` correction
/// The kernel's steady state is `currentAmplitude -> targetAmplitude *
/// antiAliasGain` (`harmonic_oscillator_bank_simd.cpp:91-93`) and the rendered
/// sample is `sinState * currentAmplitude` (`:96`). `sinState` is NOT a unit
/// sinusoid: for the Modified-Coupled-Form recurrence `s' = s + eps*c`,
/// `c' = c - eps*s'` with `eps = 2*sin(w/2)`, the closed-form solution is
/// `s[n] = A*sin(w*n + theta)`, `c[n] = A*cos(w*n + theta + w/2)`, so with the
/// FR-016 initialisation `s = sin(phi)`, `c = cos(phi)`:
/// @code
///   A^2 * cos^2(w/2) = 1 + sin(w/2) * sin(2*phi)
/// @endcode
/// i.e. the orbit amplitude carries a phase-dependent term on top of the
/// `1/cos(pi*f/fs)` factor the anti-alias gain pre-compensates for
/// (`harmonic_oscillator_bank.h:1077-1079`). At `f0 = 110 Hz` / 48 kHz that term
/// spans +1.60 / -2.55 dB at partial 64 — five times SC-014's 0.5 dB budget, so
/// an uncorrected comparison fails a CORRECT implementation. Modelled worst
/// per-partial error at r = 1: 2.33 dB uncorrected, 0.000 dB corrected.
///
/// `A` is an invariant of the orbit, so it can be recovered from the state at any
/// time — including after the render — via `A^2 = s^2 + (c + k*s)^2 / q^2` with
/// `k = sin(pi*f/fs)`, `q = cos(pi*f/fs)`. The correction therefore uses only
/// shipped FR-008 accessors and re-implements no part of the amplitude law.
[[nodiscard]] RichnessMeasurement measureRichness(float richness) {
    HarmonicCloud cloud;
    cloud.prepare(kSampleRate);
    cloud.setSeed(kSc014Seed);
    cloud.setFundamentalHz(kF0Hz);
    cloud.setRichness(richness);
    cloud.setSpectralTiltDb(0.0f);      // isolate the FR-041 rolloff from FR-061
    cloud.setInharmonicity(0.0f);       // exactly harmonic partial ratios
    cloud.setSpectralGravity(0.0f);
    cloud.setMutation(0.0f);            // w_i == 1
    cloud.setDriftDepthCents(0.0f);     // detune == 1, so the bins do not move
    cloud.setStereoSpread(0.0f);        // both channels identical
    cloud.noteOn();

    std::vector<float> mono(kRenderSamples, 0.0f);
    std::array<float, kRenderBlock> left{};
    std::array<float, kRenderBlock> right{};
    for (std::size_t done = 0; done < kRenderSamples; done += kRenderBlock) {
        const std::size_t n = std::min(kRenderBlock, kRenderSamples - done);
        cloud.processStereoBlock(left.data(), right.data(), n);
        for (std::size_t s = 0; s < n; ++s) {
            mono[done + s] = 0.5f * (left[s] + right[s]);
        }
    }

    RichnessMeasurement result;
    result.activeCount = cloud.getActivePartialCount();

    const std::size_t analysisStart = kRenderSamples - kFftSize;
    std::vector<float> frame(kFftSize, 0.0f);
    std::vector<float> window(kFftSize, 0.0f);
    Window::generateBlackmanHarris(window.data(), kFftSize);
    for (std::size_t i = 0; i < kFftSize; ++i) {
        const float sample = mono[analysisStart + i];
        result.peak = std::max(result.peak, std::abs(sample));
        frame[i] = sample * window[i];
    }

    FFT fft;
    fft.prepare(kFftSize);
    REQUIRE(fft.isPrepared());  // plan §7.2: fail loudly, never analyse a zero-size spectrum

    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(frame.data(), spectrum.data());

    const std::size_t lastBin = fft.numBins() - 1;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        const float fHz = cloud.getPartialFrequencyHz(i);

        // Precondition for the whole bin-mapping strategy: with inharmonicity and
        // gravity at zero the partials must be exactly harmonic, so each main lobe
        // sits where frequencyToBin() puts it and the +-4 bin windows never overlap
        // (110 Hz is 150.2 bins apart at this FFT size).
        REQUIRE(fHz == Approx(kF0Hz * static_cast<float>(i + 1)).margin(0.05));

        const std::size_t center = TestUtils::frequencyToBin(fHz, kSampleRateF, kFftSize);
        const std::size_t lo = (center > kLobeHalfWidth) ? (center - kLobeHalfWidth) : 0;
        const std::size_t hi = std::min(center + kLobeHalfWidth, lastBin);

        float power = 0.0f;
        for (std::size_t b = lo; b <= hi; ++b) {
            const float mag = spectrum[b].magnitude();
            power += mag * mag;
        }
        result.powerLin[i] = power;
        result.levelDb[i] = powerToDb(power);

        const float halfAngle = kPi * fHz / kSampleRateF;
        const float k = std::sin(halfAngle);
        const float q = std::cos(halfAngle);
        const float s = cloud.getPartialSinState(i);
        const float c = cloud.getPartialCosState(i);
        const float cross = c + k * s;
        const float orbitAmplitude = std::sqrt(s * s + (cross * cross) / (q * q));
        const float renderGain = cloud.getPartialAntiAliasGain(i) * orbitAmplitude;
        result.renderGainDb[i] = (renderGain > 0.0f) ? 20.0f * std::log10(renderGain) : kFloorDb;
    }

    float strongestDb = kFloorDb;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        strongestDb = std::max(strongestDb, result.levelDb[i]);
    }
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        if (result.levelDb[i] >= strongestDb + kAudibilityFloorDb) {
            ++result.countAboveFloor;
        }
    }

    float totalPower = 0.0f;
    float abovePower = 0.0f;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        totalPower += result.powerLin[i];
        if (i > 0) {
            abovePower += result.powerLin[i];
        }
    }
    const float fraction = (totalPower > 0.0f) ? (abovePower / totalPower) : 0.0f;
    result.energyFractionDb =
        (fraction > 0.0f) ? std::max(kEnergyFractionFloorDb, 10.0f * std::log10(fraction))
                          : kEnergyFractionFloorDb;

    return result;
}

// =============================================================================
// SC-003 pinned configuration (spec.md SC-003, plan §7.3, §7.4)
// =============================================================================

/// Richness `r = log64(32) = 5/6`, which FR-041(a) maps to
/// `N = round(64^(5/6)) = 32` and FR-041(b) to rolloff exponent
/// `p = 3.0 - 2.5*(5/6) = 0.91667`.
constexpr float kSc003Richness = 5.0f / 6.0f;
constexpr std::size_t kSc003Partials = 32;

/// Pinned seed (plan §7.3 / risk R14). FR-016 draws each partial's initial MCF
/// phase from the seeded stream and the partials are exactly harmonic here
/// (`B = 0`, `g = 0`), so the render is periodic at 110 Hz and its PEAK is a
/// random variable in the seed while its RMS is pinned at `kTargetOscRms`.
///
/// @par Measured this session on the real component (T005 step 1)
/// Windows 11, MSVC Release, 2026-07-25. 32 seeds x 5 tilts x spread {0, 1},
/// 4 s renders at 48 kHz, worst peak per rendered channel over the same 65536
/// sample analysis window the FFT consumes:
/// @code
///   worst over ALL 32 seeds:  t-12 0.5781 (9.87 dB)   t-6  0.6857 (8.38 dB)
///                             t 0  1.0307 (4.84 dB)   t+6  1.2352 (3.27 dB)
///                             t+12 1.3515 (2.49 dB)
///   this seed (0x5E3A3303):   0.5390  0.5865  0.7814  0.9085  0.8494
///                             -> worst 0.9085, margin to 1.8 = 5.94 dB
/// @endcode
/// It was chosen as the lowest worst-over-tilts peak of that 32-seed sweep. The
/// criterion does not *depend* on that choice: the worst seed of the whole sweep
/// still clears the bound by 2.49 dB, so a future change to the seed -> phase
/// mapping cannot red this precondition on a correct implementation. The plan's
/// modelled table was mildly pessimistic — the measured worst-over-32-seeds peak
/// beats the model by 0.2 to 1.1 dB at every tilt (at +12 dB/oct: 1.3515 measured
/// against 1.436 modelled).
///
/// `setStereoSpread(0.0f)` is pinned for the same reason (plan §7.3): spread
/// pushes partials off-centre onto one channel and costs further margin.
/// (At the time of measurement FR-021's `positionScatter_` is still a
/// placeholder of zeros, so the spread-1 column measured identically to
/// spread 0; pinning spread 0 makes the test immune to that landing.)
constexpr std::uint32_t kSc003Seed = 0x5E3A3303u;

/// The FR-041 rolloff expressed as a slope: `-20 * p * log10(2)` at
/// `p = 0.91667`. Asserting it is what pins the Richness rolloff law itself, so
/// the differential evaluation below is an isolation of FR-061, not a relaxation.
constexpr float kSc003RolloffDbPerOct = -5.52f;

/// SC-003's single tolerance: slope in dB/octave and per-partial gain in dB.
constexpr float kSc003ToleranceDb = 0.5f;

struct TiltMeasurement {
    std::array<float, kSc003Partials> levelDb{};  ///< main-lobe power sum, dB
    float slopeDbPerOct = 0.0f;                   ///< least-squares fit vs log2(n)
    float peak = 0.0f;                            ///< worst channel peak in the analysed window
};

/// @brief Least-squares slope of per-partial level (dB) against log2(partial index).
[[nodiscard]] float fitSlopeDbPerOctave(const std::array<float, kSc003Partials>& levelDb) noexcept {
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    for (std::size_t i = 0; i < kSc003Partials; ++i) {
        const double x = std::log2(static_cast<double>(i + 1));
        const double y = static_cast<double>(levelDb[i]);
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
    }
    const double n = static_cast<double>(kSc003Partials);
    return static_cast<float>((n * sumXY - sumX * sumY) / (n * sumXX - sumX * sumX));
}

/// @brief Render one tilt setting at SC-003's pinned configuration and measure it.
///
/// @note No MCF-orbit correction is applied here, unlike measureRichness(). SC-003
///       is evaluated DIFFERENTIALLY against the tilt-0 render of the identical
///       configuration, and tilt changes neither a partial's frequency nor its
///       initial phase — so the phase-dependent orbit amplitude `A_n` (worth up to
///       +1.13 / -0.89 dB at partial 32 here) is bit-identical in both renders and
///       cancels exactly in `dB(t,n) - dB(0,n)`. It also cancels in
///       `slope(t) - slope(0)`. It survives only in the tilt-0 slope itself, where
///       it is a near-zero-mean scatter uncorrelated with log2(n): measured tilt-0
///       slope -5.5396 dB/oct against the -5.52 target, error 0.0196 dB/oct.
[[nodiscard]] TiltMeasurement measureTilt(float tiltDb) {
    HarmonicCloud cloud;
    cloud.prepare(kSampleRate);
    cloud.setSeed(kSc003Seed);          // pinned: see kSc003Seed
    cloud.setFundamentalHz(kF0Hz);      // 110 Hz
    cloud.setRichness(kSc003Richness);  // -> 32 active partials
    cloud.setSpectralTiltDb(tiltDb);
    cloud.setInharmonicity(0.0f);    // exactly harmonic ratios -> fixed main-lobe bins
    cloud.setSpectralGravity(0.0f);  // ditto
    cloud.setMutation(0.0f);         // w_i == 1
    cloud.setDriftDepthCents(0.0f);  // detune == 1, so the bins do not move
    cloud.setStereoSpread(0.0f);     // pinned: see kSc003Seed
    cloud.noteOn();

    // Partial 32 sits at 3.52 kHz, far below the FR-015 fade band (0.8 * Nyquist
    // = 19.2 kHz at 48 kHz). A suppressed partial would bend the fit, so the
    // constraint is asserted rather than assumed.
    REQUIRE(cloud.getActivePartialCount() == kSc003Partials);
    REQUIRE(kF0Hz * static_cast<float>(kSc003Partials)
            < HarmonicCloud::kAntiAliasFadeStart * 0.5f * kSampleRateF);

    std::vector<float> left(kRenderSamples, 0.0f);
    std::vector<float> right(kRenderSamples, 0.0f);
    std::array<float, kRenderBlock> leftBlock{};
    std::array<float, kRenderBlock> rightBlock{};
    for (std::size_t done = 0; done < kRenderSamples; done += kRenderBlock) {
        const std::size_t n = std::min(kRenderBlock, kRenderSamples - done);
        cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), n);
        for (std::size_t s = 0; s < n; ++s) {
            left[done + s] = leftBlock[s];
            right[done + s] = rightBlock[s];
        }
    }

    TiltMeasurement result;

    // The LEFT channel is what the FFT consumes, so the precondition peak is taken
    // over exactly those samples — plus the right channel, which spread 0 makes
    // identical but which a pan regression would not.
    const std::size_t analysisStart = kRenderSamples - kFftSize;
    std::vector<float> frame(kFftSize, 0.0f);
    std::vector<float> window(kFftSize, 0.0f);
    Window::generateBlackmanHarris(window.data(), kFftSize);
    for (std::size_t i = 0; i < kFftSize; ++i) {
        const float sample = left[analysisStart + i];
        result.peak = std::max({result.peak, std::abs(sample),
                                std::abs(right[analysisStart + i])});
        frame[i] = sample * window[i];
    }

    FFT fft;
    fft.prepare(kFftSize);
    REQUIRE(fft.isPrepared());  // plan §7.2: never analyse a zero-size spectrum

    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(frame.data(), spectrum.data());

    const std::size_t lastBin = fft.numBins() - 1;
    for (std::size_t i = 0; i < kSc003Partials; ++i) {
        const float fHz = cloud.getPartialFrequencyHz(i);

        // Same bin-mapping precondition as SC-014: exactly harmonic partials, so
        // each main lobe sits where frequencyToBin() puts it and the +-4 bin
        // windows never overlap (110 Hz is 150.2 bins apart at this FFT size).
        REQUIRE(fHz == Approx(kF0Hz * static_cast<float>(i + 1)).margin(0.05));

        const std::size_t center = TestUtils::frequencyToBin(fHz, kSampleRateF, kFftSize);
        const std::size_t lo = (center > kLobeHalfWidth) ? (center - kLobeHalfWidth) : 0;
        const std::size_t hi = std::min(center + kLobeHalfWidth, lastBin);

        float power = 0.0f;
        for (std::size_t b = lo; b <= hi; ++b) {
            const float mag = spectrum[b].magnitude();
            power += mag * mag;
        }
        result.levelDb[i] = powerToDb(power);
    }

    result.slopeDbPerOct = fitSlopeDbPerOctave(result.levelDb);
    return result;
}

// =============================================================================
// SC-011 pinned configuration (spec.md SC-011, plan §7 row SC-011, risk R5)
// =============================================================================

/// 44.1 kHz is pinned by the criterion: it is the lowest supported rate, so it
/// puts the most partials above Nyquist at the FR-013 maximum fundamental.
constexpr double kSc011SampleRate = 44100.0;
constexpr float kSc011SampleRateF = 44100.0f;
constexpr float kSc011NyquistHz = 0.5f * kSc011SampleRateF;

/// FR-013's maximum fundamental — the worst case for aliasing.
constexpr float kSc011F0Hz = HarmonicCloud::kMaxFundamentalHz;  // 4000 Hz

/// 65536-point analysis at 44.1 kHz: 0.6729 Hz per bin.
constexpr std::size_t kSc011FftSize = 65536;

/// 4.0 s render; the last kSc011FftSize samples (1.49 s) are analysed, leaving
/// 2.5 s for FR-014's 2 ms smoother and FR-017's 20 ms normalizer to settle.
constexpr std::size_t kSc011RenderSamples = 176400;

/// Half-width of the exclusion window around every legitimately synthesized
/// sub-Nyquist partial, in bins (SC-011 step 3).
constexpr std::size_t kSc011ExclusionRadius = 2;

/// SC-011's threshold. NOT tightened: tightening is only permitted on a measured
/// render, and this session did not run one.
constexpr float kSc011ThresholdDb = -60.0f;

/// Pinned seed. FR-016 draws each partial's initial MCF phase from the seeded
/// stream and the MCF orbit amplitude is phase-dependent (see measureRichness()),
/// so the fundamental's measured bin level moves by a dB or two with the seed.
/// That is irrelevant against a 60 dB budget, but the measurement should still be
/// a fixed number rather than a draw.
constexpr std::uint32_t kSc011Seed = 0x5E3A0011u;

struct AliasMeasurement {
    float fundamentalDb = kFloorDb;    ///< power in the fundamental's bin, dB
    float aliasDb = kFloorDb;          ///< power in the alias-only bins, dB
    std::size_t subNyquistCount = 0;   ///< partials with f_i <= Nyquist
    std::size_t aliasBinCount = 0;     ///< distinct alias bins left after exclusion
    float peak = 0.0f;                 ///< worst channel peak over the analysed window
};

/// @brief Render the SC-011 worst case at one gravity setting and measure the
///        energy in bins reachable ONLY by an aliased partial.
///
/// @par Why the bins come from the component's own frequencies
/// `TestUtils::getAliasedBins` / `AliasingTestConfig` (`spectral_analysis.h:168-183`,
/// `:112-118`) are deliberately NOT used. They enumerate fold-back bins for the
/// INTEGER harmonics of one fundamental (`freq = testFrequencyHz * n`, default
/// `maxHarmonic = 10`, plus a waveshaper-flavoured `driveGain`). Here FR-051's
/// `sqrt(1 + B*n^2)` and FR-081's `n^(1 +- 0.1)` warp make every partial a
/// non-integer multiple of `f0`, so that helper would enumerate bins this
/// component's partials never fold into — and would pass a genuinely aliasing
/// build. Every frequency below is read back from FR-008's
/// `getPartialFrequencyHz`, which is the law's own output.
///
/// @par Why drift depth is pinned to zero
/// `getPartialFrequencyHz` is documented as the UNDETUNED frequency
/// (`harmonic_cloud.h:628-631`). The whole bin mapping — and the +-2 bin exclusion
/// window with it — is only valid while `detuneMultiplier == 1`; 50 cents at
/// 20 kHz is ~880 bins at this FFT size, which would put the fold-back images
/// nowhere near the bins computed here. Depth 0 is therefore a precondition of the
/// measurement, not a softening of the criterion.
///
/// @par Frequencies this configuration produces
/// Derived by evaluating FR-083's law in double precision (the same expression
/// `recalculateFrequencies()` computes, `harmonic_cloud.h:880-899`) — a derivation,
/// NOT a rendered measurement:
/// @code
///   g = +1: partials 1..3 are sub-Nyquist (4195.2, 10145.1, 18461.6 Hz;
///           partial 3 sits in the FR-015 fade band -> gain 0.814),
///           partials 4..64 exceed Nyquist. Nearest fold-back image to a
///           sub-Nyquist partial's bin: 122 bins.
///   g = -1: partials 1..3 are sub-Nyquist (4195.2, 8831.8, 14819.9 Hz, all at
///           full gain), partials 4..64 exceed Nyquist. Nearest image: 88 bins.
/// @endcode
/// Both are far outside the 4-term Blackman-Harris main lobe (+-4 bins), so the
/// +-2 bin exclusion is not load-bearing at this configuration — it is kept
/// because SC-011 specifies it and because it must stay correct if the law moves.
[[nodiscard]] AliasMeasurement measureAliasFloor(float gravity) {
    HarmonicCloud cloud;
    cloud.prepare(kSc011SampleRate);
    cloud.setSeed(kSc011Seed);
    cloud.setFundamentalHz(kSc011F0Hz);                              // FR-013 maximum
    cloud.setRichness(1.0f);                                         // N(1) = 64 (FR-012)
    cloud.setInharmonicity(HarmonicCloud::kMaxInharmonicity);        // B = 0.1 (FR-052)
    cloud.setSpectralGravity(gravity);                               // |g| = 1 (FR-081)
    cloud.setSpectralTiltDb(0.0f);   // isolate FR-015 from FR-061
    cloud.setMutation(0.0f);         // w_i == 1
    cloud.setDriftDepthCents(0.0f);  // precondition of the bin mapping, see above
    cloud.setStereoSpread(0.0f);     // both channels identical
    cloud.noteOn();

    // noteOn() flushes the deferred FR-083 recompute while quiescent, so the
    // frequency law is already the one this render will use.
    REQUIRE(cloud.getActivePartialCount() == HarmonicCloud::kMaxPartials);

    std::vector<float> left(kSc011RenderSamples, 0.0f);
    std::vector<float> right(kSc011RenderSamples, 0.0f);
    std::array<float, kRenderBlock> leftBlock{};
    std::array<float, kRenderBlock> rightBlock{};
    for (std::size_t done = 0; done < kSc011RenderSamples; done += kRenderBlock) {
        const std::size_t n = std::min(kRenderBlock, kSc011RenderSamples - done);
        cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), n);
        for (std::size_t s = 0; s < n; ++s) {
            left[done + s] = leftBlock[s];
            right[done + s] = rightBlock[s];
        }
    }

    AliasMeasurement result;

    const std::size_t analysisStart = kSc011RenderSamples - kSc011FftSize;
    std::vector<float> frame(kSc011FftSize, 0.0f);
    std::vector<float> window(kSc011FftSize, 0.0f);
    Window::generateBlackmanHarris(window.data(), kSc011FftSize);
    for (std::size_t i = 0; i < kSc011FftSize; ++i) {
        const float sample = left[analysisStart + i];
        result.peak = std::max({result.peak, std::abs(sample),
                                std::abs(right[analysisStart + i])});
        frame[i] = sample * window[i];
    }

    FFT fft;
    fft.prepare(kSc011FftSize);
    REQUIRE(fft.isPrepared());  // plan §7.2: never analyse a zero-size spectrum

    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(frame.data(), spectrum.data());

    const std::size_t lastBin = fft.numBins() - 1;

    // Step 1/2: classify every partial by its OWN synthesized frequency, and fold
    // the ones above Nyquist with harmonic number 1 — the helper multiplies the
    // harmonic number in itself (`spectral_analysis.h:58-78`), so 1 folds the
    // actual frequency instead of `f0 * n`.
    std::vector<std::size_t> subNyquistBins;
    std::vector<std::size_t> aliasBins;
    subNyquistBins.reserve(HarmonicCloud::kMaxPartials);
    aliasBins.reserve(HarmonicCloud::kMaxPartials);

    REQUIRE(cloud.getPartialFrequencyHz(0) <= kSc011NyquistHz);  // the fundamental must be audible
    const std::size_t fundamentalBin =
        std::min(TestUtils::frequencyToBin(cloud.getPartialFrequencyHz(0), kSc011SampleRateF,
                                           kSc011FftSize),
                 lastBin);

    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        const float fHz = cloud.getPartialFrequencyHz(i);
        if (fHz > kSc011NyquistHz) {
            const float folded =
                TestUtils::calculateAliasedFrequency(fHz, 1, kSc011SampleRateF);
            aliasBins.push_back(std::min(
                TestUtils::frequencyToBin(folded, kSc011SampleRateF, kSc011FftSize), lastBin));
        } else {
            subNyquistBins.push_back(std::min(
                TestUtils::frequencyToBin(fHz, kSc011SampleRateF, kSc011FftSize), lastBin));
        }
    }
    result.subNyquistCount = subNyquistBins.size();

    // Step 3: distinct bins only (two partials may fold onto one bin, and
    // sumBinPower would otherwise count that bin twice), then drop every bin
    // within +-2 of a legitimately synthesized sub-Nyquist partial.
    std::sort(aliasBins.begin(), aliasBins.end());
    aliasBins.erase(std::unique(aliasBins.begin(), aliasBins.end()), aliasBins.end());

    std::vector<std::size_t> aliasOnlyBins;
    aliasOnlyBins.reserve(aliasBins.size());
    for (const std::size_t bin : aliasBins) {
        bool nearLegitimate = false;
        for (const std::size_t legit : subNyquistBins) {
            const std::size_t distance = (bin > legit) ? (bin - legit) : (legit - bin);
            if (distance <= kSc011ExclusionRadius) {
                nearLegitimate = true;
                break;
            }
        }
        if (!nearLegitimate) {
            aliasOnlyBins.push_back(bin);
        }
    }
    result.aliasBinCount = aliasOnlyBins.size();

    // `TestUtils::` is mandatory on every one of these, not stylistic: this TU also
    // pulls `Krate::DSP::detail` in through `harmonic_cloud.h` -> `core/db_utils.h`,
    // so the bare name `detail` is ambiguous and clang rejects it (plan risk R15).
    std::vector<std::size_t> fundamentalBins;
    fundamentalBins.push_back(fundamentalBin);
    result.fundamentalDb =
        TestUtils::detail::toDb(TestUtils::detail::sumBinPower(spectrum.data(), fundamentalBins));
    result.aliasDb =
        TestUtils::detail::toDb(TestUtils::detail::sumBinPower(spectrum.data(), aliasOnlyBins));

    return result;
}

// =============================================================================
// SC-010 — the two-stage heterodyne frequency estimator (plan §7.2)
// =============================================================================
// Reproduced here rather than shared, because the T002 definition lives in the
// anonymous namespace of harmonic_cloud_test.cpp and this TU is deliberately a
// separate binary-level translation unit (plan §7.1). Same algorithm, same
// constants: stage 1 M = 4096 (brackets +-5.86 Hz at 48 kHz), stage 2 M = N/2
// around the stage-1 result; Hann window; DOUBLE accumulators throughout.
// Documented resolution < 0.001 cent (`HarmonicCloud_FrequencyEstimatorResolution`
// in harmonic_cloud_test.cpp asserts it), i.e. 100x finer than the 0.1 cent
// SC-001 tolerance this criterion borrows.

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

/// One heterodyne phase-slope stage.
///
/// Two Hann-windowed correlations of length M, separated by exactly M samples.
/// The window transforms contribute the same real factor to both and cancel in
/// arg(S2 * conj(S1)), so dphi = 2*pi*(f - fRef)*M/fs exactly. Unambiguous while
/// |f - fRef| < fs / (2M).
///
/// @param x    Signal, must hold at least 2*M samples
/// @param M    Per-window length
/// @param fs   Sample rate in Hz
/// @param fRef Reference (heterodyne) frequency in Hz
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

/// Two-stage heterodyne phase-slope frequency estimator.
[[nodiscard]] double estimateFrequency(const float* x, std::size_t N, double fs, double fRef) {
    constexpr std::size_t kStage1M = 4096;
    if (x == nullptr || N < 2 * kStage1M) {
        return 0.0;
    }
    const double coarse = heterodyneStage(x, kStage1M, fs, fRef);
    return heterodyneStage(x, N / 2, fs, coarse);
}

// =============================================================================
// SC-010 pinned configuration (spec.md SC-010, plan §7.4, tasks T018)
// =============================================================================

constexpr std::size_t kSc010NumRates = 3;
constexpr std::array<double, kSc010NumRates> kSc010Rates{44100.0, 48000.0, 96000.0};

/// Deliberately the SAME value as `kSc014Seed`, not an independent draw. FR-016
/// draws each partial's initial MCF phase from the seeded stream and the summed
/// crest factor of a 64-partial incoherent sum is a random variable in the seed,
/// so the unclipped precondition below needs a pinned one. This configuration
/// (f0 = 110 Hz, Richness 1, tilt 0, spread 0) is EXACTLY measureRichness()'s
/// r = 1 case, whose modelled worst channel peak at this seed is 0.92 — 5.8 dB of
/// margin to the 0.9 * kOutputClamp = 1.8 bound (see kSc014Seed).
constexpr std::uint32_t kSc010Seed = kSc014Seed;

/// SC-010's amplitude scope: 0.8 x Nyquist at the LOWEST rate under test.
/// FR-015's fade starts at a fixed FRACTION of Nyquist (kAntiAliasFadeStart),
/// i.e. 17.64 kHz at 44.1 kHz but 38.4 kHz at 96 kHz, so an unscoped amplitude
/// comparison fails a CORRECT implementation. The configuration is chosen so no
/// active partial reaches this bound (top partial 110 x 64 = 7040 Hz) and the
/// test ASSERTS that rather than assuming it.
constexpr float kSc010AmplitudeScopeHz =
    HarmonicCloud::kAntiAliasFadeStart * 0.5f * 44100.0f;  // 17640 Hz

constexpr float kSc010AmplitudeToleranceDb = 0.5f;

/// The SC-001 estimator tolerance, borrowed verbatim (spec.md SC-010).
constexpr double kSc010FrequencyToleranceCents = 0.1;

/// One control chunk at the lowest rate under test: kControlChunkSamples / 44100.
constexpr double kSc010TimingToleranceSec =
    static_cast<double>(HarmonicCloud::kControlChunkSamples) / 44100.0;

/// The shipped attack floor (50 ms). Short renders, and 20x the FR-014 smoother
/// time constant, so the smoother's ~2 ms lag stays a small, rate-independent
/// offset on the 25 ms crossing.
constexpr float kSc010AttackSec = HarmonicCloud::kMinAttackSec;

/// Frequency half: 1.2 s rendered, first 200 ms skipped (FR-014's smoother and
/// the 50 ms attack have both settled by then). 1.0 s of analysis is 44100
/// samples at the lowest rate, well past the estimator's 2 x 4096 minimum.
constexpr double kSc010FreqRenderSec = 1.2;
constexpr double kSc010FreqSkipSec = 0.2;

/// Amplitude half: 2.5 s rendered, the last kFftSize samples analysed. At the
/// lowest rate that leaves 44714 samples (1.01 s) of settling ahead of the
/// analysis window.
constexpr double kSc010SpectrumRenderSec = 2.5;

/// Timing half: sample the envelope trajectory every 0.25 ms so the crossing
/// quantization (<= 0.25 ms per rate) stays far inside the 1.45 ms tolerance.
constexpr double kSc010TimingIntervalSec = 0.00025;
constexpr double kSc010TimingRenderSec = 0.15;

/// @brief The one pinned musical configuration, applied identically at every rate.
///
/// Drift depth 0 is load-bearing twice over: it makes `detune == 1`, where plan
/// D5's `sqrt(1 - (eps*detune/2)^2)` anti-alias form is exactly the reference
/// `cos(pi*f/fs)` form, and it keeps every partial's bin fixed so the +-4 bin
/// main-lobe windows below stay valid.
void configureSc010Cloud(HarmonicCloud& cloud, double sampleRate) {
    cloud.prepare(sampleRate);
    cloud.setSeed(kSc010Seed);
    cloud.setFundamentalHz(kF0Hz);   // 110 Hz -> top partial 7040 Hz
    cloud.setRichness(1.0f);         // N(1) = 64 (FR-041)
    cloud.setSpectralTiltDb(0.0f);
    cloud.setInharmonicity(0.0f);    // exactly harmonic ratios
    cloud.setSpectralGravity(0.0f);
    cloud.setMutation(0.0f);         // w_i == 1
    cloud.setDriftDepthCents(0.0f);  // detune == 1, see above
    cloud.setStereoSpread(0.0f);     // both channels identical
    cloud.setAttackTimeSec(kSc010AttackSec);
    cloud.setEnvelopeOffsetSpread(0.0f);  // every partial shares one attack time
}

/// @brief Solo one partial at one rate and estimate its frequency in Hz.
[[nodiscard]] double measureSc010PartialHz(double sampleRate, std::size_t partialIndex,
                                           double expectedHz) {
    const auto totalSamples = static_cast<std::size_t>(kSc010FreqRenderSec * sampleRate);
    const auto skipSamples = static_cast<std::size_t>(kSc010FreqSkipSec * sampleRate);

    HarmonicCloud cloud;
    configureSc010Cloud(cloud, sampleRate);
    cloud.soloPartial(partialIndex);  // FR-008, itself faded by the FR-014 smoother
    cloud.noteOn();

    std::vector<float> mono(totalSamples, 0.0f);
    std::array<float, kRenderBlock> leftBlock{};
    std::array<float, kRenderBlock> rightBlock{};
    for (std::size_t done = 0; done < totalSamples; done += kRenderBlock) {
        const std::size_t n = std::min(kRenderBlock, totalSamples - done);
        cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), n);
        for (std::size_t s = 0; s < n; ++s) {
            mono[done + s] = 0.5f * (leftBlock[s] + rightBlock[s]);
        }
    }

    return estimateFrequency(mono.data() + skipSamples, totalSamples - skipSamples, sampleRate,
                             expectedHz);
}

struct Sc010Spectrum {
    /// Main-lobe power sum in dB, with the MCF orbit gain divided out (see below).
    std::array<float, HarmonicCloud::kMaxPartials> correctedDb{};
    /// FR-008's own frequency-law output, per partial, in Hz.
    std::array<float, HarmonicCloud::kMaxPartials> synthesizedHz{};
    std::size_t activeCount = 0;
    float peak = 0.0f;  ///< worst channel peak over the analysed window
};

/// @brief Render the pinned configuration at one rate and measure its per-partial
///        amplitude spectrum from the RENDERED SIGNAL (SC-010, plan §7.4).
///
/// @par Why the amplitude is not read from the accessors
/// `getPartialCurrentAmplitude / getPartialAntiAliasGain` is a vacuous ratio for
/// this criterion. The kernel drives `currentAmplitude -> targetAmplitude *
/// antiAliasGain` (`harmonic_oscillator_bank_simd.cpp:91-93`), whose steady state
/// is independent of `ampSmoothCoeff_`, and dividing by `antiAliasGain` cancels
/// the only sample-rate-dependent factor left — leaving
/// `targetAmplitude = gain * a_i`, which contains no sample-rate term at all and
/// comes back bit-identical at all three rates. The test would then pass even
/// with a coefficient wrongly expressed in samples, which is the whole failure
/// SC-010 exists to catch.
///
/// @par Why the MCF orbit correction is mandatory HERE and not only in SC-014
/// The rendered sample is `sinState * currentAmplitude`, and `sinState` is not a
/// unit sinusoid: for `s' = s + eps*c`, `c' = c - eps*s'` with `eps = 2*sin(w/2)`,
/// the orbit amplitude satisfies `A^2 * cos^2(w/2) = 1 + sin(w/2) * sin(2*phi)`.
/// `antiAliasGain` pre-compensates the `1/cos(w/2)` factor, but the residual
/// `sqrt(1 + sin(w/2)*sin(2*phi))` depends on `w = 2*pi*f/fs` and therefore on the
/// SAMPLE RATE, at the same seeded phase. At the 7040 Hz top partial that residual
/// spans +1.71/-2.85 dB at 44.1 kHz against +0.89/-1.13 dB at 96 kHz — up to
/// ~1.7 dB of legitimate difference, more than triple SC-010's 0.5 dB budget. It
/// is a property of the MCF recurrence, not of a coefficient, so an uncorrected
/// comparison would fail a CORRECT implementation.
///
/// `A` is an invariant of the orbit, recovered from the state at any time via
/// `A^2 = s^2 + (c + k*s)^2 / q^2` with `k = sin(pi*f/fs)`, `q = cos(pi*f/fs)` —
/// the identical correction measureRichness() applies, using only shipped FR-008
/// accessors and re-implementing no part of the amplitude law.
[[nodiscard]] Sc010Spectrum measureSc010Spectrum(double sampleRate) {
    const auto sampleRateF = static_cast<float>(sampleRate);
    const auto totalSamples = static_cast<std::size_t>(kSc010SpectrumRenderSec * sampleRate);
    REQUIRE(totalSamples > kFftSize);

    HarmonicCloud cloud;
    configureSc010Cloud(cloud, sampleRate);
    cloud.noteOn();

    std::vector<float> left(totalSamples, 0.0f);
    std::vector<float> right(totalSamples, 0.0f);
    std::array<float, kRenderBlock> leftBlock{};
    std::array<float, kRenderBlock> rightBlock{};
    for (std::size_t done = 0; done < totalSamples; done += kRenderBlock) {
        const std::size_t n = std::min(kRenderBlock, totalSamples - done);
        cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), n);
        for (std::size_t s = 0; s < n; ++s) {
            left[done + s] = leftBlock[s];
            right[done + s] = rightBlock[s];
        }
    }

    Sc010Spectrum result;
    result.activeCount = cloud.getActivePartialCount();

    const std::size_t analysisStart = totalSamples - kFftSize;
    std::vector<float> frame(kFftSize, 0.0f);
    std::vector<float> window(kFftSize, 0.0f);
    Window::generateBlackmanHarris(window.data(), kFftSize);
    for (std::size_t i = 0; i < kFftSize; ++i) {
        const float sample = left[analysisStart + i];
        result.peak = std::max({result.peak, std::abs(sample),
                                std::abs(right[analysisStart + i])});
        frame[i] = sample * window[i];
    }

    FFT fft;
    fft.prepare(kFftSize);
    REQUIRE(fft.isPrepared());  // plan §7.2: never analyse a zero-size spectrum

    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(frame.data(), spectrum.data());

    const std::size_t lastBin = fft.numBins() - 1;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        const float fHz = cloud.getPartialFrequencyHz(i);
        result.synthesizedHz[i] = fHz;

        // The window transform is a function of BINS, not of Hz, so the same +-4
        // bin main-lobe sum is the correct measurement at every rate; summing
        // POWER over the whole lobe removes the fractional-bin scalloping loss
        // that would otherwise differ across rates (110 Hz is 163.5 bins apart at
        // 44.1 kHz and 75.1 at 96 kHz, so the lobes never overlap either).
        const std::size_t center = TestUtils::frequencyToBin(fHz, sampleRateF, kFftSize);
        const std::size_t lo = (center > kLobeHalfWidth) ? (center - kLobeHalfWidth) : 0;
        const std::size_t hi = std::min(center + kLobeHalfWidth, lastBin);

        float power = 0.0f;
        for (std::size_t b = lo; b <= hi; ++b) {
            const float mag = spectrum[b].magnitude();
            power += mag * mag;
        }

        const float halfAngle = kPi * fHz / sampleRateF;
        const float k = std::sin(halfAngle);
        const float q = std::cos(halfAngle);
        const float s = cloud.getPartialSinState(i);
        const float c = cloud.getPartialCosState(i);
        const float cross = c + k * s;
        const float orbitAmplitude = std::sqrt(s * s + (cross * cross) / (q * q));
        const float renderGain = cloud.getPartialAntiAliasGain(i) * orbitAmplitude;
        const float renderGainDb =
            (renderGain > 0.0f) ? 20.0f * std::log10(renderGain) : kFloorDb;

        result.correctedDb[i] = powerToDb(power) - renderGainDb;
    }

    return result;
}

struct Sc010Timing {
    double crossingSec = -1.0;      ///< first time the fundamental reaches 50 % of steady state
    float steadyAmplitude = 0.0f;   ///< unmutated target x anti-alias gain (SC-013's reference)
    float finalAmplitude = 0.0f;    ///< current amplitude at the end of the render
};

/// @brief Measure partial 0's attack 50 %-crossing time IN SECONDS at one rate.
///
/// This is SC-010's one sample-rate-SENSITIVE assertion — the FR-003 failure the
/// criterion exists to catch. `ampSmoothCoeff_`, `crossfadeLengthSamples_` and the
/// envelope's `dt = chunk / fs` are all expressed in samples internally; if any of
/// them is derived from a hard-coded rate the crossing moves in TIME while every
/// frequency and every steady-state amplitude above stays put.
///
/// The reference level is `getPartialUnmutatedTargetAmplitude(i) *
/// getPartialAntiAliasGain(i)` — the accessor pair that excludes BOTH the mutation
/// weight and the envelope (plan §7.4, SC-013), so it is a fixed steady-state
/// level even while `env` is moving. `getPartialTargetAmplitude` would not do:
/// it is `unmutated * w * env` and therefore moves WITH the envelope, which would
/// fire the crossing at roughly twice the smoother lag instead of at half the
/// sounding level.
///
/// Offset spread is 0 (see configureSc010Cloud), so partial 0 carries no offset
/// and its attack time is exactly `kSc010AttackSec` at every rate.
[[nodiscard]] Sc010Timing measureSc010AttackCrossing(double sampleRate) {
    const auto blockSamples =
        static_cast<std::size_t>(std::lround(sampleRate * kSc010TimingIntervalSec));
    REQUIRE(blockSamples > 0u);
    const auto numBlocks =
        static_cast<std::size_t>(kSc010TimingRenderSec / kSc010TimingIntervalSec) + 1;

    HarmonicCloud cloud;
    configureSc010Cloud(cloud, sampleRate);
    cloud.noteOn();

    std::vector<float> leftBlock(blockSamples, 0.0f);
    std::vector<float> rightBlock(blockSamples, 0.0f);
    std::vector<float> trajectory;
    trajectory.reserve(numBlocks);

    for (std::size_t b = 0; b < numBlocks; ++b) {
        cloud.processStereoBlock(leftBlock.data(), rightBlock.data(), blockSamples);
        trajectory.push_back(cloud.getPartialCurrentAmplitude(0));
    }

    Sc010Timing result;
    result.steadyAmplitude =
        cloud.getPartialUnmutatedTargetAmplitude(0) * cloud.getPartialAntiAliasGain(0);
    result.finalAmplitude = trajectory.back();

    const float mark = 0.5f * result.steadyAmplitude;
    for (std::size_t b = 0; b < numBlocks; ++b) {
        if (trajectory[b] >= mark) {
            result.crossingSec =
                static_cast<double>((b + 1) * blockSamples) / sampleRate;
            break;
        }
    }
    return result;
}

}  // namespace

// =============================================================================
// SC-014 — Richness law: count, rolloff, and the two monotone metrics
// =============================================================================
// FR-041 (a) N(r) = round(64^r); (b) a_n = n^(-p(r)), p(r) = 3.0 - 2.5*r.
// FR-042 monotone non-decreasing count above the audibility floor and
//        above-fundamental energy fraction; r=1 beats r=0 by >=16 partials and
//        >=20 dB.
//
// Modelled reference values for this configuration (double-precision model of
// plan §3's amplitude chain + §2's anti-alias + the MCF orbit, same seed):
//   r      N   p      worst law error (corrected / uncorrected)  count  fracDb
//   0      1   3.000        0.000 / 0.000 dB                        1   -80.00
//   0.25   3   2.375        0.000 / 0.116 dB                        3   -13.80
//   0.5    8   1.750        0.000 / 0.172 dB                        8    -9.47
//   0.75  23   1.125        0.000 / 0.525 dB                       23    -5.07
//   1     64   0.500        0.000 / 2.327 dB                       64    -1.03
// =============================================================================

TEST_CASE("HarmonicCloud_RichnessAddsPartialsAndEnergy", "[systems][harmonic_cloud][spectral]") {
    constexpr std::size_t kNumSettings = 5;
    constexpr std::array<float, kNumSettings> kRichness{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    // N(r) = round(64^r), EXACTLY (FR-041(a)): 1, 2.828, 8, 22.63, 64.
    constexpr std::array<std::size_t, kNumSettings> kExpectedCount{1u, 3u, 8u, 23u, 64u};

    // Weakest active partial's rolloff at each setting, N^(-p(r)), as a sanity
    // anchor on the corrected spec table (spec.md SC-014, plan D7): -22.7, -31.6,
    // -30.6, -18.1 dB for r = 0.25 .. 1 — all far above the -60 dB floor, which
    // is why the measured count must equal the active count.
    constexpr std::array<float, kNumSettings> kWeakestPartialDb{0.0f, -22.7f, -31.6f, -30.6f,
                                                                -18.1f};

    std::array<RichnessMeasurement, kNumSettings> measured{};

    for (std::size_t s = 0; s < kNumSettings; ++s) {
        const float r = kRichness[s];
        INFO("Richness = " << r);

        measured[s] = measureRichness(r);
        const RichnessMeasurement& m = measured[s];

        // ---- 1. Active count is explicit state and follows round(64^r) exactly.
        REQUIRE(m.activeCount == kExpectedCount[s]);

        // FR-017 must keep the summed output out of FR-006's clamp; a clipped
        // render would corrupt every dB below, so name that failure here.
        REQUIRE(m.peak < HarmonicCloud::kOutputClamp);

        // ---- 2. Per-partial rolloff follows a_n = n^(-p(r)) within 0.5 dB.
        // Referencing partial 1 removes the single FR-017 scalar normalization
        // gain (one scalar for all partials, so it cannot bend a slope);
        // subtracting renderGainDb removes the anti-alias gain and the
        // phase-dependent MCF orbit gain (see measureRichness()).
        const float p = 3.0f - 2.5f * r;
        const float reference = m.levelDb[0] - m.renderGainDb[0];

        for (std::size_t i = 1; i < m.activeCount; ++i) {
            const float n = static_cast<float>(i + 1);
            const float relativeDb = (m.levelDb[i] - m.renderGainDb[i]) - reference;
            const float expectedDb = -20.0f * p * std::log10(n);
            INFO("partial n = " << n << ", measured " << relativeDb << " dB, expected "
                                << expectedDb << " dB");
            REQUIRE(std::abs(relativeDb - expectedDb) <= 0.5f);
        }

        // The weakest active partial must sit where the spec's table says, well
        // clear of the audibility floor.
        if (m.activeCount > 1) {
            const float weakest =
                (m.levelDb[m.activeCount - 1] - m.renderGainDb[m.activeCount - 1]) - reference;
            INFO("weakest active partial = " << weakest << " dB");
            // 0.5 dB (the law tolerance) plus the table's own rounding to 0.1 dB.
            REQUIRE(std::abs(weakest - kWeakestPartialDb[s]) <= 0.6f);
            REQUIRE(weakest > kAudibilityFloorDb);
        }

        // ---- Metric (a) sanity: everything active is audible, nothing else is.
        REQUIRE(m.countAboveFloor == kExpectedCount[s]);
    }

    // ---- 3./4. Both metrics monotonically non-decreasing across the settings.
    for (std::size_t s = 1; s < kNumSettings; ++s) {
        INFO("Richness " << kRichness[s - 1] << " -> " << kRichness[s]);
        INFO("count " << measured[s - 1].countAboveFloor << " -> " << measured[s].countAboveFloor);
        INFO("energy fraction " << measured[s - 1].energyFractionDb << " dB -> "
                                << measured[s].energyFractionDb << " dB");
        REQUIRE(measured[s].countAboveFloor >= measured[s - 1].countAboveFloor);
        REQUIRE(measured[s].energyFractionDb >= measured[s - 1].energyFractionDb);
    }

    // ---- 5. Endpoint discrimination (FR-042).
    const auto countAtZero = static_cast<int>(measured[0].countAboveFloor);
    const auto countAtOne = static_cast<int>(measured[kNumSettings - 1].countAboveFloor);
    INFO("count(r=1) = " << countAtOne << ", count(r=0) = " << countAtZero);
    REQUIRE(countAtOne - countAtZero >= 16);

    const float fractionAtZero = measured[0].energyFractionDb;
    const float fractionAtOne = measured[kNumSettings - 1].energyFractionDb;
    INFO("energyFractionDb(r=1) = " << fractionAtOne << ", (r=0) = " << fractionAtZero);
    REQUIRE(fractionAtOne - fractionAtZero >= 20.0f);
}

// =============================================================================
// SC-003 — spectral tilt law (FR-061), differentially isolated from FR-041
// =============================================================================
// The rendered spectrum carries BOTH the FR-061 tilt gain and the FR-041 rolloff
// `a_n = n^(-p)`, which at this Richness contributes a fixed -5.52 dB/octave.
// The criterion is therefore evaluated differentially against the tilt-0 render
// of the identical configuration — and the tilt-0 render's own fitted slope is
// separately required to match -5.52 dB/oct, which pins the Richness rolloff law
// itself. That is what makes the differential an isolation rather than a
// relaxation.
//
// Measured on the real component (T005 step 1; Windows 11, MSVC Release,
// 2026-07-25), seed 0x5E3A3303, spread 0, against the 0.5 dB/oct budget:
//
//   tilt   fitted slope   slope(t)-slope(0)   error   worst per-partial residual
//   -12    -17.5396          -12.0000        0.0000        0.0006 dB
//    -6    -11.5396           -6.0000        0.0000        0.0000 dB
//     0     -5.5396           +0.0000        0.0000        0.0000 dB   (vs -5.52:
//    +6     +0.4604           +6.0000        0.0000        0.0000 dB    err 0.0196)
//   +12     +6.4604          +12.0000        0.0000        0.0000 dB
//
// and the unclipped precondition (peak over the analysed window, worst channel):
//   0.5390 / 0.5865 / 0.7814 / 0.9085 / 0.8494 against the 1.8 bound
//   -> worst margin 5.94 dB. See kSc003Seed for the 32-seed sweep behind it.
// =============================================================================

TEST_CASE("HarmonicCloud_TiltSlopeMatchesSetting", "[systems][harmonic_cloud][spectral]") {
    constexpr std::size_t kNumTilts = 5;
    constexpr std::array<float, kNumTilts> kTilts{-12.0f, -6.0f, 0.0f, 6.0f, 12.0f};
    constexpr std::size_t kTiltZeroIndex = 2;  // kTilts[2] == 0.0f

    static_assert(kTilts[kTiltZeroIndex] == 0.0f, "the reference render must be tilt 0");
    static_assert(kTilts[0] == HarmonicCloud::kMinTiltDbPerOct, "sweep spans FR-062's range");
    static_assert(kTilts[kNumTilts - 1] == HarmonicCloud::kMaxTiltDbPerOct,
                  "sweep spans FR-062's range");

    std::array<TiltMeasurement, kNumTilts> measured{};

    // ---- Precondition FIRST, on the rendered channel buffers.
    // FR-017's normalization is what makes this criterion measurable at all: at
    // +12 dB/oct the raw gain law gives gain(32) ~ 10^3 x the fundamental. A
    // clipped render would silently FLATTEN the fitted slope, so a normalization
    // failure has to fail here, loudly, before any fit runs.
    for (std::size_t t = 0; t < kNumTilts; ++t) {
        INFO("tilt = " << kTilts[t] << " dB/oct");
        measured[t] = measureTilt(kTilts[t]);
        INFO("worst channel peak over the analysed window = " << measured[t].peak);
        REQUIRE(measured[t].peak < 0.9f * HarmonicCloud::kOutputClamp);
    }

    // ---- Clause 2: the tilt-0 slope pins the FR-041 rolloff, -20*p*log10(2) at
    // p = 0.9167. Without it the differential below could be satisfied by an
    // implementation whose rolloff law is wrong in the same way at every tilt.
    const float slopeZero = measured[kTiltZeroIndex].slopeDbPerOct;
    INFO("tilt-0 fitted slope = " << slopeZero << " dB/oct, expected "
                                  << kSc003RolloffDbPerOct);
    REQUIRE(std::abs(slopeZero - kSc003RolloffDbPerOct) <= kSc003ToleranceDb);

    for (std::size_t t = 0; t < kNumTilts; ++t) {
        const float tilt = kTilts[t];
        INFO("tilt = " << tilt << " dB/oct");

        // ---- Clause 1: the tilt-isolated slope equals the setting.
        const float differentialSlope = measured[t].slopeDbPerOct - slopeZero;
        INFO("slope(t) = " << measured[t].slopeDbPerOct << ", slope(t) - slope(0) = "
                           << differentialSlope);
        REQUIRE(std::abs(differentialSlope - tilt) <= kSc003ToleranceDb);

        // ---- Clause 3: per-partial gain follows 10^(tilt*log2(n)/20) exactly,
        // after removing ONE fitted scalar offset `c`. Two scalars are legitimately
        // absorbed there and neither can change a slope or a partial ratio: the
        // FR-017 normalization gain (one scalar across all partials, FR-017) and the
        // tilt-0 reference level.
        std::array<float, kSc003Partials> residual{};
        double sum = 0.0;
        for (std::size_t i = 0; i < kSc003Partials; ++i) {
            const float n = static_cast<float>(i + 1);
            residual[i] = measured[t].levelDb[i] - measured[kTiltZeroIndex].levelDb[i]
                          - tilt * std::log2(n);
            sum += static_cast<double>(residual[i]);
        }
        const auto offset = static_cast<float>(sum / static_cast<double>(kSc003Partials));

        for (std::size_t i = 0; i < kSc003Partials; ++i) {
            INFO("partial n = " << (i + 1) << ", residual " << residual[i] << " dB, fitted offset "
                                << offset << " dB");
            REQUIRE(std::abs(residual[i] - offset) <= kSc003ToleranceDb);
        }
    }
}

// =============================================================================
// SC-011 — no aliasing at the extremes
// =============================================================================
// FR-015: partials at or above Nyquist are amplitude-suppressed before they can
// alias — full gain below 0.8 x Nyquist, fading to zero at Nyquist, with the MCF
// orbit correction and the |eps| < 2 clamp.
//
// The worst case for aliasing is the FR-013 maximum fundamental (4000 Hz) with
// every partial active (FR-012), maximum inharmonicity (FR-052) and maximum
// |gravity| (FR-081) — both of which push high partials FURTHER up — at the
// lowest supported sample rate. BOTH gravity signs are run: |g| = 1 is two
// distinct spectra (n^1.1 stretches, n^0.9 compresses) and they fold to different
// bins, so testing one sign leaves half of FR-081's range unmeasured.
//
// The energy compared against the fundamental is only ever in bins reachable by a
// fold-back image and by nothing else — every bin within +-2 of a legitimately
// synthesized sub-Nyquist partial is excluded first.
// =============================================================================

TEST_CASE("HarmonicCloud_NoAliasingAtExtremes", "[systems][harmonic_cloud][spectral]") {
    constexpr std::size_t kNumGravities = 2;
    constexpr std::array<float, kNumGravities> kGravities{1.0f, -1.0f};

    for (std::size_t s = 0; s < kNumGravities; ++s) {
        const float gravity = kGravities[s];
        INFO("gravity = " << gravity);

        const AliasMeasurement m = measureAliasFloor(gravity);

        // ---- Preconditions, so a broken measurement cannot look like a pass.
        // A clipped render would generate genuine intermodulation products in the
        // alias bins, i.e. the criterion would fail for a reason that has nothing
        // to do with FR-015. Name that failure here instead.
        INFO("worst channel peak over the analysed window = " << m.peak);
        REQUIRE(m.peak < HarmonicCloud::kOutputClamp);

        // The fundamental must be audible and there must be something to measure:
        // if no partial exceeded Nyquist, or every image fell inside an exclusion
        // window, the criterion would be vacuously satisfied.
        INFO("sub-Nyquist partials = " << m.subNyquistCount
                                       << ", alias-only bins = " << m.aliasBinCount);
        REQUIRE(m.subNyquistCount >= 1u);
        REQUIRE(m.subNyquistCount < HarmonicCloud::kMaxPartials);
        REQUIRE(m.aliasBinCount > 0u);

        // ---- SC-011 itself.
        INFO("alias " << m.aliasDb << " dB, fundamental " << m.fundamentalDb << " dB, ratio "
                      << (m.aliasDb - m.fundamentalDb) << " dB");
        REQUIRE(m.aliasDb - m.fundamentalDb <= kSc011ThresholdDb);
    }
}

// =============================================================================
// SC-010 — sample-rate invariance (FR-003)
// =============================================================================
// The same MUSICAL configuration rendered at 44.1 / 48 / 96 kHz must give the same
// PHYSICAL result. Three independent halves, because each one alone is passable by
// a broken implementation:
//
//   1. FREQUENCY (Hz) per partial, within the SC-001 estimator tolerance. Measured
//      with the two-stage heterodyne estimator on a soloed partial.
//   2. AMPLITUDE (dB) per partial, within 0.5 dB, measured from the RENDERED
//      SIGNAL and SCOPED to partials below 0.8 x Nyquist AT THE LOWEST RATE — the
//      scope is asserted, not assumed, because FR-015's fade starts at a fixed
//      FRACTION of Nyquist and an unscoped comparison fails a correct build.
//   3. TIMING: the attack 50 %-crossing time IN SECONDS, within one control chunk
//      (64 / 44100 s). This is the assertion with teeth for FR-003 — every
//      sample-rate-dependent coefficient in the component (`ampSmoothCoeff_`,
//      `crossfadeLengthSamples_`, the envelope's `dt = chunk / fs`) is expressed
//      in samples, and clauses 1 and 2 measure only steady state, where none of
//      them appears.
// =============================================================================

TEST_CASE("HarmonicCloud_SampleRateInvariant", "[systems][harmonic_cloud][spectral]") {
    // -------------------------------------------------------------------------
    // Preconditions and scope (SC-010's "asserted rather than assumed" clause)
    // -------------------------------------------------------------------------
    std::array<Sc010Spectrum, kSc010NumRates> spectra{};

    for (std::size_t r = 0; r < kSc010NumRates; ++r) {
        INFO("sample rate = " << kSc010Rates[r] << " Hz");
        spectra[r] = measureSc010Spectrum(kSc010Rates[r]);

        // Richness 1 -> N(1) = 64 at every rate; the partial count is explicit
        // state (FR-041) and must not be a function of the sample rate.
        REQUIRE(spectra[r].activeCount == HarmonicCloud::kMaxPartials);

        // A clipped render would corrupt every dB below, so name that failure here.
        INFO("worst channel peak over the analysed window = " << spectra[r].peak);
        REQUIRE(spectra[r].peak < 0.9f * HarmonicCloud::kOutputClamp);
    }

    // The whole comparison rests on the SYNTHESIZED frequency set being identical
    // across rates; if it were not, comparing per-partial bins would be comparing
    // different partials.
    for (std::size_t r = 1; r < kSc010NumRates; ++r) {
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("rate " << kSc010Rates[r] << " Hz, partial n = " << (i + 1));
            REQUIRE(spectra[r].synthesizedHz[i]
                    == Approx(spectra[0].synthesizedHz[i]).margin(1.0e-3));
        }
    }

    // SC-010's amplitude scope, ASSERTED: every active partial must sit below
    // 0.8 x Nyquist at the LOWEST rate under test, so no partial in the comparison
    // below is legitimately attenuated at 44.1 kHz while being at full gain at
    // 96 kHz. f0 = 110 Hz puts the top partial at 7040 Hz, 1.3 octaves clear of the
    // 17640 Hz bound — but a configuration change must fail here, loudly, rather
    // than silently turn clause 2 into a comparison of fade gains.
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        INFO("partial n = " << (i + 1) << " at " << spectra[0].synthesizedHz[i] << " Hz, bound "
                            << kSc010AmplitudeScopeHz << " Hz");
        REQUIRE(spectra[0].synthesizedHz[i] < kSc010AmplitudeScopeHz);
    }

    // -------------------------------------------------------------------------
    // 1. Frequency in Hz agrees across rates within the SC-001 tolerance
    // -------------------------------------------------------------------------
    std::array<std::array<double, HarmonicCloud::kMaxPartials>, kSc010NumRates> measuredHz{};

    for (std::size_t r = 0; r < kSc010NumRates; ++r) {
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            const double expectedHz =
                static_cast<double>(kF0Hz) * static_cast<double>(i + 1);
            measuredHz[r][i] = measureSc010PartialHz(kSc010Rates[r], i, expectedHz);

            INFO("rate " << kSc010Rates[r] << " Hz, partial n = " << (i + 1) << ", expected "
                         << expectedHz << " Hz, measured " << measuredHz[r][i] << " Hz");
            REQUIRE(measuredHz[r][i] > 0.0);
        }
    }

    for (std::size_t r = 1; r < kSc010NumRates; ++r) {
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            const double ratio = measuredHz[r][i] / measuredHz[0][i];
            const double cents =
                100.0 * static_cast<double>(ratioToSemitones(static_cast<float>(ratio)));

            INFO("partial n = " << (i + 1) << ": " << kSc010Rates[0] << " Hz -> "
                                << measuredHz[0][i] << " Hz, " << kSc010Rates[r] << " Hz -> "
                                << measuredHz[r][i] << " Hz, difference " << cents << " cent");
            REQUIRE(std::abs(cents) < kSc010FrequencyToleranceCents);
        }
    }

    // -------------------------------------------------------------------------
    // 2. Per-partial amplitude spectrum agrees across rates within 0.5 dB
    // -------------------------------------------------------------------------
    // Compared ABSOLUTELY, with no per-rate offset fitted: FR-017's normalization
    // gain is derived from the un-mutated `a_i` set alone and carries no
    // sample-rate term, so a global level difference across rates would itself be
    // the defect this criterion is looking for.
    for (std::size_t r = 1; r < kSc010NumRates; ++r) {
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            const float deltaDb = spectra[r].correctedDb[i] - spectra[0].correctedDb[i];

            INFO("partial n = " << (i + 1) << " at " << spectra[0].synthesizedHz[i] << " Hz: "
                                << kSc010Rates[0] << " Hz -> " << spectra[0].correctedDb[i]
                                << " dB, " << kSc010Rates[r] << " Hz -> "
                                << spectra[r].correctedDb[i] << " dB, difference " << deltaDb
                                << " dB");
            REQUIRE(std::abs(deltaDb) <= kSc010AmplitudeToleranceDb);
        }
    }

    // -------------------------------------------------------------------------
    // 3. The attack 50 %-crossing time in SECONDS agrees within one control chunk
    // -------------------------------------------------------------------------
    std::array<Sc010Timing, kSc010NumRates> timing{};

    for (std::size_t r = 0; r < kSc010NumRates; ++r) {
        INFO("sample rate = " << kSc010Rates[r] << " Hz");
        timing[r] = measureSc010AttackCrossing(kSc010Rates[r]);

        // Preconditions: there has to be a steady state to be half of, and the
        // render has to have reached it, or the crossing measures nothing.
        INFO("steady-state amplitude = " << timing[r].steadyAmplitude
                                         << ", final = " << timing[r].finalAmplitude);
        REQUIRE(timing[r].steadyAmplitude > 0.0f);
        REQUIRE(timing[r].finalAmplitude >= 0.95f * timing[r].steadyAmplitude);
        REQUIRE(timing[r].crossingSec > 0.0);

        // Absolute anchor: FR-023's attack is a LINEAR time-to-100 %, so the 50 %
        // mark of the steady-state level falls at attack/2 (25 ms), delayed by the
        // FR-014 smoother's one-time-constant lag on a linear ramp (~2 ms). An
        // envelope whose `dt` is counted in samples rather than seconds lands
        // nowhere near this window at any rate.
        const double attackSec = static_cast<double>(kSc010AttackSec);
        const double smootherSec = static_cast<double>(HarmonicCloud::kAmpSmoothTimeSec);
        const double lowerBound = 0.4 * attackSec;                    // 0.020 s
        const double upperBound = 0.6 * attackSec + 5.0 * smootherSec;  // 0.040 s

        INFO("crossing = " << timing[r].crossingSec << " s, expected ~"
                           << (0.5 * attackSec + smootherSec) << " s, window [" << lowerBound
                           << ", " << upperBound << "] s");
        REQUIRE(timing[r].crossingSec > lowerBound);
        REQUIRE(timing[r].crossingSec < upperBound);
    }

    for (std::size_t r = 1; r < kSc010NumRates; ++r) {
        const double deltaSec = timing[r].crossingSec - timing[0].crossingSec;

        INFO("crossing at " << kSc010Rates[0] << " Hz = " << timing[0].crossingSec << " s, at "
                            << kSc010Rates[r] << " Hz = " << timing[r].crossingSec
                            << " s, difference " << deltaSec << " s, tolerance "
                            << kSc010TimingToleranceSec << " s (one control chunk at 44.1 kHz)");
        REQUIRE(std::abs(deltaSec) <= kSc010TimingToleranceSec);
    }
}
