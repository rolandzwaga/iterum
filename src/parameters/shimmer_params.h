#pragma once

// ==============================================================================
// Shimmer Delay Parameters
// ==============================================================================
// Mode-specific parameter pack for Shimmer Delay (spec 029)
// Contains atomic storage, normalization helpers, and VST3 integration functions.
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/note_value_ui.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"

#include <atomic>
#include <cstdio>

namespace Iterum {

// ==============================================================================
// ShimmerParams Struct
// ==============================================================================
// Atomic parameter storage for real-time thread safety.
// All values stored in denormalized (real) units.
// ==============================================================================

struct ShimmerParams {
    std::atomic<float> delayTime{500.0f};        // 10-5000ms
    std::atomic<int> timeMode{0};                // 0=Free, 1=Synced (spec 043)
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};  // 0-19 (note values)
    std::atomic<float> pitchSemitones{12.0f};    // -24 to +24 semitones
    std::atomic<float> pitchCents{0.0f};         // -100 to +100 cents
    std::atomic<float> shimmerMix{1.0f};         // 0-1 (shimmer blend)
    std::atomic<float> feedback{0.5f};           // 0-1.2 (0-120%)
    std::atomic<float> diffusionAmount{0.5f};    // 0-1 (diffusion amount)
    std::atomic<float> diffusionSize{50.0f};     // 0-100%
    std::atomic<bool> filterEnabled{false};      // on/off
    std::atomic<float> filterCutoff{4000.0f};    // 20-20000Hz
    std::atomic<float> dryWet{0.5f};             // 0-1 (dry/wet mix)
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================
// Called from Processor::processParameterChanges() when a shimmer param changes.
// Denormalizes the value and stores in the atomic.
// ==============================================================================

inline void handleShimmerParamChange(
    ShimmerParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) noexcept
{
    switch (id) {
        case kShimmerDelayTimeId:
            // 10-5000ms
            params.delayTime.store(
                static_cast<float>(10.0 + normalizedValue * 4990.0),
                std::memory_order_relaxed);
            break;

        case kShimmerTimeModeId:
            // 0=Free, 1=Synced
            params.timeMode.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;

        case kShimmerNoteValueId:
            // 0-19 (note values)
            params.noteValue.store(
                static_cast<int>(normalizedValue * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                std::memory_order_relaxed);
            break;

        case kShimmerPitchSemitonesId:
            // -24 to +24 semitones
            params.pitchSemitones.store(
                static_cast<float>(-24.0 + normalizedValue * 48.0),
                std::memory_order_relaxed);
            break;

        case kShimmerPitchCentsId:
            // -100 to +100 cents
            params.pitchCents.store(
                static_cast<float>(-100.0 + normalizedValue * 200.0),
                std::memory_order_relaxed);
            break;

        case kShimmerPitchBlendId:
            // 0-1 (passthrough)
            params.shimmerMix.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kShimmerFeedbackId:
            // 0-1.2 (0-120%)
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.2),
                std::memory_order_relaxed);
            break;

        case kShimmerDiffusionAmountId:
            // 0-1 (passthrough)
            params.diffusionAmount.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kShimmerDiffusionSizeId:
            // 0-100%
            params.diffusionSize.store(
                static_cast<float>(normalizedValue * 100.0),
                std::memory_order_relaxed);
            break;

        case kShimmerFilterEnabledId:
            params.filterEnabled.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;

        case kShimmerFilterCutoffId:
            // 20-20000Hz (logarithmic might be better but linear for simplicity)
            params.filterCutoff.store(
                static_cast<float>(20.0 + normalizedValue * 19980.0),
                std::memory_order_relaxed);
            break;

        case kShimmerMixId:
            // 0-1 (passthrough)
            params.dryWet.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================
// Called from Controller::initialize() to register all shimmer parameters.
// ==============================================================================

inline void registerShimmerParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Delay Time: 10-5000ms
    parameters.addParameter(
        STR16("Delay Time"),
        STR16("ms"),
        0,
        0.098,  // ~500ms default = (500-10)/4990
        ParameterInfo::kCanAutomate,
        kShimmerDelayTimeId,
        0,
        STR16("Dly")
    );

    // Time Mode (Free/Synced) - spec 043
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Shimmer Time Mode"), kShimmerTimeModeId,
        0,  // default: Free (index 0)
        {STR16("Free"), STR16("Synced")}
    ));

    // Note Value - uses centralized dropdown strings (spec 043)
    parameters.addParameter(createNoteValueDropdown(
        STR16("Shimmer Note Value"), kShimmerNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // Pitch Semitones: -24 to +24
    parameters.addParameter(
        STR16("Pitch"),
        STR16("st"),
        0,
        0.75,  // +12 semitones default = (12+24)/48 = 0.75
        ParameterInfo::kCanAutomate,
        kShimmerPitchSemitonesId,
        0,
        STR16("Pitch")
    );

    // Pitch Cents: -100 to +100
    parameters.addParameter(
        STR16("Fine Tune"),
        STR16("ct"),
        0,
        0.5,  // 0 cents default
        ParameterInfo::kCanAutomate,
        kShimmerPitchCentsId,
        0,
        STR16("Fine")
    );

    // Shimmer Mix: 0-100%
    parameters.addParameter(
        STR16("Shimmer Mix"),
        STR16("%"),
        0,
        1.0,  // 100% default (full shimmer)
        ParameterInfo::kCanAutomate,
        kShimmerPitchBlendId,
        0,
        STR16("Shim")
    );

    // Feedback: 0-120%
    parameters.addParameter(
        STR16("Feedback"),
        STR16("%"),
        0,
        0.417,  // 50% default = 0.5/1.2
        ParameterInfo::kCanAutomate,
        kShimmerFeedbackId,
        0,
        STR16("Fdbk")
    );

    // Diffusion Amount: 0-100%
    parameters.addParameter(
        STR16("Diffusion"),
        STR16("%"),
        0,
        0.5,  // 50% default
        ParameterInfo::kCanAutomate,
        kShimmerDiffusionAmountId,
        0,
        STR16("Diff")
    );

    // Diffusion Size: 0-100%
    parameters.addParameter(
        STR16("Diff Size"),
        STR16("%"),
        0,
        0.5,  // 50% default
        ParameterInfo::kCanAutomate,
        kShimmerDiffusionSizeId,
        0,
        STR16("Size")
    );

    // Filter Enabled: on/off toggle
    parameters.addParameter(
        STR16("Filter"),
        nullptr,
        1,  // stepCount 1 = toggle
        0.0,  // off default
        ParameterInfo::kCanAutomate,
        kShimmerFilterEnabledId,
        0,
        STR16("Flt")
    );

    // Filter Cutoff: 20-20000Hz
    parameters.addParameter(
        STR16("Filter Cutoff"),
        STR16("Hz"),
        0,
        0.199,  // ~4000Hz default = (4000-20)/19980
        ParameterInfo::kCanAutomate,
        kShimmerFilterCutoffId,
        0,
        STR16("Cutoff")
    );

    // Dry/Wet: 0-100%
    parameters.addParameter(
        STR16("Dry/Wet"),
        STR16("%"),
        0,
        0.5,  // 50% default
        ParameterInfo::kCanAutomate,
        kShimmerMixId,
        0,
        STR16("Mix")
    );
}

// ==============================================================================
// Parameter Display Formatting
// ==============================================================================
// Called from Controller::getParamStringByValue() to format parameter values.
// ==============================================================================

inline Steinberg::tresult formatShimmerParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string)
{
    using namespace Steinberg;

    switch (id) {
        case kShimmerDelayTimeId: {
            // 10-5000ms
            double ms = 10.0 + valueNormalized * 4990.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", ms);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kShimmerPitchSemitonesId: {
            // -24 to +24 semitones
            double st = -24.0 + valueNormalized * 48.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%+.0f", st);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kShimmerPitchCentsId: {
            // -100 to +100 cents
            double ct = -100.0 + valueNormalized * 200.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%+.0f", ct);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kShimmerPitchBlendId:
        case kShimmerDiffusionAmountId:
        case kShimmerDiffusionSizeId:
        case kShimmerMixId: {
            // 0-100%
            double percent = valueNormalized * 100.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", percent);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kShimmerFeedbackId: {
            // 0-120%
            double percent = valueNormalized * 120.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", percent);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kShimmerFilterEnabledId: {
            UString(string, 128).fromAscii(
                valueNormalized >= 0.5 ? "On" : "Off");
            return kResultTrue;
        }

        case kShimmerFilterCutoffId: {
            // 20-20000Hz
            double hz = 20.0 + valueNormalized * 19980.0;
            char text[32];
            if (hz >= 1000.0) {
                std::snprintf(text, sizeof(text), "%.1fk", hz / 1000.0);
            } else {
                std::snprintf(text, sizeof(text), "%.0f", hz);
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
// Save/load shimmer parameters to/from stream.
// ==============================================================================

inline void saveShimmerParams(
    const ShimmerParams& params,
    Steinberg::IBStreamer& streamer)
{
    streamer.writeFloat(params.delayTime.load(std::memory_order_relaxed));
    streamer.writeInt32(params.timeMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitchSemitones.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitchCents.load(std::memory_order_relaxed));
    streamer.writeFloat(params.shimmerMix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.diffusionAmount.load(std::memory_order_relaxed));
    streamer.writeFloat(params.diffusionSize.load(std::memory_order_relaxed));
    Steinberg::int32 filterEnabled = params.filterEnabled.load(std::memory_order_relaxed) ? 1 : 0;
    streamer.writeInt32(filterEnabled);
    streamer.writeFloat(params.filterCutoff.load(std::memory_order_relaxed));
    streamer.writeFloat(params.dryWet.load(std::memory_order_relaxed));
}

inline void loadShimmerParams(
    ShimmerParams& params,
    Steinberg::IBStreamer& streamer)
{
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    if (streamer.readFloat(floatVal)) {
        params.delayTime.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.timeMode.store(intVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.noteValue.store(intVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.pitchSemitones.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.pitchCents.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.shimmerMix.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.feedback.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.diffusionAmount.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.diffusionSize.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.filterEnabled.store(intVal != 0, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.filterCutoff.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.dryWet.store(floatVal, std::memory_order_relaxed);
    }
}

// ==============================================================================
// Controller State Sync
// ==============================================================================
// Called from Controller::setComponentState() to sync processor state to UI.
// Template version allows custom parameter setting callback for flexibility.
// ==============================================================================

template<typename SetParamFunc>
inline void loadShimmerParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    using namespace Steinberg;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Delay Time: 10-5000ms -> normalized = (val-10)/4990
    if (streamer.readFloat(floatVal)) {
        setParam(kShimmerDelayTimeId,
            static_cast<double>((floatVal - 10.0f) / 4990.0f));
    }

    // Time Mode: 0-1 -> normalized = val
    if (streamer.readInt32(intVal)) {
        setParam(kShimmerTimeModeId, intVal != 0 ? 1.0 : 0.0);
    }

    // Note Value: 0-19 -> normalized = val/19
    if (streamer.readInt32(intVal)) {
        setParam(kShimmerNoteValueId,
            static_cast<double>(intVal) / (Parameters::kNoteValueDropdownCount - 1));
    }

    // Pitch Semitones: -24 to +24 -> normalized = (val+24)/48
    if (streamer.readFloat(floatVal)) {
        setParam(kShimmerPitchSemitonesId,
            static_cast<double>((floatVal + 24.0f) / 48.0f));
    }

    // Pitch Cents: -100 to +100 -> normalized = (val+100)/200
    if (streamer.readFloat(floatVal)) {
        setParam(kShimmerPitchCentsId,
            static_cast<double>((floatVal + 100.0f) / 200.0f));
    }

    // Shimmer Mix: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParam(kShimmerPitchBlendId,
            static_cast<double>(floatVal));
    }

    // Feedback: 0-1.2 -> normalized = val/1.2
    if (streamer.readFloat(floatVal)) {
        setParam(kShimmerFeedbackId,
            static_cast<double>(floatVal / 1.2f));
    }

    // Diffusion Amount: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParam(kShimmerDiffusionAmountId,
            static_cast<double>(floatVal));
    }

    // Diffusion Size: 0-100% -> normalized = val/100
    if (streamer.readFloat(floatVal)) {
        setParam(kShimmerDiffusionSizeId,
            static_cast<double>(floatVal / 100.0f));
    }

    // Filter Enabled
    if (streamer.readInt32(intVal)) {
        setParam(kShimmerFilterEnabledId, intVal ? 1.0 : 0.0);
    }

    // Filter Cutoff: 20-20000Hz -> normalized = (val-20)/19980
    if (streamer.readFloat(floatVal)) {
        setParam(kShimmerFilterCutoffId,
            static_cast<double>((floatVal - 20.0f) / 19980.0f));
    }

    // Dry/Wet: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParam(kShimmerMixId,
            static_cast<double>(floatVal));
    }
}

inline void syncShimmerParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    loadShimmerParamsToController(streamer,
        [&controller](Steinberg::Vst::ParamID id, double normalizedValue) {
            controller.setParamNormalized(id, normalizedValue);
        });
}

} // namespace Iterum
