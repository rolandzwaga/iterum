#pragma once

// ==============================================================================
// Granular Delay Parameters
// ==============================================================================
// Mode-specific parameter pack for Granular Delay (spec 034)
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
// GranularParams Struct
// ==============================================================================
// Atomic parameter storage for real-time thread safety.
// All values stored in denormalized (real) units.
// ==============================================================================

struct GranularParams {
    std::atomic<float> grainSize{100.0f};      // 10-500ms
    std::atomic<float> density{10.0f};         // 1-100 grains/sec
    std::atomic<float> delayTime{500.0f};      // 0-2000ms
    std::atomic<float> pitch{0.0f};            // -24 to +24 semitones
    std::atomic<float> pitchSpray{0.0f};       // 0-1
    std::atomic<float> positionSpray{0.0f};    // 0-1
    std::atomic<float> panSpray{0.0f};         // 0-1
    std::atomic<float> reverseProb{0.0f};      // 0-1
    std::atomic<bool> freeze{false};
    std::atomic<float> feedback{0.0f};         // 0-1.2
    std::atomic<float> dryWet{0.5f};           // 0-1
    std::atomic<int> envelopeType{0};          // 0-3 (Hann, Trapezoid, Sine, Blackman)
    std::atomic<int> timeMode{0};              // 0=Free, 1=Synced (spec 038)
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};  // 0-19 dropdown index (spec 038)
    std::atomic<float> jitter{0.5f};           // 0-1 timing randomness (Phase 2.1)
    std::atomic<int> pitchQuantMode{0};        // 0-4 (Off, Semitones, Octaves, Fifths, Scale) (Phase 2.2)
    std::atomic<float> texture{0.0f};          // 0-1 grain amplitude variation (Phase 2.3)
    std::atomic<float> stereoWidth{1.0f};      // 0-1 stereo width (0=mono, 1=stereo) (Phase 2.4)
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================
// Called from Processor::processParameterChanges() when a granular param changes.
// Denormalizes the value and stores in the atomic.
// ==============================================================================

inline void handleGranularParamChange(
    GranularParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) noexcept
{
    switch (id) {
        case kGranularGrainSizeId:
            // 10-500ms range
            params.grainSize.store(
                static_cast<float>(10.0 + normalizedValue * 490.0),
                std::memory_order_relaxed);
            break;

        case kGranularDensityId:
            // 1-100 grains/sec
            params.density.store(
                static_cast<float>(1.0 + normalizedValue * 99.0),
                std::memory_order_relaxed);
            break;

        case kGranularDelayTimeId:
            // 0-2000ms
            params.delayTime.store(
                static_cast<float>(normalizedValue * 2000.0),
                std::memory_order_relaxed);
            break;

        case kGranularPitchId:
            // -24 to +24 semitones
            params.pitch.store(
                static_cast<float>(-24.0 + normalizedValue * 48.0),
                std::memory_order_relaxed);
            break;

        case kGranularPitchSprayId:
            // 0-1 (already normalized)
            params.pitchSpray.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kGranularPositionSprayId:
            // 0-1 (already normalized)
            params.positionSpray.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kGranularPanSprayId:
            // 0-1 (already normalized)
            params.panSpray.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kGranularReverseProbId:
            // 0-1 (already normalized)
            params.reverseProb.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kGranularFreezeId:
            // Boolean switch
            params.freeze.store(normalizedValue >= 0.5, std::memory_order_relaxed);
            break;

        case kGranularFeedbackId:
            // 0-1.2 range (allows self-oscillation)
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.2),
                std::memory_order_relaxed);
            break;

        case kGranularMixId:
            // 0-1 (already normalized)
            params.dryWet.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kGranularEnvelopeTypeId:
            // 0-3 (Hann, Trapezoid, Sine, Blackman)
            params.envelopeType.store(
                static_cast<int>(normalizedValue * 3.0 + 0.5),
                std::memory_order_relaxed);
            break;

        case kGranularTimeModeId:
            // 0=Free, 1=Synced (spec 038)
            params.timeMode.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;

        case kGranularNoteValueId:
            // 0-19 dropdown index (spec 038)
            params.noteValue.store(
                static_cast<int>(normalizedValue * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                std::memory_order_relaxed);
            break;

        case kGranularJitterId:
            // 0-1 timing randomness (Phase 2.1)
            params.jitter.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kGranularPitchQuantId:
            // 0-4 (Off, Semitones, Octaves, Fifths, Scale) (Phase 2.2)
            params.pitchQuantMode.store(
                static_cast<int>(normalizedValue * 4.0 + 0.5),
                std::memory_order_relaxed);
            break;

        case kGranularTextureId:
            // 0-1 grain amplitude variation (Phase 2.3)
            params.texture.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kGranularStereoWidthId:
            // 0-1 stereo width (Phase 2.4)
            params.stereoWidth.store(
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
// Called from Controller::initialize() to register all granular parameters.
// ==============================================================================

inline void registerGranularParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Grain Size: 10-500ms
    parameters.addParameter(
        STR16("Grain Size"),
        STR16("ms"),
        0,
        0.184,  // (100-10)/(500-10) = 90/490 ≈ 0.184 (100ms default)
        ParameterInfo::kCanAutomate,
        kGranularGrainSizeId,
        0,
        STR16("GrSize")
    );

    // Density: 1-100 grains/sec
    parameters.addParameter(
        STR16("Density"),
        STR16("gr/s"),
        0,
        0.091,  // (10-1)/(100-1) = 9/99 ≈ 0.091 (10 grains/sec default)
        ParameterInfo::kCanAutomate,
        kGranularDensityId,
        0,
        STR16("Dens")
    );

    // Delay Time: 0-2000ms
    parameters.addParameter(
        STR16("Delay Time"),
        STR16("ms"),
        0,
        0.25,  // 500/2000 = 0.25 (500ms default)
        ParameterInfo::kCanAutomate,
        kGranularDelayTimeId,
        0,
        STR16("Delay")
    );

    // Pitch: -24 to +24 semitones
    parameters.addParameter(
        STR16("Pitch"),
        STR16("st"),
        0,
        0.5,  // (0+24)/(48) = 0.5 (0 semitones default)
        ParameterInfo::kCanAutomate,
        kGranularPitchId,
        0,
        STR16("Pitch")
    );

    // Pitch Spray: 0-1
    parameters.addParameter(
        STR16("Pitch Spray"),
        STR16("%"),
        0,
        0.0,  // 0% default
        ParameterInfo::kCanAutomate,
        kGranularPitchSprayId,
        0,
        STR16("PSpray")
    );

    // Position Spray: 0-1
    parameters.addParameter(
        STR16("Position Spray"),
        STR16("%"),
        0,
        0.0,  // 0% default
        ParameterInfo::kCanAutomate,
        kGranularPositionSprayId,
        0,
        STR16("Spray")
    );

    // Pan Spray: 0-1
    parameters.addParameter(
        STR16("Pan Spray"),
        STR16("%"),
        0,
        0.0,  // 0% default
        ParameterInfo::kCanAutomate,
        kGranularPanSprayId,
        0,
        STR16("Pan")
    );

    // Reverse Probability: 0-1
    parameters.addParameter(
        STR16("Reverse Prob"),
        STR16("%"),
        0,
        0.0,  // 0% default
        ParameterInfo::kCanAutomate,
        kGranularReverseProbId,
        0,
        STR16("Rev")
    );

    // Freeze: on/off toggle
    parameters.addParameter(
        STR16("Freeze"),
        nullptr,
        1,  // stepCount 1 = toggle
        0.0,  // off default
        ParameterInfo::kCanAutomate,
        kGranularFreezeId,
        0,
        STR16("Freeze")
    );

    // Feedback: 0-1.2
    parameters.addParameter(
        STR16("Feedback"),
        STR16("%"),
        0,
        0.0,  // 0% default
        ParameterInfo::kCanAutomate,
        kGranularFeedbackId,
        0,
        STR16("Fdbk")
    );

    // Dry/Wet: 0-1
    parameters.addParameter(
        STR16("Dry/Wet"),
        STR16("%"),
        0,
        0.5,  // 50% default
        ParameterInfo::kCanAutomate,
        kGranularMixId,
        0,
        STR16("Mix")
    );

    // Envelope Type: 0-3 (Hann, Trapezoid, Sine, Blackman) - MUST use StringListParameter
    parameters.addParameter(createDropdownParameter(
        STR16("Envelope"), kGranularEnvelopeTypeId,
        {STR16("Hann"), STR16("Trapezoid"), STR16("Sine"), STR16("Blackman")}
    ));

    // Time Mode: 0=Free, 1=Synced (spec 038)
    parameters.addParameter(createDropdownParameter(
        STR16("Time Mode"), kGranularTimeModeId,
        {STR16("Free"), STR16("Synced")}
    ));

    // Note Value: uses centralized dropdown strings (spec 038)
    parameters.addParameter(createNoteValueDropdown(
        STR16("Note Value"), kGranularNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // Jitter: 0-1 (timing randomness - Phase 2.1)
    parameters.addParameter(
        STR16("Jitter"),
        STR16("%"),
        0,
        0.5,  // 50% default
        ParameterInfo::kCanAutomate,
        kGranularJitterId,
        0,
        STR16("Jitter")
    );

    // Pitch Quantization: 0-4 (Off, Semitones, Octaves, Fifths, Scale) - Phase 2.2
    parameters.addParameter(createDropdownParameter(
        STR16("Pitch Quant"), kGranularPitchQuantId,
        {STR16("Off"), STR16("Semitones"), STR16("Octaves"), STR16("Fifths"), STR16("Scale")}
    ));

    // Texture: 0-1 (grain amplitude variation - Phase 2.3)
    parameters.addParameter(
        STR16("Texture"),
        STR16("%"),
        0,
        0.0,  // 0% default (uniform amplitudes)
        ParameterInfo::kCanAutomate,
        kGranularTextureId,
        0,
        STR16("Texture")
    );

    // Stereo Width: 0-1 (stereo decorrelation - Phase 2.4)
    parameters.addParameter(
        STR16("Stereo Width"),
        STR16("%"),
        0,
        1.0,  // 100% default (full stereo)
        ParameterInfo::kCanAutomate,
        kGranularStereoWidthId,
        0,
        STR16("Width")
    );
}

// ==============================================================================
// Parameter Display Formatting
// ==============================================================================
// Called from Controller::getParamStringByValue() to format parameter values.
// ==============================================================================

inline Steinberg::tresult formatGranularParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string)
{
    using namespace Steinberg;

    switch (id) {
        case kGranularGrainSizeId: {
            // 10-500ms
            double ms = 10.0 + valueNormalized * 490.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", ms);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kGranularDensityId: {
            // 1-100 grains/sec
            double density = 1.0 + valueNormalized * 99.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.1f", density);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kGranularDelayTimeId: {
            // 0-2000ms
            double ms = valueNormalized * 2000.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", ms);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kGranularPitchId: {
            // -24 to +24 semitones
            double semitones = -24.0 + valueNormalized * 48.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%+.1f", semitones);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kGranularPitchSprayId:
        case kGranularPositionSprayId:
        case kGranularPanSprayId:
        case kGranularReverseProbId:
        case kGranularMixId:
        case kGranularJitterId:
        case kGranularTextureId:
        case kGranularStereoWidthId: {
            // 0-100%
            double percent = valueNormalized * 100.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", percent);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        case kGranularFreezeId: {
            UString(string, 128).fromAscii(
                valueNormalized >= 0.5 ? "On" : "Off");
            return kResultTrue;
        }

        case kGranularFeedbackId: {
            // 0-120%
            double percent = valueNormalized * 120.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", percent);
            UString(string, 128).fromAscii(text);
            return kResultTrue;
        }

        // kGranularEnvelopeTypeId: handled by StringListParameter::toString() automatically

        default:
            return Steinberg::kResultFalse;
    }
}

// ==============================================================================
// State Persistence
// ==============================================================================
// Save/load granular parameters to/from stream.
// ==============================================================================

inline void saveGranularParams(
    const GranularParams& params,
    Steinberg::IBStreamer& streamer)
{
    streamer.writeFloat(params.grainSize.load(std::memory_order_relaxed));
    streamer.writeFloat(params.density.load(std::memory_order_relaxed));
    streamer.writeFloat(params.delayTime.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitch.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitchSpray.load(std::memory_order_relaxed));
    streamer.writeFloat(params.positionSpray.load(std::memory_order_relaxed));
    streamer.writeFloat(params.panSpray.load(std::memory_order_relaxed));
    streamer.writeFloat(params.reverseProb.load(std::memory_order_relaxed));
    Steinberg::int32 freeze = params.freeze.load(std::memory_order_relaxed) ? 1 : 0;
    streamer.writeInt32(freeze);
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.dryWet.load(std::memory_order_relaxed));
    streamer.writeInt32(params.envelopeType.load(std::memory_order_relaxed));
    // Tempo sync parameters (spec 038) - appended for backward compatibility
    streamer.writeInt32(params.timeMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    // Phase 2 parameters - appended for backward compatibility
    streamer.writeFloat(params.jitter.load(std::memory_order_relaxed));
    streamer.writeInt32(params.pitchQuantMode.load(std::memory_order_relaxed));
    streamer.writeFloat(params.texture.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stereoWidth.load(std::memory_order_relaxed));
}

inline void loadGranularParams(
    GranularParams& params,
    Steinberg::IBStreamer& streamer)
{
    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    if (streamer.readFloat(floatVal)) {
        params.grainSize.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.density.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.delayTime.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.pitch.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.pitchSpray.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.positionSpray.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.panSpray.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.reverseProb.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.freeze.store(intVal != 0, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.feedback.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.dryWet.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.envelopeType.store(intVal, std::memory_order_relaxed);
    }
    // Tempo sync parameters (spec 038) - optional for backward compatibility
    // If not present, defaults from struct initialization are used (Free mode, 1/8 note)
    if (streamer.readInt32(intVal)) {
        params.timeMode.store(intVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.noteValue.store(intVal, std::memory_order_relaxed);
    }
    // Phase 2 parameters - optional for backward compatibility
    if (streamer.readFloat(floatVal)) {
        params.jitter.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        params.pitchQuantMode.store(intVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.texture.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        params.stereoWidth.store(floatVal, std::memory_order_relaxed);
    }
}

// ==============================================================================
// Controller State Sync
// ==============================================================================
// Called from Controller::setComponentState() to sync processor state to UI.
// ==============================================================================

// Template function that reads granular params from stream and calls setParam callback
// SetParamFunc signature: void(Steinberg::Vst::ParamID paramId, double normalizedValue)
template<typename SetParamFunc>
inline void loadGranularParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    using namespace Steinberg;

    float floatVal = 0.0f;
    int32 intVal = 0;

    // Grain Size: 10-500ms -> normalized = (val - 10) / 490
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularGrainSizeId,
            static_cast<double>((floatVal - 10.0f) / 490.0f));
    }

    // Density: 1-100 -> normalized = (val - 1) / 99
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularDensityId,
            static_cast<double>((floatVal - 1.0f) / 99.0f));
    }

    // Delay Time: 0-2000ms -> normalized = val / 2000
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularDelayTimeId,
            static_cast<double>(floatVal / 2000.0f));
    }

    // Pitch: -24 to +24 -> normalized = (val + 24) / 48
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularPitchId,
            static_cast<double>((floatVal + 24.0f) / 48.0f));
    }

    // Pitch Spray: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularPitchSprayId, static_cast<double>(floatVal));
    }

    // Position Spray: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularPositionSprayId, static_cast<double>(floatVal));
    }

    // Pan Spray: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularPanSprayId, static_cast<double>(floatVal));
    }

    // Reverse Probability: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularReverseProbId, static_cast<double>(floatVal));
    }

    // Freeze: boolean
    if (streamer.readInt32(intVal)) {
        setParam(kGranularFreezeId, intVal ? 1.0 : 0.0);
    }

    // Feedback: 0-1.2 -> normalized = val / 1.2
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularFeedbackId,
            static_cast<double>(floatVal / 1.2f));
    }

    // Dry/Wet: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularMixId, static_cast<double>(floatVal));
    }

    // Envelope Type: 0-3 -> normalized = val / 3
    if (streamer.readInt32(intVal)) {
        setParam(kGranularEnvelopeTypeId,
            static_cast<double>(intVal) / 3.0);
    }

    // Time Mode: 0-1 -> normalized = val / 1 (spec 038)
    if (streamer.readInt32(intVal)) {
        setParam(kGranularTimeModeId,
            static_cast<double>(intVal));
    }

    // Note Value: 0-19 -> normalized = val / 19 (spec 038)
    if (streamer.readInt32(intVal)) {
        setParam(kGranularNoteValueId,
            static_cast<double>(intVal) / (Parameters::kNoteValueDropdownCount - 1));
    }

    // Jitter: 0-1 (already normalized) - Phase 2.1
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularJitterId, static_cast<double>(floatVal));
    }

    // Pitch Quantization: 0-4 -> normalized = val / 4 - Phase 2.2
    if (streamer.readInt32(intVal)) {
        setParam(kGranularPitchQuantId,
            static_cast<double>(intVal) / 4.0);
    }

    // Texture: 0-1 (already normalized) - Phase 2.3
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularTextureId, static_cast<double>(floatVal));
    }

    // Stereo Width: 0-1 (already normalized) - Phase 2.4
    if (streamer.readFloat(floatVal)) {
        setParam(kGranularStereoWidthId, static_cast<double>(floatVal));
    }
}

// Wrapper function for backward compatibility
inline void syncGranularParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    loadGranularParamsToController(streamer,
        [&controller](Steinberg::Vst::ParamID paramId, double normalizedValue) {
            controller.setParamNormalized(paramId, normalizedValue);
        });
}

} // namespace Iterum
