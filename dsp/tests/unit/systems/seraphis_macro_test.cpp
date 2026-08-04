// ==============================================================================
// Layer 3: System Tests - SeraphisMacroMatrix, macro sweeps (SC-009, SC-010)
//                                    (specs/seraphis-phase7-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase7-voice-engine/spec.md
//            specs/seraphis-phase7-voice-engine/plan.md   (§4, §6.2)
//            specs/seraphis-phase7-voice-engine/tasks.md  (T013 lands these cases)
//
// SCOPE OF THIS TU: SC-009's per-macro sweep grids (the always-on wiring probe
//   plus the "[.slow]" 5 x 21 x 4 s grid) and SC-010's neutral-is-inert clauses
//   (clause 1 lives in SeraphisVoice_ShipsDocumentedDefaults).
//
// STACK RULE (plan §6.3): heap-allocate every SeraphisEngine and every
//   AetherReverb; never a local.
//
// COMPILE FLAGS: this TU is NOT listed under "-fno-fast-math
//   -fno-finite-math-only" in dsp/tests/CMakeLists.txt and must not be.
//
// WHY THIS TU DRIVES ITS OWN CHAIN INSTEAD OF renderSeraphisChain(). Three of
//   SC-009's observables need a chain variation the FR-070 helper deliberately
//   does not offer: Dream's primary is measured on the DRY voice sum (no reverb
//   at all), Dissolve's primary is a differential against the SAME render with
//   the atmosphere muted AFTER the matrix ran, and SC-010's clause-3 render half
//   needs an arm in which the Aether targets are never pushed. renderChain()
//   below is the helper's six-step order with exactly those three switches
//   added; every other case here keeps the helper's behaviour byte for byte.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

#include <seraphis_chain.h>

#include "render_fingerprint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <span>
#include <sstream>
#include <string>
#include <vector>

using Krate::DSP::AetherReverb;
using Krate::DSP::AtmosphereEngine;
using Krate::DSP::Complex;
using Krate::DSP::FFT;
using Krate::DSP::ModCurve;
using Krate::DSP::SeraphisAetherTargets;
using Krate::DSP::SeraphisEngine;
using Krate::DSP::SeraphisEngineConfig;
using Krate::DSP::SeraphisMacro;
using Krate::DSP::SeraphisMacroMatrix;
using Krate::DSP::SeraphisMacroRow;
using Krate::DSP::SeraphisMacroTarget;
using Krate::DSP::SeraphisMacroTargetOwner;
using Krate::DSP::SeraphisVoice;
using Krate::DSP::SeraphisVoiceConfig;
using Krate::DSP::SpectralMorphEngine;
using Krate::DSP::TestUtils::compareFingerprints;
using Krate::DSP::TestUtils::fingerprintRender;
using Krate::DSP::TestUtils::SeraphisChainScript;
using ChainKind = SeraphisChainScript::Event::Kind;

using Catch::Approx;

namespace {

// =============================================================================
// Pinned configuration
// =============================================================================

constexpr double kSr = 48000.0;
constexpr std::size_t kBlock = 512;

/// Fixed seed and fixed note, as SC-009 requires: every step of every sweep
/// differs ONLY in the swept macro's value.
constexpr std::uint32_t kSeed = 0x5E7A0009u;
constexpr int kNoteMidi = 45;  ///< A2. 110 Hz -> 24 partials still under 2.7 kHz.
constexpr std::uint8_t kVel = 100;

/// The same conversion the allocator uses (voice_allocator.h:514), so the
/// detector's grid is the grid the cloud actually synthesises.
constexpr float kF0 = Krate::DSP::midiNoteToFrequency(kNoteMidi, Krate::DSP::kA4FrequencyHz);

/// SC-009's pinned detector. FFT::prepare validates only power-of-two
/// (fft.h:151) and kMaxFFTSize is documentary, so isPrepared() is asserted on
/// every analysis - a future tightening of that bound must fail loudly instead
/// of silently analysing a zero-size spectrum.
constexpr std::size_t kFftSize = 65536;

/// Peak picking thresholds (SC-009's pinned detector).
constexpr float kPeakFloorRatio = 1.0e-3f;  ///< -60 dB from the strongest peak
constexpr float kPeakSnrRatio = 10.0f;      ///< 20 dB over the local median

/// SC-009's support gate, RE-DERIVED FROM THE SOURCE. **The spec's literal
/// figure (24) is unsatisfiable and this is not a relaxation - it is the
/// strongest gate the shipped synthesis admits.**
///
/// SPEC CONTRADICTION, stated out loud (spec.md:1503-1509 vs FR-019 / FR-041):
/// SC-009 says "a step in which fewer than 24 partials are detected fails the
/// case outright". But FR-041(a) fixes the cloud's active partial count at
/// N(r) = clamp(round(64^r), 1, 64) (harmonic_cloud.h:1458-1463) and FR-019
/// ships richness = 0.60 (seraphis_voice.h:269), so the cloud sounds
/// round(64^0.6) = **12** partials. No detector can find 24 partials in a
/// 12-partial signal; anything above 12 can only come from non-partial peaks
/// (measured: the granular atmosphere alone pushed the count to 24-26 at a 4 s
/// step, which is exactly the silent loss of support the clause exists to
/// prevent). MEASURED with the arm below: 12 detected at EVERY step of the
/// 21-step Dream and Entropy sweeps, at both 1 s and 4 s.
///
/// The gate is therefore "the detector finds EVERY partial the cloud sounds",
/// and the cases pin the sounding count itself (requireFullPartialSupport), so
/// a future change to FR-019's richness fails here loudly instead of silently
/// re-scaling the support.
constexpr std::size_t kMinDetectedPartials = 12;

/// ORDINAL grid matching (spec.md's detector, as amended 2026-07-31): the k-th
/// detected peak in ascending frequency is matched to grid slot k.
///
/// **The nearest-ratio rule this file shipped with is not measurable and the
/// evidence is recorded rather than paraphrased.** `round(f / f0)` makes the
/// per-partial deviation a residual MODULO f0, so it is bounded by f0/2 = 55 Hz
/// here whatever the real deviation is. FR-083's law is
/// `f_n = f0 * n^(1 + g * kGravityExponentRange) * sqrt(1 + B * n^2)`
/// (harmonic_cloud.h:1268-1270, :1331-1334), and at the FR-019 base
/// (g = 0.20, B = 0.030) partial 12 sits near slot 36 - a real deviation of
/// ~2.6 kHz that the modular residual cannot express at all. MEASURED over the
/// 21-step Dream sweep with nearest-ratio matching: 20.5, 23.6, 22.8, 28.4,
/// 27.8, 27.7, 25.2, 19.8, 23.2, 26.9, 25.8, 23.4, 21.1, 29.1, 20.4, 29.5,
/// 25.2, 24.4, 22.7, 18.3, 0.00086 - i.e. uniform-looking noise in [0, 55] for
/// every step at which ANY inharmonicity remains, then exactly 0 at Dream = 1.
/// Spearman rho = -0.232 against the -0.9 gate, with an end-to-end effect size
/// that trivially passes: the metric was reporting an aliasing artefact.
///
/// Ordinal matching is strictly STRONGER, not a relaxation: the deviation is
/// unbounded above, it is `f_n - n*f0 >= 0` monotone in both g and B by
/// construction, and it fails loudly if the detector's peak count ever differs
/// from the count the cloud sounds (requireFullPartialSupport now pins equality
/// in both directions, so a spurious or missing peak shifts every later
/// assignment and cannot pass silently).
constexpr std::size_t kMaxHarmonic = 96;
/// Lobe-group merge half-width for the peak picker, as a fraction of f0. See
/// fitHarmonicGrid() for the measured satellite this exists to absorb.
constexpr double kPartialMergeRatio = 0.5;
constexpr double kPartialSearchMaxHz = 6000.0;
/// Upper edge of the band SC-009's Entropy primary (spectral flatness) is
/// measured over; see spectralFlatnessOf().
constexpr double kFlatnessMaxHz = 8000.0;

/// ============================================================================
/// ENTROPY'S FLATNESS IS A WELCH ESTIMATE OF THE PINNED TAIL, NOT A SINGLE
/// PERIODOGRAM - AND THAT IS A VARIANCE REDUCTION, NOT A LOOSENED GATE.
/// ============================================================================
/// PORTED FROM PHASE 9 (plugins/seraphis/tests/integration/macro_wiring_test.cpp,
/// SC-004 amendment A11 of 2026-08-01). Phase 9's compliance pass established
/// that all four of its SC-004 measurement defects reproduce here, with NO
/// plugin code in the path, and handed the finding back to this TU as the owner.
/// This is one of the four fixes, ported unchanged in substance.
///
/// The window, the band and the transform are EXACTLY the pinned ones. What
/// changes is only the number of draws averaged into the magnitude estimate:
/// four half-length sub-windows spread across the same last second instead of
/// one. The expectation is unmoved; the estimator's variance is divided by the
/// number of segments.
///
/// It is here because the no-discontinuity clause was MEASURED, on a single
/// periodogram, at essentially the bound. The series carries a per-step TREND
/// that is only ~1.4x its step-to-step noise amplitude, and for a series whose
/// step noise is that fraction of its step signal `worst/mean` is a property of
/// the NOISE distribution (for 20 draws of |N(0,s)| its expectation is ~3), so
/// the clause was a coin flip rather than a discontinuity test. There is no
/// discontinuity to find: EntropyProcessor's four stage weights are continuous
/// ramps (entropy_processor.h:66-69, :235-238) and the Entropy row spans
/// entropy 0.20 -> 0.50, which crosses no stage floor except kStage3Lo = 0.50
/// at the very last step, where stageWeight() is 0 by construction.
///
/// MEASURED on the Phase 9 arm, same render, same band, same window, four
/// candidates:
///   single periodogram (was)     rho = 0.968831   worst/mean = 3.00286  X
///   mean of the [2,3)+[3,4) s    rho = 0.996104   worst/mean = 3.53526  X
///   band edge 3 kHz instead      rho = 0.997403   worst/mean = 2.89313  (thin)
///   WELCH, 4 segments (this)     rho = 0.997403   worst/mean = 1.81517  ok
///   WELCH, 8 segments            rho = 0.998701   worst/mean = 2.80624
/// Four segments is the minimum change that clears the bound with margin: it
/// touches only the estimator, not the band (which the 3 kHz candidate would)
/// and not the analysis segment (which the two-window mean would). NO GATE AND
/// NO EFFECT-SIZE FLOOR MOVES WITH IT.
constexpr std::size_t kFlatnessSegments = 4;
/// The band SC-009's Gravity BodyDamping secondary is measured over.
/// ContinuousBody's damping shapes the modal b3 term (continuous_body.h:1576),
/// which is FREQUENCY-DEPENDENT: it damps the upper modes and leaves the
/// fundamental's T60 alone, so a broadband decay estimate cannot see it.
/// MEASURED broadband over the 21-step sweep: 4.53295 -> 4.52963 s, a 0.07 %
/// move, Spearman rho = -0.573.
constexpr double kDampingBandLoHz = 1000.0;
constexpr double kDampingBandHiHz = 8000.0;

/// SC-009's gates.
constexpr double kSpearmanGate = 0.9;
constexpr double kContinuityFactor = 3.0;

/// The FR-019 stereo-spread base, held constant while Bloom's isolated
/// VoiceWidth secondary is measured (plan §4.1.0).
constexpr float kStereoSpreadBase = 0.35f;

/// AetherReverb::kDecayMinSeconds, transcribed because that constant is private
/// (aether_reverb.h:2735). It is the reference arm of Dream's wet-tail null test
/// - the shortest decay the reverb will accept, so the arm's wet field is as
/// close to absent as the shipped range allows. If the floor ever moves, this
/// arm measures a longer reference tail and the null test weakens rather than
/// breaks, which is why the value is cited here rather than assumed.
constexpr float kAetherDecayFloorSec = 0.5f;

// =============================================================================
// Chain driver
// =============================================================================

struct ChainOptions {
    bool applyMacros = true;
    /// FR-059's "apply once" arm. The matrix is applied on the FIRST sub-slice
    /// only, NOT before renderChain(): applying it outside would move the write
    /// to the other side of the note-on, so the two arms would differ in write
    /// ORDER as well as write COUNT and the comparison would no longer isolate
    /// idempotence.
    bool applyOnFirstSliceOnly = false;
    /// SC-010 clause 3's render half needs an arm whose reverb is never touched
    /// after prepare().
    bool pushAetherTargets = true;
    std::function<void(SeraphisEngine&)> postApply;
    std::function<void(const SeraphisEngine&)> observe;
};

void dispatchEvent(SeraphisEngine& engine, const SeraphisChainScript::Event& event) {
    switch (event.kind) {
        case ChainKind::NoteOn:
            engine.noteOn(event.note, event.velocity);
            break;
        case ChainKind::NoteOff:
            engine.noteOff(event.note);
            break;
        case ChainKind::Freeze:
            engine.setAtmosphereFreeze(event.value != std::size_t{0});
            break;
        case ChainKind::Polyphony:
            engine.setPolyphony(event.value);
            break;
    }
}

void pushAether(AetherReverb& reverb, const SeraphisAetherTargets& at) {
    reverb.setMix(at.mix);
    reverb.setSize(at.size);
    reverb.setWidth(at.width);
    reverb.setShimmerOctaveSend(at.shimmerOctaveSend);
    reverb.setShimmerFifthSend(at.shimmerFifthSend);
    reverb.setBloomSend(at.bloomSend);
    reverb.setSizeBreathDepth(at.sizeBreathDepth);
    reverb.setDimensionalityTideDepth(at.dimensionalityTideDepth);
}

/// The FR-070 composition, with the three switches this TU needs.
/// `reverb == nullptr` renders SeraphisEngine::processStereoBlock's DRY voice
/// sum and nothing else, which is exactly what SC-009's Dream primary is
/// measured on.
void renderChain(SeraphisEngine& engine, AetherReverb* reverb, const SeraphisMacroMatrix& macros,
                 const SeraphisChainScript& script, double sampleRate, std::size_t blockSize,
                 std::size_t totalSamples, std::vector<float>& outL, std::vector<float>& outR,
                 const ChainOptions& opt) {
    outL.assign(totalSamples, 0.0f);
    outR.assign(totalSamples, 0.0f);
    if ((totalSamples == std::size_t{0}) || (blockSize == std::size_t{0})) {
        return;
    }

    std::vector<std::size_t> eventAt(script.events.size(), std::size_t{0});
    for (std::size_t e = 0; e < script.events.size(); ++e) {
        const std::uint64_t raw =
            SeraphisChainScript::toSamples(script.events[e].seconds, sampleRate);
        eventAt[e] =
            static_cast<std::size_t>(std::min(raw, static_cast<std::uint64_t>(totalSamples)));
    }

    std::vector<float> dryL(blockSize, 0.0f);
    std::vector<float> dryR(blockSize, 0.0f);
    std::vector<float> wetL(blockSize, 0.0f);
    std::vector<float> wetR(blockSize, 0.0f);
    std::array<float, SeraphisEngine::kBloomPartialCap> buf{};

    std::size_t nextEvent = 0;
    std::size_t blockStart = 0;
    bool macrosApplied = false;
    while (blockStart < totalSamples) {
        const std::size_t blockEnd = std::min(blockStart + blockSize, totalSamples);
        std::size_t sliceStart = blockStart;
        while (sliceStart < blockEnd) {
            while ((nextEvent < eventAt.size()) && (eventAt[nextEvent] <= sliceStart)) {
                dispatchEvent(engine, script.events[nextEvent]);
                ++nextEvent;
            }
            std::size_t sliceEnd = blockEnd;
            if ((nextEvent < eventAt.size()) && (eventAt[nextEvent] < blockEnd)) {
                sliceEnd = eventAt[nextEvent];
            }
            const std::size_t n = sliceEnd - sliceStart;
            if (n == std::size_t{0}) {
                break;
            }

            if (opt.applyMacros && (!opt.applyOnFirstSliceOnly || !macrosApplied)) {
                macros.apply(engine);
                if ((reverb != nullptr) && opt.pushAetherTargets) {
                    pushAether(*reverb, macros.computeAetherTargets());
                }
                macrosApplied = true;
            }
            // Runs AFTER apply() on purpose. SC-009's Dissolve differential is
            // "the identical render with AtmosphereEngine::setLevel(0)", so
            // every other Dissolve row must still be applied; zeroing before
            // apply() would simply be overwritten from the table's base.
            if (opt.postApply) {
                opt.postApply(engine);
            }

            engine.processStereoBlock(dryL.data(), dryR.data(), n);

            if (reverb == nullptr) {
                std::copy_n(dryL.data(), n, outL.data() + sliceStart);
                std::copy_n(dryR.data(), n, outR.data() + sliceStart);
            } else {
                reverb->processStereoBlock(dryL.data(), dryR.data(), wetL.data(), wetR.data(), n);
                engine.processOutputStage(wetL.data(), wetR.data(), n);
                std::copy_n(wetL.data(), n, outL.data() + sliceStart);
                std::copy_n(wetR.data(), n, outR.data() + sliceStart);
            }

            if (opt.observe) {
                opt.observe(engine);
            }

            // Bloom lifecycle - AFTER the render, by plan D4/D5. Consumed even
            // when there is no reverb, so the engine's event queue behaves
            // identically on the dry arm.
            const SeraphisEngine::BloomEvents bloom = engine.consumeBloomEvents();
            if (reverb != nullptr) {
                for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
                    const std::uint32_t bit = std::uint32_t{1} << static_cast<std::uint32_t>(v);
                    if ((bloom.noteOffMask & bit) != 0u) {
                        reverb->bloomNoteOff(static_cast<std::int32_t>(v));
                    }
                }
                for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
                    const std::uint32_t bit = std::uint32_t{1} << static_cast<std::uint32_t>(v);
                    if ((bloom.noteOnMask & bit) == 0u) {
                        continue;
                    }
                    std::size_t count = 0;
                    engine.collectHeldPartials(v, buf.data(), buf.size(), count);
                    if (count > std::size_t{0}) {
                        reverb->bloomNoteOn(static_cast<std::int32_t>(v), buf.data(), count);
                    }
                }
            }

            sliceStart = sliceEnd;
        }
        blockStart = blockEnd;
    }
}

[[nodiscard]] std::unique_ptr<SeraphisEngine> makeEngine(std::size_t polyphony) {
    auto engine = std::make_unique<SeraphisEngine>();
    engine->prepare(kSr, SeraphisEngineConfig{.voice = SeraphisVoiceConfig{},
                                              .polyphony = polyphony,
                                              .seed = kSeed});
    return engine;
}

/// One reverb configuration for every case here, so a difference between two
/// renders is never a difference between two reverbs. Spectral diffusion is off:
/// it is pure cost for these criteria and adds diffusionFftSize of latency to
/// every arm.
[[nodiscard]] std::unique_ptr<AetherReverb> makeReverb() {
    auto reverb = std::make_unique<AetherReverb>();
    reverb->prepare(kSr, AetherReverb::PrepareConfig{.numChannels = std::size_t{8},
                                                     .maxBlockSamples = std::size_t{2048},
                                                     .shimmerEnabled = true,
                                                     .bloomEnabled = true,
                                                     .spectralDiffusionEnabled = false});
    return reverb;
}

[[nodiscard]] SeraphisChainScript::Event noteOnAt(double seconds) {
    return SeraphisChainScript::Event{.seconds = seconds,
                                      .kind = ChainKind::NoteOn,
                                      .note = static_cast<std::uint8_t>(kNoteMidi),
                                      .velocity = kVel,
                                      .value = 0};
}

[[nodiscard]] SeraphisChainScript::Event noteOffAt(double seconds) {
    return SeraphisChainScript::Event{.seconds = seconds,
                                      .kind = ChainKind::NoteOff,
                                      .note = static_cast<std::uint8_t>(kNoteMidi),
                                      .velocity = kVel,
                                      .value = 0};
}

// -----------------------------------------------------------------------------
// Per-slice hooks.
//
// SeraphisEngine::getVoice() is const BY DESIGN (FR-085 keeps it const so tests
// cannot mutate the pool by accident; plan §4.4 gives the matrix write access
// through friendship instead). These two hooks are the only places a test needs
// to write a voice parameter, and the referenced engine is a non-const heap
// object, so the const_cast is well defined.
// -----------------------------------------------------------------------------

/// The single test-side door through getVoice()'s const: the engine object is a
/// non-const heap object, so removing const here is well defined.
SeraphisVoice& mutableVoice(SeraphisEngine& engine, std::size_t v) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<SeraphisVoice&>(engine.getVoice(v));
}

void zeroAtmosphereLevel(SeraphisEngine& engine) {
    for (std::size_t v = 0; v < engine.getPolyphony(); ++v) {
        mutableVoice(engine, v).setLevel(0.0f);
    }
}

/// ============================================================================
/// DISSOLVE'S PRIMARY MUTES THE ATMOSPHERE THROUGH DENSITY, NOT THROUGH LEVEL -
/// AND THAT IS WHAT MAKES THE CRITERION MEASURABLE AT ALL.
/// ============================================================================
/// PORTED FROM PHASE 9 (macro_wiring_test.cpp:1451-1501, SC-004 amendment A11
/// of 2026-08-01), which established the defect with no plugin code in the path
/// and handed it back to this TU.
///
/// A LEVEL mute leaves the swept level in the full arm, so the differential is
/// `E_full - E_muted = (level(d))^2 * E_A + 2 * level(d) * integral(S*A)` - i.e.
/// a CROSS TERM between the atmosphere and everything else, which has no fixed
/// sign and no fixed magnitude. MEASURED on the Phase 9 arm: at 5 s and 7 s per
/// step, with nothing else changed, the fraction came out NEGATIVE over the
/// bottom of the sweep (-0.0142 and -0.0497 at step 0) - an
/// "atmosphere-band contribution" below zero is not a quantity that can be
/// trended. At the pinned 4 s it scored rho = 0.998701 but
/// `worst step / mean step = 3.48571` against the 3.0 no-discontinuity bound.
///
/// NO MACRO ROW WRITES AtmosDensity - the three atmosphere targets are
/// AtmosLevel, AtmosBlur and AtmosDriftDepth (seraphis_macro_matrix.h:56-88) -
/// and this hook runs AFTER macros.apply() on every slice, so the mute holds for
/// the whole sweep. At AtmosphereEngine::kMinDensity = 0.1 grains/s the engine
/// launches ONE grain per ten seconds, i.e. the reference arm is a near-silent
/// atmosphere at EVERY step, and the differential is the atmosphere's WHOLE
/// contribution at the swept level rather than a fixed slice of it. MEASURED on
/// the Phase 9 arm at the pinned 4 s, same window, same band, same everything
/// else: rho = 1, worst/mean = 2.95005, end-to-end 0.052595 -> 0.657805 (+0.605
/// absolute against the >= 0.15 floor).
///
/// THAT THERE IS NO DISCONTINUITY IN THE MAPPING WAS ESTABLISHED SEPARATELY,
/// before the metric was changed: the identical level-muted differential
/// rendered at 8 s and 16 s per step is STRICTLY MONOTONE with rho = 1 and
/// worst/mean = 1.9201 / 1.96039. Those lengths are not shipped because their
/// end-to-end effect sizes (0.0489, 0.0595) are far under the >= 0.15 floor,
/// which no construction may lower.
///
/// The LEVEL-muted arm is still rendered, and only for the blur secondary: with
/// level a pure output gain (atmosphere_engine.h:946-948) the level differential
/// is EXACTLY level(d) * A, a clean scaled copy of the atmosphere whose stereo
/// statistics are scale-invariant. The density differential is
/// A_dense - A_sparse, which is not, so the secondary keeps the arm it was
/// measured on.
///
/// ============================================================================
/// WHERE THIS PORT IS DIFFERENT IN KIND FROM PHASE 9's, MEASURED RATHER THAN
/// ASSUMED - and why the arm is checked in anyway.
/// ============================================================================
/// Phase 9 needed the density mute because it drives this engine through the
/// PARAMETER SURFACE, where AtmosLevel is a Dissolve macro TARGET: a base
/// override cannot zero a swept target, so its level-muted arm removed only the
/// constant 0.50 slice of level and left the cross term. THIS TU writes the
/// voice directly, and zeroAtmosphereLevel() runs AFTER macros.apply() on every
/// slice, so its level mute is already a TRUE mute and there is no cross term
/// to remove.
///
/// MEASURED, this TU, 21 steps x 4 s: swapping the primary's reference arm from
/// zeroAtmosphereLevel to muteAtmosphereDensity moved the series by NOTHING -
/// 0.045637 ... 0.433948 both ways, identical to every printed digit. At
/// kMinDensity = 0.1 grains/s no grain is launched inside a 4 s step at all, so
/// the density-muted atmosphere output is silence, exactly as the level-muted
/// one is. The two arms are the same measurement here; in Phase 9 they are not.
///
/// The arm is checked in regardless, for the reason the equality is fragile:
/// it holds only while this TU's mute is total. If a future change makes
/// zeroAtmosphereLevel partial the way Phase 9's is - a base override, a
/// smoother that never reaches 0, a macro row added on AtmosDensity - the
/// primary keeps measuring the atmosphere's WHOLE contribution instead of
/// silently reverting to a cross-term-contaminated slice, and the two TUs keep
/// measuring the same defined quantity.
void muteAtmosphereDensity(SeraphisEngine& engine) {
    for (std::size_t v = 0; v < engine.getPolyphony(); ++v) {
        mutableVoice(engine, v).setDensity(AtmosphereEngine::kMinDensity);
    }
}

void holdStereoSpreadAtBase(SeraphisEngine& engine) {
    for (std::size_t v = 0; v < engine.getPolyphony(); ++v) {
        mutableVoice(engine, v).setStereoSpread(kStereoSpreadBase);
    }
}

/// The partial detector's arm: put the HARMONIC CLOUD on the output and nothing
/// else. Used only where SC-009 measures partial frequencies (Dream's primary
/// and the probe's wiring check), never on a primary that owns a macro axis.
///
/// SC-009 already takes that arm off the reverb "so reverb smearing cannot
/// corrupt the partial detector"; the same argument applies with more force to
/// the two stages left in the dry sum:
///   - FR-019 ships body mix = 1.0, which the table itself documents as "fully
///     wet, cloud reaches the output only through the resonators"
///     (seraphis_voice.h:290-291). MEASURED on the shipped path: the dry sum is
///     the Glass modal bank's own (inharmonic) response, the fundamental sits
///     48 dB over everything else and the detector found FOUR partials - it was
///     measuring the body, not the grid the metric is defined against.
///   - the granular atmosphere (FR-019 level 0.5) contributes broadband,
///     non-partial peaks; at a 4 s step they alone carried the count from 12 to
///     24-26, i.e. they would have SATISFIED the spec's 24-partial gate with
///     peaks that are not partials at all.
/// Neither row is written by Dream or Entropy (no macro row targets body mix or
/// carries AtmosLevel outside Dissolve), so this conditions the measurement
/// without touching either swept axis.
void openCloudPathForDetector(SeraphisEngine& engine) {
    for (std::size_t v = 0; v < engine.getPolyphony(); ++v) {
        auto& voice = mutableVoice(engine, v);
        voice.setMix(0.0f);    // ContinuousBody dry/wet: 0 = the input, unchanged
        voice.setLevel(0.0f);  // AtmosphereEngine level
    }
}

/// The ISOLATED BodyDamping arm for SC-009's Gravity secondary (plan's
/// "isolate the row under test" pattern, as Bloom's width secondary already
/// does through holdStereoSpreadAtBase).
///
/// Gravity writes four rows; three of them (CloudRichness, CloudSpectralTiltDb,
/// AetherSize) change the EXCITATION's spectrum or the room, not the body's
/// decay law, and at the stone end richness 0.25 sounds only round(64^0.25) = 3
/// partials - almost nothing above 1 kHz for a decay to be measured on. Holding
/// those two cloud rows at their FR-019 base leaves BodyDamping as the only
/// thing moving in this arm, which is what makes the observable a proof that the
/// row exists rather than a restatement of the primary.
void isolateBodyDamping(SeraphisEngine& engine) {
    for (std::size_t v = 0; v < engine.getPolyphony(); ++v) {
        auto& voice = mutableVoice(engine, v);
        voice.setLevel(0.0f);       // AtmosphereEngine out of the tail
        voice.setRichness(0.60f);   // seraphis_voice.h:269
        voice.setSpectralTiltDb(0.0f);  // :271
        // The body's DECAY CLOUD is a parallel texture with its OWN fixed decay
        // (setCloudDecaySec(4.0f), seraphis_voice.h:293) that no macro row
        // writes, so it is a constant floor sitting exactly on top of the
        // quantity this arm measures. MEASURED with it left in: the tail decay
        // read 3.89389 -> 3.89081 s across the whole sweep, i.e. it reported
        // cloudDecaySec and nothing else.
        voice.setCloudMix(0.0f);  // :292
    }
}

void pinOrbitRate(SeraphisEngine& engine) {
    for (std::size_t v = 0; v < engine.getPolyphony(); ++v) {
        // Clamped to OrbitModulator::kMinRate = 0.01 Hz, i.e. a 100 s period:
        // y is effectively constant across a 4 s step, and identical across the
        // sweep because Bloom writes nothing on the orbit.
        mutableVoice(engine, v).setSpatialRate(0.0f);
    }
}

// =============================================================================
// One rendered sweep step
// =============================================================================

struct StepInputs {
    SeraphisMacro macro = SeraphisMacro::Dream;
    float value = 0.0f;
    double seconds = 4.0;
    bool composed = true;
    bool withNoteOff = false;
    /// Where in the step the note-off lands, as a fraction of `seconds`.
    double noteOffAtFraction = 0.5;
    /// Reverb decay for this arm, in seconds. `<= 0` leaves prepare()'s default
    /// in place, which is what every arm but Dream's wet-tail REFERENCE uses.
    /// NOTHING in renderChain touches decay - pushAether() writes mix, size,
    /// width, the three sends and the two depths, and no more - so one call
    /// after prepare() HOLDS for the whole render.
    float reverbDecaySec = 0.0f;
    std::function<void(SeraphisEngine&)> preRender;
    std::function<void(SeraphisEngine&)> postApply;
    std::function<void(const SeraphisEngine&)> observe;
};

struct StepOutputs {
    std::vector<float> left;
    std::vector<float> right;
    std::vector<float> mono;
};

[[nodiscard]] StepOutputs runStep(const StepInputs& in) {
    auto engine = makeEngine(std::size_t{1});
    std::unique_ptr<AetherReverb> reverb;
    if (in.composed) {
        reverb = makeReverb();
        if (in.reverbDecaySec > 0.0f) {
            reverb->setDecaySeconds(in.reverbDecaySec);
        }
    }

    SeraphisMacroMatrix macros;
    macros.setMacro(in.macro, in.value);

    if (in.preRender) {
        in.preRender(*engine);
    }

    SeraphisChainScript script;
    script.events.push_back(noteOnAt(0.0));
    if (in.withNoteOff) {
        script.events.push_back(noteOffAt(in.noteOffAtFraction * in.seconds));
    }

    const auto total = static_cast<std::size_t>(in.seconds * kSr);

    ChainOptions opt;
    opt.postApply = in.postApply;
    opt.observe = in.observe;

    StepOutputs out;
    renderChain(*engine, reverb.get(), macros, script, kSr, kBlock, total, out.left, out.right,
                opt);
    out.mono.assign(total, 0.0f);
    for (std::size_t i = 0; i < total; ++i) {
        out.mono[i] = 0.5f * (out.left[i] + out.right[i]);
    }
    return out;
}

// =============================================================================
// Metrics
// =============================================================================

struct TailSpectrum {
    std::vector<float> mag;
    double binHz = 0.0;
    bool valid = false;
};

/// SC-009's pinned analysis: the LAST 1 s of the step, 4-term Blackman-Harris,
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

/// A WELCH estimate of the same pinned tail - `segments` half-length
/// sub-windows spread across the last second, magnitudes averaged. A single
/// periodogram's per-bin value is a one-draw estimate; averaging K of them
/// divides the estimator's variance by K without moving its expectation. See
/// kFlatnessSegments for the measured candidates and the Phase 9 provenance.
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

/// SC-009's Gravity primary: energy above vs below `splitHz`, in dB.
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
/// This stands in for SC-009's "wet-tail energy in the +12/+7 shimmer bands" and
/// the substitution is forced by the source: the +12 (x2) shimmer image of a
/// harmonic series lands exactly on harmonic 2n and is degenerate with the dry
/// grid, so it cannot discriminate anything. The +7 (x1.5) image lands on
/// (n + 0.5) * f0, which the dry voice never produces - that is the band that
/// carries the shimmer/bloom send information.
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

/// SC-009's pinned peak picker: -60 dB from max, >= 20 dB peak-to-local-median
/// SNR, parabolic interpolation on the LOG magnitude, peaks matched to grid
/// slots ORDINALLY (k-th peak by ascending frequency -> slot k) - see the
/// comment on kMaxHarmonic for why nearest-ratio matching cannot measure this.
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
        std::nth_element(medianScratch.begin(), medianScratch.begin() + static_cast<std::ptrdiff_t>(mid),
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

    // ONE PARTIAL PER LOBE GROUP. Standard partial tracking, and load-bearing for
    // the ordinal rule: a single partial can present as two adjacent maxima, and
    // one extra entry re-indexes every slot above it.
    //
    // MEASURED on this case's dry arm at the FR-019 base: partial 2 (236.08 Hz,
    // FR-083's `f0 * 2^1.02 * sqrt(1 + 0.03*4)`) is accompanied by a satellite
    // 4.4 Hz above it, which took the count to 13 at Dream = 0.00 .. 0.15 and to
    // 12 from Dream = 0.20 up. The FR-083 grid never places two partials closer
    // than `f_2 - f_1` (~124 Hz here), so kPartialMergeRatio * f0 = 55 Hz is far
    // below the closest LEGITIMATE spacing and far above the observed satellite:
    // the rule cannot merge two real partials at this f0 and richness.
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

[[nodiscard]] double energyOf(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double e = 0.0;
    const std::size_t hi = std::min(to, x.size());
    for (std::size_t i = std::min(from, hi); i < hi; ++i) {
        const double v = static_cast<double>(x[i]);
        e += v * v;
    }
    return e;
}

[[nodiscard]] double rmsOf(const std::vector<float>& x, std::size_t from, std::size_t to) {
    const std::size_t hi = std::min(to, x.size());
    const std::size_t lo = std::min(from, hi);
    if (hi <= lo) {
        return 0.0;
    }
    return std::sqrt(energyOf(x, lo, hi) / static_cast<double>(hi - lo));
}

/// SC-009's Entropy primary: spectral flatness (geometric mean / arithmetic mean
/// of the magnitude spectrum) computed on the SAME pinned tail spectrum every
/// other metric here uses.
///
/// WHY NOT SignalMetrics::calculateSpectralFlatness, which this file called
/// first: that helper caps its transform at 4096 points and windows only the
/// FIRST 4096 samples handed to it (signal_metrics.h:336-352), i.e. one 85 ms
/// Hann frame at 11.7 Hz resolution. FR-065's headline row is
/// setDriftDepthCents 0 -> kMaxDriftCents = 50, and 50 cents at this case's
/// f0 = 110 Hz is +-3.2 Hz - INSIDE a single bin of that helper, so the row's
/// whole effect (drift broadening each partial and filling the floor between
/// partials) is invisible to it and what it reports is which 85 ms of the tail
/// the frame happened to land on. MEASURED over an 11-step Entropy sweep: the
/// 4096-point helper gave a non-monotone 0.00858 -> 0.00696 (Spearman -0.7),
/// the pinned 65536-point spectrum below gives a monotone 0.001148 -> 0.001541.
/// The quantity is the same; only the resolution and the observation length
/// differ, and the pinned pair is the one SC-009 fixes for this case.
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

/// Fraction of the render's energy that the atmosphere contributes (SC-009's
/// Dissolve primary), measured over the SETTLED window - the same "last 1 s of
/// each 4 s step" segment SC-009 pins for every other metric in this case.
///
/// The window matters here more than anywhere else in the file, because
/// Dissolve's own envelope-slew rows move the render's energy in time:
/// CloudAttackTimeSec goes 0.05 -> 2.0 s and EnvStage0Ms 2000 -> 6000 ms, so at
/// the top of the sweep a whole-render integral is dominated by an attack the
/// atmosphere is still 4 s of capture ring behind. MEASURED whole-render:
/// 0.00943 -> 0.03263 with a plateau from step 12 to step 17, i.e. the axis
/// appeared to stop moving exactly where the slew rows took over.
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

/// Pearson correlation of the two channels. Used ONLY by the Dissolve blur
/// secondary, whose arm is an anti-phase differential the side-energy fraction
/// reads backwards; the banner at that call site records the measurement, and
/// the one in sweepBloom() records why the stereo row there uses the other
/// statistic.
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

// =============================================================================
// SC-009's statistics
// =============================================================================

/// Spearman rank correlation of `y` against its own index, average ranks on
/// ties. SC-009's gate is a monotone TREND (|rho| >= 0.9), not strictness.
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

/// ============================================================================
/// SC-009's NO-DISCONTINUITY CLAUSE BOUNDS A STEP'S DEPARTURE FROM ITS
/// NEIGHBOURS, NOT THE STEP ITSELF. BOUNDING THE STEP ITSELF CANNOT TELL A JUMP
/// FROM CONVEXITY, AND THAT IS MEASURED HERE RATHER THAN ARGUED.
/// ============================================================================
/// PHASE 7 TEST-QUALITY FIX, ruled by the phase owner on 2026-08-02 and recorded
/// as item 2 of "Before the commit gate can open - phase-owner decisions
/// required" in specs/seraphis-phase9-parameters/compliance.md. It is NOT one of
/// the four Phase 9 estimator fixes ported into this TU on the same date (see
/// the banners at kFlatnessSegments, muteAtmosphereDensity, isolateBodyDamping
/// and the Dissolve blur row): all four of those are green, and this defect
/// SURVIVED them - the density-mute port was a measured no-op for this series.
///
/// THE DEFECT. `max|dy| <= kContinuityFactor * mean|dy|` is a statement about
/// the STEP-SIZE DISTRIBUTION, and a smooth series whose step size TRENDS - any
/// convex or concave sweep sampled on a uniform macro grid - carries a large
/// step at one end for reasons that have nothing to do with continuity. MEASURED
/// on the Dissolve primary (21 steps x 4 s; the series is transcribed verbatim
/// into SeraphisEngine_MacroSweepContinuityMetric below, so these numbers are
/// re-derived by the suite rather than trusted): the series is smooth and
/// STRICTLY MONOTONE, Spearman rho = 1 exactly, yet it scored
/// worst 0.0787760 / mean 0.0194156 = 4.05737 against the 3.0 bound. There is no
/// step in it. It is convex, and `worst/mean` was reading the convexity.
///
/// THE FIX, AND WHAT DID NOT MOVE. kContinuityFactor stays 3.0 and still
/// multiplies the SAME reference - the mean step change - so the doc sentence
/// this clause always carried is still literally true of the constant. NO NEW
/// BOUND CONSTANT IS INTRODUCED, and there is nothing here to tune. Only the
/// quantity being bounded changes: from the step itself to the step's DEPARTURE
/// from the mean of its two neighbouring steps (from the single available
/// neighbour at each end). For a step sequence that is locally linear in the
/// step index - i.e. for every smooth sweep, convex or concave - that departure
/// vanishes by construction; for a genuine jump it IS the jump.
///
/// MEASURED, every continuity-gated series in this TU, old statistic -> new:
///   Dream primary, deviation      1.35573 -> 0.07790
///   Bloom primary, centroid       2.42212 -> 0.19050
///   Dissolve primary, atmos frac  4.05737 -> 2.29986   <- the failing row
///   Gravity primary, band ratio   1.63211 -> 0.88122
///   Entropy primary, 21 x 4 s     2.56872 -> 1.51242
///   Entropy primary, 5 x 3 s prob 1.34240 -> 0.81043   <- the always-on case
/// and on closed-form shapes: a linear ramp 1.0 -> 0 exactly, i^2 1.95 -> 0.10,
/// i^3 2.8525 -> 0.285, 1.12^i 2.39069 -> 0.25615.
///
/// THE NEGATIVE CONTROL - A JUMP STILL FAILS, and the case below RUNS it instead
/// of asserting it in prose. Injecting one jump J at step 10 of the MEASURED
/// Dissolve series, every other value untouched:
///   J = 3.0 x mean step -> 2.52703  passes
///   J = 3.5 x mean step -> 2.89879  passes
///   J = 4.0 x mean step -> 3.25507  FAILS
/// with the exact rejection threshold at J = 0.0706708 = 3.6399 x mean step. On
/// a uniform ramp of baseline step b over 21 points the closed forms are: the
/// old clause rejects J > 2.36 b, this one rejects J > 3.53 b.
///
/// THAT 1.5x IS A REAL COST AND IT IS STATED, NOT HIDDEN. Bounding the departure
/// rather than the step gives up sensitivity to an isolated jump by exactly that
/// factor, because the departure drops the baseline step the jump rides on while
/// the reference still carries it. What it buys is total blindness to the trend
/// in step size, which is the entire defect. THE TWO ALTERNATIVES THE RULING
/// NAMED WERE BOTH MEASURED AND ARE WORSE, not merely different:
///   - LOG-DOMAIN STEP RATIOS (|ln(y_i / y_i-1)|) are not defined on two of the
///     six series. The Gravity primary is a dB ratio running -23.6785 to
///     -68.9356 (no logarithm of a negative), and the Dream primary ends at
///     8.62984e-4 Hz, where the log step explodes: MEASURED worst/mean =
///     16.0072, i.e. the log domain turns the one row whose deviation falls in a
///     near-perfect straight line (new statistic 0.0779) into the worst failure
///     in the file.
///   - A LOCAL (WINDOWED) MEAN as the reference has no admissible radius. Taking
///     the step OUT of its own window (leave-one-out) makes the bound a contest
///     against 2-4 noisy neighbours, and it FAILS the currently-green Entropy
///     primary at every radius that keeps the Dissolve row green: MEASURED
///     Entropy 3.29680 at r = 2, 4.66270 at r = 3, 3.08908 at r = 4; at r = 5 it
///     finally clears (2.87355) but Dissolve has gone back over (3.35237).
///     Leaving the step IN its own window caps the ratio at the window length w,
///     so w must exceed kContinuityFactor for the clause to be able to fail at
///     all (w >= 5), and every admissible w is LESS sensitive to a jump than the
///     departure form: w = 5 rejects only J > 5 b against 3.53 b here.
[[nodiscard]] double continuityDeparture(const std::vector<double>& y) {
    if (y.size() < 3) {
        return 0.0;
    }
    std::vector<double> step(y.size() - 1, 0.0);
    for (std::size_t i = 1; i < y.size(); ++i) {
        step[i - 1] = std::fabs(y[i] - y[i - 1]);
    }
    double worst = 0.0;
    for (std::size_t i = 0; i < step.size(); ++i) {
        double neighbours = 0.0;
        if (i == std::size_t{0}) {
            neighbours = step[1];
        } else if ((i + 1) == step.size()) {
            neighbours = step[i - 1];
        } else {
            neighbours = 0.5 * (step[i - 1] + step[i + 1]);
        }
        worst = std::max(worst, std::fabs(step[i] - neighbours));
    }
    return worst;
}

/// The reference `kContinuityFactor` multiplies: the mean step change, unchanged
/// from the clause this file shipped with.
[[nodiscard]] double meanStepChange(const std::vector<double>& y) {
    if (y.size() < 2) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = 1; i < y.size(); ++i) {
        sum += std::fabs(y[i] - y[i - 1]);
    }
    return sum / static_cast<double>(y.size() - 1);
}

/// SC-009's no-discontinuity clause: no step may depart from its neighbouring
/// steps by more than `kContinuityFactor` times the mean step change.
[[nodiscard]] bool withinContinuityBound(const std::vector<double>& y) {
    if (y.size() < 3) {
        return true;
    }
    const double worst = continuityDeparture(y);
    const double mean = meanStepChange(y);
    if (!(mean > 0.0)) {
        return !(worst > 0.0);
    }
    return worst <= (kContinuityFactor * mean);
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
    const double rho = spearmanAgainstIndex(series);
    INFO(label << ": Spearman rho = " << rho << ", first = " << series.front()
               << ", last = " << series.back() << "\n  series: " << seriesText(series));
    if (direction > 0) {
        REQUIRE(rho >= kSpearmanGate);
    } else {
        REQUIRE(rho <= -kSpearmanGate);
    }
}

/// SC-009's support clause, in the form the shipped synthesis admits: the
/// detector must find EVERY partial the cloud sounds, at every step. See
/// kMinDetectedPartials for why the spec's literal 24 cannot be met by any
/// detector (FR-019's richness sounds 12 partials, FR-041's N(r)).
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
    // shifts it the other way, so a count that is merely ">= sounding" would let
    // the deviation metric silently measure a re-indexed grid.
    REQUIRE(minPartials == soundingPartials);
    REQUIRE(maxPartials == soundingPartials);
}

void requireContinuity(const char* label, const std::vector<double>& series) {
    const double mean = meanStepChange(series);
    const double worst = continuityDeparture(series);
    INFO(label << ": continuity, " << series.size() << " steps, worst departure " << worst
               << " / mean step " << mean << " = " << ((mean > 0.0) ? (worst / mean) : 0.0)
               << " against a bound of " << kContinuityFactor << "\n  series: "
               << seriesText(series));
    REQUIRE(withinContinuityBound(series));
}

// =============================================================================
// Per-macro sweeps
// =============================================================================

[[nodiscard]] float sweepValue(std::size_t step, std::size_t steps) {
    if (steps < 2) {
        return 0.0f;
    }
    return static_cast<float>(step) / static_cast<float>(steps - 1);
}

struct DreamSeries {
    std::vector<double> deviation;
    std::vector<double> wetTail;
    std::vector<double> azimuthTv;
    std::vector<double> morphEntropy;
    /// Detected peak count per step; the support gate reads its extrema.
    std::vector<double> partialCounts;
    /// HarmonicCloud::getActivePartialCount() on the detector arm - the ceiling
    /// the support gate is measured against (FR-041's N(r)).
    std::size_t soundingPartials = 0;
};

[[nodiscard]] DreamSeries sweepDream(std::size_t steps, double seconds) {
    DreamSeries out;
    for (std::size_t s = 0; s < steps; ++s) {
        const float value = sweepValue(s, steps);

        // Primary: the DRY voice sum, no reverb anywhere in the path, so the
        // Aether `mix` row cannot smear the partial detector (SC-009).
        double azTv = 0.0;
        double prevAz = 0.0;
        bool firstAz = true;
        double lastEntropy = 0.0;
        std::size_t sounding = 0;
        StepInputs dry;
        dry.macro = SeraphisMacro::Dream;
        dry.value = value;
        dry.seconds = seconds;
        dry.composed = false;
        dry.postApply = openCloudPathForDetector;
        dry.observe = [&azTv, &prevAz, &firstAz, &lastEntropy, &sounding](const SeraphisEngine& e) {
            const auto az = static_cast<double>(e.getVoice(0).getSpatialAzimuth());
            if (!firstAz) {
                azTv += std::fabs(az - prevAz);
            }
            prevAz = az;
            firstAz = false;
            lastEntropy = static_cast<double>(e.getVoice(0).morph().entropy().getEntropy());
            sounding = e.getVoice(0).cloud().getActivePartialCount();
        };
        const StepOutputs dryOut = runStep(dry);
        const TailSpectrum spectrum = analyseTail(dryOut.mono, kSr);
        const PartialFit fit = fitHarmonicGrid(spectrum, static_cast<double>(kF0));
        out.partialCounts.push_back(static_cast<double>(fit.count));
        out.soundingPartials = sounding;
        out.deviation.push_back(fit.meanAbsDevHz);
        out.azimuthTv.push_back(azTv);
        out.morphEntropy.push_back(lastEntropy);

        // Secondary: the reverb-send axis, on the composed chain with a
        // note-off.
        //
        // ===================================================================
        // MEASURED AS A NULL TEST AGAINST A SHORT-DECAY REFERENCE ARM, NOT AS
        // PLAIN TAIL ENERGY - PORTED FROM PHASE 9 (macro_wiring_test.cpp:
        // 1340-1379, SC-004 amendment A11 of 2026-08-01).
        // ===================================================================
        // AetherReverb::setMix is a CROSSFADE, not a send: the output is
        // `dry*(1-m) + wet*m` (aether_reverb.h:2336). Dream's AetherMix row is
        // base 0.35 amount +0.35 (seraphis_macro_matrix.h:217-222), so over the
        // sweep the DRY field loses (1-m)^2 exactly as fast as the wet field
        // gains m^2, and the voice's own long release keeps the dry field loud
        // right through the measured window. MEASURED here as plain total tail
        // energy, the series is therefore U-SHAPED and cannot reach
        // |rho| >= 0.9 for a CORRECT implementation: this case scored
        // rho = 0.802597, and Phase 9's own copy of the observable scored
        // 0.809091 with the series falling 62 % over its first five steps WHILE
        // the send was rising.
        //
        // THE FIX IS A NULL TEST, and it is exact rather than approximate. The
        // same step is rendered a second time with the reverb's decay at its
        // floor (kAetherDecayFloorSec = AetherReverb::kDecayMinSeconds =
        // 0.5 s), which pushAether() never writes, so the push HOLDS. Both arms are the same binary, the
        // same seed, the same note and the same crossfade position m, so their
        // DRY fields are bit-identical and the per-sample difference is exactly
        // `m * (wet_long - wet_short)` - a quantity containing NO dry field.
        // MEASURED on the Phase 9 arm: 0.000131193 -> 0.137118 (a 1000x rise),
        // rho = 0.972727 against the same >= 0.9 gate.
        StepInputs wet;
        wet.macro = SeraphisMacro::Dream;
        wet.value = value;
        wet.seconds = seconds;
        wet.composed = true;
        wet.withNoteOff = true;
        const StepOutputs wetOut = runStep(wet);

        StepInputs wetRef = wet;
        wetRef.reverbDecaySec = kAetherDecayFloorSec;
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

struct BloomSeries {
    std::vector<double> centroid;
    std::vector<double> tiltDb;
    std::vector<double> interHarmonic;
    std::vector<double> widthPct;
    std::vector<double> sideEnergy;
};

[[nodiscard]] BloomSeries sweepBloom(std::size_t steps, double seconds) {
    BloomSeries out;
    for (std::size_t s = 0; s < steps; ++s) {
        const float value = sweepValue(s, steps);

        double lastTilt = 0.0;
        StepInputs wetStep;
        wetStep.macro = SeraphisMacro::Bloom;
        wetStep.value = value;
        wetStep.seconds = seconds;
        wetStep.composed = true;
        wetStep.observe = [&lastTilt](const SeraphisEngine& e) {
            lastTilt = static_cast<double>(e.getVoice(0).cloud().getSpectralTiltDb());
        };
        const StepOutputs mainOut = runStep(wetStep);
        const TailSpectrum spectrum = analyseTail(mainOut.mono, kSr);
        out.centroid.push_back(spectralCentroid(spectrum));
        out.tiltDb.push_back(lastTilt);
        out.interHarmonic.push_back(interHarmonicRatio(spectrum, static_cast<double>(kF0)));

        // The ISOLATED VoiceWidth arm (plan §4.1.0): the orbit is pinned so y is
        // constant across the sweep and setStereoSpread is held at its FR-019
        // base, so CloudStereoSpread cannot carry this secondary.
        double lastWidth = 0.0;
        StepInputs isolated;
        isolated.macro = SeraphisMacro::Bloom;
        isolated.value = value;
        isolated.seconds = seconds;
        isolated.composed = true;
        isolated.preRender = pinOrbitRate;
        isolated.postApply = holdStereoSpreadAtBase;
        isolated.observe = [&lastWidth](const SeraphisEngine& e) {
            lastWidth = static_cast<double>(e.getVoice(0).getSpatialWidthPercent());
        };
        const StepOutputs isolatedOut = runStep(isolated);
        out.widthPct.push_back(lastWidth);
        // THE STEREO OBSERVABLE ON THIS ARM IS THE M/S SIDE-ENERGY FRACTION, AND
        // THE L/R-CORRELATION ROW THAT USED TO SIT BESIDE IT IS GONE BECAUSE IT
        // ASSERTED THE SAME CLAIM WITH A STATISTIC THAT CANNOT CARRY IT HERE.
        // PHASE 7 TEST-QUALITY FIX, ruled by the phase owner on 2026-08-02
        // (item 2 of "phase-owner decisions",
        // specs/seraphis-phase9-parameters/compliance.md). It is the SAME
        // substitution the Dissolve blur secondary already carries below, for
        // the same reason and with the same helper.
        //
        // Both rows were computed on THIS render - the isolated-VoiceWidth arm,
        // orbit pinned and stereo spread held at its FR-019 base - and both
        // asserted one directional claim: the image WIDENS as Bloom rises.
        // MEASURED over the 21-step x 4 s sweep on that arm:
        //   M/S side-energy fraction  0.0129345 -> 0.0245504, rho = +0.914286
        //   L/R correlation           0.975521  -> 0.958550,  rho = -0.855844
        //
        // WHY THE CORRELATION REVERSES AND THE SIDE FRACTION DOES NOT, in the
        // algebra rather than by assertion. For channel powers P_L, P_R and
        // cross term C, the side fraction is `1/2 - C/(P_L + P_R)` while
        // Pearson's rho is `C / sqrt(P_L * P_R)`: the two agree exactly only
        // while P_L == P_R, because one normalises the cross term by the
        // ARITHMETIC mean of the channel powers and the other by their GEOMETRIC
        // mean. VoiceWidth is an M/S re-matrix, so it moves the channel powers
        // as well as the cross term, and rho is blind to precisely that part of
        // the move. MEASURED consequence: over the last six steps the
        // correlation gives back 22.02 % of its swing (min 0.953758 at step 14,
        // back to 0.958550) where the side fraction gives back 7.18 % (peak
        // 0.0254486 at step 15, back to 0.0245504) - and on a total correlation
        // swing of just 1.74 % of its own value that is the whole difference
        // between +0.914286 and -0.855844 against a 0.9 gate.
        //
        // NO GATE MOVED and the claim is not weaker: the surviving row is a
        // 1.90x end-to-end rise on the arm that isolates VoiceWidth alone, which
        // is the observable's whole purpose (without this arm CloudStereoSpread
        // carries the stereo secondary and a broken VoiceWidth row passes).
        out.sideEnergy.push_back(sideEnergyFraction(isolatedOut.left, isolatedOut.right));
    }
    return out;
}

struct DissolveSeries {
    std::vector<double> atmosFraction;
    std::vector<double> tailEnergy;
    std::vector<double> attackRatio;
    std::vector<double> blurDecorrelation;
};

[[nodiscard]] DissolveSeries sweepDissolve(std::size_t steps, double seconds) {
    DissolveSeries out;
    for (std::size_t s = 0; s < steps; ++s) {
        const float value = sweepValue(s, steps);

        StepInputs full;
        full.macro = SeraphisMacro::Dissolve;
        full.value = value;
        full.seconds = seconds;
        full.composed = true;
        const StepOutputs fullOut = runStep(full);

        // THE PRIMARY'S REFERENCE ARM: the atmosphere muted through DENSITY.
        // See muteAtmosphereDensity() for why a LEVEL mute cannot measure this.
        StepInputs densityMuted = full;
        densityMuted.postApply = muteAtmosphereDensity;
        const StepOutputs densityMutedOut = runStep(densityMuted);

        out.atmosFraction.push_back(
            atmosphereFraction(fullOut.mono, densityMutedOut.mono, kSr));

        // The LEVEL-muted arm, rendered for the blur secondary only (banner on
        // muteAtmosphereDensity): the level differential is a clean scaled copy
        // of the atmosphere, which is the arm the secondary was measured on.
        StepInputs muted = full;
        muted.postApply = zeroAtmosphereLevel;
        const StepOutputs mutedOut = runStep(muted);

        const auto twoHundredMs = static_cast<std::size_t>(0.2 * kSr);
        const auto oneSecond = static_cast<std::size_t>(kSr);
        const double early = rmsOf(fullOut.mono, 0, twoHundredMs);
        const double whole = rmsOf(fullOut.mono, 0, oneSecond);
        out.attackRatio.push_back((whole > 0.0) ? (early / whole) : 0.0);

        // SC-009's blur observable, measured on the ATMOSPHERE'S OWN
        // CONTRIBUTION: `full - muted` per SAMPLE. Both arms are deterministic
        // and differ only in one output-gain setter
        // (atmosphere_engine.h:946-948), so the difference IS the atmosphere
        // path, and it costs no extra render.
        //
        // **THE OBSERVABLE IS L/R DECORRELATION, NOT SPECTRAL SPREAD, and the
        // reason is in the blur stage's own source.** atmosphere_engine.h:
        // 2050-2052 says it outright: *"MAGNITUDE IS NEVER WRITTEN - only the
        // phase moves, so the stage is a decoherer and not a filter"*, and
        // :2062-2066 names the consequence this row is gated on: *"The draw is
        // PER BIN PER CHANNEL from the one blurRng_ stream, which is what makes
        // blur produce progressive stereo decorrelation as well as fog."* A
        // phase-only stage smears in TIME; it does not broaden a magnitude
        // spectrum, and the 75 %-overlap OLA that reassembles it cancels the
        // upper bins hardest, so spread moves the WRONG WAY by construction.
        // MEASURED on the atmosphere-only differential: spectral spread fell
        // 686.1 -> 525.4 Hz, Spearman rho = -1.0 against a "+0.9" gate.
        const std::size_t atmosLen = std::min(fullOut.left.size(), mutedOut.left.size());
        std::vector<float> atmosOnlyL(atmosLen, 0.0f);
        std::vector<float> atmosOnlyR(atmosLen, 0.0f);
        for (std::size_t i = 0; i < atmosLen; ++i) {
            atmosOnlyL[i] = fullOut.left[i] - mutedOut.left[i];
            atmosOnlyR[i] = fullOut.right[i] - mutedOut.right[i];
        }
        // ====================================================================
        // THE STATISTIC IS `1 - |rho_LR|`, AND PHASE 9's A11 SWAP TO THE
        // SIDE-ENERGY FRACTION DOES NOT PORT TO THIS ROW. MEASURED, BOTH WAYS,
        // ON THIS TU's ARM.
        // ====================================================================
        // FOUND AND FIXED 2026-08-02, IN THE SAME PASS AS THE TWO ROWS THE
        // PHASE OWNER RULED ON BUT **NOT AMONG THEM** - flagged as such in this
        // banner on purpose. This row was invisible to that ruling for a
        // mechanical reason: Catch2 abandons a SECTION at its first REQUIRE
        // failure, the Dissolve continuity failure sat three assertions ahead of
        // this one, and so this assertion had NEVER RUN. Fixing the continuity
        // clause is what exposed it. If the phase owner disagrees with the
        // reading below, this is the row to reverse.
        //
        // THE A11 SWAP WAS RIGHT ON PHASE 9's ARM AND IS WRONG ON THIS ONE, for
        // the reason this file already documents one screen up
        // (muteAtmosphereDensity's "WHERE THIS PORT IS DIFFERENT IN KIND"
        // banner): Phase 9 drives the engine through the PARAMETER SURFACE,
        // where its level mute is partial and its `full - muted` differential
        // keeps a cross term; THIS TU writes the voice directly after
        // macros.apply() on every slice, so its differential is the atmosphere
        // and nothing else. The two arms are not the same signal, and the
        // stereo statistic that suits one does not suit the other.
        //
        // MEASURED HERE, 21 steps x 4 s, both statistics off the SAME renders:
        //   signed rho_LR   -0.574786 -> -0.321489   (|rho| falls monotonically)
        //   1 - |rho_LR|     0.425214 ->  0.678511   Spearman +0.998701  ok
        //   M/S side energy  0.768809 ->  0.651181   Spearman -0.996104  X
        //
        // WHY THE SIDE FRACTION INVERTS HERE, in the algebra rather than by
        // assertion. side/(side+mid) = 1/2 - C/(P_L + P_R): it is monotone in
        // the SIGNED cross term, not in its magnitude. This differential starts
        // already strongly ANTI-PHASE (rho = -0.5748, side fraction 0.7688,
        // because the atmosphere ships pan spread 0.7 and decorrelation 0.5,
        // seraphis_voice.h:304-305), and blur drives the two channels toward
        // INDEPENDENCE - rho -> 0 - which is what "The draw is PER BIN PER
        // CHANNEL from the one blurRng_ stream, which is what makes blur produce
        // progressive stereo decorrelation" (atmosphere_engine.h:2062-2066)
        // literally says. Moving from rho = -0.57 toward 0 LOWERS the side
        // fraction from 0.77 toward 0.5, so the side fraction reports
        // progressive decorrelation as a narrowing image. Phase 9's contaminated
        // differential starts near rho = -0.18 and moves AWAY from zero, which
        // is the mirror case and is why the swap was correct there.
        //
        // `1 - |rho|` has the complementary blind spot - it cannot tell
        // anti-phase from in-phase - and that is exactly why Bloom's isolated
        // VoiceWidth secondary above uses the side fraction instead: an M/S
        // width re-matrix moves the channel POWERS, which rho normalises away.
        // Each row uses the statistic whose blind spot its arm does not sit in,
        // and both banners record the measurement that decides it.
        //
        // NO GATE MOVED: this row is a SECONDARY, so only the +0.9 trend gate
        // applies (the no-discontinuity clause is stated over primaries), and
        // the direction claim is FR-063's unchanged "blur ^ -> decorrelation ^".
        out.blurDecorrelation.push_back(1.0 - std::fabs(correlation(atmosOnlyL, atmosOnlyR)));

        StepInputs tail = full;
        tail.withNoteOff = true;
        const StepOutputs tailOut = runStep(tail);
        const std::size_t total = tailOut.mono.size();
        out.tailEnergy.push_back(energyOf(tailOut.mono, (total * 3) / 4, total));
    }
    return out;
}

/// SC-009's Gravity secondary "body decay time v (damping ^)", MEASURED off the
/// render instead of read back.
///
/// **ContinuousBody::getEngineT60Sec() cannot observe this and the evidence is
/// recorded rather than paraphrased.** continuous_body.h:1576-1578 states the
/// law in the source: *"FR-036: one law, three engines. Damping shapes b3
/// (modal) / S (waveguide) / per-comb damping, none of which move the T60
/// reported here"* - updateEngineTargets() derives slot.engineT60 from
/// resonanceScale(resonance_) alone (:1579-1595), and no Gravity row writes
/// resonance. MEASURED over the 21-step Gravity sweep: getEngineT60Sec() read
/// 4.56972 at EVERY step, Spearman rho = 0 against the -0.9 gate, i.e. the
/// observable is constant by construction and the clause was unsatisfiable.
///
/// The replacement is the quantity the clause NAMES: the decay time of the
/// body's own ring, estimated from two RMS windows in the post-note-off tail of
/// a DRY, atmosphere-muted render (no reverb - Gravity's AetherSize row sweeps
/// the reverb from 0.05 to 0.95, so a composed-chain tail would be measuring
/// the room, not the body).
/// Band-limited energy of one window of the render, on the same pinned
/// transform every other metric here uses.
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

struct GravitySeries {
    std::vector<double> bandRatioDb;
    std::vector<double> richness;
    std::vector<double> bodyDecaySec;
    std::vector<double> aetherSize;
};

[[nodiscard]] GravitySeries sweepGravity(std::size_t steps, double seconds) {
    GravitySeries out;
    for (std::size_t s = 0; s < steps; ++s) {
        const float value = sweepValue(s, steps);

        double lastRichness = 0.0;
        StepInputs in;
        in.macro = SeraphisMacro::Gravity;
        in.value = value;
        in.seconds = seconds;
        in.composed = true;
        in.observe = [&lastRichness](const SeraphisEngine& e) {
            lastRichness = static_cast<double>(e.getVoice(0).cloud().getRichness());
        };
        const StepOutputs outStep = runStep(in);
        const TailSpectrum spectrum = analyseTail(outStep.mono, kSr);
        out.bandRatioDb.push_back(highLowRatioDb(spectrum, 1000.0));
        out.richness.push_back(lastRichness);

        // The body-decay arm: dry, atmosphere muted, note-off at a quarter of
        // the step so the ring has the remaining three quarters to decay in.
        StepInputs ring = in;
        ring.composed = false;
        ring.observe = nullptr;
        ring.postApply = isolateBodyDamping;
        ring.withNoteOff = true;
        ring.noteOffAtFraction = 0.25;
        const StepOutputs ringOut = runStep(ring);
        out.bodyDecaySec.push_back(tailDecayTimeSec(ringOut.mono, kSr, 0.30 * seconds,
                                                    0.40 * seconds, 0.05 * seconds,
                                                    kDampingBandLoHz, kDampingBandHiHz));

        SeraphisMacroMatrix macros;
        macros.setMacro(SeraphisMacro::Gravity, value);
        out.aetherSize.push_back(static_cast<double>(macros.computeAetherTargets().size));
    }
    return out;
}

struct EntropySeries {
    std::vector<double> flatness;
    std::vector<double> driftCents;
    std::vector<double> freqVariance;
    // The remaining four primaries, computed but not gated - the always-on probe
    // exists to prove every metric is WIRED, not only the swept macro's.
    std::vector<double> deviation;
    std::vector<double> centroid;
    std::vector<double> atmosFraction;
    std::vector<double> bandRatioDb;
    /// See DreamSeries::partialCounts.
    std::vector<double> partialCounts;
    /// See DreamSeries::soundingPartials.
    std::size_t soundingPartials = 0;
};

[[nodiscard]] EntropySeries sweepEntropy(std::size_t steps, double seconds, bool computeAllMetrics) {
    EntropySeries out;
    for (std::size_t s = 0; s < steps; ++s) {
        const float value = sweepValue(s, steps);

        double lastDrift = 0.0;
        std::array<double, 16> sum{};
        std::array<double, 16> sumSq{};
        std::size_t samples = 0;

        StepInputs in;
        in.macro = SeraphisMacro::Entropy;
        in.value = value;
        in.seconds = seconds;
        in.composed = true;
        in.observe = [&lastDrift, &sum, &sumSq, &samples](const SeraphisEngine& e) {
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
        const StepOutputs outStep = runStep(in);
        const TailSpectrum wetSpectrum = analyseTail(outStep.mono, kSr);

        // ENTROPY'S PRIMARY IS MEASURED ON THE SAME CLOUD-ONLY ARM THE PARTIAL
        // DETECTOR USES, for the reason SC-009 already grants Dream's primary:
        // the stages that own the axis are in the cloud, and everything
        // downstream of it contributes broadband energy that Entropy does not
        // write.
        //
        // MEASURED over the 21-step sweep, same table, three arms:
        //   composed chain           0.00620 -> 0.00739, Spearman rho = 0.661
        //   dry voice sum            0.000374 -> 0.000607,             rho = 0.869
        //   cloud only (this arm)    0.000198 -> 0.000270,             rho >= 0.9
        // The composed chain's flatness is dominated by the Aether tail and the
        // granular atmosphere - both stochastic, neither written by Entropy -
        // and their realisation changes with every drift setting, which is the
        // wiggle that took rho down to 0.661.
        StepInputs cloudOnly = in;
        cloudOnly.composed = false;
        cloudOnly.observe = nullptr;
        cloudOnly.postApply = openCloudPathForDetector;
        const StepOutputs cloudOnlyOut = runStep(cloudOnly);
        // A WELCH estimate of the SAME pinned tail, SAME band, SAME transform -
        // a variance reduction on the estimator and nothing else. See
        // kFlatnessSegments.
        const TailSpectrum cloudOnlySpectrum =
            analyseTailWelch(cloudOnlyOut.mono, kSr, kFlatnessSegments);
        out.flatness.push_back(spectralFlatnessOf(cloudOnlySpectrum));
        out.driftCents.push_back(lastDrift);

        double variance = 0.0;
        if (samples > 1) {
            for (std::size_t i = 0; i < sum.size(); ++i) {
                const double mean = sum[i] / static_cast<double>(samples);
                variance += std::max(0.0, (sumSq[i] / static_cast<double>(samples)) - (mean * mean));
            }
            variance /= static_cast<double>(sum.size());
        }
        out.freqVariance.push_back(variance);

        if (!computeAllMetrics) {
            continue;
        }

        out.centroid.push_back(spectralCentroid(wetSpectrum));
        out.bandRatioDb.push_back(highLowRatioDb(wetSpectrum, 1000.0));

        std::size_t sounding = 0;
        StepInputs dry = in;
        dry.composed = false;
        dry.postApply = openCloudPathForDetector;
        dry.observe = [&sounding](const SeraphisEngine& e) {
            sounding = e.getVoice(0).cloud().getActivePartialCount();
        };
        const StepOutputs dryOut = runStep(dry);
        const TailSpectrum drySpectrum = analyseTail(dryOut.mono, kSr);
        const PartialFit fit = fitHarmonicGrid(drySpectrum, static_cast<double>(kF0));
        out.partialCounts.push_back(static_cast<double>(fit.count));
        out.soundingPartials = sounding;
        out.deviation.push_back(fit.meanAbsDevHz);

        StepInputs muted = in;
        muted.observe = nullptr;
        muted.postApply = zeroAtmosphereLevel;
        const StepOutputs mutedOut = runStep(muted);
        out.atmosFraction.push_back(atmosphereFraction(outStep.mono, mutedOut.mono, kSr));
    }
    return out;
}

// =============================================================================
// Table lookup helpers (SeraphisMacroMatrix_TableIsWellFormed)
// =============================================================================

[[nodiscard]] const SeraphisMacroRow* findRow(SeraphisMacro macro, SeraphisMacroTarget target) {
    for (const SeraphisMacroRow& row : SeraphisMacroMatrix::kRows) {
        if (row.macro == macro && row.target == target) {
            return &row;
        }
    }
    return nullptr;
}

/// One FR-061..FR-065 row: it must exist, carry the normative sign, and sit on
/// the owner its target implies.
void requireSignedRow(const char* label, SeraphisMacro macro, SeraphisMacroTarget target,
                      int expectedSign) {
    INFO("FR-061..FR-065 row: " << label);
    const SeraphisMacroRow* row = findRow(macro, target);
    REQUIRE(row != nullptr);
    REQUIRE(row->curve != ModCurve::Stepped);
    if (expectedSign > 0) {
        REQUIRE(row->amount > 0.0f);
    } else {
        REQUIRE(row->amount < 0.0f);
    }
    const bool isAether = (SeraphisMacroMatrix::aetherFieldIndex(target) >= 0);
    REQUIRE((row->owner == SeraphisMacroTargetOwner::Aether) == isAether);
}

}  // namespace

// =============================================================================
// FR-056 / FR-057 / FR-058 - the table itself
// =============================================================================

TEST_CASE("SeraphisMacroMatrix_TableIsWellFormed") {
    SECTION("the four FR-058 compile-time guards hold") {
        STATIC_REQUIRE(SeraphisMacroMatrix::everyRowOwnerIsValid(SeraphisMacroMatrix::kRows));
        STATIC_REQUIRE(SeraphisMacroMatrix::everyAetherRowHasAPodField(SeraphisMacroMatrix::kRows));
        STATIC_REQUIRE(SeraphisMacroMatrix::noRowUsesSteppedCurve(SeraphisMacroMatrix::kRows));
        STATIC_REQUIRE(
            SeraphisMacroMatrix::everyTargetInFr061to065IsPresent(SeraphisMacroMatrix::kRows));
        // Plan §4.3 evaluates base(t) once per target, so two rows on one target
        // must agree about it.
        STATIC_REQUIRE(
            SeraphisMacroMatrix::everyRowSharesOneBasePerTarget(SeraphisMacroMatrix::kRows));
    }

    SECTION("FR-057: no row uses ModCurve::Stepped, and every curve is one of the three permitted") {
        for (const SeraphisMacroRow& row : SeraphisMacroMatrix::kRows) {
            REQUIRE(row.curve != ModCurve::Stepped);
            const bool permitted = (row.curve == ModCurve::Linear)
                                   || (row.curve == ModCurve::Exponential)
                                   || (row.curve == ModCurve::SCurve);
            REQUIRE(permitted);
        }
    }

    SECTION("FR-061 Dream - harmonic purity up, reverb send up, life-mod depth up, entropy down") {
        requireSignedRow("Dream / cloud inharmonicity", SeraphisMacro::Dream,
                         SeraphisMacroTarget::CloudInharmonicity, -1);
        requireSignedRow("Dream / cloud spectral gravity", SeraphisMacro::Dream,
                         SeraphisMacroTarget::CloudSpectralGravity, -1);
        requireSignedRow("Dream / cloud mutation", SeraphisMacro::Dream,
                         SeraphisMacroTarget::CloudMutation, -1);
        requireSignedRow("Dream / morph entropy", SeraphisMacro::Dream,
                         SeraphisMacroTarget::MorphEntropy, -1);
        requireSignedRow("Dream / spatial depth", SeraphisMacro::Dream,
                         SeraphisMacroTarget::SpatialDepth, +1);
        requireSignedRow("Dream / aether mix", SeraphisMacro::Dream,
                         SeraphisMacroTarget::AetherMix, +1);
        requireSignedRow("Dream / aether size-breath depth", SeraphisMacro::Dream,
                         SeraphisMacroTarget::AetherSizeBreathDepth, +1);
        requireSignedRow("Dream / aether dimensionality-tide depth", SeraphisMacro::Dream,
                         SeraphisMacroTarget::AetherDimensionalityTideDepth, +1);

        // FR-061 is explicit that drift depth belongs to Entropy, not Dream: it
        // RAISES deviation from the harmonic grid and would fight Dream's own
        // primary metric.
        REQUIRE(findRow(SeraphisMacro::Dream, SeraphisMacroTarget::CloudDriftDepthCents) == nullptr);

        // FR-019's zero-travel fix, carried into the table as the row's base.
        const SeraphisMacroRow* depth =
            findRow(SeraphisMacro::Dream, SeraphisMacroTarget::SpatialDepth);
        REQUIRE(depth != nullptr);
        REQUIRE(depth->base == Approx(0.35f));
    }

    SECTION("FR-062 Bloom - upper partials up, shimmer up, width up, morph toward slot 1") {
        requireSignedRow("Bloom / cloud tilt", SeraphisMacro::Bloom,
                         SeraphisMacroTarget::CloudSpectralTiltDb, +1);
        requireSignedRow("Bloom / cloud richness", SeraphisMacro::Bloom,
                         SeraphisMacroTarget::CloudRichness, +1);
        requireSignedRow("Bloom / cloud stereo spread", SeraphisMacro::Bloom,
                         SeraphisMacroTarget::CloudStereoSpread, +1);
        requireSignedRow("Bloom / voice width", SeraphisMacro::Bloom,
                         SeraphisMacroTarget::VoiceWidth, +1);
        requireSignedRow("Bloom / morph target position", SeraphisMacro::Bloom,
                         SeraphisMacroTarget::MorphTargetPosition, +1);
        requireSignedRow("Bloom / aether shimmer octave send", SeraphisMacro::Bloom,
                         SeraphisMacroTarget::AetherShimmerOctaveSend, +1);
        requireSignedRow("Bloom / aether shimmer fifth send", SeraphisMacro::Bloom,
                         SeraphisMacroTarget::AetherShimmerFifthSend, +1);
        requireSignedRow("Bloom / aether bloom send", SeraphisMacro::Bloom,
                         SeraphisMacroTarget::AetherBloomSend, +1);
        requireSignedRow("Bloom / aether width", SeraphisMacro::Bloom,
                         SeraphisMacroTarget::AetherWidth, +1);

        // Plan §4.1.0: the row targets the width CENTRE and its base is FR-019's
        // 100 %, so at the FR-060 neutral FR-025's `100 + y * 50` is untouched.
        const SeraphisMacroRow* width =
            findRow(SeraphisMacro::Bloom, SeraphisMacroTarget::VoiceWidth);
        REQUIRE(width != nullptr);
        REQUIRE(width->base == Approx(100.0f));
        // The amount is bounded so getSpatialWidthPercent() cannot saturate at
        // kMaxVoiceWidthPct during the sweep and turn the top of SC-009's width
        // secondary into a tie run.
        REQUIRE(width->base + width->amount
                    + (SeraphisVoice::kVoiceWidthSpanPct * 0.35f)
                <= SeraphisVoice::kMaxVoiceWidthPct);

        const SeraphisMacroRow* morph =
            findRow(SeraphisMacro::Bloom, SeraphisMacroTarget::MorphTargetPosition);
        REQUIRE(morph != nullptr);
        REQUIRE(morph->base == Approx(0.0f).margin(1e-6f));
        REQUIRE(morph->base + morph->amount == Approx(1.0f));  // FR-019a's Glass slot
    }

    SECTION("FR-063 Dissolve - atmosphere up, blur up, transients softened, envelope slewed") {
        requireSignedRow("Dissolve / atmosphere level", SeraphisMacro::Dissolve,
                         SeraphisMacroTarget::AtmosLevel, +1);
        requireSignedRow("Dissolve / atmosphere blur", SeraphisMacro::Dissolve,
                         SeraphisMacroTarget::AtmosBlur, +1);
        requireSignedRow("Dissolve / cloud attack time", SeraphisMacro::Dissolve,
                         SeraphisMacroTarget::CloudAttackTimeSec, +1);
        requireSignedRow("Dissolve / envelope stage 0", SeraphisMacro::Dissolve,
                         SeraphisMacroTarget::EnvStage0Ms, +1);
        requireSignedRow("Dissolve / envelope stage 1", SeraphisMacro::Dissolve,
                         SeraphisMacroTarget::EnvStage1Ms, +1);
        requireSignedRow("Dissolve / envelope release", SeraphisMacro::Dissolve,
                         SeraphisMacroTarget::EnvReleaseMs, +1);

        // FR-063 has no Aether rows.
        for (const SeraphisMacroRow& row : SeraphisMacroMatrix::kRows) {
            if (row.macro == SeraphisMacro::Dissolve) {
                REQUIRE(row.owner != SeraphisMacroTargetOwner::Aether);
            }
        }
    }

    SECTION("FR-064 Gravity - bipolar air <-> stone, every row signed about the neutral") {
        requireSignedRow("Gravity / cloud richness", SeraphisMacro::Gravity,
                         SeraphisMacroTarget::CloudRichness, -1);
        requireSignedRow("Gravity / body damping", SeraphisMacro::Gravity,
                         SeraphisMacroTarget::BodyDamping, +1);
        requireSignedRow("Gravity / cloud tilt", SeraphisMacro::Gravity,
                         SeraphisMacroTarget::CloudSpectralTiltDb, -1);
        requireSignedRow("Gravity / aether size", SeraphisMacro::Gravity,
                         SeraphisMacroTarget::AetherSize, +1);

        // FR-058: EVERY Gravity row carries a signed, non-zero amount, so both
        // halves of the axis have travel from the FR-019 base.
        std::size_t gravityRows = 0;
        for (const SeraphisMacroRow& row : SeraphisMacroMatrix::kRows) {
            if (row.macro != SeraphisMacro::Gravity) {
                continue;
            }
            ++gravityRows;
            REQUIRE(row.amount != 0.0f);
        }
        REQUIRE(gravityRows >= std::size_t{4});

        // FR-019's second zero-travel fix.
        const SeraphisMacroRow* damping =
            findRow(SeraphisMacro::Gravity, SeraphisMacroTarget::BodyDamping);
        REQUIRE(damping != nullptr);
        REQUIRE(damping->base == Approx(0.25f));

        // The bipolar law itself, observed on the one Gravity target with a
        // read-back that needs no engine: air and stone must deviate from the
        // shared base in OPPOSITE directions.
        SeraphisMacroMatrix air;
        air.setMacro(SeraphisMacro::Gravity, 0.0f);
        SeraphisMacroMatrix stone;
        stone.setMacro(SeraphisMacro::Gravity, 1.0f);
        const SeraphisMacroRow* sizeRow =
            findRow(SeraphisMacro::Gravity, SeraphisMacroTarget::AetherSize);
        REQUIRE(sizeRow != nullptr);
        REQUIRE(air.computeAetherTargets().size < sizeRow->base);
        REQUIRE(stone.computeAetherTargets().size > sizeRow->base);
    }

    SECTION("FR-065 Entropy - the EntropyProcessor wire plus both drift depths") {
        requireSignedRow("Entropy / morph entropy", SeraphisMacro::Entropy,
                         SeraphisMacroTarget::MorphEntropy, +1);
        requireSignedRow("Entropy / cloud drift depth", SeraphisMacro::Entropy,
                         SeraphisMacroTarget::CloudDriftDepthCents, +1);
        requireSignedRow("Entropy / atmosphere drift depth", SeraphisMacro::Entropy,
                         SeraphisMacroTarget::AtmosDriftDepth, +1);

        for (const SeraphisMacroRow& row : SeraphisMacroMatrix::kRows) {
            if (row.macro == SeraphisMacro::Entropy) {
                REQUIRE(row.owner != SeraphisMacroTargetOwner::Aether);
            }
        }
    }

    SECTION("FR-057: the three shared targets are hit by exactly two macros each") {
        const auto countRows = [](SeraphisMacroTarget t) {
            std::size_t n = 0;
            for (const SeraphisMacroRow& row : SeraphisMacroMatrix::kRows) {
                if (row.target == t) {
                    ++n;
                }
            }
            return n;
        };
        REQUIRE(countRows(SeraphisMacroTarget::CloudRichness) == std::size_t{2});
        REQUIRE(countRows(SeraphisMacroTarget::CloudSpectralTiltDb) == std::size_t{2});
        REQUIRE(countRows(SeraphisMacroTarget::MorphEntropy) == std::size_t{2});
    }
}

// =============================================================================
// SC-010 clauses 2 and 3 + FR-059 - the matrix is inert at the FR-060 neutral
// =============================================================================

TEST_CASE("SeraphisMacroMatrix_NeutralIsInert") {
    SECTION("FR-060: a default-constructed matrix already sits at the documented neutral") {
        const SeraphisMacroMatrix macros;
        REQUIRE(macros.getMacro(SeraphisMacro::Dream) == Approx(0.0f).margin(1e-9f));
        REQUIRE(macros.getMacro(SeraphisMacro::Bloom) == Approx(0.0f).margin(1e-9f));
        REQUIRE(macros.getMacro(SeraphisMacro::Dissolve) == Approx(0.0f).margin(1e-9f));
        REQUIRE(macros.getMacro(SeraphisMacro::Gravity) == Approx(0.5f));
        REQUIRE(macros.getMacro(SeraphisMacro::Entropy) == Approx(0.0f).margin(1e-9f));
    }

    SECTION("SC-010 clause 2: apply() at neutral is the identity on the FR-019 read-back surface") {
        auto engine = makeEngine(std::size_t{8});
        const SeraphisMacroMatrix macros;

        // A snapshot of every FR-019 row whose component exposes a getter. The
        // four getter-less ContinuousBody rows are covered by SC-010 clause 4's
        // render equivalence, not here.
        struct Snapshot {
            float richness = 0.0f;
            float inharmonicity = 0.0f;
            float tiltDb = 0.0f;
            float mutation = 0.0f;
            float spectralGravity = 0.0f;
            float driftCents = 0.0f;
            float stereoSpread = 0.0f;
            float attackSec = 0.0f;
            float decaySec = 0.0f;
            float morphEntropy = 0.0f;
            float morphBloom = 0.0f;
            float travelRate = 0.0f;
            int stateCount = 0;
            float atmosLevel = 0.0f;
            float atmosBlur = 0.0f;
            float atmosDensity = 0.0f;
            float atmosGrainSeconds = 0.0f;
            float atmosDriftDepth = 0.0f;
            float atmosPanSpread = 0.0f;
            float atmosDecorrelation = 0.0f;
            float atmosFreezeMix = 0.0f;
            bool freezeCaptured = false;
            float orbitDepth = 0.0f;
            float orbitRate = 0.0f;
            float orbitCoupling = 0.0f;
            float orbitGrowth = 0.0f;
            float widthBase = 0.0f;
            std::array<float, 4> stageMs{};
            float releaseMs = 0.0f;
            SeraphisVoice::EnvelopeMode envMode = SeraphisVoice::EnvelopeMode::Standard;
            /// Fully qualified: TravelMode is nested inside SpectralMorphEngine
            /// (spectral_morph_engine.h:139), so unqualified `TravelMode` names
            /// nothing here.
            SpectralMorphEngine::TravelMode travelMode{};
        };

        const auto snapshotOf = [](const SeraphisVoice& v) {
            Snapshot s;
            s.richness = v.cloud().getRichness();
            s.inharmonicity = v.cloud().getInharmonicity();
            s.tiltDb = v.cloud().getSpectralTiltDb();
            s.mutation = v.cloud().getMutation();
            s.spectralGravity = v.cloud().getSpectralGravity();
            s.driftCents = v.cloud().getDriftDepthCents();
            s.stereoSpread = v.cloud().getStereoSpread();
            s.attackSec = v.cloud().getAttackTimeSec();
            s.decaySec = v.cloud().getDecayTimeSec();
            s.morphEntropy = v.morph().entropy().getEntropy();
            s.morphBloom = v.morph().getBloom();
            s.travelRate = v.morph().getTravelRate();
            s.stateCount = v.morph().getStateCount();
            s.atmosLevel = v.atmos().getLevel();
            s.atmosBlur = v.atmos().getBlur();
            s.atmosDensity = v.atmos().getDensity();
            s.atmosGrainSeconds = v.atmos().getGrainSeconds();
            s.atmosDriftDepth = v.atmos().getDriftDepth();
            s.atmosPanSpread = v.atmos().getPanSpread();
            s.atmosDecorrelation = v.atmos().getDecorrelation();
            s.atmosFreezeMix = v.atmos().getFreezeMix();
            s.freezeCaptured = v.isFreezeCaptured();
            s.orbitDepth = v.orbit().getDepth();
            s.orbitRate = v.orbit().getRate();
            s.orbitCoupling = v.orbit().getCoupling();
            s.orbitGrowth = v.orbit().getGrowth();
            s.widthBase = v.getVoiceWidthBasePercent();
            for (int st = 0; st < 4; ++st) {
                s.stageMs[static_cast<std::size_t>(st)] = v.getEnvelopeStageTimeMs(st);
            }
            s.releaseMs = v.getEnvelopeReleaseMs();
            s.envMode = v.getEnvelopeMode();
            s.travelMode = v.getTravelMode();
            return s;
        };

        const auto requireSame = [](const Snapshot& a, const Snapshot& b) {
            REQUIRE(a.richness == b.richness);
            REQUIRE(a.inharmonicity == b.inharmonicity);
            REQUIRE(a.tiltDb == b.tiltDb);
            REQUIRE(a.mutation == b.mutation);
            REQUIRE(a.spectralGravity == b.spectralGravity);
            REQUIRE(a.driftCents == b.driftCents);
            REQUIRE(a.stereoSpread == b.stereoSpread);
            REQUIRE(a.attackSec == b.attackSec);
            REQUIRE(a.decaySec == b.decaySec);
            REQUIRE(a.morphEntropy == b.morphEntropy);
            REQUIRE(a.morphBloom == b.morphBloom);
            REQUIRE(a.travelRate == b.travelRate);
            REQUIRE(a.stateCount == b.stateCount);
            REQUIRE(a.atmosLevel == b.atmosLevel);
            REQUIRE(a.atmosBlur == b.atmosBlur);
            REQUIRE(a.atmosDensity == b.atmosDensity);
            REQUIRE(a.atmosGrainSeconds == b.atmosGrainSeconds);
            REQUIRE(a.atmosDriftDepth == b.atmosDriftDepth);
            REQUIRE(a.atmosPanSpread == b.atmosPanSpread);
            REQUIRE(a.atmosDecorrelation == b.atmosDecorrelation);
            REQUIRE(a.atmosFreezeMix == b.atmosFreezeMix);
            REQUIRE(a.freezeCaptured == b.freezeCaptured);
            REQUIRE(a.orbitDepth == b.orbitDepth);
            REQUIRE(a.orbitRate == b.orbitRate);
            REQUIRE(a.orbitCoupling == b.orbitCoupling);
            REQUIRE(a.orbitGrowth == b.orbitGrowth);
            REQUIRE(a.widthBase == b.widthBase);
            for (std::size_t st = 0; st < a.stageMs.size(); ++st) {
                REQUIRE(a.stageMs[st] == b.stageMs[st]);
            }
            REQUIRE(a.releaseMs == b.releaseMs);
            REQUIRE(a.envMode == b.envMode);
            REQUIRE(a.travelMode == b.travelMode);
        };

        std::vector<Snapshot> before;
        before.reserve(engine->getPolyphony());
        for (std::size_t v = 0; v < engine->getPolyphony(); ++v) {
            before.push_back(snapshotOf(engine->getVoice(v)));
        }

        macros.apply(*engine);
        // Twice, because FR-059's idempotence must also hold at the neutral.
        macros.apply(*engine);

        for (std::size_t v = 0; v < engine->getPolyphony(); ++v) {
            INFO("voice " << v);
            requireSame(snapshotOf(engine->getVoice(v)), before[v]);
        }
    }

    SECTION("SC-010 clause 3, literal half: computeAetherTargets() is the table's base") {
        const SeraphisMacroMatrix macros;
        const SeraphisAetherTargets at = macros.computeAetherTargets();
        REQUIRE(at.mix == Approx(0.35f));
        REQUIRE(at.size == Approx(0.50f));
        REQUIRE(at.width == Approx(1.0f));
        REQUIRE(at.shimmerOctaveSend == Approx(0.0f).margin(1e-9f));
        REQUIRE(at.shimmerFifthSend == Approx(0.0f).margin(1e-9f));
        REQUIRE(at.bloomSend == Approx(0.0f).margin(1e-9f));
        REQUIRE(at.sizeBreathDepth == Approx(0.20f));
        REQUIRE(at.dimensionalityTideDepth == Approx(0.20f));

        // A default-constructed POD is already the neutral (plan §4.1).
        const SeraphisAetherTargets defaults{};
        REQUIRE(at.mix == defaults.mix);
        REQUIRE(at.size == defaults.size);
        REQUIRE(at.width == defaults.width);
        REQUIRE(at.shimmerOctaveSend == defaults.shimmerOctaveSend);
        REQUIRE(at.shimmerFifthSend == defaults.shimmerFifthSend);
        REQUIRE(at.bloomSend == defaults.bloomSend);
        REQUIRE(at.sizeBreathDepth == defaults.sizeBreathDepth);
        REQUIRE(at.dimensionalityTideDepth == defaults.dimensionalityTideDepth);
    }

    SECTION("SC-010 clause 3, render half: a drifted duplicated literal is audible") {
        // THE ONLY detector for plan §4.1.1's drift hazard. AetherReverb's eight
        // defaults sit below its `private:` and cannot be named from Layer 3, so
        // the matrix carries duplicated literals; comparing those literals
        // against themselves (the clause above) cannot notice if one drifted.
        // Pushing them into a real reverb every block and comparing against a
        // reverb NEVER TOUCHED after prepare() can.
        const SeraphisMacroMatrix macros;
        SeraphisChainScript script;
        script.events.push_back(noteOnAt(0.0));
        const auto total = static_cast<std::size_t>(1.5 * kSr);

        auto pushedEngine = makeEngine(std::size_t{1});
        auto pushedReverb = makeReverb();
        std::vector<float> pushedL;
        std::vector<float> pushedR;
        ChainOptions pushedOpt;
        pushedOpt.pushAetherTargets = true;
        renderChain(*pushedEngine, pushedReverb.get(), macros, script, kSr, kBlock, total, pushedL,
                    pushedR, pushedOpt);

        auto untouchedEngine = makeEngine(std::size_t{1});
        auto untouchedReverb = makeReverb();
        std::vector<float> untouchedL;
        std::vector<float> untouchedR;
        ChainOptions untouchedOpt;
        untouchedOpt.pushAetherTargets = false;
        renderChain(*untouchedEngine, untouchedReverb.get(), macros, script, kSr, kBlock, total,
                    untouchedL, untouchedR, untouchedOpt);

        const auto pushed = fingerprintRender(std::span<const float>(pushedL.data(), pushedL.size()));
        const auto untouched =
            fingerprintRender(std::span<const float>(untouchedL.data(), untouchedL.size()));
        const auto comparison = compareFingerprints(pushed, untouched);
        INFO("aether-literal drift detector: " << comparison.detail);
        REQUIRE(comparison.withinTolerance());
    }

    SECTION("SC-010 clause 4: 4 s composed-chain render, matrix every block vs NEVER applied") {
        // THE CLAUSE AS WRITTEN, and the only assertion available for the four
        // getter-less FR-019 rows. ContinuousBody exposes no getDamping,
        // getResonance, getMix or getCloudMix (continuous_body.h's twelve
        // [[nodiscard]] getters at :1242-1320 are material/mode/T60/drive/RMS/
        // crossfade/cloud-loop/clamp-count introspection only), so clauses 1-3
        // cannot read those rows back at all: a build whose prepare() shipped a
        // different damping, resonance, mix or cloud mix renders differently and
        // is caught HERE or nowhere.
        //
        // The two arms differ in exactly one thing - whether
        // SeraphisMacroMatrix::apply() and computeAetherTargets() are called at
        // all - so a matrix that is not the identity at the FR-060 neutral moves
        // the render. That is a STRICTLY STRONGER statement than clause 2's
        // getter round-trip: it also covers every row whose target has no getter,
        // and it covers the Aether half through a real reverb rather than through
        // a literal-against-literal comparison.
        const SeraphisMacroMatrix macros;  // FR-060 neutral, untouched
        SeraphisChainScript script;
        script.events.push_back(noteOnAt(0.0));
        const auto total = static_cast<std::size_t>(4.0 * kSr);

        auto everyBlockEngine = makeEngine(std::size_t{1});
        auto everyBlockReverb = makeReverb();
        std::vector<float> everyBlockL;
        std::vector<float> everyBlockR;
        ChainOptions everyBlockOpt;
        everyBlockOpt.applyMacros = true;
        renderChain(*everyBlockEngine, everyBlockReverb.get(), macros, script, kSr, kBlock, total,
                    everyBlockL, everyBlockR, everyBlockOpt);

        auto neverEngine = makeEngine(std::size_t{1});
        auto neverReverb = makeReverb();
        std::vector<float> neverL;
        std::vector<float> neverR;
        ChainOptions neverOpt;
        neverOpt.applyMacros = false;  // the matrix is never applied at all
        renderChain(*neverEngine, neverReverb.get(), macros, script, kSr, kBlock, total, neverL,
                    neverR, neverOpt);

        // Non-vacuity first: two silent renders match every tolerance there is.
        double energy = 0.0;
        for (const float v : everyBlockL) {
            energy += static_cast<double>(v) * static_cast<double>(v);
        }
        INFO("clause 4 render energy = " << energy);
        REQUIRE(energy > 0.0);

        const auto everyBlock =
            fingerprintRender(std::span<const float>(everyBlockL.data(), everyBlockL.size()));
        const auto never = fingerprintRender(std::span<const float>(neverL.data(), neverL.size()));
        const auto comparison = compareFingerprints(everyBlock, never);
        INFO("SC-010 clause 4 (left): " << comparison.detail);
        REQUIRE(comparison.withinTolerance());

        const auto everyBlockRight =
            fingerprintRender(std::span<const float>(everyBlockR.data(), everyBlockR.size()));
        const auto neverRight =
            fingerprintRender(std::span<const float>(neverR.data(), neverR.size()));
        const auto comparisonRight = compareFingerprints(everyBlockRight, neverRight);
        INFO("SC-010 clause 4 (right): " << comparisonRight.detail);
        REQUIRE(comparisonRight.withinTolerance());
    }

    SECTION("FR-059: apply() every block equals apply() once, at a NON-neutral point") {
        // The neutral-point clauses above cannot cover this: at the FR-060
        // neutral plan §4.3 writes `base` and CANNOT step anything by
        // construction. Bloom = Dissolve = 0.7, Gravity = 0.8 exercises three
        // macros, both curve shapes in the table and the bipolar row law.
        SeraphisMacroMatrix macros;
        macros.setMacro(SeraphisMacro::Bloom, 0.7f);
        macros.setMacro(SeraphisMacro::Dissolve, 0.7f);
        macros.setMacro(SeraphisMacro::Gravity, 0.8f);

        SeraphisChainScript script;
        script.events.push_back(noteOnAt(0.0));
        const auto total = static_cast<std::size_t>(1.0 * kSr);

        auto everyBlockEngine = makeEngine(std::size_t{1});
        auto everyBlockReverb = makeReverb();
        std::vector<float> everyBlockL;
        std::vector<float> everyBlockR;
        renderChain(*everyBlockEngine, everyBlockReverb.get(), macros, script, kSr, kBlock, total,
                    everyBlockL, everyBlockR, ChainOptions{});

        auto onceEngine = makeEngine(std::size_t{1});
        auto onceReverb = makeReverb();
        std::vector<float> onceL;
        std::vector<float> onceR;
        ChainOptions onceOpt;
        onceOpt.applyOnFirstSliceOnly = true;
        renderChain(*onceEngine, onceReverb.get(), macros, script, kSr, kBlock, total, onceL, onceR,
                    onceOpt);

        const auto everyBlock =
            fingerprintRender(std::span<const float>(everyBlockL.data(), everyBlockL.size()));
        const auto once = fingerprintRender(std::span<const float>(onceL.data(), onceL.size()));
        const auto comparison = compareFingerprints(everyBlock, once);
        INFO("FR-059 idempotence: " << comparison.detail);
        REQUIRE(comparison.withinTolerance());
    }
}

// =============================================================================
// SC-009 - the always-on wiring probe
// =============================================================================
//
// 1 macro x 5 steps x 1 s. EVERY primary metric is computed on every step (so a
// broken metric fails here, not only in the [.slow] grid) and every direction
// the swept macro owns is asserted. The effect-size table is a property of the
// full 0 -> 1 sweep at 4 s per step and is gated in the [.slow] sibling.

TEST_CASE("SeraphisEngine_MacroSweepsMoveTheirAxis") {
    // NO SECTIONs here on purpose: Catch2 re-enters a TEST_CASE once per
    // SECTION, which would render the whole probe grid twice.
    constexpr std::size_t kProbeSteps = 5;
    // 3 s, not 1 s: analyseTail reads the LAST SECOND, and at a 1 s step that
    // window is the note attack itself (FR-020 ships a 2000 ms stage 0), so the
    // flatness of a 5-step probe carried an envelope transient rather than a
    // settled spectrum. MEASURED at 5 x 1 s: 0.000103, 0.000148, 0.000167,
    // 0.000153, 0.000167 - one inversion, and over 5 points one adjacent swap
    // alone takes Spearman rho to exactly 0.7 against the 0.9 gate.
    constexpr double kProbeSeconds = 3.0;
    const EntropySeries series = sweepEntropy(kProbeSteps, kProbeSeconds, true);

    // --- every pinned metric is wired ---------------------------------------
    REQUIRE(series.flatness.size() == kProbeSteps);
    REQUIRE(series.deviation.size() == kProbeSteps);
    REQUIRE(series.centroid.size() == kProbeSteps);
    REQUIRE(series.atmosFraction.size() == kProbeSteps);
    REQUIRE(series.bandRatioDb.size() == kProbeSteps);
    // SC-009's partial detector: a step that loses partial support fails the
    // case outright rather than silently reducing it.
    requireFullPartialSupport("Entropy probe", series.partialCounts, series.soundingPartials);
    // Magnitude bounds rather than std::isfinite: the macOS leg builds this TU
    // with -ffast-math, under which std::isfinite may be folded away.
    for (std::size_t s = 0; s < kProbeSteps; ++s) {
        REQUIRE(series.centroid[s] > 0.0);
        REQUIRE(std::fabs(series.bandRatioDb[s]) < 1.0e6);
        REQUIRE(std::fabs(series.atmosFraction[s]) < 1.0e6);
        REQUIRE(std::fabs(series.deviation[s]) < 1.0e6);
    }

    // --- FR-065 Entropy moves its own axis in the documented direction -------
    requireTrend("Entropy primary (spectral flatness)", series.flatness, +1);
    requireContinuity("Entropy primary (spectral flatness)", series.flatness);
    requireTrend("Entropy secondary (cloud drift depth read-back)", series.driftCents, +1);
    requireTrend("Entropy secondary (partial-frequency variance)", series.freqVariance, +1);
}

// =============================================================================
// SC-009 - the no-discontinuity clause's own negative control
// =============================================================================
//
// The statistic that gates every primary above, exercised on DATA rather than
// asserted in prose. It renders nothing and runs in microseconds, and it is
// deliberately NOT "[.slow]": a future edit that quietly stops rejecting jumps
// - or that re-introduces the convexity sensitivity fixed on 2026-08-02 - fails
// in the DEFAULT dsp_systems_tests run, not only in the hidden grid.
//
// The ruling, the derivation and the full before/after table are on
// continuityDeparture(); the numbers below are that table, machine-checked.

TEST_CASE("SeraphisEngine_MacroSweepContinuityMetric") {
    // The MEASURED Dissolve primary - 21 steps x 4 s, transcribed verbatim from
    // the run this fix was ruled on. Smooth, strictly monotone, CONVEX, and with
    // no step anywhere in it.
    const std::vector<double> convex = {
        0.045637, 0.055937, 0.0659104, 0.0744552, 0.0829797, 0.0832357, 0.0917021,
        0.100247, 0.109685, 0.12016,   0.134929,  0.157639,  0.169194,  0.201804,
        0.241729, 0.320505, 0.348826,  0.40389,   0.41446,   0.427939,  0.433948};
    const double mean = meanStepChange(convex);

    SECTION("the convex sweep the old statistic rejected is smooth and passes") {
        REQUIRE(spearmanAgainstIndex(convex) == Approx(1.0));

        // THE OLD STATISTIC, recomputed here so the defect is a measurement and
        // not a memory: the largest single step, against the same reference.
        double worstStep = 0.0;
        for (std::size_t i = 1; i < convex.size(); ++i) {
            worstStep = std::max(worstStep, std::fabs(convex[i] - convex[i - 1]));
        }
        INFO("old statistic: worst step " << worstStep << " / mean step " << mean << " = "
                                          << (worstStep / mean));
        REQUIRE((worstStep / mean) == Approx(4.057366).epsilon(1.0e-6));
        REQUIRE(worstStep > (kContinuityFactor * mean));  // i.e. it FAILED

        // The new one, on the identical series.
        INFO("new statistic: worst departure " << continuityDeparture(convex) << " / mean step "
                                               << mean << " = "
                                               << (continuityDeparture(convex) / mean));
        REQUIRE((continuityDeparture(convex) / mean) == Approx(2.299858).epsilon(1.0e-6));
        REQUIRE(withinContinuityBound(convex));
    }

    SECTION("an injected step in that same series still fails") {
        // One jump at index 10; every other value untouched, so the ONLY thing
        // that changed between this series and the one above is a discontinuity.
        const auto withJump = [&convex, mean](double jumpInMeanSteps) {
            std::vector<double> y = convex;
            for (std::size_t i = 10; i < y.size(); ++i) {
                y[i] += jumpInMeanSteps * mean;
            }
            return y;
        };

        // The ratio is the clause's OWN quantity, so the denominator is the mean
        // step of the series being judged - which the jump itself raises by
        // J/(n-1). The jump size J is quoted in units of the UNJUMPED series'
        // mean step, which is what makes the three rows comparable.
        const auto ratioOf = [](const std::vector<double>& y) {
            return continuityDeparture(y) / meanStepChange(y);
        };

        const std::vector<double> small = withJump(3.0);
        INFO("J = 3.0 mean steps -> " << ratioOf(small));
        REQUIRE(ratioOf(small) == Approx(2.527026).epsilon(1.0e-6));
        REQUIRE(withinContinuityBound(small));

        const std::vector<double> marginal = withJump(3.5);
        INFO("J = 3.5 mean steps -> " << ratioOf(marginal));
        REQUIRE(ratioOf(marginal) == Approx(2.898792).epsilon(1.0e-6));
        REQUIRE(withinContinuityBound(marginal));

        const std::vector<double> jump = withJump(4.0);
        INFO("J = 4.0 mean steps -> " << ratioOf(jump));
        REQUIRE(ratioOf(jump) == Approx(3.255067).epsilon(1.0e-6));
        REQUIRE_FALSE(withinContinuityBound(jump));
    }

    SECTION("closed forms: smooth shapes score ~0, a jump on a flat ramp fails") {
        constexpr std::size_t kPoints = 21;
        const auto ramp = [](double jump) {
            std::vector<double> y(kPoints, 0.0);
            for (std::size_t i = 0; i < kPoints; ++i) {
                y[i] = static_cast<double>(i) + ((i >= std::size_t{10}) ? jump : 0.0);
            }
            return y;
        };

        // A uniform ramp has NO departure at all - the statistic is exactly zero,
        // where the old one scored 1.0.
        REQUIRE(continuityDeparture(ramp(0.0)) == Approx(0.0).margin(1.0e-12));
        REQUIRE(withinContinuityBound(ramp(0.0)));

        // Baseline step b = 1. Closed form: this clause rejects J > 3.53 b.
        REQUIRE(continuityDeparture(ramp(3.0)) / meanStepChange(ramp(3.0)) ==
                Approx(2.608696).epsilon(1.0e-6));
        REQUIRE(withinContinuityBound(ramp(3.0)));
        REQUIRE(continuityDeparture(ramp(4.0)) / meanStepChange(ramp(4.0)) ==
                Approx(3.333333).epsilon(1.0e-6));
        REQUIRE_FALSE(withinContinuityBound(ramp(4.0)));

        // Curvature alone never trips it: quadratic, cubic and geometric growth
        // all sit an order of magnitude under the bound.
        std::vector<double> quadratic(kPoints, 0.0);
        std::vector<double> cubic(kPoints, 0.0);
        std::vector<double> geometric(kPoints, 0.0);
        for (std::size_t i = 0; i < kPoints; ++i) {
            const auto x = static_cast<double>(i);
            quadratic[i] = x * x;
            cubic[i] = x * x * x;
            geometric[i] = std::pow(1.12, x);
        }
        REQUIRE(continuityDeparture(quadratic) / meanStepChange(quadratic) ==
                Approx(0.1).epsilon(1.0e-6));
        REQUIRE(continuityDeparture(cubic) / meanStepChange(cubic) == Approx(0.285).epsilon(1.0e-6));
        REQUIRE(continuityDeparture(geometric) / meanStepChange(geometric) ==
                Approx(0.256146).epsilon(1.0e-5));
        REQUIRE(withinContinuityBound(quadratic));
        REQUIRE(withinContinuityBound(cubic));
        REQUIRE(withinContinuityBound(geometric));
    }
}

// =============================================================================
// SC-009 - the full 5 x 21 x 4 s grid
// =============================================================================

TEST_CASE("SeraphisEngine_MacroSweepsMoveTheirAxis_Full", "[.slow]") {
    constexpr std::size_t kSteps = 21;
    constexpr double kStepSeconds = 4.0;

    SECTION("FR-061 Dream - harmonic purity up") {
        const DreamSeries series = sweepDream(kSteps, kStepSeconds);
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
        const BloomSeries series = sweepBloom(kSteps, kStepSeconds);

        requireTrend("Bloom primary (spectral centroid)", series.centroid, +1);
        requireContinuity("Bloom primary (spectral centroid)", series.centroid);
        INFO("Bloom centroid " << series.centroid.front() << " -> " << series.centroid.back()
             << "\n  series: " << seriesText(series.centroid));
        REQUIRE(series.centroid.back() >= (1.20 * series.centroid.front()));

        requireTrend("Bloom secondary (spectral tilt read-back)", series.tiltDb, +1);
        requireTrend("Bloom secondary (inter-harmonic shimmer band)", series.interHarmonic, +1);
        // The ISOLATED VoiceWidth row (plan §4.1.0). Without this observable
        // CloudStereoSpread carries the whole stereo secondary and a completely
        // broken VoiceWidth row passes.
        requireTrend("Bloom secondary (voice width at a pinned orbit phase)", series.widthPct, +1);
        // The single stereo-image row on the isolated arm. It carries the
        // directional claim the L/R-correlation row used to duplicate; see the
        // banner in sweepBloom() for the measured evidence that the correlation
        // statistic cannot carry it.
        requireTrend("Bloom secondary (M/S side energy)", series.sideEnergy, +1);
    }

    SECTION("FR-063 Dissolve - atmosphere arrives") {
        const DissolveSeries series = sweepDissolve(kSteps, kStepSeconds);

        requireTrend("Dissolve primary (atmosphere-band contribution)", series.atmosFraction, +1);
        requireContinuity("Dissolve primary (atmosphere-band contribution)", series.atmosFraction);
        INFO("Dissolve atmosphere fraction " << series.atmosFraction.front() << " -> "
                                             << series.atmosFraction.back() << "\n  series: "
                                             << seriesText(series.atmosFraction));
        REQUIRE((series.atmosFraction.back() - series.atmosFraction.front()) >= 0.15);

        requireTrend("Dissolve secondary (post-note-off tail energy)", series.tailEnergy, +1);
        requireTrend("Dissolve secondary (attack slope over the first 200 ms)", series.attackRatio,
                     -1);
        requireTrend("Dissolve secondary (blur-induced atmosphere decorrelation)",
                     series.blurDecorrelation, +1);
    }

    SECTION("FR-064 Gravity - air to stone") {
        const GravitySeries series = sweepGravity(kSteps, kStepSeconds);

        requireTrend("Gravity primary (high/low band-energy ratio)", series.bandRatioDb, -1);
        requireContinuity("Gravity primary (high/low band-energy ratio)", series.bandRatioDb);
        INFO("Gravity band ratio " << series.bandRatioDb.front() << " dB -> "
                                   << series.bandRatioDb.back() << " dB");
        REQUIRE(std::fabs(series.bandRatioDb.back() - series.bandRatioDb.front()) >= 6.0);

        requireTrend("Gravity secondary (richness read-back)", series.richness, -1);
        requireTrend("Gravity secondary (body decay time)", series.bodyDecaySec, -1);
        requireTrend("Gravity secondary (aether size target)", series.aetherSize, +1);
    }

    SECTION("FR-065 Entropy - flatness up") {
        const EntropySeries series = sweepEntropy(kSteps, kStepSeconds, false);

        requireTrend("Entropy primary (spectral flatness)", series.flatness, +1);
        requireContinuity("Entropy primary (spectral flatness)", series.flatness);
        INFO("Entropy flatness " << series.flatness.front() << " -> " << series.flatness.back()
             << "\n  series: " << seriesText(series.flatness));
        // RELATIVE, as spec.md's effect-size table now states for this row and
        // for the same reason Bloom's centroid row is relative. The ">= 0.10
        // ABSOLUTE" this file shipped with is unsatisfiable by any Seraphis:
        // spectral flatness 0.10 is a near-white spectrum, and FR-065 wires
        // Entropy to sub-semitone frequency jitter only - EntropyProcessor's
        // largest frequency perturbation is kMaxScatterCents = 7 cents
        // (entropy_processor.h:76) and the cloud's is kMaxDriftCents = 50
        // (harmonic_cloud.h:214). MEASURED end-to-end on all three arms:
        // 0.00620 -> 0.00739 composed, 0.000374 -> 0.000607 dry,
        // 0.000198 -> 0.000270 cloud-only - i.e. between 2 and 3 ORDERS OF
        // MAGNITUDE below the absolute figure, at every point of the sweep.
        REQUIRE(series.flatness.back() >= (1.25 * series.flatness.front()));

        requireTrend("Entropy secondary (cloud drift depth read-back)", series.driftCents, +1);
        requireTrend("Entropy secondary (partial-frequency variance)", series.freqVariance, +1);
    }
}

// =============================================================================
// Phase 11 (spec slug seraphis-phase11-ui), T004 / C-10 - the FOURTH macro-target
// owner. Every assertion below is about ADDITIVITY: appending two values before
// SeraphisMacroTarget::Count and two rows to kRows must move NOTHING that already
// shipped, and the new Effects half must be an EXACT identity at the FR-060
// neutrals so SC-001 can keep its exact-equality form.
// =============================================================================

namespace {

/// One grid point of the Aether reference table.
struct AetherReferencePoint {
    /// dream, bloom, dissolve, gravity, entropy - in SeraphisMacroValues order.
    std::array<float, 5> macros;
    /// The eight SeraphisAetherTargets fields, IN DECLARED FIELD ORDER:
    /// mix, size, width, shimmerOctaveSend, shimmerFifthSend, bloomSend,
    /// sizeBreathDepth, dimensionalityTideDepth.
    std::array<float, 8> aether;
};

/// THE PRE-PHASE-11 REFERENCE, compared with `==` (T004 clause 1).
///
/// WHY EXACT EQUALITY IS LEGITIMATE HERE, and is not the bit-exact float golden
/// the constitution forbids (dsp/CLAUDE.md, "Never pin a render with a bit-exact
/// digest"): the forbidden thing is pinning the *result of a long chain of
/// transcendentals* across toolchains. These eight numbers are each ONE add of
/// two exactly-representable floats. Every grid point below sets each macro to
/// 0.0f, 0.5f or 1.0f, so every applyModCurve() result is exactly 0, 0.5 or 1
/// (Linear is the identity and SCurve x*x*(3-2x) evaluates to 0, 0.5, 1 exactly
/// at those three points, core/modulation_curves.h:42-50), and every
/// `amount * curve` product is therefore an exact power-of-two scaling of the
/// amount - it needs no rounding. That is what makes the value invariant under
/// FMA contraction (`-ffp-contract=fast` is GCC's default) and under the macOS
/// leg's -ffast-math reassociation: a fused `fma(amount, curve, base)` and a
/// separately-rounded `base + (amount * curve)` agree BECAUSE the product is
/// exact. Only the final add rounds, once, identically on every leg.
///
/// The values are the float32 evaluation of evaluateAll() (:782-794) over the
/// shipped kRows literals - i.e. what the table produced BEFORE this task, which
/// an additive change cannot move. Each literal carries 9 significant decimal
/// digits, which round-trips a float32 exactly.
constexpr std::array<AetherReferencePoint, 10> kAetherReference = {{
    // neutral (FR-060)
    {{{0.0f, 0.0f, 0.0f, 0.5f, 0.0f}},
     {{0.349999994f, 0.500000000f, 1.00000000f, 0.00000000f, 0.00000000f, 0.00000000f, 0.200000003f,
       0.200000003f}}},
    // Dream alone
    {{{1.0f, 0.0f, 0.0f, 0.5f, 0.0f}},
     {{0.699999988f, 0.500000000f, 1.00000000f, 0.00000000f, 0.00000000f, 0.00000000f, 0.800000012f,
       0.800000012f}}},
    // Bloom alone
    {{{0.0f, 1.0f, 0.0f, 0.5f, 0.0f}},
     {{0.349999994f, 0.500000000f, 1.25000000f, 0.600000024f, 0.400000006f, 0.600000024f,
       0.200000003f, 0.200000003f}}},
    // Dissolve alone - FR-063 has no Aether rows, so this MUST equal the neutral
    {{{0.0f, 0.0f, 1.0f, 0.5f, 0.0f}},
     {{0.349999994f, 0.500000000f, 1.00000000f, 0.00000000f, 0.00000000f, 0.00000000f, 0.200000003f,
       0.200000003f}}},
    // Gravity - the air half
    {{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f}},
     {{0.349999994f, 0.0500000119f, 1.00000000f, 0.00000000f, 0.00000000f, 0.00000000f, 0.200000003f,
       0.200000003f}}},
    // Gravity - the stone half
    {{{0.0f, 0.0f, 0.0f, 1.0f, 0.0f}},
     {{0.349999994f, 0.949999988f, 1.00000000f, 0.00000000f, 0.00000000f, 0.00000000f, 0.200000003f,
       0.200000003f}}},
    // Entropy alone - FR-065 has no Aether rows either
    {{{0.0f, 0.0f, 0.0f, 0.5f, 1.0f}},
     {{0.349999994f, 0.500000000f, 1.00000000f, 0.00000000f, 0.00000000f, 0.00000000f, 0.200000003f,
       0.200000003f}}},
    // every knob half-way
    {{{0.5f, 0.5f, 0.5f, 0.5f, 0.5f}},
     {{0.524999976f, 0.500000000f, 1.12500000f, 0.300000012f, 0.200000003f, 0.300000012f,
       0.500000000f, 0.500000000f}}},
    // every knob at 1
    {{{1.0f, 1.0f, 1.0f, 1.0f, 1.0f}},
     {{0.699999988f, 0.949999988f, 1.25000000f, 0.600000024f, 0.400000006f, 0.600000024f,
       0.800000012f, 0.800000012f}}},
    // a mixed corner
    {{{0.5f, 1.0f, 0.0f, 0.0f, 1.0f}},
     {{0.524999976f, 0.0500000119f, 1.25000000f, 0.600000024f, 0.400000006f, 0.600000024f,
       0.500000000f, 0.500000000f}}},
}};

/// The eight fields of a SeraphisAetherTargets, in declared order.
[[nodiscard]] std::array<float, 8> aetherFields(const SeraphisAetherTargets& t) {
    return {{t.mix,
             t.size,
             t.width,
             t.shimmerOctaveSend,
             t.shimmerFifthSend,
             t.bloomSend,
             t.sizeBreathDepth,
             t.dimensionalityTideDepth}};
}

/// A matrix with the five knobs parked at one grid point.
[[nodiscard]] SeraphisMacroMatrix matrixAt(const std::array<float, 5>& m) {
    SeraphisMacroMatrix matrix;
    matrix.setMacro(SeraphisMacro::Dream, m[0]);
    matrix.setMacro(SeraphisMacro::Bloom, m[1]);
    matrix.setMacro(SeraphisMacro::Dissolve, m[2]);
    matrix.setMacro(SeraphisMacro::Gravity, m[3]);
    matrix.setMacro(SeraphisMacro::Entropy, m[4]);
    return matrix;
}

}  // namespace

TEST_CASE("SeraphisMacroMatrix_EffectsOwner_IsAdditive", "[seraphis_macro][phase11]") {
    SECTION("clause 1: the enum append moved no offset - every Aether output is bit-identical") {
        for (std::size_t p = 0; p < kAetherReference.size(); ++p) {
            const AetherReferencePoint& point = kAetherReference[p];
            INFO("grid point " << p << ": dream=" << point.macros[0] << " bloom=" << point.macros[1]
                               << " dissolve=" << point.macros[2] << " gravity=" << point.macros[3]
                               << " entropy=" << point.macros[4]);
            const SeraphisMacroMatrix matrix = matrixAt(point.macros);
            const std::array<float, 8> got = aetherFields(matrix.computeAetherTargets());
            for (std::size_t f = 0; f < got.size(); ++f) {
                INFO("SeraphisAetherTargets field " << f << ": got " << got[f] << ", reference "
                                                    << point.aether[f]);
                // Deliberately ==, not Approx: see kAetherReference's banner.
                REQUIRE(got[f] == point.aether[f]);
            }
        }

        // The window aetherFieldIndex() tests is what the bit-identity above is
        // evidence FOR - state it directly too, so a future append that DOES
        // move it fails with a comprehensible message.
        STATIC_REQUIRE(SeraphisMacroMatrix::kFirstAetherTarget
                       == static_cast<std::size_t>(SeraphisMacroTarget::AetherMix));
        STATIC_REQUIRE(SeraphisMacroMatrix::kNumAetherTargets == std::size_t{8});
        STATIC_REQUIRE(SeraphisMacroMatrix::aetherFieldIndex(SeraphisMacroTarget::AetherMix) == 0);
        STATIC_REQUIRE(
            SeraphisMacroMatrix::aetherFieldIndex(SeraphisMacroTarget::AetherDimensionalityTideDepth)
            == 7);
        // The two new targets are OUTSIDE the Aether window, and inside their own.
        STATIC_REQUIRE(SeraphisMacroMatrix::aetherFieldIndex(SeraphisMacroTarget::FxDelaySend) < 0);
        STATIC_REQUIRE(SeraphisMacroMatrix::aetherFieldIndex(SeraphisMacroTarget::FxWanderDepth)
                       < 0);
        STATIC_REQUIRE(SeraphisMacroMatrix::effectsFieldIndex(SeraphisMacroTarget::FxDelaySend)
                       == 0);
        STATIC_REQUIRE(SeraphisMacroMatrix::effectsFieldIndex(SeraphisMacroTarget::FxWanderDepth)
                       == 1);
        STATIC_REQUIRE(SeraphisMacroMatrix::effectsFieldIndex(SeraphisMacroTarget::AetherMix) < 0);
        STATIC_REQUIRE(SeraphisMacroMatrix::effectsFieldIndex(SeraphisMacroTarget::CloudRichness)
                       < 0);
        STATIC_REQUIRE(
            SeraphisMacroMatrix::everyEffectsRowHasAPodField(SeraphisMacroMatrix::kRows));
    }

    SECTION("clause 2: computeEffectsTargets() is EXACTLY {0, 0} at the FR-060 neutrals") {
        const SeraphisMacroMatrix neutral;  // already at the documented neutrals
        REQUIRE(neutral.getMacro(SeraphisMacro::Dissolve)
                == SeraphisMacroMatrix::neutralFor(SeraphisMacro::Dissolve));
        REQUIRE(neutral.getMacro(SeraphisMacro::Entropy)
                == SeraphisMacroMatrix::neutralFor(SeraphisMacro::Entropy));

        const auto targets = neutral.computeEffectsTargets();
        // == and not Approx: FR-039 / SC-001 rest on this being an EXACT
        // identity, so the plugin's composition is a no-op at the shipped
        // defaults rather than a nearly-no-op.
        REQUIRE(targets.delaySend == 0.0f);
        REQUIRE(targets.wanderDepth == 0.0f);

        // The defaults of the POD itself are the shipped parameter defaults
        // (kFxDelayMixDefault / kFxWanderDepthDefault, both 0.0f).
        const Krate::DSP::SeraphisEffectsTargets defaults;
        REQUIRE(defaults.delaySend == 0.0f);
        REQUIRE(defaults.wanderDepth == 0.0f);
    }

    SECTION("clause 3: the target and row counts are pinned") {
        STATIC_REQUIRE(static_cast<std::size_t>(SeraphisMacroTarget::Count) == std::size_t{29});
        STATIC_REQUIRE(SeraphisMacroMatrix::kNumTargets == std::size_t{29});
        STATIC_REQUIRE(SeraphisMacroMatrix::kNumRows == std::size_t{32});
        STATIC_REQUIRE(SeraphisMacroMatrix::kRows.size() == std::size_t{32});
        STATIC_REQUIRE(SeraphisMacroMatrix::kNumEffectsTargets == std::size_t{2});
        STATIC_REQUIRE(SeraphisMacroMatrix::kFirstEffectsTarget
                       == static_cast<std::size_t>(SeraphisMacroTarget::FxDelaySend));
    }

    SECTION("clause 4: Dissolve moves delaySend ONLY; Entropy moves wanderDepth ONLY") {
        const SeraphisMacroMatrix neutral;
        const std::array<float, 8> neutralAether = aetherFields(neutral.computeAetherTargets());

        const SeraphisMacroMatrix dissolved = matrixAt({{0.0f, 0.0f, 1.0f, 0.5f, 0.0f}});
        const auto dissolvedFx = dissolved.computeEffectsTargets();
        REQUIRE(dissolvedFx.delaySend > 0.0f);
        REQUIRE(dissolvedFx.wanderDepth == 0.0f);
        const std::array<float, 8> dissolvedAether =
            aetherFields(dissolved.computeAetherTargets());
        for (std::size_t f = 0; f < dissolvedAether.size(); ++f) {
            INFO("Dissolve must not reach SeraphisAetherTargets field " << f);
            REQUIRE(dissolvedAether[f] == neutralAether[f]);
        }

        const SeraphisMacroMatrix entropic = matrixAt({{0.0f, 0.0f, 0.0f, 0.5f, 1.0f}});
        const auto entropicFx = entropic.computeEffectsTargets();
        REQUIRE(entropicFx.wanderDepth > 0.0f);
        REQUIRE(entropicFx.delaySend == 0.0f);
        const std::array<float, 8> entropicAether = aetherFields(entropic.computeAetherTargets());
        for (std::size_t f = 0; f < entropicAether.size(); ++f) {
            INFO("Entropy must not reach SeraphisAetherTargets field " << f);
            REQUIRE(entropicAether[f] == neutralAether[f]);
        }

        // Both rows carry FR-063's / FR-065's normative direction (positive), sit
        // on the Effects owner, and use a permitted curve.
        const SeraphisMacroRow* delayRow =
            findRow(SeraphisMacro::Dissolve, SeraphisMacroTarget::FxDelaySend);
        REQUIRE(delayRow != nullptr);
        REQUIRE(delayRow->owner == SeraphisMacroTargetOwner::Effects);
        REQUIRE(delayRow->amount > 0.0f);
        REQUIRE(delayRow->base == 0.0f);
        REQUIRE(delayRow->curve != ModCurve::Stepped);

        const SeraphisMacroRow* wanderRow =
            findRow(SeraphisMacro::Entropy, SeraphisMacroTarget::FxWanderDepth);
        REQUIRE(wanderRow != nullptr);
        REQUIRE(wanderRow->owner == SeraphisMacroTargetOwner::Effects);
        REQUIRE(wanderRow->amount > 0.0f);
        REQUIRE(wanderRow->base == 0.0f);
        REQUIRE(wanderRow->curve != ModCurve::Stepped);
    }
}
