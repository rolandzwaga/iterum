#pragma once

// ==============================================================================
// Spectral Delay Parameters
// ==============================================================================
// Mode-specific parameter pack for Spectral Delay (spec 033)
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
// SpectralParams Struct
// ==============================================================================
// Atomic parameter storage for real-time thread safety.
// All values stored in denormalized (real) units.
// ==============================================================================

struct SpectralParams {
    std::atomic<int> fftSize{1024};           // 512, 1024, 2048, 4096
    std::atomic<float> baseDelay{250.0f};     // 0-2000ms
    std::atomic<float> spread{0.0f};          // 0-2000ms
    std::atomic<int> spreadDirection{0};      // 0-2 (LowToHigh, HighToLow, CenterOut)
    std::atomic<float> feedback{0.0f};        // 0-1.2
    std::atomic<float> feedbackTilt{0.0f};    // -1.0 to +1.0
    std::atomic<bool> freeze{false};          // on/off
    std::atomic<float> diffusion{0.0f};       // 0-1
    std::atomic<float> dryWet{50.0f};         // 0-100%
    std::atomic<int> spreadCurve{0};          // 0-1 (Linear, Logarithmic)
    std::atomic<float> stereoWidth{0.0f};     // 0-1 (stereo decorrelation)

    // Tempo Sync (spec 041)
    std::atomic<int> timeMode{0};             // 0=Free, 1=Synced
    std::atomic<int> noteValue{4};            // 0-9 dropdown index (default 4 = 1/8 note)
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================
// Called from Processor::processParameterChanges() when a spectral param changes.
// Denormalizes the value and stores in the atomic.
// ==============================================================================

inline void handleSpectralParamChange(
    SpectralParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) noexcept
{
    switch (id) {
        case kSpectralFFTSizeId:
            // 0-3 -> 512, 1024, 2048, 4096
            {
                int index = static_cast<int>(normalizedValue * 3.0 + 0.5);
                int sizes[] = {512, 1024, 2048, 4096};
                params.fftSize.store(sizes[index < 0 ? 0 : (index > 3 ? 3 : index)],
                    std::memory_order_relaxed);
            }
            break;

        case kSpectralBaseDelayId:
            // 0-2000ms
            params.baseDelay.store(
                static_cast<float>(normalizedValue * 2000.0),
                std::memory_order_relaxed);
            break;

        case kSpectralSpreadId:
            // 0-2000ms
            params.spread.store(
                static_cast<float>(normalizedValue * 2000.0),
                std::memory_order_relaxed);
            break;

        case kSpectralSpreadDirectionId:
            // 0-2 (LowToHigh, HighToLow, CenterOut)
            params.spreadDirection.store(
                static_cast<int>(normalizedValue * 2.0 + 0.5),
                std::memory_order_relaxed);
            break;

        case kSpectralFeedbackId:
            // 0-1.2
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.2),
                std::memory_order_relaxed);
            break;

        case kSpectralFeedbackTiltId:
            // -1.0 to +1.0
            params.feedbackTilt.store(
                static_cast<float>(-1.0 + normalizedValue * 2.0),
                std::memory_order_relaxed);
            break;

        case kSpectralFreezeId:
            // Boolean switch
            params.freeze.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;

        case kSpectralDiffusionId:
            // 0-1
            params.diffusion.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kSpectralDryWetId:
            // 0-100%
            params.dryWet.store(
                static_cast<float>(normalizedValue * 100.0),
                std::memory_order_relaxed);
            break;

        case kSpectralSpreadCurveId:
            // 0-1 (Linear=0, Logarithmic=1)
            params.spreadCurve.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;

        case kSpectralStereoWidthId:
            // 0-1 (stereo decorrelation)
            params.stereoWidth.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        // Tempo Sync (spec 041)
        case kSpectralTimeModeId:
            // 0=Free, 1=Synced
            params.timeMode.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;

        case kSpectralNoteValueId:
            // 0-9 dropdown index
            params.noteValue.store(
                static_cast<int>(normalizedValue * 9.0 + 0.5),
                std::memory_order_relaxed);
            break;

        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================
// Called from Controller::initialize() to register all spectral parameters.
// ==============================================================================

inline void registerSpectralParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // FFT Size: 512, 1024, 2048, 4096 - MUST use StringListParameter
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("FFT Size"), kSpectralFFTSizeId,
        1,  // default: 1024 (index 1)
        {STR16("512"), STR16("1024"), STR16("2048"), STR16("4096")}
    ));

    // Base Delay: 0-2000ms
    parameters.addParameter(
        STR16("Base Delay"),
        STR16("ms"),
        0,
        0.125,  // 250/2000 = 0.125 (250ms default)
        ParameterInfo::kCanAutomate,
        kSpectralBaseDelayId,
        0,
        STR16("Delay")
    );

    // Spread: 0-2000ms
    parameters.addParameter(
        STR16("Spread"),
        STR16("ms"),
        0,
        0.0,  // 0ms default
        ParameterInfo::kCanAutomate,
        kSpectralSpreadId,
        0,
        STR16("Spread")
    );

    // Spread Direction: LowToHigh, HighToLow, CenterOut - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Spread Dir"), kSpectralSpreadDirectionId,
        {STR16("Low->High"), STR16("High->Low"), STR16("Center Out")}
    ));

    // Feedback: 0-120%
    parameters.addParameter(
        STR16("Feedback"),
        STR16("%"),
        0,
        0.0,  // 0% default
        ParameterInfo::kCanAutomate,
        kSpectralFeedbackId,
        0,
        STR16("Fdbk")
    );

    // Feedback Tilt: -100% to +100%
    parameters.addParameter(
        STR16("Feedback Tilt"),
        STR16("%"),
        0,
        0.5,  // 0% default (center)
        ParameterInfo::kCanAutomate,
        kSpectralFeedbackTiltId,
        0,
        STR16("Tilt")
    );

    // Freeze: on/off toggle
    parameters.addParameter(
        STR16("Freeze"),
        nullptr,
        1,  // stepCount 1 = toggle
        0.0,  // off default
        ParameterInfo::kCanAutomate,
        kSpectralFreezeId,
        0,
        STR16("Freeze")
    );

    // Diffusion: 0-100%
    parameters.addParameter(
        STR16("Diffusion"),
        STR16("%"),
        0,
        0.0,  // 0% default
        ParameterInfo::kCanAutomate,
        kSpectralDiffusionId,
        0,
        STR16("Diff")
    );

    // Dry/Wet: 0-100%
    parameters.addParameter(
        STR16("Dry/Wet"),
        STR16("%"),
        0,
        0.5,  // 50% default
        ParameterInfo::kCanAutomate,
        kSpectralDryWetId,
        0,
        STR16("Mix")
    );

    // Spread Curve: Linear, Logarithmic - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Spread Curve"), kSpectralSpreadCurveId,
        {STR16("Linear"), STR16("Logarithmic")}
    ));

    // Stereo Width: 0-100%
    parameters.addParameter(
        STR16("Stereo Width"),
        STR16("%"),
        0,
        0.0,  // 0% default (mono-like)
        ParameterInfo::kCanAutomate,
        kSpectralStereoWidthId,
        0,
        STR16("Width")
    );

    // Tempo Sync (spec 041)
    // Time Mode: Free, Synced - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Time Mode"), kSpectralTimeModeId,
        {STR16("Free"), STR16("Synced")}
    ));

    // Note Value: 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Note Value"), kSpectralNoteValueId,
        4,  // default: 1/8 note (index 4)
        {STR16("1/32"), STR16("1/16T"), STR16("1/16"), STR16("1/8T"), STR16("1/8"),
         STR16("1/4T"), STR16("1/4"), STR16("1/2T"), STR16("1/2"), STR16("1/1")}
    ));
}

// ==============================================================================
// Parameter Display Formatting
// ==============================================================================
// Called from Controller::getParamStringByValue() to format parameter values.
// ==============================================================================

inline Steinberg::tresult formatSpectralParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string)
{
    using namespace Steinberg;

    switch (id) {
        // kSpectralFFTSizeId: handled by StringListParameter::toString() automatically

        case kSpectralBaseDelayId:
        case kSpectralSpreadId: {
            // 0-2000ms
            double ms = valueNormalized * 2000.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", ms);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        // kSpectralSpreadDirectionId: handled by StringListParameter::toString() automatically

        case kSpectralFeedbackId: {
            // 0-120%
            double percent = valueNormalized * 120.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", percent);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kSpectralFeedbackTiltId: {
            // -100% to +100%
            double percent = (-1.0 + valueNormalized * 2.0) * 100.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%+.0f", percent);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kSpectralFreezeId: {
            UString(string, 128).fromAscii(
                valueNormalized >= 0.5 ? "On" : "Off");
            return kResultTrue;
        }

        case kSpectralDiffusionId:
        case kSpectralDryWetId:
        case kSpectralStereoWidthId: {
            // 0-100%
            double percent = valueNormalized * 100.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", percent);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        // kSpectralSpreadCurveId: handled by StringListParameter::toString() automatically

        default:
            return Steinberg::kResultFalse;
    }
}

// ==============================================================================
// State Persistence
// ==============================================================================
// Save/load spectral parameters to/from stream.
// ==============================================================================

inline void saveSpectralParams(
    const SpectralParams& params,
    Steinberg::IBStreamer& streamer)
{
    streamer.writeInt32(params.fftSize.load(std::memory_order_relaxed));
    streamer.writeFloat(params.baseDelay.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spread.load(std::memory_order_relaxed));
    streamer.writeInt32(params.spreadDirection.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedbackTilt.load(std::memory_order_relaxed));
    Steinberg::int32 freeze = params.freeze.load(std::memory_order_relaxed) ? 1 : 0;
    streamer.writeInt32(freeze);
    streamer.writeFloat(params.diffusion.load(std::memory_order_relaxed));
    streamer.writeFloat(params.dryWet.load(std::memory_order_relaxed));
    streamer.writeInt32(params.spreadCurve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stereoWidth.load(std::memory_order_relaxed));

    // Tempo Sync (spec 041)
    streamer.writeInt32(params.timeMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
}

inline void loadSpectralParams(
    SpectralParams& params,
    Steinberg::IBStreamer& streamer)
{
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    if (streamer.readInt32(intVal)) {
        params.fftSize.store(intVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.baseDelay.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.spread.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.spreadDirection.store(intVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.feedback.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.feedbackTilt.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.freeze.store(intVal != 0, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.diffusion.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.dryWet.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.spreadCurve.store(intVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.stereoWidth.store(floatVal, std::memory_order_relaxed);
    }

    // Tempo Sync (spec 041)
    if (streamer.readInt32(intVal)) {
        params.timeMode.store(intVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.noteValue.store(intVal, std::memory_order_relaxed);
    }
}

// ==============================================================================
// Controller State Sync
// ==============================================================================
// Called from Controller::setComponentState() to sync processor state to UI.
// ==============================================================================

inline void syncSpectralParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // FFT Size: 512=0, 1024=1, 2048=2, 4096=3 -> normalized = index/3
    if (streamer.readInt32(intVal)) {
        int index = 0;
        if (intVal <= 512) index = 0;
        else if (intVal <= 1024) index = 1;
        else if (intVal <= 2048) index = 2;
        else index = 3;
        controller.setParamNormalized(kSpectralFFTSizeId,
            static_cast<double>(index) / 3.0);
    }

    // Base Delay: 0-2000ms -> normalized = val/2000
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kSpectralBaseDelayId,
            static_cast<double>(floatVal / 2000.0f));
    }

    // Spread: 0-2000ms -> normalized = val/2000
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kSpectralSpreadId,
            static_cast<double>(floatVal / 2000.0f));
    }

    // Spread Direction: 0-2 -> normalized = val/2
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kSpectralSpreadDirectionId,
            static_cast<double>(intVal) / 2.0);
    }

    // Feedback: 0-1.2 -> normalized = val/1.2
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kSpectralFeedbackId,
            static_cast<double>(floatVal / 1.2f));
    }

    // Feedback Tilt: -1.0 to +1.0 -> normalized = (val+1)/2
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kSpectralFeedbackTiltId,
            static_cast<double>((floatVal + 1.0f) / 2.0f));
    }

    // Freeze: boolean
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kSpectralFreezeId, intVal ? 1.0 : 0.0);
    }

    // Diffusion: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kSpectralDiffusionId,
            static_cast<double>(floatVal));
    }

    // Dry/Wet: 0-100% -> normalized = val/100
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kSpectralDryWetId,
            static_cast<double>(floatVal / 100.0f));
    }

    // Spread Curve: 0-1 -> normalized = val (already 0 or 1)
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kSpectralSpreadCurveId,
            static_cast<double>(intVal));
    }

    // Stereo Width: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        controller.setParamNormalized(kSpectralStereoWidthId,
            static_cast<double>(floatVal));
    }

    // Tempo Sync (spec 041)
    // Time Mode: 0=Free, 1=Synced -> normalized = val (already 0 or 1)
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kSpectralTimeModeId,
            static_cast<double>(intVal));
    }

    // Note Value: 0-9 -> normalized = val/9
    if (streamer.readInt32(intVal)) {
        controller.setParamNormalized(kSpectralNoteValueId,
            static_cast<double>(intVal) / 9.0);
    }
}

} // namespace Iterum
