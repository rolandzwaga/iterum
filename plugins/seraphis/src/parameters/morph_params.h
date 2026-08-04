#pragma once

// ==============================================================================
// Seraphis - Spectral Morph / Entropy Parameters (ID 400-412)
// ==============================================================================
// The six-function contract (global_params.h:85, :124, :160, :202, :209, :232)
// with ONE NAMED EXCEPTION, plan 2.3.0: both loaders diverge, because this pack's
// state block carries the four 541-byte SpectralState payloads of spec C-8 and
// FR-041b forbids a SpectralState inside an atomic pack.
//
//   loadMorphParams(MorphParams&, IBStreamer&, std::array<SpectralState,4>&)
//       PROCESSOR side. The third parameter is the payloads' destination - a
//       Processor-owned staging buffer (plan 3.7), never a field here.
//
//   loadMorphParamsToController(IBStreamer&, SetParamFunc,
//                               std::array<SpectralState,4>& mirror)
//       CONTROLLER side. PHASE 11 T018 (FR-046 re-seed source 2) gave it the
//       third parameter and stopped it discarding the payloads: they now land in
//       the controller's DISPLAY-ONLY slotMirror_, without which a reloaded
//       project would draw the factory states the dropdowns name while the
//       processor rendered the user's edited ones. The two-argument form is kept
//       as an overload, so no other caller changes shape.
//
//       The body MUST STILL CONSUME all 4 x 541 = 2164 payload bytes whatever
//       happens to their contents. A loader that stopped after the 13 scalars -
//       or that bailed out of the payload loop on a REJECTED record - would
//       leave the cursor short and the following [life]/[body]/[atmos]/[aether]
//       blocks (55 parameters) would each be read from the wrong offset.
//
// saveMorphParams writes THE THIRTEEN SCALARS ONLY. It has no payload source by
// construction (same FR-041b rule), so the caller writes the four payloads
// immediately after it, exactly as it writes saveGlobalSeed after [macro]. See
// plan 5.1's [morph] block and 5.4's encoder.
// ==============================================================================

#include "parameters/dropdown_mappings.h"  // kStateCountLabels, kSpectralStateLabels, ...
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/processors/spline_trajectory.h>
#include <krate/dsp/systems/spectral_morph_engine.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>

namespace Seraphis {

// ==============================================================================
// C-6 plain ranges and defaults
// ==============================================================================

inline constexpr double kMorphBloomMin = 0.0;
inline constexpr double kMorphBloomMax =
    static_cast<double>(Krate::DSP::SpectralMorphEngine::kMaxBloomFraction);   // :100
inline constexpr double kMorphPositionMin = 0.0;
inline constexpr double kMorphPositionMax =
    static_cast<double>(Krate::DSP::SpectralMorphEngine::kMaxStates - 1);      // :97
inline constexpr double kMorphTravelRateMin =
    static_cast<double>(Krate::DSP::SpectralMorphEngine::kMinTravelRate);      // :101
inline constexpr double kMorphTravelRateMax =
    static_cast<double>(Krate::DSP::SpectralMorphEngine::kMaxTravelRate);      // :102
inline constexpr double kMorphWaypointMin =
    static_cast<double>(Krate::DSP::SplineTrajectory::kMinInterval);           // :117
inline constexpr double kMorphWaypointMax =
    static_cast<double>(Krate::DSP::SplineTrajectory::kMaxInterval);           // :119

inline constexpr double kMorphEntropyDefault      = 0.20;
inline constexpr double kMorphBloomDefault        = 0.0;
inline constexpr double kMorphPositionDefault     = 0.0;
inline constexpr double kMorphTravelRateDefault   = kMorphTravelRateMin;
/// The DSP constant, never a re-typed 2.0 (plan 2.3.1 rule 1): SplineTrajectory
/// seeds interval_ from it (spline_trajectory.h:123, :275), so a drift here
/// would make the registered default disagree with the shipped spline.
inline constexpr double kMorphWaypointDefault =
    static_cast<double>(Krate::DSP::SplineTrajectory::kDefaultInterval);       // :123

/// C-6 default INDICES for the five dropdowns / the one toggle.
inline constexpr int kMorphTravelModeDefaultIndex = 0;  ///< External
inline constexpr int kMorphSyncNoteDefaultIndex   = 4;  ///< "1 Bar" (C-7)
inline constexpr int kMorphStateCountDefaultIndex = 0;  ///< index 0 -> count 2
inline constexpr bool kMorphSyncDefault           = false;

/// C-6's four factory-slot defaults: SineStack, Glass, SineStack, SineStack.
inline constexpr std::array<int, 4> kMorphSlotDefaultIndices = {0, 3, 0, 0};

/// ID 408 stores the COUNT, not the list index: Processor::
/// pushSpectralStatesIfPending hands morphParams_.stateCount straight to
/// SeraphisEngine::applySpectralStates(count) (plan 3.3), and
/// dropdown_mappings.h:132-135 records that index i selects count i + kMinStates.
inline constexpr int kMorphStateCountMin = Krate::DSP::SpectralMorphEngine::kMinStates;
inline constexpr int kMorphStateCountMax = Krate::DSP::SpectralMorphEngine::kMaxStates;

// ==============================================================================
// Parameter Storage - 5 float + 4 int/bool + 4 int atomics
// ==============================================================================

struct MorphParams {
    std::atomic<float> entropy{static_cast<float>(kMorphEntropyDefault)};
    std::atomic<float> bloom{static_cast<float>(kMorphBloomDefault)};
    std::atomic<float> position{static_cast<float>(kMorphPositionDefault)};
    std::atomic<float> travelRate{static_cast<float>(kMorphTravelRateDefault)};
    std::atomic<float> waypointSeconds{static_cast<float>(kMorphWaypointDefault)};

    std::atomic<int> travelMode{kMorphTravelModeDefaultIndex};
    /// ID 405. Stored as a bool because every consumer reads it as one
    /// (plan.md:1957); it is still PERSISTED as an int32, so plan 2.3's
    /// "4 x int32" state row is unchanged.
    std::atomic<bool> sync{kMorphSyncDefault};
    std::atomic<int> syncNote{kMorphSyncNoteDefaultIndex};
    std::atomic<int> stateCount{kMorphStateCountMin + kMorphStateCountDefaultIndex};

    /// IDs 409-412. The factory-state INDEX (SpectralStateId), which is what
    /// factoryStates_[...] is subscripted with (plan.md:2034).
    std::array<std::atomic<int>, 4> slot{{{kMorphSlotDefaultIndices[0]},
                                          {kMorphSlotDefaultIndices[1]},
                                          {kMorphSlotDefaultIndices[2]},
                                          {kMorphSlotDefaultIndices[3]}}};
};

// ==============================================================================
// Parameter Change Handler (FR-017, FR-018)
// ==============================================================================

namespace detail {

/// The `L` denormalization form of plan 2.3.1, shared by this pack's five
/// dropdowns. Named inside `Seraphis::detail` so no other pack collides with it.
[[nodiscard]] inline int morphDropdownIndex(Steinberg::Vst::ParamValue value,
                                            int entryCount) noexcept {
    return std::clamp(static_cast<int>(value * static_cast<double>(entryCount - 1) + 0.5),
                      0, entryCount - 1);
}

}  // namespace detail

inline void handleMorphParamChange(
    MorphParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    switch (id) {
        case kMorphEntropyId:
            params.entropy.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                                 std::memory_order_relaxed);
            break;
        case kMorphBloomId:
            params.bloom.store(
                std::clamp(static_cast<float>(kMorphBloomMin +
                                              value * (kMorphBloomMax - kMorphBloomMin)),
                           static_cast<float>(kMorphBloomMin),
                           static_cast<float>(kMorphBloomMax)),
                std::memory_order_relaxed);
            break;
        case kMorphPositionId:
            params.position.store(
                std::clamp(static_cast<float>(kMorphPositionMin +
                                              value * (kMorphPositionMax - kMorphPositionMin)),
                           static_cast<float>(kMorphPositionMin),
                           static_cast<float>(kMorphPositionMax)),
                std::memory_order_relaxed);
            break;
        case kMorphTravelModeId:
            params.travelMode.store(
                detail::morphDropdownIndex(value,
                                           static_cast<int>(kTravelModeLabels.size())),
                std::memory_order_relaxed);
            break;
        case kMorphTravelRateId:
            params.travelRate.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kMorphTravelRateMin, kMorphTravelRateMax)),
                std::memory_order_relaxed);
            break;
        case kMorphSyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kMorphSyncNoteId:
            params.syncNote.store(
                detail::morphDropdownIndex(value,
                                           static_cast<int>(kSyncNoteLabels.size())),
                std::memory_order_relaxed);
            break;
        case kMorphWaypointIntervalId:
            params.waypointSeconds.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kMorphWaypointMin, kMorphWaypointMax)),
                std::memory_order_relaxed);
            break;
        case kMorphStateCountId:
            params.stateCount.store(
                kMorphStateCountMin +
                    detail::morphDropdownIndex(
                        value, static_cast<int>(kStateCountLabels.size())),
                std::memory_order_relaxed);
            break;
        case kMorphState0Id:
        case kMorphState1Id:
        case kMorphState2Id:
        case kMorphState3Id:
            params.slot[static_cast<std::size_t>(id - kMorphState0Id)].store(
                detail::morphDropdownIndex(
                    value, static_cast<int>(kSpectralStateLabels.size())),
                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration (FR-016, FR-048 - the registered types are FROZEN)
// ==============================================================================

// This pack's five dropdown IDs (403, 406, 408, 409-412) register through
// Seraphis::addDropdownParam (dropdown_mappings.h), the single path that pins
// info.defaultNormalizedValue - C-6 gives 406 ("1 Bar") and 410 ("Glass") a
// NON-ZERO default index, which the shared helper leaves at 0.

inline void registerMorphParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Morph Entropy"), STR16(""), 0,
                            kMorphEntropyDefault,
                            ParameterInfo::kCanAutomate, kMorphEntropyId);
    parameters.addParameter(STR16("Morph Bloom"), STR16(""), 0,
                            (kMorphBloomDefault - kMorphBloomMin) /
                                (kMorphBloomMax - kMorphBloomMin),
                            ParameterInfo::kCanAutomate, kMorphBloomId);
    parameters.addParameter(STR16("Morph Position"), STR16(""), 0,
                            (kMorphPositionDefault - kMorphPositionMin) /
                                (kMorphPositionMax - kMorphPositionMin),
                            ParameterInfo::kCanAutomate, kMorphPositionId);

    addDropdownParam(parameters, STR16("Morph Travel Mode"), kMorphTravelModeId,
                        kMorphTravelModeDefaultIndex, kTravelModeLabels.data(),
                        static_cast<int>(kTravelModeLabels.size()));

    parameters.addParameter(STR16("Morph Travel Rate"), STR16("j/s"), 0,
                            Krate::Plugins::logMapToNormalized(kMorphTravelRateDefault,
                                                               kMorphTravelRateMin,
                                                               kMorphTravelRateMax),
                            ParameterInfo::kCanAutomate, kMorphTravelRateId);

    // ID 405: a stepped toggle, exactly like kSoftLimitId (global_params.h:142).
    parameters.addParameter(STR16("Morph Sync"), STR16(""), 1,
                            kMorphSyncDefault ? 1.0 : 0.0,
                            ParameterInfo::kCanAutomate, kMorphSyncId);

    addDropdownParam(parameters, STR16("Morph Sync Note"), kMorphSyncNoteId,
                        kMorphSyncNoteDefaultIndex, kSyncNoteLabels.data(),
                        static_cast<int>(kSyncNoteLabels.size()));

    parameters.addParameter(STR16("Morph Waypoint Interval"), STR16("s"), 0,
                            Krate::Plugins::logMapToNormalized(kMorphWaypointDefault,
                                                               kMorphWaypointMin,
                                                               kMorphWaypointMax),
                            ParameterInfo::kCanAutomate, kMorphWaypointIntervalId);

    addDropdownParam(parameters, STR16("Morph State Count"), kMorphStateCountId,
                        kMorphStateCountDefaultIndex, kStateCountLabels.data(),
                        static_cast<int>(kStateCountLabels.size()));

    addDropdownParam(parameters, STR16("Morph State 1"), kMorphState0Id,
                        kMorphSlotDefaultIndices[0], kSpectralStateLabels.data(),
                        static_cast<int>(kSpectralStateLabels.size()));
    addDropdownParam(parameters, STR16("Morph State 2"), kMorphState1Id,
                        kMorphSlotDefaultIndices[1], kSpectralStateLabels.data(),
                        static_cast<int>(kSpectralStateLabels.size()));
    addDropdownParam(parameters, STR16("Morph State 3"), kMorphState2Id,
                        kMorphSlotDefaultIndices[2], kSpectralStateLabels.data(),
                        static_cast<int>(kSpectralStateLabels.size()));
    addDropdownParam(parameters, STR16("Morph State 4"), kMorphState3Id,
                        kMorphSlotDefaultIndices[3], kSpectralStateLabels.data(),
                        static_cast<int>(kSpectralStateLabels.size()));
}

// ==============================================================================
// Display Formatting
// ==============================================================================
// FR-061: the five StringListParameters (403, 406, 408, 409-412) format
// THEMSELVES and MUST NOT be claimed here.

inline Steinberg::tresult formatMorphParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    char8 text[32];

    switch (id) {
        case kMorphEntropyId:
            snprintf(text, sizeof(text), "%.0f%%", std::clamp(value, 0.0, 1.0) * 100.0);
            break;
        case kMorphBloomId:
            snprintf(text, sizeof(text), "%.2f",
                     kMorphBloomMin + value * (kMorphBloomMax - kMorphBloomMin));
            break;
        case kMorphPositionId:
            snprintf(text, sizeof(text), "%.2f",
                     kMorphPositionMin + value * (kMorphPositionMax - kMorphPositionMin));
            break;
        case kMorphTravelRateId:
            snprintf(text, sizeof(text), "%.4f j/s",
                     Krate::Plugins::logMapFromNormalized(value, kMorphTravelRateMin,
                                                          kMorphTravelRateMax));
            break;
        case kMorphSyncId:
            snprintf(text, sizeof(text), "%s", (value >= 0.5) ? "Synced" : "Free");
            break;
        case kMorphWaypointIntervalId:
            snprintf(text, sizeof(text), "%.2f s",
                     Krate::Plugins::logMapFromNormalized(value, kMorphWaypointMin,
                                                          kMorphWaypointMax));
            break;
        default:
            return kResultFalse;
    }

    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 52 scalar bytes, plan 5.1's [morph] block order
// ==============================================================================
// 5 floats (400, 401, 402, 404, 407) | 4 int32 (403, 405, 406, 408)
// | 4 int32 factory slot ids (409-412).
//
// THE FOUR 541-BYTE PAYLOADS ARE NOT WRITTEN HERE. There is no payload source in
// MorphParams by construction (FR-041b), so the caller writes them immediately
// after this call, per plan 5.4's encoder. loadMorphParams below DOES read them,
// because its third parameter supplies the destination the writer side lacks.

inline void saveMorphParams(const MorphParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.entropy.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bloom.load(std::memory_order_relaxed));
    streamer.writeFloat(params.position.load(std::memory_order_relaxed));
    streamer.writeFloat(params.travelRate.load(std::memory_order_relaxed));
    streamer.writeFloat(params.waypointSeconds.load(std::memory_order_relaxed));

    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.travelMode.load(std::memory_order_relaxed)));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.syncNote.load(std::memory_order_relaxed)));
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.stateCount.load(std::memory_order_relaxed)));

    for (std::size_t i = 0; i < params.slot.size(); ++i) {
        streamer.writeInt32(
            static_cast<Steinberg::int32>(params.slot[i].load(std::memory_order_relaxed)));
    }
}

/// The WRITER half of plan 5.4's encoder, paired with the decoder inside
/// loadMorphParams below so the two cannot drift apart.
///
/// @p source is the caller's message-thread-safe four-slot array (plan 3.7's
/// published staging buffer, else factoryStates_[morphParams_.slot[i]]) - never
/// the audio-thread-owned spectralSlots_.
///
/// An INVALID state writes 541 ZERO bytes rather than a short record, so the
/// block is always exactly 4 x kSpectralStateBytes and every following field
/// keeps its offset. In Phase 9's factory-selection-only design that path is
/// unreachable (every reachable slot is a makeFactoryState result or a payload
/// this thread itself deserialized), and it exists for robustness against a
/// corrupt in-memory slot only - see plan 5.4.
inline void saveSpectralPayloads(const std::array<Krate::DSP::SpectralState, 4>& source,
                                 Steinberg::IBStreamer& streamer) {
    std::array<std::byte, Krate::DSP::kSpectralStateBytes> buf{};
    for (std::size_t i = 0; i < source.size(); ++i) {
        const std::size_t written =
            Krate::DSP::serializeSpectralState(source[i], buf.data(), buf.size());
        if (written == 0) {
            buf.fill(std::byte{0});
        }
        streamer.writeRaw(buf.data(), static_cast<Steinberg::int32>(buf.size()));
    }
}

/// PROCESSOR-side loader. NAMED EXCEPTION to the six-function contract
/// (plan 2.3.0): @p destination receives the four SpectralState payloads, which
/// FR-041b forbids storing in MorphParams.
///
/// EOF-safe throughout: a short stream leaves every unread scalar at its
/// registered default and every unread slot of @p destination bitwise untouched,
/// and returns false - which is not an error, it is what a version-1 stream does.
inline bool loadMorphParams(MorphParams& params,
                            Steinberg::IBStreamer& streamer,
                            std::array<Krate::DSP::SpectralState, 4>& destination) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (!streamer.readFloat(fv)) { return false; }
    params.entropy.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.bloom.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.position.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.travelRate.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; }
    params.waypointSeconds.store(fv, std::memory_order_relaxed);

    if (!streamer.readInt32(iv)) { return false; }
    params.travelMode.store(
        std::clamp(static_cast<int>(iv), 0, static_cast<int>(kTravelModeLabels.size()) - 1),
        std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; }
    params.sync.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; }
    params.syncNote.store(
        std::clamp(static_cast<int>(iv), 0, static_cast<int>(kSyncNoteLabels.size()) - 1),
        std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; }
    // Clamped for the same reason clampPolyphony exists (global_params.h:65-73):
    // an out-of-range count from a corrupt stream must not reach the engine or
    // FR-042's change detector.
    params.stateCount.store(
        std::clamp(static_cast<int>(iv), kMorphStateCountMin, kMorphStateCountMax),
        std::memory_order_relaxed);

    for (std::size_t i = 0; i < params.slot.size(); ++i) {
        if (!streamer.readInt32(iv)) { return false; }
        params.slot[i].store(
            std::clamp(static_cast<int>(iv), 0,
                       static_cast<int>(kSpectralStateLabels.size()) - 1),
            std::memory_order_relaxed);
    }

    // --- the four payloads (plan 5.4's decoder) ------------------------------
    std::array<std::byte, Krate::DSP::kSpectralStateBytes> buf{};
    const auto payloadBytes = static_cast<Steinberg::TSize>(buf.size());
    for (std::size_t i = 0; i < destination.size(); ++i) {
        if (streamer.readRaw(buf.data(), payloadBytes) != payloadBytes) {
            return false;  // EOF-safe: this and every later slot stay factory.
        }
        // deserializeSpectralState leaves `out` BITWISE UNTOUCHED on rejection
        // (spectral_state.h:274-286), so a bad slot is not a bad preset.
        (void) Krate::DSP::deserializeSpectralState(buf.data(), buf.size(), destination[i]);
    }

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above, THEN decodes the payloads)
// ==============================================================================
// Phase 11 T018 / FR-046. @p mirror receives the four SpectralState payloads -
// the controller's display-only slotMirror_. The scalar half is unchanged, and
// the ORDER matters: the four dropdown values are replayed through @p setParam
// FIRST (which is what re-seeds the mirror from the factory table on the
// controller), and the payloads then overwrite each entry, so a stream's own
// authored state always wins over the factory state its dropdown names.

template <typename SetParamFunc>
inline void loadMorphParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam,
    std::array<Krate::DSP::SpectralState, 4>& mirror) {

    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (streamer.readFloat(fv)) {
        setParam(kMorphEntropyId, std::clamp(static_cast<double>(fv), 0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kMorphBloomId,
                 std::clamp((static_cast<double>(fv) - kMorphBloomMin) /
                                (kMorphBloomMax - kMorphBloomMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kMorphPositionId,
                 std::clamp((static_cast<double>(fv) - kMorphPositionMin) /
                                (kMorphPositionMax - kMorphPositionMin),
                            0.0, 1.0));
    }
    if (streamer.readFloat(fv)) {
        setParam(kMorphTravelRateId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kMorphTravelRateMin,
                                                    kMorphTravelRateMax));
    }
    if (streamer.readFloat(fv)) {
        setParam(kMorphWaypointIntervalId,
                 Krate::Plugins::logMapToNormalized(static_cast<double>(fv),
                                                    kMorphWaypointMin, kMorphWaypointMax));
    }

    if (streamer.readInt32(iv)) {
        setParam(kMorphTravelModeId,
                 static_cast<double>(std::clamp(
                     static_cast<int>(iv), 0,
                     static_cast<int>(kTravelModeLabels.size()) - 1)) /
                     static_cast<double>(kTravelModeLabels.size() - 1));
    }
    if (streamer.readInt32(iv)) {
        setParam(kMorphSyncId, iv != 0 ? 1.0 : 0.0);
    }
    if (streamer.readInt32(iv)) {
        setParam(kMorphSyncNoteId,
                 static_cast<double>(std::clamp(
                     static_cast<int>(iv), 0,
                     static_cast<int>(kSyncNoteLabels.size()) - 1)) /
                     static_cast<double>(kSyncNoteLabels.size() - 1));
    }
    if (streamer.readInt32(iv)) {
        const int count = std::clamp(static_cast<int>(iv), kMorphStateCountMin,
                                     kMorphStateCountMax);
        setParam(kMorphStateCountId,
                 static_cast<double>(count - kMorphStateCountMin) /
                     static_cast<double>(kStateCountLabels.size() - 1));
    }
    for (int i = 0; i < 4; ++i) {
        if (!streamer.readInt32(iv)) { return; }
        setParam(static_cast<Steinberg::Vst::ParamID>(kMorphState0Id + i),
                 static_cast<double>(std::clamp(
                     static_cast<int>(iv), 0,
                     static_cast<int>(kSpectralStateLabels.size()) - 1)) /
                     static_cast<double>(kSpectralStateLabels.size() - 1));
    }

    // --- THE PAYLOAD LOOP, and it is load-bearing ----------------------------
    // Phase 11 T018: what used to be a discard now decodes into @p mirror. The
    // 2164 bytes MUST still leave the cursor whatever the decode does, or the
    // following [life]/[body]/[atmos]/[aether] blocks - 55 parameters - are read
    // from the wrong offset (plan 2.3.0).
    //
    // THE RETURN VALUE IS IGNORED DELIBERATELY. deserializeSpectralState leaves
    // `out` BITWISE UNTOUCHED on rejection (spectral_state.h:274-286), which is
    // exactly the right display fallback - a corrupt payload shows whatever the
    // mirror already held, and the cursor has still advanced the full 541 bytes.
    // Bailing out here on a rejected record would desynchronise every parameter
    // that follows.
    std::array<std::byte, Krate::DSP::kSpectralStateBytes> scratch{};
    const auto payloadBytes = static_cast<Steinberg::TSize>(scratch.size());
    for (std::size_t i = 0; i < mirror.size(); ++i) {
        if (streamer.readRaw(scratch.data(), payloadBytes) != payloadBytes) {
            return;  // EOF-safe: this and every later slot keep their value.
        }
        (void) Krate::DSP::deserializeSpectralState(scratch.data(), scratch.size(), mirror[i]);
    }
}

/// The two-argument form, kept so no pre-Phase-11 caller changes shape: the
/// payloads are consumed into a local mirror that is then thrown away, which is
/// byte-for-byte the behaviour this function had before T018.
template <typename SetParamFunc>
inline void loadMorphParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    std::array<Krate::DSP::SpectralState, 4> discarded{};
    loadMorphParamsToController(streamer, setParam, discarded);
}

}  // namespace Seraphis
