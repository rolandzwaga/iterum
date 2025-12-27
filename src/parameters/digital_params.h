#pragma once

// ==============================================================================
// Digital Delay Parameters
// ==============================================================================
// Parameter pack for Digital Delay (spec 026)
// ID Range: 600-699
// ==============================================================================

#include "plugin_ids.h"
#include "pluginterfaces/base/ftypes.h"
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

struct DigitalParams {
    std::atomic<float> delayTime{500.0f};       // 1-10000ms
    std::atomic<int> timeMode{0};               // 0=Free, 1=Synced
    std::atomic<int> noteValue{4};              // 0-9 (note values)
    std::atomic<float> feedback{0.4f};          // 0-1.2
    std::atomic<int> limiterCharacter{0};       // 0=Soft, 1=Medium, 2=Hard
    std::atomic<int> era{0};                    // 0=Pristine, 1=80s, 2=LoFi
    std::atomic<float> age{0.0f};               // 0-1
    std::atomic<float> modulationDepth{0.0f};   // 0-1
    std::atomic<float> modulationRate{1.0f};    // 0.1-10Hz
    std::atomic<int> modulationWaveform{0};     // 0-5 (waveforms)
    std::atomic<float> mix{0.5f};               // 0-1
    std::atomic<float> outputLevel{1.0f};       // linear gain
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
            // 0-9
            params.noteValue.store(
                static_cast<int>(normalizedValue * 9.0 + 0.5),
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
        case kDigitalOutputLevelId:
            // -96 to +12 dB -> linear
            {
                double dB = -96.0 + normalizedValue * 108.0;
                double linear = (dB <= -96.0) ? 0.0 : std::pow(10.0, dB / 20.0);
                params.outputLevel.store(static_cast<float>(linear), std::memory_order_relaxed);
            }
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

    // Time Mode (Free/Synced)
    auto* timeModeParam = parameters.addParameter(
        STR16("Digital Time Mode"),
        nullptr,
        1,  // stepCount: 2 values
        0,  // default: Free
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList,
        kDigitalTimeModeId);
    if (timeModeParam) {
        timeModeParam->getInfo().stepCount = 1;
    }

    // Note Value
    auto* noteValueParam = parameters.addParameter(
        STR16("Digital Note Value"),
        nullptr,
        9,  // stepCount: 10 values
        0.444,  // default: Quarter note
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList,
        kDigitalNoteValueId);
    if (noteValueParam) {
        noteValueParam->getInfo().stepCount = 9;
    }

    // Feedback (0-120%)
    parameters.addParameter(
        STR16("Digital Feedback"),
        STR16("%"),
        0,
        0.333,  // default: 40%
        ParameterInfo::kCanAutomate,
        kDigitalFeedbackId);

    // Limiter Character
    auto* limiterParam = parameters.addParameter(
        STR16("Digital Limiter"),
        nullptr,
        2,  // stepCount: 3 values
        0,  // default: Soft
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList,
        kDigitalLimiterCharacterId);
    if (limiterParam) {
        limiterParam->getInfo().stepCount = 2;
    }

    // Era
    auto* eraParam = parameters.addParameter(
        STR16("Digital Era"),
        nullptr,
        2,  // stepCount: 3 values
        0,  // default: Pristine
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList,
        kDigitalEraId);
    if (eraParam) {
        eraParam->getInfo().stepCount = 2;
    }

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

    // Modulation Waveform
    auto* waveformParam = parameters.addParameter(
        STR16("Digital Mod Waveform"),
        nullptr,
        5,  // stepCount: 6 values
        0,  // default: Sine
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList,
        kDigitalModWaveformId);
    if (waveformParam) {
        waveformParam->getInfo().stepCount = 5;
    }

    // Mix (0-100%)
    parameters.addParameter(
        STR16("Digital Mix"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kDigitalMixId);

    // Output Level (-96 to +12 dB)
    parameters.addParameter(
        STR16("Digital Output Level"),
        STR16("dB"),
        0,
        0.889,  // default: 0dB
        ParameterInfo::kCanAutomate,
        kDigitalOutputLevelId);
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

        case kDigitalTimeModeId: {
            Steinberg::UString(string, 128).fromAscii(
                normalizedValue >= 0.5 ? "Synced" : "Free");
            return kResultOk;
        }

        case kDigitalNoteValueId: {
            int note = static_cast<int>(normalizedValue * 9.0 + 0.5);
            const char* names[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2T", "1/2", "1/1"};
            Steinberg::UString(string, 128).fromAscii(
                names[note < 0 ? 0 : (note > 9 ? 9 : note)]);
            return kResultOk;
        }

        case kDigitalFeedbackId: {
            float percent = static_cast<float>(normalizedValue * 120.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kDigitalLimiterCharacterId: {
            int limiter = static_cast<int>(normalizedValue * 2.0 + 0.5);
            const char* names[] = {"Soft", "Medium", "Hard"};
            Steinberg::UString(string, 128).fromAscii(
                names[limiter < 0 ? 0 : (limiter > 2 ? 2 : limiter)]);
            return kResultOk;
        }

        case kDigitalEraId: {
            int era = static_cast<int>(normalizedValue * 2.0 + 0.5);
            const char* names[] = {"Pristine", "80s Digital", "Lo-Fi"};
            Steinberg::UString(string, 128).fromAscii(
                names[era < 0 ? 0 : (era > 2 ? 2 : era)]);
            return kResultOk;
        }

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

        case kDigitalModWaveformId: {
            int waveform = static_cast<int>(normalizedValue * 5.0 + 0.5);
            const char* names[] = {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random"};
            Steinberg::UString(string, 128).fromAscii(
                names[waveform < 0 ? 0 : (waveform > 5 ? 5 : waveform)]);
            return kResultOk;
        }

        case kDigitalMixId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kDigitalOutputLevelId: {
            double dB = -96.0 + normalizedValue * 108.0;
            char8 text[32];
            if (dB <= -96.0) {
                snprintf(text, sizeof(text), "-inf dB");
            } else {
                snprintf(text, sizeof(text), "%.1f dB", dB);
            }
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
    streamer.writeFloat(params.outputLevel.load(std::memory_order_relaxed));
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
    params.outputLevel.store(floatVal, std::memory_order_relaxed);
}

// ==============================================================================
// Controller State Sync (from IBStreamer)
// ==============================================================================

inline void syncDigitalParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Delay Time: 1-10000ms -> normalized = (val-1)/9999
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDigitalDelayTimeId,
            static_cast<double>((floatVal - 1.0f) / 9999.0f));
    }

    // Time Mode
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kDigitalTimeModeId, intVal != 0 ? 1.0 : 0.0);
    }

    // Note Value: 0-9 -> normalized = val/9
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kDigitalNoteValueId,
            static_cast<double>(intVal) / 9.0);
    }

    // Feedback: 0-1.2 -> normalized = val/1.2
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDigitalFeedbackId,
            static_cast<double>(floatVal / 1.2f));
    }

    // Limiter Character: 0-2 -> normalized = val/2
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kDigitalLimiterCharacterId,
            static_cast<double>(intVal) / 2.0);
    }

    // Era: 0-2 -> normalized = val/2
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kDigitalEraId,
            static_cast<double>(intVal) / 2.0);
    }

    // Age: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDigitalAgeId,
            static_cast<double>(floatVal));
    }

    // Mod Depth: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDigitalModDepthId,
            static_cast<double>(floatVal));
    }

    // Mod Rate: 0.1-10Hz -> normalized = (val-0.1)/9.9
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDigitalModRateId,
            static_cast<double>((floatVal - 0.1f) / 9.9f));
    }

    // Mod Waveform: 0-5 -> normalized = val/5
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kDigitalModWaveformId,
            static_cast<double>(intVal) / 5.0);
    }

    // Mix: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDigitalMixId,
            static_cast<double>(floatVal));
    }

    // Output Level: linear -> dB -> normalized = (dB+96)/108
    if (streamer.readFloat(floatVal)) {
        double dB = (floatVal <= 0.0f) ? -96.0 : 20.0 * std::log10(floatVal);
        controller.setParamNormalized(kDigitalOutputLevelId,
            (dB + 96.0) / 108.0);
    }
}

} // namespace Iterum
