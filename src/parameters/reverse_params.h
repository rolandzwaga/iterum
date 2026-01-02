#pragma once

// ==============================================================================
// Reverse Delay Parameters
// ==============================================================================
// Parameter pack for Reverse Delay (spec 030)
// ID Range: 800-899
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/note_value_ui.h"
#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <atomic>
#include <cmath>

namespace Iterum {

// ==============================================================================
// Parameter Storage
// ==============================================================================

struct ReverseParams {
    std::atomic<float> chunkSize{500.0f};       // 10-2000ms
    std::atomic<int> timeMode{0};               // 0=Free, 1=Synced (spec 043)
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};  // 0-19 (note values)
    std::atomic<float> crossfade{0.5f};         // 0-1 (crossfade amount)
    std::atomic<int> playbackMode{0};           // 0=FullReverse, 1=Alternating, 2=Random
    std::atomic<float> feedback{0.0f};          // 0-1.2
    std::atomic<bool> filterEnabled{false};
    std::atomic<float> filterCutoff{4000.0f};   // 20-20000Hz
    std::atomic<int> filterType{0};             // 0=LowPass, 1=HighPass, 2=BandPass
    std::atomic<float> dryWet{0.5f};            // 0-1
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleReverseParamChange(
    ReverseParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) {

    using namespace Steinberg;

    switch (id) {
        case kReverseChunkSizeId:
            // 10-2000ms
            params.chunkSize.store(
                static_cast<float>(10.0 + normalizedValue * 1990.0),
                std::memory_order_relaxed);
            break;
        case kReverseTimeModeId:
            // 0=Free, 1=Synced
            params.timeMode.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;
        case kReverseNoteValueId:
            // 0-19 (note values)
            params.noteValue.store(
                static_cast<int>(normalizedValue * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                std::memory_order_relaxed);
            break;
        case kReverseCrossfadeId:
            // 0-1 (passthrough)
            params.crossfade.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kReversePlaybackModeId:
            // 0-2 (FullReverse, Alternating, Random)
            params.playbackMode.store(
                static_cast<int>(normalizedValue * 2.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kReverseFeedbackId:
            // 0-1.2
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.2),
                std::memory_order_relaxed);
            break;
        case kReverseFilterEnabledId:
            params.filterEnabled.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;
        case kReverseFilterCutoffId:
            // 20-20000Hz (logarithmic)
            params.filterCutoff.store(
                static_cast<float>(20.0 * std::pow(1000.0, normalizedValue)),
                std::memory_order_relaxed);
            break;
        case kReverseFilterTypeId:
            // 0-2 (LowPass, HighPass, BandPass)
            params.filterType.store(
                static_cast<int>(normalizedValue * 2.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kReverseMixId:
            // 0-1
            params.dryWet.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
    }
}

// ==============================================================================
// Parameter Registration (for Controller)
// ==============================================================================

inline void registerReverseParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Chunk Size (10-2000ms)
    parameters.addParameter(
        STR16("Reverse Chunk Size"),
        STR16("ms"),
        0,
        0.246,  // default: 500ms normalized
        ParameterInfo::kCanAutomate,
        kReverseChunkSizeId);

    // Time Mode (Free/Synced) - spec 043
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Reverse Time Mode"), kReverseTimeModeId,
        0,  // default: Free (index 0)
        {STR16("Free"), STR16("Synced")}
    ));

    // Note Value - uses centralized dropdown strings (spec 043)
    parameters.addParameter(createNoteValueDropdown(
        STR16("Reverse Note Value"), kReverseNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // Crossfade (0-100%)
    parameters.addParameter(
        STR16("Reverse Crossfade"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kReverseCrossfadeId);

    // Playback Mode (FullReverse, Alternating, Random) - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Reverse Playback Mode"), kReversePlaybackModeId,
        {STR16("Full Reverse"), STR16("Alternating"), STR16("Random")}
    ));

    // Feedback (0-120%)
    parameters.addParameter(
        STR16("Reverse Feedback"),
        STR16("%"),
        0,
        0,  // default: 0%
        ParameterInfo::kCanAutomate,
        kReverseFeedbackId);

    // Filter Enabled (on/off)
    parameters.addParameter(
        STR16("Reverse Filter Enable"),
        nullptr,
        1,
        0,  // default: off
        ParameterInfo::kCanAutomate,
        kReverseFilterEnabledId);

    // Filter Cutoff (20-20000Hz)
    parameters.addParameter(
        STR16("Reverse Filter Cutoff"),
        STR16("Hz"),
        0,
        0.434,  // default: ~4000Hz (log scale)
        ParameterInfo::kCanAutomate,
        kReverseFilterCutoffId);

    // Filter Type (LowPass, HighPass, BandPass) - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Reverse Filter Type"), kReverseFilterTypeId,
        {STR16("LowPass"), STR16("HighPass"), STR16("BandPass")}
    ));

    // Dry/Wet Mix (0-100%)
    parameters.addParameter(
        STR16("Reverse Dry/Wet"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kReverseMixId);
}

// ==============================================================================
// Parameter Display Formatting (for Controller)
// ==============================================================================

inline Steinberg::tresult formatReverseParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        case kReverseChunkSizeId: {
            float ms = static_cast<float>(10.0 + normalizedValue * 1990.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kReverseCrossfadeId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // kReversePlaybackModeId: handled by StringListParameter::toString() automatically

        case kReverseFeedbackId: {
            float percent = static_cast<float>(normalizedValue * 120.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kReverseFilterEnabledId:
            Steinberg::UString(string, 128).assign(
                normalizedValue >= 0.5 ? STR16("On") : STR16("Off"));
            return kResultOk;

        case kReverseFilterCutoffId: {
            float hz = static_cast<float>(20.0 * std::pow(1000.0, normalizedValue));
            char8 text[32];
            if (hz >= 1000.0f) {
                snprintf(text, sizeof(text), "%.2f kHz", hz / 1000.0f);
            } else {
                snprintf(text, sizeof(text), "%.0f Hz", hz);
            }
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // kReverseFilterTypeId: handled by StringListParameter::toString() automatically

        case kReverseMixId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }
    }

    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Persistence
// ==============================================================================

inline void saveReverseParams(const ReverseParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.chunkSize.load(std::memory_order_relaxed));
    streamer.writeInt32(params.timeMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeFloat(params.crossfade.load(std::memory_order_relaxed));
    streamer.writeInt32(params.playbackMode.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeInt32(params.filterEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.filterCutoff.load(std::memory_order_relaxed));
    streamer.writeInt32(params.filterType.load(std::memory_order_relaxed));
    streamer.writeFloat(params.dryWet.load(std::memory_order_relaxed));
}

inline void loadReverseParams(ReverseParams& params, Steinberg::IBStreamer& streamer) {
    float chunkSize = 500.0f;
    streamer.readFloat(chunkSize);
    params.chunkSize.store(chunkSize, std::memory_order_relaxed);

    Steinberg::int32 timeMode = 0;
    streamer.readInt32(timeMode);
    params.timeMode.store(timeMode, std::memory_order_relaxed);

    Steinberg::int32 noteValue = 4;
    streamer.readInt32(noteValue);
    params.noteValue.store(noteValue, std::memory_order_relaxed);

    float crossfade = 50.0f;
    streamer.readFloat(crossfade);
    params.crossfade.store(crossfade, std::memory_order_relaxed);

    Steinberg::int32 playbackMode = 0;
    streamer.readInt32(playbackMode);
    params.playbackMode.store(playbackMode, std::memory_order_relaxed);

    float feedback = 0.0f;
    streamer.readFloat(feedback);
    params.feedback.store(feedback, std::memory_order_relaxed);

    Steinberg::int32 filterEnabled = 0;
    streamer.readInt32(filterEnabled);
    params.filterEnabled.store(filterEnabled != 0, std::memory_order_relaxed);

    float filterCutoff = 4000.0f;
    streamer.readFloat(filterCutoff);
    params.filterCutoff.store(filterCutoff, std::memory_order_relaxed);

    Steinberg::int32 filterType = 0;
    streamer.readInt32(filterType);
    params.filterType.store(filterType, std::memory_order_relaxed);

    float dryWet = 0.5f;
    streamer.readFloat(dryWet);
    params.dryWet.store(dryWet, std::memory_order_relaxed);
}

// ==============================================================================
// Controller State Sync (from IBStreamer)
// ==============================================================================

template<typename SetParamFunc>
inline void loadReverseParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    using namespace Steinberg;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Chunk Size: 10-2000ms -> normalized = (val-10)/1990
    if (streamer.readFloat(floatVal)) {
        setParam(kReverseChunkSizeId,
            static_cast<double>((floatVal - 10.0f) / 1990.0f));
    }

    // Time Mode: 0-1 -> normalized = val
    if (streamer.readInt32(intVal)) {
        setParam(kReverseTimeModeId, intVal != 0 ? 1.0 : 0.0);
    }

    // Note Value: 0-19 -> normalized = val/19
    if (streamer.readInt32(intVal)) {
        setParam(kReverseNoteValueId,
            static_cast<double>(intVal) / (Parameters::kNoteValueDropdownCount - 1));
    }

    // Crossfade: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParam(kReverseCrossfadeId,
            static_cast<double>(floatVal));
    }

    // Playback Mode: 0-2 -> normalized = val/2
    if (streamer.readInt32(intVal)) {
        setParam(kReversePlaybackModeId,
            static_cast<double>(intVal) / 2.0);
    }

    // Feedback: 0-1.2 -> normalized = val/1.2
    if (streamer.readFloat(floatVal)) {
        setParam(kReverseFeedbackId,
            static_cast<double>(floatVal / 1.2f));
    }

    // Filter Enabled
    if (streamer.readInt32(intVal)) {
        setParam(kReverseFilterEnabledId, intVal != 0 ? 1.0 : 0.0);
    }

    // Filter Cutoff: 20-20000Hz (log scale) -> normalized = log(val/20)/log(1000)
    if (streamer.readFloat(floatVal)) {
        setParam(kReverseFilterCutoffId,
            std::log(floatVal / 20.0f) / std::log(1000.0f));
    }

    // Filter Type: 0-2 -> normalized = val/2
    if (streamer.readInt32(intVal)) {
        setParam(kReverseFilterTypeId,
            static_cast<double>(intVal) / 2.0);
    }

    // Dry/Wet: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kReverseMixId,
            static_cast<double>(floatVal));
    }
}

inline void syncReverseParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    loadReverseParamsToController(streamer,
        [&controller](Steinberg::Vst::ParamID id, double normalizedValue) {
            controller.setParamNormalized(id, normalizedValue);
        });
}

} // namespace Iterum
