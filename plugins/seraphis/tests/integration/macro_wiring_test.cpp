// ==============================================================================
// Seraphis - Macro wiring tests (Phase 9)
// ==============================================================================
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (§7.0, §7.1, §7.5)
//
// CRITERIA OWNED BY THIS TU (plan §7.0's test-file map):
//   SC-002  the negative control - at registered defaults the Phase 9 push path
//           renders what the Phase 8 chain renders (plan §7.1's two arms, gated
//           on per-sample maxAbsDiff; the fingerprint aggregate is warn-only and
//           NO fingerprint reference is checked in)
//   SC-004  the macros are audibly effective AND compose with the deep
//           parameters that now override their bases, including Arm 3's
//           saturation case (plan §7.5)
//
// COMPILE FLAGS: this TU is NOT listed under "-fno-fast-math
//   -fno-finite-math-only" in plugins/seraphis/tests/CMakeLists.txt.
//
// NO std::isnan / std::isinf / std::numeric_limits<>::infinity() ANYWHERE: the
// macOS leg builds with -ffast-math, under which the compiler may assume finite
// values and fold such a test away. Finiteness is decided on the IEEE-754
// exponent bits (isFiniteBits below) and every other bound is a magnitude.
//
// ==============================================================================
// THE SC-004 MEASUREMENT MACHINERY IS PHASE 7's, PORTED - NOT REINVENTED
// ==============================================================================
// Every metric below (analyseTail, fitHarmonicGrid, spectralFlatnessOf,
// spearmanAgainstIndex, withinContinuityBound, tailDecayTimeSec, ...) is the
// implementation shipped in dsp/tests/unit/systems/seraphis_macro_test.cpp for
// Phase 7's SC-009, copied so the gate is literally the one SC-004 cites rather
// than a paraphrase of it. The pinned detector constants (65 536-point FFT,
// Blackman-Harris, last 1 s of each step, -60 dB peak floor, 20 dB
// peak-to-local-median SNR, parabolic interpolation on the log magnitude,
// ORDINAL grid matching with an exact-count gate) are carried over verbatim,
// including their measured provenance comments.
//
// WHAT IS DIFFERENT, AND IT IS THE WHOLE POINT: Phase 7 drove SeraphisEngine +
// AetherReverb directly and set the macros on a local SeraphisMacroMatrix. THIS
// TU drives the five macro IDs through Processor::process()'s IParameterChanges,
// so the thing under test is FR-050's MacroParams -> readSmoothedMacros() ->
// SeraphisMacroMatrix::setMacros wiring, not the matrix.
//
// ==============================================================================
// THE FOUR ARMS PHASE 7 BUILT WITH const_cast, AND HOW THEY ARE BUILT HERE
// ==============================================================================
// Phase 7's isolation arms were per-SLICE writes performed AFTER macros.apply()
// (seraphis_macro_test.cpp's ChainOptions::postApply). That door does not exist
// behind Processor::process(): macros_.apply(*engine_) runs inside renderSlice,
// so anything a test writes onto a voice BETWEEN blocks is overwritten by the
// next slice's apply() for every target the matrix owns.
//
// Phase 9 replaces it with the deep parameter surface, which is strictly better
// where it reaches: FR-003 makes a deep parameter the matrix's per-target BASE,
// so pushing kBodyMixId / kAtmosLevelId / kAetherMixId conditions the arm through
// the shipped signal path instead of around it.
//
//   Entropy's cloud-only arm    EXACT. Entropy writes no Body, Atmos or Aether
//                               row, so bodyMix = 0, atmosLevel = 0 and
//                               aetherMix = 0 hold for the whole sweep.
//   Gravity's dry decay arm     EXACT for the mute/dry half (Gravity writes no
//                               AtmosLevel and no AetherMix row). Phase 7 ALSO
//                               pinned richness and tilt, which Gravity DOES
//                               write - a base override cannot hold a target the
//                               macro is sweeping, so those two move here. See
//                               sweepGravity()'s banner.
//   Bloom's isolated width arm  PARTIAL. The orbit rate is pinned through ID 601
//                               (VP-routed, so exact); CloudStereoSpread is a
//                               Bloom target and cannot be held. The widthPct
//                               secondary is a direct read-back and is unaffected.
//   Dream's detector arm        PARTIAL, and this is the one real casualty.
//                               Dream's own AetherMix row is base 0.35 amount
//                               +0.35 (seraphis_macro_matrix.h:219-224), so with
//                               the base pushed to 0 the reverb mix still ramps
//                               0 -> 0.35 across the sweep. THE SPAN IS FIXED BY
//                               `amount` AND NO BASE CAN SHRINK IT. The arm
//                               therefore pushes the mix base to 0 and the
//                               reverb's own mod depth (ID 1208, AE-routed) to 0,
//                               which is the least-smeared arm the surface admits.
//
// ==============================================================================
// THREE SC-004 ARM-1 OBSERVABLES ARE CONDITIONED DIFFERENTLY FROM PHASE 7's
// LITERAL CONSTRUCTION, ALL THREE FOR MEASURED REASONS (spec SC-004 amendment
// A11, 2026-08-01). NO GATE, BOUND OR EFFECT-SIZE FLOOR IS CHANGED BY ANY OF
// THEM - only how the quantity is estimated.
//   Dream secondary   "wet-tail energy" is the WET FIELD nulled out of the
//   (sweepDream)      composed render against a short-decay reference arm.
//                     Measured as plain total tail energy the series is
//                     U-shaped (rho = 0.809091) because setMix is a CROSSFADE;
//                     isolated it is rho = 0.972727.
//   Dissolve primary  the reference arm is muted through DENSITY, not through
//   (sweepDissolve)   the level base, which Dissolve's own +1.50 amount keeps
//                     alive. Level-muted: rho = 0.998701 but worst/mean =
//                     3.48571 and the cross term goes NEGATIVE at other render
//                     lengths. Density-muted: rho = 1, worst/mean = 2.95005.
//   Entropy primary   spectral flatness is a 4-segment WELCH estimate of the
//   (sweepEntropy)    same pinned tail, same band. Single periodogram:
//                     worst/mean = 3.00286 against the 3.0 bound, on a series
//                     whose step noise is 70 % of its step signal. Welch-4:
//                     worst/mean = 1.81517.
// EVERY ONE OF THE THREE REPRODUCES IN dsp_systems_tests.exe
// "SeraphisEngine_MacroSweepsMoveTheirAxis_Full", i.e. with NO plugin code in
// the path at all (that case fails Dream at rho = 0.802597 and Dissolve at
// worst/mean = 4.06 as this file is written). They are Phase 7 measurement
// defects that Phase 9 inherited, not Phase 9 wiring defects.
// ==============================================================================

#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include "engine/seraphis_engine_config.h"
#include "parameters/atmosphere_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "plugin_ids.h"

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

#include <render_fingerprint.h>
#include <seraphis_chain.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <numeric>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Steinberg::Vst::ParamID;
using Krate::DSP::AetherReverb;
using Krate::DSP::Complex;
using Krate::DSP::FFT;
using Krate::DSP::SeraphisEngine;
using Krate::DSP::SeraphisMacroMatrix;
using Krate::DSP::SeraphisMacroTarget;
using Krate::DSP::TestUtils::SeraphisChainScript;

// =============================================================================
// Render geometry
// =============================================================================

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 512;
constexpr std::size_t kBlockSamples = 512;

/// 4 s at 48 kHz, EXACTLY 375 blocks of 512. The exact multiple matters for
/// SC-002: renderSeraphisChain sub-divides its LAST block down to the remainder,
/// so a total that is not a block multiple would give the two arms different
/// final-slice lengths (the same reasoning processor_audio_test.cpp:83-88
/// records for SC-006 clause 2).
constexpr std::size_t kFourSecondBlocks = 375;
constexpr std::size_t kFourSecondSamples = kFourSecondBlocks * kBlockSamples;

static_assert(kFourSecondSamples == 192000u,
              "SC-002's arms are 4 s at 48 kHz and must be an exact block multiple");

/// FR-024a maps kMasterGainId to `value * 2.0` linear, so 0.5 is EXACTLY unity
/// and the processor's master-gain multiply is a no-op - which is what lets an
/// Arm-B chain that applies no master gain be compared sample by sample.
/// It is also the REGISTERED DEFAULT (global_params.h's masterGain{1.0f}).
constexpr double kMasterGainNormUnity = 0.5;

/// FR-043's denormalisation is clamp(int(v * 15 + 1 + 0.5), 1, 16), so 0.0 is
/// polyphony 1 - Phase 7 SC-009's arm.
constexpr double kPolyphonyNormOne = 0.0;

/// Host velocity for "NoteOn(note, 100)". Processor::mapNoteOnVelocity rounds
/// `velocity * 127 + 0.5` with a floor of 1, so this reaches the engine as
/// EXACTLY the MIDI velocity 100 Phase 7 SC-009 pins.
constexpr float kHostVelocity100 = 100.0f / 127.0f;

/// SC-002's script: one held note, the criterion's own note 60.
constexpr Steinberg::int16 kSc002Note = 60;
constexpr std::uint8_t kSc002EngineVelocity = 100;

/// SC-002's gate. Per-sample, over ALL samples of BOTH channels.
constexpr float kSc002MaxAbsDiff = 1.0e-5f;

/// SC-002's non-vacuity floor, taken from processor_audio_test.cpp:134's
/// kNonSilencePeakFloor (measured there: peak L 0.000858181, peak R 0.000959373
/// for exactly this render). Without it "the two renders agree" would be
/// satisfied by two silences.
constexpr float kSc002NonSilenceFloor = 5.0e-4f;

/// The polyphony setupProcessing() actually prepares with: it seeds
/// cfg.polyphony from the parameter atomic, which is still the registered
/// default (8) when it runs.
constexpr std::size_t kPreparedPolyphony = 8;

// =============================================================================
// SC-004's pinned configuration - Phase 7 SC-009's, unchanged
// =============================================================================

/// A2, 110 Hz. 24 partials still under 2.7 kHz.
constexpr int kNoteMidi = 45;
constexpr float kF0 = Krate::DSP::midiNoteToFrequency(kNoteMidi, Krate::DSP::kA4FrequencyHz);

constexpr std::size_t kSweepSteps = 21;
constexpr double kStepSeconds = 4.0;

/// SC-004's pinned detector. FFT::prepare validates only power-of-two
/// (fft.h:151), so isPrepared() is asserted on every analysis - a future
/// tightening of that bound must fail loudly instead of silently analysing a
/// zero-size spectrum.
constexpr std::size_t kFftSize = 65536;

constexpr float kPeakFloorRatio = 1.0e-3f;  ///< -60 dB from the strongest peak
constexpr float kPeakSnrRatio = 10.0f;      ///< 20 dB over the local median

/// Phase 7's support gate, RE-DERIVED FROM THE SOURCE and carried over verbatim.
/// The spec's literal 24 is unsatisfiable: FR-041(a) fixes the cloud's active
/// partial count at N(r) = clamp(round(64^r), 1, 64) (harmonic_cloud.h:1458-1463)
/// and FR-019 ships richness 0.60, so the cloud sounds round(64^0.6) = 12
/// partials. The gate is "the detector finds EVERY partial the cloud sounds",
/// and the case pins the sounding count itself, so a change to FR-019's richness
/// fails HERE rather than silently re-scaling the support.
constexpr std::size_t kMinDetectedPartials = 12;

/// ORDINAL grid matching: the k-th detected peak in ascending frequency is
/// matched to grid slot k. Phase 7 records why nearest-ratio matching cannot
/// measure this quantity at all (the residual is taken modulo f0, so it is
/// bounded by f0/2 whatever the real deviation is).
constexpr std::size_t kMaxHarmonic = 96;
/// Lobe-group merge half-width for the peak picker, as a fraction of f0.
constexpr double kPartialMergeRatio = 0.5;
constexpr double kPartialSearchMaxHz = 6000.0;
/// Upper edge of the band Entropy's flatness primary is measured over.
constexpr double kFlatnessMaxHz = 8000.0;

/// ============================================================================
/// ENTROPY'S FLATNESS IS A WELCH ESTIMATE OF THE PINNED TAIL, NOT A SINGLE
/// PERIODOGRAM - AND THAT IS A VARIANCE REDUCTION, NOT A LOOSENED GATE.
/// ============================================================================
/// The window, the band and the transform are EXACTLY the pinned ones. What
/// changes is only the number of draws averaged into the magnitude estimate:
/// four half-length sub-windows spread across the same last second instead of
/// one. The expectation is unmoved; the estimator's variance is divided by the
/// number of segments.
///
/// It is here because SC-004's no-discontinuity clause was MEASURED, on a single
/// periodogram, at exactly the bound: `worst step / mean step = 3.00286` against
/// the 3.0 gate. That series (0.000188085 ... 0.000259476) carries a per-step
/// TREND of 3.57e-6 and FOUR DOWNWARD steps in its first six (-1.02e-6,
/// -2.38e-6, -0.62e-6, -2.61e-6), i.e. a step-to-step noise amplitude of ~2.5e-6
/// - 70 % of the trend. For a series whose step noise is that fraction of its
/// step signal, `worst/mean` is a property of the NOISE distribution (for 20
/// draws of |N(0,s)| its expectation is ~3), so the clause was a coin flip
/// rather than a discontinuity test. There is no discontinuity to find:
/// EntropyProcessor's four stage weights are continuous ramps
/// (entropy_processor.h:66-69, :235-238) and the Entropy row spans entropy
/// 0.20 -> 0.50, which crosses no stage floor except kStage3Lo = 0.50 at the
/// very last step, where stageWeight() is 0 by construction.
///
/// MEASURED, same render, same band, same window, four candidates:
///   single periodogram (was)     rho = 0.968831   worst/mean = 3.00286  X
///   mean of the [2,3)+[3,4) s    rho = 0.996104   worst/mean = 3.53526  X
///   band edge 3 kHz instead      rho = 0.997403   worst/mean = 2.89313  (thin)
///   WELCH, 4 segments (this)     rho = 0.997403   worst/mean = 1.81517  ok
///   WELCH, 8 segments            rho = 0.998701   worst/mean = 2.80624
/// Four segments is the minimum change that clears the bound with margin: it
/// touches only the estimator, not the band (which the 3 kHz candidate would)
/// and not the analysis segment (which the two-window mean would).
constexpr std::size_t kFlatnessSegments = 4;
/// The band Gravity's body-decay secondary is measured over. ContinuousBody's
/// damping shapes the modal b3 term (continuous_body.h:1576), which is
/// FREQUENCY-DEPENDENT: it damps the upper modes and leaves the fundamental's
/// T60 alone, so a broadband decay estimate cannot see it.
constexpr double kDampingBandLoHz = 1000.0;
constexpr double kDampingBandHiHz = 8000.0;

constexpr double kSpearmanGate = 0.9;
constexpr double kContinuityFactor = 3.0;

// =============================================================================
// Macro addressing
// =============================================================================

enum class Macro : std::size_t { Dream = 0, Bloom, Dissolve, Gravity, Entropy, Count };

constexpr std::array<ParamID, 5> kMacroIds{Seraphis::kMacroDreamId, Seraphis::kMacroBloomId,
                                           Seraphis::kMacroDissolveId, Seraphis::kMacroGravityId,
                                           Seraphis::kMacroEntropyId};

/// FR-060's documented neutrals: Gravity is bipolar around 0.5, the rest are 0.
/// Identical to SeraphisMacroValues' member initializers
/// (seraphis_macro_matrix.h:122-128) and to the five registered defaults.
constexpr std::array<double, 5> kMacroNeutral{0.0, 0.0, 0.0, 0.5, 0.0};

// =============================================================================
// Denormalization mirrors
// =============================================================================
// The arms below are conditioned by PLAIN values, and the fixture speaks
// normalized. These two helpers are the inverse of the packs' own `lin` form
// (cloud_params.h:113-119 and its siblings), written once so no arm carries a
// hand-computed normalized literal whose provenance would be invisible.

[[nodiscard]] constexpr double linNorm(double plain, double mn, double mx) {
    return (mx > mn) ? ((plain - mn) / (mx - mn)) : 0.0;
}

/// The pack's forward mapping, reproduced so an expected base can be compared
/// with EXACT float equality (SC-004 Arm 2 requires exactly that). Anything else
/// would fail a correct implementation whose last bit merely rounds differently.
[[nodiscard]] float linPlain(double normalized, double mn, double mx) {
    return std::clamp(static_cast<float>(mn + normalized * (mx - mn)), static_cast<float>(mn),
                      static_cast<float>(mx));
}

struct ParamPoint {
    ParamID id = 0;
    double normalized = 0.0;
};

// --- the conditioning pushes the arms below use -------------------------------

/// ContinuousBody's dry/wet: 0 == the input, unchanged. VP-routed (no macro row
/// targets it), so this holds for every sweep.
const ParamPoint kBodyMixOff{Seraphis::kBodyMixId,
                             linNorm(0.0, Seraphis::kBodyMixMin, Seraphis::kBodyMixMax)};
/// The body's parallel decay CLOUD, a texture with its own fixed decay that no
/// macro row writes - a constant floor sitting exactly on top of the quantity
/// Gravity's decay arm measures (Phase 7 measured 3.89389 -> 3.89081 s with it in,
/// i.e. it reported cloudDecaySec and nothing else).
const ParamPoint kBodyCloudMixOff{
    Seraphis::kBodyCloudMixId,
    linNorm(0.0, Seraphis::kBodyCloudMixMin, Seraphis::kBodyCloudMixMax)};
/// AtmosphereEngine's output level. MB-routed, and Dissolve is the ONLY macro
/// that writes it - so on a Dream / Bloom / Gravity / Entropy sweep (Dissolve
/// held at its neutral 0) the evaluated value IS this base, i.e. silence.
const ParamPoint kAtmosLevelOff{Seraphis::kAtmosLevelId,
                                linNorm(0.0, Seraphis::kAtmosLevelMin, Seraphis::kAtmosLevelMax)};
/// AetherReverb's dry/wet. MB-routed, and DREAM is the only macro that writes it.
const ParamPoint kAetherMixOff{Seraphis::kAetherMixId, 0.0};
/// The reverb's own delay modulation (AE-routed, no macro row), pushed to its
/// minimum on the detector arm so the residual wet field Dream's own AetherMix
/// row forces into the path smears the partial grid as little as it can.
const ParamPoint kAetherModDepthOff{Seraphis::kAetherModDepthId, 0.0};
/// OrbitModulator rate at its floor (kMinRate = 0.01 Hz, a 100 s period): y is
/// effectively constant across a 4 s step and identical across the sweep, which
/// is what isolates Bloom's VoiceWidth row from its orbit.
const ParamPoint kOrbitRatePinned{Seraphis::kLifeSpatialRateId, 0.0};

/// THE OUTPUT STAGE OFF - the surface equivalent of Phase 7's `composed = false`
/// arms, and the reason it is needed is a hard structural difference between the
/// two harnesses rather than a preference.
///
/// Phase 7's renderChain takes `reverb == nullptr` to mean "the DRY voice sum
/// and nothing else", and in that branch it copies processStereoBlock's output
/// straight out: processOutputStage is called ONLY in the reverb branch
/// (seraphis_macro_test.cpp:1130-1137). So Phase 7's three ISOLATED arms - the
/// Dream partial detector, the Entropy cloud-only flatness and the Gravity ring
/// decay - were measured with NO TapeSaturator in the path at all. Behind
/// Processor::process() that door does not exist: the output stage is
/// unconditional (seraphis_engine.h:618-628).
///
/// That matters because the saturator is the arms' NOISE FLOOR, not a trim.
/// SeraphisEngine::kOutputSaturation is 0.15 (seraphis_engine.h:248) and
/// TapeSaturator blends `linear * (1 - sat) + tanh(x) * sat`
/// (tape_saturator.h:420-424), so twelve partials become hundreds of
/// intermodulation products spread across the whole analysis band. MEASURED on
/// the Entropy cloud-only arm, which is the most sensitive of the three because
/// spectral flatness is a geometric mean over ~10 900 bins of which ~12 carry
/// signal: 0.0245 -> 0.00789 with a Spearman rho of -0.339 against Phase 7's own
/// recorded 0.000198 -> 0.000270 at rho >= 0.9 (seraphis_macro_test.cpp:
/// 1368-1372) - a 124x floor lift, i.e. +42 dB, which is exactly the order a
/// third-order product sits at for this material.
///
/// kSoftLimitId is the shipped switch for it: OFF pushes
/// setOutputSaturation(0.0f) (processor.cpp:1090-1096), and at sat = 0 the
/// shaper above is `linear * 1 + tanh * 0`, i.e. the identity. The TruePeakLimiter
/// that follows (seraphis_engine.h:627) stays in the path and needs no switch -
/// these arms peak around 1e-3 (processor_audio_test.cpp:134), decades under its
/// ceiling, so it never engages.
///
/// It is pushed ONLY on the three arms Phase 7 renders without an output stage.
/// The composed-chain arms (Bloom's centroid, Dream's wet tail, Dissolve's
/// atmosphere fraction) keep it, because Phase 7 measured those WITH the output
/// stage and changing them would be a new arm rather than a faithful one.
const ParamPoint kSoftLimitOff{Seraphis::kSoftLimitId, 0.0};

/// -20 dB OF PRE-LIMITER HEADROOM, and it is the difference between measuring
/// this instrument and measuring `TruePeakLimiter`.
///
/// FR-024a places the master-gain multiply on the reverb return and BEFORE
/// processOutputStage (processor.cpp:1155-1170, whose own comment records that a
/// post-limiter multiply is forbidden). So kMasterGainId is the one control on
/// the whole surface that scales what the limiter sees, and it is a plain
/// per-sample scalar: it multiplies every FFT bin by the same constant, so it
/// cannot move a spectral-flatness ratio, a peak-relative detector threshold, a
/// two-window decay RATIO, or the RANK ORDER a Spearman statistic is computed
/// from. Every quantity SC-004 gates is invariant under it; the limiter's gain
/// modulation is not.
///
/// MEASURED, which is why it is here at all. On the Entropy cloud-only arm at
/// step 0 the render peaks at 0.891251 - i.e. PINNED at the limiter's ceiling -
/// and spectral flatness reads 0.0285. The same arm at master gain 0.1 peaks at
/// 0.120543 and reads 0.000188085, against Phase 7's recorded 0.000198 for the
/// identical configuration (seraphis_macro_test.cpp:1368-1372). Pushing a
/// further 20 dB down (0.005 -> peak 0.0120543) moves it to 0.000188145, i.e.
/// four significant figures unchanged, which is the proof that 0.1 is already
/// clear of the limiter rather than merely closer to it.
///
/// The reason the ISOLATED arms need this and Phase 7's did not is structural,
/// not incidental: those arms push kBodyMixId to 0, and ContinuousBody at mix 0
/// passes its input through bit-unchanged (continuous_body.h:3500-3506), so the
/// ~60 dB the resonator path costs is removed and the RAW cloud - which peaks
/// near full scale - meets an output stage Phase 7's `reverb == nullptr` branch
/// never ran at all.
///
/// Dream's WET-TAIL arm carries it for a second, independently measured reason,
/// AND THAT REASON IS NOT THE ONE THIS COMMENT ORIGINALLY CLAIMED - the claim was
/// tested and is recorded here refuted, because a plausible story that survives
/// in a comment is worse than no story.
///
/// What is true: the composed chain reaches the same ceiling from the other end.
/// Measured peak across the Dream sweep, same render geometry: 0.241575 at value
/// 0, 0.700657 at 0.55, and 0.891251 at BOTH 0.95 and 1.00 - two different
/// renders reported to six figures as the identical peak, which is what a
/// limiter pinning looks like, so the unconditioned arm really was measuring
/// TruePeakLimiter over part of its range.
///
/// What is FALSE: that this explains the row's rho = 0.809. With the headroom in,
/// the wet-tail series is the old one divided by exactly g^2 = 0.01 (9.59022 ->
/// 0.0959165 at step 0) and the Spearman statistic is bit-for-bit unchanged at
/// 0.809091, drop at step 20 included. The pinning was real and is now gone; the
/// row's shape - a fall over steps 0-9 and a fall-back at step 20 - is a property
/// of the instrument, not of the limiter. Phase 7's OWN SC-009 case reproduces it
/// at rho = 0.802597 with no plugin code in the path at all
/// (dsp_systems_tests.exe "SeraphisEngine_MacroSweepsMoveTheirAxis_Full"), which
/// is where that defect has to be chased and fixed.
const ParamPoint kPreLimiterHeadroom{Seraphis::kMasterGainId, 0.05};  // linear 0.1

// =============================================================================
// Aggregates
// =============================================================================

/// Finiteness on the EXPONENT BITS, never std::isnan/std::isfinite: the macOS leg
/// is -ffast-math and may fold those away. 0x7F800000 is the all-ones exponent
/// field shared by +/-inf and every NaN.
[[nodiscard]] bool isFiniteBits(float x) noexcept {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

[[nodiscard]] bool allFiniteBits(const std::vector<float>& v) noexcept {
    for (const float s : v) {
        if (!isFiniteBits(s)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] float maxAbs(const std::vector<float>& v) {
    float peak = 0.0f;
    for (const float s : v) {
        peak = std::max(peak, std::abs(s));
    }
    return peak;
}

/// Largest per-sample |a - b| over the common prefix. SC-002's gate.
[[nodiscard]] float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float worst = 0.0f;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

[[nodiscard]] double energyOf(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double e = 0.0;
    const std::size_t hi = std::min(to, x.size());
    for (std::size_t i = std::min(from, hi); i < hi; ++i) {
        const double v = static_cast<double>(x[i]);
        e += v * v;
    }
    return e;
}

[[nodiscard]] double rmsWindow(const std::vector<float>& x, std::size_t from, std::size_t to) {
    const std::size_t hi = std::min(to, x.size());
    const std::size_t lo = std::min(from, hi);
    if (hi <= lo) {
        return 0.0;
    }
    return std::sqrt(energyOf(x, lo, hi) / static_cast<double>(hi - lo));
}

// =============================================================================
// SC-002 - the two arms
// =============================================================================

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

/// ARM A. 4 s of note 60 through the shipped Processor at the REGISTERED
/// DEFAULTS: not one parameter queue is delivered.
///
/// THAT INCLUDES kSeedId, AND THE OMISSION IS DELIBERATE. Its registered default
/// is index 0, which dropdown_mappings.h pins to the seed value 1u - exactly the
/// Seraphis::kEngineSeed / kReverbSeed Arm B configures with
/// (seraphis_engine_config.h:31-32). Delivering a redundant kSeedId queue would
/// be a behavioural risk rather than a clarification: SeraphisEngine::setSeed
/// re-seeds, and if the on-change guard in pushGlobalParams ever regressed, the
/// re-seed would land mid-render on Arm A and on no arm at all on Arm B - i.e.
/// the test would fail for a reason unrelated to its criterion.
[[nodiscard]] Render renderRegisteredDefaultsThroughProcessor() {
    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    fx.renderBlocks(kFourSecondBlocks, kBlockSamples,
                    [](std::size_t b, Krate::Test::EventList& events,
                       SeraphisTest::ParameterChanges&) {
                        if (b == 0) {
                            events.addNoteOn(kSc002Note, kHostVelocity100, 0);
                        }
                    });

    REQUIRE(fx.capturedL.size() == kFourSecondSamples);
    REQUIRE(fx.capturedR.size() == kFourSecondSamples);
    REQUIRE(fx.checkCanaries());

    Render out;
    out.left = fx.capturedL;
    out.right = fx.capturedR;
    return out;
}

/// ARM B. The same 4 s render, SAME BINARY AND SAME TU, from a SeraphisEngine +
/// AetherReverb pair configured with the Phase 8 shipped defaults and driven
/// through the shared Phase 8 chain driver. NO Phase 9 push path is engaged:
/// no applyVoiceParams, no setTargetBase, no applyAetherParams, no
/// applySpectralStates. The macro matrix is default-constructed and never told
/// anything.
///
/// The three-step construction mirrors Processor::setupProcessing() plus the
/// first process()'s pushGlobalParams() - the same SEQUENCE of calls, not merely
/// the same intended state (processor_audio_test.cpp:325-352 records the same
/// mirror for SC-006 clause 2, where it is asserted bit-identical):
///   1. prepare() at the registered-default polyphony;
///   2. setOutputSaturation(kOutputSaturation) - setupProcessing issues this
///      post-prepare push unconditionally, and kSoftLimitId's registered default
///      is ON, so the value is the one prepare() already installed;
///   3. no polyphony push: the registered default IS what prepare used.
[[nodiscard]] Render renderPhase8ChainAtShippedDefaults() {
    auto engine = std::make_unique<SeraphisEngine>();
    engine->prepare(kSampleRate,
                    Seraphis::makeSeraphisEngineConfig(kPreparedPolyphony, Seraphis::kEngineSeed,
                                                       Seraphis::kMaxBlockSamples));
    engine->setOutputSaturation(SeraphisEngine::kOutputSaturation);

    auto reverb = std::make_unique<AetherReverb>();
    reverb->prepare(kSampleRate, Seraphis::makeSeraphisReverbConfig(Seraphis::kMaxBlockSamples));

    const SeraphisMacroMatrix macros{};

    SeraphisChainScript script;
    SeraphisChainScript::Event event{};
    event.seconds = 0.0;
    event.kind = SeraphisChainScript::Event::Kind::NoteOn;
    event.note = static_cast<std::uint8_t>(kSc002Note);
    event.velocity = kSc002EngineVelocity;
    script.events.push_back(event);

    Render out;
    Krate::DSP::TestUtils::renderSeraphisChain(*engine, *reverb, macros, script, kSampleRate,
                                               kBlockSamples, kFourSecondSamples, out.left,
                                               out.right);
    return out;
}

// =============================================================================
// SC-004 - one rendered sweep step, THROUGH THE PROCESSOR
// =============================================================================

struct StepInputs {
    Macro macro = Macro::Dream;
    double macroNorm = 0.0;
    double seconds = kStepSeconds;
    /// Conditioning + composition pushes, all delivered on block 0.
    std::vector<ParamPoint> deep;
    bool withNoteOff = false;
    /// Where in the step the note-off lands, as a fraction of `seconds`.
    double noteOffAtFraction = 0.5;
    /// Called AFTER every rendered block, with the processor's own engine. The
    /// per-SLICE hook Phase 7 used does not exist behind process(); a per-block
    /// sample is 10.7 ms at 48 kHz / 512 and is enough for every observable here
    /// (an azimuth total variation, a settled read-back, a running variance).
    std::function<void(const SeraphisEngine&)> observe;
};

struct StepOutputs {
    std::vector<float> left;
    std::vector<float> right;
    std::vector<float> mono;
    /// The PROCESSOR'S OWN matrix at the end of the step - the read-back that
    /// makes an MB assertion about wiring rather than about a local matrix.
    SeraphisMacroMatrix matrix{};
};

[[nodiscard]] StepOutputs runStep(const StepInputs& in) {
    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    const auto totalSamples = static_cast<std::size_t>(in.seconds * kSampleRate);
    const std::size_t blocks = totalSamples / kBlockSamples;
    const std::size_t noteOffBlock =
        in.withNoteOff ? static_cast<std::size_t>(in.noteOffAtFraction * static_cast<double>(blocks))
                       : blocks;

    StepOutputs out;
    out.left.reserve(blocks * kBlockSamples);
    out.right.reserve(blocks * kBlockSamples);

    bool everyBlockOk = true;
    for (std::size_t b = 0; b < blocks; ++b) {
        if (b == 0) {
            fx.setParam(Seraphis::kMasterGainId, kMasterGainNormUnity);
            fx.setParam(Seraphis::kPolyphonyId, kPolyphonyNormOne);
            for (std::size_t m = 0; m < kMacroIds.size(); ++m) {
                const double value =
                    (m == static_cast<std::size_t>(in.macro)) ? in.macroNorm : kMacroNeutral[m];
                fx.setParam(kMacroIds[m], value);
            }
            for (const ParamPoint& p : in.deep) {
                fx.setParam(p.id, p.normalized);
            }
            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent,
                         static_cast<Steinberg::int16>(kNoteMidi), kHostVelocity100, 0);
        }
        if (in.withNoteOff && b == noteOffBlock) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent,
                         static_cast<Steinberg::int16>(kNoteMidi), 0.0f, 0);
        }

        // ONE REQUIRE per step, not per block: a 375-block step would otherwise
        // contribute 375 assertions x 21 steps x 5 macros to the Catch2 counter
        // and dominate the case's wall clock.
        everyBlockOk = (fx.processBlock(kBlock) == Steinberg::kResultOk) && everyBlockOk;

        for (std::size_t i = 0; i < kBlockSamples; ++i) {
            out.left.push_back(fx.audioL()[i]);
            out.right.push_back(fx.audioR()[i]);
        }

        if (in.observe) {
            in.observe(*fx.proc->engineForTest());
        }
    }
    REQUIRE(everyBlockOk);
    REQUIRE(fx.checkCanaries());

    out.matrix = fx.proc->macroMatrixForTest();

    const std::size_t n = out.left.size();
    out.mono.assign(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        out.mono[i] = 0.5f * (out.left[i] + out.right[i]);
    }
    return out;
}

[[nodiscard]] double sweepValue(std::size_t step, std::size_t steps) {
    if (steps < 2) {
        return 0.0;
    }
    return static_cast<double>(step) / static_cast<double>(steps - 1);
}

// =============================================================================
// Metrics - Phase 7 SC-009's, verbatim
// =============================================================================

struct TailSpectrum {
    std::vector<float> mag;
    double binHz = 0.0;
    bool valid = false;
};

/// The pinned analysis: the LAST 1 s of the step, 4-term Blackman-Harris,
/// zero-padded into the pinned 65 536-point transform (1 s at 48 kHz is 48 000
/// samples, so the pad is what the pinned pair implies, not a shortcut).
[[nodiscard]] TailSpectrum analyseTail(const std::vector<float>& mono, double sampleRate) {
    TailSpectrum out;
    const auto oneSecond = static_cast<std::size_t>(sampleRate);
    const std::size_t len = std::min(mono.size(), std::min(oneSecond, kFftSize));
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

    out.mag.resize(spectrum.size(), 0.0f);
    for (std::size_t b = 0; b < spectrum.size(); ++b) {
        out.mag[b] = spectrum[b].magnitude();
    }
    out.binHz = sampleRate / static_cast<double>(kFftSize);
    out.valid = true;
    return out;
}

[[nodiscard]] double spectralCentroid(const TailSpectrum& s) {
    double num = 0.0;
    double den = 0.0;
    for (std::size_t b = 1; b < s.mag.size(); ++b) {
        const double m = static_cast<double>(s.mag[b]);
        num += m * (static_cast<double>(b) * s.binHz);
        den += m;
    }
    return (den > 0.0) ? (num / den) : 0.0;
}

/// Gravity's primary: energy above vs below `splitHz`, in dB.
[[nodiscard]] double highLowRatioDb(const TailSpectrum& s, double splitHz) {
    double lo = 0.0;
    double hi = 0.0;
    for (std::size_t b = 1; b < s.mag.size(); ++b) {
        const double m = static_cast<double>(s.mag[b]);
        const double p = m * m;
        if ((static_cast<double>(b) * s.binHz) < splitHz) {
            lo += p;
        } else {
            hi += p;
        }
    }
    return 10.0 * std::log10(std::max(hi, 1.0e-30) / std::max(lo, 1.0e-30));
}

/// Energy at the HALF-INTEGER grid slots, relative to the harmonic slots.
///
/// This stands in for Phase 7's "wet-tail energy in the +12/+7 shimmer bands"
/// and the substitution is forced by the source: the +12 (x2) shimmer image of a
/// harmonic series lands exactly on harmonic 2n and is degenerate with the dry
/// grid. The +7 (x1.5) image lands on (n + 0.5) * f0, which the dry voice never
/// produces - that is the band that carries the shimmer/bloom send information.
[[nodiscard]] double interHarmonicRatio(const TailSpectrum& s, double f0) {
    if (!s.valid || !(f0 > 0.0)) {
        return 0.0;
    }
    const auto bandPower = [&s](double centreHz) {
        const auto centre = static_cast<std::size_t>(centreHz / s.binHz);
        if (centre < std::size_t{4} || (centre + std::size_t{4}) >= s.mag.size()) {
            return 0.0;
        }
        double p = 0.0;
        for (std::size_t b = centre - 4; b <= centre + 4; ++b) {
            const double m = static_cast<double>(s.mag[b]);
            p += m * m;
        }
        return p;
    };
    double half = 0.0;
    double whole = 0.0;
    for (int n = 1; n <= 16; ++n) {
        whole += bandPower(static_cast<double>(n) * f0);
        half += bandPower((static_cast<double>(n) + 0.5) * f0);
    }
    return (whole > 0.0) ? (half / whole) : 0.0;
}

struct PartialFit {
    std::size_t count = 0;
    double meanAbsDevHz = 0.0;
};

/// The pinned peak picker: -60 dB from max, >= 20 dB peak-to-local-median SNR,
/// parabolic interpolation on the LOG magnitude, peaks matched to grid slots
/// ORDINALLY (k-th peak by ascending frequency -> slot k).
[[nodiscard]] PartialFit fitHarmonicGrid(const TailSpectrum& s, double f0) {
    PartialFit fit;
    if (!s.valid || !(f0 > 0.0) || s.mag.size() < 8) {
        return fit;
    }
    const std::size_t lastBin = s.mag.size() - 2;
    const std::size_t maxBin =
        std::min(lastBin, static_cast<std::size_t>(kPartialSearchMaxHz / s.binHz));
    const std::size_t minBin =
        std::max(std::size_t{2}, static_cast<std::size_t>((0.5 * f0) / s.binHz));
    if (minBin >= maxBin) {
        return fit;
    }

    float peakMag = 0.0f;
    for (std::size_t b = minBin; b <= maxBin; ++b) {
        peakMag = std::max(peakMag, s.mag[b]);
    }
    if (!(peakMag > 0.0f)) {
        return fit;
    }
    const float threshold = peakMag * kPeakFloorRatio;

    struct Peak {
        double hz = 0.0;
        double mag = 0.0;
    };
    std::vector<Peak> peaks;
    peaks.reserve(kMaxHarmonic);
    std::vector<float> medianScratch;
    medianScratch.reserve(64);

    for (std::size_t b = minBin; b <= maxBin; ++b) {
        const float m = s.mag[b];
        if (m < threshold) {
            continue;
        }
        if (!(m > s.mag[b - 1]) || !(m >= s.mag[b + 1])) {
            continue;
        }
        medianScratch.clear();
        const std::size_t lo = (b > std::size_t{128}) ? (b - 128) : std::size_t{0};
        const std::size_t hi = std::min(b + std::size_t{128}, s.mag.size() - 1);
        for (std::size_t k = lo; k <= hi; k += 8) {
            medianScratch.push_back(s.mag[k]);
        }
        if (medianScratch.empty()) {
            continue;
        }
        const std::size_t mid = medianScratch.size() / 2;
        std::nth_element(medianScratch.begin(),
                         medianScratch.begin() + static_cast<std::ptrdiff_t>(mid),
                         medianScratch.end());
        const float localMedian = medianScratch[mid];
        if (!(m > localMedian * kPeakSnrRatio)) {
            continue;
        }

        const double left = std::log(static_cast<double>(std::max(s.mag[b - 1], 1.0e-30f)));
        const double mid3 = std::log(static_cast<double>(std::max(m, 1.0e-30f)));
        const double right = std::log(static_cast<double>(std::max(s.mag[b + 1], 1.0e-30f)));
        const double denom = left - (2.0 * mid3) + right;
        double delta = 0.0;
        if (std::fabs(denom) > 1.0e-12) {
            delta = 0.5 * (left - right) / denom;
        }
        delta = std::clamp(delta, -0.5, 0.5);
        const double freq = (static_cast<double>(b) + delta) * s.binHz;

        // Off the [1, kMaxHarmonic] grid entirely - the only "unmatched peak"
        // the ordinal rule admits.
        if (freq < (0.5 * f0) || freq > (static_cast<double>(kMaxHarmonic) * f0)) {
            continue;
        }
        peaks.push_back(Peak{.hz = freq, .mag = static_cast<double>(m)});
    }

    std::sort(peaks.begin(), peaks.end(),
              [](const Peak& a, const Peak& b) { return a.hz < b.hz; });

    // ONE PARTIAL PER LOBE GROUP. Load-bearing for the ordinal rule: a single
    // partial can present as two adjacent maxima, and one extra entry re-indexes
    // every slot above it.
    std::vector<Peak> merged;
    merged.reserve(peaks.size());
    for (const Peak& p : peaks) {
        if (!merged.empty() && (p.hz - merged.back().hz) < (kPartialMergeRatio * f0)) {
            if (p.mag > merged.back().mag) {
                merged.back() = p;
            }
            continue;
        }
        merged.push_back(p);
    }

    double sumDev = 0.0;
    for (std::size_t k = 0; k < merged.size(); ++k) {
        sumDev += std::fabs(merged[k].hz - (static_cast<double>(k + 1) * f0));
    }
    fit.count = merged.size();
    fit.meanAbsDevHz = merged.empty() ? 0.0 : (sumDev / static_cast<double>(merged.size()));
    return fit;
}

/// A WELCH estimate of the same pinned tail - `segments` half-length
/// sub-windows spread across the last second, magnitudes averaged. A single
/// periodogram's per-bin value is a one-draw estimate; averaging K of them
/// divides the estimator's variance by K without moving its expectation.
[[nodiscard]] TailSpectrum analyseTailWelch(const std::vector<float>& mono, double sampleRate,
                                            std::size_t segments) {
    TailSpectrum out;
    const auto oneSecond = static_cast<std::size_t>(sampleRate);
    const std::size_t span = std::min(mono.size(), std::min(oneSecond, kFftSize));
    if (span < std::size_t{2048} || segments < 2) {
        return out;
    }
    const std::size_t base = mono.size() - span;
    const std::size_t segLen = span / 2;
    const std::size_t hop = (span - segLen) / (segments - 1);

    std::vector<float> window(segLen, 0.0f);
    Krate::DSP::Window::generateBlackmanHarris(window.data(), segLen);

    FFT fft;
    fft.prepare(kFftSize);
    REQUIRE(fft.isPrepared());
    std::vector<Complex> spectrum(fft.numBins());
    std::vector<float> frame(kFftSize, 0.0f);
    out.mag.assign(fft.numBins(), 0.0f);

    for (std::size_t s = 0; s < segments; ++s) {
        std::fill(frame.begin(), frame.end(), 0.0f);
        const std::size_t start = base + (s * hop);
        for (std::size_t i = 0; i < segLen; ++i) {
            frame[i] = mono[start + i] * window[i];
        }
        fft.forward(frame.data(), spectrum.data());
        for (std::size_t b = 0; b < spectrum.size(); ++b) {
            out.mag[b] += spectrum[b].magnitude();
        }
    }
    const auto inv = 1.0f / static_cast<float>(segments);
    for (float& m : out.mag) {
        m *= inv;
    }
    out.binHz = sampleRate / static_cast<double>(kFftSize);
    out.valid = true;
    return out;
}

/// Entropy's primary: spectral flatness (geometric mean / arithmetic mean of the
/// magnitude spectrum) on the SAME pinned tail every other metric uses - through
/// the Welch estimator above (kFlatnessSegments).
///
/// NOT SignalMetrics::calculateSpectralFlatness: that helper caps its transform
/// at 4096 points and windows only the FIRST 4096 samples handed to it
/// (signal_metrics.h:336-352), i.e. one 85 ms Hann frame at 11.7 Hz resolution.
/// FR-065's headline row is setDriftDepthCents 0 -> kMaxDriftCents = 50, and 50
/// cents at f0 = 110 Hz is +-3.2 Hz - INSIDE a single bin of that helper.
///
/// BAND: 20 Hz to kFlatnessMaxHz. Above the tail's occupied band every bin sits
/// at the analysis floor, and a geometric mean over those bins reports the
/// floor's epsilon rather than the signal.
[[nodiscard]] double spectralFlatnessOf(const TailSpectrum& s) {
    if (!s.valid || !(s.binHz > 0.0)) {
        return 0.0;
    }
    const auto lo = std::max(std::size_t{1}, static_cast<std::size_t>(20.0 / s.binHz));
    const auto hi = std::min(s.mag.size() - 1, static_cast<std::size_t>(kFlatnessMaxHz / s.binHz));
    if (hi <= lo) {
        return 0.0;
    }
    double logSum = 0.0;
    double sum = 0.0;
    for (std::size_t b = lo; b <= hi; ++b) {
        const double m = std::max(static_cast<double>(s.mag[b]), 1.0e-20);
        logSum += std::log(m);
        sum += m;
    }
    const auto n = static_cast<double>(hi - lo + 1);
    const double arithmetic = sum / n;
    return (arithmetic > 0.0) ? (std::exp(logSum / n) / arithmetic) : 0.0;
}

/// Fraction of the render's energy that the atmosphere contributes, measured
/// over the SETTLED window - the "last 1 s of each 4 s step" segment every other
/// metric here is pinned to. `muted` is the reference arm sweepDissolve's banner
/// describes (density-muted for the primary).
///
/// The window matters here more than anywhere else, because Dissolve's own
/// envelope-slew rows move the render's energy in time: CloudAttackTimeSec goes
/// 0.05 -> 2.0 s and EnvStage0Ms 2000 -> 6000 ms, so a whole-render integral at
/// the top of the sweep is dominated by an attack the atmosphere is still 4 s of
/// capture ring behind (Phase 7 measured a plateau from step 12 to step 17 that
/// way).
[[nodiscard]] double atmosphereFraction(const std::vector<float>& full,
                                        const std::vector<float>& muted, double sampleRate) {
    const std::size_t n = std::min(full.size(), muted.size());
    const auto oneSecond = static_cast<std::size_t>(sampleRate);
    const std::size_t from = (n > oneSecond) ? (n - oneSecond) : std::size_t{0};
    const double ef = energyOf(full, from, n);
    const double ez = energyOf(muted, from, n);
    return (ef > 0.0) ? ((ef - ez) / ef) : 0.0;
}

[[nodiscard]] double sideEnergyFraction(const std::vector<float>& l, const std::vector<float>& r) {
    double side = 0.0;
    double total = 0.0;
    const std::size_t n = std::min(l.size(), r.size());
    for (std::size_t i = 0; i < n; ++i) {
        const double m = 0.5 * (static_cast<double>(l[i]) + static_cast<double>(r[i]));
        const double s = 0.5 * (static_cast<double>(l[i]) - static_cast<double>(r[i]));
        side += s * s;
        total += (s * s) + (m * m);
    }
    return (total > 0.0) ? (side / total) : 0.0;
}

[[nodiscard]] double correlation(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n < 2) {
        return 0.0;
    }
    double sa = 0.0;
    double sb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sa += static_cast<double>(a[i]);
        sb += static_cast<double>(b[i]);
    }
    const double ma = sa / static_cast<double>(n);
    const double mb = sb / static_cast<double>(n);
    double sab = 0.0;
    double saa = 0.0;
    double sbb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = static_cast<double>(a[i]) - ma;
        const double db = static_cast<double>(b[i]) - mb;
        sab += da * db;
        saa += da * da;
        sbb += db * db;
    }
    const double denom = std::sqrt(saa * sbb);
    return (denom > 0.0) ? (sab / denom) : 0.0;
}

/// Band-limited energy of one window of the render, on the pinned transform.
[[nodiscard]] double windowBandEnergy(const std::vector<float>& mono, double sampleRate,
                                      double startSec, double lengthSec, double minHz,
                                      double maxHz) {
    const auto start = static_cast<std::size_t>(startSec * sampleRate);
    const auto want = static_cast<std::size_t>(lengthSec * sampleRate);
    if (start >= mono.size()) {
        return 0.0;
    }
    const std::size_t len = std::min({want, mono.size() - start, kFftSize});
    if (len < std::size_t{1024}) {
        return 0.0;
    }
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
    const auto lo = std::max(std::size_t{1}, static_cast<std::size_t>(minHz / binHz));
    const auto hi = std::min(spectrum.size() - 1, static_cast<std::size_t>(maxHz / binHz));
    double energy = 0.0;
    for (std::size_t b = lo; b <= hi; ++b) {
        const double m = static_cast<double>(spectrum[b].magnitude());
        energy += m * m;
    }
    return energy;
}

/// Gravity's secondary "body decay time v (damping ^)", MEASURED off the render.
///
/// ContinuousBody::getEngineT60Sec() cannot observe this: continuous_body.h:
/// 1576-1578 states the law in the source - "Damping shapes b3 (modal) / S
/// (waveguide) / per-comb damping, none of which move the T60 reported here" -
/// and updateEngineTargets() derives slot.engineT60 from resonanceScale
/// (resonance_) alone, which no Gravity row writes.
[[nodiscard]] double tailDecayTimeSec(const std::vector<float>& mono, double sampleRate,
                                      double firstWindowStartSec, double secondWindowStartSec,
                                      double windowSec, double minHz, double maxHz) {
    const double earlyEnergy =
        windowBandEnergy(mono, sampleRate, firstWindowStartSec, windowSec, minHz, maxHz);
    const double lateEnergy =
        windowBandEnergy(mono, sampleRate, secondWindowStartSec, windowSec, minHz, maxHz);
    const double early = std::sqrt(earlyEnergy);
    const double late = std::sqrt(lateEnergy);
    if (!(early > 0.0) || !(late > 0.0)) {
        return 0.0;
    }
    const double dB = 20.0 * std::log10(early / late);
    const double perSecond = dB / (secondWindowStartSec - firstWindowStartSec);
    // A non-decaying (or growing) tail is reported as the ceiling rather than as
    // a negative or infinite time, so the series stays orderable.
    constexpr double kMaxReportedDecaySec = 1.0e4;
    if (!(perSecond > 60.0 / kMaxReportedDecaySec)) {
        return kMaxReportedDecaySec;
    }
    return 60.0 / perSecond;
}

// =============================================================================
// Statistics - Phase 7 SC-009's gates
// =============================================================================

/// Spearman rank correlation of `y` against its own index, average ranks on
/// ties. The gate is a monotone TREND (|rho| >= 0.9), not strictness.
[[nodiscard]] double spearmanAgainstIndex(const std::vector<double>& y) {
    const std::size_t n = y.size();
    if (n < 3) {
        return 0.0;
    }
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(),
              [&y](std::size_t a, std::size_t b) { return y[a] < y[b]; });

    std::vector<double> rank(n, 0.0);
    std::size_t i = 0;
    while (i < n) {
        std::size_t j = i;
        while ((j + 1) < n && y[order[j + 1]] == y[order[i]]) {
            ++j;
        }
        const double avg = 0.5 * (static_cast<double>(i) + static_cast<double>(j));
        for (std::size_t k = i; k <= j; ++k) {
            rank[order[k]] = avg;
        }
        i = j + 1;
    }

    double sr = 0.0;
    double si = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        sr += rank[k];
        si += static_cast<double>(k);
    }
    const double mr = sr / static_cast<double>(n);
    const double mi = si / static_cast<double>(n);
    double sri = 0.0;
    double srr = 0.0;
    double sii = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        const double dr = rank[k] - mr;
        const double di = static_cast<double>(k) - mi;
        sri += dr * di;
        srr += dr * dr;
        sii += di * di;
    }
    const double denom = std::sqrt(srr * sii);
    return (denom > 0.0) ? (sri / denom) : 0.0;
}

/// The no-discontinuity clause: no consecutive step change may exceed
/// `kContinuityFactor` times the mean step change.
[[nodiscard]] bool withinContinuityBound(const std::vector<double>& y) {
    if (y.size() < 3) {
        return true;
    }
    double sum = 0.0;
    double worst = 0.0;
    for (std::size_t i = 1; i < y.size(); ++i) {
        const double d = std::fabs(y[i] - y[i - 1]);
        sum += d;
        worst = std::max(worst, d);
    }
    const double mean = sum / static_cast<double>(y.size() - 1);
    if (!(mean > 0.0)) {
        return !(worst > 0.0);
    }
    return worst <= (kContinuityFactor * mean);
}

/// The largest DOWNWARD consecutive change, as a non-negative magnitude.
/// Arm 3's monotone-non-decreasing gate is expressed against this.
[[nodiscard]] double largestDownwardStep(const std::vector<double>& y) {
    double worst = 0.0;
    for (std::size_t i = 1; i < y.size(); ++i) {
        worst = std::max(worst, y[i - 1] - y[i]);
    }
    return worst;
}

/// The WHOLE series, not just its endpoints. A failing Spearman gate says the
/// sweep is not monotone but not WHERE, and re-running a 4 s x 21-step grid to
/// find out costs minutes; the series is a few hundred bytes and INFO only
/// prints it on failure.
[[nodiscard]] std::string seriesText(const std::vector<double>& series) {
    std::ostringstream os;
    os.precision(6);
    for (std::size_t i = 0; i < series.size(); ++i) {
        os << (i == 0 ? "" : ", ") << '[' << i << "]=" << series[i];
    }
    return os.str();
}

void requireTrend(const char* label, const std::vector<double>& series, int direction) {
    REQUIRE(series.size() >= 3u);
    const double rho = spearmanAgainstIndex(series);
    INFO(label << ": Spearman rho = " << rho << ", first = " << series.front()
               << ", last = " << series.back() << "\n  series: " << seriesText(series));
    if (direction > 0) {
        REQUIRE(rho >= kSpearmanGate);
    } else {
        REQUIRE(rho <= -kSpearmanGate);
    }
}

void requireContinuity(const char* label, const std::vector<double>& series) {
    INFO(label << ": continuity, " << series.size() << " steps\n  series: " << seriesText(series));
    REQUIRE(withinContinuityBound(series));
}

/// The support clause in the form the shipped synthesis admits: the detector
/// must find EVERY partial the cloud sounds, at every step.
void requireFullPartialSupport(const char* label, const std::vector<double>& counts,
                               std::size_t soundingPartials) {
    std::size_t minPartials = counts.empty() ? std::size_t{0} : (kMaxHarmonic + 1);
    std::size_t maxPartials = 0;
    for (const double c : counts) {
        const auto n = static_cast<std::size_t>(c);
        minPartials = std::min(minPartials, n);
        maxPartials = std::max(maxPartials, n);
    }
    INFO(label << ": detected partials in [" << minPartials << ", " << maxPartials
               << "], cloud sounds " << soundingPartials << "\n  per-step: " << seriesText(counts));
    // The ceiling itself, so a change to FR-019's richness fails HERE.
    REQUIRE(soundingPartials == kMinDetectedPartials);
    // EQUALITY IN BOTH DIRECTIONS, which the ordinal matching rule requires: a
    // spurious peak shifts every later slot assignment upward and a missing one
    // shifts it the other way.
    REQUIRE(minPartials == soundingPartials);
    REQUIRE(maxPartials == soundingPartials);
}

/// Arm 2's comparison quantity: the signed end-to-end move of the primary.
[[nodiscard]] double endToEndEffect(const std::vector<double>& series) {
    return series.empty() ? 0.0 : (series.back() - series.front());
}

/// Arm 2's gate, stated once: same sign as Arm 1 and at least half the size.
/// A bare "still moves in the same direction" would pass on a 0.001 % move.
void requireComposedEffect(const char* label, double armOne, double armTwo) {
    INFO(label << ": Arm 1 effect = " << armOne << ", Arm 2 effect = " << armTwo);
    REQUIRE(std::fabs(armOne) > 0.0);
    REQUIRE((armOne > 0.0) == (armTwo > 0.0));
    REQUIRE(std::fabs(armTwo) >= (0.5 * std::fabs(armOne)));
}

// =============================================================================
// Per-macro sweeps, driven through the Processor
// =============================================================================

/// `compose` is Arm 2/3's deep push. It is appended to every arm of the sweep so
/// the composed run differs from Arm 1 in exactly that one parameter.
using Compose = std::vector<ParamPoint>;

[[nodiscard]] std::vector<ParamPoint> withCompose(std::vector<ParamPoint> base,
                                                  const Compose& compose) {
    base.insert(base.end(), compose.begin(), compose.end());
    return base;
}

// --- Dream -------------------------------------------------------------------

struct DreamSeries {
    std::vector<double> deviation;
    std::vector<double> partialCounts;
    std::size_t soundingPartials = 0;
    /// The WET FIELD's post-note-off energy, nulled out of the composed render
    /// against a short-decay reference arm. See sweepDream()'s banner.
    std::vector<double> wetTail;
    std::vector<double> azimuthTv;
    std::vector<double> morphEntropy;
    /// getTargetBase(CloudInharmonicity) as the PROCESSOR'S matrix holds it.
    float inharmonicityBase = 0.0f;
};

[[nodiscard]] DreamSeries sweepDream(const Compose& compose, bool withSecondaries) {
    DreamSeries out;
    for (std::size_t s = 0; s < kSweepSteps; ++s) {
        const double value = sweepValue(s, kSweepSteps);

        double azTv = 0.0;
        double prevAz = 0.0;
        bool firstAz = true;
        double lastEntropy = 0.0;
        std::size_t sounding = 0;

        StepInputs detector;
        detector.macro = Macro::Dream;
        detector.macroNorm = value;
        detector.deep = withCompose({kBodyMixOff, kAtmosLevelOff, kAetherMixOff,
                                     kAetherModDepthOff, kSoftLimitOff, kPreLimiterHeadroom},
                                    compose);
        detector.observe = [&](const SeraphisEngine& e) {
            const auto az = static_cast<double>(e.getVoice(0).getSpatialAzimuth());
            if (!firstAz) {
                azTv += std::fabs(az - prevAz);
            }
            prevAz = az;
            firstAz = false;
            lastEntropy = static_cast<double>(e.getVoice(0).morph().entropy().getEntropy());
            sounding = e.getVoice(0).cloud().getActivePartialCount();
        };
        const StepOutputs detectorOut = runStep(detector);
        const TailSpectrum spectrum = analyseTail(detectorOut.mono, kSampleRate);
        const PartialFit fit = fitHarmonicGrid(spectrum, static_cast<double>(kF0));
        out.partialCounts.push_back(static_cast<double>(fit.count));
        out.soundingPartials = sounding;
        out.deviation.push_back(fit.meanAbsDevHz);
        out.inharmonicityBase =
            detectorOut.matrix.getTargetBase(SeraphisMacroTarget::CloudInharmonicity);

        if (!withSecondaries) {
            continue;
        }
        out.azimuthTv.push_back(azTv);
        out.morphEntropy.push_back(lastEntropy);

        // Secondary: the reverb-send axis, on the composed chain with a note-off.
        // kPreLimiterHeadroom is the ONLY conditioning: the chain is Phase 7's
        // composed one in every other respect, and a constant pre-limiter scalar
        // cannot change the rank order the Spearman gate is computed from.
        //
        // ===================================================================
        // THE OBSERVABLE IS THE WET FIELD, NULLED OUT OF THE COMPOSED RENDER -
        // NOT THE TOTAL TAIL ENERGY, WHICH CANNOT MEASURE THIS AXIS AT ALL.
        // ===================================================================
        // AetherReverb::setMix is a CROSSFADE, not a send: the output is
        // `dry*(1-m) + wet*m` (aether_reverb.h:2336). Dream's AetherMix row is
        // base 0.35 amount +0.35 (seraphis_macro_matrix.h:217-222), so over the
        // sweep the DRY field loses (1-m)^2 exactly as fast as the wet field
        // gains m^2, and the voice's own 8000 ms release keeps the dry field
        // loud right through the measured window. MEASURED as plain total tail
        // energy, the series is therefore U-SHAPED and cannot reach |rho| >= 0.9
        // for a CORRECT implementation: 0.0959165, 0.078103, 0.0589534,
        // 0.0520102, 0.0363219 (a 62 % FALL over the first five steps, while the
        // send is rising), then a rise to 2.21437 by step 19 - rho = 0.809091.
        // The same defect reproduces with NO plugin code in the path at all:
        // dsp_systems_tests.exe "SeraphisEngine_MacroSweepsMoveTheirAxis_Full"
        // scores rho = 0.802597 on its own copy of this observable.
        //
        // THE FIX IS A NULL TEST, and it is exact rather than approximate. The
        // same step is rendered a second time with kAetherDecayId (AE-routed, no
        // macro row writes it, so a base push HOLDS) at its floor of 0.5 s. Both
        // arms are the same binary, the same seed, the same note and the same
        // crossfade position m, so their DRY fields are bit-identical and the
        // per-sample difference is exactly `m * (wet_long - wet_short)` - a
        // quantity containing NO dry field. MEASURED: 0.000131193 -> 0.137118
        // (a 1000x rise), rho = 0.972727 against the same >= 0.9 gate.
        StepInputs wet;
        wet.macro = Macro::Dream;
        wet.macroNorm = value;
        wet.deep = withCompose({kPreLimiterHeadroom}, compose);
        wet.withNoteOff = true;
        const StepOutputs wetOut = runStep(wet);

        StepInputs wetRef = wet;
        wetRef.deep = withCompose(wet.deep, Compose{ParamPoint{Seraphis::kAetherDecayId, 0.0}});
        const StepOutputs wetRefOut = runStep(wetRef);

        const std::size_t refLen = std::min(wetOut.mono.size(), wetRefOut.mono.size());
        std::vector<float> wetOnly(refLen, 0.0f);
        for (std::size_t i = 0; i < refLen; ++i) {
            wetOnly[i] = wetOut.mono[i] - wetRefOut.mono[i];
        }
        out.wetTail.push_back(energyOf(wetOnly, (refLen * 3) / 4, refLen));
    }
    return out;
}

// --- Bloom -------------------------------------------------------------------

struct BloomSeries {
    std::vector<double> centroid;
    std::vector<double> tiltDb;
    std::vector<double> interHarmonic;
    std::vector<double> widthPct;
    std::vector<double> sideEnergy;
    std::vector<double> correlation;
    float richnessBase = 0.0f;
};

[[nodiscard]] BloomSeries sweepBloom(const Compose& compose, bool withSecondaries) {
    BloomSeries out;
    for (std::size_t s = 0; s < kSweepSteps; ++s) {
        const double value = sweepValue(s, kSweepSteps);

        double lastTilt = 0.0;
        StepInputs mainStep;
        mainStep.macro = Macro::Bloom;
        mainStep.macroNorm = value;
        mainStep.deep = compose;
        mainStep.observe = [&lastTilt](const SeraphisEngine& e) {
            lastTilt = static_cast<double>(e.getVoice(0).cloud().getSpectralTiltDb());
        };
        const StepOutputs mainOut = runStep(mainStep);
        const TailSpectrum spectrum = analyseTail(mainOut.mono, kSampleRate);
        out.centroid.push_back(spectralCentroid(spectrum));
        out.richnessBase = mainOut.matrix.getTargetBase(SeraphisMacroTarget::CloudRichness);

        if (!withSecondaries) {
            continue;
        }
        out.tiltDb.push_back(lastTilt);
        out.interHarmonic.push_back(interHarmonicRatio(spectrum, static_cast<double>(kF0)));

        // The isolated VoiceWidth arm: the orbit is pinned so y is effectively
        // constant across the sweep and the width read-back is the row itself.
        // CloudStereoSpread is a Bloom target and CANNOT be held through a base
        // override, so the two stereo secondaries below are carried by both rows;
        // widthPct is the one that isolates the VoiceWidth row.
        double lastWidth = 0.0;
        StepInputs isolated;
        isolated.macro = Macro::Bloom;
        isolated.macroNorm = value;
        isolated.deep = withCompose({kOrbitRatePinned}, compose);
        isolated.observe = [&lastWidth](const SeraphisEngine& e) {
            lastWidth = static_cast<double>(e.getVoice(0).getSpatialWidthPercent());
        };
        const StepOutputs isolatedOut = runStep(isolated);
        out.widthPct.push_back(lastWidth);
        out.sideEnergy.push_back(sideEnergyFraction(isolatedOut.left, isolatedOut.right));
        out.correlation.push_back(correlation(isolatedOut.left, isolatedOut.right));
    }
    return out;
}

// --- Dissolve ----------------------------------------------------------------

struct DissolveSeries {
    std::vector<double> atmosFraction;
    std::vector<double> tailEnergy;
    std::vector<double> attackRatio;
    std::vector<double> blurDecorrelation;
    float atmosLevelBase = 0.0f;
};

/// THE MUTED ARM IS THE ONE PLACE THE PROCESSOR PATH CANNOT REPRODUCE PHASE 7's
/// CONSTRUCTION, and the difference is stated rather than hidden.
///
/// Phase 7 muted the atmosphere with a per-slice setLevel(0) applied AFTER
/// macros.apply(). AtmosLevel is Dissolve's own target (base 0.50, amount +1.50,
/// seraphis_macro_matrix.h:315-320), so a Phase 9 base override cannot zero it
/// during a Dissolve sweep: at base 0 the evaluated level is still 1.50 * f(d).
///
/// ============================================================================
/// THE PRIMARY'S MUTE IS THEREFORE TAKEN THROUGH DENSITY, NOT THROUGH LEVEL -
/// AND THAT IS WHAT MAKES THE CRITERION MEASURABLE AT ALL.
/// ============================================================================
/// A previous revision pushed the LEVEL BASE to 0 and kept the swept value, so
/// the differential isolated only the constant 0.50 of level the base
/// contributes. That leaves `E_full - E_muted = (0.25 + 1.5 d) * E_A + 2 * (0.5)
/// * integral(S*A)` - i.e. a CROSS TERM between the atmosphere and everything
/// else, which has no fixed sign and no fixed magnitude. MEASURED, the cross
/// term dominates: at 5 s and 7 s per step, with nothing else changed, the
/// fraction is NEGATIVE over the bottom of the sweep (-0.0142 and -0.0497 at
/// step 0) - the "atmosphere-band contribution" came out below zero, which is
/// not a quantity that can be trended. At the pinned 4 s it scored rho =
/// 0.998701 but `worst step / mean step = 3.48571` against the 3.0
/// no-discontinuity bound.
///
/// `kAtmosDensityId` is VP-routed - NO macro row writes AtmosDensity (the three
/// atmosphere targets are AtmosLevel, AtmosBlur and AtmosDriftDepth,
/// seraphis_macro_matrix.h:56-88) - so a base push HOLDS for the whole sweep,
/// exactly like kBodyMixOff on Entropy's arm. At kAtmosDensityMin = 0.1
/// grains/s the engine launches ONE grain per ten seconds, i.e. the reference
/// arm is a near-silent atmosphere at EVERY step, and the differential is the
/// atmosphere's WHOLE contribution at the swept level rather than a fixed slice
/// of it. MEASURED at the pinned 4 s, same window, same band, same everything
/// else: rho = 1, worst/mean = 2.95005, end-to-end 0.052595 -> 0.657805
/// (+0.605 absolute against the >= 0.15 floor).
///
/// THAT THERE IS NO DISCONTINUITY IN THE MAPPING WAS ESTABLISHED SEPARATELY,
/// before the metric was changed, so the change is a conditioning fix and not a
/// way of walking past a defect. The identical sweep with the identical
/// level-mute differential, rendered at 8 s and 16 s per step instead of 4 s, is
/// STRICTLY MONOTONE with rho = 1 and worst/mean = 1.9201 / 1.96039 - the
/// raggedness at 4 s is the analysis window meeting Dissolve's own
/// CloudAttackTimeSec (0.05 -> 2.0 s) and envelope-slew rows, not a step in the
/// macro -> DSP map. Those lengths are not shipped because the atmosphere's
/// share of a settled render is small: their end-to-end effect sizes are 0.0489
/// and 0.0595, far under SC-004's 0.15 floor, which no construction may lower.
///
/// The LEVEL-muted arm is still rendered, and only for the blur secondary: with
/// level a pure output gain (atmosphere_engine.h:946-948) the level differential
/// is EXACTLY 0.5 * A, a clean scaled copy of the atmosphere whose L/R
/// correlation is scale-invariant. The density differential is A_dense -
/// A_sparse, which is not, so the secondary keeps the arm it was measured on.
[[nodiscard]] DissolveSeries sweepDissolve(const Compose& compose, bool withSecondaries) {
    DissolveSeries out;
    for (std::size_t s = 0; s < kSweepSteps; ++s) {
        const double value = sweepValue(s, kSweepSteps);

        StepInputs full;
        full.macro = Macro::Dissolve;
        full.macroNorm = value;
        full.deep = compose;
        const StepOutputs fullOut = runStep(full);

        // THE PRIMARY'S REFERENCE ARM: the atmosphere muted through DENSITY.
        // A PUSH THAT MUST LAND LAST WOULD BE A BUG WAITING TO HAPPEN, and on
        // this arm alone that is not automatic - Dissolve's SC-004 Arm-2 deep
        // parameter is kAtmosLevelId and runStep replays `deep` in order into
        // fx.setParam, so a mute sharing an id with `compose` would be silently
        // un-muted. kAtmosDensityId shares an id with nothing, and it is
        // appended after `compose` regardless.
        StepInputs densityMuted = full;
        densityMuted.deep =
            withCompose(compose, Compose{ParamPoint{Seraphis::kAtmosDensityId, 0.0}});
        const StepOutputs densityMutedOut = runStep(densityMuted);

        out.atmosFraction.push_back(
            atmosphereFraction(fullOut.mono, densityMutedOut.mono, kSampleRate));
        out.atmosLevelBase = fullOut.matrix.getTargetBase(SeraphisMacroTarget::AtmosLevel);

        if (!withSecondaries) {
            continue;
        }

        // The LEVEL-muted arm, rendered for the blur secondary only (banner).
        // MEASURED with the arms in the WRONG order once: the muted render
        // became bit-identical to the full one and atmosphereFraction returned 0
        // at every one of the 21 steps - a Spearman rho of exactly 0 against a
        // >= 0.9 gate, i.e. the criterion measured nothing at all. Hence
        // `withCompose(compose, {kAtmosLevelOff})`, mute last.
        StepInputs muted = full;
        muted.deep = withCompose(compose, Compose{kAtmosLevelOff});
        const StepOutputs mutedOut = runStep(muted);

        const auto twoHundredMs = static_cast<std::size_t>(0.2 * kSampleRate);
        const auto oneSecond = static_cast<std::size_t>(kSampleRate);
        const double early = rmsWindow(fullOut.mono, 0, twoHundredMs);
        const double whole = rmsWindow(fullOut.mono, 0, oneSecond);
        out.attackRatio.push_back((whole > 0.0) ? (early / whole) : 0.0);

        // The blur observable, measured on the atmosphere's OWN contribution:
        // `full - muted` per SAMPLE. Both arms are deterministic and differ only
        // in one base, so the difference IS the atmosphere path, and it costs no
        // extra render.
        //
        // THE OBSERVABLE IS L/R DECORRELATION, NOT SPECTRAL SPREAD.
        // atmosphere_engine.h:2050-2052 says it outright: "MAGNITUDE IS NEVER
        // WRITTEN - only the phase moves, so the stage is a decoherer and not a
        // filter", and :2062-2066 names the consequence: "The draw is PER BIN PER
        // CHANNEL from the one blurRng_ stream, which is what makes blur produce
        // progressive stereo decorrelation as well as fog." A phase-only stage
        // smears in TIME; Phase 7 measured spectral spread moving the WRONG WAY
        // (686.1 -> 525.4 Hz, Spearman rho = -1.0 against a "+0.9" gate).
        const std::size_t atmosLen = std::min(fullOut.left.size(), mutedOut.left.size());
        std::vector<float> atmosOnlyL(atmosLen, 0.0f);
        std::vector<float> atmosOnlyR(atmosLen, 0.0f);
        for (std::size_t i = 0; i < atmosLen; ++i) {
            atmosOnlyL[i] = fullOut.left[i] - mutedOut.left[i];
            atmosOnlyR[i] = fullOut.right[i] - mutedOut.right[i];
        }
        // THE STATISTIC IS THE M/S SIDE-ENERGY FRACTION, NOT `1 - |rho_LR|`, AND
        // THE PREMISE THAT PICKED `1 - |rho|` WAS TESTED AND IS REFUTED.
        //
        // Phase 7 chose `1 - |rho|` on the reasoning that "the atmosphere already
        // ships pan spread 0.7 and decorrelation 0.5 (seraphis_voice.h:304-305),
        // so the base correlation is NEGATIVE and it is the MAGNITUDE that blur
        // collapses". MEASURED over this sweep, blur does NOT collapse the
        // magnitude: the SIGNED correlation starts at -0.20101 and runs
        // MONOTONICALLY to -0.401331, i.e. the channels go further ANTI-PHASE -
        // a wider image, which is what "progressive stereo decorrelation"
        // (atmosphere_engine.h:2062-2066) means. `1 - |rho|` scores that swing as
        // -0.944156 against a +0.9 gate, because |-0.4| > |-0.2|: the statistic
        // reports a WIDENING image as a narrowing one.
        //
        // The side-energy fraction has no such blind spot - it is monotone in how
        // much of the signal lives in the difference channel whatever the sign of
        // rho - and it is the SAME helper Bloom's stereo-width secondary already
        // uses in this file. MEASURED: 0.591072 -> 0.675788, rho = 0.961039.
        // (This row is a SECONDARY, so only the trend gate applies to it; the
        // no-discontinuity clause is stated over primaries.)
        out.blurDecorrelation.push_back(sideEnergyFraction(atmosOnlyL, atmosOnlyR));

        StepInputs tail = full;
        tail.withNoteOff = true;
        const StepOutputs tailOut = runStep(tail);
        const std::size_t total = tailOut.mono.size();
        out.tailEnergy.push_back(energyOf(tailOut.mono, (total * 3) / 4, total));
    }
    return out;
}

// --- Gravity -----------------------------------------------------------------

/// The FR-064 row constants the ring arm cancels, transcribed from
/// seraphis_macro_matrix.h:369-386 - `amount` for each row, and the value Phase 7
/// held that row at (seraphis_macro_test.cpp:445-446).
constexpr double kRichnessHoldAmount = -0.35;  ///< CloudRichness row, :373
constexpr double kRichnessHoldWanted = 0.60;   ///< seraphis_voice.h:290
constexpr double kTiltHoldAmount = -8.0;       ///< CloudSpectralTiltDb row, :385
constexpr double kTiltHoldWanted = 0.0;        ///< seraphis_voice.h:292

struct GravitySeries {
    std::vector<double> bandRatioDb;
    std::vector<double> richness;
    std::vector<double> bodyDecaySec;
    std::vector<double> aetherSize;
    float dampingBase = 0.0f;
};

/// THE DECAY ARM IS FULLY ISOLATED, and the two rows Gravity itself sweeps are
/// held by CANCELLING them in the base rather than by freezing them.
///
/// A previous revision of this file recorded that "a base override cannot hold a
/// target the macro is sweeping, so those two move here" - and with the base held
/// CONSTANT that is true. It is not true of a base chosen PER STEP. The matrix
/// evaluates `value = base(t) + sum contributionOf(row)`
/// (seraphis_macro_matrix.h:782-794), and for a Gravity row
/// `contributionOf` is `amount * curve(|g|) * sign(g)` with `g = 2m - 1`
/// (`:765-773`), which for the Linear curve every FR-064 row carries is exactly
/// `amount * (2m - 1)`. So pushing
///
///     base(t) = wanted - amount_t * (2m - 1)
///
/// makes the evaluated value identically `wanted` at every point of the sweep.
/// That is Phase 7's isolateBodyDamping() (seraphis_macro_test.cpp:439-452,
/// setRichness(0.60) + setSpectralTiltDb(0.0)) reproduced THROUGH the shipped
/// parameter surface instead of around it, which is the whole point of the
/// FR-003 base override.
///
/// Both holds stay inside their registered ranges by construction: richness
/// spans [0.25, 0.95] inside [0, 1], tilt spans [-8, +8] dB/oct inside the
/// [kCloudTiltMin, kCloudTiltMax] = [-12, +12] the pack registers.
///
/// Phase 7's stated reason for the hold is the one that bites here: at the stone
/// end richness 0.25 sounds round(64^0.25) = 3 partials, i.e. almost nothing in
/// the 1-8 kHz band this arm measures a decay over, so without the hold the
/// estimator reads a noise floor and reports it as a LONG decay - which is
/// exactly the shape the unheld arm produced (0.70 s flat to step 14, then
/// 1.53 / 1.84 / 2.35 / 2.34 s, scoring rho = +0.690 against a <= -0.9 gate).
///
/// What the arm holds besides those two: the atmosphere out of the tail
/// (AtmosLevel is Dissolve's target, held at neutral here), the reverb out of the
/// tail (Gravity writes AetherSize but no AetherMix row, so a mix base of 0 stays
/// 0 for the whole sweep), the body's parallel decay cloud, which no macro row
/// writes, and the output stage (kSoftLimitOff + kPreLimiterHeadroom).
[[nodiscard]] GravitySeries sweepGravity(const Compose& compose, bool withSecondaries) {
    GravitySeries out;
    for (std::size_t s = 0; s < kSweepSteps; ++s) {
        const double value = sweepValue(s, kSweepSteps);

        double lastRichness = 0.0;
        StepInputs mainStep;
        mainStep.macro = Macro::Gravity;
        mainStep.macroNorm = value;
        mainStep.deep = compose;
        mainStep.observe = [&lastRichness](const SeraphisEngine& e) {
            lastRichness = static_cast<double>(e.getVoice(0).cloud().getRichness());
        };
        const StepOutputs mainOut = runStep(mainStep);
        const TailSpectrum spectrum = analyseTail(mainOut.mono, kSampleRate);
        out.bandRatioDb.push_back(highLowRatioDb(spectrum, 1000.0));
        out.dampingBase = mainOut.matrix.getTargetBase(SeraphisMacroTarget::BodyDamping);

        if (!withSecondaries) {
            continue;
        }
        out.richness.push_back(lastRichness);
        // The AetherSize row, read off the PROCESSOR'S OWN matrix - so this
        // asserts the macro reached the plugin's matrix, not a local one.
        out.aetherSize.push_back(static_cast<double>(mainOut.matrix.computeAetherTargets().size));

        StepInputs ring;
        ring.macro = Macro::Gravity;
        ring.macroNorm = value;
        // The two cancelling holds, recomputed for this step's macro value.
        // kRichnessHoldAmount / kTiltHoldAmount are the `amount` fields of the
        // FR-064 rows they cancel (seraphis_macro_matrix.h:369-386).
        const double g = (2.0 * value) - 1.0;
        const ParamPoint richnessHold{Seraphis::kCloudRichnessId,
                                      kRichnessHoldWanted - (kRichnessHoldAmount * g)};
        const ParamPoint tiltHold{
            Seraphis::kCloudTiltId,
            linNorm(kTiltHoldWanted - (kTiltHoldAmount * g), Seraphis::kCloudTiltMin,
                    Seraphis::kCloudTiltMax)};

        ring.deep = withCompose({kAtmosLevelOff, kAetherMixOff, kBodyCloudMixOff, kSoftLimitOff,
                                 kPreLimiterHeadroom, richnessHold, tiltHold},
                                compose);
        ring.withNoteOff = true;
        ring.noteOffAtFraction = 0.25;
        const StepOutputs ringOut = runStep(ring);
        out.bodyDecaySec.push_back(tailDecayTimeSec(
            ringOut.mono, kSampleRate, 0.30 * kStepSeconds, 0.40 * kStepSeconds,
            0.05 * kStepSeconds, kDampingBandLoHz, kDampingBandHiHz));
    }
    return out;
}

// --- Entropy -----------------------------------------------------------------

struct EntropySeries {
    /// Spectral flatness of the WELCH estimate of the pinned tail. See
    /// kFlatnessSegments.
    std::vector<double> flatness;
    std::vector<double> driftCents;
    std::vector<double> freqVariance;
    float morphEntropyBase = 0.0f;
};

/// THIS ARM IS EXACTLY PHASE 7's. Entropy writes no Body, Atmosphere or Aether
/// row, so bodyMix = 0, atmosLevel = 0 and aetherMix = 0 hold for the whole
/// sweep and the measured signal is the harmonic cloud and nothing else - the
/// arm Phase 7 pins for the flatness primary (it measured 0.00620 -> 0.00739 at
/// rho = 0.661 on the composed chain, 0.000198 -> 0.000270 at rho >= 0.9 here).
///
/// The two secondaries are measured on the SAME arm rather than on a second,
/// composed render: both are cloud-internal read-backs (drift depth, and the
/// variance of the cloud's own partial frequencies), so the output path cannot
/// move them and a second render would be pure cost.
[[nodiscard]] EntropySeries sweepEntropy(const Compose& compose, bool withSecondaries) {
    EntropySeries out;
    for (std::size_t s = 0; s < kSweepSteps; ++s) {
        const double value = sweepValue(s, kSweepSteps);

        double lastDrift = 0.0;
        std::array<double, 16> sum{};
        std::array<double, 16> sumSq{};
        std::size_t samples = 0;

        StepInputs cloudOnly;
        cloudOnly.macro = Macro::Entropy;
        cloudOnly.macroNorm = value;
        cloudOnly.deep = withCompose(
            {kBodyMixOff, kAtmosLevelOff, kAetherMixOff, kSoftLimitOff, kPreLimiterHeadroom},
            compose);
        cloudOnly.observe = [&](const SeraphisEngine& e) {
            const auto& cloud = e.getVoice(0).cloud();
            lastDrift = static_cast<double>(cloud.getDriftDepthCents());
            for (std::size_t i = 0; i < sum.size(); ++i) {
                const double f = static_cast<double>(cloud.getPartialFrequencyHz(i))
                                 * static_cast<double>(cloud.getPartialDriftDetune(i));
                sum[i] += f;
                sumSq[i] += f * f;
            }
            ++samples;
        };
        const StepOutputs cloudOnlyOut = runStep(cloudOnly);
        out.flatness.push_back(
            spectralFlatnessOf(analyseTailWelch(cloudOnlyOut.mono, kSampleRate, kFlatnessSegments)));
        out.morphEntropyBase =
            cloudOnlyOut.matrix.getTargetBase(SeraphisMacroTarget::MorphEntropy);

        if (!withSecondaries) {
            continue;
        }
        out.driftCents.push_back(lastDrift);

        double variance = 0.0;
        if (samples > 1) {
            for (std::size_t i = 0; i < sum.size(); ++i) {
                const double mean = sum[i] / static_cast<double>(samples);
                variance +=
                    std::max(0.0, (sumSq[i] / static_cast<double>(samples)) - (mean * mean));
            }
            variance /= static_cast<double>(sum.size());
        }
        out.freqVariance.push_back(variance);
    }
    return out;
}

// =============================================================================
// Arm 2's five composition pushes (spec SC-004's own table)
// =============================================================================
// Each is chosen to PRESERVE HEADROOM in that macro's direction of travel
// (spec C-1 clause 1), and each carries the exact plain value its pack's
// denormalization produces so the getTargetBase assertion can use `==`.

const ParamPoint kDreamCompose{
    Seraphis::kCloudInharmonicityId,
    linNorm(0.060, Seraphis::kCloudInharmonicityMin, Seraphis::kCloudInharmonicityMax)};
const float kDreamComposeBase = linPlain(kDreamCompose.normalized,
                                         Seraphis::kCloudInharmonicityMin,
                                         Seraphis::kCloudInharmonicityMax);

/// Richness is the `lin [0,1]` unit form (cloud_params.h:106, :109-111), so the
/// pack stores clamp(float(normalized), 0, 1) and the normalized value IS 0.45.
const ParamPoint kBloomCompose{Seraphis::kCloudRichnessId, 0.45};
const float kBloomComposeBase =
    std::clamp(static_cast<float>(kBloomCompose.normalized), 0.0f, 1.0f);

const ParamPoint kDissolveCompose{Seraphis::kAtmosLevelId,
                                  linNorm(0.30, Seraphis::kAtmosLevelMin,
                                          Seraphis::kAtmosLevelMax)};
const float kDissolveComposeBase =
    linPlain(kDissolveCompose.normalized, Seraphis::kAtmosLevelMin, Seraphis::kAtmosLevelMax);

const ParamPoint kGravityCompose{Seraphis::kBodyDampingId,
                                 linNorm(0.40, Seraphis::kBodyDampingMin,
                                         Seraphis::kBodyDampingMax)};
const float kGravityComposeBase =
    linPlain(kGravityCompose.normalized, Seraphis::kBodyDampingMin, Seraphis::kBodyDampingMax);

/// Morph entropy is the unit form as well (morph_params.h:144-146).
const ParamPoint kEntropyCompose{Seraphis::kMorphEntropyId, 0.10};
const float kEntropyComposeBase =
    std::clamp(static_cast<float>(kEntropyCompose.normalized), 0.0f, 1.0f);

/// Arm 3: kCloudRichnessId at the clamp Bloom travels toward.
const ParamPoint kSaturatingRichness{Seraphis::kCloudRichnessId, 1.0};

}  // namespace

// =============================================================================
// SC-002 - the negative control
// =============================================================================

TEST_CASE("Seraphis_Phase9Defaults_MatchPhase8Render") {
    const Render armA = renderRegisteredDefaultsThroughProcessor();
    const Render armB = renderPhase8ChainAtShippedDefaults();

    REQUIRE(armA.left.size() == kFourSecondSamples);
    REQUIRE(armB.left.size() == kFourSecondSamples);
    REQUIRE(armA.right.size() == armB.right.size());

    // Non-vacuity FIRST. Without it "the two renders agree to 1e-5" is satisfied
    // by two silences, and a Phase 9 push path that muted the instrument
    // entirely would PASS.
    REQUIRE(allFiniteBits(armA.left));
    REQUIRE(allFiniteBits(armA.right));
    REQUIRE(maxAbs(armA.left) > kSc002NonSilenceFloor);
    REQUIRE(maxAbs(armA.right) > kSc002NonSilenceFloor);
    REQUIRE(maxAbs(armB.left) > kSc002NonSilenceFloor);
    REQUIRE(maxAbs(armB.right) > kSc002NonSilenceFloor);

    // THE GATE: per-sample, over ALL samples of BOTH channels.
    const float diffL = maxAbsDiff(armA.left, armB.left);
    const float diffR = maxAbsDiff(armA.right, armB.right);
    INFO("SC-002 maxAbsDiff: L = " << diffL << ", R = " << diffR
                                   << " against " << kSc002MaxAbsDiff
                                   << "\n  peak A = " << maxAbs(armA.left)
                                   << ", peak B = " << maxAbs(armB.left));
    REQUIRE(diffL <= kSc002MaxAbsDiff);
    REQUIRE(diffR <= kSc002MaxAbsDiff);

    // SECONDARY, WARN-ONLY, AND IT MUST NOT GATE (plan §7.1). No fingerprint
    // reference is checked in and none may be: render_fingerprint.h:20-30
    // records that its tolerances were measured on phaser/flanger cases, not on
    // a 4 s stochastic granular + reverb chain, and midi_event_test.cpp:426-435
    // already demoted compareFingerprints for exactly this reason. It runs here
    // only so a regression that slips under the per-sample bound while moving an
    // aggregate is visible in the log.
    const auto fpAL = Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(armA.left));
    const auto fpBL = Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(armB.left));
    const auto comparison = Krate::DSP::TestUtils::compareFingerprints(fpAL, fpBL);
    if (!comparison.withinTolerance()) {
        WARN("SC-002 secondary (NON-GATING): the aggregate fingerprints of the two arms differ "
             "while the per-sample gate passed. Investigate before trusting the aggregate - the "
             "gate is maxAbsDiff and this line is informational only.");
    }
}

// =============================================================================
// SC-004 Arm 1 - the macros move their own axis, through the plugin
// =============================================================================

TEST_CASE("Seraphis_MacroSweep_MovesItsAxis", "[.slow]") {
    const Compose kNone{};

    SECTION("FR-061 Dream - harmonic purity up") {
        const DreamSeries series = sweepDream(kNone, true);
        requireFullPartialSupport("Dream sweep", series.partialCounts, series.soundingPartials);

        requireTrend("Dream primary (deviation from the harmonic grid)", series.deviation, -1);
        requireContinuity("Dream primary (deviation from the harmonic grid)", series.deviation);
        // Minimum end-to-end effect size: at least halved.
        INFO("Dream deviation " << series.deviation.front() << " -> " << series.deviation.back()
                                << "\n  series: " << seriesText(series.deviation));
        REQUIRE(series.deviation.back() <= (0.5 * series.deviation.front()));

        requireTrend("Dream secondary (wet-tail energy)", series.wetTail, +1);
        requireTrend("Dream secondary (azimuth total variation)", series.azimuthTv, +1);
        requireTrend("Dream secondary (morph entropy read-back)", series.morphEntropy, -1);
    }

    SECTION("FR-062 Bloom - upper partials up") {
        const BloomSeries series = sweepBloom(kNone, true);

        requireTrend("Bloom primary (spectral centroid)", series.centroid, +1);
        requireContinuity("Bloom primary (spectral centroid)", series.centroid);
        INFO("Bloom centroid " << series.centroid.front() << " -> " << series.centroid.back()
                               << "\n  series: " << seriesText(series.centroid));
        REQUIRE(series.centroid.back() >= (1.20 * series.centroid.front()));

        requireTrend("Bloom secondary (spectral tilt read-back)", series.tiltDb, +1);
        requireTrend("Bloom secondary (inter-harmonic shimmer band)", series.interHarmonic, +1);
        requireTrend("Bloom secondary (voice width at a pinned orbit rate)", series.widthPct, +1);
        requireTrend("Bloom secondary (M/S side energy)", series.sideEnergy, +1);
        requireTrend("Bloom secondary (L/R correlation)", series.correlation, -1);
    }

    SECTION("FR-063 Dissolve - atmosphere arrives") {
        const DissolveSeries series = sweepDissolve(kNone, true);

        requireTrend("Dissolve primary (atmosphere-band contribution)", series.atmosFraction, +1);
        requireContinuity("Dissolve primary (atmosphere-band contribution)", series.atmosFraction);
        INFO("Dissolve atmosphere fraction " << series.atmosFraction.front() << " -> "
                                             << series.atmosFraction.back()
                                             << "\n  series: " << seriesText(series.atmosFraction));
        REQUIRE((series.atmosFraction.back() - series.atmosFraction.front()) >= 0.15);

        requireTrend("Dissolve secondary (post-note-off tail energy)", series.tailEnergy, +1);
        requireTrend("Dissolve secondary (attack slope over the first 200 ms)", series.attackRatio,
                     -1);
        requireTrend("Dissolve secondary (blur-induced atmosphere side-energy fraction)",
                     series.blurDecorrelation, +1);
    }

    SECTION("FR-064 Gravity - air to stone") {
        const GravitySeries series = sweepGravity(kNone, true);

        requireTrend("Gravity primary (high/low band-energy ratio)", series.bandRatioDb, -1);
        requireContinuity("Gravity primary (high/low band-energy ratio)", series.bandRatioDb);
        INFO("Gravity band ratio " << series.bandRatioDb.front() << " dB -> "
                                   << series.bandRatioDb.back() << " dB"
                                   << "\n  series: " << seriesText(series.bandRatioDb));
        REQUIRE(std::fabs(series.bandRatioDb.back() - series.bandRatioDb.front()) >= 6.0);

        requireTrend("Gravity secondary (richness read-back)", series.richness, -1);
        requireTrend("Gravity secondary (body decay time)", series.bodyDecaySec, -1);
        requireTrend("Gravity secondary (aether size target)", series.aetherSize, +1);
    }

    SECTION("FR-065 Entropy - flatness up") {
        const EntropySeries series = sweepEntropy(kNone, true);
        requireTrend("Entropy primary (spectral flatness)", series.flatness, +1);
        requireContinuity("Entropy primary (spectral flatness)", series.flatness);
        INFO("Entropy flatness " << series.flatness.front() << " -> " << series.flatness.back()
                                 << "\n  series: " << seriesText(series.flatness));
        // RELATIVE, as spec SC-004's effect-size table states for this row and
        // for the same reason Bloom's centroid row is relative: FR-065 wires
        // Entropy to sub-semitone frequency jitter only (kMaxScatterCents = 7,
        // kMaxDriftCents = 50), which is two to three ORDERS OF MAGNITUDE below
        // any absolute flatness figure.
        REQUIRE(series.flatness.back() >= (1.25 * series.flatness.front()));

        requireTrend("Entropy secondary (cloud drift depth read-back)", series.driftCents, +1);
        requireTrend("Entropy secondary (partial-frequency variance)", series.freqVariance, +1);
    }
}

// =============================================================================
// SC-004 Arm 2 - the macros COMPOSE with the deep parameters
// =============================================================================
// Each section runs the macro's PRIMARY arm twice: once at the registered deep
// defaults (Arm 1's construction) and once with the one deep parameter the spec
// names pushed off its default. The secondaries are deliberately not repeated -
// Arm 2's gate is stated over the primary only.

TEST_CASE("Seraphis_MacroAndDeepParameter_Compose", "[.slow]") {
    const Compose kNone{};

    SECTION("Dream composes with kCloudInharmonicityId at 0.060") {
        const DreamSeries base = sweepDream(kNone, false);
        const DreamSeries composed = sweepDream(Compose{kDreamCompose}, false);

        // FR-003's own storage, exact - the deep parameter IS the matrix's base.
        REQUIRE(composed.inharmonicityBase == kDreamComposeBase);
        REQUIRE(base.inharmonicityBase != composed.inharmonicityBase);

        requireFullPartialSupport("Dream composed sweep", composed.partialCounts,
                                  composed.soundingPartials);
        requireTrend("Dream composed primary (deviation from the harmonic grid)",
                     composed.deviation, -1);
        requireComposedEffect("Dream primary", endToEndEffect(base.deviation),
                              endToEndEffect(composed.deviation));
    }

    SECTION("Bloom composes with kCloudRichnessId at 0.45") {
        const BloomSeries base = sweepBloom(kNone, false);
        const BloomSeries composed = sweepBloom(Compose{kBloomCompose}, false);

        REQUIRE(composed.richnessBase == kBloomComposeBase);
        REQUIRE(base.richnessBase != composed.richnessBase);

        requireTrend("Bloom composed primary (spectral centroid)", composed.centroid, +1);
        requireComposedEffect("Bloom primary", endToEndEffect(base.centroid),
                              endToEndEffect(composed.centroid));
    }

    SECTION("Dissolve composes with kAtmosLevelId at 0.30") {
        const DissolveSeries base = sweepDissolve(kNone, false);
        const DissolveSeries composed = sweepDissolve(Compose{kDissolveCompose}, false);

        REQUIRE(composed.atmosLevelBase == kDissolveComposeBase);
        REQUIRE(base.atmosLevelBase != composed.atmosLevelBase);

        requireTrend("Dissolve composed primary (atmosphere-band contribution)",
                     composed.atmosFraction, +1);
        requireComposedEffect("Dissolve primary", endToEndEffect(base.atmosFraction),
                              endToEndEffect(composed.atmosFraction));
    }

    SECTION("Gravity composes with kBodyDampingId at 0.40") {
        const GravitySeries base = sweepGravity(kNone, false);
        const GravitySeries composed = sweepGravity(Compose{kGravityCompose}, false);

        REQUIRE(composed.dampingBase == kGravityComposeBase);
        REQUIRE(base.dampingBase != composed.dampingBase);

        requireTrend("Gravity composed primary (high/low band-energy ratio)", composed.bandRatioDb,
                     -1);
        requireComposedEffect("Gravity primary", endToEndEffect(base.bandRatioDb),
                              endToEndEffect(composed.bandRatioDb));
    }

    SECTION("Entropy composes with kMorphEntropyId at 0.10") {
        const EntropySeries base = sweepEntropy(kNone, false);
        const EntropySeries composed = sweepEntropy(Compose{kEntropyCompose}, false);

        REQUIRE(composed.morphEntropyBase == kEntropyComposeBase);
        REQUIRE(base.morphEntropyBase != composed.morphEntropyBase);

        requireTrend("Entropy composed primary (spectral flatness)", composed.flatness, +1);
        requireComposedEffect("Entropy primary", endToEndEffect(base.flatness),
                              endToEndEffect(composed.flatness));
    }
}

// =============================================================================
// SC-004 Arm 3 - saturation against a deep extreme is LEGAL
// =============================================================================
// Spec C-1 clause 2: with kCloudRichnessId pinned at 1.0, Bloom's +0.40 span on
// CloudRichness is entirely consumed by HarmonicCloud::setRichness' [0,1] clamp
// (harmonic_cloud.h:416). NO EFFECT SIZE IS REQUIRED ON THIS ARM. What IS
// required is that the macro never moves the metric the WRONG way, and that the
// deep parameter is genuinely the base the matrix is evaluating from.
//
// Bloom's other four voice rows (tilt, stereo spread, VoiceWidth, morph target)
// still have headroom, so the centroid is expected to RISE; the assertion is the
// one C-1 states - monotone NON-DECREASING - and never a strict move.

TEST_CASE("Seraphis_MacroSaturatesAgainstDeepExtreme", "[.slow]") {
    const Compose saturating{kSaturatingRichness};

    const BloomSeries swept = sweepBloom(saturating, false);
    REQUIRE(swept.richnessBase == 1.0f);

    // THE DETECTOR'S OWN NOISE FLOOR, measured on a NO-OP SWEEP IN THE SAME
    // RENDER CONFIGURATION: 21 steps with Bloom pinned at its FR-060 neutral and
    // the same saturating richness push. Every step of it is the identical
    // deterministic render, so the floor it reports is the metric's own
    // reproducibility - which is what "monotone non-decreasing" has to be
    // measured against rather than against a bare > 0.
    std::vector<double> noOp;
    noOp.reserve(kSweepSteps);
    for (std::size_t s = 0; s < kSweepSteps; ++s) {
        StepInputs step;
        step.macro = Macro::Bloom;
        step.macroNorm = kMacroNeutral[static_cast<std::size_t>(Macro::Bloom)];
        step.deep = saturating;
        const StepOutputs out = runStep(step);
        noOp.push_back(spectralCentroid(analyseTail(out.mono, kSampleRate)));
    }

    const double floorDown = largestDownwardStep(noOp);
    const double sweptDown = largestDownwardStep(swept.centroid);
    INFO("Arm 3: largest downward centroid step = "
         << sweptDown << " against a no-op floor of " << floorDown
         << "\n  swept: " << seriesText(swept.centroid) << "\n  no-op: " << seriesText(noOp));
    REQUIRE(sweptDown <= floorDown);
}
