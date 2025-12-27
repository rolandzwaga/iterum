// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"

#include <cstdio>
#include <string>

namespace Iterum {

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    // Always call parent first
    Steinberg::tresult result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // ==========================================================================
    // Register Parameters
    // Constitution Principle V: All values normalized 0.0 to 1.0
    // ==========================================================================

    // Bypass parameter (standard VST3 bypass)
    parameters.addParameter(
        STR16("Bypass"),           // title
        nullptr,                    // units
        1,                          // stepCount (1 = toggle)
        0,                          // defaultValue (normalized)
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsBypass,
        kBypassId,                  // parameter ID
        0,                          // unitId
        STR16("Bypass")            // shortTitle
    );

    // Gain parameter
    parameters.addParameter(
        STR16("Gain"),             // title
        STR16("dB"),               // units
        0,                          // stepCount (0 = continuous)
        0.5,                        // defaultValue (normalized: 0.5 = unity)
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGainId,                    // parameter ID
        0,                          // unitId
        STR16("Gain")              // shortTitle
    );

    // ==========================================================================
    // Granular Delay Parameters (spec 034)
    // ==========================================================================

    // Grain Size: 10-500ms
    parameters.addParameter(
        STR16("Grain Size"),
        STR16("ms"),
        0,
        0.184,                     // (100-10)/(500-10) = 90/490 ≈ 0.184 (100ms default)
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularGrainSizeId,
        0,
        STR16("GrSize")
    );

    // Density: 1-100 grains/sec
    parameters.addParameter(
        STR16("Density"),
        STR16("gr/s"),
        0,
        0.091,                     // (10-1)/(100-1) = 9/99 ≈ 0.091 (10 grains/sec default)
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularDensityId,
        0,
        STR16("Dens")
    );

    // Delay Time: 0-2000ms
    parameters.addParameter(
        STR16("Delay Time"),
        STR16("ms"),
        0,
        0.25,                      // 500/2000 = 0.25 (500ms default)
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularDelayTimeId,
        0,
        STR16("Delay")
    );

    // Pitch: -24 to +24 semitones
    parameters.addParameter(
        STR16("Pitch"),
        STR16("st"),
        0,
        0.5,                       // (0+24)/(48) = 0.5 (0 semitones default)
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularPitchId,
        0,
        STR16("Pitch")
    );

    // Pitch Spray: 0-1
    parameters.addParameter(
        STR16("Pitch Spray"),
        STR16("%"),
        0,
        0.0,                       // 0% default
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularPitchSprayId,
        0,
        STR16("PSpray")
    );

    // Position Spray: 0-1
    parameters.addParameter(
        STR16("Position Spray"),
        STR16("%"),
        0,
        0.0,                       // 0% default
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularPositionSprayId,
        0,
        STR16("Spray")
    );

    // Pan Spray: 0-1
    parameters.addParameter(
        STR16("Pan Spray"),
        STR16("%"),
        0,
        0.0,                       // 0% default
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularPanSprayId,
        0,
        STR16("Pan")
    );

    // Reverse Probability: 0-1
    parameters.addParameter(
        STR16("Reverse Prob"),
        STR16("%"),
        0,
        0.0,                       // 0% default
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularReverseProbId,
        0,
        STR16("Rev")
    );

    // Freeze: on/off toggle
    parameters.addParameter(
        STR16("Freeze"),
        nullptr,
        1,                         // stepCount 1 = toggle
        0.0,                       // off default
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularFreezeId,
        0,
        STR16("Freeze")
    );

    // Feedback: 0-1.2
    parameters.addParameter(
        STR16("Feedback"),
        STR16("%"),
        0,
        0.0,                       // 0% default
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularFeedbackId,
        0,
        STR16("Fdbk")
    );

    // Dry/Wet: 0-1
    parameters.addParameter(
        STR16("Dry/Wet"),
        STR16("%"),
        0,
        0.5,                       // 50% default
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularDryWetId,
        0,
        STR16("Mix")
    );

    // Output Gain: -96 to +6 dB
    parameters.addParameter(
        STR16("Output Gain"),
        STR16("dB"),
        0,
        0.941,                     // (0+96)/(102) ≈ 0.941 (0dB default)
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        kGranularOutputGainId,
        0,
        STR16("Out")
    );

    // Envelope Type: 0-3 (Hann, Trapezoid, Sine, Blackman)
    parameters.addParameter(
        STR16("Envelope"),
        nullptr,
        3,                         // stepCount 3 = 4 discrete values
        0.0,                       // Hann default
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList,
        kGranularEnvelopeTypeId,
        0,
        STR16("Env")
    );

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    return EditControllerEx1::terminate();
}

// ==============================================================================
// IEditController - State Management
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setComponentState(
    Steinberg::IBStream* state) {
    // ==========================================================================
    // Constitution Principle I: Controller syncs TO processor state
    // This is called by host after processor state is loaded
    // We must read the SAME format that Processor::getState() writes
    // ==========================================================================

    if (!state) {
        return Steinberg::kResultFalse;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read gain (must match Processor::getState order)
    float gain = 0.5f;
    if (streamer.readFloat(gain)) {
        // Convert from linear gain to normalized parameter value
        // gain range: 0.0 to 2.0, normalized = gain / 2.0
        setParamNormalized(kGainId, static_cast<double>(gain / 2.0f));
    }

    // Read bypass
    Steinberg::int32 bypass = 0;
    if (streamer.readInt32(bypass)) {
        setParamNormalized(kBypassId, bypass ? 1.0 : 0.0);
    }

    // ==========================================================================
    // Read granular delay parameters (spec 034)
    // Must match Processor::getState() order exactly
    // ==========================================================================
    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    // Grain Size: 10-500ms -> normalized = (val - 10) / 490
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularGrainSizeId,
            static_cast<double>((floatVal - 10.0f) / 490.0f));
    }

    // Density: 1-100 -> normalized = (val - 1) / 99
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularDensityId,
            static_cast<double>((floatVal - 1.0f) / 99.0f));
    }

    // Delay Time: 0-2000ms -> normalized = val / 2000
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularDelayTimeId,
            static_cast<double>(floatVal / 2000.0f));
    }

    // Pitch: -24 to +24 -> normalized = (val + 24) / 48
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularPitchId,
            static_cast<double>((floatVal + 24.0f) / 48.0f));
    }

    // Pitch Spray: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularPitchSprayId, static_cast<double>(floatVal));
    }

    // Position Spray: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularPositionSprayId, static_cast<double>(floatVal));
    }

    // Pan Spray: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularPanSprayId, static_cast<double>(floatVal));
    }

    // Reverse Probability: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularReverseProbId, static_cast<double>(floatVal));
    }

    // Freeze: boolean
    if (streamer.readInt32(intVal)) {
        setParamNormalized(kGranularFreezeId, intVal ? 1.0 : 0.0);
    }

    // Feedback: 0-1.2 -> normalized = val / 1.2
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularFeedbackId,
            static_cast<double>(floatVal / 1.2f));
    }

    // Dry/Wet: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularDryWetId, static_cast<double>(floatVal));
    }

    // Output Gain: -96 to +6 dB -> normalized = (val + 96) / 102
    if (streamer.readFloat(floatVal)) {
        setParamNormalized(kGranularOutputGainId,
            static_cast<double>((floatVal + 96.0f) / 102.0f));
    }

    // Envelope Type: 0-3 -> normalized = val / 3
    if (streamer.readInt32(intVal)) {
        setParamNormalized(kGranularEnvelopeTypeId,
            static_cast<double>(intVal) / 3.0);
    }

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
    // Save controller-specific state (UI preferences, not audio parameters)
    // Constitution Principle V: UI-only state goes here

    // Example: Save which tab is selected, zoom level, etc.
    // For now, we have no controller-specific state
    (void)state;  // Unused for now

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) {
    // Restore controller-specific state
    (void)state;  // Unused for now

    return Steinberg::kResultTrue;
}

// ==============================================================================
// IEditController - Editor Creation
// ==============================================================================

Steinberg::IPlugView* PLUGIN_API Controller::createView(
    Steinberg::FIDString name) {
    // ==========================================================================
    // Constitution Principle V: Use UIDescription for UI layout
    // ==========================================================================

    if (Steinberg::FIDStringsEqual(name, Steinberg::Vst::ViewType::kEditor)) {
        // Create VSTGUI editor from UIDescription file
        auto* editor = new VSTGUI::VST3Editor(
            this,                           // controller
            "Editor",                       // viewName (matches uidesc)
            "editor.uidesc"                 // UIDescription file
        );
        return editor;
    }

    return nullptr;
}

// ==============================================================================
// IEditController - Parameter Display
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string) {

    switch (id) {
        case kGainId: {
            // Convert normalized (0-1) to dB display
            // normalized 0.5 = 0 dB (unity gain)
            double linearGain = valueNormalized * 2.0;
            double dB = (linearGain > 0.0001)
                ? 20.0 * std::log10(linearGain)
                : -80.0;

            char text[32];
            std::snprintf(text, sizeof(text), "%.1f", dB);

            Steinberg::UString(string, 128).fromAscii(text);
            return Steinberg::kResultTrue;
        }

        case kBypassId: {
            Steinberg::UString(string, 128).fromAscii(
                valueNormalized >= 0.5 ? "On" : "Off");
            return Steinberg::kResultTrue;
        }

        // =====================================================================
        // Granular Delay Parameters (spec 034)
        // =====================================================================

        case kGranularGrainSizeId: {
            // 10-500ms
            double ms = 10.0 + valueNormalized * 490.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return Steinberg::kResultTrue;
        }

        case kGranularDensityId: {
            // 1-100 grains/sec
            double density = 1.0 + valueNormalized * 99.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.1f", density);
            Steinberg::UString(string, 128).fromAscii(text);
            return Steinberg::kResultTrue;
        }

        case kGranularDelayTimeId: {
            // 0-2000ms
            double ms = valueNormalized * 2000.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return Steinberg::kResultTrue;
        }

        case kGranularPitchId: {
            // -24 to +24 semitones
            double semitones = -24.0 + valueNormalized * 48.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%+.1f", semitones);
            Steinberg::UString(string, 128).fromAscii(text);
            return Steinberg::kResultTrue;
        }

        case kGranularPitchSprayId:
        case kGranularPositionSprayId:
        case kGranularPanSprayId:
        case kGranularReverseProbId:
        case kGranularDryWetId: {
            // 0-100%
            double percent = valueNormalized * 100.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return Steinberg::kResultTrue;
        }

        case kGranularFreezeId: {
            Steinberg::UString(string, 128).fromAscii(
                valueNormalized >= 0.5 ? "On" : "Off");
            return Steinberg::kResultTrue;
        }

        case kGranularFeedbackId: {
            // 0-120%
            double percent = valueNormalized * 120.0;
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return Steinberg::kResultTrue;
        }

        case kGranularOutputGainId: {
            // -96 to +6 dB
            double dB = -96.0 + valueNormalized * 102.0;
            char text[32];
            if (dB <= -96.0) {
                std::snprintf(text, sizeof(text), "-inf");
            } else {
                std::snprintf(text, sizeof(text), "%+.1f", dB);
            }
            Steinberg::UString(string, 128).fromAscii(text);
            return Steinberg::kResultTrue;
        }

        case kGranularEnvelopeTypeId: {
            // 0-3 (Hann, Trapezoid, Sine, Blackman)
            int type = static_cast<int>(valueNormalized * 3.0 + 0.5);
            const char* names[] = {"Hann", "Trapezoid", "Sine", "Blackman"};
            Steinberg::UString(string, 128).fromAscii(
                names[type < 0 ? 0 : (type > 3 ? 3 : type)]);
            return Steinberg::kResultTrue;
        }

        default:
            return EditControllerEx1::getParamStringByValue(
                id, valueNormalized, string);
    }
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string,
    Steinberg::Vst::ParamValue& valueNormalized) {

    switch (id) {
        case kGainId: {
            // Parse dB value from string
            char asciiString[128];
            Steinberg::UString(string, 128).toAscii(asciiString, 128);

            double dB = 0.0;
            if (std::sscanf(asciiString, "%lf", &dB) == 1) {
                // Convert dB to linear, then to normalized
                double linearGain = std::pow(10.0, dB / 20.0);
                valueNormalized = linearGain / 2.0;
                return Steinberg::kResultTrue;
            }
            return Steinberg::kResultFalse;
        }

        default:
            return EditControllerEx1::getParamValueByString(
                id, string, valueNormalized);
    }
}

// ==============================================================================
// VST3EditorDelegate
// ==============================================================================

VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* description,
    VSTGUI::VST3Editor* editor) {
    // ==========================================================================
    // Constitution Principle V: Create custom views here
    // Return nullptr to use default view creation
    // ==========================================================================

    // Silence unused parameter warnings
    (void)name;
    (void)attributes;
    (void)description;
    (void)editor;

    // Example:
    // if (VSTGUI::UTF8StringView(name) == "MyCustomKnob") {
    //     return new MyCustomKnob(...);
    // }

    return nullptr;
}

void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    // Called when editor is opened
    // Good place to start timers, fetch initial state, etc.
    (void)editor;
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    // Called before editor closes
    // Clean up any resources created in didOpen
    (void)editor;
}

} // namespace Iterum
