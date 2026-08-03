// ==============================================================================
// Seraphis - Parameter character tests (Phase 9)
// ==============================================================================
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (§7.0, §7.11)
//
// CRITERIA OWNED BY THIS TU (plan §7.0's test-file map):
//   SC-019  sample-rate independence - the same parameter settings produce the
//           same character at 44.1 kHz and 96 kHz
//   SC-020  the seed - kSeedId selects a reproducible stochastic realization,
//           at the thresholds plan §7.11 pins from measurement
//
// COMPILE FLAGS: this TU is NOT listed under "-fno-fast-math
//   -fno-finite-math-only" in plugins/seraphis/tests/CMakeLists.txt.
//   Consequences, both handled below:
//     * every finiteness check goes through isFiniteBits() (exponent bits), never
//       std::isnan / std::isfinite, which -ffast-math folds away on the macOS leg;
//     * no threshold in this file is a bit-exact float golden. SC-020 clause 1
//       compares through render_fingerprint.h's measured tolerances; SC-019's two
//       gates are 1.0 dB and 5 %, both spec-pinned.
//
// THE PINNED ANALYSIS (SC-019, spec.md:2315-2336 / plan §7.11)
//   the SETTLED LAST SECOND of a 4 s render of note 60, 65 536-point FFT,
//   4-term Blackman-Harris, metrics over the 20 Hz - 16 kHz band ONLY. The band
//   matters: a centroid computed to Nyquist is not comparable between 44.1 and
//   96 kHz by construction, because the two integrals do not even span the same
//   frequencies.
//
//   Gates: output RMS within 1.0 dB across 44.1 / 48 / 96 kHz; band-limited
//   spectral centroid within 5 %. SPECTRAL FLATNESS IS RECORDED, NOT GATED - it
//   is dominated by the stochastic atmosphere and reverb tails, whose realisation
//   is exactly what RollingCaptureBuffer::prepare's power-of-two capacity
//   rounding changes between rates (an 8.8 % rate-dependent spread in ring
//   seconds, per Phase 5). The "no denormalization reads sampleRate" clause is
//   FR-019's, a review/lint item, and is deliberately NOT asserted here.
// ==============================================================================

#include "seraphis_test_fixture.h"

#include "parameters/dropdown_mappings.h"
#include "plugin_ids.h"

#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>

#include <render_fingerprint.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Steinberg::Vst::ParamID;
using Krate::DSP::Complex;
using Krate::DSP::FFT;
using Krate::DSP::TestUtils::compareFingerprints;
using Krate::DSP::TestUtils::fingerprintRender;

// =============================================================================
// Render geometry - shared by both criteria
// =============================================================================

constexpr Steinberg::int32 kBlock = 512;
constexpr std::size_t kBlockSamples = 512;

/// Both criteria render 4 s. The render is truncated to a WHOLE number of
/// blocks (blocks = floor(sr * 4 / 512)), so the three SC-019 arms differ in
/// length only by the sample rate and never by a ragged final slice:
///   44.1 kHz -> 344 blocks (176 128 samples, 3.9938 s)
///   48   kHz -> 375 blocks (192 000 samples, exactly 4 s)
///   96   kHz -> 750 blocks (384 000 samples, exactly 4 s)
/// Every metric is taken from the END of the render, so the missing 62 ms at
/// 44.1 kHz shifts the analysis window, it does not shorten it.
constexpr double kRenderSeconds = 4.0;

constexpr Steinberg::int16 kNote60 = 60;

/// Processor::mapNoteOnVelocity rounds `velocity * 127 + 0.5` with a floor of 1,
/// so this reaches the engine as EXACTLY MIDI velocity 100 - SC-020's pinned
/// operating point (macro_wiring_test.cpp:156-159 records the same derivation).
constexpr float kHostVelocity100 = 100.0f / 127.0f;

/// FR-024a maps kMasterGainId to `value * 2.0` linear, so 0.5 is EXACTLY unity
/// and the processor's master-gain multiply is a no-op.
constexpr double kMasterGainUnityNorm = 0.5;

/// FR-043's denormalisation is clamp(int(v * 15 + 1 + 0.5), 1, 16)
/// (global_params.h:99-101), so 7/15 lands on polyphony 8: 7 + 1.5 = 8.5 -> 8.
/// That is also the registered default; it is pinned so a future default change
/// fails HERE rather than silently re-scaling both criteria's operating point.
constexpr double kPolyphony8Norm = 7.0 / 15.0;

/// NON-VACUITY floor, not a level assertion. processor_audio_test.cpp:112-134
/// measured peak L 0.000858181 / peak R 0.000959373 for the 4 s note-60 render
/// at the registered defaults; this floor sits an order of magnitude below that,
/// so it rejects silence (and only silence) at every operating point here.
constexpr float kNonSilencePeakFloor = 5.0e-5f;

// =============================================================================
// The pinned detector
// =============================================================================

/// FFT::prepare validates only power-of-two (fft.h:151) - the documented
/// kMaxFFTSize of 8192 (fft.h:47) is a guidance constant, not an enforced bound,
/// and macro_wiring_test.cpp:194 already runs this same 65 536-point transform.
/// isPrepared() is asserted on every analysis so a future tightening of that
/// bound fails loudly instead of silently analysing a zero-size spectrum.
constexpr std::size_t kFftSize = 65536;

constexpr double kBandLoHz = 20.0;
constexpr double kBandHiHz = 16000.0;

/// SC-019's two gates, verbatim from spec.md:2329-2333.
constexpr double kRmsSpreadDb = 1.0;
constexpr double kCentroidSpreadFraction = 0.05;

// =============================================================================
// SC-020 clause 2 - THE MEASURED GATE
// =============================================================================
// The gate is `floor(min observed pairwise spread / 1.05)` over the sixteen
// entries of C-10's kSeedValues, rendered at SC-020's pinned operating point.
// The case renders all sixteen and PRINTS the full total-variation table plus
// the derived gate on every run (WARN, so it lands in the run log even on a pass
// - that table is what compliance.md records), then gates the measured minimum.
//
// FIRST MEASUREMENT, 2026-08-01, and what it cost.
//   The table shipped by T007 was an arbitrary set of sixteen hex constants. Its
//   measured minimum pairwise spread was 0.202 TV units (indices 7 and 9), so
//   `floor(0.202 / 1.05) = 0` - a gate with no teeth. Per C-10 (spec.md:2360-2364)
//   that is A DEFECT OF THE TABLE and its only remedy is re-picking the
//   constants; lowering the gate is not available, and neither is re-examining
//   the engine's seed derivation. It is also the EXPECTED outcome of an
//   arbitrary set: sixteen values scattered over an ~80-unit TV range have a
//   minimum gap of order 80/16^2 ~ 0.3 by construction, so this criterion can
//   only ever be met by a table whose members were CHOSEN to be separated.
//   dropdown_mappings.h:60-88 records the curation that followed (224-candidate
//   splitmix64 pool, each rendered through the shipped Processor at this exact
//   operating point, fifteen picked to maximise the minimum pairwise gap subject
//   to the fixed 1u anchor at index 0).
//
// IF A FUTURE RUN PRINTS A SMALL MINIMUM, THE REMEDY IS STILL C-10's AND ONLY
// C-10's: re-pick the offending constant in
// plugins/seraphis/src/parameters/dropdown_mappings.h:88-92 and re-measure.
//
// NOTE ON THE ARITHMETIC, so it is not mistaken for a bug: `floor(x / 1.05)`
// rounds DOWN TO A WHOLE UNIT of total variation. If the measured minimum spread
// is itself below 1.05, the derived gate is 0 and the criterion has no teeth -
// which is not a licence to ship a zero gate, it is C-10's "the spread is too
// small" signal in another form, and takes the same remedy.
//
// PORTABILITY OF THE GATE: total variation is one of render_fingerprint.h's four
// aggregate metrics, whose measured cross-toolchain spread is 1.9e-7 RELATIVE
// (render_fingerprint.h:22-29, g++ -O3 / g++ -O3 -ffast-math / clang++ -O2). At
// TV ~ 310 that is 6e-5 absolute, ~80 000x below the 4.899-unit margin this gate
// leaves, so it is not a Windows-only number.
// -----------------------------------------------------------------------------

/// Set to true in the SAME edit that replaces kSeedSpreadGate with the measured
/// value. While it is false the case fails, by design.
constexpr bool kSeedSpreadGateIsMeasured = true;

/// MEASURED - `floor(min observed pairwise spread / 1.05)`
/// = floor(4.899 / 1.05) = 4.
///
/// RE-VERIFIED T028 (2026-08-01), same build, same operating point: the case's
/// own report re-derived `min pairwise spread = 4.89874 (seeds 0 and 5)` and
/// `floor(min / 1.05) = 4`, reproducing the table below exactly. The pin stands
/// unchanged; T028 changed no value here. Note the gate is deliberately the
/// FLOOR and not the observation - a run that derives a spread below 4.89874 is
/// a defect of C-10's checked-in seed table (dropdown_mappings.h) and is fixed
/// by re-picking the offending constant, never by lowering this number.
constexpr double kSeedSpreadGate = 4.0;

// MEASURED SIXTEEN-SEED TABLE
//   windows-x64-release, MSVC 19.4x, 2026-08-01, at the operating point pinned
//   below (note 60 vel 100, held 3 s, 4 s total, 48 kHz, block 512, polyphony 8,
//   cloud drift depth 25 cents, body material Glass):
//
//   idx  seed         TV(L) + TV(R)
//   ---  -----------  -------------
//    0   0x00000001      251.213
//    1   0x51A8749B      229.917
//    2   0x2ACBD1F1      235.467
//    3   0x2E89A193      240.634
//    4   0x72403C09      246.283
//    5   0x3B343439      256.112
//    6   0x3B0E7D2F      261.038
//    7   0x46980CAD      266.080
//    8   0x6BA7EEE9      271.038
//    9   0xB43343A1      276.424
//   10   0xD4367D77      281.947
//   11   0x3B1E1B79      287.931
//   12   0x6C6AD50F      293.101
//   13   0xA6F2B569      298.292
//   14   0x724C81ED      303.371
//   15   0x743AAE49      309.280
//
//   min pairwise spread = 4.89874  (indices 0 and 5)
//   floor(4.89874 / 1.05) = 4      -> kSeedSpreadGate

/// Taken FROM C-10's table rather than written as a literal 16, so a change to
/// the table's size is a compile-time failure here (C-9 freezes the registered
/// entry count, so it should never move - but the arithmetic must not be the
/// place that silently absorbs it if it does).
constexpr std::size_t kNumSeeds = Seraphis::kSeedValues.size();
static_assert(kNumSeeds == 16u, "C-10 / C-9: kSeedId is exactly sixteen entries");

/// SC-020's pinned operating point: note held 3 s, 4 s total.
constexpr double kSeedNoteOffSeconds = 3.0;

/// SC-020 renders at 48 kHz only.
constexpr double kSeedSampleRate = 48000.0;

// =============================================================================
// Small numeric helpers
// =============================================================================

/// Finiteness on the EXPONENT BITS, never std::isnan/std::isfinite: this TU is
/// built WITH fast-math on the macOS leg and those calls fold away there.
/// 0x7F800000 is the all-ones exponent field shared by +/-inf and every NaN.
[[nodiscard]] bool isFiniteBits(float x) noexcept {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

[[nodiscard]] bool allFiniteBits(const std::vector<float>& v) noexcept {
    return std::ranges::all_of(v, [](float s) { return isFiniteBits(s); });
}

[[nodiscard]] float maxAbs(const std::vector<float>& v) noexcept {
    float peak = 0.0f;
    for (const float s : v) {
        peak = std::max(peak, std::abs(s));
    }
    return peak;
}

[[nodiscard]] double linToDb(double lin) {
    return 20.0 * std::log10(std::max(lin, 1.0e-30));
}

// =============================================================================
// Rendering through the shipped Processor
// =============================================================================

struct ParamPoint {
    ParamID id = 0;
    double normalized = 0.0;
};

struct Render {
    std::vector<float> left;
    std::vector<float> right;
    std::vector<float> mono;
};

/// One 4 s render of note 60 through the shipped Processor.
///
/// `block0` is delivered as parameter queues on block 0 only - the surface is
/// static for the whole render, which is what makes "the same parameter
/// settings" (SC-019) and "identical parameters including kSeedId" (SC-020
/// clause 1) mean the same thing at all three rates.
///
/// `noteOffSeconds < 0` holds the note for the whole render (SC-019); otherwise
/// a note-off is delivered on the block containing that instant (SC-020's 3 s).
[[nodiscard]] Render renderThroughProcessor(double sampleRate,
                                            const std::vector<ParamPoint>& block0,
                                            double noteOffSeconds) {
    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(sampleRate, kBlock) == Steinberg::kResultOk);

    const auto totalSamples = static_cast<std::size_t>(sampleRate * kRenderSeconds);
    const std::size_t blocks = totalSamples / kBlockSamples;
    REQUIRE(blocks > 0u);

    // `blocks` is never a legal block index, so this is "no note-off at all".
    const std::size_t noteOffBlock =
        (noteOffSeconds < 0.0)
            ? blocks
            : static_cast<std::size_t>(noteOffSeconds * sampleRate) / kBlockSamples;

    Render out;
    out.left.reserve(blocks * kBlockSamples);
    out.right.reserve(blocks * kBlockSamples);

    // ONE REQUIRE per render, not per block: a 750-block render would otherwise
    // contribute 750 assertions per arm to the Catch2 counter and dominate the
    // case's wall clock (macro_wiring_test.cpp:505-508 records the same choice).
    bool everyBlockOk = true;
    for (std::size_t b = 0; b < blocks; ++b) {
        if (b == 0) {
            for (const ParamPoint& p : block0) {
                fx.setParam(p.id, p.normalized);
            }
            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNote60, kHostVelocity100, 0);
        }
        if (b == noteOffBlock) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, kNote60, 0.0f, 0);
        }

        everyBlockOk = (fx.processBlock(kBlock) == Steinberg::kResultOk) && everyBlockOk;

        for (std::size_t i = 0; i < kBlockSamples; ++i) {
            out.left.push_back(fx.audioL()[i]);
            out.right.push_back(fx.audioR()[i]);
        }
    }
    REQUIRE(everyBlockOk);
    REQUIRE(fx.checkCanaries());

    out.mono.assign(out.left.size(), 0.0f);
    for (std::size_t i = 0; i < out.left.size(); ++i) {
        out.mono[i] = 0.5f * (out.left[i] + out.right[i]);
    }
    return out;
}

// =============================================================================
// The metrics
// =============================================================================

/// Time-domain RMS of the settled last second. Deliberately NOT taken from the
/// spectrum: the FFT window below is truncated at 96 kHz (see analyseSettled()),
/// and SC-019's RMS gate is over the last second at every rate.
[[nodiscard]] double rmsOfLastSecond(const std::vector<float>& mono, double sampleRate) {
    const std::size_t n = std::min(mono.size(), static_cast<std::size_t>(sampleRate));
    if (n == 0u) {
        return 0.0;
    }
    const std::size_t start = mono.size() - n;
    double sum = 0.0;
    for (std::size_t i = start; i < mono.size(); ++i) {
        const double v = static_cast<double>(mono[i]);
        sum += v * v;
    }
    return std::sqrt(sum / static_cast<double>(n));
}

struct BandMetrics {
    double centroidHz = 0.0;
    double flatness = 0.0;   ///< RECORDED, never gated (SC-019).
    std::size_t bins = 0u;
    bool valid = false;
};

/// The pinned analysis: the settled tail, 4-term Blackman-Harris, zero-padded
/// into the pinned 65 536-point transform, metrics over 20 Hz - 16 kHz.
///
/// THE WINDOW LENGTH IS min(one second, kFftSize), which is one second at 44.1
/// and 48 kHz (44 100 / 48 000 samples, zero-padded to 65 536) and 65 536
/// samples (0.6827 s) at 96 kHz. The truncation is forced by the pinned pair
/// "last second" + "65 536-point FFT" and is the same resolution macro_wiring_
/// test.cpp:552-558 settled on. It costs nothing here: the signal is stationary
/// over the tail by construction (the envelope is settled - see the shaping note
/// on rateProbeParams()), so a shorter window changes the variance of the
/// estimate, not its centre.
[[nodiscard]] BandMetrics analyseSettled(const std::vector<float>& mono, double sampleRate) {
    BandMetrics out;
    const auto oneSecond = static_cast<std::size_t>(sampleRate);
    const std::size_t len = std::min({mono.size(), oneSecond, kFftSize});
    if (len < std::size_t{1024}) {
        return out;
    }
    const std::size_t start = mono.size() - len;

    std::vector<float> window(len, 0.0f);
    Krate::DSP::Window::generateBlackmanHarris(window.data(), len);

    std::vector<float> frame(kFftSize, 0.0f);
    for (std::size_t i = 0; i < len; ++i) {
        frame[i] = mono[start + i] * window[i];
    }

    FFT fft;
    fft.prepare(kFftSize);
    REQUIRE(fft.isPrepared());

    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(frame.data(), spectrum.data());

    const double binHz = sampleRate / static_cast<double>(kFftSize);

    // Bin 0 is DC and is excluded at every rate, so the low edge is at least 1.
    auto loBin = static_cast<std::size_t>(std::ceil(kBandLoHz / binHz));
    loBin = std::max(loBin, std::size_t{1});
    auto hiBin = static_cast<std::size_t>(std::floor(kBandHiHz / binHz));
    hiBin = std::min(hiBin, spectrum.size() - std::size_t{1});
    if (hiBin <= loBin) {
        return out;
    }

    // A magnitude floor, not an epsilon: the geometric mean of a band containing
    // one exactly-zero bin is zero, which would report flatness 0 for any signal.
    constexpr double kMagFloor = 1.0e-20;

    double num = 0.0;
    double den = 0.0;
    double logSum = 0.0;
    std::size_t count = 0u;
    for (std::size_t b = loBin; b <= hiBin; ++b) {
        const double m = std::max(static_cast<double>(spectrum[b].magnitude()), kMagFloor);
        num += m * (static_cast<double>(b) * binHz);
        den += m;
        logSum += std::log(m);
        ++count;
    }

    out.centroidHz = (den > 0.0) ? (num / den) : 0.0;
    const double arithmeticMean = den / static_cast<double>(count);
    const double geometricMean = std::exp(logSum / static_cast<double>(count));
    out.flatness = (arithmeticMean > 0.0) ? (geometricMean / arithmeticMean) : 0.0;
    out.bins = count;
    out.valid = true;
    return out;
}

// =============================================================================
// SC-019's parameter table
// =============================================================================
// EVERY entry is a TIME-DOMAIN control - a seconds, a milliseconds, a Hz or a
// grains-per-second - because those are the only denormalizations that COULD
// read the sample rate, and SC-019 exists to show that none of them does. The
// values are stated NORMALIZED, which is the domain the VST3 boundary speaks;
// the plain value each one maps to is the pack's business and is deliberately
// not restated here (a restated constant is a second source of truth that drifts
// the moment a range moves).
//
// Two shaping choices, both so the LAST SECOND IS SETTLED, which the criterion
// requires and which nothing else in the table guarantees:
//   * IDs 702/703 (voice envelope stage times, log-mapped over [1, 10000] ms,
//     life_mod_params.h:56-58) sit well below their 2000 / 4000 ms defaults, so
//     the envelope reaches its sustain level inside the first half second
//     instead of still climbing at t = 4 s;
//   * ID 208 (cloud attack, log-mapped over [0.05, 30] s, harmonic_cloud.h:218-219)
//     stays near its floor for the same reason.
// Neither is a weakening: both are non-default values that a sample-rate-reading
// denormalization would still get wrong.
//
// A THIRD SHAPING CHOICE, ADDED 2026-08-01 AFTER THE FIRST MEASUREMENT - IDs
// 1002 / 1003, THE ATMOSPHERE'S GRAIN RATE AND GRAIN LENGTH.
//   The first version of this table set them to 0.50 / 0.25 normalized. Through
//   atmosphere_params.h's log maps over AtmosphereEngine::kMinDensity..kMaxDensity
//   = [0.1, 20] grains/s and kMinGrainSeconds..kMaxGrainSeconds = [0.05, 30] s
//   (atmosphere_engine.h:299-302) those are 1.41 grains/s of 0.247 s each, i.e.
//   a MEAN CONCURRENT GRAIN COUNT OF 0.35. At that operating point the granular
//   layer is a sparse Poisson click train, and the RMS of any one-second window
//   is decided by whether a grain happens to be alive in it. Measured, that put
//   SC-019's RMS spread at 1.130 dB against its 1.0 dB gate - and the failure
//   said nothing about the parameter surface. Decomposed on the same three
//   arms (2026-08-01):
//
//     full chain, 1.41 grains/s x 0.247 s   RMS spread 1.130 dB
//     ... with ID 1000 (atmosphere level) 0    RMS spread 0.336 dB
//     ... also body mix 0 (cloud only)         RMS spread 0.146 dB
//
//   so the whole of the excess was the grain layer's realisation, which is
//   rate-dependent BY DESIGN (Phase 5 records the 8.8 % rate-dependent spread in
//   RollingCaptureBuffer ring seconds and gates the atmosphere's own RMS at the
//   same 1.0 dB). Neither 1002 nor 1003 denormalizes through sampleRate -
//   atmosphere_params.h:132-143 is two pure log maps - so this was measurement
//   noise swamping the criterion, not the defect the criterion hunts.
//
//   The values below are 0.85 / 0.55 = 9.03 grains/s of 1.686 s, a mean
//   concurrent grain count of 15.2: a continuous texture whose one-second RMS is
//   an average over many grains rather than a coin flip. Measured RMS spread
//   0.258 dB, centroid spread 1.90 %, against gates of 1.0 dB and 5 %.
//
//   THIS IS NOT A WEAKENING, AND NO GATE MOVED. Both IDs stay non-default,
//   log-mapped, seconds-and-per-second controls; a denormalization that read
//   sampleRate would be wrong by 2.18x at 96 kHz and would move a DENSE layer's
//   level and centroid far more visibly than a sparse one's. The spec pins
//   SC-019's analysis and its two thresholds (spec.md:2315-2336) and leaves the
//   parameter values to this TU, which is the knob that was turned.

[[nodiscard]] std::vector<ParamPoint> rateProbeParams() {
    return {
        {.id = Seraphis::kMasterGainId, .normalized = kMasterGainUnityNorm},
        {.id = Seraphis::kPolyphonyId, .normalized = kPolyphony8Norm},

        // Harmonic cloud: per-partial envelope times + the drift depth.
        {.id = Seraphis::kCloudAttackId, .normalized = 0.05},
        {.id = Seraphis::kCloudDecayId, .normalized = 0.35},
        {.id = Seraphis::kCloudDriftDepthId, .normalized = 0.50},

        // Voice envelope: two stage times and the release.
        {.id = Seraphis::kEnvStage0MsId, .normalized = 0.50},
        {.id = Seraphis::kEnvStage1MsId, .normalized = 0.60},
        {.id = Seraphis::kEnvReleaseMsId, .normalized = 0.50},

        // Life modulators: the orbit rate, in Hz.
        {.id = Seraphis::kLifeSpatialRateId, .normalized = 0.40},

        // Spectral morph: the travel rate (journeys/s) and the spline waypoint
        // interval (seconds).
        {.id = Seraphis::kMorphTravelRateId, .normalized = 0.35},
        {.id = Seraphis::kMorphWaypointIntervalId, .normalized = 0.40},

        // Continuous body: the parallel decay cloud's decay time, in seconds.
        {.id = Seraphis::kBodyCloudDecayId, .normalized = 0.30},

        // Atmosphere: grains/s and grain length in seconds - the two controls
        // that turn into sample counts inside the grain scheduler. 9.03 grains/s
        // of 1.686 s each = 15.2 concurrent (see the third shaping note above).
        {.id = Seraphis::kAtmosDensityId, .normalized = 0.85},
        {.id = Seraphis::kAtmosGrainSecondsId, .normalized = 0.55},

        // Aether: RT60 in seconds and the pre-delay in milliseconds.
        {.id = Seraphis::kAetherDecayId, .normalized = 0.30},
        {.id = Seraphis::kAetherPreDelayId, .normalized = 0.50},
    };
}

// =============================================================================
// SC-020's parameter table
// =============================================================================
// The operating point is PINNED by spec.md:2339-2347: registered defaults except
// kCloudDriftDepthId (205) at 25 cents and kBodyMaterialId (800) at Glass, plus
// the polyphony-8 and unity-gain pins above.
//
// BOTH DEVIATIONS ARE REQUIRED, NOT COSMETIC:
//   * cloud drift depth defaults to 0.0 cents (seraphis_voice.h:295), so at the
//     registered defaults most of the seed's influence is switched off before it
//     is asserted to be audible. cloud_params.h:54-56 ranges the control over
//     [0, HarmonicCloud::kMaxDriftCents = 50] cents LINEARLY, so 0.5 is exactly
//     25 cents;
//   * ContinuousBody::setSeed drives "exactly one thing - the per-voice modal
//     micro-detune ... on the three MODAL materials only", with Strings and
//     Chamber documented seed-independent (continuous_body.h:1323-1348). Glass is
//     modal and is index 0 of kBodyMaterialLabels (dropdown_mappings.h:170-172),
//     which is also the registered default - it is pushed EXPLICITLY so that a
//     future default change fails here rather than silently moving the criterion
//     onto a seed-independent material.

[[nodiscard]] std::vector<ParamPoint> seedProbeParams(int seedIndex) {
    // handleGlobalParamChange denormalizes kSeedId as clamp(int(v*15 + 0.5), 0, 15)
    // (global_params.h:111-113), so index/15 selects the index exactly.
    const double seedNorm = static_cast<double>(seedIndex) / 15.0;
    return {
        {.id = Seraphis::kMasterGainId, .normalized = kMasterGainUnityNorm},
        {.id = Seraphis::kPolyphonyId, .normalized = kPolyphony8Norm},
        {.id = Seraphis::kSeedId, .normalized = seedNorm},
        {.id = Seraphis::kCloudDriftDepthId, .normalized = 0.50},  // 25 cents
        {.id = Seraphis::kBodyMaterialId, .normalized = 0.0},      // Glass
    };
}

/// SC-020 clause 2's statistic: total variation over BOTH channels.
///
/// Total variation is render_fingerprint.h's sharp metric - it tracks waveform
/// shape, so a different stochastic realization moves it even when the RMS lands
/// in the same place (render_fingerprint.h:14-18). The two channels are SUMMED
/// rather than mixed to mono because a mono mix can cancel a purely
/// decorrelating seed difference, which is precisely the difference the
/// per-voice azimuth and the per-grain pan spread produce.
[[nodiscard]] double totalVariationOf(const Render& r) {
    return fingerprintRender(r.left).totalVariation + fingerprintRender(r.right).totalVariation;
}

}  // namespace

// =============================================================================
// SC-019
// =============================================================================

// NOTE ON TAGS: neither case carries a leading-dot tag. A Catch2 tag beginning
// with `.` HIDES the case from the default run, and both of these are success
// criteria - a hidden criterion is an unverified one. They are affordable: 3 + 18
// renders of 4 s each with a single sounding voice, against the 105 four-second
// renders macro_wiring_test.cpp's (hidden) macro sweeps perform.
TEST_CASE("Seraphis_ParameterSurface_IsSampleRateIndependent", "[seraphis][phase9][sc019]") {
    constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};

    std::array<double, 3> rmsDb{};
    std::array<double, 3> centroidHz{};
    std::array<double, 3> flatness{};

    const std::vector<ParamPoint> params = rateProbeParams();

    std::ostringstream table;
    table << "\nSC-019 - sample-rate independence of the parameter surface\n"
          << "  settled tail, 65536-pt Blackman-Harris, metrics over "
          << kBandLoHz << " Hz - " << kBandHiHz << " Hz\n"
          << "  rate(Hz)      RMS(dBFS)   centroid(Hz)   flatness(recorded)\n";

    for (std::size_t r = 0; r < kRates.size(); ++r) {
        const double sr = kRates[r];
        const Render render = renderThroughProcessor(sr, params, /*noteOffSeconds=*/-1.0);

        REQUIRE(allFiniteBits(render.left));
        REQUIRE(allFiniteBits(render.right));
        REQUIRE(maxAbs(render.mono) > kNonSilencePeakFloor);

        const BandMetrics m = analyseSettled(render.mono, sr);
        REQUIRE(m.valid);
        REQUIRE(m.bins > 0u);

        rmsDb[r] = linToDb(rmsOfLastSecond(render.mono, sr));
        centroidHz[r] = m.centroidHz;
        flatness[r] = m.flatness;

        table << "  " << sr << "    " << rmsDb[r] << "    " << centroidHz[r] << "    "
              << flatness[r] << "\n";
    }

    // Recorded on every run - the flatness column in particular exists ONLY as a
    // record (spec.md:2331-2333) and must never grow a gate.
    WARN(table.str());

    // --- gate 1: output RMS within 1.0 dB across the three rates --------------
    const double rmsMin = *std::min_element(rmsDb.begin(), rmsDb.end());
    const double rmsMax = *std::max_element(rmsDb.begin(), rmsDb.end());
    INFO("RMS spread " << (rmsMax - rmsMin) << " dB over " << rmsMin << " .. " << rmsMax
                       << " dBFS; gate " << kRmsSpreadDb << " dB");
    REQUIRE((rmsMax - rmsMin) <= kRmsSpreadDb);

    // --- gate 2: band-limited spectral centroid within 5 % --------------------
    const double cMin = *std::min_element(centroidHz.begin(), centroidHz.end());
    const double cMax = *std::max_element(centroidHz.begin(), centroidHz.end());
    REQUIRE(cMin > 0.0);
    INFO("centroid spread " << ((cMax - cMin) / cMin) << " over " << cMin << " .. " << cMax
                            << " Hz; gate " << kCentroidSpreadFraction);
    REQUIRE(((cMax - cMin) / cMin) <= kCentroidSpreadFraction);
}

// =============================================================================
// SC-020
// =============================================================================

TEST_CASE("Seraphis_Seed_IsDeterministicAndDistinct", "[seraphis][phase9][sc020]") {
    SECTION("clause 3 - kSeedValues[0] is pinned to the Phase 8 seed") {
        // SC-002's negative control depends on this: at the registered default
        // index 0, kSeedId must seed engine and reverb exactly as Phase 8's
        // kEngineSeed / kReverbSeed do (seraphis_engine_config.h:28-29).
        static_assert(Seraphis::kSeedValues[0] == 1u,
                      "SC-020 cl.3 / C-10: index 0 is pinned to 1u");
        REQUIRE(Seraphis::kSeedValues[0] == 1u);

        // A table property, checked where the table is used: sixteen DISTINCT
        // constants. Two equal entries would make clause 2's minimum spread 0 by
        // construction, and the failure would read as a DSP defect instead of the
        // table defect it is.
        REQUIRE(kNumSeeds == 16u);
        bool allDistinct = true;
        for (std::size_t i = 0; i < kNumSeeds; ++i) {
            for (std::size_t j = i + 1u; j < kNumSeeds; ++j) {
                allDistinct = allDistinct && (Seraphis::kSeedValues[i] != Seraphis::kSeedValues[j]);
            }
        }
        REQUIRE(allDistinct);
    }

    SECTION("clause 1 - two instances with identical parameters render identically") {
        const std::vector<ParamPoint> params = seedProbeParams(/*seedIndex=*/0);

        const Render a =
            renderThroughProcessor(kSeedSampleRate, params, kSeedNoteOffSeconds);
        const Render b =
            renderThroughProcessor(kSeedSampleRate, params, kSeedNoteOffSeconds);

        REQUIRE(allFiniteBits(a.left));
        REQUIRE(allFiniteBits(a.right));
        REQUIRE(a.left.size() == b.left.size());

        // Without this the clause is satisfied by two silences.
        REQUIRE(maxAbs(a.left) > kNonSilencePeakFloor);
        REQUIRE(maxAbs(a.right) > kNonSilencePeakFloor);

        // render_fingerprint.h's MEASURED tolerances, never a bit-exact digest:
        // MSVC / GCC / Apple-Clang differ in the last bits of every
        // transcendental and macOS builds with -ffast-math
        // (render_fingerprint.h:1-31).
        const auto cmpL = compareFingerprints(fingerprintRender(a.left), fingerprintRender(b.left));
        INFO("L: " << cmpL.detail << " worstMetricRel=" << cmpL.worstMetricRelativeError
                   << " worstSample=" << cmpL.worstSampleError);
        REQUIRE(cmpL.withinTolerance());

        const auto cmpR =
            compareFingerprints(fingerprintRender(a.right), fingerprintRender(b.right));
        INFO("R: " << cmpR.detail << " worstMetricRel=" << cmpR.worstMetricRelativeError
                   << " worstSample=" << cmpR.worstSampleError);
        REQUIRE(cmpR.withinTolerance());
    }

    SECTION("clause 2 - the sixteen seeds are separated, and the gate is measured") {
        // Sized FROM the table, so "all 16 entries of kSeedValues" cannot drift
        // into "the first 16 of however many there are".
        std::array<double, kNumSeeds> tv{};

        for (std::size_t s = 0; s < tv.size(); ++s) {
            const Render r = renderThroughProcessor(
                kSeedSampleRate, seedProbeParams(static_cast<int>(s)), kSeedNoteOffSeconds);
            REQUIRE(allFiniteBits(r.left));
            REQUIRE(allFiniteBits(r.right));
            REQUIRE(maxAbs(r.left) > kNonSilencePeakFloor);
            tv[s] = totalVariationOf(r);
        }

        double minSpread = std::numeric_limits<double>::max();
        std::size_t minI = 0u;
        std::size_t minJ = 1u;
        for (std::size_t i = 0; i < tv.size(); ++i) {
            for (std::size_t j = i + 1u; j < tv.size(); ++j) {
                const double spread = std::abs(tv[i] - tv[j]);
                if (spread < minSpread) {
                    minSpread = spread;
                    minI = i;
                    minJ = j;
                }
            }
        }

        const double derivedGate = std::floor(minSpread / 1.05);

        std::ostringstream table;
        table << "\nSC-020 cl.2 - sixteen-seed total-variation table\n"
              << "  (note 60 vel 100, held 3 s, 4 s total, 48 kHz, block 512, poly 8,\n"
              << "   drift depth 25 cents, material Glass)\n"
              << "  idx  seed        TV(L)+TV(R)\n";
        for (std::size_t s = 0; s < tv.size(); ++s) {
            table << "  " << s << "    " << Seraphis::kSeedValues[s] << "    " << tv[s] << "\n";
        }
        table << "  min pairwise spread = " << minSpread << "  (seeds " << minI << " and " << minJ
              << ")\n"
              << "  floor(min / 1.05)   = " << derivedGate << "   (shipped kSeedSpreadGate = "
              << kSeedSpreadGate
              << "; a LOWER derived value means C-10's table needs re-picking - "
                 "see the block at the top of this file)\n";
        WARN(table.str());

        // The gate is only a gate once it comes from the table above. While
        // kSeedSpreadGateIsMeasured is false this case fails ON PURPOSE - see the
        // block at the top of this file for the one-line close-out, and for why a
        // small spread is fixed in dropdown_mappings.h and never here.
        const bool gateIsMeasured = kSeedSpreadGateIsMeasured;
        INFO("kSeedSpreadGate is still the PENDING placeholder; the measured table "
             "printed above supplies floor(min / 1.05) = "
             << derivedGate);
        REQUIRE(gateIsMeasured);

        INFO("min pairwise spread " << minSpread << " between seeds " << minI << " and " << minJ
                                    << "; gate " << kSeedSpreadGate);
        REQUIRE(minSpread > kSeedSpreadGate);
    }
}
