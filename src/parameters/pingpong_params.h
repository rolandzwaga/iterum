#pragma once

// ==============================================================================
// PingPong Delay Parameters
// ==============================================================================
// Parameter pack for PingPong Delay (spec 027)
// ID Range: 700-799
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

namespace Iterum {

// ==============================================================================
// Parameter Storage
// ==============================================================================

struct PingPongParams {
    std::atomic<float> delayTime{500.0f};       // 1-10000ms
    std::atomic<int> timeMode{1};               // 0=Free, 1=Synced (default: Synced)
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};  // 0-19 (note values)
    std::atomic<int> lrRatio{0};                // 0-6 (ratio presets)
    std::atomic<float> feedback{0.5f};          // 0-1.2
    std::atomic<float> crossFeedback{1.0f};     // 0-1 (0=dual mono, 1=full ping-pong)
    std::atomic<float> width{100.0f};           // 0-200%
    std::atomic<float> modulationDepth{0.0f};   // 0-1
    std::atomic<float> modulationRate{1.0f};    // 0.1-10Hz
    std::atomic<float> mix{0.5f};               // 0-1
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handlePingPongParamChange(
    PingPongParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) {

    using namespace Steinberg;

    switch (id) {
        case kPingPongDelayTimeId:
            // 1-10000ms
            params.delayTime.store(
                static_cast<float>(1.0 + normalizedValue * 9999.0),
                std::memory_order_relaxed);
            break;
        case kPingPongTimeModeId:
            // 0-1
            params.timeMode.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;
        case kPingPongNoteValueId:
            // 0-19 (note values)
            params.noteValue.store(
                static_cast<int>(normalizedValue * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                std::memory_order_relaxed);
            break;
        case kPingPongLRRatioId:
            // 0-6
            params.lrRatio.store(
                static_cast<int>(normalizedValue * 6.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kPingPongFeedbackId:
            // 0-1.2
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.2),
                std::memory_order_relaxed);
            break;
        case kPingPongCrossFeedbackId:
            // 0-1
            params.crossFeedback.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kPingPongWidthId:
            // 0-200%
            params.width.store(
                static_cast<float>(normalizedValue * 200.0),
                std::memory_order_relaxed);
            break;
        case kPingPongModDepthId:
            // 0-1
            params.modulationDepth.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kPingPongModRateId:
            // 0.1-10Hz
            params.modulationRate.store(
                static_cast<float>(0.1 + normalizedValue * 9.9),
                std::memory_order_relaxed);
            break;
        case kPingPongMixId:
            // 0-1
            params.mix.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
    }
}

// ==============================================================================
// Parameter Registration (for Controller)
// ==============================================================================

inline void registerPingPongParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Delay Time (1-10000ms)
    parameters.addParameter(
        STR16("PingPong Delay Time"),
        STR16("ms"),
        0,
        0.050,  // default: 500ms
        ParameterInfo::kCanAutomate,
        kPingPongDelayTimeId);

    // Time Mode (Free/Synced) - MUST use StringListParameter
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("PingPong Time Mode"), kPingPongTimeModeId,
        1,  // default: Synced (index 1)
        {STR16("Free"), STR16("Synced")}
    ));

    // Note Value - uses centralized dropdown strings
    parameters.addParameter(createNoteValueDropdown(
        STR16("PingPong Note Value"), kPingPongNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // L/R Ratio - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("PingPong L/R Ratio"), kPingPongLRRatioId,
        {STR16("1:1"), STR16("2:1"), STR16("3:2"), STR16("4:3"), STR16("1:2"), STR16("2:3"), STR16("3:4")}
    ));

    // Feedback (0-120%)
    parameters.addParameter(
        STR16("PingPong Feedback"),
        STR16("%"),
        0,
        0.417,  // default: 50%
        ParameterInfo::kCanAutomate,
        kPingPongFeedbackId);

    // Cross-Feedback (0-100%)
    parameters.addParameter(
        STR16("PingPong Cross-Feedback"),
        STR16("%"),
        0,
        1.0,  // default: 100% (full ping-pong)
        ParameterInfo::kCanAutomate,
        kPingPongCrossFeedbackId);

    // Width (0-200%)
    parameters.addParameter(
        STR16("PingPong Width"),
        STR16("%"),
        0,
        0.5,  // default: 100%
        ParameterInfo::kCanAutomate,
        kPingPongWidthId);

    // Modulation Depth (0-100%)
    parameters.addParameter(
        STR16("PingPong Mod Depth"),
        STR16("%"),
        0,
        0,  // default: 0%
        ParameterInfo::kCanAutomate,
        kPingPongModDepthId);

    // Modulation Rate (0.1-10Hz)
    parameters.addParameter(
        STR16("PingPong Mod Rate"),
        STR16("Hz"),
        0,
        0.091,  // default: 1Hz
        ParameterInfo::kCanAutomate,
        kPingPongModRateId);

    // Mix (0-100%)
    parameters.addParameter(
        STR16("PingPong Mix"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kPingPongMixId);
}

// ==============================================================================
// Parameter Display Formatting (for Controller)
// ==============================================================================

inline Steinberg::tresult formatPingPongParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        case kPingPongDelayTimeId: {
            float ms = static_cast<float>(1.0 + normalizedValue * 9999.0);
            char8 text[32];
            if (ms >= 1000.0f) {
                snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
            } else {
                snprintf(text, sizeof(text), "%.1f ms", ms);
            }
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // kPingPongTimeModeId: handled by StringListParameter::toString() automatically
        // kPingPongNoteValueId: handled by StringListParameter::toString() automatically
        // kPingPongLRRatioId: handled by StringListParameter::toString() automatically

        case kPingPongFeedbackId: {
            float percent = static_cast<float>(normalizedValue * 120.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kPingPongCrossFeedbackId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kPingPongWidthId: {
            float percent = static_cast<float>(normalizedValue * 200.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kPingPongModDepthId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kPingPongModRateId: {
            float hz = static_cast<float>(0.1 + normalizedValue * 9.9);
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", hz);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kPingPongMixId: {
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

inline void savePingPongParams(const PingPongParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.delayTime.load(std::memory_order_relaxed));
    streamer.writeInt32(params.timeMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeInt32(params.lrRatio.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.crossFeedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.width.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modulationDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modulationRate.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
}

inline void loadPingPongParams(PingPongParams& params, Steinberg::IBStreamer& streamer) {
    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    streamer.readFloat(floatVal);
    params.delayTime.store(floatVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.timeMode.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.noteValue.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.lrRatio.store(intVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.feedback.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.crossFeedback.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.width.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.modulationDepth.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.modulationRate.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.mix.store(floatVal, std::memory_order_relaxed);
}

// ==============================================================================
// Controller State Sync (from IBStreamer)
// ==============================================================================
// Template function that reads stream values and calls a callback with
// (paramId, normalizedValue). This allows both syncPingPongParamsToController
// and loadComponentStateWithNotify to use the same parsing logic.
// ==============================================================================

template<typename SetParamFunc>
inline void loadPingPongParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    using namespace Steinberg;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Delay Time: 1-10000ms -> normalized = (val-1)/9999
    if (streamer.readFloat(floatVal)) {
        setParam(kPingPongDelayTimeId, static_cast<double>((floatVal - 1.0f) / 9999.0f));
    }

    // Time Mode
    if (streamer.readInt32(intVal)) {
        setParam(kPingPongTimeModeId, intVal != 0 ? 1.0 : 0.0);
    }

    // Note Value: 0-19 -> normalized = val/19
    if (streamer.readInt32(intVal)) {
        setParam(kPingPongNoteValueId, static_cast<double>(intVal) / (Parameters::kNoteValueDropdownCount - 1));
    }

    // L/R Ratio: 0-6 -> normalized = val/6
    if (streamer.readInt32(intVal)) {
        setParam(kPingPongLRRatioId, static_cast<double>(intVal) / 6.0);
    }

    // Feedback: 0-1.2 -> normalized = val/1.2
    if (streamer.readFloat(floatVal)) {
        setParam(kPingPongFeedbackId, static_cast<double>(floatVal / 1.2f));
    }

    // Cross-Feedback: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kPingPongCrossFeedbackId, static_cast<double>(floatVal));
    }

    // Width: 0-200 -> normalized = val/200
    if (streamer.readFloat(floatVal)) {
        setParam(kPingPongWidthId, static_cast<double>(floatVal / 200.0f));
    }

    // Mod Depth: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kPingPongModDepthId, static_cast<double>(floatVal));
    }

    // Mod Rate: 0.1-10Hz -> normalized = (val-0.1)/9.9
    if (streamer.readFloat(floatVal)) {
        setParam(kPingPongModRateId, static_cast<double>((floatVal - 0.1f) / 9.9f));
    }

    // Mix: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kPingPongMixId, static_cast<double>(floatVal));
    }
}

// Convenience wrapper for setComponentState path
inline void syncPingPongParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    loadPingPongParamsToController(streamer, [&](Steinberg::Vst::ParamID id, double val) {
        controller.setParamNormalized(id, val);
    });
}

} // namespace Iterum
