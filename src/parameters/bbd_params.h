#pragma once

// ==============================================================================
// BBD Delay Parameters
// ==============================================================================
// Parameter pack for BBD Delay (spec 025)
// ID Range: 500-599
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
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

struct BBDParams {
    std::atomic<float> delayTime{300.0f};      // 20-1000ms
    std::atomic<float> feedback{0.4f};         // 0-1.2
    std::atomic<float> modulationDepth{0.0f};  // 0-1
    std::atomic<float> modulationRate{0.5f};   // 0.1-10Hz
    std::atomic<float> age{0.2f};              // 0-1
    std::atomic<int> era{0};                   // 0-3 (MN3005, MN3007, MN3205, SAD1024)
    std::atomic<float> mix{0.5f};              // 0-1
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleBBDParamChange(
    BBDParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) {

    using namespace Steinberg;

    switch (id) {
        case kBBDDelayTimeId:
            // 20-1000ms
            params.delayTime.store(
                static_cast<float>(20.0 + normalizedValue * 980.0),
                std::memory_order_relaxed);
            break;
        case kBBDFeedbackId:
            // 0-1.2
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.2),
                std::memory_order_relaxed);
            break;
        case kBBDModDepthId:
            // 0-1
            params.modulationDepth.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kBBDModRateId:
            // 0.1-10Hz
            params.modulationRate.store(
                static_cast<float>(0.1 + normalizedValue * 9.9),
                std::memory_order_relaxed);
            break;
        case kBBDAgeId:
            // 0-1
            params.age.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kBBDEraId:
            // 0-3
            params.era.store(
                static_cast<int>(normalizedValue * 3.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kBBDMixId:
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

inline void registerBBDParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Delay Time (20-1000ms)
    parameters.addParameter(
        STR16("BBD Delay Time"),
        STR16("ms"),
        0,
        0.286,  // default: 300ms normalized = (300-20)/980
        ParameterInfo::kCanAutomate,
        kBBDDelayTimeId);

    // Feedback (0-120%)
    parameters.addParameter(
        STR16("BBD Feedback"),
        STR16("%"),
        0,
        0.333,  // default: 40% normalized = 0.4/1.2
        ParameterInfo::kCanAutomate,
        kBBDFeedbackId);

    // Modulation Depth (0-100%)
    parameters.addParameter(
        STR16("BBD Mod Depth"),
        STR16("%"),
        0,
        0,  // default: 0%
        ParameterInfo::kCanAutomate,
        kBBDModDepthId);

    // Modulation Rate (0.1-10Hz)
    parameters.addParameter(
        STR16("BBD Mod Rate"),
        STR16("Hz"),
        0,
        0.040,  // default: 0.5Hz normalized = (0.5-0.1)/9.9
        ParameterInfo::kCanAutomate,
        kBBDModRateId);

    // Age (0-100%)
    parameters.addParameter(
        STR16("BBD Age"),
        STR16("%"),
        0,
        0.2,  // default: 20%
        ParameterInfo::kCanAutomate,
        kBBDAgeId);

    // Era (4 chip models) - MUST use StringListParameter for correct toPlain()
    // DIAGNOSTIC: Using explicit calls like Mode selector to test if helper is the issue
    {
        auto* eraParam = new Steinberg::Vst::StringListParameter(
            STR16("BBD Era"),
            kBBDEraId,
            nullptr,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsList
        );
        eraParam->appendString(STR16("MN3005"));
        eraParam->appendString(STR16("MN3007"));
        eraParam->appendString(STR16("MN3205"));
        eraParam->appendString(STR16("SAD1024"));
        parameters.addParameter(eraParam);
    }

    // Mix (0-100%)
    parameters.addParameter(
        STR16("BBD Mix"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kBBDMixId);
}

// ==============================================================================
// Parameter Display Formatting (for Controller)
// ==============================================================================

inline Steinberg::tresult formatBBDParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        case kBBDDelayTimeId: {
            float ms = static_cast<float>(20.0 + normalizedValue * 980.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kBBDFeedbackId: {
            float percent = static_cast<float>(normalizedValue * 120.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kBBDModDepthId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kBBDModRateId: {
            float hz = static_cast<float>(0.1 + normalizedValue * 9.9);
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", hz);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kBBDAgeId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // kBBDEraId: handled by StringListParameter::toString() automatically

        case kBBDMixId: {
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

inline void saveBBDParams(const BBDParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.delayTime.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modulationDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modulationRate.load(std::memory_order_relaxed));
    streamer.writeFloat(params.age.load(std::memory_order_relaxed));
    streamer.writeInt32(params.era.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
}

inline void loadBBDParams(BBDParams& params, Steinberg::IBStreamer& streamer) {
    float delayTime = 300.0f;
    streamer.readFloat(delayTime);
    params.delayTime.store(delayTime, std::memory_order_relaxed);

    float feedback = 0.4f;
    streamer.readFloat(feedback);
    params.feedback.store(feedback, std::memory_order_relaxed);

    float modulationDepth = 0.0f;
    streamer.readFloat(modulationDepth);
    params.modulationDepth.store(modulationDepth, std::memory_order_relaxed);

    float modulationRate = 0.5f;
    streamer.readFloat(modulationRate);
    params.modulationRate.store(modulationRate, std::memory_order_relaxed);

    float age = 0.2f;
    streamer.readFloat(age);
    params.age.store(age, std::memory_order_relaxed);

    Steinberg::int32 era = 0;
    streamer.readInt32(era);
    params.era.store(era, std::memory_order_relaxed);

    float mix = 0.5f;
    streamer.readFloat(mix);
    params.mix.store(mix, std::memory_order_relaxed);
}

// ==============================================================================
// Controller State Sync (from IBStreamer)
// ==============================================================================

inline void syncBBDParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Delay Time: 20-1000ms -> normalized = (val-20)/980
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kBBDDelayTimeId,
            static_cast<double>((floatVal - 20.0f) / 980.0f));
    }

    // Feedback: 0-1.2 -> normalized = val/1.2
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kBBDFeedbackId,
            static_cast<double>(floatVal / 1.2f));
    }

    // Modulation Depth: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kBBDModDepthId,
            static_cast<double>(floatVal));
    }

    // Modulation Rate: 0.1-10Hz -> normalized = (val-0.1)/9.9
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kBBDModRateId,
            static_cast<double>((floatVal - 0.1f) / 9.9f));
    }

    // Age: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kBBDAgeId,
            static_cast<double>(floatVal));
    }

    // Era: 0-3 -> normalized = val/3
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kBBDEraId,
            static_cast<double>(intVal) / 3.0);
    }

    // Mix: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kBBDMixId,
            static_cast<double>(floatVal));
    }
}

} // namespace Iterum
