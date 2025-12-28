#pragma once

// ==============================================================================
// Tape Delay Parameters
// ==============================================================================
// Parameter pack for Tape Delay (spec 024)
// ID Range: 400-499
// ==============================================================================

#include "plugin_ids.h"
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

struct TapeParams {
    // Main parameters
    std::atomic<float> motorSpeed{500.0f};      // 20-2000ms (delay time)
    std::atomic<float> motorInertia{300.0f};    // 100-1000ms
    std::atomic<float> wear{0.3f};              // 0-1
    std::atomic<float> saturation{0.5f};        // 0-1
    std::atomic<float> age{0.3f};               // 0-1
    std::atomic<bool> spliceEnabled{false};
    std::atomic<float> spliceIntensity{0.5f};   // 0-1
    std::atomic<float> feedback{0.4f};          // 0-1.2
    std::atomic<float> mix{0.5f};               // 0-1
    std::atomic<float> outputLevel{1.0f};       // linear gain

    // Head parameters (3 heads like RE-201 Space Echo)
    std::atomic<bool> head1Enabled{true};
    std::atomic<bool> head2Enabled{false};
    std::atomic<bool> head3Enabled{false};
    std::atomic<float> head1Level{1.0f};        // linear gain (-96 to +6 dB)
    std::atomic<float> head2Level{1.0f};
    std::atomic<float> head3Level{1.0f};
    std::atomic<float> head1Pan{0.0f};          // -1 to +1
    std::atomic<float> head2Pan{0.0f};
    std::atomic<float> head3Pan{0.0f};
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleTapeParamChange(
    TapeParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) {

    using namespace Steinberg;

    switch (id) {
        case kTapeMotorSpeedId:
            // 20-2000ms
            params.motorSpeed.store(
                static_cast<float>(20.0 + normalizedValue * 1980.0),
                std::memory_order_relaxed);
            break;
        case kTapeMotorInertiaId:
            // 100-1000ms
            params.motorInertia.store(
                static_cast<float>(100.0 + normalizedValue * 900.0),
                std::memory_order_relaxed);
            break;
        case kTapeWearId:
            // 0-1
            params.wear.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kTapeSaturationId:
            // 0-1
            params.saturation.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kTapeAgeId:
            // 0-1
            params.age.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kTapeSpliceEnabledId:
            params.spliceEnabled.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;
        case kTapeSpliceIntensityId:
            // 0-1
            params.spliceIntensity.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kTapeFeedbackId:
            // 0-1.2
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.2),
                std::memory_order_relaxed);
            break;
        case kTapeMixId:
            // 0-1
            params.mix.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kTapeOutputLevelId:
            // -96 to +12 dB -> linear
            {
                double dB = -96.0 + normalizedValue * 108.0;
                double linear = (dB <= -96.0) ? 0.0 : std::pow(10.0, dB / 20.0);
                params.outputLevel.store(static_cast<float>(linear), std::memory_order_relaxed);
            }
            break;
        case kTapeHead1EnabledId:
            params.head1Enabled.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;
        case kTapeHead2EnabledId:
            params.head2Enabled.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;
        case kTapeHead3EnabledId:
            params.head3Enabled.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;
        case kTapeHead1LevelId:
            // -96 to +6 dB -> linear
            {
                double dB = -96.0 + normalizedValue * 102.0;
                double linear = (dB <= -96.0) ? 0.0 : std::pow(10.0, dB / 20.0);
                params.head1Level.store(static_cast<float>(linear), std::memory_order_relaxed);
            }
            break;
        case kTapeHead2LevelId:
            {
                double dB = -96.0 + normalizedValue * 102.0;
                double linear = (dB <= -96.0) ? 0.0 : std::pow(10.0, dB / 20.0);
                params.head2Level.store(static_cast<float>(linear), std::memory_order_relaxed);
            }
            break;
        case kTapeHead3LevelId:
            {
                double dB = -96.0 + normalizedValue * 102.0;
                double linear = (dB <= -96.0) ? 0.0 : std::pow(10.0, dB / 20.0);
                params.head3Level.store(static_cast<float>(linear), std::memory_order_relaxed);
            }
            break;
        case kTapeHead1PanId:
            // -1 to +1
            params.head1Pan.store(
                static_cast<float>(normalizedValue * 2.0 - 1.0),
                std::memory_order_relaxed);
            break;
        case kTapeHead2PanId:
            params.head2Pan.store(
                static_cast<float>(normalizedValue * 2.0 - 1.0),
                std::memory_order_relaxed);
            break;
        case kTapeHead3PanId:
            params.head3Pan.store(
                static_cast<float>(normalizedValue * 2.0 - 1.0),
                std::memory_order_relaxed);
            break;
    }
}

// ==============================================================================
// Parameter Registration (for Controller)
// ==============================================================================

inline void registerTapeParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Motor Speed (20-2000ms)
    parameters.addParameter(
        STR16("Tape Motor Speed"),
        STR16("ms"),
        0,
        0.242,  // default: ~500ms
        ParameterInfo::kCanAutomate,
        kTapeMotorSpeedId);

    // Motor Inertia (100-1000ms)
    parameters.addParameter(
        STR16("Tape Motor Inertia"),
        STR16("ms"),
        0,
        0.222,  // default: ~300ms
        ParameterInfo::kCanAutomate,
        kTapeMotorInertiaId);

    // Wear (0-100%)
    parameters.addParameter(
        STR16("Tape Wear"),
        STR16("%"),
        0,
        0.3,  // default: 30%
        ParameterInfo::kCanAutomate,
        kTapeWearId);

    // Saturation (0-100%)
    parameters.addParameter(
        STR16("Tape Saturation"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kTapeSaturationId);

    // Age (0-100%)
    parameters.addParameter(
        STR16("Tape Age"),
        STR16("%"),
        0,
        0.3,  // default: 30%
        ParameterInfo::kCanAutomate,
        kTapeAgeId);

    // Splice Enabled (on/off)
    parameters.addParameter(
        STR16("Tape Splice Enable"),
        nullptr,
        1,
        0,  // default: off
        ParameterInfo::kCanAutomate,
        kTapeSpliceEnabledId);

    // Splice Intensity (0-100%)
    parameters.addParameter(
        STR16("Tape Splice Intensity"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kTapeSpliceIntensityId);

    // Feedback (0-120%)
    parameters.addParameter(
        STR16("Tape Feedback"),
        STR16("%"),
        0,
        0.333,  // default: ~40%
        ParameterInfo::kCanAutomate,
        kTapeFeedbackId);

    // Mix (0-100%)
    parameters.addParameter(
        STR16("Tape Mix"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kTapeMixId);

    // Output Level (-96 to +12 dB)
    parameters.addParameter(
        STR16("Tape Output Level"),
        STR16("dB"),
        0,
        0.889,  // default: 0dB
        ParameterInfo::kCanAutomate,
        kTapeOutputLevelId);

    // Head 1 Enabled
    parameters.addParameter(
        STR16("Tape Head 1 Enable"),
        nullptr,
        1,
        1,  // default: on
        ParameterInfo::kCanAutomate,
        kTapeHead1EnabledId);

    // Head 2 Enabled
    parameters.addParameter(
        STR16("Tape Head 2 Enable"),
        nullptr,
        1,
        0,  // default: off
        ParameterInfo::kCanAutomate,
        kTapeHead2EnabledId);

    // Head 3 Enabled
    parameters.addParameter(
        STR16("Tape Head 3 Enable"),
        nullptr,
        1,
        0,  // default: off
        ParameterInfo::kCanAutomate,
        kTapeHead3EnabledId);

    // Head 1 Level (-96 to +6 dB)
    parameters.addParameter(
        STR16("Tape Head 1 Level"),
        STR16("dB"),
        0,
        0.941,  // default: 0dB
        ParameterInfo::kCanAutomate,
        kTapeHead1LevelId);

    // Head 2 Level
    parameters.addParameter(
        STR16("Tape Head 2 Level"),
        STR16("dB"),
        0,
        0.941,
        ParameterInfo::kCanAutomate,
        kTapeHead2LevelId);

    // Head 3 Level
    parameters.addParameter(
        STR16("Tape Head 3 Level"),
        STR16("dB"),
        0,
        0.941,
        ParameterInfo::kCanAutomate,
        kTapeHead3LevelId);

    // Head 1 Pan (-100 to +100)
    parameters.addParameter(
        STR16("Tape Head 1 Pan"),
        nullptr,
        0,
        0.5,  // default: center
        ParameterInfo::kCanAutomate,
        kTapeHead1PanId);

    // Head 2 Pan
    parameters.addParameter(
        STR16("Tape Head 2 Pan"),
        nullptr,
        0,
        0.5,
        ParameterInfo::kCanAutomate,
        kTapeHead2PanId);

    // Head 3 Pan
    parameters.addParameter(
        STR16("Tape Head 3 Pan"),
        nullptr,
        0,
        0.5,
        ParameterInfo::kCanAutomate,
        kTapeHead3PanId);
}

// ==============================================================================
// Parameter Display Formatting (for Controller)
// ==============================================================================

inline Steinberg::tresult formatTapeParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        case kTapeMotorSpeedId: {
            float ms = static_cast<float>(20.0 + normalizedValue * 1980.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kTapeMotorInertiaId: {
            float ms = static_cast<float>(100.0 + normalizedValue * 900.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kTapeWearId:
        case kTapeSaturationId:
        case kTapeAgeId:
        case kTapeSpliceIntensityId:
        case kTapeMixId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kTapeFeedbackId: {
            float percent = static_cast<float>(normalizedValue * 120.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kTapeSpliceEnabledId:
        case kTapeHead1EnabledId:
        case kTapeHead2EnabledId:
        case kTapeHead3EnabledId:
            Steinberg::UString(string, 128).assign(
                normalizedValue >= 0.5 ? STR16("On") : STR16("Off"));
            return kResultOk;

        case kTapeOutputLevelId: {
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

        case kTapeHead1LevelId:
        case kTapeHead2LevelId:
        case kTapeHead3LevelId: {
            double dB = -96.0 + normalizedValue * 102.0;
            char8 text[32];
            if (dB <= -96.0) {
                snprintf(text, sizeof(text), "-inf dB");
            } else {
                snprintf(text, sizeof(text), "%.1f dB", dB);
            }
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kTapeHead1PanId:
        case kTapeHead2PanId:
        case kTapeHead3PanId: {
            float pan = static_cast<float>(normalizedValue * 200.0 - 100.0);
            char8 text[32];
            if (pan < -1.0f) {
                snprintf(text, sizeof(text), "L%.0f", -pan);
            } else if (pan > 1.0f) {
                snprintf(text, sizeof(text), "R%.0f", pan);
            } else {
                snprintf(text, sizeof(text), "C");
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

inline void saveTapeParams(const TapeParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.motorSpeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.motorInertia.load(std::memory_order_relaxed));
    streamer.writeFloat(params.wear.load(std::memory_order_relaxed));
    streamer.writeFloat(params.saturation.load(std::memory_order_relaxed));
    streamer.writeFloat(params.age.load(std::memory_order_relaxed));
    streamer.writeInt32(params.spliceEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.spliceIntensity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.outputLevel.load(std::memory_order_relaxed));
    streamer.writeInt32(params.head1Enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.head2Enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.head3Enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.head1Level.load(std::memory_order_relaxed));
    streamer.writeFloat(params.head2Level.load(std::memory_order_relaxed));
    streamer.writeFloat(params.head3Level.load(std::memory_order_relaxed));
    streamer.writeFloat(params.head1Pan.load(std::memory_order_relaxed));
    streamer.writeFloat(params.head2Pan.load(std::memory_order_relaxed));
    streamer.writeFloat(params.head3Pan.load(std::memory_order_relaxed));
}

inline void loadTapeParams(TapeParams& params, Steinberg::IBStreamer& streamer) {
    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    if (streamer.readFloat(floatVal)) params.motorSpeed.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.motorInertia.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.wear.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.saturation.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.age.store(floatVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal)) params.spliceEnabled.store(intVal != 0, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.spliceIntensity.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.feedback.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.mix.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.outputLevel.store(floatVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal)) params.head1Enabled.store(intVal != 0, std::memory_order_relaxed);
    if (streamer.readInt32(intVal)) params.head2Enabled.store(intVal != 0, std::memory_order_relaxed);
    if (streamer.readInt32(intVal)) params.head3Enabled.store(intVal != 0, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.head1Level.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.head2Level.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.head3Level.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.head1Pan.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.head2Pan.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal)) params.head3Pan.store(floatVal, std::memory_order_relaxed);
}

// ==============================================================================
// Controller State Sync (from IBStreamer)
// ==============================================================================

inline void syncTapeParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Motor Speed: 20-2000ms -> normalized = (val-20)/1980
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeMotorSpeedId,
            static_cast<double>((floatVal - 20.0f) / 1980.0f));
    }

    // Motor Inertia: 100-1000ms -> normalized = (val-100)/900
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeMotorInertiaId,
            static_cast<double>((floatVal - 100.0f) / 900.0f));
    }

    // Wear: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeWearId, static_cast<double>(floatVal));
    }

    // Saturation: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeSaturationId, static_cast<double>(floatVal));
    }

    // Age: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeAgeId, static_cast<double>(floatVal));
    }

    // Splice Enabled
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kTapeSpliceEnabledId, intVal != 0 ? 1.0 : 0.0);
    }

    // Splice Intensity: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeSpliceIntensityId, static_cast<double>(floatVal));
    }

    // Feedback: 0-1.2 -> normalized = val/1.2
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeFeedbackId,
            static_cast<double>(floatVal / 1.2f));
    }

    // Mix: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeMixId, static_cast<double>(floatVal));
    }

    // Output Level: linear -> dB -> normalized = (dB+96)/108
    if (streamer.readFloat(floatVal)) {
        double dB = (floatVal <= 0.0f) ? -96.0 : 20.0 * std::log10(floatVal);
        controller.setParamNormalized(kTapeOutputLevelId, (dB + 96.0) / 108.0);
    }

    // Head enables
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kTapeHead1EnabledId, intVal != 0 ? 1.0 : 0.0);
    }
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kTapeHead2EnabledId, intVal != 0 ? 1.0 : 0.0);
    }
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kTapeHead3EnabledId, intVal != 0 ? 1.0 : 0.0);
    }

    // Head levels: linear -> dB -> normalized = (dB+96)/102
    if (streamer.readFloat(floatVal)) {
        double dB = (floatVal <= 0.0f) ? -96.0 : 20.0 * std::log10(floatVal);
        controller.setParamNormalized(kTapeHead1LevelId, (dB + 96.0) / 102.0);
    }
    if (streamer.readFloat(floatVal)) {
        double dB = (floatVal <= 0.0f) ? -96.0 : 20.0 * std::log10(floatVal);
        controller.setParamNormalized(kTapeHead2LevelId, (dB + 96.0) / 102.0);
    }
    if (streamer.readFloat(floatVal)) {
        double dB = (floatVal <= 0.0f) ? -96.0 : 20.0 * std::log10(floatVal);
        controller.setParamNormalized(kTapeHead3LevelId, (dB + 96.0) / 102.0);
    }

    // Head pans: -1 to +1 -> normalized = (val+1)/2
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeHead1PanId, static_cast<double>((floatVal + 1.0f) / 2.0f));
    }
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeHead2PanId, static_cast<double>((floatVal + 1.0f) / 2.0f));
    }
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kTapeHead3PanId, static_cast<double>((floatVal + 1.0f) / 2.0f));
    }
}

} // namespace Iterum
