#pragma once

// ==============================================================================
// Ducking Delay Parameters
// ==============================================================================
// Mode-specific parameter pack for Ducking Delay (spec 032)
// Contains atomic storage, normalization helpers, and VST3 integration functions.
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"

#include <atomic>
#include <cstdio>

namespace Iterum {

// ==============================================================================
// DuckingParams Struct
// ==============================================================================
// Atomic parameter storage for real-time thread safety.
// All values stored in denormalized (real) units.
// ==============================================================================

struct DuckingParams {
    // Ducking controls
    std::atomic<bool> duckingEnabled{false};     // on/off
    std::atomic<float> threshold{-30.0f};        // -60 to 0 dB
    std::atomic<float> duckAmount{50.0f};        // 0-100%
    std::atomic<float> attackTime{10.0f};        // 0.1-100ms
    std::atomic<float> releaseTime{200.0f};      // 10-2000ms
    std::atomic<float> holdTime{50.0f};          // 0-500ms
    std::atomic<int> duckTarget{0};              // 0-2 (Output, Feedback, Both)

    // Sidechain filter
    std::atomic<bool> sidechainFilterEnabled{false};  // on/off
    std::atomic<float> sidechainFilterCutoff{80.0f};  // 20-500Hz

    // Delay/output
    std::atomic<float> delayTime{500.0f};        // 10-5000ms
    std::atomic<float> feedback{0.0f};           // 0-120%
    std::atomic<float> dryWet{50.0f};            // 0-100%
    std::atomic<float> outputGain{0.0f};         // -96 to +6 dB
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================
// Called from Processor::processParameterChanges() when a ducking param changes.
// Denormalizes the value and stores in the atomic.
// ==============================================================================

inline void handleDuckingParamChange(
    DuckingParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) noexcept
{
    switch (id) {
        case kDuckingEnabledId:
            params.duckingEnabled.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;

        case kDuckingThresholdId:
            // -60 to 0 dB
            params.threshold.store(
                static_cast<float>(-60.0 + normalizedValue * 60.0),
                std::memory_order_relaxed);
            break;

        case kDuckingDuckAmountId:
            // 0-100%
            params.duckAmount.store(
                static_cast<float>(normalizedValue * 100.0),
                std::memory_order_relaxed);
            break;

        case kDuckingAttackTimeId:
            // 0.1-100ms (logarithmic mapping)
            params.attackTime.store(
                static_cast<float>(0.1 + normalizedValue * 99.9),
                std::memory_order_relaxed);
            break;

        case kDuckingReleaseTimeId:
            // 10-2000ms (logarithmic mapping would be better but linear for simplicity)
            params.releaseTime.store(
                static_cast<float>(10.0 + normalizedValue * 1990.0),
                std::memory_order_relaxed);
            break;

        case kDuckingHoldTimeId:
            // 0-500ms
            params.holdTime.store(
                static_cast<float>(normalizedValue * 500.0),
                std::memory_order_relaxed);
            break;

        case kDuckingDuckTargetId:
            // 0-2 (Output, Feedback, Both)
            params.duckTarget.store(
                static_cast<int>(normalizedValue * 2.0 + 0.5),
                std::memory_order_relaxed);
            break;

        case kDuckingSidechainFilterEnabledId:
            params.sidechainFilterEnabled.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;

        case kDuckingSidechainFilterCutoffId:
            // 20-500Hz
            params.sidechainFilterCutoff.store(
                static_cast<float>(20.0 + normalizedValue * 480.0),
                std::memory_order_relaxed);
            break;

        case kDuckingDelayTimeId:
            // 10-5000ms
            params.delayTime.store(
                static_cast<float>(10.0 + normalizedValue * 4990.0),
                std::memory_order_relaxed);
            break;

        case kDuckingFeedbackId:
            // 0-120%
            params.feedback.store(
                static_cast<float>(normalizedValue * 120.0),
                std::memory_order_relaxed);
            break;

        case kDuckingDryWetId:
            // 0-100%
            params.dryWet.store(
                static_cast<float>(normalizedValue * 100.0),
                std::memory_order_relaxed);
            break;

        case kDuckingOutputGainId:
            // -96 to +6 dB
            params.outputGain.store(
                static_cast<float>(-96.0 + normalizedValue * 102.0),
                std::memory_order_relaxed);
            break;

        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================
// Called from Controller::initialize() to register all ducking parameters.
// ==============================================================================

inline void registerDuckingParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Ducking Enabled: on/off toggle
    parameters.addParameter(
        STR16("Ducking Enable"),
        nullptr,
        1,  // stepCount 1 = toggle
        0.0,  // off default
        ParameterInfo::kCanAutomate,
        kDuckingEnabledId,
        0,
        STR16("Duck")
    );

    // Threshold: -60 to 0 dB
    parameters.addParameter(
        STR16("Threshold"),
        STR16("dB"),
        0,
        0.5,  // -30dB default = (30/60) = 0.5
        ParameterInfo::kCanAutomate,
        kDuckingThresholdId,
        0,
        STR16("Thrs")
    );

    // Duck Amount: 0-100%
    parameters.addParameter(
        STR16("Duck Amount"),
        STR16("%"),
        0,
        0.5,  // 50% default
        ParameterInfo::kCanAutomate,
        kDuckingDuckAmountId,
        0,
        STR16("Amt")
    );

    // Attack Time: 0.1-100ms
    parameters.addParameter(
        STR16("Attack Time"),
        STR16("ms"),
        0,
        0.099,  // ~10ms default = (10-0.1)/99.9
        ParameterInfo::kCanAutomate,
        kDuckingAttackTimeId,
        0,
        STR16("Atk")
    );

    // Release Time: 10-2000ms
    parameters.addParameter(
        STR16("Release Time"),
        STR16("ms"),
        0,
        0.095,  // ~200ms default = (200-10)/1990
        ParameterInfo::kCanAutomate,
        kDuckingReleaseTimeId,
        0,
        STR16("Rel")
    );

    // Hold Time: 0-500ms
    parameters.addParameter(
        STR16("Hold Time"),
        STR16("ms"),
        0,
        0.1,  // 50ms default = 50/500
        ParameterInfo::kCanAutomate,
        kDuckingHoldTimeId,
        0,
        STR16("Hold")
    );

    // Duck Target: Output, Feedback, Both - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Duck Target"), kDuckingDuckTargetId,
        {STR16("Output"), STR16("Feedback"), STR16("Both")}
    ));

    // Sidechain Filter Enabled: on/off toggle
    parameters.addParameter(
        STR16("SC Filter"),
        nullptr,
        1,  // stepCount 1 = toggle
        0.0,  // off default
        ParameterInfo::kCanAutomate,
        kDuckingSidechainFilterEnabledId,
        0,
        STR16("SCFlt")
    );

    // Sidechain Filter Cutoff: 20-500Hz
    parameters.addParameter(
        STR16("SC Cutoff"),
        STR16("Hz"),
        0,
        0.125,  // 80Hz default = (80-20)/480
        ParameterInfo::kCanAutomate,
        kDuckingSidechainFilterCutoffId,
        0,
        STR16("SCHz")
    );

    // Delay Time: 10-5000ms
    parameters.addParameter(
        STR16("Delay Time"),
        STR16("ms"),
        0,
        0.098,  // ~500ms default = (500-10)/4990
        ParameterInfo::kCanAutomate,
        kDuckingDelayTimeId,
        0,
        STR16("Dly")
    );

    // Feedback: 0-120%
    parameters.addParameter(
        STR16("Feedback"),
        STR16("%"),
        0,
        0.0,  // 0% default
        ParameterInfo::kCanAutomate,
        kDuckingFeedbackId,
        0,
        STR16("Fdbk")
    );

    // Dry/Wet: 0-100%
    parameters.addParameter(
        STR16("Dry/Wet"),
        STR16("%"),
        0,
        0.5,  // 50% default
        ParameterInfo::kCanAutomate,
        kDuckingDryWetId,
        0,
        STR16("Mix")
    );

    // Output Gain: -96 to +6 dB
    parameters.addParameter(
        STR16("Output Gain"),
        STR16("dB"),
        0,
        0.941,  // 0dB default = (0+96)/102
        ParameterInfo::kCanAutomate,
        kDuckingOutputGainId,
        0,
        STR16("Out")
    );
}

// ==============================================================================
// Parameter Display Formatting
// ==============================================================================
// Called from Controller::getParamStringByValue() to format parameter values.
// ==============================================================================

inline Steinberg::tresult formatDuckingParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string)
{
    using namespace Steinberg;

    switch (id) {
        case kDuckingEnabledId:
        case kDuckingSidechainFilterEnabledId: {
            UString(string, 128).fromAscii(
                valueNormalized >= 0.5 ? "On" : "Off");
            return kResultTrue;
        }

        case kDuckingThresholdId: {
            // -60 to 0 dB
            double dB = -60.0 + valueNormalized * 60.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.1f", dB);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kDuckingDuckAmountId:
        case kDuckingDryWetId: {
            // 0-100%
            double percent = valueNormalized * 100.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", percent);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kDuckingAttackTimeId: {
            // 0.1-100ms
            double ms = 0.1 + valueNormalized * 99.9;
            char text[32];
            std::snprintf(text, sizeof(text), "%.1f", ms);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kDuckingReleaseTimeId: {
            // 10-2000ms
            double ms = 10.0 + valueNormalized * 1990.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", ms);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kDuckingHoldTimeId: {
            // 0-500ms
            double ms = valueNormalized * 500.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", ms);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        // kDuckingDuckTargetId: handled by StringListParameter::toString() automatically

        case kDuckingSidechainFilterCutoffId: {
            // 20-500Hz
            double hz = 20.0 + valueNormalized * 480.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", hz);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kDuckingDelayTimeId: {
            // 10-5000ms
            double ms = 10.0 + valueNormalized * 4990.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", ms);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kDuckingFeedbackId: {
            // 0-120%
            double percent = valueNormalized * 120.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", percent);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kDuckingOutputGainId: {
            // -96 to +6 dB
            double dB = -96.0 + valueNormalized * 102.0;
            char text[32];
            if (dB <= -96.0) {
                std::snprintf(text, sizeof(text), "-inf");
            } else {
                std::snprintf(text, sizeof(text), "%+.1f", dB);
            }
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        default:
            return Steinberg::kResultFalse;
    }
}

// ==============================================================================
// State Persistence
// ==============================================================================
// Save/load ducking parameters to/from stream.
// ==============================================================================

inline void saveDuckingParams(
    const DuckingParams& params,
    Steinberg::IBStreamer& streamer)
{
    Steinberg::int32 enabled = params.duckingEnabled.load(std::memory_order_relaxed) ? 1 : 0;
    streamer.writeInt32(enabled);
    streamer.writeFloat(params.threshold.load(std::memory_order_relaxed));
    streamer.writeFloat(params.duckAmount.load(std::memory_order_relaxed));
    streamer.writeFloat(params.attackTime.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseTime.load(std::memory_order_relaxed));
    streamer.writeFloat(params.holdTime.load(std::memory_order_relaxed));
    streamer.writeInt32(params.duckTarget.load(std::memory_order_relaxed));

    Steinberg::int32 scEnabled = params.sidechainFilterEnabled.load(std::memory_order_relaxed) ? 1 : 0;
    streamer.writeInt32(scEnabled);
    streamer.writeFloat(params.sidechainFilterCutoff.load(std::memory_order_relaxed));

    streamer.writeFloat(params.delayTime.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.dryWet.load(std::memory_order_relaxed));
    streamer.writeFloat(params.outputGain.load(std::memory_order_relaxed));
}

inline void loadDuckingParams(
    DuckingParams& params,
    Steinberg::IBStreamer& streamer)
{
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    if (streamer.readInt32(intVal)) {
        params.duckingEnabled.store(intVal != 0, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.threshold.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.duckAmount.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.attackTime.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.releaseTime.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.holdTime.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.duckTarget.store(intVal, std::memory_order_relaxed);
    }

    if (streamer.readInt32(intVal)) {
        params.sidechainFilterEnabled.store(intVal != 0, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.sidechainFilterCutoff.store(floatVal, std::memory_order_relaxed);
    }

    if (streamer.readFloat(floatVal)) {
        params.delayTime.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.feedback.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.dryWet.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.outputGain.store(floatVal, std::memory_order_relaxed);
    }
}

// ==============================================================================
// Controller State Sync
// ==============================================================================
// Called from Controller::setComponentState() to sync processor state to UI.
// ==============================================================================

inline void syncDuckingParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Ducking Enabled
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kDuckingEnabledId, intVal ? 1.0 : 0.0);
    }

    // Threshold: -60 to 0 dB -> normalized = (val+60)/60
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDuckingThresholdId,
            static_cast<double>((floatVal + 60.0f) / 60.0f));
    }

    // Duck Amount: 0-100% -> normalized = val/100
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDuckingDuckAmountId,
            static_cast<double>(floatVal / 100.0f));
    }

    // Attack Time: 0.1-100ms -> normalized = (val-0.1)/99.9
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDuckingAttackTimeId,
            static_cast<double>((floatVal - 0.1f) / 99.9f));
    }

    // Release Time: 10-2000ms -> normalized = (val-10)/1990
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDuckingReleaseTimeId,
            static_cast<double>((floatVal - 10.0f) / 1990.0f));
    }

    // Hold Time: 0-500ms -> normalized = val/500
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDuckingHoldTimeId,
            static_cast<double>(floatVal / 500.0f));
    }

    // Duck Target: 0-2 -> normalized = val/2
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kDuckingDuckTargetId,
            static_cast<double>(intVal) / 2.0);
    }

    // Sidechain Filter Enabled
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kDuckingSidechainFilterEnabledId, intVal ? 1.0 : 0.0);
    }

    // Sidechain Filter Cutoff: 20-500Hz -> normalized = (val-20)/480
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDuckingSidechainFilterCutoffId,
            static_cast<double>((floatVal - 20.0f) / 480.0f));
    }

    // Delay Time: 10-5000ms -> normalized = (val-10)/4990
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDuckingDelayTimeId,
            static_cast<double>((floatVal - 10.0f) / 4990.0f));
    }

    // Feedback: 0-120% -> normalized = val/120
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDuckingFeedbackId,
            static_cast<double>(floatVal / 120.0f));
    }

    // Dry/Wet: 0-100% -> normalized = val/100
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDuckingDryWetId,
            static_cast<double>(floatVal / 100.0f));
    }

    // Output Gain: -96 to +6 dB -> normalized = (val+96)/102
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kDuckingOutputGainId,
            static_cast<double>((floatVal + 96.0f) / 102.0f));
    }
}

} // namespace Iterum
