// ==============================================================================
// Layer 3: System Tests - AtmosphereEngine, spectral / artifact TU
//                                        (specs/seraphis-phase5-atmosphere)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase5-atmosphere/spec.md
//            specs/seraphis-phase5-atmosphere/plan.md   (S15.3, S15.5)
//            specs/seraphis-phase5-atmosphere/tasks.md  (T002 creates this TU;
//                                                        T009 lands SC-003;
//                                                        T013 lands SC-005..007)
//
// SCOPE OF THIS TU (plan.md S15's TU-ownership table):
//   SC-003  AtmosphereEngine_NoGrainBoundaryClicks   (S15.3)   <- T009, here
//   SC-005  AtmosphereEngine_BlurMonotonicity        (S15.5)   <- T013, here
//   SC-006  AtmosphereEngine_BlurTransparentAtZero   (S15.5)   <- T012, here
//   SC-007  AtmosphereEngine_FreezeStability         (S15.5)   <- T014, here
//
// PINNED INPUTS LIVE HERE. Both metrics used by this TU are RELATIVE, so an
//   unpinned input makes the criterion unreproducible. Two file-local
//   generators, defined once and shared between the cases that plan S15 names:
//     - makeHarmonicStack() : 220 Hz fundamental, partials 2x..9x at 1/n
//                             amplitude, all sine, zero phase, band-limited
//                             below 2 kHz, scaled to peak 0.5.  (SC-003 ONLY)
//                             DEFINED BELOW by T009.
//     - makeBroadbandStack(): 1 kHz fundamental, partials 1x..16x at 1/n
//                             amplitude, sine, zero phase, peak 0.5 - SC-003's
//                             construction rule spanning the whole analysed
//                             band.                        (SC-005 flatness/rho)
//                             DEFINED BELOW, in SC-005's own namespace block.
//                             READ SECTION (I) OF THIS BANNER BEFORE TOUCHING
//                             IT: it resolves a contradiction between two
//                             binding halves of spec.md SC-005.
//     - makeImpulseTrain()  : x[n] = 1.0f on both channels at every multiple of
//                             24 000 samples (one impulse per 0.5 s), 0
//                             elsewhere.                          (SC-005 crest)
//                             DEFINED BELOW by T013, in SC-005's own namespace
//                             block rather than at the top of the file: it is
//                             the crest clause's input and nothing else uses it.
//
// METRIC NOTES THAT ARE BINDING, NOT ADVISORY:
//   - ClickDetector config is stated VERBATIM (plan S15.3): frameSize 512,
//     hopSize 256, energyThresholdDb -60.0f, mergeGap 5. detectionThreshold is
//     the ONE field SC-003 authorises moving, and it HAS been moved off 5.0f -
//     see the measured false-positive-floor section below for the numbers and
//     the reason. Never relax the 0-detection requirement on the engine render.
//   - calculateSpectralFlatness must be called FULLY QUALIFIED as
//     Krate::DSP::TestUtils::SignalMetrics::calculateSpectralFlatness - a
//     same-named 2-argument overload exists at
//     dsp/include/krate/dsp/primitives/spectral_utils.h:335 and an unqualified
//     call can bind the wrong one.
//   - Flatness windows start at settleSamples and are EXACTLY 8192 long, so the
//     helper selects fftSize = 4096. A naive call on the first 4096 samples
//     reads guaranteed-silence and the criterion would pass on a silent engine.
//
// ------------------------------------------------------------------------------
// SC-003 REFERENCE-RENDER FALSE-POSITIVE FLOOR - MEASURED, NOT ASSUMED
// (spec.md SC-003's closing clause, plan S15.3)
// ------------------------------------------------------------------------------
//   Detector sigma in force: kClickThresholdSigma = 12.5f - the MEASURED
//   grid-wide zero point, with nothing added to it. The rest of the config is
//   the verbatim one; detectionThreshold is the ONE field SC-003 authorises
//   raising, precisely for this, and it authorises exactly "the smallest value
//   that gives 0 detections on the reference render".
//
//   WHY 5.0f IS NOT USABLE HERE, MEASURED. ClickDetector flags any |dy| above
//   mean + sigma*stddev of |dy| WITHIN each 512-sample frame
//   (artifact_detection.h:186-193, :209-218). That statistic is a poor fit for
//   granular output for a reason that has nothing to do with clicks: a frame
//   spanning a grain fade-in, or straddling silence between sparse grains, has
//   a |dy| distribution so skewed that the frame's own maximum clears
//   mean + 5*stddev. At sigma 5 the floor is between 47 and 9 146 detections in
//   EVERY one of the 30 cells, reference and engine render alike - including
//   Hann and Blackman, which close at exactly 0 and whose flagged samples are,
//   on inspection, the steepest point of a smooth sinusoidal cycle with no
//   discontinuity anywhere near them. The pinned input ITSELF measures 0
//   detections at sigma 5, so the floor is created by the granulation, not by
//   the source.
//
//   MEASURED ZERO POINTS, per cell, on the reference AND the engine render
//   (48 kHz, this file's pinned input, seed and densities, both channels):
//     grainSeconds 0.05 : 11.5 for Hann/Trapezoid/Sine/Blackman/Linear,
//                         12.5 for Exponential   <- grid-wide worst
//     grainSeconds 0.2  : 6.5 .. 7.5
//     grainSeconds 1    : 7.0
//     grainSeconds 5    : 7.0 .. 7.5
//     grainSeconds 30   : 6.0 .. 6.5
//   The tail is thin: at sigma 11.0 the worst cells sit at 1-4 detections, and
//   every cell reads 0 from sigma 12.5 upward.
//
//   THE CONSTANT IS 12.5f - THE MEASURED WORST CELL, WITH NOTHING ADDED.
//   An earlier revision carried 14.0f = 12.5 + a 1.5 cross-toolchain margin, on
//   the (real) grounds that MSVC, GCC and Apple Clang do not produce
//   bit-identical renders and that the rung below 12.5 reads 1-4 detections. It
//   was removed anyway: SC-003 authorises RAISING detectionThreshold to "the
//   smallest value that gives 0 detections on the reference render" and to no
//   more than that, and every sigma of padding is a sigma of weakening applied
//   to the ENGINE render, which is the thing this criterion tests. The
//   cross-toolchain risk is carried instead by the REFERENCE GATE, which is
//   what it is for: the reference floor is ASSERTED to be 0 per cell before the
//   engine render is judged, so a toolchain that moves the floor fails LOUDLY,
//   naming the smallest ladder sigma that would clear it, rather than silently
//   consuming margin that was meant for the engine. If that gate fires, raise
//   this constant to the sigma the message names - not to that plus a margin -
//   and update the table above. NEVER relax the 0-detection requirement on the
//   ENGINE render, and never delete the reference gate.
//
//   AN ENGINE DEFECT WAS FOUND BY THIS PROCEDURE, NOT PAPERED OVER BY IT.
//   Before FR-027's edge ramp (atmosphere_engine.h, kEnvelopeEdgeFadeEntries)
//   the Exponential cell at grainSeconds 0.05 needed sigma 21.0 where the five
//   naturally-closing envelopes needed 11.5: at that lifetime the table stride
//   is 1.707 entries per sample, so the render stepped OVER the forced-zero
//   entries and every grain opened on a 1.658 % and closed on a 1.334 % step of
//   its own amplitude. The fix went into the engine; only the residual
//   material-driven floor above is calibrated for here.
//
// SC-003 CONFIGURATION DECISIONS THAT PLAN S15.3 LEAVES OPEN (recorded so a
// later reader does not read them as drift):
//   - blurEnabled = false, freezeEnabled = false. SC-003 measures GRAIN
//     BOUNDARY discontinuities; the blur and freeze legs are SC-005/006/007's
//     business, and routing the grain sum through an STFT <-> OverlapAdd
//     round-trip would fold a COLA-reconstruction question into a criterion
//     that is not about it (and shift every sample by the blur latency, RA-3).
//   - captureSeconds = 30 (the maximum). The 30 s cell must exercise a REAL 30 s
//     grain, so the ring is sized such that FR-025's truncation cannot bind for
//     any pitch-spread draw. It is also the maximally cache-hostile ring, which
//     is the honest worst case for a read-path defect.
//   - The reference render is PER CELL and differs from the cell in EXACTLY ONE
//     respect: silence()/reset() are not exercised. Same input, same seed, same
//     grainSeconds, same envelope, SAME DENSITY - spec.md SC-003 and plan.md
//     line 2147 both word it "a reference render of the same input and seed
//     **with pool saturation** and `silence()` **not** exercised": the "not"
//     negates the silence() clause, and the pool saturation is a property the
//     reference SHARES.
//
//     THIS IS LOAD-BEARING FOR THE RATIO BOUND, AND WAS MEASURED THE WRONG WAY
//     ROUND ONCE. An earlier revision read the sentence as negating both and
//     lowered the reference density to `kMaxGrains / 2 / grainSeconds`. That
//     makes the two renders differ in GRAIN COUNT, so `max |dy|` compares the
//     FR-028 1/sqrt(n) sum statistics instead of the transitions: measured, the
//     grainSeconds = 30 Hann cell (64 live grains) came out at 1.577x its
//     33-grain reference and failed a bound that had nothing to say about
//     clicks. With the density matched, the two renders are identical up to the
//     40 s silence() call, so the ratio asks precisely the question the bound
//     exists for - does the latch, the latched span or the post-reset cold-ring
//     refill produce a bigger first difference than the steady state did?
//     Measured across all 30 cells: ratio <= 1.0000, and the post-reset span's
//     max |dy| is below the pre-silence max in every cell.
//
// ------------------------------------------------------------------------------
// SC-005 - THE THREE FLOORS ARE MINIMUMS, AND THEY ARE NOT YET MEASURED (O-2)
// (spec.md SC-005; tasks.md T013's closing clause)
// ------------------------------------------------------------------------------
//   spec.md states three floors, all as MINIMUMS:
//     (a) averaged spectral flatness at blur = 1.0 is at least 1.25x the value
//         at blur = 0.0;
//     (b) normalised inter-channel correlation rho falls by at least 0.20 from
//         blur = 0.0 to blur = 1.0;
//     (c) impulse-train crest factor at blur = 1.0 is at least 3 dB below the
//         value at blur = 0.0.
//   O-2 requires each to be REPLACED by the MEASURED value less a stated
//   margin, and thereafter moved only UPWARD.
//
//   STATUS: CALIBRATED. Measured on Windows/MSVC Release, 2026-07, against the
//   delivered blur stage. All three floors are now ABOVE their spec minimum.
//
//   | Floor                            | Spec min | Measured | Margin | In force |
//   |----------------------------------|----------|----------|--------|----------|
//   | flatness(1.0) / flatness(0.0)    | 1.25x    | 1.948x   | -10 %  | 1.75x    |
//   | rho(0.0) - rho(1.0)              | 0.20     | 1.0385   | -10 %  | 0.93     |
//   | crest(0.0) - crest(1.0), dB      | 3.0 dB   | 14.51 dB | -10 %  | 13.0 dB  |
//
//   Per-step measured values behind the table (averaged over the four windows,
//   both channels):
//     flatness: 0.070889, 0.101696, 0.120933, 0.133247, 0.138113  (strictly
//               increasing; smallest step +3.7 %, against a 2 % epsilon)
//     rho:      1.000000, 0.937250, 0.693820, 0.199200, -0.038479 (strictly
//               decreasing)
//     crest dB: blur 0 -> L 38.522, R 38.522;  blur 1 -> L 23.616, R 24.016
//               (drops 14.906 dB and 14.506 dB; the SMALLER is the calibration
//               input, because the clause is asserted per channel)
//
//   CALIBRATION PROCEDURE (mechanical, unchanged). The case emits every input
//   to the table as an INFO line - the five per-step averaged flatness values,
//   the five rho values, and both crest factors per channel:
//     1. run `dsp_systems_tests.exe "AtmosphereEngine_BlurMonotonicity*"
//        --success` and read the three measured quantities;
//     2. set kFlatnessRatioFloor / kCorrelationDropFloor / kCrestDropFloorDb to
//        the measured value less 10 % of it - the same cross-toolchain
//        allowance the SC-003 sigma above carries, for the same reason (MSVC,
//        GCC and Apple Clang do not produce bit-identical renders, and the
//        macOS leg builds with -ffast-math);
//     3. fill the Measured and Margin columns above.
//   NEVER lower a constant below its spec minimum - that is a spec change, not
//   a tolerance adjustment. If a measured value comes out BELOW its spec
//   minimum, the blur stage is the defect, not the threshold.
//
// ------------------------------------------------------------------------------
// SC-005 DEVIATES FROM spec.md ON TWO CONFIGURATION POINTS. BOTH ARE RECORDED
// HERE LOUDLY, WITH THE MEASUREMENTS THAT FORCED THEM, BECAUSE THE FIRST IS A
// SPEC CONTRADICTION AND NOT A TOLERANCE CHOICE.
// ------------------------------------------------------------------------------
//
// (I) THE FLATNESS/rho SWEEP DOES NOT USE SC-003's PINNED HARMONIC STACK.
//     It uses makeBroadbandStack() - 1 kHz fundamental, partials 1x..16x at 1/n,
//     sine, zero phase, peak 0.5 - defined next to makeImpulseTrain() below.
//
//     spec.md SC-005 pins the input as "the 220 Hz harmonic stack described in
//     SC-003, peak 0.5" AND pins the metric as the 3-argument
//     calculateSpectralFlatness on the raw render. THOSE TWO HALVES CONTRADICT
//     EACH OTHER, and the contradiction is a property of the criterion, not of
//     the engine. SC-003's stack is band-limited below 2 kHz by construction
//     (partials 2x..9x of 220 Hz, top partial 1980 Hz), while the helper
//     analyses the WHOLE 0..24 kHz band with fftSize = 4096. Roughly 92 % of the
//     2048 analysed bins therefore contain no signal at any blur value, and the
//     geometric mean - which those near-empty bins dominate - tracks the overall
//     output level rather than the smearing. Blur legitimately LOWERS the level
//     (random phase makes the four overlapping OLA frames sum incoherently
//     instead of coherently, ~5 dB at 75 % overlap), and it lowers the empty
//     high band slightly more than the occupied low band, so the full-band
//     flatness of a CORRECT implementation FALLS.
//
//     MEASURED, on the delivered engine with SC-003's stack (4096-point Hann
//     analysis of the first settled window, left channel):
//       blur 0.0 -> full-band flatness 0.0013799, bins[1,256) (11.7 Hz..3 kHz,
//                   i.e. the occupied band) flatness 0.038668
//       blur 0.5 -> full-band 0.0012350, occupied-band 0.101292
//       blur 1.0 -> full-band 0.0011440, occupied-band 0.112112
//     The blur stage smears the band it is given by 2.9x. The spec's own stated
//     mechanism ("inter-frame phase decoherence widens spectral lines into
//     skirts and is visible") is demonstrably happening; the full-band statistic
//     simply cannot see it, and reports the level ratio between an empty high
//     band and the occupied low band instead. Averaged over the four windows the
//     criterion read 0.0014931, 0.0014966, 0.0014256, 0.0013637, 0.0013223 -
//     failing the non-decreasing clause at step 1 -> 2 and the 1.25x ratio.
//
//     The spec ALREADY accepts that a clause may need its own pinned input when
//     the metric demands one: the crest clause carries makeImpulseTrain() for
//     exactly that reason ("the metric is relative and an unpinned input makes
//     the criterion unreproducible" - the rationale is REPRODUCIBILITY, which
//     any pinned generator satisfies). makeBroadbandStack applies that same
//     precedent to the flatness clause. It keeps SC-003's construction rule
//     verbatim - a harmonic series at 1/n amplitude, sine, zero phase, peak 0.5
//     - and moves only the fundamental and the partial count, so the series
//     spans the band the metric actually analyses. The top partial is 16 kHz and
//     not higher precisely so the engine's own pitch-shift range (pitchSpread
//     plus a 2-semitone drift range) cannot push it past Nyquist and fold
//     aliasing into the measurement.
//
//     Nothing was relaxed to make this pass: with the broadband input every
//     floor moved UP (1.25x -> 1.75x, 0.20 -> 0.93, 3.0 dB -> 13.0 dB) and the
//     flatness sweep became strictly increasing rather than merely
//     non-decreasing. makeHarmonicStack is untouched and still owns SC-003.
//
// (II) THE SWEEP RENDERS WITH panSpread = 0 AND decorrelation = 0.
//     An earlier revision pinned 0.5 and 0.3. Those two controls decorrelate the
//     two channels BEFORE blur is applied, and measured, they left rho(0.0) at
//     only 0.0828 - so the FR-042 clause had 0.08 of headroom against a floor of
//     0.20 and could not be satisfied by any implementation. Worse, it could not
//     DISCRIMINATE: the defect the clause exists to catch (one draw applied to
//     both channels) would also have produced rho(1.0) ~ 0.08, i.e. the same
//     answer as the correct implementation. With both controls at 0 the dry
//     signal is perfectly correlated, so rho(0.0) = 1.000 exactly and the
//     correct implementation drives it to -0.038 while a shared-draw
//     implementation would leave it at 1.000. The clause is strictly more
//     discriminating this way. spec.md pins neither control for SC-005.
//
// NO BIT-EXACT FLOAT GOLDENS: aggregate/spectral metrics at measured
//   tolerances only (node tools/lint-float-bit-goldens.js gates this).
//
// ALLOCATION DETECTION: this TU must NOT include
//   <allocation_operator_overrides.h> - the single owner in dsp_systems_tests is
//   dsp/tests/unit/systems/selectable_oscillator_test.cpp:388, and a second
//   include is a duplicate-symbol link error. <allocation_detector.h> only.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include "artifact_detection.h"

#include <signal_metrics.h>

#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/systems/atmosphere_engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// =============================================================================
// Pinned render geometry (SC-003, plan S15.3)
// =============================================================================

constexpr double kSampleRate = 48000.0;

/// 100 blocks per second, so EVERY event in plan S15.3's schedule (silence() at
/// 40 s, reset() at 50 s, end at 60 s) lands exactly on a block boundary. With
/// 512 the 50 s point falls mid-block and the schedule stops being pinned.
constexpr std::size_t kBlockSamples = 480;

constexpr std::size_t kRenderSamples = 60u * 48000u;  // 2 880 000
constexpr std::size_t kSilenceSample = 40u * 48000u;  // silence() here
constexpr std::size_t kResetSample = 50u * 48000u;    // reset() here

/// AtmosphereEngine::kSilenceRampMs (10 ms) at 48 kHz. The engine subtracts
/// 1/(kSilenceRampMs * 1e-3 * sr) per sample, so the gain reaches 0 - and the
/// latch fires - within this many samples of the silence() call.
constexpr std::size_t kSilenceRampSamples = 480;
/// Twice the ramp: the exact-zero span is asserted from here, so a one-sample
/// disagreement about where the ramp ends is not what the clause measures.
constexpr std::size_t kLatchSettleSamples = 2u * kSilenceRampSamples;

/// Tail window for the post-reset non-silence clause: the last 2 s.
constexpr std::size_t kTailWindowSamples = 2u * 48000u;

constexpr std::uint32_t kSeed = 0x5E2A0005u;
constexpr float kCaptureSeconds = 30.0f;
constexpr float kCellDensity = 20.0f;  // FR-009's maximum, per plan S15.3

/// The CALIBRATED detector sigma: the MEASURED grid-wide zero point, and
/// nothing added to it.
///
/// SC-003 authorises exactly one value - "the smallest value that gives 0
/// detections on the reference render" - and the per-cell measurement in the
/// file banner puts that at 12.5. An earlier revision carried 14.0 (12.5 plus a
/// 1.5 cross-toolchain margin); the margin was removed because it is not what
/// the criterion authorises, and a padded sigma is a weaker gate on the ENGINE
/// render, which is the thing under test. Read the measured
/// false-positive-floor section in the file banner in full before changing it -
/// the number is derived from a per-cell measurement, not chosen.
constexpr float kClickThresholdSigma = 12.5f;

/// The secondary bound is a RATIO, never an absolute: an absolute |dy| bound
/// would constrain input bandwidth and level rather than the engine (a 5 kHz
/// sine at 0.5 already has per-sample deltas of 0.33 at 48 kHz).
constexpr double kMaxDeltaRatio = 1.5;

// =============================================================================
// The pinned input (plan S15.3; shared with SC-005 / T013)
// =============================================================================

/// @brief 220 Hz fundamental plus partials 2x..9x at 1/n amplitude, all sine,
///        zero phase, scaled to peak 0.5. Both channels carry the same signal;
///        every stereo difference in the output therefore comes from the
///        engine's per-grain pan and decorrelation, not from the source.
///
/// Band-limited below 2 kHz by construction: the top partial is 9 * 220 =
/// 1980 Hz. No such generator exists in tests/test_helpers/test_signals.h,
/// which offers sine / noise / sweep / square / saw only.
///
/// Deliberately far from Gaussian - it is a truncated sawtooth series, so its
/// first difference is dominated by one large jump per 220 Hz period and the
/// mean + 5*sigma within-frame statistic ClickDetector uses stays usable (see
/// the file banner). The phase argument is accumulated in DOUBLE from the
/// sample index, never by summing a per-sample increment, so the 60 s render
/// carries no phase drift.
void makeHarmonicStack(float* outLeft, float* outRight, std::size_t numSamples,
                       double sampleRate) {
    if (outLeft == nullptr || outRight == nullptr || numSamples == 0 || sampleRate <= 0.0) {
        return;
    }

    constexpr int kNumPartials = 9;
    constexpr double kFundamentalHz = 220.0;
    constexpr double kTwoPiD = 6.283185307179586476925286766559;

    double peak = 0.0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        double sum = 0.0;
        for (int n = 1; n <= kNumPartials; ++n) {
            const double nd = static_cast<double>(n);
            sum += std::sin(kTwoPiD * kFundamentalHz * nd * t) / nd;
        }
        outLeft[i] = static_cast<float>(sum);
        peak = std::fmax(peak, std::fabs(sum));
    }

    const double scale = (peak > 0.0) ? (0.5 / peak) : 0.0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        const auto scaled = static_cast<float>(static_cast<double>(outLeft[i]) * scale);
        outLeft[i] = scaled;
        outRight[i] = scaled;
    }
}

// =============================================================================
// Metrics
// =============================================================================

/// THE PINNED DETECTOR CONFIGURATION (plan S15.3, stated verbatim). Designated
/// initialisers throughout - Clang rejects narrowing in brace initialisation -
/// and the field order is the declaration order at artifact_detection.h:38-45.
[[nodiscard]] std::size_t countClicks(const std::vector<float>& buffer, float sigma) {
    Krate::DSP::TestUtils::ClickDetectorConfig cfg{.sampleRate =
                                                       static_cast<float>(kSampleRate),
                                                   .frameSize = 512,
                                                   .hopSize = 256,
                                                   .detectionThreshold = sigma,
                                                   .energyThresholdDb = -60.0f,
                                                   .mergeGap = 5};
    Krate::DSP::TestUtils::ClickDetector detector(cfg);
    detector.prepare();
    return detector.detect(buffer.data(), buffer.size()).size();
}

/// Smallest sigma on a fixed ladder that yields 0 detections on `buffer`.
/// Used ONLY to build the failure message when the reference gate fires, so
/// that raising kClickThresholdSigma is a one-step, measured edit rather than a
/// guess. Returns 0 when no ladder value clears the buffer.
[[nodiscard]] float smallestZeroSigma(const std::vector<float>& buffer) {
    for (int step = 10; step <= 60; ++step) {  // 5.0 .. 30.0 in 0.5 increments
        const float sigma = 0.5f * static_cast<float>(step);
        if (countClicks(buffer, sigma) == 0u) {
            return sigma;
        }
    }
    return 0.0f;
}

[[nodiscard]] double maxAbsDelta(const std::vector<float>& buffer) {
    double worst = 0.0;
    for (std::size_t i = 1; i < buffer.size(); ++i) {
        worst = std::fmax(worst, std::fabs(static_cast<double>(buffer[i]) -
                                           static_cast<double>(buffer[i - 1])));
    }
    return worst;
}

[[nodiscard]] double rmsDb(const std::vector<float>& buffer, std::size_t first, std::size_t last) {
    if (last <= first || last > buffer.size()) {
        return -200.0;
    }
    double sumSquares = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const double v = static_cast<double>(buffer[i]);
        sumSquares += v * v;
    }
    const double meanSquare = sumSquares / static_cast<double>(last - first);
    return 10.0 * std::log10(meanSquare + 1e-20);
}

[[nodiscard]] const char* envelopeName(Krate::DSP::GrainEnvelopeType type) noexcept {
    switch (type) {
        case Krate::DSP::GrainEnvelopeType::Hann:
            return "Hann";
        case Krate::DSP::GrainEnvelopeType::Trapezoid:
            return "Trapezoid";
        case Krate::DSP::GrainEnvelopeType::Sine:
            return "Sine";
        case Krate::DSP::GrainEnvelopeType::Blackman:
            return "Blackman";
        case Krate::DSP::GrainEnvelopeType::Linear:
            return "Linear";
        case Krate::DSP::GrainEnvelopeType::Exponential:
            return "Exponential";
    }
    return "unknown";
}

// =============================================================================
// The render harness
// =============================================================================

struct CellOptions {
    float grainSeconds = 4.0f;
    float density = kCellDensity;
    Krate::DSP::GrainEnvelopeType envelope = Krate::DSP::GrainEnvelopeType::Hann;
    /// When true: silence() at 40 s and reset() at 50 s (plan S15.3's latch
    /// clause). The reference render leaves this false.
    bool exerciseLatch = false;
};

struct CellStats {
    /// Captured IMMEDIATELY before reset(), because reset() zeroes both counters
    /// (atmosphere_engine.h reset() step 10) and re-saturating the pool
    /// afterwards costs seconds of ring refill - asserting the live counter at
    /// the end of the render would be a timing lottery, not a precondition.
    std::uint64_t skipPoolFull = 0;
    std::uint64_t totalBorn = 0;
    std::size_t maxActive = 0;
    std::uint64_t bornAfterReset = 0;
    std::size_t maxActiveWhileLatched = 0;
};

/// Render one 60 s cell into `outLeft` / `outRight` (both pre-sized to
/// kRenderSamples). The engine is prepared per call: SC-003 sweeps
/// configurations that differ in ring-relevant ways, and a re-prepare is the
/// only honest way to start each from the documented post-prepare state.
[[nodiscard]] CellStats renderCell(const std::vector<float>& inLeft,
                                   const std::vector<float>& inRight,
                                   std::vector<float>& outLeft, std::vector<float>& outRight,
                                   const CellOptions& opt) {
    using Krate::DSP::AtmosphereEngine;

    CellStats stats;

    AtmosphereEngine engine;
    AtmosphereEngine::PrepareConfig cfg;
    cfg.captureSeconds = kCaptureSeconds;
    cfg.blurEnabled = false;
    cfg.freezeEnabled = false;
    cfg.maxBlockSamples = kBlockSamples;
    engine.prepare(kSampleRate, cfg);

    engine.setSeed(kSeed);
    engine.setGrainSeconds(opt.grainSeconds);
    engine.setDensity(opt.density);
    engine.setGrainEnvelope(opt.envelope);

    bool latched = false;

    for (std::size_t start = 0; start < kRenderSamples; start += kBlockSamples) {
        if (opt.exerciseLatch && start == kSilenceSample) {
            // Both preconditions are read HERE, before the ramp retires every
            // grain and long before reset() zeroes the counters.
            stats.skipPoolFull = engine.getSkippedTriggerCountPoolFull();
            stats.totalBorn = engine.getTotalGrainsBorn();
            engine.silence();
            latched = true;
        }
        if (opt.exerciseLatch && start == kResetSample) {
            engine.reset();
            latched = false;
        }

        engine.processStereoBlock(inLeft.data() + start, inRight.data() + start,
                                  outLeft.data() + start, outRight.data() + start, kBlockSamples);

        stats.maxActive = std::max(stats.maxActive, engine.getActiveGrainCount());
        if (latched && start >= kSilenceSample + kLatchSettleSamples) {
            stats.maxActiveWhileLatched =
                std::max(stats.maxActiveWhileLatched, engine.getActiveGrainCount());
        }
    }

    if (opt.exerciseLatch) {
        // Post-reset births: reset() zeroed the counter at 50 s, so whatever it
        // reads now was scheduled in the final 10 s.
        stats.bornAfterReset = engine.getTotalGrainsBorn();
    } else {
        stats.skipPoolFull = engine.getSkippedTriggerCountPoolFull();
        stats.totalBorn = engine.getTotalGrainsBorn();
    }
    return stats;
}

/// Index of the first sample in [first, last) that is not EXACTLY 0.0f, or
/// `last` when the whole span is exact silence. FR-007's latch clause is an
/// exact-zero requirement, not an amplitude threshold.
[[nodiscard]] std::size_t firstNonZero(const std::vector<float>& buffer, std::size_t first,
                                       std::size_t last) {
    for (std::size_t i = first; i < last && i < buffer.size(); ++i) {
        if (buffer[i] != 0.0f) {
            return i;
        }
    }
    return last;
}

}  // namespace

// =============================================================================
// SC-003 - No clicks at grain boundaries at ANY lifetime, with the FR-007 latch
//          and reset() inside the measured window (plan S15.3, task T009)
// =============================================================================
//
// This case GATES the grain engine (tasks T006-T008). A failure here is a real
// defect, not a tolerance question - the likeliest causes, in order, are: the
// L'-1 envelope phase denominator or the forced tail run (FR-027 / plan S9.6),
// an ACCUMULATED rather than multiplied envelope phase, a stolen grain
// (FR-023's skip-never-steal), or the 1/sqrt(n) smoother cadence (FR-028).
//
TEST_CASE("AtmosphereEngine_NoGrainBoundaryClicks", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;
    using Krate::DSP::GrainEnvelopeType;

    std::vector<float> inLeft(kRenderSamples, 0.0f);
    std::vector<float> inRight(kRenderSamples, 0.0f);
    makeHarmonicStack(inLeft.data(), inRight.data(), kRenderSamples, kSampleRate);

    // The generator must actually have produced the pinned signal - a silent
    // input would make every 0-detection result a silent-engine pass.
    REQUIRE(maxAbsDelta(inLeft) > 0.0);
    REQUIRE(rmsDb(inLeft, 0, kRenderSamples) > -20.0);

    std::vector<float> outLeft(kRenderSamples, 0.0f);
    std::vector<float> outRight(kRenderSamples, 0.0f);
    std::vector<float> refLeft(kRenderSamples, 0.0f);
    std::vector<float> refRight(kRenderSamples, 0.0f);

    constexpr std::array<float, 5> kGrainSecondsCells{0.05f, 0.2f, 1.0f, 5.0f, 30.0f};
    constexpr std::array<GrainEnvelopeType, 6> kEnvelopes{
        GrainEnvelopeType::Hann,      GrainEnvelopeType::Trapezoid,
        GrainEnvelopeType::Sine,      GrainEnvelopeType::Blackman,
        GrainEnvelopeType::Linear,    GrainEnvelopeType::Exponential};

    for (const float grainSeconds : kGrainSecondsCells) {
        // The mean concurrent grain count is density * grainSeconds. FR-009
        // caps density at 20, so at grainSeconds in {0.05, 0.2, 1} the maxima
        // are 1 / 4 / 20 against kMaxGrains = 64 and skipPoolFull_ is
        // STRUCTURALLY 0 there. A blanket pool-full precondition would fail a
        // CORRECT implementation in three of five cells - hence the split
        // below (deviation D-17). This is not a weakening: SC-001
        // (grainSeconds = 30, density = 20) still carries the unconditional
        // pool-full precondition, so FR-023 remains gated.
        const double meanConcurrent =
            static_cast<double>(kCellDensity) * static_cast<double>(grainSeconds);
        const bool saturatesPool =
            meanConcurrent > static_cast<double>(AtmosphereEngine::kMaxGrains);

        // The reference differs from the cell in EXACTLY ONE respect: no
        // silence()/reset(). Density included - see the reference-render
        // paragraph in the file banner for why matching it is what makes the
        // max |dy| ratio measure the transitions rather than the 1/sqrt(n) sum.
        const float referenceDensity = kCellDensity;

        for (const GrainEnvelopeType envelope : kEnvelopes) {
            INFO("grainSeconds = " << grainSeconds << ", envelope = " << envelopeName(envelope)
                                   << ", meanConcurrent = " << meanConcurrent
                                   << ", referenceDensity = " << referenceDensity);

            // -----------------------------------------------------------------
            // (1) Reference render, and the false-positive floor it establishes
            // -----------------------------------------------------------------
            const CellOptions refOpt{.grainSeconds = grainSeconds,
                                     .density = referenceDensity,
                                     .envelope = envelope,
                                     .exerciseLatch = false};
            const CellStats refStats = renderCell(inLeft, inRight, refLeft, refRight, refOpt);

            INFO("reference: born = " << refStats.totalBorn
                                      << ", maxActive = " << refStats.maxActive
                                      << ", poolFullSkips = " << refStats.skipPoolFull);

            // The reference must be the SAME operating point as the cell, not a
            // quieter one - that is the whole basis of the ratio bound below, so
            // it is asserted rather than assumed. Where the cell saturates the
            // pool the reference must saturate it too; where it structurally
            // cannot (D-17), both must still have rendered real grains.
            if (saturatesPool) {
                REQUIRE(refStats.skipPoolFull > 0u);
            } else {
                REQUIRE(refStats.skipPoolFull == 0u);
            }
            REQUIRE(refStats.totalBorn > 0u);
            REQUIRE(refStats.maxActive > 0u);

            const std::size_t refClicksLeft = countClicks(refLeft, kClickThresholdSigma);
            const std::size_t refClicksRight = countClicks(refRight, kClickThresholdSigma);
            if (refClicksLeft != 0u || refClicksRight != 0u) {
                UNSCOPED_INFO(
                    "REFERENCE FALSE-POSITIVE FLOOR IS NON-ZERO at sigma = "
                    << kClickThresholdSigma << " (L = " << refClicksLeft
                    << ", R = " << refClicksRight
                    << "). Smallest ladder sigma giving 0 on the reference: L = "
                    << smallestZeroSigma(refLeft) << ", R = " << smallestZeroSigma(refRight)
                    << ". FIRST establish whether this is a real engine artifact (compare the "
                       "envelopes against each other - one type standing out is a grain-boundary "
                       "step, all of them moving together is the material-driven floor). If it "
                       "is the floor, raise kClickThresholdSigma to EXACTLY that value - SC-003 "
                       "authorises the smallest sigma that gives 0 on the reference and no more "
                       "- and update the measured table in the TU header. NEVER relax the "
                       "0-detection requirement on the ENGINE render.");
            }
            REQUIRE(refClicksLeft == 0u);
            REQUIRE(refClicksRight == 0u);

            const double refDelta = std::fmax(maxAbsDelta(refLeft), maxAbsDelta(refRight));
            REQUIRE(refDelta > 0.0);

            // -----------------------------------------------------------------
            // (2) The engine render: density = 20, silence() at 40 s,
            //     reset() at 50 s, measured over the whole 60 s
            // -----------------------------------------------------------------
            const CellOptions cellOpt{.grainSeconds = grainSeconds,
                                      .density = kCellDensity,
                                      .envelope = envelope,
                                      .exerciseLatch = true};
            const CellStats cellStats = renderCell(inLeft, inRight, outLeft, outRight, cellOpt);

            INFO("cell: bornBeforeSilence = " << cellStats.totalBorn << ", maxActive = "
                                              << cellStats.maxActive << ", poolFullSkips = "
                                              << cellStats.skipPoolFull << ", bornAfterReset = "
                                              << cellStats.bornAfterReset);

            // Scoped precondition (D-17), per plan S15.3's table.
            if (saturatesPool) {
                REQUIRE(cellStats.skipPoolFull > 0u);  // FR-023's skip-never-steal path
            } else {
                REQUIRE(cellStats.totalBorn > 0u);
                REQUIRE(cellStats.maxActive > 0u);
            }

            // -----------------------------------------------------------------
            // (3) FR-007 latch clause: exact zeros from the end of the ramp to
            //     reset(), no live grain across the latched span, and audible
            //     again once the ring has refilled after reset()
            // -----------------------------------------------------------------
            const std::size_t latchFirst = kSilenceSample + kLatchSettleSamples;
            const std::size_t nonZeroLeft = firstNonZero(outLeft, latchFirst, kResetSample);
            const std::size_t nonZeroRight = firstNonZero(outRight, latchFirst, kResetSample);
            INFO("latched span [" << latchFirst << ", " << kResetSample
                                  << "): first non-zero L = " << nonZeroLeft
                                  << ", R = " << nonZeroRight);
            REQUIRE(nonZeroLeft == kResetSample);
            REQUIRE(nonZeroRight == kResetSample);
            REQUIRE(cellStats.maxActiveWhileLatched == 0u);

            REQUIRE(cellStats.bornAfterReset > 0u);
            const double tailLeftDb =
                rmsDb(outLeft, kRenderSamples - kTailWindowSamples, kRenderSamples);
            const double tailRightDb =
                rmsDb(outRight, kRenderSamples - kTailWindowSamples, kRenderSamples);
            INFO("post-reset tail RMS: L = " << tailLeftDb << " dBFS, R = " << tailRightDb
                                             << " dBFS");
            REQUIRE(std::fmax(tailLeftDb, tailRightDb) > -60.0);

            // -----------------------------------------------------------------
            // (4) THE CRITERION: 0 detections over the whole 60 s, with both
            //     transitions inside the measured window
            // -----------------------------------------------------------------
            const std::size_t clicksLeft = countClicks(outLeft, kClickThresholdSigma);
            const std::size_t clicksRight = countClicks(outRight, kClickThresholdSigma);
            INFO("engine detections: L = " << clicksLeft << ", R = " << clicksRight);
            REQUIRE(clicksLeft == 0u);
            REQUIRE(clicksRight == 0u);

            // -----------------------------------------------------------------
            // (5) Secondary bound - a ratio, never an absolute
            // -----------------------------------------------------------------
            const double cellDelta = std::fmax(maxAbsDelta(outLeft), maxAbsDelta(outRight));
            INFO("max |dy|: cell = " << cellDelta << ", reference = " << refDelta
                                     << ", ratio = " << (cellDelta / refDelta));
            REQUIRE(cellDelta <= kMaxDeltaRatio * refDelta);
        }
    }
}

// =============================================================================
// SC-006 - AtmosphereEngine_BlurTransparentAtZero (plan S15.5, task T012)
// =============================================================================
// THIS IS THE PLUMBING GATE FOR THE WHOLE BLUR STAGE, and it is written first
// for that reason: at blur = 0 the phase perturbation is identically 0, so the
// STFT <-> OverlapAdd round trip must reproduce the grain bus to within the
// numerical noise of an FFT plus one polar round trip. Everything structural
// about the stage is therefore in scope here, and each failure mode is LOUD
// rather than marginal:
//
//   - applySynthesisWindow = false at 75 % overlap (or true at 50 %): the COLA
//     sum stops being constant and the reconstruction ripples at the hop rate;
//   - a hop that is not fftSize / 4: same, plus a wrong colaNormalization_
//     (stft.h:226-239 sums w[k]^2 over the hop positions);
//   - OverlapAdd::pullSamples moved OUTSIDE the drain loop: synthesize always
//     accumulates at outputBuffer_[0 .. fftSize) with no offset
//     (stft.h:277-285) and the per-frame hop offset comes ONLY from the pull's
//     shift-left (:309-323), so two synthesizes without an intervening pull
//     stack both frames at the same offset;
//   - a mis-initialised FIFO: reset() must leave the write cursor LEADING the
//     read cursor by the blurFftSize_ pre-fill occupancy. Both cursors at 0
//     alongside a non-zero count makes the true latency the FIFO CAPACITY while
//     getLatencySamples() still reports blurFftSize_, so the delay compensation
//     below lands on entirely the wrong samples.
//
// THE COMPARISON IS DELAY-COMPENSATED BY getLatencySamples(), NOT BY A LITERAL.
// That is deliberate: it makes the accessor part of the criterion. An engine
// whose real latency and reported latency disagree fails here even if its
// reconstruction is perfect.
//
// WHY THE TWO RENDERS ARE COMPARABLE AT ALL. blurRng_ is a stream of its own
// (FR-044) and the blur stage is its only consumer, so enabling blur draws
// nothing from the grain-birth stream, from GrainScheduler's jitter stream or
// from the 64 drift-lane streams. The grain bus feeding the two engines is the
// same signal; the born-count equality asserted below is the standing proof.
//
// NO BIT-EXACT COMPARISON ANYWHERE: the metric is an RMS ratio in dB against a
// stated threshold, per dsp/CLAUDE.md.

namespace {

/// 6 s at 48 kHz, an exact multiple of kBlockSamples (600 blocks). Deliberately
/// NOT the 60 s SC-003 geometry: this criterion is a per-sample difference, so
/// what it needs is a busy grain population early, not a long tail.
constexpr std::size_t kTransparencySamples = 6u * 48000u;

/// The setter history applied IDENTICALLY to both engines. Short grains at a
/// healthy density with a small position keep the bus dense from ~0.3 s on, so
/// the measured window is continuous grain audio rather than sparse events.
void configureTransparency(Krate::DSP::AtmosphereEngine& engine) {
    engine.setSeed(0x5E2A0006u);
    engine.setGrainSeconds(1.5f);
    engine.setDensity(8.0f);
    engine.setJitter(0.4f);
    engine.setPositionSeconds(0.25f);
    engine.setPositionSpread(0.2f);
    engine.setPitchSemitones(0.0f);
    engine.setPitchSpread(0.1f);
    engine.setDriftDepth(0.3f);
    engine.setDriftSmoothness(0.7f);
    engine.setDriftRangeSemitones(2.0f);
    engine.setPanSpread(0.5f);
    engine.setDecorrelation(0.3f);
    engine.setLevel(1.0f);
    // THE ONE VALUE THE CRITERION IS ABOUT. Called on BOTH engines, including
    // the blur-disabled one, so the setter history is identical down to the
    // call sequence.
    engine.setBlur(0.0f);
}

struct TransparencyRender {
    std::vector<float> left;
    std::vector<float> right;
    std::uint64_t born = 0;
    std::size_t latency = 0;
};

[[nodiscard]] TransparencyRender renderTransparency(const std::vector<float>& inLeft,
                                                    const std::vector<float>& inRight,
                                                    bool blurEnabled) {
    using Krate::DSP::AtmosphereEngine;

    TransparencyRender out;
    out.left.assign(kTransparencySamples, 0.0f);
    out.right.assign(kTransparencySamples, 0.0f);

    AtmosphereEngine engine;
    AtmosphereEngine::PrepareConfig cfg;
    cfg.captureSeconds = 8.0f;
    cfg.blurEnabled = blurEnabled;
    // Freeze OFF in both legs: T013's delay-matched freeze leg belongs to
    // SC-007, and routing it through here would fold a second latency question
    // into a criterion that is only about COLA and the FIFO.
    cfg.freezeEnabled = false;
    cfg.blurFftSize = 1024;
    cfg.maxBlockSamples = kBlockSamples;
    engine.prepare(kSampleRate, cfg);
    configureTransparency(engine);

    for (std::size_t start = 0; start < kTransparencySamples; start += kBlockSamples) {
        engine.processStereoBlock(inLeft.data() + start, inRight.data() + start,
                                  out.left.data() + start, out.right.data() + start,
                                  kBlockSamples);
    }

    out.born = engine.getTotalGrainsBorn();
    out.latency = engine.getLatencySamples();
    return out;
}

/// RMS of (delay-compensated blurred - dry) over the settled window, in dB
/// relative to the dry RMS over the SAME window. A ratio, never an absolute:
/// an absolute bound would constrain the input level instead of the engine.
[[nodiscard]] double transparencyDb(const std::vector<float>& blurred,
                                    const std::vector<float>& dry, std::size_t latency,
                                    std::size_t warmup) {
    double sumDiff = 0.0;
    double sumDry = 0.0;
    std::size_t count = 0;
    for (std::size_t i = warmup; i + latency < dry.size(); ++i) {
        const double d = static_cast<double>(dry[i]);
        const double b = static_cast<double>(blurred[i + latency]);
        sumDiff += (b - d) * (b - d);
        sumDry += d * d;
        ++count;
    }
    if (count == 0 || sumDry <= 0.0) {
        return 0.0;  // 0 dB: the caller's non-vacuousness guards reject this
    }
    return 10.0 * std::log10((sumDiff / sumDry) + 1e-30);
}

}  // namespace

TEST_CASE("AtmosphereEngine_BlurTransparentAtZero", "[atmosphere]") {
    std::vector<float> inLeft(kTransparencySamples, 0.0f);
    std::vector<float> inRight(kTransparencySamples, 0.0f);
    makeHarmonicStack(inLeft.data(), inRight.data(), kTransparencySamples, kSampleRate);
    REQUIRE(rmsDb(inLeft, 0, kTransparencySamples) > -20.0);

    const TransparencyRender dry = renderTransparency(inLeft, inRight, /*blurEnabled=*/false);
    const TransparencyRender wet = renderTransparency(inLeft, inRight, /*blurEnabled=*/true);

    // FR-045 / FR-046: the two legs differ in latency by exactly the snapped
    // blur FFT size, and in nothing else.
    REQUIRE(dry.latency == 0u);
    REQUIRE(wet.latency == 1024u);

    // NON-VACUOUSNESS FIRST. Two silent engines agree perfectly, and so do two
    // engines that never admitted a grain.
    CAPTURE(dry.born, wet.born);
    REQUIRE(dry.born > 0u);
    REQUIRE(wet.born == dry.born);

    // Discard 2 * fftSize of OverlapAdd warm-up: the FIFO's blurFftSize_
    // pre-fill zeros plus the first hops, which are only partially overlapped
    // and therefore genuinely not unity-gain.
    const std::size_t warmup = 2u * wet.latency;
    const double dryLeftDb = rmsDb(dry.left, warmup, kTransparencySamples);
    const double dryRightDb = rmsDb(dry.right, warmup, kTransparencySamples);
    INFO("dry RMS over the measured window: L = " << dryLeftDb << " dBFS, R = " << dryRightDb
                                                  << " dBFS");
    REQUIRE(dryLeftDb > -40.0);
    REQUIRE(dryRightDb > -40.0);

    // -------------------------------------------------------------------------
    // THE CRITERION
    // -------------------------------------------------------------------------
    const double leftDb = transparencyDb(wet.left, dry.left, wet.latency, warmup);
    const double rightDb = transparencyDb(wet.right, dry.right, wet.latency, warmup);
    INFO("delay-compensated difference: L = " << leftDb << " dB, R = " << rightDb
                                              << " dB (bound -60 dB)");
    REQUIRE(leftDb <= -60.0);
    REQUIRE(rightDb <= -60.0);
}

// =============================================================================
// SC-005 - AtmosphereEngine_BlurMonotonicity (plan S15.5, task T013)
// =============================================================================
// THREE CLAUSES, ONE CASE, because all three are the SAME stage measured three
// ways and a split would let two of them silently stop being run together:
//
//   (1) MONOTONICITY. Averaged spectral flatness is non-decreasing over
//       blur in {0, 0.25, 0.5, 0.75, 1}, and flatness(1) >= 1.25 * flatness(0).
//       Phase randomisation never touches magnitude (atmosphere_engine.h's
//       pumpBlur comment: "MAGNITUDE IS NEVER WRITTEN"), so the whole effect on
//       the measured spectrum is INTER-FRAME decoherence: with the phases of
//       successive 1024-point frames perturbed independently, overlap-add no
//       longer reconstructs the line spectrum and each partial spreads into a
//       skirt. That is why the analysis window is deliberately LONGER than the
//       blur FFT (see the fftSize note below) - a window shorter than one frame
//       would see a single coherent frame and measure almost nothing.
//
//   (2) STEREO DECORRELATION (FR-042). The blur draw is per bin PER CHANNEL
//       from the one blurRng_ stream (atmosphere_engine.h pumpBlur, the
//       `for ch` loop encloses the per-bin draw), so L and R receive
//       INDEPENDENT perturbations and rho(L,R) falls as blur rises. An
//       implementation that drew once per bin and applied the same value to
//       both channels would fog identically and decorrelate not at all: it
//       would pass clause (1), pass SC-006 (blur = 0 is the identity either
//       way) and pass SC-007. This clause is the only thing in the phase that
//       can see it.
//
//   (3) CREST FACTOR, on a SEPARATE PINNED INPUT. Flatness is a frequency-domain
//       statistic and says nothing about whether transients survive; an
//       implementation that smeared the spectrum while leaving impulses intact
//       would pass (1). The impulse train is the input that makes the
//       time-domain question askable at all - a sustained harmonic stack has no
//       transient to smear, and its crest factor is a property of the source.
//
// WHY THE FLATNESS MEASUREMENT IS WRITTEN THE WAY IT IS (binding, not style).
// calculateSpectralFlatness picks fftSize as the largest power of two <= n
// capped at 4096 (tests/test_helpers/signal_metrics.h:336-339) and fills its
// analysis window from signal[0 .. fftSize) ONLY (:350-352) - it does NOT
// average over n. On an all-zero window it returns 0.0f via the
// `arithMean < 1e-10` early return (:376-378). The naive call - the whole
// render from sample 0 - therefore reads the guaranteed-silent head of the
// render (positionSeconds is pinned to 1.0 s below, so no grain can be born
// before 48 000 samples, and the blur leg adds 1024 more of latency), returns
// 0.0f for EVERY blur value, and the criterion degenerates: "non-decreasing"
// holds trivially on 0 >= 0 and the ratio clause reduces to 0 >= 1.25 * 0.
// It would pass on an engine that emitted nothing at all. Hence:
//   * every window starts at kMonotonicitySettle, static_asserted to be at
//     least positionSeconds * sampleRate + 2 * blurFftSize;
//   * every window is EXACTLY 8192 samples, so the helper selects
//     fftSize = 4096 - four times the 1024-sample blur frame, which is what
//     makes the inter-frame decoherence of clause (1) visible;
//   * four disjoint windows are averaged, so one atypical grain population
//     cannot decide a step;
//   * the non-silence precondition (window RMS > -40 dBFS on BOTH channels,
//     and flatness(0) > 0) is asserted BEFORE any threshold is compared.
//
// THE TWO EPSILONS ARE DIFFERENT IN KIND, DELIBERATELY. Flatness is a positive
// scale quantity, so its 2 % is RELATIVE (f[i] >= f[i-1] * 0.98). rho is a
// bounded correlation that legitimately approaches 0 at full blur, where a
// relative epsilon degenerates to nothing; its 2 % is therefore ABSOLUTE on the
// [-1, 1] scale (rho[i] <= rho[i-1] + 0.02).

namespace {

// --- geometry ---------------------------------------------------------------

/// 6 s at 48 kHz = 600 blocks of kBlockSamples. Long enough to hold the settle
/// point plus four spaced analysis windows, short enough that a five-point blur
/// sweep is five 6 s renders rather than five 60 s ones.
constexpr std::size_t kMonotonicitySamples = 6u * 48000u;

/// PINNED birth read age. The settle bound below is computed FROM this value,
/// so the two cannot drift apart: change one and the static_assert fires.
constexpr float kBlurSweepPositionSeconds = 1.0f;
constexpr std::size_t kBlurSweepPositionSamples = 48000u;  // 1.0 s at kSampleRate

constexpr std::size_t kBlurFftSize = 1024u;

/// 4 s. Comfortably past the binding minimum (positionSeconds * sampleRate +
/// 2 * blurFftSize = 50 048), and far enough in that the grain population has
/// reached its steady mean of density * grainSeconds rather than still filling.
constexpr std::size_t kMonotonicitySettle = 4u * 48000u;

/// EXACTLY 8192 so calculateSpectralFlatness selects fftSize = 4096 (:336-339).
/// Not a tunable: 4096 vs the 1024-sample blur frame is the whole measurement.
constexpr std::size_t kFlatnessWindow = 8192u;
/// Stride > window, so the four windows are disjoint AND spread over ~1 s of
/// render rather than being one contiguous 32 768-sample block.
constexpr std::size_t kFlatnessWindowStride = 16384u;
constexpr std::size_t kFlatnessWindows = 4u;

/// 20 s at 48 kHz (tasks.md T013), measured over the 10 s at [settle, settle+480k).
constexpr std::size_t kCrestSamples = 20u * 48000u;
constexpr std::size_t kCrestMeasureSamples = 480000u;
/// One impulse every 0.5 s.
constexpr std::size_t kImpulsePeriod = 24000u;

constexpr std::uint32_t kBlurSweepSeed = 0x5E2A0007u;

static_assert(kMonotonicitySettle >= kBlurSweepPositionSamples + 2u * kBlurFftSize,
              "the analysis windows must start past the guaranteed-silent head of the "
              "render (birth read age + blur latency) or every flatness reads 0");
static_assert(kMonotonicitySettle + (kFlatnessWindows - 1u) * kFlatnessWindowStride +
                      kFlatnessWindow <=
                  kMonotonicitySamples,
              "the four analysis windows must fit inside the render");
static_assert(kFlatnessWindowStride >= kFlatnessWindow, "the windows must be disjoint");
static_assert(kMonotonicitySamples % kBlockSamples == 0u, "render must be a whole block count");
static_assert(kCrestSamples % kBlockSamples == 0u, "render must be a whole block count");
static_assert(kMonotonicitySettle + kCrestMeasureSamples <= kCrestSamples,
              "the crest measurement window must fit inside the crest render");

// --- the three floors (see the SC-005 section of the file banner) ------------
// All three currently stand at spec.md's MINIMUMS. They are calibrated upward
// from measurement per O-2; they are never lowered.

constexpr double kFlatnessRatioFloor = 1.75;    ///< measured 1.948, less 10 %
constexpr double kCorrelationDropFloor = 0.93;  ///< measured 1.0385, less 10 %
constexpr double kCrestDropFloorDb = 13.0;      ///< measured 14.506 dB, less 10 %

/// RELATIVE, because flatness is a positive scale quantity.
constexpr double kFlatnessStepEpsilon = 0.02;
/// ABSOLUTE on the [-1, 1] correlation scale, because rho legitimately reaches
/// ~0 at full blur and a relative epsilon would vanish exactly there.
constexpr double kCorrelationStepEpsilon = 0.02;

/// The crest render's input is an impulse train - one unit sample per 24 000 -
/// whose own RMS is sqrt(1/24000) = -43.8 dBFS BEFORE granulation and the
/// 1/sqrt(n) sum. The -40 dBFS non-silence bound the flatness windows carry is
/// therefore structurally unreachable here and would fail a correct engine;
/// this bound is set well below the source level instead, and its job is
/// unchanged - reject a silent render before any threshold is compared.
constexpr double kCrestNonSilenceDb = -80.0;

// --- the second pinned input: the flatness / rho sweep's source --------------

/// Top partial of makeBroadbandStack, in Hz. Not a tunable: the engine's own
/// pitch range (setPitchSpread 0.1 plus the 2-semitone drift range pinned in
/// configureBlurSweep) reads the capture ring at up to ~1.13x, so 16 kHz maps to
/// ~18.1 kHz - clear of the 24 kHz Nyquist, with no fold-back to contaminate a
/// measurement that is about smearing.
constexpr double kBroadbandFundamental = 1000.0;
constexpr int kBroadbandPartials = 16;

static_assert(kBroadbandFundamental * kBroadbandPartials * 1.25 < 0.5 * 48000.0,
              "the top partial must stay below Nyquist even at the maximum "
              "grain playback ratio, or aliasing enters the flatness statistic");

/// @brief Broadband harmonic stack: 1 kHz fundamental, partials 1x..16x at 1/n
///        amplitude, sine, zero phase, scaled to peak 0.5. Both channels
///        identical, so any stereo difference in the output is the engine's.
///
/// SC-003's construction rule verbatim - harmonic series, 1/n amplitude, sine,
/// zero phase, peak 0.5 - with only the fundamental and the partial count moved,
/// so the series spans the 0..24 kHz band the flatness helper analyses. Read
/// section (I) of the file banner before changing anything here: this input is
/// the resolution of a contradiction between two binding halves of spec.md
/// SC-005, and reverting it to makeHarmonicStack makes the criterion measure the
/// output LEVEL rather than the smearing.
///
/// Accumulated in double and normalised at the end, exactly as makeHarmonicStack
/// is: the partial sum peaks well above 1.0 before scaling.
void makeBroadbandStack(float* outLeft, float* outRight, std::size_t numSamples, double sampleRate) {
    if (outLeft == nullptr || outRight == nullptr || sampleRate <= 0.0) {
        return;
    }

    constexpr double kTwoPi = 6.283185307179586476925286766559;

    std::vector<double> accumulator(numSamples, 0.0);
    for (int harmonic = 1; harmonic <= kBroadbandPartials; ++harmonic) {
        const double frequency = kBroadbandFundamental * static_cast<double>(harmonic);
        const double amplitude = 1.0 / static_cast<double>(harmonic);
        for (std::size_t i = 0; i < numSamples; ++i) {
            accumulator[i] +=
                amplitude * std::sin(kTwoPi * frequency * static_cast<double>(i) / sampleRate);
        }
    }

    double peak = 0.0;
    for (const double value : accumulator) {
        peak = std::max(peak, std::abs(value));
    }
    const double gain = (peak > 0.0) ? (0.5 / peak) : 1.0;

    for (std::size_t i = 0; i < numSamples; ++i) {
        outLeft[i] = static_cast<float>(accumulator[i] * gain);
        outRight[i] = outLeft[i];
    }
}

// --- the third pinned input --------------------------------------------------

/// @brief Otherwise-silent stereo impulse train: x[n] = 1.0f on BOTH channels at
///        every multiple of kImpulsePeriod, 0 elsewhere (tasks.md T013).
///
/// Pinned for the same reason makeHarmonicStack is: crest factor is a property
/// of the input as much as of the processing, so an unpinned input makes the
/// 3 dB drop unreproducible. Both channels carry the identical train, so any
/// stereo difference in the output is the engine's.
void makeImpulseTrain(float* outLeft, float* outRight, std::size_t numSamples) {
    if (outLeft == nullptr || outRight == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < numSamples; ++i) {
        const float value = (i % kImpulsePeriod == 0u) ? 1.0f : 0.0f;
        outLeft[i] = value;
        outRight[i] = value;
    }
}

// --- the render harness ------------------------------------------------------

/// The setter history applied IDENTICALLY at every blur step. Everything except
/// setBlur is pinned, so the sweep varies exactly one thing.
void configureBlurSweep(Krate::DSP::AtmosphereEngine& engine, float blur) {
    engine.setSeed(kBlurSweepSeed);
    engine.setGrainSeconds(1.5f);
    engine.setDensity(8.0f);
    engine.setJitter(0.4f);
    engine.setPositionSeconds(kBlurSweepPositionSeconds);
    engine.setPositionSpread(0.2f);
    engine.setPitchSemitones(0.0f);
    engine.setPitchSpread(0.1f);
    engine.setDriftDepth(0.3f);
    engine.setDriftSmoothness(0.7f);
    engine.setDriftRangeSemitones(2.0f);
    // BOTH ZERO, AND THAT IS LOAD-BEARING FOR CLAUSE (2) - see section (II) of
    // the file banner. These two controls decorrelate L and R BEFORE blur runs;
    // at 0.5 / 0.3 the dry render already measured rho(0.0) = 0.0828, which
    // leaves the FR-042 clause no headroom AND makes it blind to the very defect
    // it exists to catch (a shared per-bin draw would land at the same 0.08).
    // At 0 the dry signal is perfectly correlated, so rho(0.0) = 1.000 and only
    // the blur stage can move it.
    engine.setPanSpread(0.0f);
    engine.setDecorrelation(0.0f);
    engine.setFreezeMix(0.0f);
    engine.setLevel(1.0f);
    // THE ONE VALUE THE SWEEP MOVES.
    engine.setBlur(blur);
}

struct SweepRender {
    std::vector<float> left;
    std::vector<float> right;
    std::uint64_t born = 0;
    std::size_t latency = 0;
};

/// Render `numSamples` (a whole multiple of kBlockSamples) with blur enabled at
/// the given amount. Freeze is OFF: the delay-matched freeze leg is SC-007's
/// business (T014), and enabling it here would fold a second latency question
/// into a criterion that is only about the blur stage.
[[nodiscard]] SweepRender renderBlurSweep(const std::vector<float>& inLeft,
                                          const std::vector<float>& inRight,
                                          std::size_t numSamples, float blur) {
    using Krate::DSP::AtmosphereEngine;

    SweepRender out;
    out.left.assign(numSamples, 0.0f);
    out.right.assign(numSamples, 0.0f);

    AtmosphereEngine engine;
    AtmosphereEngine::PrepareConfig cfg;
    cfg.captureSeconds = 8.0f;
    cfg.blurEnabled = true;
    cfg.freezeEnabled = false;
    cfg.blurFftSize = kBlurFftSize;
    cfg.maxBlockSamples = kBlockSamples;
    engine.prepare(kSampleRate, cfg);
    configureBlurSweep(engine, blur);

    for (std::size_t start = 0; start + kBlockSamples <= numSamples; start += kBlockSamples) {
        engine.processStereoBlock(inLeft.data() + start, inRight.data() + start,
                                  out.left.data() + start, out.right.data() + start,
                                  kBlockSamples);
    }

    out.born = engine.getTotalGrainsBorn();
    out.latency = engine.getLatencySamples();
    return out;
}

// --- metrics -----------------------------------------------------------------

/// FULLY QUALIFIED, never a using-declaration and never unqualified: a
/// same-named 2-argument overload exists at
/// dsp/include/krate/dsp/primitives/spectral_utils.h:335 and an unqualified
/// call can bind the wrong one. The length is ALWAYS kFlatnessWindow so the
/// helper's fftSize selection is fixed at 4096.
[[nodiscard]] double flatnessAt(const std::vector<float>& buffer, std::size_t start) {
    return static_cast<double>(
        Krate::DSP::TestUtils::SignalMetrics::calculateSpectralFlatness(
            buffer.data() + start, kFlatnessWindow, static_cast<float>(kSampleRate)));
}

/// Normalised inter-channel correlation over [start, start + length).
/// Not mean-subtracted: the engine's output is DC-free by construction (the
/// capture ring carries audio and the grain envelope is zero-mean-preserving),
/// so subtracting a sample mean would only add estimator noise.
[[nodiscard]] double correlationAt(const std::vector<float>& left, const std::vector<float>& right,
                                   std::size_t start, std::size_t length) {
    double sumLR = 0.0;
    double sumLL = 0.0;
    double sumRR = 0.0;
    for (std::size_t i = start; i < start + length && i < left.size() && i < right.size(); ++i) {
        const double l = static_cast<double>(left[i]);
        const double r = static_cast<double>(right[i]);
        sumLR += l * r;
        sumLL += l * l;
        sumRR += r * r;
    }
    const double denom = std::sqrt(sumLL * sumRR);
    return (denom > 0.0) ? (sumLR / denom) : 0.0;
}

[[nodiscard]] double crestDbAt(const std::vector<float>& buffer, std::size_t start,
                               std::size_t length) {
    if (start + length > buffer.size()) {
        return 0.0;
    }
    return static_cast<double>(Krate::DSP::TestUtils::SignalMetrics::calculateCrestFactorDb(
        buffer.data() + start, length));
}

}  // namespace

TEST_CASE("AtmosphereEngine_BlurMonotonicity", "[atmosphere]") {
    constexpr std::size_t kSteps = 5;
    constexpr std::array<float, kSteps> kBlurSteps{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    // -------------------------------------------------------------------------
    // (0) The pinned input, and proof that the generator produced it
    // -------------------------------------------------------------------------
    // NOT makeHarmonicStack: section (I) of the file banner records why, with the
    // measurements. SC-003's stack is band-limited below 2 kHz and the metric
    // analyses 0..24 kHz, so the statistic is dominated by bins that carry no
    // signal at any blur value.
    std::vector<float> inLeft(kMonotonicitySamples, 0.0f);
    std::vector<float> inRight(kMonotonicitySamples, 0.0f);
    makeBroadbandStack(inLeft.data(), inRight.data(), kMonotonicitySamples, kSampleRate);
    REQUIRE(rmsDb(inLeft, 0, kMonotonicitySamples) > -20.0);

    std::array<double, kSteps> flatness{};
    std::array<double, kSteps> correlation{};

    for (std::size_t s = 0; s < kSteps; ++s) {
        const SweepRender render =
            renderBlurSweep(inLeft, inRight, kMonotonicitySamples, kBlurSteps[s]);

        INFO("blur = " << kBlurSteps[s] << ", born = " << render.born
                       << ", latency = " << render.latency);

        // The blur leg must actually be in the path, and grains must actually
        // have been admitted: two silent engines have equal flatness (0) and
        // perfect correlation, and both clauses below would pass on them.
        REQUIRE(render.latency == kBlurFftSize);
        REQUIRE(render.born > 0u);

        double flatSum = 0.0;
        double corrSum = 0.0;
        for (std::size_t w = 0; w < kFlatnessWindows; ++w) {
            const std::size_t start = kMonotonicitySettle + w * kFlatnessWindowStride;

            // NON-SILENCE PRECONDITION, BEFORE ANY THRESHOLD IS COMPARED.
            const double windowLeftDb = rmsDb(render.left, start, start + kFlatnessWindow);
            const double windowRightDb = rmsDb(render.right, start, start + kFlatnessWindow);
            INFO("window " << w << " at " << start << ": RMS L = " << windowLeftDb
                           << " dBFS, R = " << windowRightDb << " dBFS");
            REQUIRE(windowLeftDb > -40.0);
            REQUIRE(windowRightDb > -40.0);

            flatSum += 0.5 * (flatnessAt(render.left, start) + flatnessAt(render.right, start));
            corrSum += correlationAt(render.left, render.right, start, kFlatnessWindow);
        }

        flatness[s] = flatSum / static_cast<double>(kFlatnessWindows);
        correlation[s] = corrSum / static_cast<double>(kFlatnessWindows);
    }

    // These two INFO lines are the calibration inputs the file banner's
    // procedure reads - do not remove them.
    INFO("averaged flatness by blur step: " << flatness[0] << ", " << flatness[1] << ", "
                                            << flatness[2] << ", " << flatness[3] << ", "
                                            << flatness[4]);
    INFO("averaged rho(L,R) by blur step: " << correlation[0] << ", " << correlation[1] << ", "
                                            << correlation[2] << ", " << correlation[3] << ", "
                                            << correlation[4]);

    // -------------------------------------------------------------------------
    // (1) MONOTONICITY. flatness(0) > 0 comes FIRST: on an all-zero window the
    //     helper returns 0.0f (signal_metrics.h:376-378), and every clause
    //     below is satisfied by the all-zeros vector.
    // -------------------------------------------------------------------------
    REQUIRE(flatness[0] > 0.0);

    for (std::size_t s = 1; s < kSteps; ++s) {
        INFO("flatness step " << (s - 1) << " -> " << s << ": " << flatness[s - 1] << " -> "
                              << flatness[s]);
        REQUIRE(flatness[s] >= flatness[s - 1] * (1.0 - kFlatnessStepEpsilon));
    }

    INFO("flatness ratio = " << (flatness[kSteps - 1] / flatness[0]) << " (floor "
                             << kFlatnessRatioFloor << ")");
    REQUIRE(flatness[kSteps - 1] >= kFlatnessRatioFloor * flatness[0]);

    // -------------------------------------------------------------------------
    // (2) STEREO DECORRELATION (FR-042). An implementation drawing one phase
    //     perturbation per bin and applying it to BOTH channels passes every
    //     other criterion in the phase and fails here.
    // -------------------------------------------------------------------------
    for (std::size_t s = 1; s < kSteps; ++s) {
        INFO("rho step " << (s - 1) << " -> " << s << ": " << correlation[s - 1] << " -> "
                         << correlation[s]);
        REQUIRE(correlation[s] <= correlation[s - 1] + kCorrelationStepEpsilon);
    }

    INFO("rho drop = " << (correlation[0] - correlation[kSteps - 1]) << " (floor "
                       << kCorrelationDropFloor << ")");
    REQUIRE(correlation[0] - correlation[kSteps - 1] >= kCorrelationDropFloor);

    // -------------------------------------------------------------------------
    // (3) CREST FACTOR, on its own pinned input (tasks.md T013)
    // -------------------------------------------------------------------------
    std::vector<float> impulseLeft(kCrestSamples, 0.0f);
    std::vector<float> impulseRight(kCrestSamples, 0.0f);
    makeImpulseTrain(impulseLeft.data(), impulseRight.data(), kCrestSamples);

    const SweepRender crestDry = renderBlurSweep(impulseLeft, impulseRight, kCrestSamples, 0.0f);
    const SweepRender crestWet = renderBlurSweep(impulseLeft, impulseRight, kCrestSamples, 1.0f);

    // Same seed, same setter history, one differing value - and blurRng_ is a
    // stream of its own (FR-044), so the grain layer feeding the two legs is
    // identical. The born-count equality is the standing proof of that.
    CAPTURE(crestDry.born, crestWet.born);
    REQUIRE(crestDry.born > 0u);
    REQUIRE(crestWet.born == crestDry.born);

    const double dryRmsLeftDb =
        rmsDb(crestDry.left, kMonotonicitySettle, kMonotonicitySettle + kCrestMeasureSamples);
    const double wetRmsLeftDb =
        rmsDb(crestWet.left, kMonotonicitySettle, kMonotonicitySettle + kCrestMeasureSamples);
    INFO("crest-render RMS (L): blur 0 = " << dryRmsLeftDb << " dBFS, blur 1 = " << wetRmsLeftDb
                                           << " dBFS (bound " << kCrestNonSilenceDb << ")");
    REQUIRE(dryRmsLeftDb > kCrestNonSilenceDb);
    REQUIRE(wetRmsLeftDb > kCrestNonSilenceDb);

    const double dryCrestLeft =
        crestDbAt(crestDry.left, kMonotonicitySettle, kCrestMeasureSamples);
    const double dryCrestRight =
        crestDbAt(crestDry.right, kMonotonicitySettle, kCrestMeasureSamples);
    const double wetCrestLeft =
        crestDbAt(crestWet.left, kMonotonicitySettle, kCrestMeasureSamples);
    const double wetCrestRight =
        crestDbAt(crestWet.right, kMonotonicitySettle, kCrestMeasureSamples);

    // The third calibration input.
    INFO("crest factor dB: blur 0 (L = " << dryCrestLeft << ", R = " << dryCrestRight
                                         << "), blur 1 (L = " << wetCrestLeft
                                         << ", R = " << wetCrestRight << "), floor "
                                         << kCrestDropFloorDb << " dB");

    // calculateCrestFactorDb returns exactly 0.0f when the RMS is below 1e-10
    // (signal_metrics.h:245-247), so a zero here means "no measurement", not
    // "0 dB of crest" - reject it before comparing drops.
    REQUIRE(dryCrestLeft > 0.0);
    REQUIRE(dryCrestRight > 0.0);

    // Asserted PER CHANNEL rather than on the stereo average: the blur draws are
    // independent per channel, so an averaged drop could hide one channel that
    // did not move.
    REQUIRE(dryCrestLeft - wetCrestLeft >= kCrestDropFloorDb);
    REQUIRE(dryCrestRight - wetCrestRight >= kCrestDropFloorDb);
}

// =============================================================================
// SC-007 - the pure-freeze drone holds its level, and the crossfade is clean
//          (plan S15.5, task T014)
// =============================================================================
//
// WHY blurEnabled = true HERE AND NOWHERE ELSE IN THIS TU. FR-052 routes the
// freeze leg through a prepare-allocated blurFftSize_-sample delay so both
// crossfade legs share ONE layer latency. If that delay is missing, wrong, or
// not advanced while the leg is in hard bypass, the two legs are offset by
// 21.3 ms at this geometry and the crossfade smears in time - which presents as
// a STEP at the transition. This case is the only place in the whole spec that
// would see it, which is why it - and not SC-005 or SC-006 - carries the
// freeze-enabled configuration.
//
// WHY THE REFERENCE WINDOW IS THE SECOND ONE. Window 1 necessarily contains the
// 100 ms kFreezeMixRampMs crossfade AND the oscillator's own overlap-add
// pre-fill (processors/spectral_freeze_oscillator.h:261-287). Referencing it
// would measure a partially-ramped signal and spend the whole +/-1.0 dB budget
// on a transient the criterion is not about.
//
// WHY blur IS PINNED AT 0.0 WHILE blurEnabled IS true. The blur AMOUNT only
// reaches the WET leg, and at a settled freezeMix of 1.0 the wet leg is
// multiplied by exactly zero - so a non-zero blur would change nothing about
// the stability clause while adding an unrelated source of transients to the
// 0-detection clause. The blur STAGE is still in the path either way: that is
// what blurEnabled controls, and getLatencySamples() == kFreezeBlurFftSize is
// asserted below to prove it.

namespace {

// --- geometry ----------------------------------------------------------------

/// Ring warm-up before captureFreeze(): 2 s. Far more than one freeze window,
/// so the capture takes its REAL path rather than the FR-051 early-out (which
/// is the main TU's AtmosphereEngine_FreezeCaptureAndRelease clause), and past
/// the birth read age so the grain bus is dense at the moment of the crossfade.
constexpr std::size_t kFreezeWarmupSamples = 2u * 48000u;

/// 60 successive non-overlapping 1 s windows, measured from the sample the
/// crossfade is commanded at.
constexpr std::size_t kFreezeWindowSamples = 48000u;
constexpr std::size_t kFreezeWindows = 60u;
constexpr std::size_t kFreezeHoldSamples = kFreezeWindows * kFreezeWindowSamples;
constexpr std::size_t kFreezeStabilitySamples = kFreezeWarmupSamples + kFreezeHoldSamples;

constexpr std::size_t kFreezeFftSize = 2048u;
constexpr std::size_t kFreezeBlurFftSize = 1024u;

/// The 0-detection clause's geometry: warm up, hold at 1 for 3 s, return to 0
/// and hold for 3 s. Both transitions are inside the measured span.
constexpr std::size_t kFreezeSweepHoldSamples = 3u * 48000u;
constexpr std::size_t kFreezeSweepSamples =
    kFreezeWarmupSamples + 2u * kFreezeSweepHoldSamples;

/// Detection starts 0.5 s BEFORE the first crossfade. The clause is about the
/// crossfade, so the cold head of the render - the first grain births and the
/// blur FIFO's pre-filled latency - is deliberately outside the measured span
/// rather than being blamed on a transition it precedes by two seconds.
constexpr std::size_t kFreezeSweepDetectStart = kFreezeWarmupSamples - 24000u;

constexpr std::uint32_t kFreezeSeed = 0x5E2A0008u;

/// The whole criterion, in two numbers. Both come straight from spec.md SC-007
/// and are never relaxed.
constexpr double kFreezePeakToleranceDb = 1.0;
constexpr double kFreezePeakFloorDb = -60.0;

static_assert(kFreezeStabilitySamples % kBlockSamples == 0u,
              "the stability render must be a whole block count");
static_assert(kFreezeSweepSamples % kBlockSamples == 0u,
              "the sweep render must be a whole block count");
static_assert(kFreezeWarmupSamples % kBlockSamples == 0u,
              "captureFreeze() must land on a block boundary");
static_assert(kFreezeWarmupSamples > kFreezeFftSize,
              "the ring must hold a WHOLE freeze window before captureFreeze()");
static_assert(kFreezeSweepHoldSamples % kBlockSamples == 0u,
              "each sweep hold must be a whole block count");
static_assert(kFreezeSweepSamples <= kFreezeStabilitySamples,
              "the sweep reuses a PREFIX of the stability render's input");

// --- the render harness ------------------------------------------------------

/// The setter history applied identically to both renders below.
void configureFreeze(Krate::DSP::AtmosphereEngine& engine) {
    engine.setSeed(kFreezeSeed);
    engine.setGrainSeconds(1.5f);
    engine.setDensity(8.0f);
    engine.setJitter(0.4f);
    engine.setPositionSeconds(0.25f);
    engine.setPositionSpread(0.2f);
    engine.setPitchSemitones(0.0f);
    engine.setPitchSpread(0.1f);
    engine.setDriftDepth(0.3f);
    engine.setDriftSmoothness(0.7f);
    engine.setDriftRangeSemitones(2.0f);
    engine.setPanSpread(0.5f);
    engine.setDecorrelation(0.3f);
    // See the case banner: pinned at 0 on purpose, with blurEnabled still true.
    engine.setBlur(0.0f);
    engine.setFreezeMix(0.0f);
    engine.setLevel(1.0f);
}

struct FreezeRender {
    std::vector<float> left;
    std::vector<float> right;
    bool armed = false;
    std::uint64_t born = 0;
    std::size_t latency = 0;
};

/// Prepare one freeze-enabled, blur-enabled engine. Shared by both clauses so
/// they cannot drift apart in geometry.
void prepareFreezeEngine(Krate::DSP::AtmosphereEngine& engine) {
    Krate::DSP::AtmosphereEngine::PrepareConfig cfg;
    cfg.captureSeconds = 8.0f;
    cfg.blurEnabled = true;
    cfg.freezeEnabled = true;
    cfg.blurFftSize = kFreezeBlurFftSize;
    cfg.freezeFftSize = kFreezeFftSize;
    cfg.maxBlockSamples = kBlockSamples;
    engine.prepare(kSampleRate, cfg);
    configureFreeze(engine);
}

/// Warm the ring, capture, then hold at freezeMix = 1 for the rest of the
/// render. The captured `armed` flag is what proves captureFreeze() took its
/// real path: both of its branches are otherwise silent.
[[nodiscard]] FreezeRender renderFreezeHold(const std::vector<float>& inLeft,
                                            const std::vector<float>& inRight) {
    FreezeRender out;
    out.left.assign(kFreezeStabilitySamples, 0.0f);
    out.right.assign(kFreezeStabilitySamples, 0.0f);

    Krate::DSP::AtmosphereEngine engine;
    prepareFreezeEngine(engine);
    out.latency = engine.getLatencySamples();

    for (std::size_t start = 0; start < kFreezeStabilitySamples; start += kBlockSamples) {
        if (start == kFreezeWarmupSamples) {
            engine.captureFreeze();
            out.armed = engine.isFreezeCaptured();
            engine.setFreezeMix(1.0f);
        }
        engine.processStereoBlock(inLeft.data() + start, inRight.data() + start,
                                  out.left.data() + start, out.right.data() + start,
                                  kBlockSamples);
    }

    out.born = engine.getTotalGrainsBorn();
    return out;
}

/// Warm the ring, capture, crossfade 0 -> 1, hold, then crossfade 1 -> 0 and
/// hold. Both transitions land on block boundaries.
[[nodiscard]] FreezeRender renderFreezeSweep(const std::vector<float>& inLeft,
                                             const std::vector<float>& inRight) {
    FreezeRender out;
    out.left.assign(kFreezeSweepSamples, 0.0f);
    out.right.assign(kFreezeSweepSamples, 0.0f);

    Krate::DSP::AtmosphereEngine engine;
    prepareFreezeEngine(engine);
    out.latency = engine.getLatencySamples();

    for (std::size_t start = 0; start < kFreezeSweepSamples; start += kBlockSamples) {
        if (start == kFreezeWarmupSamples) {
            engine.captureFreeze();
            out.armed = engine.isFreezeCaptured();
            engine.setFreezeMix(1.0f);
        } else if (start == kFreezeWarmupSamples + kFreezeSweepHoldSamples) {
            // Back to the grain layer. It never lapsed - there is no bypass at a
            // settled m = 1 (plan S12.3) - so this returns to a full grain
            // population rather than to one swelling back from empty.
            engine.setFreezeMix(0.0f);
        }
        engine.processStereoBlock(inLeft.data() + start, inRight.data() + start,
                                  out.left.data() + start, out.right.data() + start,
                                  kBlockSamples);
    }

    out.born = engine.getTotalGrainsBorn();
    return out;
}

// --- metrics -----------------------------------------------------------------

/// Peak |y| over [first, last), in dBFS. Floored so a silent window is a finite
/// number: no infinity may be formed anywhere in this TU.
[[nodiscard]] double freezePeakDb(const std::vector<float>& buffer, std::size_t first,
                                  std::size_t last) {
    double peak = 0.0;
    for (std::size_t i = first; i < last && i < buffer.size(); ++i) {
        peak = std::fmax(peak, std::fabs(static_cast<double>(buffer[i])));
    }
    return 20.0 * std::log10(std::fmax(peak, 1e-12));
}

}  // namespace

TEST_CASE("AtmosphereEngine_FreezeStability", "[atmosphere]") {
    // -------------------------------------------------------------------------
    // (0) The pinned input, and proof the generator produced it
    // -------------------------------------------------------------------------
    std::vector<float> inLeft(kFreezeStabilitySamples, 0.0f);
    std::vector<float> inRight(kFreezeStabilitySamples, 0.0f);
    makeHarmonicStack(inLeft.data(), inRight.data(), kFreezeStabilitySamples, kSampleRate);
    REQUIRE(rmsDb(inLeft, 0, kFreezeStabilitySamples) > -20.0);

    // -------------------------------------------------------------------------
    // (1) THE DRONE HOLDS ITS LEVEL
    // -------------------------------------------------------------------------
    const FreezeRender hold = renderFreezeHold(inLeft, inRight);

    INFO("hold render: armed = " << hold.armed << ", born = " << hold.born
                                 << ", latency = " << hold.latency);
    // The delay-matched leg must actually be in the path (FR-046 reports ONE
    // latency for both legs), the capture must have taken its real path, and
    // grains must have been admitted - a silent engine passes a peak-ratio
    // criterion trivially, which is what the floor clause below also guards.
    REQUIRE(hold.latency == kFreezeBlurFftSize);
    REQUIRE(hold.armed);
    REQUIRE(hold.born > 0u);

    std::vector<double> peakLeftDb(kFreezeWindows, 0.0);
    std::vector<double> peakRightDb(kFreezeWindows, 0.0);
    for (std::size_t k = 0; k < kFreezeWindows; ++k) {
        const std::size_t start = kFreezeWarmupSamples + k * kFreezeWindowSamples;
        peakLeftDb[k] = freezePeakDb(hold.left, start, start + kFreezeWindowSamples);
        peakRightDb[k] = freezePeakDb(hold.right, start, start + kFreezeWindowSamples);
    }

    // Window index 1 is spec.md's "peak(2)" - the SECOND window. See the case
    // banner for why window 0 is not the reference.
    const double referenceLeftDb = peakLeftDb[1];
    const double referenceRightDb = peakRightDb[1];

    INFO("peak(2): L = " << referenceLeftDb << " dBFS, R = " << referenceRightDb
                         << " dBFS (floor " << kFreezePeakFloorDb << ")");
    INFO("peak(1): L = " << peakLeftDb[0] << " dBFS, R = " << peakRightDb[0]
                         << " dBFS (the ramp + pre-fill window, NOT asserted)");
    // NON-SILENCE BEFORE ANY TOLERANCE: without this every window is -240 dBFS
    // and every |difference| below is 0.
    REQUIRE(referenceLeftDb >= kFreezePeakFloorDb);
    REQUIRE(referenceRightDb >= kFreezePeakFloorDb);

    double worstLeftDb = 0.0;
    double worstRightDb = 0.0;
    std::size_t worstWindow = 1;
    for (std::size_t k = 1; k < kFreezeWindows; ++k) {
        const double deltaLeft = std::fabs(peakLeftDb[k] - referenceLeftDb);
        const double deltaRight = std::fabs(peakRightDb[k] - referenceRightDb);
        if (std::fmax(deltaLeft, deltaRight) > std::fmax(worstLeftDb, worstRightDb)) {
            worstWindow = k;
        }
        worstLeftDb = std::fmax(worstLeftDb, deltaLeft);
        worstRightDb = std::fmax(worstRightDb, deltaRight);
    }

    INFO("worst peak deviation from peak(2): L = "
         << worstLeftDb << " dB, R = " << worstRightDb << " dB, at window index " << worstWindow
         << " (peak L = " << peakLeftDb[worstWindow] << " dBFS, R = " << peakRightDb[worstWindow]
         << " dBFS), tolerance " << kFreezePeakToleranceDb << " dB");
    REQUIRE(worstLeftDb <= kFreezePeakToleranceDb);
    REQUIRE(worstRightDb <= kFreezePeakToleranceDb);

    // -------------------------------------------------------------------------
    // (2) THE CROSSFADE IS CLEAN. 0 detections, using SC-003's pinned detector
    //     configuration and its calibrated sigma - both unchanged.
    // -------------------------------------------------------------------------
    const FreezeRender sweep = renderFreezeSweep(inLeft, inRight);

    INFO("sweep render: armed = " << sweep.armed << ", born = " << sweep.born);
    REQUIRE(sweep.armed);
    REQUIRE(sweep.born > 0u);

    const std::vector<float> detectLeft(sweep.left.begin() +
                                            static_cast<std::ptrdiff_t>(kFreezeSweepDetectStart),
                                        sweep.left.end());
    const std::vector<float> detectRight(sweep.right.begin() +
                                             static_cast<std::ptrdiff_t>(kFreezeSweepDetectStart),
                                         sweep.right.end());

    // NON-SILENCE, again before the threshold: ClickDetector reports 0
    // detections on silence, so this is what stops clause (2) passing on a
    // muted engine.
    const double sweepLeftDb = rmsDb(detectLeft, 0, detectLeft.size());
    const double sweepRightDb = rmsDb(detectRight, 0, detectRight.size());
    INFO("sweep RMS over the detected span: L = " << sweepLeftDb << " dBFS, R = " << sweepRightDb
                                                  << " dBFS");
    REQUIRE(sweepLeftDb > -60.0);
    REQUIRE(sweepRightDb > -60.0);

    const std::size_t clicksLeft = countClicks(detectLeft, kClickThresholdSigma);
    const std::size_t clicksRight = countClicks(detectRight, kClickThresholdSigma);

    // On a failure, report the smallest ladder sigma that clears the buffer, so
    // that investigating is a measurement rather than a guess. Raising
    // kClickThresholdSigma is NOT the remedy here: it is calibrated by SC-003's
    // measured false-positive floor and a step at the crossfade is a real
    // defect - the missing or unadvanced FR-052 delay is the first suspect.
    if (clicksLeft != 0u || clicksRight != 0u) {
        INFO("smallest zero-detection sigma: L = " << smallestZeroSigma(detectLeft)
                                                   << ", R = " << smallestZeroSigma(detectRight)
                                                   << " (in force: " << kClickThresholdSigma
                                                   << ")");
        INFO("max |dy| over the detected span: L = " << maxAbsDelta(detectLeft)
                                                     << ", R = " << maxAbsDelta(detectRight));
        FAIL("freezeMix 0 -> 1 -> 0 produced click detections: L = " << clicksLeft
                                                                    << ", R = " << clicksRight);
    }
    REQUIRE(clicksLeft == 0u);
    REQUIRE(clicksRight == 0u);
}

