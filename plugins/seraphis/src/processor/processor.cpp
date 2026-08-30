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

// Phase 11 C-5 (plan 6.1, T010). THE ONE HEADER UNDER src/ui/ THE PROCESSOR MAY
// SEE, and it is deliberate rather than a boundary violation: edit_message.h
// declares a POD plus two constexpr strings, includes only <cstdint>, and names
// no VSTGUI type. It is the WIRE FORMAT, shared by both sides - the same
// sanctioned shared-POD exception as processor/cloud_frame.h in the other
// direction.
#include "ui/edit_message.h"

#include "base/source/fstreamer.h"  // IBStreamer (getState/setState)
#include "public.sdk/source/common/memorystream.h"  // kind-8 stream wrap (notify)

#include "pluginterfaces/vst/ivstevents.h"            // IEventList / Vst::Event (FR-025, FR-031)
#include "pluginterfaces/vst/ivstparameterchanges.h"  // IParameterChanges/IParamValueQueue

// Phase 11 FR-011 (plan 5.2): DataExchangeHandler's definition. processor.h
// only forward-declares it, so this is the ONE translation unit that sees it.
#include "public.sdk/source/vst/utility/dataexchange.h"

// Phase 11 C-5 (T010). The DEFINITION site of detail::isNaN (:54) /
// detail::isInf (:175) - the bit-pattern finiteness tests applyEditMessage()
// screens EditMessage::a / ::b with. std::isnan is NOT usable: the macOS leg
// builds with -ffast-math, under which it is optimised away.
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/scoped_denormal_mode.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>  // Phase 10 FR-041 clause 1: the effects-stage scoped timer
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>      // Phase 11 C-2: memset/memcpy of the CloudFrame payload
#include <type_traits>  // std::is_trivially_copyable_v (the CloudFrame memset guard)
#include <utility>      // std::cmp_greater (C++20 mixed-sign integer comparison)
#include <vector>       // kind-8 stream copy (notify, message thread)

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

/// Phase 11.5 Step 0. Close one decomposition timer region: add the elapsed ns
/// since `start` into `slot` and return a fresh start point for the next
/// region. Only ever called behind the processDecompInstrumented_ gate, so no
/// shipping path reaches the clock read (Constitution II).
[[nodiscard]] std::chrono::steady_clock::time_point decompLap(
    double& slot, std::chrono::steady_clock::time_point start) noexcept {
    const auto now = std::chrono::steady_clock::now();
    slot += std::chrono::duration<double, std::nano>(now - start).count();
    return now;
}

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
//   ENG   -> a direct SeraphisEngine setter         ( 5 IDs, own trackers)
//   FX    -> pushEffectsParams() / the send stage   (15 IDs, own trackers)
//   Local -> consumed inside the processor          ( 8 IDs)
enum class Route : std::uint8_t { VP, MB, AE, CFG, ENG, FX, Local };

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

        // --- Effects (1400-1443) --------------------------------------------
        // ID 1400 is ENG and is listed EXPLICITLY, not left to the default arm.
        // Its target is a SeraphisEngine setter, so C-6's Route column says ENG;
        // the default below returns Route::Local, which would classify it wrongly
        // the moment anything distinguishes the two. Today markDirty()'s ENG and
        // Local arms share one `break;`, so the mistake would be invisible - which
        // is exactly why the case is written now rather than when it starts to
        // matter.
        case kFxSaturationId:
            return Route::ENG;
        case kFxDelayMixId:
        case kFxDelayTimeId:
        case kFxDelaySpreadId:
        case kFxDelaySpreadDirectionId:
        case kFxDelayFeedbackId:
        case kFxDelayTiltId:
        case kFxDelayDiffusionId:
        case kFxDelayWidthId:
        case kFxDelaySyncId:
        case kFxDelaySyncNoteId:
        case kFxSpectralFreezeId:
        case kFxWidthId:
        case kFxWanderDepthId:
        case kFxWanderRateId:
        case kFxAzimuthDepthId:
            return Route::FX;

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

// ==============================================================================
// Phase 11 FR-034a (plan 6.4) - the [partials] block
// ==============================================================================
// THE FORMAT VERSION DOES NOT MOVE. kCurrentStateVersion stays 3 (plugin_ids.h:
// 27): this block is APPENDED LAST, after [effects], and read back by the same
// EOF-safe, version-blind loader chain setState() already documents. A v1, a v2
// or a Phase-10 v3 stream simply runs out before it and every override stays
// absent; an older binary reading a Phase-11 stream ignores the tail.
//
// Layout, 272 bytes:
//
//   | Offset | Size | Field                                        |
//   |      0 |  256 | 64 x float pan, index order (writeFloat)     |
//   |    256 |    8 | uint64 panOverrideBits      (writeInt64u)    |
//   |    264 |    8 | uint64 maskBits             (writeInt64u)    |
//
// The 64-BIT ACCESSORS ARE USED BECAUSE THEY EXIST: IBStreamer publicly inherits
// FStreamer (extern/vst3sdk/base/source/fstreamer.h:202), which declares public
// writeInt64u(uint64) / readInt64u(uint64&) at :103-104 - already used in this
// codebase at plugins/disrumpo/src/processor/processor_state.cpp. Splitting each
// mask into four int32s would be unmotivated work: two 64-bit masks are 16 bytes
// either way.
//
// FR-094 (getState -> setState -> getState is byte-identical) survives by
// construction: every field here is a STORED VALUE, never an arithmetic result -
// the same argument Phase 9's [morph] payload uses.
//
// BOTH FUNCTIONS ARE MESSAGE-THREAD ONLY, and neither touches the engine. The
// atomics are the same ones the C-5 edit channel writes (plan 6.2 kinds 2 and 3)
// and the audio thread reads under partialOverridesPending_'s release/acquire
// handshake; `relaxed` is correct here for exactly the same reason it is there.

using PartialPanTable =
    std::array<std::atomic<float>, Krate::DSP::HarmonicCloud::kMaxPartials>;

static_assert(Krate::DSP::HarmonicCloud::kMaxPartials == 64,
              "FR-034a: the [partials] block is 64 pan floats + two 64-bit masks = 272 "
              "bytes; a wider cloud changes the block and needs a spec amendment");

void savePartialOverrides(const PartialPanTable& pan, std::uint64_t panOverrideBits,
                          std::uint64_t maskBits, IBStreamer& streamer) {
    for (const std::atomic<float>& value : pan) {
        streamer.writeFloat(value.load(std::memory_order_relaxed));
    }
    streamer.writeInt64u(static_cast<uint64>(panOverrideBits));
    streamer.writeInt64u(static_cast<uint64>(maskBits));
}

/// @return true only when the WHOLE block was present. A short stream leaves
///         every field it did not reach untouched and returns false, which is
///         not an error: it is what makes a pre-Phase-11 stream load with the
///         overrides absent rather than with garbage.
///
/// The pan array is read BEFORE the two masks on purpose: a PARTIALLY truncated
/// block therefore leaves both masks 0, and every pan value is unreferenced
/// (repushPartialOverrides() only pushes a pan whose panOverrideBits bit is
/// set) - absent, not garbage.
[[nodiscard]] bool loadPartialOverrides(PartialPanTable& pan,
                                        std::atomic<std::uint64_t>& panOverrideBits,
                                        std::atomic<std::uint64_t>& maskBits,
                                        IBStreamer& streamer) {
    for (std::atomic<float>& slot : pan) {
        float value = 0.0f;
        if (!streamer.readFloat(value)) {
            return false;
        }
        // A stream is untrusted input. BIT PATTERN, never std::isnan: the macOS
        // leg is -ffast-math (applyEditMessage() screens EditMessage::a the same
        // way). std::clamp of a NaN would propagate it, and HarmonicCloud::
        // setPartialPosition would then reject the push and leave the two
        // surfaces disagreeing about what was stored.
        if (Krate::DSP::detail::isNaN(value) || Krate::DSP::detail::isInf(value)) {
            value = 0.0f;
        }
        slot.store(std::clamp(value, -1.0f, 1.0f), std::memory_order_relaxed);
    }

    uint64 storedPanBits = 0;
    if (!streamer.readInt64u(storedPanBits)) {
        return false;
    }
    uint64 storedMaskBits = 0;
    if (!streamer.readInt64u(storedMaskBits)) {
        return false;
    }

    panOverrideBits.store(static_cast<std::uint64_t>(storedPanBits), std::memory_order_relaxed);
    maskBits.store(static_cast<std::uint64_t>(storedMaskBits), std::memory_order_relaxed);
    return true;
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

    // Phase 11 plan 6.2a. The MESSAGE-THREAD mirror starts on exactly the same
    // four states, so a kind-7 BlendBegin or a kind-1 edit that arrives before
    // the host has ever moved a dropdown authors from the registered defaults
    // rather than from a default-constructed (all-zero) payload. The companion
    // tracker records what each entry was seeded from, so
    // syncAuthoringMirrorFromDropdowns() does not re-seed until a dropdown
    // actually moves and silently discard an edit.
    for (std::size_t s = 0; s < spectralSlotsAuthoring_.size(); ++s) {
        spectralSlotsAuthoring_[s] = spectralSlots_[s];
        lastAuthoredSlotStateId_[s] = kMorphSlotDefaultIndices[s];
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

    // KNOWN RESIDUAL (plan §8.3), recorded rather than hidden: when the composed
    // amount below is 0, this push RAMPS instead of snapping. SeraphisEngine::
    // prepare() sets satL_/satR_.setSaturation(kOutputSaturation) BEFORE
    // satL_.prepare() precisely so the saturator's smoothers are snapped
    // (seraphis_engine.h:225-231); this setter necessarily runs AFTER prepare()
    // - calling it before is useless, because prepare() re-applies
    // kOutputSaturation unconditionally - and post-prepare
    // TapeSaturator::setSaturation takes the ramping branch
    // (tape_saturator.h:248-252). Effect: the first kDefaultSmoothingMs = 5.0f
    // of the render carries a decaying <= 0.15 tanh/linear blend that should
    // have been 0. Removing it requires threading outputSaturation through
    // SeraphisEngineConfig - a dsp/ change the phase scope forbids.
    //
    // PHASE 10 D-2 / FR-021: WRITER 1 OF THE SINGLE WRITER, in the SAME COMPOSED
    // FORM pushEffectsParams() uses - ID 2 (kSoftLimitId) is the GATE, ID 1400
    // (kFxSaturationId) is the AMOUNT that gate passes. Pushing the literal
    // kOutputSaturation here instead would install 0.15 on a prepare whose
    // ID 1400 says 0.8, and the render would only converge on the first
    // process(). At the C-6 defaults the two forms are bit-identical: soft is
    // true and kFxSaturationDefault IS SeraphisEngine::kOutputSaturation
    // (effects_params.h:104).
    const float saturationAmount =
        lastPushedSoftLimit_ ? effectsParams_.saturation.load(std::memory_order_relaxed)
                             : 0.0f;
    engine_->setOutputSaturation(saturationAmount);
    // FR-045's shape: the VALUE is seeded, the CADENCE COUNTER is not. A
    // re-prepare really did re-initialise the saturator, so leaving the validity
    // flag false makes the first process() after every prepare push exactly once
    // and count it once - the cadence engSoftLimitPushCountForTest() (and
    // SC-007's ENG clause) is asserted against.
    lastPushedSaturation_ = saturationAmount;
    lastPushedSaturationValid_ = false;

    // 5. FR-028: scratch sized ONCE, to the constant - never to the host block.
    dryL_.assign(bound, 0.0f);
    dryR_.assign(bound, 0.0f);
    wetL_.assign(bound, 0.0f);
    wetR_.assign(bound, 0.0f);

    // 5b. Phase 10 FR-041 clause 6 / FR-028. The pre-output-stage tap takes the
    //     SAME constant bound - deliberately NOT setup.maxSamplesPerBlock. A host
    //     block larger than 2048 is legal (the slice loop's own cap at :832 is the
    //     branch it enters), and the tap then holds the FIRST 2048 samples of the
    //     block with preOutTapTruncated_ raised, so a criterion that measured half
    //     a render fails loudly instead of silently.
    preOutTapL_.assign(bound, 0.0f);
    preOutTapR_.assign(bound, 0.0f);
    preOutTapCursor_ = 0;
    preOutTapSize_ = 0;
    preOutTapTruncated_ = false;

    // 5c. Phase 10 C-1 STEP 4 - THE SEND. The three setters run BEFORE
    //     prepare(), and THAT ORDER IS LOAD-BEARING, not stylistic: prepare()
    //     ends in snapParameters() (spectral_delay.h:206, :550), whereas a
    //     post-prepare setDryWetMix() only sets a smoother TARGET (:500-503)
    //     which is advanced exactly ONCE PER process() CALL (:373, :389) despite
    //     being configured with a per-sample 50 ms coefficient (:184-194). Pushed
    //     after prepare it would therefore creep from kDefaultDryWet = 0.5f
    //     (:109) toward 1.0 by ~0.04 % of the remaining distance per call - tens
    //     of seconds of un-aligned CURRENT-BLOCK dry leaking into the bus, which
    //     is the exact comb C-2 exists to prevent (FR-004). Pushed before,
    //     prepare()'s own setTarget (:202) plus snapParameters() snap it to 1.0.
    //     This is the same hazard seraphis_engine.h:331-333 already records for
    //     the saturator.
    spectralDelay_.setFFTSize(Krate::DSP::SpectralDelay::kDefaultFFTSize);  // :408
    spectralDelay_.setDryWetMix(1.0f);                                      // :500
    spectralDelay_.setSpreadCurve(Krate::DSP::SpreadCurve::Logarithmic);    // :448
    spectralDelay_.prepare(sampleRate_, bound);                             // :131

    // 5d. FR-027. THE SEND IS NOT DETERMINISTIC AS PREPARED: its RNG is seeded
    //     from reinterpret_cast<uintptr_t>(this) ^ sampleRate
    //     (spectral_delay.h:223-224), i.e. from an ASLR-dependent address, and
    //     reset() re-draws 2 x numBins stereo phases from it (:279-284). The
    //     shipped kSeedValues table is the ONLY seed source, exactly as it is for
    //     engine_/reverb_.
    //
    //     seedRng THEN reset, which is the order the header itself documents
    //     (:295-296). The reason is NOT that the reverse order leaves stale
    //     ASLR-seeded phases - it does not, seedRng() re-draws them itself
    //     (:297-304) - it is that reset() then re-draws them AGAIN from the
    //     freshly seeded stream, making the post-prepare state a pure function of
    //     the seed. One order, stated once, so pushEffectsParams()' seed-change
    //     burst can be literally the same two calls.
    const std::size_t fxSeedIndex =
        clampSeedIndex(globalParams_.seedIndex.load(std::memory_order_relaxed));
    spectralDelay_.seedRng(kSeedValues[fxSeedIndex]);  // :297
    spectralDelay_.reset();                            // :242
    lastPushedFxSeedIndex_ = static_cast<int>(fxSeedIndex);

    // 5e. The send accumulator (C-2 clause 5, plan section 3.1). assign(), NEVER
    //     resize(): the FIFOs must start ZEROED, because the output side is read
    //     one whole chunk ahead of the first chunk the component ever produces
    //     and resize() would leave whatever the previous prepare left behind in
    //     the low indices of a grown vector.
    fxInL_.assign(kFxFifoCapacity, 0.0f);
    fxInR_.assign(kFxFifoCapacity, 0.0f);
    fxOutL_.assign(kFxFifoCapacity, 0.0f);
    fxOutR_.assign(kFxFifoCapacity, 0.0f);
    fxChunkL_.assign(kFxSendChunkSamples, 0.0f);
    fxChunkR_.assign(kFxSendChunkSamples, 0.0f);

    // FR-009a's window, in samples at THIS rate - hence a member and not a
    // constant. std::llround, not a truncating cast: the window is a wall-clock
    // quantity and 2000 ms at 44 100 Hz is 88 200 exactly only in real
    // arithmetic.
    fxSendDrainSamples_ = static_cast<std::int64_t>(
        std::llround(static_cast<double>(kFxSendDrainMs) * 0.001 * sampleRate_));

    fxSendState_ = FxSendState::Bypassed;
    fxBypassedSamples_ = 0;
    fxLiveSamplesSinceEngage_ = 0;
    fxDrainRemaining_ = 0;
    fxResetDue_ = false;
    fxFifoClearDue_ = false;
    // DELIBERATELY ABOVE kFxSendDrainFloor. A drain that has not yet run a chunk
    // must never take the energy exit, which would annihilate the tail FR-008 and
    // FR-009a exist to preserve.
    fxDrainPeak_ = 1.0f;
    fxEffectiveReturnGain_ = 0.0f;
    fxSendRuns_ = false;

    // THE one establishing point of the section 3.1 invariant - shared verbatim
    // with setActive(false) and the single deferred mid-render site.
    clearFifos();

    // 5f. Phase 10 C-1 STEP 5 - the wander (FR-006, FR-011, FR-024, FR-024a).
    //
    //     The two drift SOURCES advance on EVERY block whatever the bypass
    //     state (FR-011), so a re-engaged wander continues a walk that was
    //     conceptually running instead of restarting one - on the absolute
    //     64-sample control grid from inside runWanderStage() while the stage
    //     runs, and by the whole call from the pre-slice block while it does not.
    //
    //     setMean(0.0f) is pushed EXPLICITLY (brownian_drift.h:165): the wander
    //     is a BIPOLAR excursion around identity, and a non-zero mean would bias
    //     the stereo image permanently rather than let it wander back.
    //
    //     The two seeds are the ONE shipped kSeedValues entry XORed with TWO
    //     DISTINCT salts (C-5 / FR-024a clause 3). Identical salts would make
    //     width and azimuth walk in lockstep off one stream, which reads as a
    //     single moving object rather than as two independent ones.
    widthDrift_.prepare(sampleRate_);    // brownian_drift.h:121
    azimuthDrift_.prepare(sampleRate_);
    widthDrift_.setMean(0.0f);           // :165
    azimuthDrift_.setMean(0.0f);
    widthDrift_.setSeed(kSeedValues[fxSeedIndex] ^ kFxWidthDriftSalt);      // :145
    azimuthDrift_.setSeed(kSeedValues[fxSeedIndex] ^ kFxAzimuthDriftSalt);
    widthDrift_.reset();                 // :133
    azimuthDrift_.reset();

    //     The GLOBAL width stage. setWidth() BEFORE reset(), because reset()
    //     SNAPS the width smoother onto width_ (midside_processor.h:114-120) -
    //     the same "setters run before the snap" rule C-2 clause 1 states for
    //     the send and seraphis_engine.h:331-333 records for the saturator. The
    //     other four smoothers reset() snaps (mid gain, side gain, both solos)
    //     are Seraphis-untouched, so those snaps are no-ops.
    globalMs_.prepare(static_cast<float>(sampleRate_), kMaxBlockSamples);  // :96
    globalMs_.setWidth(effectsParams_.width.load(std::memory_order_relaxed));  // :133
    globalMs_.reset();                                                     // :114

    //     C-5's azimuth pan pair, at kParamSmoothMs and SNAPPED to unity. They
    //     are NOT in classBSmoothers() - runWanderStage()'s per-sample
    //     .process() is their sole advance - so the snap here is what makes
    //     wanderAtIdentity() true from the very first block at the C-6 defaults,
    //     which is in turn what keeps SC-002's skip bit-exact.
    azimuthGainLSm_.configure(kParamSmoothMs, static_cast<float>(sampleRate_));
    azimuthGainRSm_.configure(kParamSmoothMs, static_cast<float>(sampleRate_));
    azimuthGainLSm_.snapTo(1.0f);
    azimuthGainRSm_.snapTo(1.0f);

    //     FR-010a's disengage window: THREE TIME CONSTANTS of the SLOWER of the
    //     two smoothers the stage owns - MidSideProcessor's own
    //     kDefaultSmoothingMs (10 ms, midside_processor.h:73), which prepare()
    //     above configured, and kParamSmoothMs (20 ms) on the azimuth pair and
    //     on the two depth smoothers. Three constants is 95 % of the step for a
    //     one-pole, and the smoothers' own isComplete() closes the remainder -
    //     this countdown exists only because MidSideProcessor has no equivalent
    //     query and the Non-goals forbid adding one.
    {
        const double slowestMs =
            std::max(static_cast<double>(Krate::DSP::MidSideProcessor::kDefaultSmoothingMs),
                     static_cast<double>(kParamSmoothMs));
        fxWanderSettleSamples_ =
            static_cast<std::int64_t>(std::llround(slowestMs * 3.0 * 0.001 * sampleRate_));
    }
    fxWanderSettleRemaining_ = 0;
    fxWanderRuns_ = false;
    fxWanderRunsEffective_ = false;
    fxWanderWasActive_ = false;
    fxWidthBase_ = effectsParams_.width.load(std::memory_order_relaxed);
    wanderControlUpdates_ = 0;

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

        // Phase 10 FR-038b clause 2's THREE. Configured HERE, beside Phase 9's
        // nine, for the same stated reason: the set cannot drift apart and a
        // sample-rate change re-derives every coefficient. ID 1410 takes
        // kFxReturnRampMs, which IS kParamSmoothMs by construction (the
        // static_assert beside it in processor.h) - FR-038b clause 2 gives ID
        // 1410 ONE smoother, not a class-(b) smoother plus a second private
        // engage ramp that would fight it.
        fxReturnGainSm_.configure(kFxReturnRampMs, sr);   // ID 1410
        fxWanderDepthSm_.configure(kParamSmoothMs, sr);   // ID 1441
        fxAzimuthDepthSm_.configure(kParamSmoothMs, sr);  // ID 1443
        // The send is Bypassed at prepare, so its return gain starts at silence
        // and the first engage RAMPS from 0 rather than stepping in at target.
        fxReturnGainSm_.snapTo(0.0f);
    }
    controlPhase_ = 0;

    // Phase 11 C-2 (plan 5.1's padding discipline). The frame is zeroed ONCE,
    // HERE, and only field-assigned thereafter, so CloudFrame's 4 interior
    // padding bytes are deterministically zero in every block that crosses the
    // process boundary by memcpy - and two frames may therefore be memcmp'd.
    //
    // THE `void*` CAST IS DELIBERATE AND MUST STAY. CloudFrame carries default
    // member initializers, which makes its default constructor non-trivial, and
    // GCC's -Wclass-memaccess fires on the implicit CloudFrame* -> void*
    // conversion, suggesting "assignment or value-initialization instead". That
    // suggestion is wrong HERE: `pendingFrame_ = CloudFrame{}` leaves the 4
    // interior padding bytes indeterminate, which is precisely what this line
    // exists to prevent. The type IS trivially copyable (asserted below), so the
    // memset is well-defined; the explicit cast says "yes, on purpose".
    static_assert(std::is_trivially_copyable_v<CloudFrame>,
                  "CloudFrame is memset and memcpy'd across the process boundary");
    std::memset(static_cast<void*>(&pendingFrame_), 0, sizeof(CloudFrame));
    cloudFrameFocusVoice_ = 0;

    // Phase 11 FR-030 / FR-043 (plan 6.3, T011). CALL SITE 6 of six:
    // setupProcessing RE-ENTRY, i.e. a sample-rate or block-size change. Step 3b
    // above reached engine_->prepare(), which reaches HarmonicCloud::reset() and
    // therefore cleared BOTH override halves (harmonic_cloud.h:331-332). The
    // authored table is a UI-owned quantity that must outlive a re-prepare -
    // FR-043's only criterion - so it is pushed back HERE, at the end, after
    // every component has been prepared.
    //
    // HOST THREAD, WITH THE AUDIO THREAD STOPPED - the same per-site exception
    // setActive's branch takes.
    //
    // The composed-spread tracker is SEEDED HERE with what prepare() actually
    // left in the cloud (SeraphisVoice::prepare pushes its own component default),
    // and NOT invalidated. Invalidating it would arm the tracker on the first
    // slice WITHOUT re-pushing, so a configuration whose composed spread differs
    // from the prepared default would clear the table on that very first apply()
    // and never restore it. Seeding makes the first slice fire if and only if
    // apply() genuinely moved the value - which is exactly the clearing event.
    repushPartialOverrides();
    lastPushedComposedSpread_ = engine_->getVoice(0).cloud().getStereoSpread();  // :549
    lastPushedSpreadVoiceCount_ = engine_->getPolyphony();                       // :665
    lastPushedComposedSpreadValid_ = true;

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
        // Re-arm the FR-024a clause 3 seam, so the first process() after
        // re-activation SNAPS the master-gain smoother to the current parameter
        // instead of ramping from the pre-deactivation value.
        anySamplesSincePrepare_ = false;

        // Phase 11 FR-011 (plan 5.2). Open the DataExchange queue.
        //
        // AMENDED COMMENT (Phase 11, plan 5.2). This branch used to say
        // "Activation does exactly ONE thing, and it allocates nothing (SC-026
        // clause 2)". That is no longer true and the honest statement is:
        // ACTIVATION ALLOCATES ONLY IN THE DataExchange QUEUE-OPEN PATH BELOW,
        // on the HOST thread, inside the window the deactivate branch's own
        // banner already relies on. In the SDK's fallback path (a host with no
        // IDataExchangeHandler) onActivate -> Impl::openQueue does make_unique +
        // Timer::create + aligned_alloc x numBlocks + allocateMessage
        // (extern/vst3sdk/public.sdk/source/vst/utility/dataexchange.cpp:76-105).
        // NO AUDIO-THREAD-REACHABLE PATH GAINS AN ALLOCATION - which is what
        // SC-026 clause 2 is narrowed to, and why
        // Seraphis_SetActiveDoesNotAllocate (tests/unit/lifecycle_test.cpp)
        // keeps its exact `== 0u` form: it measures a DISCONNECTED instance,
        // i.e. exactly the audio-thread-reachable configuration.
        if (dataExchangeHandler_ != nullptr) {
            Vst::ProcessSetup setup{};
            setup.processMode = Vst::kRealtime;
            setup.symbolicSampleSize = Vst::kSample32;
            // kMaxBlockSamples, not the host's negotiated block: it is the SAME
            // constant setupProcessing() prepares both components with
            // (seraphis_engine_config.h:40), and the queue's own sizing does not
            // depend on it at all - only blockSize/numBlocks from the config
            // callback do.
            setup.maxSamplesPerBlock = static_cast<int32>(kMaxBlockSamples);
            setup.sampleRate = sampleRate_;
            dataExchangeHandler_->onActivate(setup);
        }
    } else {
        // Phase 11 FR-011. Close the queue before the tails are cleared below,
        // so no block is handed out across the deactivated window.
        if (dataExchangeHandler_ != nullptr) {
            dataExchangeHandler_->onDeactivate();
        }

        // Deactivation must leave no ringing tail (SC-026 clause 1).
        // SeraphisEngine::silence() is documented NOT an audio-thread operation
        // (seraphis_engine.h:306-307; ~32 MiB of capture-ring clearing) - which
        // is correct HERE and only here.
        if (engine_ != nullptr) {
            engine_->silence();

            // Phase 11 FR-030 (plan 6.3, T011). CALL SITE 4 of six: an engine
            // RESET. silence() runs voices_[v].reset() on every slot
            // (seraphis_engine.h:414-420), and HarmonicCloud::reset() does
            // positionOverridden_.fill(false); masked_.fill(false)
            // (harmonic_cloud.h:331-332) - it clears BOTH halves of the table.
            //
            // HOST THREAD, WITH THE AUDIO THREAD STOPPED, which is what makes it
            // legal here and what makes the exception PER-SITE: any
            // message-thread path must publish partialOverridesPending_ and let
            // process() do the fan-out instead.
            repushPartialOverrides();
        }
        if (reverb_ != nullptr) {
            // Dereferenced form, not `reverb_->reset()`: the arrow spelling is
            // ambiguous with unique_ptr's OWN reset() (which would destroy the
            // reverb here), and clang-tidy flags it for exactly that reason
            // (readability-ambiguous-smartptr-reset-call). This clears the
            // reverb's tail - aether_reverb.h:1971 - and keeps the object.
            (*reverb_).reset();
        }

        // Phase 10 FR-035. The send is the third tail on this bus and it is the
        // longest-lived of the three: at kFxDelayFeedbackMax the per-bin
        // recursion decays over seconds, and its accumulator would additionally
        // hold up to one whole chunk of the pre-deactivation render. Clearing the
        // component WITHOUT clearing the FIFOs would leave that chunk to be
        // played out on re-activation, so the two go together - and the state
        // re-initialisation goes with them, because a send left "Draining" across
        // a deactivation would resume draining audio that no longer exists.
        //
        // spectralDelay_ is a VALUE member, so unlike engine_/reverb_ there is no
        // null check to make here.
        //
        // seedRng THEN reset - the SAME two calls, in the SAME order, that
        // setupProcessing() step 5d (:631-634) and pushEffectsParams()' seed burst
        // (:1619-1624) make, and the order the header itself documents (:295-296).
        // reset() ALONE would not restore the post-prepare state: it does not
        // rewind the RNG, it CONSUMES the next 2 x numBins draws to re-randomize
        // the stereo phase walk (:279-284) off a stream the pre-deactivation
        // render has already advanced by a further 2 x numBins per frame
        // (:704-708). Those phases reach the output at any stereo width above
        // 0.001 (:864-870) - and the C-6 default is 0.5 - so a send re-activated
        // after reset() alone would decorrelate with DIFFERENT phases than a fresh
        // prepare, which is precisely the "same state a fresh setupProcessing()
        // would leave" that FR-035 requires. Re-seeding first makes the
        // re-activated state a pure function of the seed again.
        //
        // spectralDelayResets_ is deliberately NOT incremented: FR-041 clause 2
        // counts the FR-008 idle-reset and the seed-change burst, both of which are
        // audio-thread events a cadence test observes across a render. A
        // deactivation is neither.
        const std::size_t fxDeactivateSeedIndex =
            clampSeedIndex(globalParams_.seedIndex.load(std::memory_order_relaxed));
        spectralDelay_.seedRng(kSeedValues[fxDeactivateSeedIndex]);  // :297
        spectralDelay_.reset();  // spectral_delay.h:242 - allocation-free
        lastPushedFxSeedIndex_ = static_cast<int>(fxDeactivateSeedIndex);

        // ...and the SEVEN PARAMETER SMOOTHERS, which reset() likewise does not
        // touch (it clears the STFT, the accumulator and the per-bin delay lines,
        // :242-291, and nothing else). They are the second half of the same FR-035
        // defect: every registered row reaches the component as a smoother TARGET
        // only (:425-512), advanced ONCE PER SPECTRAL FRAME (:691-696) off a
        // per-sample 50 ms coefficient (:184-194) - a ~5.1 s time constant at the
        // 1024-point default - so the pre-deactivation render leaves them part-way
        // along a ramp that a fresh setupProcessing() has never started.
        //
        // The state a fresh setupProcessing() DOES leave is the one prepare()'s own
        // setTarget-then-snapParameters() pair produces (:197-206), and at that
        // moment the only rows anything has pushed are the three C-2/C-7 pre-prepare
        // ones. Every registered row is therefore snapped to the component's
        // CONSTRUCTION default (:936-944: 250 ms, 0 spread, 0 feedback, 0 tilt,
        // 0 diffusion, 0 width) with dry/wet at the 1.0 FR-004 pushes before
        // prepare - which is exactly what the seven calls below reinstate.
        //
        // These are deliberately the CTOR defaults and NOT the registered values: a
        // snap to the registered set would leave the send running parameters a
        // freshly prepared one is still ramping toward, i.e. a DIFFERENT state, and
        // FR-035's contract is equality with the prepare, not an improvement on it.
        // (The prepare-time ramp-in itself is pre-existing Phase 10 behaviour that
        // SC-019's 22 s settle window and SC-008's click bound are both calibrated
        // against; changing it is not this path's business.)
        spectralDelay_.setBaseDelayMs(Krate::DSP::SpectralDelay::kDefaultDelayMs);  // :425
        spectralDelay_.setSpreadMs(0.0f);                                           // :432
        spectralDelay_.setFeedback(0.0f);                                           // :460
        spectralDelay_.setFeedbackTilt(0.0f);                                       // :468
        spectralDelay_.setDiffusion(0.0f);                                          // :489
        spectralDelay_.setStereoWidth(0.0f);                                        // :512
        // The seventh smoother. Its target is ALREADY 1.0 (FR-004 pushes it before
        // prepare and no parameter can reach it, C-2 clause 1), so this is a
        // restatement rather than a correction - written out so the list covers all
        // seven and a future push cannot silently leave one behind.
        spectralDelay_.setDryWetMix(1.0f);  // :500
        spectralDelay_.snapParameters();    // :595 - the setTarget/snap pair, as prepare()

        // ...and therefore the trackers, or the next process() would see "nothing
        // changed" against the pre-deactivation values and leave every target at the
        // ctor default just installed. Raising the first-push flag makes that call
        // deliver the WHOLE registered set - targets only, exactly as the first
        // process() after a prepare does.
        lastPushedFxValid_ = false;
        clearFifos();            // plan section 3.1 - restores the one-chunk pre-fill
        fxSendState_ = FxSendState::Bypassed;
        fxBypassedSamples_ = 0;
        fxLiveSamplesSinceEngage_ = 0;
        fxDrainRemaining_ = 0;
        fxResetDue_ = false;
        fxFifoClearDue_ = false;
        fxDrainPeak_ = 1.0f;  // above kFxSendDrainFloor - see setupProcessing()
        fxEffectiveReturnGain_ = 0.0f;
        fxSendRuns_ = false;
        // FR-035's RETURN-GAIN RAMP clause. snapTo, not setTarget: a deactivated
        // send holds nothing, so re-activation must RAMP UP from silence rather
        // than resume mid-ramp at whatever gain the pre-deactivation render had
        // reached. Pairing it with fxEffectiveReturnGain_ = 0.0f keeps the
        // smoother and the value setParamSmootherTargets() will re-target it to
        // consistent across the deactivated window.
        fxReturnGainSm_.snapTo(0.0f);

        // FR-011's two drift sources. They advance every block regardless of
        // bypass, so they are genuinely running state and a deactivation must
        // clear them like any other (brownian_drift.h:133). Their SEEDS are not
        // re-pushed here: setupProcessing() owns that, and re-activation without
        // a re-prepare must not silently re-phase a deterministic walk.
        widthDrift_.reset();
        azimuthDrift_.reset();

        // FR-035's WANDER clause. The width smoother and the azimuth pair are
        // running state exactly as the drifts are, and a deactivated bus holds
        // nothing - so both SNAP back to identity rather than resuming a ramp
        // from wherever the pre-deactivation render left them. The latch state
        // goes with them: a stage left "effective" across a deactivation would
        // run its disengage tail against audio that no longer exists, and
        // fxWanderWasActive_ left true would skip FR-010a's ENGAGE snap on the
        // first re-activated block that engages.
        //
        // IDENTITY, not the current ID-1440 atomic: a deactivated stage renders
        // nothing, so the state it must be restored to is the one the FR-010
        // skip leaves the bus in - unity width, unity gains. FR-010a's ENGAGE
        // arm makes the same choice for the same reason, and the plan's FR-035
        // row asserts exactly `globalMs_.getWidth() == kDefaultWidth`.
        globalMs_.setWidth(Krate::DSP::MidSideProcessor::kDefaultWidth);
        globalMs_.reset();  // midside_processor.h:114 - snaps every smoother
        azimuthGainLSm_.snapTo(1.0f);
        azimuthGainRSm_.snapTo(1.0f);
        fxWanderSettleRemaining_ = 0;
        fxWanderRuns_ = false;
        fxWanderRunsEffective_ = false;
        fxWanderWasActive_ = false;
        wanderControlUpdates_ = 0;
    }
    return AudioEffect::setActive(state);
}

// ==============================================================================
// Phase 11 FR-011 (plan 5.2) - the DataExchange lifecycle.
// ==============================================================================
// Shape copied verbatim from Membrum
// (plugins/membrum/src/processor/processor.cpp:1136-1164). The handler is built
// here and released in disconnect(); setActive() opens and closes the queue.
//
// A CONNECTED instance is the ONLY configuration in which any of this runs: the
// headless ProcessorFixture never calls connect(), so dataExchangeHandler_ stays
// null through every plugin-side test and publishCloudFrame() takes its
// skipped-block accounting instead. That is by design, not an accident - see
// publishCloudFrame()'s body-order banner.
tresult PLUGIN_API Processor::connect(Vst::IConnectionPoint* other) {
    const tresult result = AudioEffect::connect(other);
    if (result == kResultTrue) {
        auto configCallback = [](Vst::DataExchangeHandler::Config& config,
                                 const Vst::ProcessSetup& /*setup*/) {
            config.blockSize = static_cast<uint32>(sizeof(CloudFrame));
            config.numBlocks = 4;
            config.alignment = 32;
            config.userContextID = kCloudFrameUserContextId;
            return true;
        };
        dataExchangeHandler_ =
            std::make_unique<Vst::DataExchangeHandler>(this, configCallback);
        dataExchangeHandler_->onConnect(other, getHostContext());
    }
    return result;
}

tresult PLUGIN_API Processor::disconnect(Vst::IConnectionPoint* other) {
    if (dataExchangeHandler_ != nullptr) {
        dataExchangeHandler_->onDisconnect(other);
        // RELEASED, not merely idled: dataExchangeHandlerLiveForTest() must go
        // false and publishCloudFrame() must resume its skipped-block accounting
        // (SC-006 arm (i)).
        dataExchangeHandler_.reset();
    }
    return AudioEffect::disconnect(other);
}

// ==============================================================================
// Phase 11 C-5 / FR-036 (plan 6.2) - the edit channel's entry point.
// ==============================================================================
// MESSAGE THREAD. Exactly one message ID is understood; everything else - a null
// message, a null ID, another plugin-defined ID - delegates to the SDK base,
// which is what keeps any future channel working.
//
// A message is UNTRUSTED INPUT. A malformed payload (no such attribute, a null
// pointer, a size that is not exactly sizeof(EditMessage)) is DROPPED SILENTLY
// with kResultOk: the sender is the plugin's own controller, so there is no
// caller to report to and no recovery to perform, and returning an error would
// have hosts log a fault for a message the plugin itself chose to ignore.
//
// The memcpy into a local is not decorative: `data` points into the host's
// attribute list with no alignment guarantee for a struct, and reading through a
// reinterpret_cast<const EditMessage*> would be UB on a strict-alignment target.
tresult PLUGIN_API Processor::notify(Vst::IMessage* message) {
    if (message == nullptr) {
        return AudioEffect::notify(message);
    }
    const char* id = message->getMessageID();
    if (id == nullptr || std::strcmp(id, UI::kSeraphisEditMessageId) != 0) {
        return AudioEffect::notify(message);
    }

    Vst::IAttributeList* attributes = message->getAttributes();
    if (attributes == nullptr) {
        return kResultOk;  // malformed -> dropped
    }

    const void* data = nullptr;
    uint32 size = 0;
    if (attributes->getBinary(UI::kSeraphisEditAttributeId, data, size) != kResultOk
        || data == nullptr || size != static_cast<uint32>(sizeof(UI::EditMessage))) {
        return kResultOk;  // malformed -> dropped (C-5 clause 5)
    }

    UI::EditMessage edit{};
    std::memcpy(&edit, data, sizeof(edit));

    // Kind 8 (Phase 12 hotfix, 2026-08-05): the preset browser's load path. The
    // stream rides a SECOND attribute, and it is applied by setState() - which
    // runs on this very thread on project load, so every rule that function
    // already enforces (EOF-safe chain, staging-ring publish, authoring-mirror
    // tracker re-arm, force-push, [partials] bits) applies verbatim. A kind-8
    // message without the attribute, or with an implausible size, is DROPPED
    // like every other malformed message; applyEditMessage()'s own switch has
    // no case 8, so the POD-only path cannot reach state either.
    if (edit.kind == UI::kEditKindPresetState) {
        const void* stateData = nullptr;
        uint32 stateSize = 0;
        if (attributes->getBinary(UI::kSeraphisStateAttributeId, stateData, stateSize)
                != kResultOk
            || stateData == nullptr || stateSize < sizeof(int32)
            || stateSize > UI::kMaxPresetStateBytes) {
            return kResultOk;  // malformed -> dropped
        }
        // Copy out of the host's attribute memory: MemoryStream has no const
        // view, and this is the message thread, where a 2.8 KB copy is free.
        std::vector<char> bytes(static_cast<std::size_t>(stateSize));
        std::memcpy(bytes.data(), stateData, bytes.size());
        Steinberg::MemoryStream stream(bytes.data(),
                                       static_cast<Steinberg::TSize>(stateSize));
        (void)setState(&stream);
        return kResultOk;
    }

    applyEditMessage(edit);
    return kResultOk;
}

tresult PLUGIN_API Processor::process(Vst::ProcessData& data) {
    // FR-029. FTZ/DAZ is PER-THREAD, so it must be armed here, on the audio
    // thread, and not in setupProcessing() (plugins/membrum/src/processor/
    // processor.cpp:1073-1075). core/scoped_denormal_mode.h:60.
    const Krate::DSP::ScopedDenormalMode denormalGuard;

    // Phase 11.5 Step 0. FALSE on every shipping path; when armed by a test the
    // per-region laps below attribute this call's wall time to DecompStage rows.
    const bool decomp = processDecompInstrumented_;
    std::chrono::steady_clock::time_point decompT{};
    if (decomp) {
        decompT = std::chrono::steady_clock::now();
    }

    // Automation is latched BEFORE the shape guards: a block that renders
    // nothing must still not lose the host's parameter changes.
    processParameterChanges(data.inputParameterChanges);
    if (decomp) {
        decompT = decompLap(decompNs_[static_cast<std::size_t>(DecompStage::Params)], decompT);
        ++decompProcessCalls_;
    }

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

    // Phase 11 FR-030 (plan 6.3, T011). CALL SITE 1 of six: an edit arrived on
    // the message thread. exchange(false, acquire) is CONSUME-AND-CLEAR - a
    // message that arrives DURING the fan-out sets the flag again and is picked
    // up next call, so no edit is lost and none is applied twice in one block -
    // and its ACQUIRE is what makes the relaxed pan/mask stores in
    // applyEditMessage() visible to the fan-out below.
    if (partialOverridesPending_.exchange(false, std::memory_order_acquire)) {
        repushPartialOverrides();                  // the fan-out, on the AUDIO thread
    }

    // Phase 10 FR-030. ONE BlockContext per process() CALL - never per slice -
    // built from the SAME tempo sample point Phase 9 already uses
    // (updateSyncedTravelRate above), with Phase 9's THREE-PART guard verbatim in
    // shape. Relying on SpectralDelay's own fallback is NOT sufficient: it fires
    // only on `tempo <= 0.0` (spectral_delay.h:325-327), and a host may leave a
    // STALE POSITIVE tempo in ProcessContext while kTempoValid is clear - which
    // would sync the send to a tempo the transport no longer has.
    //
    // Designated initializers, in declaration order (block_context.h:62-80): a
    // narrowing brace-init is a hard error on the Clang legs, and the two time-
    // signature fields are deliberately left at their defaults because nothing
    // the send does reads them.
    {
        const Vst::ProcessContext* pc = data.processContext;
        const bool tempoOk = pc != nullptr
                             && (pc->state & Vst::ProcessContext::kTempoValid) != 0
                             && pc->tempo > 0.0;
        fxBlockCtx_ = Krate::DSP::BlockContext{
            .sampleRate = sampleRate_,
            .blockSize = total,
            // 120.0 is BlockContext's own documented default (block_context.h:69),
            // i.e. the standalone-scenario tempo, not a number invented here.
            .tempoBPM = tempoOk ? pc->tempo : 120.0,
            .isPlaying =
                pc != nullptr && (pc->state & Vst::ProcessContext::kPlaying) != 0};
    }

    // Phase 10 FR-012 / plan section 3.3. The send's three-state machine, ONCE
    // PER process() CALL and never per slice - `++bypassPredicateEvals_` exactly
    // once, which SC-018 clause (c) asserts against the CALL count over a render
    // whose blocks carry several MIDI slices. It runs BEFORE pushEffectsParams()
    // because it composes fxEffectiveReturnGain_ (FR-023a lifts the return gain
    // while the freeze is engaged) and raises the FR-008 reset request that
    // runSendStage() consumes, and BEFORE setParamSmootherTargets() below, which
    // targets that composed value rather than the raw ID 1410 atomic.
    //
    // It takes the WHOLE CALL's sample count: every counter it advances is a
    // wall-clock quantity, so advancing them per slice would make the bypass
    // age, the drain countdown and the freeze priming window depend on how the
    // host and the MIDI-slice loop happened to partition the block.
    //
    // Phase 11 C-10 / FR-037-FR-039 (plan section 4). THE COMPOSED EFFECTS
    // TARGETS, and this is the ONLY place they are computed.
    //
    // ORDER IS NORMATIVE: it sits immediately ABOVE updateEffectsBypassState(),
    // which is the first of the three consumers, and therefore also above the
    // FR-010 wander ENGAGE predicate below and above setParamSmootherTargets().
    // All three read composedEffects_ instead of the raw ID 1410 / 1441 atomics.
    //
    // IT DOES NOT MOVE INTO THE SLICE LOOP. FR-012 fixes updateEffectsBypassState
    // at once per process() call and the send's chunk machine
    // (kFxSendChunkSamples) is not slice-partitionable. The consequence - a MACRO
    // written on block N first composes on block N+1, because macros_.setMacros()
    // is refreshed inside the loop below - is the RULED and ACCEPTED one-block
    // lag: 10.67 ms at 512/48 kHz, well inside the 20 ms class-(b) smoothing both
    // consumers already impose.
    //
    // THE DEEP HALF IS NOT LAGGED, and must not be: the two Effects-owned bases
    // ARE the raw ID 1410 / 1441 atomics, and FR-039 requires the send-stage skip
    // to be taken "on exactly the same blocks" as before the composition existed.
    // pushEffectsMacroBases() is therefore called HERE, immediately above the
    // composition and above all three of its consumers - see its banner for the
    // three shipped Phase 10 properties a block-lagged deep path breaks.
    //
    // COST: one evaluateAll() (32 rows x one applyModCurve) per process() call.
    pushEffectsMacroBases();
    composedEffects_ = macros_.computeEffectsTargets();
    ++composedEffectsRecomputes_;

    updateEffectsBypassState(total);

    pushEffectsParams();                           // Phase 10 D-2 / FR-021

    // Phase 10 FR-011. The two wander sources advance on EVERY block REGARDLESS
    // of any bypass state (C-3's final clause), so re-engaging the stage does not
    // restart a walk that was conceptually running - a restart would read as the
    // stereo image jumping rather than as one object that kept moving. This is
    // the counter SC-018 clause (b) compares against the block count under BOTH
    // bypass states, which is why the increment sits here and not inside
    // runWanderStage() - and it is incremented UNCONDITIONALLY, before the
    // predicate below decides WHERE this block's advance happens.
    ++widthDriftBlocks_;

    // Phase 10 FR-010 / FR-010a / FR-012. The wander's bypass predicate, hoisted
    // ONCE PER process() CALL beside the send's, and in TWO PARTS.
    //
    // Part 1 is FR-010's EXACT predicate on the RAW atomics. Exactness is the
    // whole point of FR-024a's plugin-side depth multiply: a depth pushed
    // through BrownianDrift::setDepth() would sit behind that component's 150 ms
    // kDriftOutputSmoothMs output smoother (brownian_drift.h:103, :159) and this
    // test could not be taken on the block the host wrote the value - which
    // SC-002 requires, because at the C-6 defaults the skip must be BIT-exact.
    //
    // Part 2 is FR-010a's DISENGAGE arm. The raw predicate goes false the
    // instant a host writes kFxWanderDepthId = 0, but globalMs_'s width smoother
    // still holds the last modulated width (it advances ONLY inside process(),
    // midside_processor.h:186-192), the azimuth pair still holds its last
    // non-unity gains and the depth smoothers are still mid-ramp. Skipping
    // applies EXACT identity, so an unlatched skip would step the stereo image
    // in one sample - reintroducing at the disengage edge exactly the lag
    // FR-024a rejects BrownianDrift::setDepth() over.
    //
    // ORDER IS NORMATIVE: arm/decrement the countdown BEFORE wanderAtIdentity()
    // reads it, so the first non-raw block still sees a positive remainder and
    // keeps the stage running.
    {
        constexpr auto kRelaxed = std::memory_order_relaxed;
        fxWidthBase_ = effectsParams_.width.load(kRelaxed);
        // The raw predicate: the stage runs unless ALL THREE controls sit at
        // identity. Spelled as the disjunction of the three "not at identity"
        // tests (De Morgan of that sentence) so clang-tidy's
        // readability-simplify-boolean-expr has nothing to say about it.
        // Phase 11 FR-038 (plan section 4, substitution 3) - THE ONE THAT DECIDES
        // WHETHER THE STAGE RUNS AT ALL. With the shipped default
        // kFxWanderDepthDefault = 0, an Entropy-macro-only move would never set
        // this predicate if it kept loading the raw ID 1441 atomic: the stage
        // would engage only through the FR-010a disengage latch below, and
        // because setParamSmootherTargets() runs LATER in the pre-slice block
        // than this, engagement would land a further block late. composedEffects_
        // is assigned above updateEffectsBypassState(), i.e. above this block, so
        // it is available here.
        fxWanderRuns_ = fxWidthBase_ != Krate::DSP::MidSideProcessor::kDefaultWidth
                        || std::clamp(composedEffects_.wanderDepth, 0.0f, 1.0f) != 0.0f
                        || effectsParams_.azimuthDepth.load(kRelaxed) != 0.0f;
        if (fxWanderRuns_) {
            fxWanderSettleRemaining_ = fxWanderSettleSamples_;
        } else {
            fxWanderSettleRemaining_ -= static_cast<std::int64_t>(total);
        }
        fxWanderRunsEffective_ = fxWanderRuns_ || !wanderAtIdentity();
    }

    // FR-011's ADVANCE, and WHERE it happens is what makes the wander block-size
    // invariant (SC-017) rather than a staircase whose step is the host's block
    // length.
    //
    // BrownianDrift::processBlock advances the walk to the END of whatever it is
    // given BEFORE any audio is rendered, and getCurrentValue() (:212) is a pure
    // read - so a single processBlock(total) here would make every control chunk
    // in the block read the value at the BLOCK BOUNDARY. At 512-sample blocks
    // that is a ~10.7 ms staircase and at 2048 a ~43 ms one, and the two produce
    // measurably different width and azimuth trajectories from the same render -
    // which SC-017 compares at kSampleTolerance = 1e-4 with the wander engaged.
    //
    // So while the stage RUNS the advance is moved inside it, onto the ABSOLUTE
    // 64-sample control grid: exactly one processBlock(64) per grid boundary, in
    // EVERY partition, so the value at grid boundary k is a pure function of k.
    // While the stage is SKIPPED nothing else would advance the walks at all, so
    // the whole block is advanced here - FR-011's requirement, and the reason a
    // re-engaged wander continues rather than restarts.
    //
    // BrownianDrift::processBlock is a ~32-sample-decimated AR(1)
    // (brownian_drift.h:105, :194), so its cost is a small fraction of a block
    // even when nothing consumes its value.
    //
    // effectsStageBypassed_ is FR-040 capability 1 and is false on every shipping
    // path, but it has to appear in this predicate: with the stage removed from
    // renderSlice() entirely, nothing inside it advances the walks either, and
    // FR-011 would then hold on the shipping path and quietly not under the
    // probe - which is the one configuration SC-002 and SC-012 measure against.
    if (!fxWanderRunsEffective_ || effectsStageBypassed_) {
        widthDrift_.processBlock(total);  // brownian_drift.h:194
        azimuthDrift_.processBlock(total);
    }

    // FR-024's control-grid witness covers ONE process() call, exactly as the
    // pre-output tap does, so a case can assert "32 evaluations over this
    // 2048-sample block" rather than an accumulation across the render.
    wanderControlUpdates_ = 0;

    // Phase 10 FR-041 clauses 1 and 6. ONCE PER process() CALL, and that is
    // load-bearing for BOTH members it touches:
    //   - effectsStageProcessCalls_ is SC-012/SC-013's DIVISOR, and their budgets
    //     are PER BLOCK. renderSlice() runs once per SLICE - the loop below
    //     subdivides on every MIDI event, on the 2048 cap and, while any
    //     class-(b) smoother is unsettled, on the absolute 64-sample grid - so
    //     incrementing there would under-report the per-block cost by up to 8x
    //     and make SC-013's 2.5 % budget structurally unable to fail.
    //   - the tap cursor restarts per CALL, so preOutputTapLForTest() always
    //     describes the block just rendered rather than an accumulation, and the
    //     truncation flag is decided from the WHOLE block's length before any
    //     slice has been taken from it.
    preOutTapCursor_ = 0;
    preOutTapSize_ = 0;
    preOutTapTruncated_ = total > kMaxBlockSamples;
    ++effectsStageProcessCalls_;

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

    // Phase 11.5 Step 0. Everything since the Params lap - the whole
    // once-per-call pre-slice push block - lands in one row.
    if (decomp) {
        decompT = decompLap(decompNs_[static_cast<std::size_t>(DecompStage::PreSlice)], decompT);
    }

    // FR-025 / FR-026: the event-driven slice loop. SeraphisEngine::noteOn /
    // noteOff take NO sample offset (seraphis_engine.h:370, 415), so
    // sub-division is the only way to deliver one.
    std::size_t cursor = 0;
    int32 nextEvent = 0;
    const int32 numEvents =
        (data.inputEvents != nullptr) ? data.inputEvents->getEventCount() : 0;

    while (cursor < total) {
        // 1. SCAN the events due at this slice start - do NOT dispatch them yet.
        //    The scan yields both how far they extend (`dueEnd`, consumed by
        //    step 4) and where the next one lands (`sliceEnd`).
        //
        //    THE SPLIT BETWEEN THIS SCAN AND STEP 4's DISPATCH IS A FIX, NOT A
        //    REFACTOR. SeraphisVoice::noteOn() READS voice configuration at the
        //    instant it runs - `if (envMode_ == Growth) growth_.trigger()`
        //    (seraphis_voice.h:533-535) - so dispatching before step 3's
        //    pushVoiceParams() gated a Growth-mode note on the PREVIOUS block's
        //    envelope mode. The trigger never fired, GrowthEnvelope held Idle at
        //    exactly 0 (growth_envelope.h:239-241) and the Growth branch's
        //    `g = velocity_ * gGrowth * mse_.process()` (seraphis_voice.h:1065-
        //    1071) made the voice BIT-SILENT for its whole life. A host that
        //    loads a preset - setState() raises forcePushAllPending_, consumed
        //    by this call's pre-slice block - or moves ID 700, and plays a note
        //    in the same buffer, hit it every time. The other 36 `VP` rows never
        //    showed it: the push still lands before renderSlice(), so no SAMPLE
        //    was ever rendered on a stale value.
        //    Regression: Seraphis_GrowthNoteInParameterBlockSounds
        //    (integration/processor_audio_test.cpp).
        //
        //    A `while`, NOT an `if`: with an `if` the second event at the same
        //    offset would resolve the next sliceEnd back to `cursor` and a
        //    zero-length slice would reach processStereoBlock (SC-022 clause 5;
        //    tests/test_helpers/seraphis_chain.h:195-198 has the same `while`).
        //
        //    Slice end = the next event's offset, the block end, or the 2048
        //    bound - whichever comes first. Events are assumed sorted by
        //    sampleOffset (VST3 requires it); the `at > cursor` test keeps the
        //    loop well formed on a malformed list by firing a late-but-earlier
        //    event at the current cursor instead of rewinding it.
        int32 dueEnd = nextEvent;
        std::size_t sliceEnd = total;
        while (dueEnd < numEvents) {
            Vst::Event event{};
            if (data.inputEvents->getEvent(dueEnd, event) != kResultOk) {
                ++dueEnd;  // unreadable here, and skipped again by step 4
                continue;
            }
            const std::size_t at = clampOffset(event.sampleOffset, total);
            if (at > cursor) {
                sliceEnd = std::min(sliceEnd, at);
                break;  // due later in this block
            }
            ++dueEnd;
        }

        // 2. FR-026, and the ONLY slice bound. kMaxBlockSamples is the SAME
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

        // 3. ORDER IS NORMATIVE: advance BEFORE the pushes, so this sub-slice
        // carries the value the smoothers just reached. FR-044 is satisfied
        // unchanged - macros_.apply() and applyAetherTargets() still run every
        // slice, at their existing positions inside renderSlice().
        if (decomp) {
            decompT = std::chrono::steady_clock::now();
        }
        advanceParamSmoothers(n);
        pushVoiceParams();    // FR-042: the 37 VP values
        pushMacroSurfaces();  // FR-043: macros + the 27 VP/AE bases
        if (decomp) {
            (void)decompLap(decompNs_[static_cast<std::size_t>(DecompStage::SlicePush)],
                            decompT);
        }

        // 4. Dispatch every event step 1 found due at this slice start, now that
        //    the surfaces those events read are current (see step 1's banner).
        //    Outside the SlicePush lap on purpose: the dispatch sat outside every
        //    Phase 11.5 decomposition row before this reorder too, so no pinned
        //    figure moves.
        while (nextEvent < dueEnd) {
            Vst::Event event{};
            if (data.inputEvents->getEvent(nextEvent, event) == kResultOk) {
                dispatchEvent(*engine_, event);
            }
            ++nextEvent;
        }

        renderSlice(outL + cursor, outR + cursor, n);
        controlPhase_ += n;
        cursor += n;
    }

    // FR-024a clause 3: the snap seam is consumed only once samples were
    // actually produced. setActive(true) and setupProcessing() re-arm it.
    anySamplesSincePrepare_ = true;

    // Phase 11 C-2 / FR-012. ONCE PER process() CALL, AFTER the slice loop and
    // before the silence-flag clause. It sits here, and never inside the loop,
    // for the reason its own banner states: the loop subdivides on every MIDI
    // event, on the 2048 cap and on the 64-sample control grid, so a per-slice
    // publish would issue up to 8x the frames for one block and exhaust the
    // 4-block queue inside one call.
    publishCloudFrame();

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
    //
    // THAT IS WHY THERE IS NO VERSION-AWARE BRANCH (Phase 10 C-8/FR-033). A v1 or
    // v2 stream simply runs out before the block that was appended after it, and
    // every field of that block keeps its registered default - which by C-7 is the
    // behaviour the older stream already had. `version` is read above only to
    // refuse a FUTURE version, never to select a layout.
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
    // ---- end of a version-2 stream: 2532 B, a STRICT PREFIX of v3 ----
    loadEffectsParams(effectsParams_, streamer); // [effects] 64 B (v3)
    // ---- end of a PHASE 10 stream: 2596 B, a STRICT PREFIX of what Phase 11 --
    // ---- writes. The version int32 is ALREADY 3 in both (FR-034a).
    const bool loadedPartialOverrides =                // [partials] 272 B (LAST)
        loadPartialOverrides(partialPanStaging_, partialPanOverrideBits_, partialMaskBits_,
                             streamer);

    // Phase 11 plan 6.2a. setState() OVERWRITES the message-thread mirror
    // wholesale, from the buffer it has just filled - AFTER the load chain, not
    // at the seeding loop above, so the mirror holds the stream's four payloads
    // rather than the factory states the seed put there. Both arrays are written
    // by THIS thread, so no synchronisation is involved.
    //
    // The dropdown tracker is re-armed in the same breath: without it the next
    // stageSlotEdit() would see morphParams_.slot[s] differing from a stale
    // lastAuthoredSlotStateId_ and re-seed the mirror from the factory table,
    // silently discarding the payloads this stream just loaded.
    spectralSlotsAuthoring_ = spectralSlotsStaging_[w];
    for (std::size_t s = 0; s < lastAuthoredSlotStateId_.size(); ++s) {
        lastAuthoredSlotStateId_[s] = morphParams_.slot[s].load(std::memory_order_relaxed);
    }

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

    // Phase 11 FR-034a / plan 6.4. A LOADED override reaches the voices the ONE
    // legal way: the release store the next process() call consumes with
    // exchange(false, acquire) beside pushSpectralStatesIfPending(). setState()
    // runs on the MESSAGE thread, and repushPartialOverrides() writes
    // HarmonicCloud state process() concurrently reads and writes, so calling
    // the fan-out from here would be a data race - exactly what plan 6.2's
    // kinds 2 and 3 defer for.
    //
    // Published only on a WHOLE block: a stream that carried none leaves the
    // table exactly as it was, which is what "absent, not garbage" means.
    if (loadedPartialOverrides) {
        partialOverridesPending_.store(true, std::memory_order_release);
    }
    return kResultOk;
}

// FR-090 / FR-094. Stream layout is FIXED (Phase 10 spec C-8): little-endian,
// 2868 bytes = the version int32 + 85 floats + 22 int32 + four 541-byte
// SpectralState payloads (2596 through [effects]) + Phase 11's 272-byte
// [partials] block. Phase 9's v2 layout was 2532 bytes (73 floats + 18 int32);
// the [effects] block of 12 floats + 4 int32 is APPENDED after it, and FR-034a's
// [partials] block is APPENDED after THAT - which is what keeps every older
// stream a strict byte prefix from offset 4 on and lets the EOF-safe loader
// chain migrate with no version-aware branch.
//
// THE VERSION INT32 STILL READS 3. Phase 11 adds no format version: an append
// plus an EOF-safe loader is the whole migration (spec Non-goals, FR-034a).
//
// IT NEVER READS spectralSlots_. That array is audio-thread-owned (plan 3.7's
// ownership table) and a message-thread read of it would be a data race whose
// visible symptom is a TORN SpectralState in the saved preset - exactly what a
// host that automates a CFG dropdown while saving would hit. The two
// message-thread-safe sources are used instead: the published staging buffer
// while a handoff is outstanding, and (Phase 11 plan 6.2a) the message-thread
// authoring mirror otherwise - which is itself seeded from the immutable factory
// table, so pre-Phase-11 streams are byte-identical to what this wrote before.
tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }

    IBStreamer streamer(state, kLittleEndian);

    // Phase 11 plan 6.2a. Reconcile the message-thread mirror with the 409-412
    // dropdowns BEFORE it is read below. getState() runs on the message thread,
    // exactly like setState() and notify(), so this is the same owner writing.
    syncAuthoringMirrorFromDropdowns();

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
    //
    // Phase 11 plan 6.2a. The FALLBACK source moves from the factory table to
    // spectralSlotsAuthoring_, and that is an EXTENSION of this rule, not a
    // third source: the mirror is the message thread's own copy of the same four
    // payloads, kept in step with the dropdowns by the sync above and with the
    // stream by setState(). Before Phase 11 the two agreed byte for byte (a slot
    // had no source but its dropdown), so no pre-existing stream changes; with
    // the edit channel in, this is what stops a partial edit from being lost the
    // moment the audio thread consumes the handoff and clears it back to -1.
    for (std::size_t s = 0; s < payloads.size(); ++s) {
        payloads[s] = havePublished
                          ? spectralSlotsStaging_[static_cast<std::size_t>(published)][s]
                          : spectralSlotsAuthoring_[s];
    }
    saveSpectralPayloads(payloads, streamer);    // [morph]  2164 B of payload

    saveLifeModParams(lifeParams_, streamer);    // [life]     40 B
    saveBodyParams(bodyParams_, streamer);       // [body]     52 B
    saveAtmosphereParams(atmosParams_, streamer);// [atmos]    68 B
    saveAetherParams(aetherParams_, streamer);   // [aether]   72 B
    // ---- Phase 10's ONE addition (C-8's strict-prefix property) -------------
    saveEffectsParams(effectsParams_, streamer); // [effects]  64 B

    // ---- Phase 11 FR-034a's ONE addition, and it is now LAST ----------------
    // Same strict-prefix property, same reason: the version int32 does NOT move.
    // The two bitmasks are read here (relaxed) rather than passed in, so the
    // block is a snapshot of the message thread's own table at this instant.
    savePartialOverrides(partialPanStaging_,
                         partialPanOverrideBits_.load(std::memory_order_relaxed),
                         partialMaskBits_.load(std::memory_order_relaxed),
                         streamer);              // [partials] 272 B

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
        } else if (id < kEffectsParamRangeEnd) {
            // FR-018. ONE MORE RUNG on the range ladder, never a 107-case switch.
            // handleEffectsParamChange ignores any ID inside 1400-1499 that C-6
            // does not name, so the reserved tail of the band costs one compare.
            handleEffectsParamChange(effectsParams_, id, value);
            markDirty(id);
        }
        // else: an ID outside every shipped range - ignored.
    }
}

// FR-024a clauses 1-2. Called ONLY from process(), which has already
// established that engine_ is non-null and that the processor is prepared.
//
// Every push here is ON CHANGE ONLY. Re-calling setPolyphony() unconditionally
// is wrong twice over: it re-arms the voice-sum smoother (sumGain_.setTarget(...),
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
// @par Real-Time Safety: allocation-free, lock-free, exception-free. Every
//      atomic is read relaxed; every engine setter is noexcept and
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
        // Phase 11 FR-030 (plan 6.3, T011). CALL SITE 5 of six: a POLYPHONY
        // change. The fan-outs already write all kMaxVoices slots
        // (seraphis_engine.h's "kMaxVoices, NOT getPolyphony()" banner), so a
        // newly usable slot carries the override the moment it becomes usable -
        // this re-push is the belt to that braces, and it costs nothing on the
        // steady-state path because setPolyphony itself is ON CHANGE ONLY.
        //
        // IT IS NOT SUFFICIENT ON ITS OWN, and must not be read as if it were:
        // it runs BEFORE this block's macros_.apply(), and apply() writes only
        // voices [0, getPolyphony()) - so the slots that just entered that loop
        // have their positionOverridden_ wiped by the first setStereoSpread they
        // ever receive, AFTER this call. The repair for that is the extent half
        // of the composed-spread tracker in renderSlice(), which fires on the
        // very next slice; see the banner at CALL SITE 2.
        repushPartialOverrides();
    }

    // NO setOutputSaturation BLOCK HERE. Phase 10 D-2 / FR-021 moved it to
    // pushEffectsParams(), which is now the SOLE writer on that setter: two
    // independent on-change trackers driving one setter is last-writer-wins with
    // no convergence, so toggling ID 2 off->on after setting ID 1400 to 0.8 used
    // to revert the engine to 0.15f until ID 1400 next moved. kSoftLimitId keeps
    // its shipped meaning as the GATE, read there.

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
        // Phase 11 FR-030 (plan 6.3, T011). CALL SITE 3 of six: the SEED BURST.
        // HarmonicCloud::setSeed does positionOverridden_.fill(false)
        // (harmonic_cloud.h:701-706) - it clears the PAN overrides only, masked_
        // is untouched - so an authored pan would silently revert to the freshly
        // re-drawn FR-021 scatter. On change only, like the burst itself.
        repushPartialOverrides();
    }

    const bool freeze = atmosParams_.freeze.load(std::memory_order_relaxed);
    if (!lastPushedFreezeValid_ || freeze != lastPushedFreeze_) {  // ON CHANGE ONLY
        engine_->setAtmosphereFreeze(freeze);  // seraphis_engine.h:551
        lastPushedFreeze_ = freeze;
        lastPushedFreezeValid_ = true;
        ++engFreezePushes_;
    }
}

// Phase 10 D-2 / FR-021 (plan 2.5.4). THE SINGLE WRITER on
// SeraphisEngine::setOutputSaturation on the audio thread. Called ONCE per
// process() call from the pre-slice block, never per slice - the hoist is valid
// for the same stated reason every other pre-slice push is hoisted:
// processParameterChanges() ran at the top of process() and took the LAST point
// of every queue, so neither atomic can change within a process() call.
//
// The pushed quantity is COMPOSED: kSoftLimitId (ID 2) keeps its shipped meaning
// as a GATE - off means no output saturation at all - and kFxSaturationId
// (ID 1400) supplies the amount that gate passes. Change detection is on the
// COMPOSED value, not on the two inputs separately: detecting on the gate as
// well would re-push kOutputSaturation the moment ID 2 was toggled off and on,
// silently discarding ID 1400's amount.
//
// engSoftLimitPushes_ is the EXISTING Phase 9 counter, incremented from here, so
// no Phase 9 cadence assertion moves.
//
// EVERYTHING BELOW STEP 1 IS FR-022 / FR-025 / FR-026 / FR-027, and every one of
// them is ON CHANGE ONLY against its own tracker, each push counted in
// effectsPushes_ (FR-041 clause 3). Re-pushing an unchanged value is not free:
// every SpectralDelay setter re-targets a parameter smoother the component
// advances once per chunk, and the seed burst below redraws ~2052 RNG values.
//
// IDs 1410, 1440, 1441 and 1443 are DELIBERATELY ABSENT. 1410, 1441 and 1443 are
// the three class-(b) smoothers (FR-038b clause 2) whose targets are set in
// setParamSmootherTargets(), and 1440 is pushed inside the wander stage on the
// 64-sample control grid. ID 1430 IS here (step 7), but as the COMPOSED
// `freezeReady` of plan D-5 rather than as a blind push of its atomic.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. Every read
//      is a relaxed atomic load; every write is a component setter that stores a
//      scalar and re-targets a smoother. The ONE unbounded-looking step is the
//      seed burst, which is bounded by construction (2 x numBins) and is the
//      second burst site SC-011 gates.
void Processor::pushEffectsParams() noexcept {
    // ---- 1. ID 1400 - the composed single writer (D-2 / FR-021) -------------
    const bool soft = globalParams_.softLimit.load(std::memory_order_relaxed);
    const float amount =
        soft ? effectsParams_.saturation.load(std::memory_order_relaxed) : 0.0f;
    if (!lastPushedSaturationValid_ || amount != lastPushedSaturation_) {  // ON CHANGE ONLY
        engine_->setOutputSaturation(amount);  // seraphis_engine.h:672
        lastPushedSaturation_ = amount;
        lastPushedSaturationValid_ = true;
        ++engSoftLimitPushes_;
    }

    // ---- 2. The seed (FR-026, FR-027) --------------------------------------
    // The send's OWN tracker, not lastPushedSeedIndex_: setupProcessing() already
    // ran this burst once, so the first process() after a prepare must not repeat
    // it - and the engine/reverb tracker is invalidated on a Reprepared scope
    // while this one is not.
    //
    // seedRng THEN reset, the order the header documents (:295-296) and the same
    // two calls setupProcessing() makes, so the post-seed state is a pure function
    // of the seed rather than of ASLR (:223-224, :279-284).
    //
    // THE FIFOs ARE NOT CLEARED HERE. fxFifoClearDue_ defers that to the top of
    // runSendStage(), before any partial chunk-loop state is live: clearing them
    // from inside the loop would zero fxChunkFill_ and then subtract 512 from a
    // std::size_t, wrapping to ~2^64 and hanging the audio thread (plan R-13).
    const auto fxSeedIndex =
        static_cast<int>(clampSeedIndex(globalParams_.seedIndex.load(std::memory_order_relaxed)));
    if (fxSeedIndex != lastPushedFxSeedIndex_) {  // ON CHANGE ONLY
        const std::uint32_t seed = kSeedValues[static_cast<std::size_t>(fxSeedIndex)];
        spectralDelay_.seedRng(seed);  // spectral_delay.h:297
        spectralDelay_.reset();        // :242
        ++spectralDelayResets_;        // FR-041 clause 2
        fxFifoClearDue_ = true;

        // TWO DISTINCT SALTS (C-5 / FR-024a clause 3). Identical salts would make
        // width and azimuth walk in lockstep off one stream, which reads as a
        // single moving object rather than as two independent ones.
        widthDrift_.setSeed(seed ^ kFxWidthDriftSalt);  // brownian_drift.h:145
        widthDrift_.reset();                            // :133
        azimuthDrift_.setSeed(seed ^ kFxAzimuthDriftSalt);
        azimuthDrift_.reset();

        lastPushedFxSeedIndex_ = fxSeedIndex;
        ++effectsPushes_;
    }

    // The first push after every prepare delivers the WHOLE registered set: the
    // component still holds its own construction defaults at this point, which
    // are not the C-6 ones (see the tracker banner in processor.h).
    const bool first = !lastPushedFxValid_;

    // ---- 3. IDs 1411, 1412, 1416, 1417 -------------------------------------
    const float delayTimeMs = effectsParams_.delayTimeMs.load(std::memory_order_relaxed);
    if (first || delayTimeMs != lastPushedFxDelayTimeMs_) {
        spectralDelay_.setBaseDelayMs(delayTimeMs);  // spectral_delay.h:425
        lastPushedFxDelayTimeMs_ = delayTimeMs;
        ++effectsPushes_;
    }

    const float spreadMs = effectsParams_.delaySpreadMs.load(std::memory_order_relaxed);
    if (first || spreadMs != lastPushedFxSpreadMs_) {
        spectralDelay_.setSpreadMs(spreadMs);  // :432
        lastPushedFxSpreadMs_ = spreadMs;
        ++effectsPushes_;
    }

    const float diffusion = effectsParams_.delayDiffusion.load(std::memory_order_relaxed);
    if (first || diffusion != lastPushedFxDiffusion_) {
        spectralDelay_.setDiffusion(diffusion);  // :489
        lastPushedFxDiffusion_ = diffusion;
        ++effectsPushes_;
    }

    const float stereoWidth = effectsParams_.delayWidth.load(std::memory_order_relaxed);
    if (first || stereoWidth != lastPushedFxStereoWidth_) {
        spectralDelay_.setStereoWidth(stereoWidth);  // :512
        lastPushedFxStereoWidth_ = stereoWidth;
        ++effectsPushes_;
    }

    // ---- 4. IDs 1414 + 1415 TOGETHER (FR-016a) ------------------------------
    // The compensated feedback is a function of BOTH, so it is recomputed and
    // re-pushed whenever EITHER moves. A build that recomputed only on a feedback
    // change would leave the uncompensated value installed when the tilt alone
    // moved - and at feedback 0.95 with tilt +1 that puts 243 of 513 bins above
    // unity loop gain, where tanh(delayedMag * binFeedback) (:751-767) has a
    // stable non-zero fixed point and the bins sustain forever.
    //
    // The divisor is the NAMED helper in effects_params.h - never an inline
    // literal here - because SC-005 clause 1's 513-bin sweep evaluates the same
    // expression from another translation unit.
    const float feedback = effectsParams_.delayFeedback.load(std::memory_order_relaxed);
    const float tilt = effectsParams_.delayTilt.load(std::memory_order_relaxed);
    const bool tiltMoved = first || tilt != lastPushedFxTilt_;
    if (tiltMoved || feedback != lastPushedFxFeedback_) {
        spectralDelay_.setFeedback(Seraphis::tiltCompensatedFeedback(feedback, tilt));  // :460
        lastPushedFxFeedback_ = feedback;
        ++effectsPushes_;
    }
    if (tiltMoved) {
        spectralDelay_.setFeedbackTilt(tilt);  // :468 - pushed UNCHANGED
        lastPushedFxTilt_ = tilt;
        ++effectsPushes_;
    }

    // ---- 5. IDs 1418 + 1419 -------------------------------------------------
    const bool sync = effectsParams_.delaySync.load(std::memory_order_relaxed);
    if (first || sync != lastPushedFxSync_) {
        spectralDelay_.setTimeMode(sync ? 1 : 0);  // :524
        lastPushedFxSync_ = sync;
        ++effectsPushes_;
    }

    const int syncNote = effectsParams_.delaySyncNote.load(std::memory_order_relaxed);
    if (first || syncNote != lastPushedFxSyncNote_) {
        spectralDelay_.setNoteValue(syncNote);  // :532 - clamps to [0, 9] itself
        lastPushedFxSyncNote_ = syncNote;
        ++effectsPushes_;
    }

    // ---- 6. ID 1413 ---------------------------------------------------------
    const int spreadDirection = effectsParams_.spreadDirection.load(std::memory_order_relaxed);
    if (first || spreadDirection != lastPushedFxSpreadDirection_) {
        // toSpreadDirection() clamps before the static_cast, which is what keeps a
        // corrupt state blob out of an out-of-range enum (dropdown_mappings.h:344).
        spectralDelay_.setSpreadDirection(toSpreadDirection(spreadDirection));  // :439
        lastPushedFxSpreadDirection_ = spreadDirection;
        ++effectsPushes_;
    }

    // ---- 7. ID 1430 - the PRIMED freeze push (FR-023a, plan D-5) ------------
    // WHY THIS IS NOT A BLIND PUSH OF THE ATOMIC. processSpectralFrame captures
    // on the first frame where `freezing && !wasFrozen_` (spectral_delay.h:
    // 677-688), reading the STFT's CURRENT analysis frame. From the C-6 defaults
    // the send has been bypassed since prepare (FR-023a's whole subject), so at
    // the instant of a freeze-FORCED engage that frame is prepare-time zeros or a
    // stale, fully-drained tail: capturing it gives a SILENT frozen spectrum, and
    // "engaging 1430 alone is audible" - the point of FR-023a - fails for an
    // implementation that follows it literally.
    //
    // So the engage waits until the send has consumed kFxFreezePrimeSamples of
    // LIVE bus (2 x kDefaultFFTSize = four hops = two analyses on wholly-live
    // frames = 42.7 ms at 48 kHz, far inside SC-007's 200 ms measurement point).
    // updateEffectsBypassState() ran earlier in THIS call (:946), so both the
    // state and the counter are this block's.
    //
    // FREEZE-OFF IS NEVER DEFERRED: freezeReady falls in the same block freezeOn
    // does, because it is a conjunction with it. And the tracker holds the
    // COMPOSED value, so the deferred engage pushes exactly once.
    const bool freezeOn = effectsParams_.spectralFreeze.load(std::memory_order_relaxed);
    const bool freezeReady = freezeOn && (fxSendState_ == FxSendState::Active)
                             && fxLiveSamplesSinceEngage_ >= kFxFreezePrimeSamples;
    if (first || freezeReady != lastPushedFxFreezeReady_) {  // ON CHANGE ONLY
        spectralDelay_.setFreezeEnabled(freezeReady);  // spectral_delay.h:479
        lastPushedFxFreezeReady_ = freezeReady;
        ++effectsPushes_;
    }

    // ---- 8. ID 1442 - ONE value, BOTH drifts (FR-025) -----------------------
    // The two differ ONLY by seed salt, so they never move in lockstep. A build
    // that pushed one drift only is invisible in audio - the other simply wanders
    // at its prepared kDefaultSmoothness - which is why FR-041 gives this its own
    // assertion rather than trusting the render.
    const float wanderRate = effectsParams_.wanderRate.load(std::memory_order_relaxed);
    if (first || wanderRate != lastPushedFxWanderRate_) {
        widthDrift_.setSmoothness(wanderRate);  // brownian_drift.h:152
        azimuthDrift_.setSmoothness(wanderRate);
        lastPushedFxWanderRate_ = wanderRate;
        ++effectsPushes_;
    }

    lastPushedFxValid_ = true;
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
    // Phase 11 SC-007's strict `>`. ONE increment per SLICE - deliberately the
    // opposite cadence to effectsStageProcessCalls_ (once per CALL), because the
    // criterion is precisely that the slice count EXCEEDS the publish-attempt
    // count on any render carrying MIDI or an unsettled class-(b) smoother.
    ++renderSlices_;

    // Phase 11.5 Step 0, same gate as process()'s copy: FALSE on every shipping
    // path, so this function pays five always-false branches per slice and no
    // clock read.
    const bool decomp = processDecompInstrumented_;
    std::chrono::steady_clock::time_point decompT{};
    if (decomp) {
        decompT = std::chrono::steady_clock::now();
    }

    // 2. Macros -> engine, and the Aether-owned half -> reverb.
    //
    //    Applied EVERY SLICE even at Phase 8's neutral macro defaults (FR-034):
    //    computeAetherTargets() is what pushes the reverb's eight controls, and
    //    "inert" describes the macro VALUES, not the push.
    macros_.apply(*engine_);                                      // macro_matrix.h:623

    // Phase 11 FR-030 (plan 6.3, T011). CALL SITE 2 of six, and the one that
    // catches the defect this task exists for.
    //
    // apply() above just pushed the COMPOSED CloudStereoSpread into every voice,
    // and HarmonicCloud::setStereoSpread wipes positionOverridden_ on any VALUE
    // change (harmonic_cloud.h:535-547). The trigger is therefore keyed on the
    // composed value - read back as the value the cloud actually STORED - and
    // NOT on ParamID 207: Bloom writes this target through the macro matrix, so
    // a macro-ring sweep with the deep knob held still clears every authored pan
    // while a ParamID-keyed tracker sees nothing (SC-014 arm 6). The deep path is
    // covered too, because 207 is this target's setTargetBase origin.
    //
    // The re-push is IMMEDIATE, not deferred through partialOverridesPending_:
    // the clearing and the render it would corrupt are in this same slice, so a
    // flag consumed by the NEXT process() call would publish one whole block of
    // scattered pans. This is the audio thread, which is exactly where the
    // fan-out belongs.
    //
    // THE EXTENT OF apply()'s LOOP IS TRACKED ALONGSIDE ITS VALUE, and that is
    // load-bearing rather than defensive. apply() writes voices [0, getPolyphony())
    // only (seraphis_macro_matrix.h:712-715), so a POLYPHONY INCREASE is itself a
    // clearing event: the slots that just entered the loop are still holding the
    // spread SeraphisVoice::prepare() left them, and the first apply() to reach
    // them pushes a DIFFERENT value and wipes their positionOverridden_. Voice 0 -
    // the read-back below - has been inside the loop all along and does not move,
    // so a value-only tracker is blind to exactly the slots that were cleared, and
    // the re-push pushGlobalParams() fires beside engine_->setPolyphony() cannot
    // help: it runs BEFORE this apply(), i.e. before the clearing it would have to
    // repair. Comparing the extent as well makes the first slice after the change
    // re-push, AFTER apply() has written the newly in-range slots.
    {
        const std::size_t appliedVoices = engine_->getPolyphony();  // seraphis_engine.h:665
        const float composedSpread = engine_->getVoice(0).cloud().getStereoSpread();  // :549
        if (!lastPushedComposedSpreadValid_ || composedSpread != lastPushedComposedSpread_
            || appliedVoices != lastPushedSpreadVoiceCount_) {
            lastPushedComposedSpread_ = composedSpread;
            lastPushedSpreadVoiceCount_ = appliedVoices;
            lastPushedComposedSpreadValid_ = true;
            repushPartialOverrides();
        }
    }

    applyAetherTargets(*reverb_, macros_.computeAetherTargets());  // :667 + FR-034a
    if (decomp) {
        decompT = decompLap(decompNs_[static_cast<std::size_t>(DecompStage::MacroApply)],
                            decompT);
    }

    // 3. Voice sum only - no reverb, no output stage.
    engine_->processStereoBlock(dryL_.data(), dryR_.data(), n);  // engine.h:441
    if (decomp) {
        decompT = decompLap(decompNs_[static_cast<std::size_t>(DecompStage::Engine)], decompT);
    }

    // 4. The Layer 4 stage the engine cannot own.
    reverb_->processStereoBlock(dryL_.data(), dryR_.data(), wetL_.data(), wetR_.data(),
                                n);  // reverb.h:2164
    if (decomp) {
        decompT = decompLap(decompNs_[static_cast<std::size_t>(DecompStage::Reverb)], decompT);
    }

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
    if (decomp) {
        (void)decompLap(decompNs_[static_cast<std::size_t>(DecompStage::MasterGain)], decompT);
    }

    // 4c. Phase 10, C-1 STEPS 4 AND 5, plus FR-041 clause 6's pre-output tap -
    //     all inside ONE scoped timer (FR-041 clause 1).
    //
    //     THE TAP COPY IS DELIBERATELY INSIDE THE TIMED REGION. Hiding it would
    //     not make it free: it would charge it to SC-014's whole-render full-poly
    //     gate, which has the least headroom of any budget in this phase (4.09
    //     percentage points), instead of to SC-012/SC-013, which are the criteria
    //     sized to pay for this phase's per-block cost. That relationship is
    //     UNCHANGED by the instrumentation gate below: the gate is on for every
    //     measured figure (ProcessorFixture::prepare() sets it), so the tap is
    //     still paid for out of the scoped timer and never out of the ceiling.
    //
    //     CONSTITUTION II - THE GATE. `effectsStageInstrumented_` is FALSE on
    //     every shipping path, so the shipped plugin executes NO clock read and
    //     NO tap copy here; it pays one always-false branch per slice. The gate
    //     is not optional politeness: this function runs ONCE PER SLICE and the
    //     slice loop subdivides on the absolute 64-sample control grid whenever a
    //     class-(b) smoother is unsettled (:1298-1301), so unconditional
    //     instrumentation put up to 64 std::chrono::steady_clock::now() calls -
    //     a system call on every platform this plugin targets - plus 32 full-bus
    //     copies into a single 2048-sample host callback of the SHIPPED build,
    //     during any parameter ramp. Constitution II forbids system calls on the
    //     audio thread outright.
    //
    //     Real-time safety (FR-029), when the gate IS on: two clock reads, one
    //     add, one buffer copy. No allocation, no lock, no throw, no file I/O -
    //     preOutTapL_/preOutTapR_ were sized once in setupProcessing() and are
    //     indexed through .data().
    {
        const bool instrumented = effectsStageInstrumented_;
        std::chrono::steady_clock::time_point stageStart{};
        if (instrumented) {
            stageStart = std::chrono::steady_clock::now();
        }

        if (!effectsStageBypassed_) {                     // FR-040 capability 1
            // FR-007's EXACT prohibition. While the send is neither active nor
            // draining the stage is NOT CALLED: no SpectralDelay::process, no
            // copy of the bus into fxIn*, no read or write of any send buffer.
            // This is the only reason the C-6 default configuration costs
            // nothing, and sendChunkCountForTest() is its CI-gated observation.
            if (fxSendRuns_) {
                runSendStage(wetL_.data(), wetR_.data(), n);  // C-1 step 4
            }
            if (!effectsStageAfterOutput_) {              // FR-040 capability 2
                runWanderStage(wetL_.data(), wetR_.data(), n);  // C-1 step 5
            }
        }

        // The bus AS THE OUTPUT STAGE WILL SEE IT. The guard is what makes the
        // truncation flag honest rather than an out-of-bounds write: a host block
        // larger than 2048 arrives as several slices and only the ones that still
        // fit are taped.
        if (instrumented && preOutTapCursor_ + n <= preOutTapL_.size()) {
            std::copy_n(wetL_.data(), n, preOutTapL_.data() + preOutTapCursor_);
            std::copy_n(wetR_.data(), n, preOutTapR_.data() + preOutTapCursor_);
            preOutTapCursor_ += n;
            preOutTapSize_ = preOutTapCursor_;
        }

        if (instrumented) {
            const std::chrono::steady_clock::time_point stageEnd =
                std::chrono::steady_clock::now();
            effectsStageNs_ +=
                std::chrono::duration<double, std::nano>(stageEnd - stageStart).count();
        }
    }

    // 5. Output stage IN PLACE on the reverb return: tape saturator -> true-peak
    //    limiter. ALWAYS LAST.
    if (decomp) {
        decompT = std::chrono::steady_clock::now();
    }
    engine_->processOutputStage(wetL_.data(), wetR_.data(), n);  // engine.h:512
    if (decomp) {
        (void)decompLap(decompNs_[static_cast<std::size_t>(DecompStage::Output)], decompT);
    }

    // FR-040 capability 2, TEST PATHS ONLY. SC-003(a)'s positive control needs
    // the deliberately-wrong order - step 5 AFTER step 6 - to provably FAIL the
    // ceiling clause, which is what makes the shipped order's pass non-vacuous.
    // effectsStageAfterOutput_ is false on every shipping path.
    if (effectsStageAfterOutput_) {
        runWanderStage(wetL_.data(), wetR_.data(), n);
    }

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
// Phase 10 - C-1 STEP 4: the fixed-size send accumulator (spec C-2, plan 3.1)
// ==============================================================================
// WHY THE ACCUMULATOR EXISTS. SpectralDelay::process is NOT partition-invariant.
// STFT::canAnalyze() requires samplesAvailable_ >= fftSize_ (stft.h:134-137),
// each analyze() consumes hopSize_ (:171), each synthesize() marks
// samplesReady_ += hopSize_ (:311), and process() pulls
// toPull = min(numSamples, availableL, availableR) (spectral_delay.h:366) while
// writing dryBuffer * dryMix - SILENCE at 100 % wet - into everything it cannot
// supply (:383-386). At fftSize 1024 / hop 512 a single 2048-sample call has
// three analyses ready and lands wet-stream sample 0 at output index 0; the SAME
// audio as four 512-blocks lands it at index 512. That is a PERMANENT one-hop
// offset of the whole send, not a start-up transient - and the Seraphis chain is
// documented block-size invariant. Hence: the component is called with a
// CONSTANT kFxSendChunkSamples, never with a slice length (FR-003a).
//
// THE INVARIANT, PROVED - not merely asserted. Let inLen be fxChunkFill_ and
// outLen be fxOutFill_ after a slice. clearFifos() establishes inLen = 0,
// outLen = 512 (the one-chunk pre-fill) and RE-establishes exactly that at every
// clear. One slice of n samples gives inLen' = inLen + n, k = floor(inLen'/512)
// chunks, inLen'' = inLen' - 512k, outLen'' = outLen + 512k - n. Substituting
// outLen = 512 - inLen:
//
//     outLen'' = 512 - inLen - n + 512k = 512 - inLen''
//
// so fxChunkFill_ + fxOutFill_ == kFxSendChunkSamples at EVERY slice boundary,
// inLen'' < 512 => outLen'' > 0, and the output FIFO can never underflow. The
// pipeline delay is therefore exactly 512 samples IN EVERY PARTITION - the
// property SC-017 tests, and the reason FR-005 keeps it out of the reported
// latency (it is a delay's own delay, absorbed into the delay time).
//
// The two assertions below GUARD that proof rather than restate it.
// fxChunkFill_ and fxOutFill_ are std::size_t, so a violated invariant is not a
// glitch but a wrap to ~2^64 that silently invalidates every later occupancy
// test and walks stale ring content into the bus. They cost nothing in Release
// and turn any future edit that breaks the proof into an immediate Debug
// failure. The path is reachable in the SHIPPING configuration - the 64-sample
// sub-slice subdivision runs for the whole of every engage/bypass ramp - so this
// is not a theoretical hazard.
//
// CAPACITY. Peak input occupancy before the chunk loop is
// 511 + kMaxBlockSamples = 2559; peak output occupancy before the mix loop is
// 512 + 2048 = 2560. kFxFifoCapacity = 4096 is a power of two, so the ring index
// is a mask and never a modulo.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. Every
//      buffer was sized once in setupProcessing() and is indexed through
//      .data() / operator[], never .at() (which throws).
void Processor::runSendStage(float* busL, float* busR, std::size_t n) noexcept {
    constexpr std::size_t kMask = kFxFifoCapacity - 1u;

    // ---- clear -------------------------------------------------------------
    // AT THE TOP, and NEVER inside the chunk loop. Inside the loop the guard
    // `fxChunkFill_ >= kFxSendChunkSamples` has ALREADY passed, so a clear that
    // zeroes fxChunkFill_ is immediately followed by `-= 512` on a std::size_t,
    // which wraps to ~2^64, keeps the loop condition true forever and calls
    // SpectralDelay::process() without bound on the audio thread - a hard hang,
    // not a glitch (plan R-13).
    //
    // This is also FR-008's deferred reset's SINGLE firing point (plan D-3):
    // `fxChunkFill_ + n >= kFxSendChunkSamples` is exactly "the next fill-chunk
    // boundary", which is the only grid there is - while bypassed the input FIFO
    // is not written at all, so no second, free-running counter could describe
    // the same thing.
    if (fxFifoClearDue_ || (fxResetDue_ && fxChunkFill_ + n >= kFxSendChunkSamples)) {
        if (fxResetDue_) {
            spectralDelay_.reset();  // spectral_delay.h:242 - allocation-free
            ++spectralDelayResets_;  // FR-041 clause 2
            fxResetDue_ = false;
        }
        clearFifos();
        fxFifoClearDue_ = false;
    }

    // ---- push --------------------------------------------------------------
    // FR-009a: while DRAINING the send is fed SILENCE, not the live bus. That is
    // what bounds a bypass excursion's cost by ENERGY rather than by wall clock -
    // the component's own per-bin feedback decays the tail, and the drain's
    // energy exit ends the window as soon as it has.
    const bool live = (fxSendState_ == FxSendState::Active);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t w = (fxInWrite_ + i) & kMask;
        fxInL_[w] = live ? busL[i] : 0.0f;
        fxInR_[w] = live ? busR[i] : 0.0f;
    }
    fxInWrite_ += n;
    fxChunkFill_ += n;

    // ---- run ---------------------------------------------------------------
    while (fxChunkFill_ >= kFxSendChunkSamples) {
        for (std::size_t i = 0; i < kFxSendChunkSamples; ++i) {
            const std::size_t r = (fxInRead_ + i) & kMask;
            fxChunkL_[i] = fxInL_[r];
            fxChunkR_[i] = fxInR_[r];
        }
        fxInRead_ += kFxSendChunkSamples;
        fxChunkFill_ -= kFxSendChunkSamples;

        // THE CONSTANT-LENGTH CALL. Never `n`.
        spectralDelay_.process(fxChunkL_.data(), fxChunkR_.data(), kFxSendChunkSamples,
                               fxBlockCtx_);  // spectral_delay.h:315
        ++sendChunks_;                        // FR-041 clause 7 / FR-007

        // FR-009a's energy measurement, taken on what the send PRODUCED. Read
        // only by the Draining arm of updateEffectsBypassState().
        float peak = 0.0f;
        for (std::size_t i = 0; i < kFxSendChunkSamples; ++i) {
            peak = std::max({peak, std::fabs(fxChunkL_[i]), std::fabs(fxChunkR_[i])});
        }
        fxDrainPeak_ = peak;

        for (std::size_t i = 0; i < kFxSendChunkSamples; ++i) {
            const std::size_t w = (fxOutWrite_ + i) & kMask;
            fxOutL_[w] = fxChunkL_[i];
            fxOutR_[w] = fxChunkR_[i];
        }
        fxOutWrite_ += kFxSendChunkSamples;
        fxOutFill_ += kFxSendChunkSamples;
    }

    // ---- mix ---------------------------------------------------------------
    assert(fxOutFill_ >= n && "plan 3.1: the output FIFO cannot underflow");

    // READ, NEVER .process()-ed. fxReturnGainSm_ is in classBSmoothers(), so
    // advanceParamSmoothers() already advanced it by n immediately before
    // renderSlice() was entered. A second per-sample advance would move it 2n
    // per n rendered while the send runs and n while it does not - halving
    // kFxReturnRampMs AND making the ramp rate state-dependent. See the
    // invariant banner beside the declaration in processor.h.
    //
    // FR-040 capability 3 (SC-008's positive control (b)): the probe snaps the
    // ramp to its target so the deliberately un-ramped engage can be shown to
    // click. It is false on every shipping path.
    if (effectsReturnRampSnap_) {
        fxReturnGainSm_.snapToTarget();  // smoother.h:257
    }
    const float g = fxReturnGainSm_.getCurrentValue();  // smoother.h:191
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t r = (fxOutRead_ + i) & kMask;
        busL[i] += fxOutL_[r] * g;
        busR[i] += fxOutR_[r] * g;
    }
    fxOutRead_ += n;
    fxOutFill_ -= n;

    assert(fxChunkFill_ + fxOutFill_ == kFxSendChunkSamples
           && "plan 3.1: inLen + outLen is invariant at every slice boundary");
}

// ==============================================================================
// Phase 10 - C-1 STEP 5: stereo wandering (spec C-5, plan 3.4)
// ==============================================================================
// It takes the POST-MASTER-GAIN bus and works IN PLACE on it, ALWAYS BEFORE the
// output stage - a width or azimuth change applied after the limiter re-inflates
// peaks above the ceiling. Its two BrownianDrift sources are prepared in
// setupProcessing() 5f and advanced on the ABSOLUTE 64-sample control grid
// below while this stage runs - and by process()' pre-slice block while it does
// not, because FR-011 requires them to advance regardless of any bypass state
// while SC-017 requires the value the controls are computed from to depend on
// the absolute sample position and not on the host's block length.
//
// THE SKIP IS MANDATORY, NOT AN OPTIMISATION. Running MidSideProcessor at width
// 100 % is an ALGEBRAIC identity - mid = (L+R)*0.5, side = (L-R)*0.5
// (midside_processor.h:200-201), reconstructed as mid +/- side (:225-226) - but
// NOT an IEEE-754 bit identity: each of those operations rounds, so e.g.
// L = 1.0f, R = 2^-30 reconstructs R_out = 0.0f. SC-002 asserts
// max |a - b| == 0.0f over a 10 s render at the C-6 defaults, and an implementer
// who trusts the algebra and leaves the stage running fails it.
//
// THE BODY IS INTERLEAVED WITH THE AUDIO, AND IT MUST BE (plan R-14). An earlier
// revision ran the whole control loop first and called globalMs_.process(l, r,
// l, r, n) plus the azimuth multiply once AFTERWARDS. That shape delivers
// NOTHING: MidSideProcessor::setWidth() only stores width_ and calls
// widthSmoother_.setTarget() (midside_processor.h:133-136) and
// OnePoleSmoother::setTarget() only stores a target (primitives/smoother.h:170),
// so every iteration but the last is overwritten before a single sample is
// touched - and the net control grid becomes one update per SLICE (up to
// kMaxBlockSamples = 2048, i.e. ~43 ms at 48 kHz in the steady wander state),
// not 64 samples. C-5's and FR-024's "at most once per 64-sample control chunk,
// on the same absolute grid the engine and the reverb already use" would have
// been a claim the code did not deliver.
//
// PER-SAMPLE TRANSCENDENTALS ARE FORBIDDEN (C-5, FR-024): the cos/sin pair is
// evaluated ONCE PER CONTROL CHUNK and the two products are SMOOTHER TARGETS,
// which is what turns 2048 cos/sin pairs per block into 32.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. Fixed-size
//      arithmetic over the caller's buffers; the only calls are into components
//      prepared in setupProcessing().
void Processor::runWanderStage(float* busL, float* busR, std::size_t n) noexcept {
    // FR-010's skip, taken on fxWanderRunsEffective_ and NOT on the raw
    // fxWanderRuns_ - the raw predicate only arms and re-arms the disengage
    // latch (plan 3.4).
    if (!fxWanderRunsEffective_) {
        fxWanderWasActive_ = false;
        return;
    }
    if (n == 0) {
        return;
    }

    // FR-010a's ENGAGE arm. globalMs_'s width smoother does NOT advance while
    // the stage is skipped (it advances only inside process(),
    // midside_processor.h:186-192), so on re-engage it would otherwise start
    // ramping from whatever width the LAST engaged span left in it - a value
    // with no relation to what the bus has been carrying since.
    //
    // WHAT IT SNAPS TO IS IDENTITY, and that is the load-bearing half. While the
    // stage was skipped the bus was passing through untouched, i.e. width 100 %
    // and unity gains; snapping to the NEW target instead would step the image
    // by the whole width change in one sample - which is precisely what FR-010a
    // and SC-008 forbid, and it would additionally defeat ID 1440's declared
    // continuity mechanism (FR-038b classifies 1440 as `Smoother`, the
    // component's own widthSmoother_). Snapped to identity, the first setWidth()
    // below ramps in over MidSideProcessor::kDefaultSmoothingMs = 10 ms and the
    // azimuth pair over kParamSmoothMs = kFxReturnRampMs = 20 ms.
    if (!fxWanderWasActive_) {
        globalMs_.setWidth(Krate::DSP::MidSideProcessor::kDefaultWidth);  // :133
        globalMs_.reset();                                                // :114
        azimuthGainLSm_.snapTo(1.0f);
        azimuthGainRSm_.snapTo(1.0f);
    }

    // The ABSOLUTE 64-sample control grid. controlPhase_ is incremented AFTER
    // renderSlice() returns, so controlPhase_ + off is the correct absolute
    // position of sample `off` inside this slice - the same grid the class-(b)
    // sub-slice clamp aligns to, so a control chunk never straddles a slice
    // boundary in the un-settled state.
    std::size_t off = 0;
    while (off < n) {
        const auto phase =
            static_cast<std::size_t>((controlPhase_ + off) % kWanderControlChunkSamples);
        const std::size_t chunkLen =
            std::min(kWanderControlChunkSamples - phase, n - off);

        // FR-011 + SC-017. ON A GRID BOUNDARY, and only there, both walks step by
        // exactly ONE control chunk. This is the whole of the block-size
        // invariance argument: the advance lands at absolute sample 64k in EVERY
        // partition, so the value the width and azimuth targets are computed from
        // at grid boundary k is a pure function of k - identical whether the host
        // delivered the render as 1-sample calls or as 2048-sample ones. The
        // pre-slice block advances the walks instead while the stage is skipped
        // (see the banner there); the two paths are mutually exclusive, so no
        // sample is ever advanced twice.
        if (phase == 0) {
            widthDrift_.processBlock(kWanderControlChunkSamples);   // brownian_drift.h:194
            azimuthDrift_.processBlock(kWanderControlChunkSamples);
        }

        // --- width -----------------------------------------------------------
        // getCurrentValue() is a pure read (brownian_drift.h:212), so between
        // grid boundaries it returns the value the boundary above established -
        // no cache is needed for that, and none is kept. The depth smoother is in
        // classBSmoothers(), advanced by advanceParamSmoothers() for this
        // sub-slice immediately before renderSlice() was entered, so it too is
        // READ and never advanced here.
        const float driftW = widthDrift_.getCurrentValue();          // [-1, +1]
        const float depthW = fxWanderDepthSm_.getCurrentValue();     // ID 1441
        const float width = std::clamp(fxWidthBase_ + depthW * driftW * kWanderWidthSpanPercent,
                                       Krate::DSP::MidSideProcessor::kMinWidth,
                                       Krate::DSP::MidSideProcessor::kMaxWidth);
        globalMs_.setWidth(width);  // midside_processor.h:133 - a TARGET

        // --- azimuth ---------------------------------------------------------
        const float driftA = azimuthDrift_.getCurrentValue();
        const float depthA = fxAzimuthDepthSm_.getCurrentValue();    // ID 1443
        const float position = std::clamp(0.5f + 0.5f * depthA * driftA, 0.0f, 1.0f);
        float gainL = 1.0f;
        float gainR = 1.0f;
        Krate::DSP::equalPowerGains(position, gainL, gainR);  // crossfade_utils.h:50
        // Plan D-4. equalPowerGains is a CROSSFADE law: across ONE stereo bus the
        // constant quantity is gL^2 + gR^2, so the raw pair drops the whole bus
        // -3.01 dB the instant kFxAzimuthDepthId leaves 0. With the compensation
        // gL^2 + gR^2 = 2 at every position and centre is unity per channel.
        azimuthGainLSm_.setTarget(gainL * kFxAzimuthCentreComp);
        azimuthGainRSm_.setTarget(gainR * kFxAzimuthCentreComp);

        // --- THIS sub-chunk's audio, before the next control update -----------
        globalMs_.process(busL + off, busR + off, busL + off, busR + off,
                          chunkLen);  // IN PLACE, midside_processor.h:181
        for (std::size_t i = off; i < off + chunkLen; ++i) {
            // These two are NOT in classBSmoothers(), so .process() here is
            // their sole advance and the "never advance a class-(b) smoother
            // twice" invariant is untouched.
            busL[i] *= azimuthGainLSm_.process();  // smoother.h:197
            busR[i] *= azimuthGainRSm_.process();
        }

        // FR-024's witness, written AFTER the audio it was applied to - which is
        // what makes it evidence of the INTERLEAVING and not merely of the
        // control loop's trip count (plan R-14).
        if (wanderControlUpdates_ < kWanderControlLogCapacity) {
            wanderAzimuthTargetL_[wanderControlUpdates_] = azimuthGainLSm_.getTarget();
            wanderAzimuthTargetR_[wanderControlUpdates_] = azimuthGainRSm_.getTarget();
            wanderChunkLengths_[wanderControlUpdates_] = static_cast<std::uint16_t>(chunkLen);
        }
        ++wanderControlUpdates_;

        off += chunkLen;
    }

    fxWanderWasActive_ = true;
}

// FR-010a's DISENGAGE arm (plan 3.4). Every clause is an EXACT comparison except
// the azimuth pair's, and that exception is measured rather than chosen - see
// kFxAzimuthIdentityEps in processor.h.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free.
bool Processor::wanderAtIdentity() const noexcept {
    // The countdown stands in for the one smoother that cannot be queried:
    // MidSideProcessor exposes getWidth() (the TARGET, :236) and no view of its
    // internal widthSmoother_'s progress, and the Non-goals forbid adding one.
    if (fxWanderSettleRemaining_ > 0) {
        return false;
    }
    if (globalMs_.getWidth() != Krate::DSP::MidSideProcessor::kDefaultWidth) {  // :236, :67
        return false;
    }
    // Both depth smoothers must have REACHED exactly zero, not merely be near
    // it: the width target is fxWidthBase_ + depthW * ... , so a residual depth
    // keeps the pushed width off kDefaultWidth and the clause above would in any
    // case still be false.
    if (!fxWanderDepthSm_.isComplete() || fxWanderDepthSm_.getCurrentValue() != 0.0f) {
        return false;
    }
    if (!fxAzimuthDepthSm_.isComplete() || fxAzimuthDepthSm_.getCurrentValue() != 0.0f) {
        return false;
    }
    if (!azimuthGainLSm_.isComplete() || !azimuthGainRSm_.isComplete()) {  // smoother.h:232
        return false;
    }
    return std::fabs(azimuthGainLSm_.getCurrentValue() - 1.0f) <= kFxAzimuthIdentityEps
           && std::fabs(azimuthGainRSm_.getCurrentValue() - 1.0f) <= kFxAzimuthIdentityEps;
}

// ==============================================================================
// Phase 10 - plan section 3.3: the send's three-state machine
// ==============================================================================
// FR-007, FR-008, FR-009, FR-009a and FR-023a in one place, evaluated ONCE per
// process() call (FR-012). Nothing here touches audio; it decides whether the
// send runs at all, at what return gain, and whether the next fill-chunk
// boundary must reset the component.
//
// WHY THE RESET IS CONDITIONAL (FR-008). SpectralDelay::reset() clears wasFrozen_
// and freezeCrossfade_ (spectral_delay.h:276-277) and the frozen spectrum buffers
// (:256-257) along with all 4 x 513 per-bin DelayLines (:259-273), and
// re-randomizes 2 x numBins phases (:279-284). An UNCONDITIONAL reset would
// annihilate the tail and any captured freeze on EVERY automation curve that
// merely touches zero - and FR-007's predicate is exact `== 0.0f`, so a bipolar
// LFO crosses it twice per cycle.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. One relaxed
//      atomic load (the freeze flag; Phase 11 replaced the ID 1410 load with a
//      read of composedEffects_) and integer arithmetic.
void Processor::updateEffectsBypassState(std::size_t blockSamples) noexcept {
    ++bypassPredicateEvals_;  // FR-041 clause 5 - EXACTLY ONCE PER CALL

    // FR-007's predicate is an EXACT comparison against 0.0f, deliberately: the
    // whole point of the bypass is that the shipped default configuration costs
    // nothing, and an epsilon would leave a band of "almost off" settings paying
    // for a send nobody can hear.
    // Phase 11 FR-037 (plan section 4, substitution 1). THE COMPOSED send level,
    // not the raw ID 1410 atomic - `deep base + the Dissolve row`. The CLAMP is
    // required, not defensive: computeEffectsTargets() returns the RAW sum
    // (range clamping belongs to the consuming setter,
    // seraphis_macro_matrix.h:662-666) and everything downstream of `mix` here -
    // the FR-007 predicate, fxEffectiveReturnGain_ and hence
    // fxReturnGainSm_ - is unit-range. Same rule applyAetherTargets() follows.
    //
    // FR-039 falls out arithmetically: at the macro neutrals the composition IS
    // the atomic bit-for-bit and std::clamp on a value the parameter surface
    // already produced in [0, 1] is the identity, so this line is `==` to the
    // load it replaced at every shipped default.
    const float mix = std::clamp(composedEffects_.delaySend, 0.0f, 1.0f);
    const bool freezeOn = effectsParams_.spectralFreeze.load(std::memory_order_relaxed);

    // FR-023a. The freeze FORCES the send active even at mix 0, and lifts the
    // return gain to kFxFreezeMinReturnGain, so engaging it is audible rather
    // than a silent no-op the user cannot distinguish from a broken control.
    const bool wantActive = (mix != 0.0f) || freezeOn;
    fxEffectiveReturnGain_ = freezeOn ? std::max(mix, kFxFreezeMinReturnGain) : mix;

    const auto blockU64 = static_cast<std::uint64_t>(blockSamples);
    const auto blockI64 = static_cast<std::int64_t>(blockSamples);
    // Spelled as an expression rather than as the UINT64_MAX macro so the
    // saturation bound is a typed constant on every leg.
    constexpr std::uint64_t kU64Max = ~std::uint64_t{0};

    switch (fxSendState_) {
        case FxSendState::Bypassed:
            if (wantActive) {
                // FR-008: reset iff (a) the send has been bypassed for LONGER
                // than the whole drain window - i.e. whatever it still holds is
                // stale by construction - AND (b) the engage was not
                // freeze-forced, because a freeze engage must capture the live
                // spectrum rather than a freshly-emptied one.
                // std::cmp_greater, not a cast: the two counters are a uint64
                // and an int64, and a cast to silence the mixed-sign compare is
                // exactly what modernize-use-integer-sign-comparison rejects.
                // The saturating counter can reach UINT64_MAX (see the else
                // arm), which no int64 can represent, so the comparison has to
                // be done in the value domain rather than by converting either
                // side.
                fxResetDue_ = std::cmp_greater(fxBypassedSamples_, fxSendDrainSamples_) && !freezeOn;
                fxSendState_ = FxSendState::Active;
                fxLiveSamplesSinceEngage_ = 0;
            } else {
                // Saturating, never wrapping. At 192 kHz a uint64 covers ~3
                // million years, so the guard is belt-and-braces - but the
                // comparison above is an ORDERING on this counter, and a wrap
                // would invert it.
                if (fxBypassedSamples_ > kU64Max - blockU64) {
                    fxBypassedSamples_ = kU64Max;
                } else {
                    fxBypassedSamples_ += blockU64;
                }
            }
            break;

        case FxSendState::Active:
            if (!wantActive) {
                fxSendState_ = FxSendState::Draining;
                fxDrainRemaining_ = fxSendDrainSamples_;
                // RE-ARMED ABOVE THE FLOOR, and this is not cosmetic: a send
                // engaged and re-bypassed inside a single chunk period runs no
                // chunk at all, so without the re-arm the energy exit below
                // would read a STALE sub-floor value from a PREVIOUS drain and
                // terminate the new drain on its very first block - exactly the
                // tail annihilation FR-008/FR-009a exist to prevent. The value
                // 1.0f is arbitrary except that it must exceed the floor; the
                // first chunk of the new drain overwrites it with a real
                // measurement.
                fxDrainPeak_ = 1.0f;
            } else {
                if (fxLiveSamplesSinceEngage_ > kU64Max - blockU64) {
                    fxLiveSamplesSinceEngage_ = kU64Max;
                } else {
                    fxLiveSamplesSinceEngage_ += blockU64;
                }
            }
            break;

        case FxSendState::Draining:
            if (wantActive) {
                fxSendState_ = FxSendState::Active;
                fxLiveSamplesSinceEngage_ = 0;
            } else if (fxDrainRemaining_ <= 0 || fxDrainPeak_ < kFxSendDrainFloor) {
                fxSendState_ = FxSendState::Bypassed;
                fxBypassedSamples_ = 0;
            } else {
                fxDrainRemaining_ -= blockI64;
            }
            break;
    }

    fxSendRuns_ = (fxSendState_ != FxSendState::Bypassed);
}

// ==============================================================================
// Phase 11 C-10 / SC-021(b)(c) - the composed-effects test seams
// ==============================================================================
// Out-of-line so nothing in a shipping translation unit can inline them into the
// render path; none is ever called from process(). They report the RAW composed
// value - unclamped - because the clamp belongs to each consuming read site and
// SC-021(b)'s bit-equality is a statement about the composition itself.
float Processor::composedFxDelaySendForTest() const noexcept {
    return composedEffects_.delaySend;
}

float Processor::composedFxWanderDepthForTest() const noexcept {
    return composedEffects_.wanderDepth;
}

std::size_t Processor::composedEffectsRecomputeCountForTest() const noexcept {
    return composedEffectsRecomputes_;
}

// Plan section 3.1. THE ONE DEFINITION of the accumulator's start state, so the
// invariant has exactly one establishing point rather than three transcriptions
// that can drift. Three call sites: setupProcessing(), setActive(false), and the
// single deferred mid-render site at the top of runSendStage().
//
// THE OUTPUT COUNTERS ARE NOT ZEROED, and that is the whole point of writing this
// out once. The output FIFO is PRE-FILLED with one chunk of silence: the send
// reads its return one chunk AHEAD of the first chunk the component will ever
// produce, which is what makes the accumulator's pipeline delay a FIXED 512
// samples in every partition. Zeroing fxOutFill_ instead would make the first
// read of `fxOutFill_ - n` wrap a std::size_t (plan R-13) and would break the
// section 3.1 invariant outright.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. The four
//      vectors were sized once in setupProcessing().
void Processor::clearFifos() noexcept {
    std::fill(fxInL_.begin(), fxInL_.end(), 0.0f);
    std::fill(fxInR_.begin(), fxInR_.end(), 0.0f);
    std::fill(fxOutL_.begin(), fxOutL_.end(), 0.0f);
    std::fill(fxOutR_.begin(), fxOutR_.end(), 0.0f);

    fxInWrite_ = 0;
    fxInRead_ = 0;
    fxChunkFill_ = 0;

    fxOutRead_ = 0;
    fxOutWrite_ = kFxSendChunkSamples;  // THE ONE-CHUNK PRE-FILL
    fxOutFill_ = kFxSendChunkSamples;
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
        // FR-019. `FX` BUMPS NO GENERATION COUNTER - neither voiceParamGeneration_
        // nor aetherParamGeneration_. The effects surface reaches the DSP through
        // pushEffectsParams()' own on-change trackers, once per process() call, so
        // a bump would only force an unrelated 37-setter x 16-voice or ten-control
        // reverb fan-out. It shares ENG's and Local's empty body deliberately: the
        // three are written as ONE branch because bugprone-branch-clone (correctly)
        // treats three consecutive `break;`s as a copy-paste smell, and the reason
        // each one is empty is recorded here rather than in three identical arms.
        case Route::FX:
        case Route::ENG:    // pushGlobalParams()' own trackers own these
        case Route::Local:  // consumed inside the processor (master gain, macros, sync pair)
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

// FR-003. The SINGLE mapping from one of C-6's MB rows to its owning atomic - 27
// through Phase 10, 29 since Phase 11 / C-10 appended the two Effects-owned rows.
// FR-055's "never also through SeraphisVoiceParams" is checkable by construction:
// SeraphisVoiceParams has no field for any of them.
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
        // -- Effects-owned (2), Phase 11 C-10 ---------------------------------
        // The RAW atomics, deliberately - unlike the four class-(b) rows above,
        // these two are not smoothed on the way IN. Their smoothing happens
        // DOWNSTREAM of the composition (fxReturnGainSm_ / fxWanderDepthSm_
        // target the COMPOSED value), so the base has to be the unsmoothed
        // origin the macros move from or the smoothing would be applied twice.
        case Target::FxDelaySend:
            return effectsParams_.delayMix.load(kRelaxed);     // ID 1410
        case Target::FxWanderDepth:
            return effectsParams_.wanderDepth.load(kRelaxed);  // ID 1441
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

// FR-043. The five macro knobs, then the 27 VOICE/AETHER-owned MB bases, each ON
// CHANGE ONLY. (Phase 11 / C-10's two Effects-owned bases belong to the
// pre-slice pushEffectsMacroBases() - see its banner for why they cannot live in
// this loop.)
//
// The macro push is its OWN owning push (the macros are route `processor`, not
// `MB`), so it deliberately does NOT increment setTargetBasePushes_ - which is
// what keeps SC-007's setTargetBase row exact at "kNumTargets at prepare, +1 for
// a class-(a) MB change".
//
// The base change detection is PER-TARGET. A single global settling flag would
// re-push all 27 of them for one class-(b) MB change, which SC-007's table
// forbids (it asserts Delta = +1 ... +N on ONE target, the others untouched).
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

    // --- the 27 VOICE/AETHER-owned MB bases (FR-003) -------------------------
    // The loop stops at kFirstEffectsTarget. The two Effects-owned bases are NOT
    // pushed here: they are owned by pushEffectsMacroBases(), which runs in the
    // pre-slice block ABOVE the composition so that a deep-knob write reaches
    // composedEffects_ on the block the host delivered it (FR-039's "on exactly
    // the same blocks"). Pushing them from inside this loop would put them one
    // whole block behind the composition that reads them - see that function's
    // banner. There is still EXACTLY ONE base writer per target.
    for (std::size_t t = 0; t < Krate::DSP::SeraphisMacroMatrix::kFirstEffectsTarget; ++t) {
        const auto target = static_cast<Krate::DSP::SeraphisMacroTarget>(t);
        const float value = baseValueForTarget(target);
        // PER-TARGET settling (plan 3.5.5), and the macro smoothers are
        // deliberately NOT in this predicate - the macro push above owns them.
        // Exactly ONE target re-pushes per chunk per un-settled class-(b) `MB`
        // row, so SC-007's Delta is 1 ... N_chunk and the other 28 are untouched.
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

// Phase 11 C-10 / FR-038 / FR-039. THE TWO EFFECTS-OWNED BASES, and the reason
// they are not in pushMacroSurfaces()' loop.
//
// WHY THIS FUNCTION EXISTS. `composedEffects_ = macros_.computeEffectsTargets()`
// is evaluated ONCE per process() call, in the pre-slice block, because FR-012
// fixes updateEffectsBypassState() at that cadence and the send's chunk machine
// is not slice-partitionable. pushMacroSurfaces() runs INSIDE the slice loop.
// So a base pushed from that loop is always one whole process() call behind the
// composition that reads it - and for the two Effects-owned targets the base IS
// the deep parameter (baseValueForTarget returns the raw ID 1410 / 1441 atomic),
// so that lag is a lag on the DEEP path, not on the macro path. It would break
// three shipped Phase 10 properties at once:
//
//   * FR-010's RAW wander predicate, which SC-018 takes on the very block the
//     host writes kFxWanderDepthId = 0 (integration/effects_chain_test.cpp's
//     "depth never reaches BrownianDrift::setDepth");
//   * FR-039's own "the send-stage skip is taken on exactly the same blocks",
//     which is what keeps SC-001 exact;
//   * SC-017's block-size invariance - a lag measured in BLOCKS moves a
//     transition to a different SAMPLE in every partition, so the identical
//     script renders differently at block 1, 512 and 4096.
//
// The macro half keeps its ruled one-block lag (spec Clarifications,
// composition-cadence entry): macros_.setMacros() is still pushed from the slice
// loop, so a move on IDs 100-104 still composes on the next call. Only the deep
// origin is made current, which is the half FR-039 pins bit-for-bit.
//
// ON CHANGE ONLY, exactly like the loop it was split out of, and sharing
// lastPushedBase_[] / lastPushedBaseValid_ with it - so SC-007's table is
// unmoved: kNumTargets invocations at prepare (2 here + 27 there) and +1 for a
// class-(a) change. There is NO settling clause: unlike the four class-(b) `MB`
// rows whose base is a smoother read-back, these two bases are raw atomics that
// STEP, and their class-(b) smoothing (fxReturnGainSm_ / fxWanderDepthSm_)
// happens DOWNSTREAM of the composition. A settling clause here would re-push an
// unchanged value on every chunk of every send ramp for no observable at all.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. Two relaxed
//      atomic loads and two noexcept scalar stores.
void Processor::pushEffectsMacroBases() noexcept {
    using Matrix = Krate::DSP::SeraphisMacroMatrix;
    for (std::size_t t = Matrix::kFirstEffectsTarget;
         t < Matrix::kFirstEffectsTarget + Matrix::kNumEffectsTargets; ++t) {
        const auto target = static_cast<Krate::DSP::SeraphisMacroTarget>(t);
        const float value = baseValueForTarget(target);
        if (lastPushedBaseValid_ && value == lastPushedBase_[t]) {
            continue;  // ON CHANGE ONLY
        }
        macros_.setTargetBase(target, value);
        lastPushedBase_[t] = value;
        ++setTargetBasePushes_;  // INVOCATIONS, for SC-007
    }
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
// slots to every voice in the mask, and SpectralMorphEngine::setState runs
// isValidSpectralState PLUS buildSanitized - a full 64-entry std::log2 pass
// (spectral_morph_engine.h:296-301, :537-543) - BEFORE the identity check at
// :302-304 that would make it a no-op. One whole-pool pass therefore costs
// 16 x 4 x 64 ~= 4096 std::log2.
//
// Phase 11 FR-033a (D-1) removed SeraphisVoice's configure-time gate from
// setSpectralState/setSpectralStateCount, so a SOUNDING voice accepts too: the
// mask now empties on the FIRST attempt and the loop below is a one-block
// convergence, not a per-block retry for the length of a held note. The retry
// machinery is kept because nothing here may assume it - a future rejecting
// caller would otherwise silently lose the push.
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

// Phase 11 FR-030 / FR-043 (plan 6.3, T011). THE ONE CONSUMER of the per-partial
// override table - see the banner on the declaration in processor.h for the
// thread ownership, the mask polarity and the cost.
//
// THE LOOP IS UNCONDITIONAL OVER ALL 64 INDICES AND PUSHES BOTH POLARITIES.
// Walking only the set bits of partialMaskBits_ would make CLEARING a bit a
// no-op at the engine - there would be no call at all for the cleared index -
// and masked_[i] would stay true for the life of the instance. SC-033's unmask
// half is exactly that observation.
//
// The pan half IS conditional, and legitimately so: setPartialPosition RAISES
// positionOverridden_[i] (harmonic_cloud.h:1069-1079), so pushing an
// un-authored partial would install an override that the seeded FR-021 scatter
// never asked for. A partial with no authored pan must be left to the scatter,
// which is what the panBits test preserves.
void Processor::repushPartialOverrides() noexcept {
    const std::uint64_t panBits = partialPanOverrideBits_.load(std::memory_order_relaxed);
    const std::uint64_t maskBits = partialMaskBits_.load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < Krate::DSP::HarmonicCloud::kMaxPartials; ++i) {
        const bool masked = ((maskBits >> i) & 1u) != 0u;
        // `active` is the INVERSE of "masked" (harmonic_cloud.h:1082-1089).
        engine_->setPartialMaskAllVoices(i, !masked);
        if (((panBits >> i) & 1u) != 0u) {
            engine_->setPartialPositionAllVoices(
                i, partialPanStaging_[i].load(std::memory_order_relaxed));
        }
    }
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
    lastPushedBaseValid_ = false;   // forces all kNumTargets setTargetBase pushes
    lastPushedMacros_ = Krate::DSP::SeraphisMacroValues{};
    lastPushedMacrosValid_ = false;
    // FR-034. Every effects push tracker is invalidated here, so a setState()
    // that arrives AFTER setupProcessing() re-pushes the whole effects surface
    // rather than leaving the DSP on the prepare-time values: the composed
    // saturation tracker, and the eleven SpectralDelay/drift trackers behind the
    // one shared first-call flag (T014's ten, plus T016's composed freeze).
    lastPushedSaturationValid_ = false;
    lastPushedFxValid_ = false;
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
// Phase 11 C-5 (plan 6.2, 6.2a) - the edit channel. MESSAGE THREAD ONLY.
// ==============================================================================

// Plan 6.2a. Lazy, message-thread reconciliation of the authoring mirror with
// the four slot dropdowns.
//
// The audio thread's own tracker for the same four IDs is
// lastPushedSlotStateId_, written by refreshSpectralSlotFromFactory() from
// processParameterChanges(); that function is NOT the place to seed this mirror,
// because it runs on the audio thread and the mirror is message-thread state.
// Reading morphParams_.slot[] here instead is legal from either thread - it is
// the pack's std::atomic<int>, which is what every other consumer already reads.
//
// factoryStates_ is immutable from the constructor on (see its banner), so the
// copy below needs no synchronisation either.
void Processor::syncAuthoringMirrorFromDropdowns() noexcept {
    for (std::size_t s = 0; s < spectralSlotsAuthoring_.size(); ++s) {
        const int stateId = morphParams_.slot[s].load(std::memory_order_relaxed);
        if (stateId == lastAuthoredSlotStateId_[s]) {
            continue;  // unchanged: an authored edit in this slot SURVIVES
        }
        lastAuthoredSlotStateId_[s] = stateId;
        spectralSlotsAuthoring_[s] = factoryStates_[clampFactoryIndex(stateId)];
    }
}

// Plan 6.2 steps 1-6. The second writer on the Phase 9 staging ring, using the
// identical published sequence: pick, seed, mutate, validity-gate, release-store,
// advance the cursor. No second interlock and no lock, because this runs on the
// same message thread setState() writes the ring from.
void Processor::stageSlotEdit(std::size_t slot,
                              const Krate::DSP::SpectralState& edited) noexcept {
    if (slot >= spectralSlotsAuthoring_.size()) {
        return;  // belt to applyEditMessage()'s braces
    }

    // (4) FIRST, because it is the only thing that can make this a no-op:
    // SpectralMorphEngine::setState rejects an invalid state wholesale
    // (spectral_morph_engine.h:296-298), so publishing one would be a silently
    // inert edit. Checking here turns it into a DROPPED one, and leaves both the
    // ring and the mirror untouched.
    if (!Krate::DSP::isValidSpectralState(edited)) {
        return;
    }

    // (1) The existing three-buffer chooser: neither published nor being copied.
    const std::size_t w = pickStagingBuffer();

    // (2) Seed the WHOLE buffer from the message-thread mirror, so the three
    // slots this edit does not touch are not lost. NEVER from spectralSlots_,
    // which is audio-thread-owned.
    spectralSlotsStaging_[w] = spectralSlotsAuthoring_;

    // (3)
    spectralSlotsStaging_[w][slot] = edited;

    // (5) Publish, then advance the cursor - plan 3.7 step 3's order exactly.
    spectralSlotsHandoff_.store(static_cast<int>(w), std::memory_order_release);
    stagingWriteCursor_ = static_cast<int>((w + 1u) % spectralSlotsStaging_.size());

    ++editStageWrites_;

    // (6) ...and the mirror follows the ring, so the NEXT edit in the same
    // gesture composes on top of this one rather than on top of a stale slot.
    spectralSlotsAuthoring_[slot] = edited;
}

// Plan 6.2. Validation first (C-5 clause 5), then the eight-row dispatch.
//
// EVERY rejection is a SILENT return: an EditMessage is untrusted input, and the
// mutators' own rejection (C-6) is the second line of defence, not the first.
void Processor::applyEditMessage(const UI::EditMessage& message) noexcept {
    // --- C-5 clause 5, in order --------------------------------------------
    if (message.kind >= UI::kEditKindCount) {
        return;  // unknown kind
    }
    const bool namesASlot = (message.kind == 1 || message.kind == 4 || message.kind == 5
                             || message.kind == 6 || message.kind == 7);
    if (namesASlot && message.slot > 3u) {
        return;
    }
    const bool namesAPartial = (message.kind == 1 || message.kind == 2 || message.kind == 3);
    if (namesAPartial
        && message.index >= static_cast<std::uint16_t>(Krate::DSP::HarmonicCloud::kMaxPartials)) {
        return;
    }
    // BIT PATTERN, never std::isnan: this TU is compiled with the plugin's normal
    // flags and the macOS leg is -ffast-math (spectral_state.h:21-23 states the
    // same rule for the mutators themselves).
    if (Krate::DSP::detail::isNaN(message.a) || Krate::DSP::detail::isInf(message.a)
        || Krate::DSP::detail::isNaN(message.b) || Krate::DSP::detail::isInf(message.b)) {
        return;
    }

    const auto slot = static_cast<std::size_t>(message.slot);
    const auto partial = static_cast<std::size_t>(message.index);

    switch (message.kind) {
        case 0: {  // EditorGate - C-2 clause 6's producer gate.
            cloudFrameEnabled_.store(message.a != 0.0f, std::memory_order_relaxed);
            return;
        }
        case 1: {  // PartialRatioAmp
            syncAuthoringMirrorFromDropdowns();
            Krate::DSP::SpectralState edited = spectralSlotsAuthoring_[slot];
            Krate::DSP::setPartial(edited, partial, message.a, message.b);
            stageSlotEdit(slot, edited);
            return;
        }
        case 2: {  // PartialPan - STAGED, no engine call (plan 6.2's reversal).
            partialPanStaging_[partial].store(std::clamp(message.a, -1.0f, 1.0f),
                                              std::memory_order_relaxed);
            const std::uint64_t bit = std::uint64_t{1} << partial;
            partialPanOverrideBits_.fetch_or(bit, std::memory_order_relaxed);
            partialOverridesPending_.store(true, std::memory_order_release);
            return;
        }
        case 3: {  // PartialMask - STAGED, no engine call.
            //
            // `a` is the TOGGLED value the controller computed from
            // CloudFrame::maskBits (Q5), never an unconditional mask, so both
            // directions are reachable and a masked partial can be un-masked.
            const std::uint64_t bit = std::uint64_t{1} << partial;
            if (message.a != 0.0f) {
                partialMaskBits_.fetch_or(bit, std::memory_order_relaxed);
            } else {
                partialMaskBits_.fetch_and(~bit, std::memory_order_relaxed);
            }
            // A mask edit is an override on that partial whichever way it went -
            // CloudFrame::overriddenBits is "pan AND/OR mask".
            partialOverridesPending_.store(true, std::memory_order_release);
            return;
        }
        case 4: {  // BlendStates - ABSOLUTE, from the latched pristine A (Q2).
            if (!blendSnapshotValid_) {
                return;  // no live kind-7 snapshot for this gesture -> DROPPED
            }
            // `b` carries slot B as a float. THE RANGE TEST HAPPENS IN FLOAT,
            // BEFORE the cast, and that order is load-bearing: `b` is only
            // screened for finiteness above, so a value like 1e30 is legal input
            // here, and static_cast<int> of a float outside int's range is
            // undefined behaviour - a fuzzed message must not be able to reach
            // it.
            if (!(message.b >= 0.0f) || !(message.b < 4.0f)) {
                return;
            }
            const auto bIndex = static_cast<std::size_t>(message.b);
            syncAuthoringMirrorFromDropdowns();
            const Krate::DSP::SpectralState blended = Krate::DSP::blendStates(
                blendSnapshotA_, spectralSlotsAuthoring_[bIndex], message.a);
            stageSlotEdit(slot, blended);
            return;
        }
        case 5: {  // TiltState - `a` is an ABSOLUTE dB/oct (C-6), never a delta.
            syncAuthoringMirrorFromDropdowns();
            Krate::DSP::SpectralState edited = spectralSlotsAuthoring_[slot];
            Krate::DSP::tiltState(edited, message.a);
            stageSlotEdit(slot, edited);
            return;
        }
        case 6: {  // SlotSelect - a slot change ENDS any gesture.
            selectedEditSlot_ = static_cast<int>(message.slot);
            blendSnapshotValid_ = false;
            return;
        }
        case 7: {  // BlendBegin - latches A and writes the ring NOT AT ALL.
            syncAuthoringMirrorFromDropdowns();
            blendSnapshotA_ = spectralSlotsAuthoring_[slot];
            blendSnapshotValid_ = true;
            return;
        }
        default:
            return;  // unreachable: kind was range-checked above
    }
}

// ==============================================================================
// FR-059 clause (b) - the class-(b) smoother machinery (plan 3.5)
// ==============================================================================

// The TWELVE class-(b) smoothers: Phase 9's nine in the order plan 3.5.3 lists
// the IDs - 801, 802, 1215, 1216, then the five macro knobs 100-104 - followed
// by Phase 10's three, IDs 1410, 1441 and 1443 (FR-038b clause 2).
//
// EVERY member of this array is READ with getCurrentValue() and NEVER
// .process()-ed: advanceParamSmoothers() advances all of them by the sub-slice's
// own sample count, so a second per-sample advance anywhere would double the
// rate. The invariant, and the masterGain_ precedent that shows the shipped code
// already respects it, are written out beside the declarations in processor.h.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. A
//      fixed-size array of twelve pointers, returned BY VALUE - see the banner
//      on the declaration for why the type is pinned.
std::array<Krate::DSP::OnePoleSmoother*, 12> Processor::classBSmoothers() noexcept {
    return {{&resonanceSm_, &bodyDampingSm_, &breathDepthSm_, &tideDepthSm_,
             macroSm_.data(), &macroSm_[1], &macroSm_[2], &macroSm_[3],
             &macroSm_[4], &fxReturnGainSm_, &fxWanderDepthSm_, &fxAzimuthDepthSm_}};
}

// Twelve setTarget() calls (smoother.h:170) from the already-clamped atomics. The
// packs denormalize and clamp on the way in, so no clamp is repeated here - with
// TWO exceptions, both of which take a COMPOSED value rather than an atomic: ID
// 1410, whose target is fxEffectiveReturnGain_, and ID 1441, whose target is the
// Phase 11 macro composition. computeEffectsTargets() returns the RAW sum, so
// that one clamp is required (seraphis_macro_matrix.h:662-666).
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

    // Phase 10, FR-038b clause 2. ID 1410 takes the COMPOSED value
    // updateEffectsBypassState() computed for this process() call, NOT the raw
    // kFxDelayMixId atomic: FR-023a lifts the return gain to
    // kFxFreezeMinReturnGain while the freeze is engaged, and FR-008/FR-009's
    // engage/bypass ramp IS this smoother travelling to that composed value.
    // Targeting the raw atomic instead would make a freeze at mix 0 silent.
    // The state machine runs earlier in the same pre-slice block, so the value
    // read here is this call's, never the previous call's.
    fxReturnGainSm_.setTarget(fxEffectiveReturnGain_);                        // ID 1410
    // Phase 11 FR-038 (plan section 4, substitution 2). ID 1441 takes the
    // COMPOSED depth - `deep base + the Entropy row` - clamped to the unit range
    // this smoother's consumer (runWanderStage's width multiply) assumes.
    // fxEffectiveReturnGain_ above needs no second substitution: it is DERIVED
    // from the already-composed `mix` inside updateEffectsBypassState(), so
    // substitution 1 carries through to the engage/bypass ramp and to FR-023a's
    // freeze-forced gain unchanged.
    fxWanderDepthSm_.setTarget(std::clamp(composedEffects_.wanderDepth, 0.0f, 1.0f));  // ID 1441
    fxAzimuthDepthSm_.setTarget(effectsParams_.azimuthDepth.load(kRelaxed));  // ID 1443
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

// PER-TARGET, never one flag for all 29 - see pushMacroSurfaces()' banner. The
// other 24 targets are class (a): their component smooths, ramps, gates or
// snapshots the pushed value itself.
//
// Phase 11 C-10's two Effects-owned rows are DELIBERATELY ABSENT. IDs 1410 and
// 1441 are class-(b) IDs on the continuity surface (param_continuity_test.cpp's
// kContinuityMechanism[] names fxReturnGainSm_ / fxWanderDepthSm_ for them), but
// this predicate is not about an ID's continuity mechanism - it is about whether
// the value pushed as a BASE is a smoother read-back that can go quiet before it
// converges. For the four rows above it is (bodyDampingSm_ etc. are read through
// getCurrentValue()); for these two it is not - baseValueForTarget() returns the
// RAW atomic, which steps, and their smoothing happens DOWNSTREAM of the
// composition. They are also not pushed by pushMacroSurfaces() at all any more
// (pushEffectsMacroBases() owns them), so a clause here would be unreachable.
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
// Phase 10 D-6 states the COST of the three new members, and it is WANTED: for
// the ~20 ms after any 1410 / 1441 / 1443 automation point, and for the whole of
// every send engage/bypass ramp, blocks now render as 64-sample sub-slices. That
// is exactly what delivers the ramp on the absolute grid rather than in one step
// per host block.
bool Processor::anyClassBSmootherUnsettled() const noexcept {
    return !resonanceSm_.isComplete() || !bodyDampingSm_.isComplete()
           || !breathDepthSm_.isComplete() || !tideDepthSm_.isComplete()
           || anyMacroSmootherUnsettled() || !fxReturnGainSm_.isComplete()
           || !fxWanderDepthSm_.isComplete() || !fxAzimuthDepthSm_.isComplete();
}

// ==============================================================================
// Phase 11 C-2 / FR-012 / FR-014 / FR-015 (plan 5.3) - the cloud-frame producer
// ==============================================================================
// THE BODY ORDER IS NORMATIVE, and every step of it is load-bearing:
//
//   1. the GATE, and it is the ONLY short-circuit;
//   2. the ATTEMPT counter - incremented whenever the GATE is open,
//      INDEPENDENTLY of whether a queue exists;
//   3. the FOCUS voice (C-2 clause 4's three clauses, in order);
//   4. fill the MEMBER pendingFrame_ field by field;
//   5. TRANSPORT ONLY FROM HERE DOWN.
//
// WHY THE HANDLER IS NOT A PRECONDITION OF THE ATTEMPT. The headless harness
// this phase's criteria run in - plugins/seraphis/tests/seraphis_test_fixture.h's
// ProcessorFixture - does initialize(nullptr) -> setupProcessing ->
// setActive(true) and NEVER calls connect(), so dataExchangeHandler_ is null in
// every plugin-side test. Returning early on a null handler would leave
// cloudFramePublishAttemptCountForTest() reading 0 and pendingFrame_ never
// written, on a CORRECT implementation - i.e. SC-001 arm A, SC-006, SC-007,
// SC-008, SC-014, SC-017 and SC-026 would all be unrunnable. A null handler is
// therefore accounted as a SKIPPED BLOCK, which C-2 clause 7 already says is
// recorded and never gating.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free, I/O-free.
//      A bounded <= 64-iteration read loop over const accessors that are plain
//      array indexes with a bounds test (harmonic_cloud.h:950-993), plus one
//      808-byte memcpy. No transcendental anywhere: fundamentalHz is a member
//      read (harmonic_cloud.h:405), not a 440 * exp2((note - 69)/12).
void Processor::publishCloudFrame() noexcept {
    // SC-009(b)/SC-010(b). The scoped timer opens OUTSIDE the gate predicate and
    // its divisor counts EVERY process() call, not every publish - that is what
    // keeps SC-010(b)'s reasoning honest, because with the gate closed the
    // measured stage time is exactly the cost of TESTING the gate.
    // Instrumentation is OFF by default, so no shipping path reads the clock.
    const bool instrumented = cloudFrameInstrumented_;
    std::chrono::steady_clock::time_point stageStart{};
    if (instrumented) {
        stageStart = std::chrono::steady_clock::now();
        ++cloudFrameStageProcessCalls_;
    }

    // --- 1. the gate, and it is the ONLY short-circuit --------------------
    //
    // Written as a WRAPPING `if` rather than an early return purely so the
    // instrumentation epilogue below is written once: the semantics are
    // identical, and the gate is still the only thing that can skip the body.
    //
    // There is deliberately NO engine_ null test: process() returns before the
    // slice loop unless prepared_ && engine_ && reverb_ (see its FR-030
    // early-outs), and this function is called only from after that loop. A
    // restated guard here would sit between the gate and the attempt counter and
    // break the normative body order for a branch that cannot be taken.
    if (cloudFrameEnabled_.load(std::memory_order_relaxed)) {
        // --- 2. the ATTEMPT counter ---------------------------------------
        ++cloudFramePublishAttempts_;

        // --- 3. the focus voice, C-2 clause 4 -----------------------------
        // (a) among slots the allocator does not report Idle, the one with the
        //     GREATEST allocation serial. Ties are impossible - the serial is
        //     documented "strictly increasing across note events"
        //     (seraphis_engine.h:1020-1031).
        // (b) else RETAIN the previous focus while its level is still above
        //     kCloudFrameSilenceLevel, so a release still animates.
        // (c) else slot 0.
        //
        // The loop bound is kMaxVoices, not getPolyphony(): a slot the allocator
        // has already handed out must not drop off the rule when polyphony
        // shrinks underneath it. At kPolyphonyId = 1 no slot above 0 is ever
        // allocated, so every arm degenerates to focus 0 by construction.
        std::size_t focus = 0;
        std::uint64_t bestSerial = 0;
        bool haveNonIdle = false;
        for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
            if (engine_->getVoiceState(v) == Krate::DSP::VoiceState::Idle) {
                continue;
            }
            const std::uint64_t serial = engine_->getVoiceAllocationSerial(v);
            if (!haveNonIdle || serial > bestSerial) {
                bestSerial = serial;
                focus = v;
                haveNonIdle = true;
            }
        }
        if (!haveNonIdle) {
            const std::size_t previous = cloudFrameFocusVoice_;
            focus =
                (engine_->getVoiceLevel(previous) > kCloudFrameSilenceLevel) ? previous : 0u;
        }
        cloudFrameFocusVoice_ = focus;

        // --- 4. fill the MEMBER frame, field by field ---------------------
        const Krate::DSP::HarmonicCloud& cloud = engine_->getVoice(focus).cloud();
        const std::size_t activeVoices = engine_->getActiveVoiceCount();
        const std::size_t n =
            std::min(cloud.getActivePartialCount(), Krate::DSP::HarmonicCloud::kMaxPartials);

        pendingFrame_.sequence = ++cloudFrameSequence_;
        pendingFrame_.activeVoices = static_cast<std::uint16_t>(activeVoices);
        pendingFrame_.focusVoice = static_cast<std::uint8_t>(focus);
        pendingFrame_.partialCount = static_cast<std::uint8_t>(n);
        // THE UNDETUNED f0, and NEVER frequencyHz[0] - which is drift-inclusive
        // by C-2 clause 3 and would silently defeat Q6/SC-024's drift-exclusion
        // guarantee. getFundamentalHz() (harmonic_cloud.h:405) returns the shadow
        // written only by setFundamentalHz (:383), whose sole caller in the whole
        // tree is SeraphisVoice::noteOn (seraphis_voice.h:529).
        //
        // The 0.0f-with-no-voice branch is DELIBERATE (SC-024 arm B): the shadow
        // otherwise retains the last note forever and the view's C4 fallback
        // becomes unreachable.
        pendingFrame_.fundamentalHz = (activeVoices > 0) ? cloud.getFundamentalHz() : 0.0f;
        pendingFrame_.voiceLevel = engine_->getVoiceLevel(focus);
        pendingFrame_.morphTravelPosition =
            engine_->getVoice(focus).morph().getTravelPosition();

        for (std::size_t i = 0; i < n; ++i) {
            // DRIFT-INCLUSIVE (C-2 clause 3): getPartialFrequencyHz is the
            // UNDETUNED synthesized frequency (harmonic_cloud.h:955) and
            // getPartialDriftDetune is a MULTIPLIER (:991), so the view draws
            // what is actually sounding.
            pendingFrame_.frequencyHz[i] =
                cloud.getPartialFrequencyHz(i) * cloud.getPartialDriftDetune(i);
            pendingFrame_.amplitude[i] =
                cloud.getPartialCurrentAmplitude(i) * cloud.getPartialAntiAliasGain(i);
            pendingFrame_.position[i] = cloud.getPartialPosition(i);  // already [-1, +1]
        }
        // NEVER STALE: entries above the active count are zeroed on every
        // publish, so a cloud that shrank cannot leave a previous frame's
        // partials on screen.
        for (std::size_t i = n; i < Krate::DSP::HarmonicCloud::kMaxPartials; ++i) {
            pendingFrame_.frequencyHz[i] = 0.0f;
            pendingFrame_.amplitude[i] = 0.0f;
            pendingFrame_.position[i] = 0.0f;
        }

        const std::uint64_t maskBits = partialMaskBits_.load(std::memory_order_relaxed);
        const std::uint64_t panBits = partialPanOverrideBits_.load(std::memory_order_relaxed);
        pendingFrame_.maskBits = maskBits;
        // "pan AND/OR mask" - FR-030's per-entry override flag read back out.
        pendingFrame_.overriddenBits = panBits | maskBits;

        // --- 5. TRANSPORT ONLY FROM HERE DOWN -----------------------------
        if (dataExchangeHandler_ == nullptr) {
            ++cloudFrameSkippedBlocks_;  // no queue: same accounting as no block
        } else {
            const Vst::DataExchangeBlock block = dataExchangeHandler_->getCurrentOrNewBlock();
            if (block.blockID == Vst::InvalidDataExchangeBlockID || block.data == nullptr
                || block.size < sizeof(CloudFrame)) {
                // RECORDED, never gating (C-2 clause 7): skipped, never retried,
                // never blocked on. At 512/48 kHz the ~94 Hz publish rate
                // deliberately outruns the 30 Hz consume rate, so this is
                // expected steady-state behaviour.
                ++cloudFrameSkippedBlocks_;
            } else {
                std::memcpy(block.data, &pendingFrame_, sizeof(CloudFrame));
                dataExchangeHandler_->sendCurrentBlock();
            }
        }
    }

    if (instrumented) {
        cloudFrameStageNs_ += std::chrono::duration<double, std::nano>(
                                  std::chrono::steady_clock::now() - stageStart)
                                  .count();
    }
}

}  // namespace Seraphis
