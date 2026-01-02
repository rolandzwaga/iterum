#pragma once

// ==============================================================================
// Digital Delay Parameters
// ==============================================================================
// Parameter pack for Digital Delay (spec 026)
// ID Range: 600-699
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

struct DigitalParams {
    std::atomic<float> delayTime{500.0f};       // 1-10000ms
    std::atomic<int> timeMode{1};               // 0=Free, 1=Synced (default: Synced)
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};  // 0-19 (note values)
    std::atomic<float> feedback{0.4f};          // 0-1.2
    std::atomic<int> limiterCharacter{0};       // 0=Soft, 1=Medium, 2=Hard
    std::atomic<int> era{0};                    // 0=Pristine, 1=80s, 2=LoFi
    std::atomic<float> age{0.0f};               // 0-1
    std::atomic<float> modulationDepth{0.0f};   // 0-1
    std::atomic<float> modulationRate{1.0f};    // 0.1-10Hz
    std::atomic<int> modulationWaveform{0};     // 0-5 (waveforms)
    std::atomic<float> mix{0.5f};               // 0-1
    std::atomic<float> width{100.0f};           // 0-200% (spec 036)
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleDigitalParamChange(
    DigitalParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) {

    using namespace Steinberg;

    switch (id) {
        case kDigitalDelayTimeId:
            // 1-10000ms
            params.delayTime.store(
                static_cast<float>(1.0 + normalizedValue * 9999.0),
                std::memory_order_relaxed);
            break;
        case kDigitalTimeModeId:
            // 0-1
            params.timeMode.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;
        case kDigitalNoteValueId:
            // 0-19 (note values)
            params.noteValue.store(
                static_cast<int>(normalizedValue * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                std::memory_order_relaxed);
            break;
        case kDigitalFeedbackId:
            // 0-1.2
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.2),
                std::memory_order_relaxed);
            break;
        case kDigitalLimiterCharacterId:
            // 0-2
            params.limiterCharacter.store(
                static_cast<int>(normalizedValue * 2.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kDigitalEraId:
            // 0-2
            params.era.store(
                static_cast<int>(normalizedValue * 2.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kDigitalAgeId:
            // 0-1
            params.age.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kDigitalModDepthId:
            // 0-1
            params.modulationDepth.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kDigitalModRateId:
            // 0.1-10Hz
            params.modulationRate.store(
                static_cast<float>(0.1 + normalizedValue * 9.9),
                std::memory_order_relaxed);
            break;
        case kDigitalModWaveformId:
            // 0-5
            params.modulationWaveform.store(
                static_cast<int>(normalizedValue * 5.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kDigitalMixId:
            // 0-1
            params.mix.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kDigitalWidthId:
            // 0-200%
            params.width.store(
                static_cast<float>(normalizedValue * 200.0),
                std::memory_order_relaxed);
            break;
    }
}

// ==============================================================================
// Parameter Registration (for Controller)
// ==============================================================================

inline void registerDigitalParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Delay Time (1-10000ms)
    parameters.addParameter(
        STR16("Digital Delay Time"),
        STR16("ms"),
        0,
        0.050,  // default: 500ms normalized = (500-1)/9999
        ParameterInfo::kCanAutomate,
        kDigitalDelayTimeId);

    // Time Mode (Free/Synced) - MUST use StringListParameter
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Digital Time Mode"), kDigitalTimeModeId,
        1,  // default: Synced (index 1)
        {STR16("Free"), STR16("Synced")}
    ));

    // Note Value - uses centralized dropdown strings
    parameters.addParameter(createNoteValueDropdown(
        STR16("Digital Note Value"), kDigitalNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // Feedback (0-120%)
    parameters.addParameter(
        STR16("Digital Feedback"),
        STR16("%"),
        0,
        0.333,  // default: 40%
        ParameterInfo::kCanAutomate,
        kDigitalFeedbackId);

    // Limiter Character - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Digital Limiter"), kDigitalLimiterCharacterId,
        {STR16("Soft"), STR16("Medium"), STR16("Hard")}
    ));

    // Era - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Digital Era"), kDigitalEraId,
        {STR16("Pristine"), STR16("80s Digital"), STR16("Lo-Fi")}
    ));

    // Age (0-100%)
    parameters.addParameter(
        STR16("Digital Age"),
        STR16("%"),
        0,
        0,  // default: 0%
        ParameterInfo::kCanAutomate,
        kDigitalAgeId);

    // Modulation Depth (0-100%)
    parameters.addParameter(
        STR16("Digital Mod Depth"),
        STR16("%"),
        0,
        0,  // default: 0%
        ParameterInfo::kCanAutomate,
        kDigitalModDepthId);

    // Modulation Rate (0.1-10Hz)
    parameters.addParameter(
        STR16("Digital Mod Rate"),
        STR16("Hz"),
        0,
        0.091,  // default: 1Hz normalized = (1-0.1)/9.9
        ParameterInfo::kCanAutomate,
        kDigitalModRateId);

    // Modulation Waveform - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Digital Mod Waveform"), kDigitalModWaveformId,
        {STR16("Sine"), STR16("Triangle"), STR16("Saw Up"), STR16("Saw Down"), STR16("Square"), STR16("Random")}
    ));

    // Mix (0-100%)
    parameters.addParameter(
        STR16("Digital Mix"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kDigitalMixId);

    // Width (0-200%)
    parameters.addParameter(
        STR16("Digital Width"),
        STR16("%"),
        0,
        0.5,  // default: 100%
        ParameterInfo::kCanAutomate,
        kDigitalWidthId);
}

// ==============================================================================
// Parameter Display Formatting (for Controller)
// ==============================================================================

inline Steinberg::tresult formatDigitalParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        case kDigitalDelayTimeId: {
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

        // kDigitalTimeModeId: handled by StringListParameter::toString() automatically
        // kDigitalNoteValueId: handled by StringListParameter::toString() automatically

        case kDigitalFeedbackId: {
            float percent = static_cast<float>(normalizedValue * 120.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // kDigitalLimiterCharacterId: handled by StringListParameter::toString() automatically
        // kDigitalEraId: handled by StringListParameter::toString() automatically

        case kDigitalAgeId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kDigitalModDepthId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kDigitalModRateId: {
            float hz = static_cast<float>(0.1 + normalizedValue * 9.9);
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", hz);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // kDigitalModWaveformId: handled by StringListParameter::toString() automatically

        case kDigitalMixId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kDigitalWidthId: {
            float percent = static_cast<float>(normalizedValue * 200.0);
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

inline void saveDigitalParams(const DigitalParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.delayTime.load(std::memory_order_relaxed));
    streamer.writeInt32(params.timeMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeInt32(params.limiterCharacter.load(std::memory_order_relaxed));
    streamer.writeInt32(params.era.load(std::memory_order_relaxed));
    streamer.writeFloat(params.age.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modulationDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modulationRate.load(std::memory_order_relaxed));
    streamer.writeInt32(params.modulationWaveform.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.width.load(std::memory_order_relaxed));
}

inline void loadDigitalParams(DigitalParams& params, Steinberg::IBStreamer& streamer) {
    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    streamer.readFloat(floatVal);
    params.delayTime.store(floatVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.timeMode.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.noteValue.store(intVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.feedback.store(floatVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.limiterCharacter.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.era.store(intVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.age.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.modulationDepth.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.modulationRate.store(floatVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.modulationWaveform.store(intVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.mix.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.width.store(floatVal, std::memory_order_relaxed);
}

// ==============================================================================
// Controller State Sync (from IBStreamer)
// ==============================================================================
// Template function that reads stream values and calls a callback with
// (paramId, normalizedValue). This allows both syncDigitalParamsToController
// and loadComponentStateWithNotify to use the same parsing logic.
// ==============================================================================

template<typename SetParamFunc>
inline void loadDigitalParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    using namespace Steinberg;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Delay Time: 1-10000ms -> normalized = (val-1)/9999
    if (streamer.readFloat(floatVal)) {
        setParam(kDigitalDelayTimeId, static_cast<double>((floatVal - 1.0f) / 9999.0f));
    }

    // Time Mode
    if (streamer.readInt32(intVal)) {
        setParam(kDigitalTimeModeId, intVal != 0 ? 1.0 : 0.0);
    }

    // Note Value: 0-19 -> normalized = val/19
    if (streamer.readInt32(intVal)) {
        setParam(kDigitalNoteValueId, static_cast<double>(intVal) / (Parameters::kNoteValueDropdownCount - 1));
    }

    // Feedback: 0-1.2 -> normalized = val/1.2
    if (streamer.readFloat(floatVal)) {
        setParam(kDigitalFeedbackId, static_cast<double>(floatVal / 1.2f));
    }

    // Limiter Character: 0-2 -> normalized = val/2
    if (streamer.readInt32(intVal)) {
        setParam(kDigitalLimiterCharacterId, static_cast<double>(intVal) / 2.0);
    }

    // Era: 0-2 -> normalized = val/2
    if (streamer.readInt32(intVal)) {
        setParam(kDigitalEraId, static_cast<double>(intVal) / 2.0);
    }

    // Age: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kDigitalAgeId, static_cast<double>(floatVal));
    }

    // Mod Depth: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kDigitalModDepthId, static_cast<double>(floatVal));
    }

    // Mod Rate: 0.1-10Hz -> normalized = (val-0.1)/9.9
    if (streamer.readFloat(floatVal)) {
        setParam(kDigitalModRateId, static_cast<double>((floatVal - 0.1f) / 9.9f));
    }

    // Mod Waveform: 0-5 -> normalized = val/5
    if (streamer.readInt32(intVal)) {
        setParam(kDigitalModWaveformId, static_cast<double>(intVal) / 5.0);
    }

    // Mix: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kDigitalMixId, static_cast<double>(floatVal));
    }

    // Width: 0-200% -> normalized = val/200
    if (streamer.readFloat(floatVal)) {
        setParam(kDigitalWidthId, static_cast<double>(floatVal / 200.0f));
    }
}

// Convenience wrapper for setComponentState path
inline void syncDigitalParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    loadDigitalParamsToController(streamer, [&](Steinberg::Vst::ParamID id, double val) {
        controller.setParamNormalized(id, val);
    });
}

} // namespace Iterum
