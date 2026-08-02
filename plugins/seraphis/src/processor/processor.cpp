// ==============================================================================
// Seraphis - Audio Processor implementation
// ==============================================================================
// Phase 8 scaffold. initialize()/terminate() own the two heap-allocated DSP
// components; every other override is filled in by a later, test-first task
// (each is marked with the task that owns it).
// ==============================================================================

#include "processor/processor.h"

#include "engine/seraphis_engine_config.h"
#include "parameters/dropdown_mappings.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"  // IBStreamer (getState/setState)

#include "pluginterfaces/vst/ivstevents.h"            // IEventList / Vst::Event (FR-025, FR-031)
#include "pluginterfaces/vst/ivstparameterchanges.h"  // IParameterChanges/IParamValueQueue

#include <krate/dsp/core/scoped_denormal_mode.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Seraphis {

using namespace Steinberg;

// ==============================================================================
// FR-031 velocity mapping (plan 1.3 C-3, 3.2)
// ==============================================================================
// Declared and defined at NAMESPACE scope, deliberately NOT in an anonymous
// namespace: SC-022 clause 6 asserts the upper clamp ("127, not 128") and that
// has no behavioural proxy anywhere in the chain - SeraphisEngine::dispatch
// computes `static_cast<float>(e.velocity) / 127.0f` (seraphis_engine.h:1137)
// and SeraphisVoice::noteOn clamps that to [0, 1] (seraphis_voice.h:527), so a
// mapped 128 and a mapped 127 render bit-identical audio. The test TU declares
// this prototype itself and calls it directly.
//
// The `+ 0.5f` rounding and the FLOOR OF 1 are load-bearing, not stylistic: a
// truncating uint8(velocity * 127.0f) turns a legal velocity of 0.003 into 0,
// which SeraphisEngine::noteOn maps to noteOff (seraphis_engine.h:374-377) - a
// note-on that releases.
//
// Only ever reached with `velocity > 0.0f`, so a NaN velocity (for which that
// test is false) never gets here; +inf clamps to 127.
//
// NOLINTBEGIN(misc-use-internal-linkage) -- external linkage is REQUIRED, not an
// oversight: tests/unit/midi_event_test.cpp:46 re-declares this prototype and
// calls Seraphis::mapNoteOnVelocity directly (:361-368) to assert the clamp.
// Internal linkage (static / anonymous namespace) would make that an unresolved
// external at link time.
[[nodiscard]] std::uint8_t mapNoteOnVelocity(float velocity) noexcept;

[[nodiscard]] std::uint8_t mapNoteOnVelocity(float velocity) noexcept {
    return static_cast<std::uint8_t>(std::clamp(velocity * 127.0f + 0.5f, 1.0f, 127.0f));
}
// NOLINTEND(misc-use-internal-linkage)

namespace {

/// Plan 3.2. Clamp a host event offset into [0, total].
///
/// Negative and past-the-end offsets are both legal inputs from a malformed
/// host, and neither may ever produce a negative slice length - which, in
/// std::size_t arithmetic, would wrap and hand processStereoBlock a length of
/// several exabytes.
[[nodiscard]] std::size_t clampOffset(Steinberg::int32 offset, std::size_t total) noexcept {
    if (offset <= 0) {
        return 0u;
    }
    const auto o = static_cast<std::size_t>(offset);
    return (o > total) ? total : o;
}

/// FR-031's translation table, in whole:
///
///   kNoteOnEvent  + velocity >  0  -> noteOn(pitch, mapNoteOnVelocity(v))
///   kNoteOnEvent  + velocity <= 0  -> noteOff(pitch)
///   kNoteOffEvent                  -> noteOff(pitch)
///   anything else                  -> ignored
///
/// The velocity-0 branch is redundant with the engine's own guard
/// (seraphis_engine.h:374-377) and is written explicitly anyway, so SC-022
/// clause 2 tests THIS WRAPPER's behaviour rather than the engine's.
///
/// Pitch is range-guarded before the uint8_t cast: Event::noteOn.pitch is an
/// int16 and a host is free to put anything in it.
void dispatchEvent(Krate::DSP::SeraphisEngine& engine, const Vst::Event& event) noexcept {
    if (event.type == Vst::Event::kNoteOnEvent) {
        const int16 pitch = event.noteOn.pitch;
        if (pitch < 0 || pitch > 127) {
            return;
        }
        const auto note = static_cast<std::uint8_t>(pitch);
        if (event.noteOn.velocity > 0.0f) {
            engine.noteOn(note, mapNoteOnVelocity(event.noteOn.velocity));  // :370
        } else {
            engine.noteOff(note);  // :415
        }
        return;
    }
    if (event.type == Vst::Event::kNoteOffEvent) {
        const int16 pitch = event.noteOff.pitch;
        if (pitch < 0 || pitch > 127) {
            return;
        }
        engine.noteOff(static_cast<std::uint8_t>(pitch));
    }
    // Every other event type (controller, poly-pressure, data, ...) is ignored:
    // Phase 8's whole note surface is the event-input bus (FR-019).
}

// ==============================================================================
// FR-042 / spec C-6: the route table (plan 3.2)
// ==============================================================================
// This switch is the ONE transcription of C-6's *Route* column. markDirty() is
// its only consumer, so the classification cannot be restated - and
// desynchronised - at three call sites.
//
//   VP    -> SeraphisVoiceParams broadcast          (37 IDs)
//   MB    -> SeraphisMacroMatrix::setTargetBase     (27 IDs)
//   AE    -> applyAetherParams                      (10 IDs)
//   CFG   -> configure-time spectral slots          ( 5 IDs)
//   ENG   -> a direct SeraphisEngine setter         ( 4 IDs, own trackers)
//   Local -> consumed inside the processor          ( 8 IDs)
enum class Route : std::uint8_t { VP, MB, AE, CFG, ENG, Local };

[[nodiscard]] constexpr Route routeOf(Vst::ParamID id) noexcept {
    switch (id) {
        // --- Global (0-99) ---------------------------------------------------
        case kPolyphonyId:
        case kSoftLimitId:
        case kSeedId:
            return Route::ENG;

        // --- Harmonic Cloud (200-399) ---------------------------------------
        case kCloudRichnessId:
        case kCloudInharmonicityId:
        case kCloudTiltId:
        case kCloudMutationId:
        case kCloudGravityId:
        case kCloudDriftDepthId:
        case kCloudStereoSpreadId:
        case kCloudAttackId:
            return Route::MB;
        case kCloudDriftSmoothnessId:
        case kCloudDecayId:
        case kCloudEnvOffsetSpreadId:
            return Route::VP;

        // --- Spectral Morph (400-599) ---------------------------------------
        case kMorphEntropyId:
        case kMorphPositionId:
            return Route::MB;
        case kMorphBloomId:
        case kMorphTravelModeId:
        case kMorphTravelRateId:
        case kMorphWaypointIntervalId:
            return Route::VP;
        case kMorphStateCountId:
        case kMorphState0Id:
        case kMorphState1Id:
        case kMorphState2Id:
        case kMorphState3Id:
            return Route::CFG;

        // --- Life Modulators + Voice Envelope (600-799) ----------------------
        case kLifeSpatialDepthId:
        case kLifeVoiceWidthId:
        case kEnvStage0MsId:
        case kEnvStage1MsId:
        case kEnvReleaseMsId:
            return Route::MB;
        case kLifeSpatialRateId:
        case kLifeSpatialCouplingId:
        case kLifeSpatialGrowthId:
        case kEnvModeId:
        case kEnvGrowthDurationId:
            return Route::VP;

        // --- Continuous Body (800-999) --------------------------------------
        case kBodyDampingId:
            return Route::MB;
        case kBodyMaterialId:
        case kBodyResonanceId:
        case kBodyKeyTrackingId:
        case kBodyDriveId:
        case kBodyMixId:
        case kBodyCloudMixId:
        case kBodyCloudDecayId:
        case kBodyCloudSizeId:
        case kBodyCloudDampingId:
        case kBodyWidthId:
        case kBodyInputAgcId:
        case kBodyResonatorBypassId:
            return Route::VP;

        // --- Granular Atmosphere (1000-1199) --------------------------------
        case kAtmosLevelId:
        case kAtmosBlurId:
        case kAtmosDriftDepthId:
            return Route::MB;
        case kAtmosFreezeId:
            return Route::ENG;
        case kAtmosDensityId:
        case kAtmosGrainSecondsId:
        case kAtmosPanSpreadId:
        case kAtmosDecorrelationId:
        case kAtmosFreezeMixId:
        case kAtmosDriftSmoothnessId:
        case kAtmosDriftRangeId:
        case kAtmosJitterId:
        case kAtmosPositionId:
        case kAtmosPositionSpreadId:
        case kAtmosPitchId:
        case kAtmosPitchSpreadId:
        case kAtmosGrainEnvelopeId:
            return Route::VP;

        // --- Aether Space (1200-1399) ---------------------------------------
        case kAetherMixId:
        case kAetherSizeId:
        case kAetherShimmerOctaveId:
        case kAetherShimmerFifthId:
        case kAetherBloomSendId:
        case kAetherSizeBreathDepthId:
        case kAetherTideDepthId:
        case kAetherWidthId:
            return Route::MB;
        case kAetherDensityId:
        case kAetherDecayId:
        case kAetherFreezeId:
        case kAetherDimensionalityId:
        case kAetherDampingId:
        case kAetherPreDelayId:
        case kAetherModDepthId:
        case kAetherModSmoothnessId:
        case kAetherBloomDecayId:
        case kAetherSpectralDiffusionId:
            return Route::AE;

        // --- Processor-local: 0, 100-104, 405, 406 --------------------------
        default:
            return Route::Local;
    }
}

/// IDs 409-412 -> slot 0..3. Only ever called for a `CFG` slot ID.
[[nodiscard]] constexpr int spectralSlotIndexOf(Vst::ParamID id) noexcept {
    const auto raw = static_cast<int>(id) - static_cast<int>(kMorphState0Id);
    return std::clamp(raw, 0, 3);
}

/// Clamp into [0, kSpectralStateCount). The pack handler already clamps, so this
/// is the belt to its braces: a corrupt state stream must never index past the
/// factory table.
[[nodiscard]] constexpr std::size_t clampFactoryIndex(int stateId) noexcept {
    const int hi = static_cast<int>(Krate::DSP::kSpectralStateCount) - 1;
    return static_cast<std::size_t>(std::clamp(stateId, 0, hi));
}

/// Clamp into [0, kSeedValues.size()). C-10's table is the only seed source.
[[nodiscard]] constexpr std::size_t clampSeedIndex(int index) noexcept {
    const int hi = static_cast<int>(kSeedValues.size()) - 1;
    return static_cast<std::size_t>(std::clamp(index, 0, hi));
}

/// Clamp into [0, kSyncNoteBeats.size()).
[[nodiscard]] constexpr std::size_t clampSyncNoteIndex(int index) noexcept {
    const int hi = static_cast<int>(kSyncNoteBeats.size()) - 1;
    return static_cast<std::size_t>(std::clamp(index, 0, hi));
}

/// FR-056 / C-3 amendment 2. The change threshold on the DERIVED travel rate:
/// 0.1 % of kMinTravelRate (spectral_morph_engine.h:101), i.e. ~1.67e-6. Far
/// below any audible change in a 600-second journey, and ~10x above the float
/// noise of the division (values <= 1.0, relative error ~1e-7).
inline constexpr float kSyncedRateEpsilon =
    Krate::DSP::SpectralMorphEngine::kMinTravelRate * 1.0e-3f;

/// FR-041b. The five factory states, evaluated ONCE (in the constructor).
/// makeFactoryState() is documented "Deterministic and stateless"
/// (spectral_state.h:349-351), so this table is exactly what a per-change call
/// would have produced - at ~200 std::pow/std::exp per entry, paid once.
static_assert(Krate::DSP::kSpectralStateCount == 5,
              "FR-041b: factoryStates_ is sized 5 in processor.h; a sixth factory state "
              "must widen BOTH, or the table silently drops it");

// SC-023 clause 6 / clause 7(d) NEGATIVE-CONTROL SEAM (see processor.h's
// setSurfacePushDisabledForTest banner for why this is a runtime pair rather
// than the compile-time switch plan 7.13 words it as). Both default to false,
// no shipping path writes them, and the only readers are setState()'s
// requestPushAllSurfaces() call and setupProcessing()'s pushAllSurfaces() call.
// std::atomic, because the test thread writes them while a prepared processor
// may still be reachable from another thread; relaxed, because they order
// nothing.
// Mutability is the whole point of the seam, per the banner above.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> gDisablePresetLoadPush{false};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> gDisableRepreparePush{false};

[[nodiscard]] std::array<Krate::DSP::SpectralState, 5> makeFactoryStateTable() {
    std::array<Krate::DSP::SpectralState, 5> table{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        table[i] = Krate::DSP::makeFactoryState(static_cast<Krate::DSP::SpectralStateId>(i));
    }
    return table;
}

// ==============================================================================
// FR-050. The five macro knobs -> SeraphisMacroMatrix::setMacros (plan 3.5.5)
// ==============================================================================

/// Krate::DSP::SeraphisMacroValues is a plain aggregate of five floats with no
/// operator== declared and none defaulted (seraphis_macro_matrix.h:122-128), and
/// C++20 does NOT synthesise == for such an aggregate - `m != lastPushedMacros_`
/// does not compile. Defaulting == in the DSP struct would touch a FIFTH dsp/
/// file, which FR-071 restricts to four, so the compare is field by field and it
/// lives in THIS TU.
[[nodiscard]] bool macrosEqual(const Krate::DSP::SeraphisMacroValues& a,
                               const Krate::DSP::SeraphisMacroValues& b) noexcept {
    return a.dream == b.dream && a.bloom == b.bloom && a.dissolve == b.dissolve
           && a.gravity == b.gravity && a.entropy == b.entropy;
}

/// FR-050. The five knobs as the CLASS-(b) SMOOTHERS currently hold them - never
/// straight off macroParams_.
///
/// IDs 100-104 are class (b) (FR-059): they reach BodyDamping,
/// AetherSizeBreathDepth and AetherDimensionalityTideDepth, which are themselves
/// class (b), so smoothing the five knobs is what covers every macro row
/// uniformly. Reading the atomics here would deliver the full step on the chunk
/// the host moved the knob and defeat that.
///
/// The array order is SeraphisMacro's: Dream, Bloom, Dissolve, Gravity, Entropy
/// (processor.h's macroSm_ banner states the same order).
[[nodiscard]] Krate::DSP::SeraphisMacroValues readSmoothedMacros(
    const std::array<Krate::DSP::OnePoleSmoother, 5>& smoothers) noexcept {
    return Krate::DSP::SeraphisMacroValues{.dream = smoothers[0].getCurrentValue(),
                                           .bloom = smoothers[1].getCurrentValue(),
                                           .dissolve = smoothers[2].getCurrentValue(),
                                           .gravity = smoothers[3].getCurrentValue(),
                                           .entropy = smoothers[4].getCurrentValue()};
}

}  // namespace

Processor::Processor() {
    setControllerClass(kControllerUID);

    // FR-041b. The factory table is built HERE, not in setupProcessing(): it needs
    // no sample rate, and deferring it would leave a window - construction through
    // the host's first prepare - in which getState() would serialize four VALID,
    // EMPTY payloads. It is immutable from this point on, which is what lets both
    // threads read it without synchronisation (plan 3.7).
    factoryStates_ = makeFactoryStateTable();

    // The audio-thread-owned live slots start at C-6's registered slot defaults,
    // so applySpectralStates() can never hand SpectralMorphEngine::setState a
    // default-constructed (all-zero, invalid) payload. lastPushedSlotStateId_
    // stays at its -1 sentinel, so the first push of even a DEFAULT slot value
    // still refreshes - which costs one 540 B copy and keeps the tracker honest.
    for (std::size_t s = 0; s < spectralSlots_.size(); ++s) {
        spectralSlots_[s] = factoryStates_[clampFactoryIndex(kMorphSlotDefaultIndices[s])];
    }
}

Processor::~Processor() = default;

void Processor::setSurfacePushDisabledForTest(bool onPresetLoad, bool onReprepare) noexcept {
    gDisablePresetLoadPush.store(onPresetLoad, std::memory_order_relaxed);
    gDisableRepreparePush.store(onReprepare, std::memory_order_relaxed);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    // FR-020: instrument shape - one event input, one stereo audio output.
    // There is deliberately NO addAudioInput(): Seraphis takes no audio in, and
    // an auxiliary input bus would also make the AU wrapper create an IO element
    // that au-info.plist's "Inputs 0 / Outputs 2" does not declare (-10875).
    // Model: plugins/membrum/src/processor/processor.cpp:117, 120.
    // Anti-model: plugins/ruinae/src/processor/processor.cpp:56.
    addEventInput(STR16("Event In"));
    addAudioOutput(STR16("Main Out"), Vst::SpeakerArr::kStereo);

    // FR-022: heap, non-RT, exactly once.
    engine_ = std::make_unique<Krate::DSP::SeraphisEngine>();
    reverb_ = std::make_unique<Krate::DSP::AetherReverb>();

    return kResultOk;
}

tresult PLUGIN_API Processor::terminate() {
    // `= nullptr`, not `.reset()`: both pointees expose their own reset() member,
    // so `engine_.reset()` reads as "reset the engine's DSP state" when it in
    // fact DESTROYS the engine (readability-ambiguous-smartptr-reset-call).
    // Assignment says the intended thing unambiguously and is identical in effect.
    engine_ = nullptr;
    reverb_ = nullptr;
    return AudioEffect::terminate();
}

// FR-021. The base AudioEffect::setBusArrangements accepts whatever the host
// proposes, so it is overridden here with three rejections, each guarding a
// distinct failure:
//   (a) no audio input bus exists to arrange;
//   (b) EXACTLY one output bus exists - accepting numOuts == 2 would let a host
//       successfully negotiate a bus that does not exist. (The model rejects
//       numOuts > kMaxOutputBuses at plugins/membrum/src/processor/processor.cpp:
//       1058-1059 because Membrum has 16 buses; Seraphis has one.)
//   (c) the render path reads channelBuffers32[0] and [1] unconditionally, so
//       accepting a mono arrangement is an out-of-bounds read on the audio
//       thread (the model's own rationale, membrum processor.cpp:1044-1049).
// The rejection is NOT the guard: a host may ignore kResultFalse and still
// present a mono bus, so process() carries an independent
// data.outputs[0].numChannels < 2 early-out (added by T018).
tresult PLUGIN_API Processor::setBusArrangements(
    Vst::SpeakerArrangement* /*inputs*/, int32 numIns,
    Vst::SpeakerArrangement* outputs, int32 numOuts) {
    if (numIns != 0) {
        return kResultFalse;
    }
    if (numOuts != 1) {
        return kResultFalse;
    }
    if (outputs == nullptr || outputs[0] != Vst::SpeakerArr::kStereo) {
        return kResultFalse;
    }
    return kResultTrue;
}

// FR-023 / FR-028 / FR-053, in exactly the order FR-023 lists.
tresult PLUGIN_API Processor::setupProcessing(Vst::ProcessSetup& setup) {
    // 0. Out-of-order host calls - setupProcessing() before initialize(), or
    //    after terminate() - are exactly what pluginval strictness 5 probes.
    //    Returning here leaves prepared_ false, so process()'s readiness guard
    //    still fires and getLatencySamples() still reports 0 (no reverb_).
    //    There is deliberately NO MXCSR/denormal call in this function: the
    //    flush-to-zero mode is PER-THREAD, and setupProcessing() runs on the
    //    host's setup thread, never the audio thread. Setting it here would
    //    never reach the render (plugins/membrum/src/processor/processor.cpp:
    //    1073-1075); ScopedDenormalMode inside process() is the real guard.
    if (engine_ == nullptr || reverb_ == nullptr) {
        return AudioEffect::setupProcessing(setup);
    }

    sampleRate_ = setup.sampleRate;

    // 1. The block bound is the CONSTANT 2048 (seraphis_engine_config.h:40),
    //    never setup.maxSamplesPerBlock: FR-026's slice loop and FR-028's
    //    scratch must not change shape when a host renegotiates its block size.
    const std::size_t bound = kMaxBlockSamples;

    // 2. Polyphony is seeded FROM THE PARAMETER - setState() may legally
    //    precede setupProcessing() - through the ONE clamping conversion
    //    (parameters/global_params.h:62), never a bare cast.
    const std::size_t poly =
        clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));

    // 3. The two prepares. Both are non-RT and allocate; this is the only place
    //    either is called.
    engine_->prepare(sampleRate_, makeSeraphisEngineConfig(poly, kEngineSeed, bound));
    reverb_->prepare(sampleRate_, makeSeraphisReverbConfig(bound));

    // DEBUG-ONLY: the two components must have adopted the SAME rate. Neither
    // exposes a sample-rate getter, so the agreement is asserted on the one
    // input that decides it: AetherReverb::prepare clamps into
    // [kMinSampleRate, kMaxSampleRate] (aether_reverb.h:1615-1616) while
    // SeraphisEngine::prepare only floors at 1.0 (seraphis_engine.h:202). Inside
    // that window - and only inside it - both adopt sampleRate_ verbatim.
    assert(sampleRate_ >= static_cast<double>(Krate::DSP::AetherReverb::kMinSampleRate) &&
           sampleRate_ <= static_cast<double>(Krate::DSP::AetherReverb::kMaxSampleRate) &&
           "engine and reverb adopt different sample rates outside the reverb's clamp window");

    // 3b. FR-047 (plan 3.4). THE shared invalidation sequence, Reprepared arm:
    //     the DSP objects really were re-initialised, so every surface must be
    //     re-pushed on the first process() after this call. It touches no DSP
    //     object of its own - it only resets the lastPushed* trackers - which is
    //     why it is safe here, with the audio thread stopped, AND from
    //     process(). Step 4's polyphony line below is Phase 8's ONE documented
    //     exception to it and must stay AFTER it.
    if (!gDisableRepreparePush.load(std::memory_order_relaxed)) {
        pushAllSurfaces(SurfaceInvalidation::Reprepared);
    }

    // 3c. FR-041b (plan 3.7). The live slots are re-seeded FROM THE CURRENT
    //     ATOMICS, never from the registered defaults: setState() may legally
    //     precede setupProcessing(), and a defaults-seeding prepare would
    //     silently overwrite a preset that had already been loaded with nothing
    //     ever re-deriving it (refreshSpectralSlotFromFactory() runs only on a
    //     CFG parameter change). Reading the atomics makes the two orders
    //     converge on the same result. factoryStates_ is NOT rebuilt - it was
    //     built in the constructor and is immutable.
    for (std::size_t s = 0; s < spectralSlots_.size(); ++s) {
        const int stateId = morphParams_.slot[s].load(std::memory_order_relaxed);
        spectralSlots_[s] = factoryStates_[clampFactoryIndex(stateId)];
        lastPushedSlotStateId_[s] = stateId;
    }

    // 4. Seed the push trackers with what prepare() ACTUALLY delivered - read
    //    back from the engine, not from `poly`, so the tracker records the
    //    engine's own clamp (seraphis_engine.h:665). NEVER 8, and never a
    //    force-push sentinel: prepare() already installed this voice count, so
    //    the first process() must NOT re-call setPolyphony(), which would
    //    re-arm sumGain_ (seraphis_engine.h:349) on every host prepare.
    lastPushedPolyphony_ = engine_->getPolyphony();
    lastPushedSoftLimit_ = globalParams_.softLimit.load(std::memory_order_relaxed);
    // FR-045. The soft-limit VALUE is seeded above (step 4's direct push below is
    // what installs it), but its FR-041a cadence counter has not moved, and a
    // re-prepare really did re-initialise the saturator. Invalidating the flag
    // makes the first process() after every prepare push exactly once and count
    // it - which is the cadence SC-007's ENG clause asserts.
    lastPushedSoftLimitValid_ = false;

    // KNOWN RESIDUAL (plan §8.3), recorded rather than hidden: when softLimit is
    // false at prepare, this push RAMPS instead of snapping. SeraphisEngine::
    // prepare() sets satL_/satR_.setSaturation(kOutputSaturation) BEFORE
    // satL_.prepare() precisely so the saturator's smoothers are snapped
    // (seraphis_engine.h:225-231); this setter necessarily runs AFTER prepare()
    // - calling it before is useless, because prepare() re-applies
    // kOutputSaturation unconditionally - and post-prepare
    // TapeSaturator::setSaturation takes the ramping branch
    // (tape_saturator.h:248-252). Effect: the first kDefaultSmoothingMs = 5.0f
    // of the render carries a decaying <= 0.15 tanh/linear blend that should
    // have been 0. Removing it requires threading outputSaturation through
    // SeraphisEngineConfig - a dsp/ change Phase 8's scope forbids.
    // DEFERRED TO PHASE 9; not silently accepted.
    engine_->setOutputSaturation(
        lastPushedSoftLimit_ ? Krate::DSP::SeraphisEngine::kOutputSaturation : 0.0f);

    // 5. FR-028: scratch sized ONCE, to the constant - never to the host block.
    dryL_.assign(bound, 0.0f);
    dryR_.assign(bound, 0.0f);
    wetL_.assign(bound, 0.0f);
    wetR_.assign(bound, 0.0f);

    // 6. Master-gain smoother, then arm the FR-024a clause 3 first-block snap.
    masterGain_.configure(kMasterGainSmoothMs, static_cast<float>(sampleRate_));

    // 6b. FR-059(b) (plan 3.5.2). The nine class-(b) smoothers take the per-ID
    //     time constant FR-059(b) clause 2's second form allows - TWO values, not
    //     one, and the split is measured rather than assumed (the derivation and
    //     the numbers are on kParamSmoothMs / kAetherDepthSmoothMs in
    //     processor.h). The body coefficients (801, 802) keep the 20 ms family of
    //     kMasterGainSmoothMs immediately above; the two aether depths and the
    //     five macros that reach them take AetherReverb::kSizeSmoothingMs's
    //     300 ms, because their stair lands on an exponentially-mapped delay READ
    //     LENGTH. Configured HERE, beside the master gain, so the set cannot
    //     drift apart and a sample-rate change re-derives every coefficient.
    //
    //     controlPhase_ restarts with the prepared configuration: it is an
    //     absolute grid, but "absolute" is a property of one prepared run, and
    //     carrying a stale phase across a re-prepare would offset the first chunk
    //     of the new one for no benefit.
    //
    //     Step 3b's pushAllSurfaces(Reprepared) already raised
    //     snapParamSmoothers_, so the first advanceParamSmoothers() after this
    //     call SNAPS all nine onto the current atomics rather than ramping from
    //     the pre-prepare values.
    {
        const auto sr = static_cast<float>(sampleRate_);
        resonanceSm_.configure(kParamSmoothMs, sr);           // ID 801
        bodyDampingSm_.configure(kParamSmoothMs, sr);         // ID 802
        breathDepthSm_.configure(kAetherDepthSmoothMs, sr);   // ID 1215
        tideDepthSm_.configure(kAetherDepthSmoothMs, sr);     // ID 1216
        for (Krate::DSP::OnePoleSmoother& s : macroSm_) {     // IDs 100-104
            s.configure(kAetherDepthSmoothMs, sr);
        }
    }
    controlPhase_ = 0;

    anySamplesSincePrepare_ = false;
    prepared_ = true;

    // 7. No latency announcement: the reported value is the constant 1024 in
    //    every reachable state, and this component has no IComponentHandler to
    //    announce on (plan §1.3 C-1/C-2).
    return AudioEffect::setupProcessing(setup);
}

// FR-032. Both branches run on the host thread with the audio thread stopped,
// which is what makes the deactivate branch legal at all.
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state != 0) {
        // Activation does exactly ONE thing, and it allocates nothing (SC-026
        // clause 2): re-arm the FR-024a clause 3 seam, so the first process()
        // after re-activation SNAPS the master-gain smoother to the current
        // parameter instead of ramping from the pre-deactivation value.
        anySamplesSincePrepare_ = false;
    } else {
        // Deactivation must leave no ringing tail (SC-026 clause 1).
        // SeraphisEngine::silence() is documented NOT an audio-thread operation
        // (seraphis_engine.h:306-307; ~32 MiB of capture-ring clearing) - which
        // is correct HERE and only here.
        if (engine_ != nullptr) {
            engine_->silence();
        }
        if (reverb_ != nullptr) {
            // Dereferenced form, not `reverb_->reset()`: the arrow spelling is
            // ambiguous with unique_ptr's OWN reset() (which would destroy the
            // reverb here), and clang-tidy flags it for exactly that reason
            // (readability-ambiguous-smartptr-reset-call). This clears the
            // reverb's tail - aether_reverb.h:1971 - and keeps the object.
            (*reverb_).reset();
        }
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API Processor::process(Vst::ProcessData& data) {
    // FR-029. FTZ/DAZ is PER-THREAD, so it must be armed here, on the audio
    // thread, and not in setupProcessing() (plugins/membrum/src/processor/
    // processor.cpp:1073-1075). core/scoped_denormal_mode.h:60.
    const Krate::DSP::ScopedDenormalMode denormalGuard;

    // Automation is latched BEFORE the shape guards: a block that renders
    // nothing must still not lose the host's parameter changes.
    processParameterChanges(data.inputParameterChanges);

    // FR-030 early-outs, in THIS ORDER. The order is load-bearing twice over:
    //  (a) buffer VALIDATION precedes the readiness check, so the one degenerate
    //      case with a valid writable buffer - process() before
    //      setupProcessing() - can be ZERO-FILLED. FR-030 says "by producing
    //      silence", and VST3 does NOT guarantee zeroed output buffers, so
    //      returning without writing hands the host back the previous
    //      plug-in's content. Both wrapped components zero-fill on their own
    //      not-prepared path (seraphis_engine.h:448-451,
    //      aether_reverb.h:2172-2176); so does this one.
    //  (b) nothing reads data.outputs[0] until numOutputs > 0 and
    //      outputs != nullptr are established (SC-021's ordering clause).
    if (data.numOutputs <= 0 || data.outputs == nullptr) {
        return kResultOk;
    }
    if (data.outputs[0].channelBuffers32 == nullptr) {
        return kResultOk;
    }
    // A host may ignore setBusArrangements' kResultFalse and still present a
    // mono bus; channelBuffers32 is then a ONE-element array and [1] is out of
    // bounds. Model: plugins/ruinae/src/processor/processor.cpp:430. Membrum -
    // the model for the rest of this processor - has no numChannels check
    // anywhere, so copying Membrum here would copy the gap.
    if (data.outputs[0].numChannels < 2) {
        return kResultOk;
    }
    if (data.numSamples <= 0) {
        return kResultOk;
    }
    const auto total = static_cast<std::size_t>(data.numSamples);  // now known > 0
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];
    if (outL == nullptr || outR == nullptr) {
        return kResultOk;
    }

    // Not ready -> SILENCE, not "leave the buffer alone".
    if (!prepared_ || engine_ == nullptr || reverb_ == nullptr) {
        std::fill_n(outL, total, 0.0f);
        std::fill_n(outR, total, 0.0f);
        data.outputs[0].silenceFlags = 3;  // both channels ARE silent
        return kResultOk;
    }

    // ---- FR-047: consume the off-audio-thread force-push request -------------
    // THE POSITION IS NORMATIVE: immediately after the not-ready silence path and
    // ABOVE pushGlobalParams(). pushAllSurfaces() invalidates the ENG trackers; if
    // the consume ran BELOW pushGlobalParams(), those sentinels would survive into
    // the NEXT block, where FR-045's code would re-run engine_->setSeed() and
    // reverb_->setSeed() for a seed that did not change - and AetherReverb::setSeed
    // is documented "Mid-render this is therefore a discontinuity in the drift and
    // tide" (aether_reverb.h:2351-2358). PresetLoad, not Reprepared: setState()
    // wrote the atomics before the release store, so the ordinary
    // atomic-vs-tracker compare already delivers anything that genuinely changed.
    if (forcePushAllPending_.exchange(false, std::memory_order_acquire)) {
        pushAllSurfaces(SurfaceInvalidation::PresetLoad);
    }

    // FR-024 STEP 0. Once per process() call, BEFORE the slice loop and before
    // the engine renders a sample. Hoisting is valid for the same reason the
    // master-gain target below is hoisted (plan D-1): processParameterChanges()
    // ran at the top of this function and took the LAST point of every queue
    // (FR-042), so neither atomic can change within a process() call and a
    // per-slice push would push the identical value.
    pushGlobalParams();

    // ---- Phase 9's pre-slice push block (plan 3.3 (B)) ----------------------
    // ONCE PER process() CALL, NEVER PER SLICE. Hoisting is valid for the same
    // stated reason the master-gain target below is hoisted:
    // processParameterChanges() ran at the top of this function and took the LAST
    // point of every queue, so no atomic can change within a process() call.
    consumeSpectralSlotHandoff();                  // FR-041b, plan 3.7 steps 3-4
    updateSyncedTravelRate(data.processContext);   // FR-056; may dirty the VP generation
    pushAetherParamsIfDirty();                     // FR-044: the 10 AE values
    pushSpectralStatesIfPending();                 // FR-046: the 5 CFG values
    // FR-059(b) (plan 3.5.4). THE TARGETS ARE SET ONCE PER process() CALL, HERE,
    // BEFORE the slice loop - never from inside advanceParamSmoothers(). The
    // hoist is valid for exactly the stated reason the master-gain target hoist
    // below is: processParameterChanges() ran at the top of this function and
    // took the LAST point of every queue, so no atomic can change within a
    // process() call and a per-slice re-target would push the identical value.
    //
    // It is also LOAD-BEARING, not merely an optimisation. The slice loop
    // evaluates anyClassBSmootherUnsettled() BEFORE it calls
    // advanceParamSmoothers(n); a target set inside the advance would leave that
    // predicate reading current_ == target_ (the OLD value) on the first slice
    // after every change, so isComplete() would be true, no subdivision would
    // happen, and advanceSamples(512) would deliver 93.0 % of the step in one
    // push - collapsing SC-005's positive control (b) to ~1.075 x against a
    // 1.5 x bound.
    setParamSmootherTargets();                     // FR-059(b)
    // pushVoiceParams() / pushMacroSurfaces() are NOT here: they run INSIDE the
    // slice loop, on the absolute 64-sample control-chunk grid, for as long as
    // any class-(b) smoother is un-settled. When every one is settled - the
    // steady state, i.e. all of ordinary playback - the loop runs exactly one
    // slice per event span and the cadence is again one push per process() call.

    // FR-024a clause 3, first half. The master-gain TARGET is read ONCE per
    // process() call, BEFORE the slice loop. Hoisting is valid, not a shortcut:
    // processParameterChanges() ran at the top of this function and took the
    // LAST point of every queue (FR-042), so the atomic cannot change within a
    // process() call - a per-slice setTarget() would push the identical value.
    // Hoisting it additionally makes the target trivially partition-invariant
    // (plan D-1, 3.1).
    const float gainTarget = globalParams_.masterGain.load(std::memory_order_relaxed);
    if (!anySamplesSincePrepare_) {
        // SNAPPED, never ramped, on the first block after setupProcessing() /
        // setActive(true) - the seam AetherReverb::applyControl uses
        // (aether_reverb.h:2951-2956). Without it a render at kMasterGainId = 0
        // would ramp DOWN from the previous value and its first ~20 ms would be
        // non-zero, failing SC-019 clause 1 for a correct implementation.
        masterGain_.snapTo(gainTarget);  // smoother.h:263
    } else {
        masterGain_.setTarget(gainTarget);  // smoother.h:170
    }

    // FR-025 / FR-026: the event-driven slice loop. SeraphisEngine::noteOn /
    // noteOff take NO sample offset (seraphis_engine.h:370, 415), so
    // sub-division is the only way to deliver one.
    std::size_t cursor = 0;
    int32 nextEvent = 0;
    const int32 numEvents =
        (data.inputEvents != nullptr) ? data.inputEvents->getEventCount() : 0;

    while (cursor < total) {
        // 1. Dispatch EVERY event due at this slice start. A `while`, NOT an
        //    `if`: with an `if` the second event at the same offset would
        //    resolve the next sliceEnd back to `cursor` and a zero-length slice
        //    would reach processStereoBlock (SC-022 clause 5;
        //    tests/test_helpers/seraphis_chain.h:195-198 has the same `while`).
        while (nextEvent < numEvents) {
            Vst::Event event{};
            if (data.inputEvents->getEvent(nextEvent, event) != kResultOk) {
                ++nextEvent;
                continue;
            }
            if (clampOffset(event.sampleOffset, total) > cursor) {
                break;  // due later in this block
            }
            dispatchEvent(*engine_, event);
            ++nextEvent;
        }

        // 2. Slice end = the next event's offset, the block end, or the 2048
        //    bound - whichever comes first. Events are assumed sorted by
        //    sampleOffset (VST3 requires it); the `at > cursor` test keeps the
        //    loop well formed on a malformed list by firing a late-but-earlier
        //    event at the current cursor instead of rewinding it.
        std::size_t sliceEnd = total;
        if (nextEvent < numEvents) {
            Vst::Event event{};
            if (data.inputEvents->getEvent(nextEvent, event) == kResultOk) {
                const std::size_t at = clampOffset(event.sampleOffset, total);
                if (at > cursor && at < sliceEnd) {
                    sliceEnd = at;
                }
            }
        }
        // FR-026, and the ONLY slice bound. kMaxBlockSamples is the SAME
        // constant makeSeraphisEngineConfig()/makeSeraphisReverbConfig() were
        // prepared with (seraphis_engine_config.h:40), so the engine ceiling and
        // the reverb ceiling cannot drift apart. This is the branch a host block
        // larger than 2048 enters.
        sliceEnd = std::min(sliceEnd, cursor + kMaxBlockSamples);
        std::size_t n = sliceEnd - cursor;
        if (n == 0) {
            break;  // unreachable given the `while` above; guarded anyway
        }

        // FR-059(b) (plan 3.5.2/3.5.4). THE ONE RULE THIS LOOP GAINS: while any
        // class-(b) smoother is un-settled, cap the slice at the distance to the
        // next ABSOLUTE 64-sample control-chunk boundary. The clamp shape is
        // ContinuousBody's own, and 64 is not a new number - it is
        // SeraphisEngine::kControlChunkSamples, the shared control clock every
        // component in this engine already runs on, and the grid on which every
        // class-(a) smoother SC-005 compares against is advanced.
        //
        // This is NOT the per-slice ramp C-3 forbids: a slice boundary is
        // event-driven and moves with MIDI placement, whereas this grid is a
        // fixed absolute 64-sample rule that neither a host's block size nor a
        // performer's timing can move. `phase == 0` means the cursor is ON a
        // boundary, so the whole next chunk is available.
        if (anyClassBSmootherUnsettled()) {
            constexpr std::size_t kChunk = Krate::DSP::SeraphisEngine::kControlChunkSamples;
            const auto phase = static_cast<std::size_t>(controlPhase_ % kChunk);
            n = std::min(n, kChunk - phase);
        }

        // ORDER IS NORMATIVE: advance BEFORE the pushes, so this sub-slice
        // carries the value the smoothers just reached. FR-044 is satisfied
        // unchanged - macros_.apply() and applyAetherTargets() still run every
        // slice, at their existing positions inside renderSlice().
        advanceParamSmoothers(n);
        pushVoiceParams();    // FR-042: the 37 VP values
        pushMacroSurfaces();  // FR-043: the 27 MB bases
        renderSlice(outL + cursor, outR + cursor, n);
        controlPhase_ += n;
        cursor += n;
    }

    // FR-024a clause 3: the snap seam is consumed only once samples were
    // actually produced. setActive(true) and setupProcessing() re-arm it.
    anySamplesSincePrepare_ = true;

    // FR-024's silence-flag clause. Seraphis writes only the CLEARING half: it
    // never asserts silence, because deciding when the instance is genuinely
    // quiet needs a "reverb has decayed" predicate Phase 8 has no criterion for.
    // getTailSamples() therefore stays at the SDK default; tail/idle reporting
    // is Phase 10. Leaving the flags alone would let a host that reads a stale
    // "silent" flag cut the (by-design near-infinite) reverb tail.
    data.outputs[0].silenceFlags = 0;
    return kResultOk;
}

// FR-033, in whole. AetherReverb::getLatencySamples() returns
// `spectralEnabled_ ? diffusionFftSize_ : 0` (aether_reverb.h:2607-2613) and
// makeSeraphisReverbConfig pins both, so this reports the CONSTANT 1024 from
// construction onwards - before the first prepare, at every sample rate, and
// after every parameter. There is no transition, hence no announcement (and no
// IComponentHandler to make one on; plan §1.3 C-1/C-2). The 0 branch is only
// reachable between construction and initialize(), or after terminate().
uint32 PLUGIN_API Processor::getLatencySamples() {
    if (reverb_ == nullptr) {
        return 0;
    }
    return static_cast<uint32>(reverb_->getLatencySamples());
}

// FR-090 - FR-094 (plan 5.5). Reading state is safe CONCURRENTLY WITH
// process(): no prepare() is reachable from here; every scalar written below is
// a std::atomic<> member of a parameter pack; the four SpectralState payloads go
// into a STAGING buffer the audio thread is not reading (plan 3.7's three-buffer
// ring); and the only thing written toward the audio thread is ONE release
// store, in requestPushAllSurfaces().
//
// (polyphony in particular enters the engine's domain only through
// pushGlobalParams(), on the audio thread, via clampPolyphony.)
tresult PLUGIN_API Processor::setState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }

    IBStreamer streamer(state, kLittleEndian);

    // The version int32 is the ONE mandatory field. A stream too short to carry
    // it is not a Seraphis state at all; a stream from a FUTURE version cannot
    // be interpreted, and applying its bytes to the current layout would install
    // garbage. Both are refused outright, before anything is stored (FR-093).
    int32 version = 0;
    if (!streamer.readInt32(version)) {
        return kResultFalse;
    }
    if (version > kCurrentStateVersion) {
        return kResultFalse;  // v3+ refused, nothing mutated
    }

    // Plan 3.7 step 1. Pick a staging buffer that is neither published nor being
    // copied, BEFORE any payload is read.
    const std::size_t w = pickStagingBuffer();

    // The staging buffer is pre-seeded from the CURRENT slot selections, so a
    // stream that stops before the payloads - every version-1 stream does, and
    // so does any truncated v2 stream - publishes the FACTORY states rather than
    // the zero-initialised array. A published all-zero SpectralState is valid
    // (numPartials 0 skips the ratio loop) and would install four SILENT slots,
    // which is strictly worse than the rejected-and-defaulted failure it would
    // be mistaken for (plan 3.7).
    for (std::size_t s = 0; s < spectralSlotsStaging_[w].size(); ++s) {
        const int stateId = morphParams_.slot[s].load(std::memory_order_relaxed);
        spectralSlotsStaging_[w][s] = factoryStates_[clampFactoryIndex(stateId)];
    }

    // Order MUST match getState (and Controller::setComponentState) EXACTLY -
    // plan 5.1's write order. Every loader is EOF-safe: a short stream leaves
    // every unread field at its registered default and returns false, which is
    // not an error here - a version-1 stream stops after [macro] and a truncated
    // preset loads as far as it goes (FR-093).
    loadGlobalParams(globalParams_, streamer);   // [global]  12 B
    loadMacroParams(macroParams_, streamer);     // [macro]   20 B
    // ---- end of a version-1 stream: 36 B, a STRICT PREFIX of v2 ----
    loadGlobalSeed(globalParams_, streamer);     // [seed]     4 B (FR-091a)
    loadCloudParams(cloudParams_, streamer);     // [cloud]   44 B
    loadMorphParams(morphParams_, streamer,      // [morph] 2216 B
                    spectralSlotsStaging_[w]);
    loadLifeModParams(lifeParams_, streamer);    // [life]    40 B
    loadBodyParams(bodyParams_, streamer);       // [body]    52 B
    loadAtmosphereParams(atmosParams_, streamer);// [atmos]   68 B
    loadAetherParams(aetherParams_, streamer);   // [aether]  72 B

    // Plan 3.7 step 3: publish, then advance the cursor. The store is the ONLY
    // thing that makes the buffer visible to the audio thread, and it happens
    // after every byte of it has been written.
    spectralSlotsHandoff_.store(static_cast<int>(w), std::memory_order_release);
    stagingWriteCursor_ = static_cast<int>((w + 1u) % spectralSlotsStaging_.size());

    // FR-047 / SC-023. ONE release store. pushAllSurfaces()' body runs at the
    // top of process(); calling it here would be a data race on ~40 tracker
    // scalars, because setState() may legally run concurrently with process().
    if (!gDisablePresetLoadPush.load(std::memory_order_relaxed)) {
        requestPushAllSurfaces();
    }
    return kResultOk;
}

// FR-090 / FR-094. Stream layout is FIXED (spec C-8, plan 5.1): little-endian,
// 2532 bytes = the version int32 + 73 floats + 18 int32 + four 541-byte
// SpectralState payloads, in the block order below.
//
// IT NEVER READS spectralSlots_. That array is audio-thread-owned (plan 3.7's
// ownership table) and a message-thread read of it would be a data race whose
// visible symptom is a TORN SpectralState in the saved preset - exactly what a
// host that automates a CFG dropdown while saving would hit. The two
// message-thread-safe sources are used instead: the published staging buffer
// while a handoff is outstanding, and the immutable factory table otherwise.
tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }

    IBStreamer streamer(state, kLittleEndian);

    streamer.writeInt32(kCurrentStateVersion);   //             4 B

    saveGlobalParams(globalParams_, streamer);   // [global]   12 B
    saveMacroParams(macroParams_, streamer);     // [macro]    20 B
    saveGlobalSeed(globalParams_, streamer);     // [seed]      4 B (FR-091a)
    saveCloudParams(cloudParams_, streamer);     // [cloud]    44 B
    saveMorphParams(morphParams_, streamer);     // [morph]    52 B of scalars

    // ...and the four payloads that saveMorphParams has no source for (FR-041b).
    std::array<Krate::DSP::SpectralState, 4> payloads{};
    const int published = spectralSlotsHandoff_.load(std::memory_order_acquire);
    // The negative test runs FIRST, so the width comparison is size_t vs size_t
    // and never a signed/unsigned mix.
    const bool havePublished =
        published >= 0
        && static_cast<std::size_t>(published) < spectralSlotsStaging_.size();
    for (std::size_t s = 0; s < payloads.size(); ++s) {
        payloads[s] =
            havePublished
                ? spectralSlotsStaging_[static_cast<std::size_t>(published)][s]
                : factoryStates_[clampFactoryIndex(
                      morphParams_.slot[s].load(std::memory_order_relaxed))];
    }
    saveSpectralPayloads(payloads, streamer);    // [morph]  2164 B of payload

    saveLifeModParams(lifeParams_, streamer);    // [life]     40 B
    saveBodyParams(bodyParams_, streamer);       // [body]     52 B
    saveAtmosphereParams(atmosParams_, streamer);// [atmos]    68 B
    saveAetherParams(aetherParams_, streamer);   // [aether]   72 B

    return kResultOk;
}

// ==============================================================================
// Private helpers
// ==============================================================================

// FR-042 / FR-043. Automation is latched by taking THE LAST POINT of each
// queue, never getPoint(0): VST3 delivers a whole block's automation lane at
// once, so the value that must be in force when the block is rendered is the
// final one in the queue. Dispatch is by ID RANGE (plugin_ids.h:79-80), not by
// an enumeration of the eight IDs, so a Phase 9 parameter added inside 0-99 or
// 100-199 reaches its pack without touching this function; anything outside
// both ranges is ignored rather than misrouted.
//
// RT-safe: no allocation, no locking, no I/O. Every store beneath this is a
// relaxed std::atomic write in the two parameter packs.
void Processor::processParameterChanges(Vst::IParameterChanges* changes) noexcept {
    if (changes == nullptr) {
        return;
    }

    const int32 numQueues = changes->getParameterCount();
    for (int32 q = 0; q < numQueues; ++q) {
        Vst::IParamValueQueue* queue = changes->getParameterData(q);
        if (queue == nullptr) {
            continue;
        }

        const int32 numPoints = queue->getPointCount();
        if (numPoints <= 0) {
            continue;
        }

        int32 sampleOffset = 0;
        Vst::ParamValue value = 0.0;
        if (queue->getPoint(numPoints - 1, sampleOffset, value) != kResultTrue) {
            continue;
        }

        const Vst::ParamID id = queue->getParameterId();
        if (id < kGlobalParamRangeEnd) {
            // Every ID in this band is ENG or processor-local, so there is no
            // generation to bump: kSeedId, kPolyphonyId and kSoftLimitId are
            // delivered by pushGlobalParams()' own on-change trackers.
            handleGlobalParamChange(globalParams_, id, value);
        } else if (id < kMacroParamRangeEnd) {
            handleMacroParamChange(macroParams_, id, value);
        } else if (id < kCloudParamRangeEnd) {
            handleCloudParamChange(cloudParams_, id, value);
            markDirty(id);
        } else if (id < kMorphParamRangeEnd) {
            handleMorphParamChange(morphParams_, id, value);
            markDirty(id);
        } else if (id < kLifeModParamRangeEnd) {
            handleLifeModParamChange(lifeParams_, id, value);
            markDirty(id);
        } else if (id < kBodyParamRangeEnd) {
            handleBodyParamChange(bodyParams_, id, value);
            markDirty(id);
        } else if (id < kAtmosParamRangeEnd) {
            handleAtmosphereParamChange(atmosParams_, id, value);
            markDirty(id);
        } else if (id < kAetherParamRangeEnd) {
            handleAetherParamChange(aetherParams_, id, value);
            markDirty(id);
        }
        // else: an ID outside every shipped range - ignored.
    }
}

// FR-024a clauses 1-2. Called ONLY from process(), which has already
// established that engine_ is non-null and that the processor is prepared.
//
// Both pushes are ON CHANGE ONLY. Re-calling setPolyphony() unconditionally is
// wrong twice over: it re-arms the voice-sum smoother (sumGain_.setTarget(...),
// seraphis_engine.h:349) every block, and setVoiceCount walks the allocator's
// excess-slot loop (:339-348) for nothing.
//
// clampPolyphony() (parameters/global_params.h:62) is MANDATORY here, not
// decorative. setPolyphony clamps to [1, kMaxVoices] (seraphis_engine.h:322)
// and getPolyphony() returns the CLAMPED value (:665), so if the stored atomic
// could hold an out-of-range number - which a hand-written or corrupt state
// stream carrying 0, 20 or a negative int32 can produce - the comparison below
// would be true on EVERY BLOCK, FOREVER: exactly the per-block sumGain_ re-arm
// and excess-slot walk this function exists to prevent. Clamping at the single
// conversion point puts both sides of the comparison in the same domain, so the
// detector converges after one push.
//
// setPolyphonyCalls_ is a test-only counter. It is a plain std::size_t written
// only from the audio thread and read only from the test thread once the render
// has completed; no atomic is needed.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. Both
//      atomics are read relaxed; both engine setters are noexcept and
//      allocation-free (setPolyphony is documented allocation-free at
//      seraphis_engine.h:319-321 because prepare() prepares all kMaxVoices
//      slots).
void Processor::pushGlobalParams() noexcept {
    const std::size_t poly =
        clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));
    if (poly != lastPushedPolyphony_) {  // ON CHANGE ONLY
        engine_->setPolyphony(poly);     // seraphis_engine.h:321
        // Re-read POST-CLAMP rather than storing `poly`, so the tracker records
        // what the engine actually holds - the same value the comparison above
        // will be made against next block.
        lastPushedPolyphony_ = engine_->getPolyphony();  // :665
        ++setPolyphonyCalls_;
    }

    const bool soft = globalParams_.softLimit.load(std::memory_order_relaxed);
    if (!lastPushedSoftLimitValid_ || soft != lastPushedSoftLimit_) {  // ON CHANGE ONLY
        engine_->setOutputSaturation(soft ? Krate::DSP::SeraphisEngine::kOutputSaturation
                                          : 0.0f);  // :566, :142
        lastPushedSoftLimit_ = soft;
        lastPushedSoftLimitValid_ = true;
        ++engSoftLimitPushes_;
    }

    // FR-045, the two Phase 9 ENG values. BOTH are on-change only, and that is not
    // an optimisation: SeraphisEngine::setSeed re-derives all sixteen voice seeds
    // (seraphis_engine.h:352-357), AetherReverb::setSeed is documented a mid-render
    // drift/tide discontinuity (aether_reverb.h:2351-2358), and
    // setAtmosphereFreeze is configure-time on every voice. Re-pushing an
    // unchanged value is a discontinuity for nothing, which is also why kSeedId is
    // exempt from SC-005 clauses 1-3.
    const int seedIndex = globalParams_.seedIndex.load(std::memory_order_relaxed);
    if (seedIndex != lastPushedSeedIndex_) {  // ON CHANGE ONLY
        // C-10's curated table is the ONLY seed source; the parameter carries an
        // INDEX, never the constant.
        const std::uint32_t seed = kSeedValues[clampSeedIndex(seedIndex)];
        engine_->setSeed(seed);  // seraphis_engine.h:353
        reverb_->setSeed(seed);  // aether_reverb.h:2361
        lastPushedSeedIndex_ = seedIndex;
        ++engSeedPushes_;
    }

    const bool freeze = atmosParams_.freeze.load(std::memory_order_relaxed);
    if (!lastPushedFreezeValid_ || freeze != lastPushedFreeze_) {  // ON CHANGE ONLY
        engine_->setAtmosphereFreeze(freeze);  // seraphis_engine.h:551
        lastPushedFreeze_ = freeze;
        lastPushedFreezeValid_ = true;
        ++engFreezePushes_;
    }
}

// FR-024 steps 2-6, in the order tests/test_helpers/seraphis_chain.h:190-259
// models them. Called ONLY from process(), which has already established that
// engine_, reverb_, outL and outR are non-null and that 0 < n <= 2048.
//
// FR-027: nothing here mirrors processOutputStage's internal 64-sample loop.
// The engine's own banner (seraphis_engine.h:506-511) states it is "a CADENCE
// CHOICE, NOT A SIZE CONSTRAINT ... Phase 8 must not copy the loop as if it
// were a requirement."
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. The
//      scratch vectors were sized once in setupProcessing() and are indexed
//      through .data() / operator[], never .at() (which throws).
void Processor::renderSlice(float* outL, float* outR, std::size_t n) noexcept {
    // 2. Macros -> engine, and the Aether-owned half -> reverb.
    //
    //    Applied EVERY SLICE even at Phase 8's neutral macro defaults (FR-034):
    //    computeAetherTargets() is what pushes the reverb's eight controls, and
    //    "inert" describes the macro VALUES, not the push.
    macros_.apply(*engine_);                                      // macro_matrix.h:623
    applyAetherTargets(*reverb_, macros_.computeAetherTargets());  // :667 + FR-034a

    // 3. Voice sum only - no reverb, no output stage.
    engine_->processStereoBlock(dryL_.data(), dryR_.data(), n);  // engine.h:441

    // 4. The Layer 4 stage the engine cannot own.
    reverb_->processStereoBlock(dryL_.data(), dryR_.data(), wetL_.data(), wetR_.data(),
                                n);  // reverb.h:2164

    // 4b. FR-024a clause 3: master gain, ONCE PER OUTPUT SAMPLE, on the reverb
    //     return. Never advanceSamples(n) and never once per slice - a ramp
    //     advanced per slice is partition-dependent BY CONSTRUCTION and would
    //     fail SC-008's block-size gate for a correct implementation.
    //
    //     The placement is load-bearing: a post-limiter multiply is FORBIDDEN,
    //     because at master gain 2.0 it produces peaks up to ~1.78 and makes
    //     SC-006's ceiling bound unsatisfiable by construction.
    for (std::size_t s = 0; s < n; ++s) {
        const float g = masterGain_.process();  // smoother.h:197
        wetL_[s] *= g;
        wetR_[s] *= g;
    }

    // 5. Output stage IN PLACE on the reverb return: tape saturator -> true-peak
    //    limiter. ALWAYS LAST.
    engine_->processOutputStage(wetL_.data(), wetR_.data(), n);  // engine.h:512

    std::copy_n(wetL_.data(), n, outL);
    std::copy_n(wetR_.data(), n, outR);

    // 6. Bloom lifecycle. Note-OFFs BEFORE note-ONs (seraphis_chain.h:236-254):
    //    a steal issues both in the same batch, and running the note-on first
    //    would release the resonator bank the note-on had just claimed.
    const auto bloom = engine_->consumeBloomEvents();  // engine.h:654
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        const std::uint32_t bit = std::uint32_t{1} << static_cast<std::uint32_t>(v);
        if ((bloom.noteOffMask & bit) != 0u) {
            reverb_->bloomNoteOff(static_cast<std::int32_t>(v));  // reverb.h:2473
        }
    }
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        const std::uint32_t bit = std::uint32_t{1} << static_cast<std::uint32_t>(v);
        if ((bloom.noteOnMask & bit) == 0u) {
            continue;
        }
        std::size_t count = 0;
        engine_->collectHeldPartials(v, bloomPartials_.data(), bloomPartials_.size(),
                                     count);  // engine.h:596
        if (count > 0) {
            reverb_->bloomNoteOn(static_cast<std::int32_t>(v), bloomPartials_.data(),
                                 count);  // reverb.h:2392
        }
    }
}

// ==============================================================================
// Phase 9 - routing (plan 3.2)
// ==============================================================================

// FR-042. `MB` DELIBERATELY BUMPS NO GENERATION COUNTER. It is pushed pre-slice,
// but by pushMacroSurfaces(), which change-detects on lastPushedBase_[] and never
// reads voiceParamGeneration_. A bump here would be purely spurious and would make
// every deep MB edit - 27 IDs, including the cloud controls users automate most -
// run applyVoiceParams, i.e. 37 setters x 16 voices = 592 setter calls it does not
// need.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. Every arm is
//      an integer increment or a 540 B POD copy.
void Processor::markDirty(Vst::ParamID id) noexcept {
    switch (routeOf(id)) {
        case Route::VP:
            ++voiceParamGeneration_;
            break;
        case Route::MB:
            break;  // see the banner above
        case Route::AE:
            ++aetherParamGeneration_;
            break;
        case Route::CFG:
            refreshSpectralSlotFromFactory(id);
            break;
        case Route::ENG:   // pushGlobalParams()' own trackers own these
        case Route::Local: // consumed inside the processor (master gain, macros, sync pair)
            break;
    }
}

// FR-041b. `id` is one of 408-412. For the four slot IDs this reads the pack's
// already-clamped atomic and copies 540 B out of the prepare-time factory table -
// NO transcendentals. It must NOT call makeFactoryState(), whose own banner reads
// "CONFIGURATION-TIME, not audio-thread: ... it evaluates ~200 std::pow/std::exp
// calls" (spectral_state.h:371-372): nothing debounces markDirty(), so a host
// automating 409-412 would re-run those every block, inside
// processParameterChanges(), a region no SC-008 arm measures.
//
// kMorphStateCountId (408) changes no slot CONTENT, but it must still raise the
// pending flag: setSpectralStateCount is on the same configure-time gate and is
// pushed by the same fan-out.
void Processor::refreshSpectralSlotFromFactory(Vst::ParamID id) noexcept {
    if (id == kMorphStateCountId) {
        const int count = morphParams_.stateCount.load(std::memory_order_relaxed);
        if (count == lastPushedStateCount_) {
            return;  // an unchanged automation point costs nothing
        }
        lastPushedStateCount_ = count;
        spectralStatesPending_ = true;
        spectralRetryMask_ = 0xFFFFu;
        return;
    }

    const int slot = spectralSlotIndexOf(id);
    const int stateId =
        morphParams_.slot[static_cast<std::size_t>(slot)].load(std::memory_order_relaxed);
    if (stateId == lastPushedSlotStateId_[static_cast<std::size_t>(slot)]) {
        return;  // an unchanged automation point costs nothing
    }
    lastPushedSlotStateId_[static_cast<std::size_t>(slot)] = stateId;
    spectralSlots_[static_cast<std::size_t>(slot)] = factoryStates_[clampFactoryIndex(stateId)];
    spectralStatesPending_ = true;
    spectralRetryMask_ = 0xFFFFu;  // every voice must accept again
}

// ==============================================================================
// Phase 9 - the pre-slice pushes (plan 3.3)
// ==============================================================================

// FR-002. The 37 VP fields, gathered from the packs. NO FIELD HERE MAY NAME A
// SeraphisMacroTarget (spec C-1/FR-055): those 27 values reach the voices through
// setTargetBase, and a second write path would double-apply them.
//
// morphTravelRate is the ONE field with a second source: while morph sync is on
// and the host supplies a valid tempo, FR-056's derived rate supersedes ID 404.
// The -1.0f sentinel is below kMinTravelRate, so it can never be a legal rate.
Krate::DSP::SeraphisVoiceParams Processor::buildVoiceParams() const noexcept {
    constexpr auto kRelaxed = std::memory_order_relaxed;
    Krate::DSP::SeraphisVoiceParams p{};

    // -- HarmonicCloud (206, 209, 210) ---------------------------------------
    p.cloudDriftSmoothness = cloudParams_.driftSmoothness.load(kRelaxed);
    p.cloudDecaySec = cloudParams_.decaySec.load(kRelaxed);
    p.cloudEnvOffsetSpread = cloudParams_.envOffsetSpread.load(kRelaxed);

    // -- SpectralMorphEngine (401, 403, 404, 407) ----------------------------
    p.morphBloom = morphParams_.bloom.load(kRelaxed);
    p.morphTravelMode = toTravelMode(morphParams_.travelMode.load(kRelaxed));
    p.morphTravelRate = (lastSyncedTravelRate_ >= 0.0f)
                            ? lastSyncedTravelRate_
                            : morphParams_.travelRate.load(kRelaxed);
    p.morphWaypointSeconds = static_cast<double>(morphParams_.waypointSeconds.load(kRelaxed));

    // -- Spatial / life modulators (601, 602, 603) ---------------------------
    p.spatialRateHz = lifeParams_.spatialRateHz.load(kRelaxed);
    p.spatialCoupling = lifeParams_.spatialCoupling.load(kRelaxed);
    p.spatialGrowth = lifeParams_.spatialGrowth.load(kRelaxed);

    // -- Voice envelope (700, 701) -------------------------------------------
    p.envMode = toEnvelopeMode(lifeParams_.envMode.load(kRelaxed));
    p.envGrowthDurationSec = lifeParams_.growthDurationSec.load(kRelaxed);

    // -- ContinuousBody (800, 801, 803-812) ----------------------------------
    p.bodyMaterial = toBodyMaterial(bodyParams_.material.load(kRelaxed));
    // FR-059(b): ID 801 is the ONE class-(b) `VP` row, so the pushed value is
    // the SMOOTHER's current value, not the raw atomic. resonance_ is stored raw
    // inside ContinuousBody (it is absent from that class's ten-smoother list)
    // and read directly at the control step, so an un-smoothed push is a step in
    // the modal gain recompute.
    p.bodyResonance = resonanceSm_.getCurrentValue();
    p.bodyKeyTracking = bodyParams_.keyTracking.load(kRelaxed);
    p.bodyDrive = bodyParams_.drive.load(kRelaxed);
    p.bodyMix = bodyParams_.mix.load(kRelaxed);
    p.bodyCloudMix = bodyParams_.cloudMix.load(kRelaxed);
    p.bodyCloudDecaySec = bodyParams_.cloudDecaySec.load(kRelaxed);
    p.bodyCloudSize = bodyParams_.cloudSize.load(kRelaxed);
    p.bodyCloudDamping = bodyParams_.cloudDamping.load(kRelaxed);
    p.bodyWidth = bodyParams_.width.load(kRelaxed);
    p.bodyInputAgc = bodyParams_.inputAgc.load(kRelaxed);
    p.bodyResonatorBypass = bodyParams_.resonatorBypass.load(kRelaxed);

    // -- AtmosphereEngine (1002, 1003, 1005-1007, 1009-1016) -----------------
    p.atmosDensity = atmosParams_.density.load(kRelaxed);
    p.atmosGrainSeconds = atmosParams_.grainSeconds.load(kRelaxed);
    p.atmosPanSpread = atmosParams_.panSpread.load(kRelaxed);
    p.atmosDecorrelation = atmosParams_.decorrelation.load(kRelaxed);
    p.atmosFreezeMix = atmosParams_.freezeMix.load(kRelaxed);
    p.atmosDriftSmoothness = atmosParams_.driftSmoothness.load(kRelaxed);
    p.atmosDriftRangeSemis = atmosParams_.driftRangeSemitones.load(kRelaxed);
    p.atmosJitter = atmosParams_.jitter.load(kRelaxed);
    p.atmosPositionSeconds = atmosParams_.positionSeconds.load(kRelaxed);
    p.atmosPositionSpread = atmosParams_.positionSpread.load(kRelaxed);
    p.atmosPitchSemitones = atmosParams_.pitchSemitones.load(kRelaxed);
    p.atmosPitchSpread = atmosParams_.pitchSpread.load(kRelaxed);
    p.atmosGrainEnvelope = toGrainEnvelopeType(atmosParams_.grainEnvelope.load(kRelaxed));

    return p;
}

// FR-003. The SINGLE mapping from one of C-6's 27 MB rows to its owning atomic.
// FR-055's "never also through SeraphisVoiceParams" is checkable by construction:
// SeraphisVoiceParams has no field for any of these 27.
float Processor::baseValueForTarget(Krate::DSP::SeraphisMacroTarget target) const noexcept {
    using Target = Krate::DSP::SeraphisMacroTarget;
    constexpr auto kRelaxed = std::memory_order_relaxed;

    switch (target) {
        // -- Voice-owned (19) -------------------------------------------------
        case Target::CloudInharmonicity:  return cloudParams_.inharmonicity.load(kRelaxed);
        case Target::CloudMutation:       return cloudParams_.mutation.load(kRelaxed);
        case Target::CloudSpectralGravity:return cloudParams_.gravity.load(kRelaxed);
        case Target::CloudRichness:       return cloudParams_.richness.load(kRelaxed);
        case Target::CloudSpectralTiltDb: return cloudParams_.tiltDbPerOct.load(kRelaxed);
        case Target::CloudStereoSpread:   return cloudParams_.stereoSpread.load(kRelaxed);
        case Target::CloudAttackTimeSec:  return cloudParams_.attackSec.load(kRelaxed);
        case Target::CloudDriftDepthCents:return cloudParams_.driftDepthCents.load(kRelaxed);
        case Target::MorphEntropy:        return morphParams_.entropy.load(kRelaxed);
        case Target::MorphTargetPosition: return morphParams_.position.load(kRelaxed);
        // FR-059(b): ID 802 is class (b) - damping_ is stored RAW inside
        // ContinuousBody, so the base carries the smoother, not the atomic.
        case Target::BodyDamping:         return bodyDampingSm_.getCurrentValue();
        case Target::AtmosLevel:          return atmosParams_.level.load(kRelaxed);
        case Target::AtmosBlur:           return atmosParams_.blur.load(kRelaxed);
        case Target::AtmosDriftDepth:     return atmosParams_.driftDepth.load(kRelaxed);
        case Target::SpatialDepth:        return lifeParams_.spatialDepth.load(kRelaxed);
        case Target::VoiceWidth:          return lifeParams_.voiceWidthPercent.load(kRelaxed);
        case Target::EnvStage0Ms:         return lifeParams_.stage0Ms.load(kRelaxed);
        case Target::EnvStage1Ms:         return lifeParams_.stage1Ms.load(kRelaxed);
        case Target::EnvReleaseMs:        return lifeParams_.releaseMs.load(kRelaxed);
        // -- Aether-owned (8) -------------------------------------------------
        case Target::AetherMix:           return aetherParams_.mix.load(kRelaxed);
        case Target::AetherSize:          return aetherParams_.size.load(kRelaxed);
        case Target::AetherWidth:         return aetherParams_.width.load(kRelaxed);
        case Target::AetherShimmerOctaveSend:
            return aetherParams_.shimmerOctave.load(kRelaxed);
        case Target::AetherShimmerFifthSend:
            return aetherParams_.shimmerFifth.load(kRelaxed);
        case Target::AetherBloomSend:     return aetherParams_.bloomSend.load(kRelaxed);
        // FR-059(b): IDs 1215 and 1216 are class (b) - both are DIRECT unsmoothed
        // member stores inside AetherReverb that scale a live [-1,+1] modulator,
        // so a depth step is a delay-length step (1215) or a dimensionality step
        // (1216). The base carries the smoother, not the atomic.
        case Target::AetherSizeBreathDepth:
            return breathDepthSm_.getCurrentValue();
        case Target::AetherDimensionalityTideDepth:
            return tideDepthSm_.getCurrentValue();
        case Target::Count:
        default:
            return 0.0f;
    }
}

// FR-042. ON CHANGE ONLY, plus the settling push.
//
// THE THIRD CLAUSE IS NOT REDUNDANT:
// OnePoleSmoother::advanceSamples snaps current_ = target_ on the chunk it
// converges (smoother.h:251-253), at which point isComplete() is ALREADY true
// (:232-234). Without the latch the converging chunk is skipped - the generation
// compare is equal and `settling` is false - and the engine keeps the PREVIOUS
// chunk's value, leaving the voice permanently ~1e-4 short of target until some
// unrelated VP parameter moves.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free.
void Processor::pushVoiceParams() noexcept {
    const bool settling = anyVoiceClassBSmootherUnsettled();
    if (voiceParamGeneration_ == lastAppliedVoiceParamGeneration_ && !settling
        && !wasVoiceClassBSettling_) {
        return;
    }
    engine_->applyVoiceParams(buildVoiceParams());  // FR-002
    lastAppliedVoiceParamGeneration_ = voiceParamGeneration_;
    wasVoiceClassBSettling_ = settling;
    ++applyVoiceParamsCalls_;
}

// FR-043. The five macro knobs, then the 27 MB bases, each ON CHANGE ONLY.
//
// The macro push is its OWN owning push (the macros are route `processor`, not
// `MB`), so it deliberately does NOT increment setTargetBasePushes_ - which is
// what keeps SC-007's setTargetBase row exact at "27 at prepare, +1 for a
// class-(a) MB change".
//
// The base change detection is PER-TARGET. A single global settling flag would
// re-push all 27 targets for one class-(b) MB change, which SC-007's table
// forbids (it asserts Delta = +1 ... +N on ONE target, the other 26 untouched).
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. setMacros and
//      setTargetBase are noexcept scalar stores.
void Processor::pushMacroSurfaces() noexcept {
    // --- the five macro knobs (FR-050) ---------------------------------------
    // The values come from macroSm_, NOT from macroParams_ - see
    // readSmoothedMacros()' banner for why IDs 100-104 are class (b).
    //
    // THE THIRD CLAUSE IS NOT REDUNDANT WITH macrosEqual(), for the same reason
    // pushVoiceParams() carries wasVoiceClassBSettling_: near convergence a
    // one-pole's successive chunk values differ by less than one float ULP and
    // compare EQUAL, so a change-only predicate can go quiet BEFORE the smoother
    // reaches its target and leave the matrix permanently short of it. While any
    // of the five is un-settled the push runs unconditionally, which guarantees
    // the chunk on which advanceSamples snaps current_ = target_
    // (smoother.h:251-253) is delivered.
    //
    // The macro push is its OWN owning push (the macros are route `processor`,
    // not `MB`), so it deliberately does NOT increment setTargetBasePushes_ -
    // which is what keeps SC-007's setTargetBase row exact at "27 at prepare,
    // +1 for a class-(a) MB change".
    const Krate::DSP::SeraphisMacroValues macros = readSmoothedMacros(macroSm_);
    if (!lastPushedMacrosValid_ || !macrosEqual(macros, lastPushedMacros_)
        || anyMacroSmootherUnsettled()) {
        macros_.setMacros(macros);  // seraphis_macro_matrix.h:599
        lastPushedMacros_ = macros;
        lastPushedMacrosValid_ = true;
    }

    // --- the 27 MB bases (FR-003) --------------------------------------------
    for (std::size_t t = 0; t < Krate::DSP::SeraphisMacroMatrix::kNumTargets; ++t) {
        const auto target = static_cast<Krate::DSP::SeraphisMacroTarget>(t);
        const float value = baseValueForTarget(target);
        // PER-TARGET settling (plan 3.5.5), and the macro smoothers are
        // deliberately NOT in this predicate - the macro push above owns them.
        // Exactly ONE target re-pushes per chunk per un-settled class-(b) `MB`
        // row, so SC-007's Delta is 1 ... N_chunk and the other 26 are untouched.
        if (lastPushedBaseValid_ && value == lastPushedBase_[t]
            && !targetClassBUnsettled(target)) {
            continue;  // ON CHANGE ONLY
        }
        macros_.setTargetBase(target, value);
        lastPushedBase_[t] = value;
        ++setTargetBasePushes_;  // INVOCATIONS, for SC-007
    }
    lastPushedBaseValid_ = true;
}

// FR-044. NO settling clause: no AE ID is class (b).
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free.
void Processor::pushAetherParamsIfDirty() noexcept {
    if (aetherParamGeneration_ == lastAppliedAetherParamGeneration_) {
        return;
    }
    applyAetherParams(*reverb_, aetherParams_);  // FR-049
    lastAppliedAetherParamGeneration_ = aetherParamGeneration_;
    ++applyAetherParamsCalls_;
}

// FR-046. On PENDING only, retried once per process() call until every voice has
// accepted, then cleared.
//
// THE RETRY IS PER-VOICE, NEVER WHOLE-POOL. applySpectralStates writes all four
// slots to every voice in the mask, and on a QUIESCENT voice the gate passes and
// SpectralMorphEngine::setState runs isValidSpectralState PLUS buildSanitized - a
// full 64-entry std::log2 pass (spectral_morph_engine.h:296-301, :537-543) -
// BEFORE the identity check at :302-304 that would make it a no-op. With one voice
// held and fifteen idle, a whole-pool retry therefore costs 15 x 4 x 64 ~= 3840
// std::log2 per block, every block, for the whole of a sustained note plus its
// release (up to 8000 ms, seraphis_voice.h:359).
//
// THE PARAMETER ATOMICS ARE NEVER TOUCHED IN RESPONSE TO A REJECTION (FR-046
// clause 4): a rejected write is retried, not rolled back.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. `before` is a
//      16-entry std::array by value on the audio stack, not a heap container.
void Processor::pushSpectralStatesIfPending() noexcept {
    if (!spectralStatesPending_) {
        return;
    }

    constexpr std::size_t kMaxVoices = Krate::DSP::SeraphisEngine::kMaxVoices;
    std::array<std::uint32_t, kMaxVoices> before{};
    for (std::size_t v = 0; v < kMaxVoices; ++v) {
        before[v] = engine_->getVoice(v).getRejectedConfigureTimeCallCount();  // :784
    }

    engine_->applySpectralStates(spectralSlots_.data(),
                                 morphParams_.stateCount.load(std::memory_order_relaxed),
                                 spectralRetryMask_);  // FR-005
    ++applySpectralStatesAttempts_;

    for (std::size_t v = 0; v < kMaxVoices; ++v) {
        const auto bit = static_cast<std::uint16_t>(std::uint16_t{1} << v);
        if ((spectralRetryMask_ & bit) == 0u) {
            continue;
        }
        if (engine_->getVoice(v).getRejectedConfigureTimeCallCount() == before[v]) {
            spectralRetryMask_ = static_cast<std::uint16_t>(spectralRetryMask_ & ~bit);
        }
    }

    if (spectralRetryMask_ == 0u) {  // FR-046 clause 3
        spectralStatesPending_ = false;
        ++applySpectralStatesCalls_;  // SC-007 counts SUCCESSES
    }
    // else: leave the flag set and retry NEXT BLOCK, to the remaining voices only.
}

// FR-041b (plan 3.7 steps 3-4). spectralSlotsConsuming_ is stored BEFORE the
// handoff is cleared; THAT ORDER IS THE WHOLE INTERLOCK - it is what stops the
// message-thread publisher from picking the buffer this copy is reading.
//
// The publisher half (setState -> staging write -> release store) lands with the
// v2 state stream; this consumer is complete and correct on its own, and is a
// no-op until a handoff is published.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. The copy is
//      4 x 540 B of trivially-copyable POD.
void Processor::consumeSpectralSlotHandoff() noexcept {
    const int handoff = spectralSlotsHandoff_.load(std::memory_order_acquire);
    // Negative test first, so the width comparison is size_t vs size_t.
    if (handoff < 0
        || static_cast<std::size_t>(handoff) >= spectralSlotsStaging_.size()) {
        return;
    }
    spectralSlotsConsuming_.store(handoff, std::memory_order_release);
    spectralSlotsHandoff_.store(-1, std::memory_order_release);
    spectralSlots_ = spectralSlotsStaging_[static_cast<std::size_t>(handoff)];
    spectralSlotsConsuming_.store(-1, std::memory_order_release);
    spectralStatesPending_ = true;
    spectralRetryMask_ = 0xFFFFu;
    ++spectralHandoffConsumes_;  // SC-023 clause 5
}

// FR-056 / C-7 / C-3 amendment 2. TEMPO IS SAMPLED ONCE PER process() CALL, never
// per slice: ProcessContext carries one tempo per block by construction, so a
// per-slice read would re-derive the identical value for nothing.
//
// The FALLBACK is stated, not incidental: no context, no valid tempo or a
// non-positive tempo falls back to the free-running kMorphTravelRateId value.
// NEVER silence, never zero, and never a retained stale synced rate.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. One division;
//      beatsPerJourney is never zero by construction (the smallest entry is 0.25,
//      bar-independent), so the division cannot produce a non-finite rate before
//      its clamp.
void Processor::updateSyncedTravelRate(const Vst::ProcessContext* ctx) noexcept {
    using Morph = Krate::DSP::SpectralMorphEngine;

    if (!morphParams_.sync.load(std::memory_order_relaxed)) {
        if (lastSyncedTravelRate_ >= 0.0f) {
            lastSyncedTravelRate_ = -1.0f;
            ++voiceParamGeneration_;  // ID 404 is back in force; re-push it
        }
        return;
    }
    if (ctx == nullptr || (ctx->state & Vst::ProcessContext::kTempoValid) == 0
        || !(ctx->tempo > 0.0)) {
        if (lastSyncedTravelRate_ >= 0.0f) {
            lastSyncedTravelRate_ = -1.0f;
            ++voiceParamGeneration_;
        }
        return;
    }

    // C-7's bar rule, and the ONLY reading of "bar" permitted.
    double barBeats = 4.0;  // common time
    if ((ctx->state & Vst::ProcessContext::kTimeSigValid) != 0 && ctx->timeSigNumerator > 0
        && ctx->timeSigDenominator > 0) {
        barBeats = static_cast<double>(ctx->timeSigNumerator)
                   * (4.0 / static_cast<double>(ctx->timeSigDenominator));
    }
    const std::size_t idx =
        clampSyncNoteIndex(morphParams_.syncNote.load(std::memory_order_relaxed));
    const double beats = kSyncNoteBeats[idx] * (kSyncNoteIsBarDenominated[idx] ? barBeats : 1.0);
    const auto rate = static_cast<float>(std::clamp(ctx->tempo / (60.0 * beats),
                                                    static_cast<double>(Morph::kMinTravelRate),
                                                    static_cast<double>(Morph::kMaxTravelRate)));
    if (lastSyncedTravelRate_ < 0.0f
        || std::fabs(rate - lastSyncedTravelRate_) > kSyncedRateEpsilon) {
        lastSyncedTravelRate_ = rate;
        ++voiceParamGeneration_;  // FR-042 amendment 2
    }
}

// FR-047. THE shared invalidation sequence - one body, two situations. It is PURE
// INVALIDATION and touches no DSP object of its own: that is the only reading that
// is thread-safe, because setState() may legally run concurrently with process()
// and writing ~40 tracker scalars from the message thread would be a data race on
// every one of them. The message thread raises forcePushAllPending_ and nothing
// else; process() consumes it and calls this.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free.
void Processor::pushAllSurfaces(SurfaceInvalidation scope) noexcept {
    // The four slot ids are captured BEFORE the fill below, so the guard at the
    // bottom can tell "a slot actually moved" from "nothing changed".
    const std::array<int, 4> previousSlotIds = lastPushedSlotStateId_;

    ++voiceParamGeneration_;
    ++aetherParamGeneration_;
    lastAppliedVoiceParamGeneration_ = kGenerationSentinel;   // cannot compare equal
    lastAppliedAetherParamGeneration_ = kGenerationSentinel;
    lastPushedBaseValid_ = false;   // forces all 27 setTargetBase pushes
    lastPushedMacros_ = Krate::DSP::SeraphisMacroValues{};
    lastPushedMacrosValid_ = false;
    lastPushedSoftLimitValid_ = false;
    lastPushedFreezeValid_ = false;
    lastSyncedTravelRate_ = -1.0f;
    // A PRESET LOAD SNAPS; it does not ramp. SC-023 clause 4 asserts every
    // route's read-back after ONE block, and a 20 ms class-(b) ramp over a 512-
    // sample block reaches only ~93 % of target - the exact-equality read-back
    // would fail for a correct implementation. Consumed by
    // advanceParamSmoothers(), the first thing the slice loop runs.
    snapParamSmoothers_ = true;
    wasVoiceClassBSettling_ = false;  // the snap makes the settling latch moot
    lastPushedSlotStateId_.fill(-1);  // force the next CFG compare to miss
    lastPushedStateCount_ = -1;

    // --- Reprepared-ONLY invalidations ---------------------------------------
    // engine_->setSeed re-derives all sixteen voice seeds and AetherReverb::setSeed
    // is a documented mid-render drift/tide discontinuity; setPolyphony walks the
    // allocator's excess-slot loop and re-targets sumGain_. On the PresetLoad path
    // setState() wrote both atomics BEFORE the release store, so the ordinary
    // compare in pushGlobalParams() already delivers a changed value and forcing an
    // unchanged one is a discontinuity for nothing.
    if (scope == SurfaceInvalidation::Reprepared) {
        lastPushedSeedIndex_ = -1;
        lastPushedPolyphony_ = kPolyphonySentinel;  // 0: never a legal clamped value
    }

    // --- The spectral fan-out is raised ONLY when a slot actually differs ------
    // Raising it unconditionally costs 16 voices x 4 slots = 64 buildSanitized
    // calls = 4096 std::log2 in ONE process() call, on EVERY setState().
    bool slotsMoved = false;
    for (std::size_t s = 0; s < previousSlotIds.size(); ++s) {
        const int wanted =
            morphParams_.slot[s].load(std::memory_order_relaxed);
        if (wanted != previousSlotIds[s]) {
            slotsMoved = true;
            break;
        }
    }
    if (scope == SurfaceInvalidation::Reprepared || slotsMoved) {
        spectralStatesPending_ = true;  // pushes NOTHING spectral itself
        spectralRetryMask_ = 0xFFFFu;
    }
}

// FR-047. The message thread's ENTIRE contribution to the force-push: one
// release store. Everything it pairs with - the ~40 tracker scalars
// pushAllSurfaces() resets - is written only from the audio thread, which
// consumes this flag at the top of process() with an acquire exchange.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free (it is not
//      on the audio thread either way).
void Processor::requestPushAllSurfaces() noexcept {
    forcePushAllPending_.store(true, std::memory_order_release);
}

// Plan 3.7 step 1. MESSAGE THREAD ONLY.
//
// At most TWO of the three buffers can be excluded - the published one and the
// one being copied - so the bounded three-iteration scan always terminates with
// a free buffer and never waits on the audio thread's progress. That last part
// is what makes it correct when setState() arrives with the audio thread
// STOPPED, where a spin on the handoff flag would never clear.
//
// The two loads are ACQUIRE, and the order (handoff first, then consuming)
// pairs with the consumer's release stores: if `handoff` reads -1, that
// happens-after the consumer's release store of -1, which happens-after its
// release store of `consuming = idx`, so the `consuming` load below sees either
// `idx` (copy in flight, buffer excluded) or -1 (copy finished, buffer safe).
std::size_t Processor::pickStagingBuffer() noexcept {
    const auto count = static_cast<int>(spectralSlotsStaging_.size());
    const int published = spectralSlotsHandoff_.load(std::memory_order_acquire);
    const int consuming = spectralSlotsConsuming_.load(std::memory_order_acquire);

    for (int k = 0; k < count; ++k) {
        const int w = (stagingWriteCursor_ + k) % count;
        if (w != published && w != consuming) {
            return static_cast<std::size_t>(w);
        }
    }
    // Unreachable with three buffers and at most two exclusions; the fallback
    // exists so the function is total rather than falling off the end.
    return static_cast<std::size_t>(((stagingWriteCursor_ % count) + count) % count);
}

// ==============================================================================
// FR-059 clause (b) - the class-(b) smoother machinery (plan 3.5)
// ==============================================================================

// The NINE class-(b) smoothers, in the order plan 3.5.3 lists the IDs:
// 801, 802, 1215, 1216, then the five macro knobs 100-104.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. A
//      fixed-size array of nine pointers, returned BY VALUE - see the banner on
//      the declaration for why the type is pinned.
std::array<Krate::DSP::OnePoleSmoother*, 9> Processor::classBSmoothers() noexcept {
    return {{&resonanceSm_, &bodyDampingSm_, &breathDepthSm_, &tideDepthSm_,
             macroSm_.data(), &macroSm_[1], &macroSm_[2], &macroSm_[3],
             &macroSm_[4]}};
}

// Nine setTarget() calls (smoother.h:170) from the already-clamped atomics. The
// packs denormalize and clamp on the way in, so no clamp is repeated here.
//
// IT DOES NOT CONSUME snapParamSmoothers_. That flag is consumed by
// advanceParamSmoothers(), which runs AFTER this function - the snap has to
// outlive the target set that precedes it, or a preset load would ramp.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free.
void Processor::setParamSmootherTargets() noexcept {
    constexpr auto kRelaxed = std::memory_order_relaxed;

    resonanceSm_.setTarget(bodyParams_.resonance.load(kRelaxed));           // ID 801
    bodyDampingSm_.setTarget(bodyParams_.damping.load(kRelaxed));           // ID 802
    breathDepthSm_.setTarget(aetherParams_.sizeBreathDepth.load(kRelaxed)); // ID 1215
    tideDepthSm_.setTarget(aetherParams_.tideDepth.load(kRelaxed));         // ID 1216

    macroSm_[0].setTarget(macroParams_.dream.load(kRelaxed));     // ID 100
    macroSm_[1].setTarget(macroParams_.bloom.load(kRelaxed));     // ID 101
    macroSm_[2].setTarget(macroParams_.dissolve.load(kRelaxed));  // ID 102
    macroSm_[3].setTarget(macroParams_.gravity.load(kRelaxed));   // ID 103
    macroSm_[4].setTarget(macroParams_.entropy.load(kRelaxed));   // ID 104
}

// Advanced by the SUB-SLICE's own sample count, so the ramp is wall-clock
// correct whatever the host does. advanceSamples() is the O(1) closed form
// (smoother.h:243-256), so advancing by n once equals advancing by n/2 twice up
// to float rounding - which is what makes the delivery partition-independent.
//
// The snap arm serves two callers, and they are deliberately the same code:
// snapParamSmoothers_ (a preset load or a re-prepare, raised by
// pushAllSurfaces()) and paramSmootherBypass_ (FR-059a's probe, SC-005's
// positive control (b)). Both mean "deliver the whole step now".
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free.
void Processor::advanceParamSmoothers(std::size_t sliceSamples) noexcept {
    if (snapParamSmoothers_ || paramSmootherBypass_) {
        for (Krate::DSP::OnePoleSmoother* s : classBSmoothers()) {
            s->snapToTarget();  // smoother.h:257
        }
        snapParamSmoothers_ = false;
        return;
    }
    for (Krate::DSP::OnePoleSmoother* s : classBSmoothers()) {
        s->advanceSamples(sliceSamples);
    }
}

// The ONLY class-(b) `VP` row is ID 801 (plan 3.5.3): the other eight are the
// five macro knobs and three `MB` bases, none of which travels through
// SeraphisVoiceParams.
bool Processor::anyVoiceClassBSmootherUnsettled() const noexcept {
    return !resonanceSm_.isComplete();
}

bool Processor::anyMacroSmootherUnsettled() const noexcept {
    return std::ranges::any_of(
        macroSm_,
        [](const Krate::DSP::OnePoleSmoother& s) noexcept { return !s.isComplete(); });
}

// PER-TARGET, never one flag for all 27 - see pushMacroSurfaces()' banner. The
// other 24 targets are class (a): their component smooths, ramps, gates or
// snapshots the pushed value itself.
bool Processor::targetClassBUnsettled(Krate::DSP::SeraphisMacroTarget target) const noexcept {
    using Target = Krate::DSP::SeraphisMacroTarget;
    switch (target) {
        case Target::BodyDamping:
            return !bodyDampingSm_.isComplete();
        case Target::AetherSizeBreathDepth:
            return !breathDepthSm_.isComplete();
        case Target::AetherDimensionalityTideDepth:
            return !tideDepthSm_.isComplete();
        default:
            return false;
    }
}

// Drives the 64-sample SUBDIVISION only. When every class-(b) smoother is
// settled this is false, the slice loop keeps Phase 8's exact structure, and the
// mechanism costs nothing.
bool Processor::anyClassBSmootherUnsettled() const noexcept {
    return !resonanceSm_.isComplete() || !bodyDampingSm_.isComplete()
           || !breathDepthSm_.isComplete() || !tideDepthSm_.isComplete()
           || anyMacroSmootherUnsettled();
}

}  // namespace Seraphis
