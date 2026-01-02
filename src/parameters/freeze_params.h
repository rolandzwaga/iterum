#pragma once

// ==============================================================================
// Freeze Mode Parameters
// ==============================================================================
// Parameter pack for Freeze Mode (spec 031)
// ID Range: 1000-1099
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

struct FreezeParams {
    std::atomic<bool> freezeEnabled{false};
    std::atomic<float> delayTime{500.0f};        // 10-5000ms
    std::atomic<int> timeMode{0};                // 0=Free, 1=Synced (spec 043)
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};  // 0-19 (note values)
    std::atomic<float> feedback{0.5f};           // 0-1.2
    std::atomic<float> pitchSemitones{0.0f};     // -24 to +24
    std::atomic<float> pitchCents{0.0f};         // -100 to +100
    std::atomic<float> shimmerMix{0.0f};         // 0-1
    std::atomic<float> decay{0.5f};              // 0-1
    std::atomic<float> diffusionAmount{0.3f};    // 0-1
    std::atomic<float> diffusionSize{0.5f};      // 0-1
    std::atomic<bool> filterEnabled{false};
    std::atomic<int> filterType{0};              // 0=LowPass, 1=HighPass, 2=BandPass
    std::atomic<float> filterCutoff{1000.0f};    // 20-20000Hz
    std::atomic<float> dryWet{0.5f};             // 0-1
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleFreezeParamChange(
    FreezeParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) {

    using namespace Steinberg;

    switch (id) {
        case kFreezeEnabledId:
            params.freezeEnabled.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;
        case kFreezeDelayTimeId:
            // 10-5000ms
            params.delayTime.store(
                static_cast<float>(10.0 + normalizedValue * 4990.0),
                std::memory_order_relaxed);
            break;
        case kFreezeTimeModeId:
            // 0=Free, 1=Synced
            params.timeMode.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;
        case kFreezeNoteValueId:
            // 0-19 (note values)
            params.noteValue.store(
                static_cast<int>(normalizedValue * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                std::memory_order_relaxed);
            break;
        case kFreezeFeedbackId:
            // 0-1.2
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.2),
                std::memory_order_relaxed);
            break;
        case kFreezePitchSemitonesId:
            // -24 to +24 semitones
            params.pitchSemitones.store(
                static_cast<float>(-24.0 + normalizedValue * 48.0),
                std::memory_order_relaxed);
            break;
        case kFreezePitchCentsId:
            // -100 to +100 cents
            params.pitchCents.store(
                static_cast<float>(-100.0 + normalizedValue * 200.0),
                std::memory_order_relaxed);
            break;
        case kFreezeShimmerMixId:
            // 0-1
            params.shimmerMix.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kFreezeDecayId:
            // 0-1
            params.decay.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kFreezeDiffusionAmountId:
            // 0-1
            params.diffusionAmount.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kFreezeDiffusionSizeId:
            // 0-1
            params.diffusionSize.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;
        case kFreezeFilterEnabledId:
            params.filterEnabled.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;
        case kFreezeFilterTypeId:
            // 0-2 (LowPass, HighPass, BandPass)
            params.filterType.store(
                static_cast<int>(normalizedValue * 2.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kFreezeFilterCutoffId:
            // 20-20000Hz (logarithmic)
            params.filterCutoff.store(
                static_cast<float>(20.0 * std::pow(1000.0, normalizedValue)),
                std::memory_order_relaxed);
            break;
        case kFreezeMixId:
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

inline void registerFreezeParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Freeze Enabled (on/off)
    parameters.addParameter(
        STR16("Freeze Enable"),
        nullptr,
        1,  // stepCount for boolean
        0,  // default: off
        ParameterInfo::kCanAutomate,
        kFreezeEnabledId);

    // Delay Time (10-5000ms)
    parameters.addParameter(
        STR16("Freeze Delay Time"),
        STR16("ms"),
        0,
        0.098,  // default: ~500ms normalized
        ParameterInfo::kCanAutomate,
        kFreezeDelayTimeId);

    // Time Mode (Free/Synced) - spec 043
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Freeze Time Mode"), kFreezeTimeModeId,
        0,  // default: Free (index 0)
        {STR16("Free"), STR16("Synced")}
    ));

    // Note Value - uses centralized dropdown strings (spec 043)
    parameters.addParameter(createNoteValueDropdown(
        STR16("Freeze Note Value"), kFreezeNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // Feedback (0-120%)
    parameters.addParameter(
        STR16("Freeze Feedback"),
        STR16("%"),
        0,
        0.417,  // default: 50%
        ParameterInfo::kCanAutomate,
        kFreezeFeedbackId);

    // Pitch Semitones (-24 to +24)
    parameters.addParameter(
        STR16("Freeze Pitch Semi"),
        STR16("st"),
        0,
        0.5,  // default: 0
        ParameterInfo::kCanAutomate,
        kFreezePitchSemitonesId);

    // Pitch Cents (-100 to +100)
    parameters.addParameter(
        STR16("Freeze Pitch Cents"),
        STR16("ct"),
        0,
        0.5,  // default: 0
        ParameterInfo::kCanAutomate,
        kFreezePitchCentsId);

    // Shimmer Mix (0-100%)
    parameters.addParameter(
        STR16("Freeze Shimmer Mix"),
        STR16("%"),
        0,
        0,  // default: 0%
        ParameterInfo::kCanAutomate,
        kFreezeShimmerMixId);

    // Decay (0-100%)
    parameters.addParameter(
        STR16("Freeze Decay"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kFreezeDecayId);

    // Diffusion Amount (0-100%)
    parameters.addParameter(
        STR16("Freeze Diffusion Amt"),
        STR16("%"),
        0,
        0.3,  // default: 30%
        ParameterInfo::kCanAutomate,
        kFreezeDiffusionAmountId);

    // Diffusion Size (0-100%)
    parameters.addParameter(
        STR16("Freeze Diffusion Size"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kFreezeDiffusionSizeId);

    // Filter Enabled (on/off)
    parameters.addParameter(
        STR16("Freeze Filter Enable"),
        nullptr,
        1,
        0,  // default: off
        ParameterInfo::kCanAutomate,
        kFreezeFilterEnabledId);

    // Filter Type (LowPass, HighPass, BandPass) - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Freeze Filter Type"), kFreezeFilterTypeId,
        {STR16("LowPass"), STR16("HighPass"), STR16("BandPass")}
    ));

    // Filter Cutoff (20-20000Hz)
    parameters.addParameter(
        STR16("Freeze Filter Cutoff"),
        STR16("Hz"),
        0,
        0.333,  // default: ~1000Hz (log scale)
        ParameterInfo::kCanAutomate,
        kFreezeFilterCutoffId);

    // Dry/Wet Mix (0-100%)
    parameters.addParameter(
        STR16("Freeze Dry/Wet"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kFreezeMixId);
}

// ==============================================================================
// Parameter Display Formatting (for Controller)
// ==============================================================================

inline Steinberg::tresult formatFreezeParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        case kFreezeEnabledId:
            Steinberg::UString(string, 128).assign(
                normalizedValue >= 0.5 ? STR16("On") : STR16("Off"));
            return kResultOk;

        case kFreezeDelayTimeId: {
            float ms = static_cast<float>(10.0 + normalizedValue * 4990.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeFeedbackId: {
            float percent = static_cast<float>(normalizedValue * 120.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezePitchSemitonesId: {
            float semitones = static_cast<float>(-24.0 + normalizedValue * 48.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%+.1f st", semitones);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezePitchCentsId: {
            float cents = static_cast<float>(-100.0 + normalizedValue * 200.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%+.0f ct", cents);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeShimmerMixId:
        case kFreezeDecayId:
        case kFreezeDiffusionAmountId:
        case kFreezeDiffusionSizeId:
        case kFreezeMixId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeFilterEnabledId:
            Steinberg::UString(string, 128).assign(
                normalizedValue >= 0.5 ? STR16("On") : STR16("Off"));
            return kResultOk;

        // kFreezeFilterTypeId: handled by StringListParameter::toString() automatically

        case kFreezeFilterCutoffId: {
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
    }

    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Persistence
// ==============================================================================

inline void saveFreezeParams(const FreezeParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.freezeEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.delayTime.load(std::memory_order_relaxed));
    streamer.writeInt32(params.timeMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitchSemitones.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitchCents.load(std::memory_order_relaxed));
    streamer.writeFloat(params.shimmerMix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decay.load(std::memory_order_relaxed));
    streamer.writeFloat(params.diffusionAmount.load(std::memory_order_relaxed));
    streamer.writeFloat(params.diffusionSize.load(std::memory_order_relaxed));
    streamer.writeInt32(params.filterEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.filterType.load(std::memory_order_relaxed));
    streamer.writeFloat(params.filterCutoff.load(std::memory_order_relaxed));
    streamer.writeFloat(params.dryWet.load(std::memory_order_relaxed));
}

inline void loadFreezeParams(FreezeParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 freezeEnabled = 0;
    streamer.readInt32(freezeEnabled);
    params.freezeEnabled.store(freezeEnabled != 0, std::memory_order_relaxed);

    float delayTime = 500.0f;
    streamer.readFloat(delayTime);
    params.delayTime.store(delayTime, std::memory_order_relaxed);

    Steinberg::int32 timeMode = 0;
    streamer.readInt32(timeMode);
    params.timeMode.store(timeMode, std::memory_order_relaxed);

    Steinberg::int32 noteValue = 4;
    streamer.readInt32(noteValue);
    params.noteValue.store(noteValue, std::memory_order_relaxed);

    float feedback = 0.5f;
    streamer.readFloat(feedback);
    params.feedback.store(feedback, std::memory_order_relaxed);

    float pitchSemitones = 0.0f;
    streamer.readFloat(pitchSemitones);
    params.pitchSemitones.store(pitchSemitones, std::memory_order_relaxed);

    float pitchCents = 0.0f;
    streamer.readFloat(pitchCents);
    params.pitchCents.store(pitchCents, std::memory_order_relaxed);

    float shimmerMix = 0.0f;
    streamer.readFloat(shimmerMix);
    params.shimmerMix.store(shimmerMix, std::memory_order_relaxed);

    float decay = 0.5f;
    streamer.readFloat(decay);
    params.decay.store(decay, std::memory_order_relaxed);

    float diffusionAmount = 0.3f;
    streamer.readFloat(diffusionAmount);
    params.diffusionAmount.store(diffusionAmount, std::memory_order_relaxed);

    float diffusionSize = 0.5f;
    streamer.readFloat(diffusionSize);
    params.diffusionSize.store(diffusionSize, std::memory_order_relaxed);

    Steinberg::int32 filterEnabled = 0;
    streamer.readInt32(filterEnabled);
    params.filterEnabled.store(filterEnabled != 0, std::memory_order_relaxed);

    Steinberg::int32 filterType = 0;
    streamer.readInt32(filterType);
    params.filterType.store(filterType, std::memory_order_relaxed);

    float filterCutoff = 1000.0f;
    streamer.readFloat(filterCutoff);
    params.filterCutoff.store(filterCutoff, std::memory_order_relaxed);

    float dryWet = 0.5f;
    streamer.readFloat(dryWet);
    params.dryWet.store(dryWet, std::memory_order_relaxed);
}

// ==============================================================================
// State Synchronization (Controller -> Processor state sync)
// ==============================================================================

inline void syncFreezeParamsToController(
    const FreezeParams& params,
    Steinberg::Vst::IEditController* controller) {

    using namespace Steinberg::Vst;

    // Convert stored values back to normalized for controller
    controller->setParamNormalized(kFreezeEnabledId,
        params.freezeEnabled.load(std::memory_order_relaxed) ? 1.0 : 0.0);

    // Delay time: 10-5000ms
    float delayTime = params.delayTime.load(std::memory_order_relaxed);
    controller->setParamNormalized(kFreezeDelayTimeId,
        (delayTime - 10.0f) / 4990.0f);

    // Feedback: 0-1.2
    controller->setParamNormalized(kFreezeFeedbackId,
        params.feedback.load(std::memory_order_relaxed) / 1.2f);

    // Pitch semitones: -24 to +24
    float semitones = params.pitchSemitones.load(std::memory_order_relaxed);
    controller->setParamNormalized(kFreezePitchSemitonesId,
        (semitones + 24.0f) / 48.0f);

    // Pitch cents: -100 to +100
    float cents = params.pitchCents.load(std::memory_order_relaxed);
    controller->setParamNormalized(kFreezePitchCentsId,
        (cents + 100.0f) / 200.0f);

    // Shimmer mix: 0-1
    controller->setParamNormalized(kFreezeShimmerMixId,
        params.shimmerMix.load(std::memory_order_relaxed));

    // Decay: 0-1
    controller->setParamNormalized(kFreezeDecayId,
        params.decay.load(std::memory_order_relaxed));

    // Diffusion amount: 0-1
    controller->setParamNormalized(kFreezeDiffusionAmountId,
        params.diffusionAmount.load(std::memory_order_relaxed));

    // Diffusion size: 0-1
    controller->setParamNormalized(kFreezeDiffusionSizeId,
        params.diffusionSize.load(std::memory_order_relaxed));

    // Filter enabled
    controller->setParamNormalized(kFreezeFilterEnabledId,
        params.filterEnabled.load(std::memory_order_relaxed) ? 1.0 : 0.0);

    // Filter type: 0-2
    controller->setParamNormalized(kFreezeFilterTypeId,
        params.filterType.load(std::memory_order_relaxed) / 2.0);

    // Filter cutoff: 20-20000Hz (log scale)
    float cutoff = params.filterCutoff.load(std::memory_order_relaxed);
    controller->setParamNormalized(kFreezeFilterCutoffId,
        std::log(cutoff / 20.0f) / std::log(1000.0f));

    // Dry/wet: 0-1
    controller->setParamNormalized(kFreezeMixId,
        params.dryWet.load(std::memory_order_relaxed));
}

// ==============================================================================
// Controller State Sync (from IBStreamer) - Template Version
// ==============================================================================

template <typename SetParamFunc>
inline void loadFreezeParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    using namespace Steinberg;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Freeze Enabled
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeEnabledId, intVal != 0 ? 1.0 : 0.0);
    }

    // Delay Time: 10-5000ms -> normalized = (val-10)/4990
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeDelayTimeId,
            static_cast<double>((floatVal - 10.0f) / 4990.0f));
    }

    // Time Mode: 0-1 -> normalized = val
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeTimeModeId, intVal != 0 ? 1.0 : 0.0);
    }

    // Note Value: 0-19 -> normalized = val/19
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeNoteValueId,
            static_cast<double>(intVal) / (Parameters::kNoteValueDropdownCount - 1));
    }

    // Feedback: 0-1.2 -> normalized = val/1.2
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeFeedbackId,
            static_cast<double>(floatVal / 1.2f));
    }

    // Pitch Semitones: -24 to +24 -> normalized = (val+24)/48
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezePitchSemitonesId,
            static_cast<double>((floatVal + 24.0f) / 48.0f));
    }

    // Pitch Cents: -100 to +100 -> normalized = (val+100)/200
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezePitchCentsId,
            static_cast<double>((floatVal + 100.0f) / 200.0f));
    }

    // Shimmer Mix: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeShimmerMixId,
            static_cast<double>(floatVal));
    }

    // Decay: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeDecayId,
            static_cast<double>(floatVal));
    }

    // Diffusion Amount: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeDiffusionAmountId,
            static_cast<double>(floatVal));
    }

    // Diffusion Size: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeDiffusionSizeId,
            static_cast<double>(floatVal));
    }

    // Filter Enabled
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeFilterEnabledId, intVal != 0 ? 1.0 : 0.0);
    }

    // Filter Type: 0-2 -> normalized = val/2
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeFilterTypeId,
            static_cast<double>(intVal) / 2.0);
    }

    // Filter Cutoff: 20-20000Hz (log scale) -> normalized = log(val/20)/log(1000)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeFilterCutoffId,
            std::log(floatVal / 20.0f) / std::log(1000.0f));
    }

    // Dry/Wet: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeMixId,
            static_cast<double>(floatVal));
    }
}

// Wrapper that calls the template with a controller lambda
inline void syncFreezeParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    loadFreezeParamsToController(streamer,
        [&controller](Steinberg::Vst::ParamID paramId, double normalizedValue) {
            controller.setParamNormalized(paramId, normalizedValue);
        });
}

} // namespace Iterum
