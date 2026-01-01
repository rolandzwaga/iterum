#pragma once

// ==============================================================================
// MultiTap Delay Parameters
// ==============================================================================
// Parameter pack for MultiTap Delay (spec 028)
// ID Range: 900-999
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

struct MultiTapParams {
    std::atomic<int> timingPattern{2};          // 0-19 (pattern presets)
    std::atomic<int> spatialPattern{2};         // 0-6 (spatial presets)
    std::atomic<int> tapCount{4};               // 2-16 taps
    std::atomic<float> baseTime{500.0f};        // 1-5000ms
    std::atomic<float> tempo{120.0f};           // 20-300 BPM
    std::atomic<float> feedback{0.5f};          // 0-1.1 (110%)
    std::atomic<float> feedbackLPCutoff{20000.0f};  // 20-20000Hz
    std::atomic<float> feedbackHPCutoff{20.0f};     // 20-20000Hz
    std::atomic<float> morphTime{500.0f};       // 50-2000ms
    std::atomic<float> dryWet{50.0f};           // 0-100%
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleMultiTapParamChange(
    MultiTapParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) {

    using namespace Steinberg;

    switch (id) {
        case kMultiTapTimingPatternId:
            // 0-19
            params.timingPattern.store(
                static_cast<int>(normalizedValue * 19.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kMultiTapSpatialPatternId:
            // 0-6
            params.spatialPattern.store(
                static_cast<int>(normalizedValue * 6.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kMultiTapTapCountId:
            // 2-16
            params.tapCount.store(
                static_cast<int>(2.0 + normalizedValue * 14.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kMultiTapBaseTimeId:
            // 1-5000ms
            params.baseTime.store(
                static_cast<float>(1.0 + normalizedValue * 4999.0),
                std::memory_order_relaxed);
            break;
        case kMultiTapTempoId:
            // 20-300 BPM
            params.tempo.store(
                static_cast<float>(20.0 + normalizedValue * 280.0),
                std::memory_order_relaxed);
            break;
        case kMultiTapFeedbackId:
            // 0-1.1 (110%)
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.1),
                std::memory_order_relaxed);
            break;
        case kMultiTapFeedbackLPCutoffId:
            // 20-20000Hz (logarithmic)
            params.feedbackLPCutoff.store(
                static_cast<float>(20.0 * std::pow(1000.0, normalizedValue)),
                std::memory_order_relaxed);
            break;
        case kMultiTapFeedbackHPCutoffId:
            // 20-20000Hz (logarithmic)
            params.feedbackHPCutoff.store(
                static_cast<float>(20.0 * std::pow(1000.0, normalizedValue)),
                std::memory_order_relaxed);
            break;
        case kMultiTapMorphTimeId:
            // 50-2000ms
            params.morphTime.store(
                static_cast<float>(50.0 + normalizedValue * 1950.0),
                std::memory_order_relaxed);
            break;
        case kMultiTapMixId:
            // 0-100%
            params.dryWet.store(
                static_cast<float>(normalizedValue * 100.0),
                std::memory_order_relaxed);
            break;
    }
}

// ==============================================================================
// Parameter Registration (for Controller)
// ==============================================================================

inline void registerMultiTapParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Timing Pattern (20 patterns) - MUST use StringListParameter
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("MultiTap Timing Pattern"), kMultiTapTimingPatternId,
        2,  // default: Quarter (index 2)
        {STR16("Whole"), STR16("Half"), STR16("Quarter"), STR16("Eighth"), STR16("16th"), STR16("32nd"),
         STR16("Dotted Half"), STR16("Dotted Qtr"), STR16("Dotted 8th"), STR16("Dotted 16th"),
         STR16("Triplet Half"), STR16("Triplet Qtr"), STR16("Triplet 8th"), STR16("Triplet 16th"),
         STR16("Golden Ratio"), STR16("Fibonacci"), STR16("Exponential"), STR16("Primes"), STR16("Linear"), STR16("Custom")}
    ));

    // Spatial Pattern (7 patterns) - MUST use StringListParameter
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("MultiTap Spatial Pattern"), kMultiTapSpatialPatternId,
        2,  // default: Centered (index 2)
        {STR16("Cascade"), STR16("Alternating"), STR16("Centered"), STR16("Widening"), STR16("Decaying"), STR16("Flat"), STR16("Custom")}
    ));

    // Tap Count (2-16)
    parameters.addParameter(
        STR16("MultiTap Tap Count"),
        nullptr,
        14,  // 15 values (2-16)
        0.143,  // default: 4 taps normalized = (4-2)/14
        ParameterInfo::kCanAutomate,
        kMultiTapTapCountId);

    // Base Time (1-5000ms)
    parameters.addParameter(
        STR16("MultiTap Base Time"),
        STR16("ms"),
        0,
        0.100,  // default: 500ms normalized = (500-1)/4999
        ParameterInfo::kCanAutomate,
        kMultiTapBaseTimeId);

    // Tempo (20-300 BPM)
    parameters.addParameter(
        STR16("MultiTap Tempo"),
        STR16("BPM"),
        0,
        0.357,  // default: 120 BPM normalized = (120-20)/280
        ParameterInfo::kCanAutomate,
        kMultiTapTempoId);

    // Feedback (0-110%)
    parameters.addParameter(
        STR16("MultiTap Feedback"),
        STR16("%"),
        0,
        0.455,  // default: 50% normalized = 0.5/1.1
        ParameterInfo::kCanAutomate,
        kMultiTapFeedbackId);

    // Feedback LP Cutoff (20-20000Hz)
    parameters.addParameter(
        STR16("MultiTap Feedback LP"),
        STR16("Hz"),
        0,
        1.0,  // default: 20000Hz (max)
        ParameterInfo::kCanAutomate,
        kMultiTapFeedbackLPCutoffId);

    // Feedback HP Cutoff (20-20000Hz)
    parameters.addParameter(
        STR16("MultiTap Feedback HP"),
        STR16("Hz"),
        0,
        0.0,  // default: 20Hz (min)
        ParameterInfo::kCanAutomate,
        kMultiTapFeedbackHPCutoffId);

    // Morph Time (50-2000ms)
    parameters.addParameter(
        STR16("MultiTap Morph Time"),
        STR16("ms"),
        0,
        0.231,  // default: 500ms normalized = (500-50)/1950
        ParameterInfo::kCanAutomate,
        kMultiTapMorphTimeId);

    // Dry/Wet Mix (0-100%)
    parameters.addParameter(
        STR16("MultiTap Dry/Wet"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kMultiTapMixId);
}

// ==============================================================================
// Parameter Display Formatting (for Controller)
// ==============================================================================

inline Steinberg::tresult formatMultiTapParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        // kMultiTapTimingPatternId: handled by StringListParameter::toString() automatically
        // kMultiTapSpatialPatternId: handled by StringListParameter::toString() automatically

        case kMultiTapTapCountId: {
            int count = static_cast<int>(2.0 + normalizedValue * 14.0 + 0.5);
            char8 text[32];
            snprintf(text, sizeof(text), "%d", count);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kMultiTapBaseTimeId: {
            float ms = static_cast<float>(1.0 + normalizedValue * 4999.0);
            char8 text[32];
            if (ms >= 1000.0f) {
                snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
            } else {
                snprintf(text, sizeof(text), "%.1f ms", ms);
            }
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kMultiTapTempoId: {
            float bpm = static_cast<float>(20.0 + normalizedValue * 280.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f BPM", bpm);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kMultiTapFeedbackId: {
            float percent = static_cast<float>(normalizedValue * 110.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kMultiTapFeedbackLPCutoffId:
        case kMultiTapFeedbackHPCutoffId: {
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

        case kMultiTapMorphTimeId: {
            float ms = static_cast<float>(50.0 + normalizedValue * 1950.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kMultiTapMixId: {
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

inline void saveMultiTapParams(const MultiTapParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.timingPattern.load(std::memory_order_relaxed));
    streamer.writeInt32(params.spatialPattern.load(std::memory_order_relaxed));
    streamer.writeInt32(params.tapCount.load(std::memory_order_relaxed));
    streamer.writeFloat(params.baseTime.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tempo.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedbackLPCutoff.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedbackHPCutoff.load(std::memory_order_relaxed));
    streamer.writeFloat(params.morphTime.load(std::memory_order_relaxed));
    streamer.writeFloat(params.dryWet.load(std::memory_order_relaxed));
}

inline void loadMultiTapParams(MultiTapParams& params, Steinberg::IBStreamer& streamer) {
    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    streamer.readInt32(intVal);
    params.timingPattern.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.spatialPattern.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.tapCount.store(intVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.baseTime.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.tempo.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.feedback.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.feedbackLPCutoff.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.feedbackHPCutoff.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.morphTime.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.dryWet.store(floatVal, std::memory_order_relaxed);
}

// ==============================================================================
// Controller State Sync (from IBStreamer)
// ==============================================================================

inline void syncMultiTapParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Timing Pattern: 0-19 -> normalized = val/19
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kMultiTapTimingPatternId,
            static_cast<double>(intVal) / 19.0);
    }

    // Spatial Pattern: 0-6 -> normalized = val/6
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kMultiTapSpatialPatternId,
            static_cast<double>(intVal) / 6.0);
    }

    // Tap Count: 2-16 -> normalized = (val-2)/14
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kMultiTapTapCountId,
            static_cast<double>(intVal - 2) / 14.0);
    }

    // Base Time: 1-5000ms -> normalized = (val-1)/4999
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kMultiTapBaseTimeId,
            static_cast<double>((floatVal - 1.0f) / 4999.0f));
    }

    // Tempo: 20-300 BPM -> normalized = (val-20)/280
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kMultiTapTempoId,
            static_cast<double>((floatVal - 20.0f) / 280.0f));
    }

    // Feedback: 0-1.1 -> normalized = val/1.1
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kMultiTapFeedbackId,
            static_cast<double>(floatVal / 1.1f));
    }

    // Feedback LP Cutoff: 20-20000Hz (log) -> normalized = log(val/20)/log(1000)
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kMultiTapFeedbackLPCutoffId,
            std::log(floatVal / 20.0f) / std::log(1000.0f));
    }

    // Feedback HP Cutoff: 20-20000Hz (log) -> normalized = log(val/20)/log(1000)
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kMultiTapFeedbackHPCutoffId,
            std::log(floatVal / 20.0f) / std::log(1000.0f));
    }

    // Morph Time: 50-2000ms -> normalized = (val-50)/1950
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kMultiTapMorphTimeId,
            static_cast<double>((floatVal - 50.0f) / 1950.0f));
    }

    // Dry/Wet: 0-100 -> normalized = val/100
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kMultiTapMixId,
            static_cast<double>(floatVal / 100.0f));
    }
}

} // namespace Iterum
