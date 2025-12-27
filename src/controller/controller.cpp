// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "parameters/bbd_params.h"
#include "parameters/digital_params.h"
#include "parameters/ducking_params.h"
#include "parameters/freeze_params.h"
#include "parameters/granular_params.h"
#include "parameters/multitap_params.h"
#include "parameters/pingpong_params.h"
#include "parameters/reverse_params.h"
#include "parameters/shimmer_params.h"
#include "parameters/spectral_params.h"
#include "parameters/tape_params.h"

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
    // Mode-Specific Parameter Registration
    // ==========================================================================

    registerGranularParams(parameters);  // Granular Delay (spec 034)
    registerSpectralParams(parameters);  // Spectral Delay (spec 033)
    registerDuckingParams(parameters);   // Ducking Delay (spec 032)
    registerFreezeParams(parameters);    // Freeze Mode (spec 031)
    registerReverseParams(parameters);   // Reverse Delay (spec 030)
    registerShimmerParams(parameters);   // Shimmer Delay (spec 029)
    registerTapeParams(parameters);      // Tape Delay (spec 024)
    registerBBDParams(parameters);       // BBD Delay (spec 025)
    registerDigitalParams(parameters);   // Digital Delay (spec 026)
    registerPingPongParams(parameters);  // PingPong Delay (spec 027)
    registerMultiTapParams(parameters);  // MultiTap Delay (spec 028)

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

    // Read global parameters (must match Processor::getState order)
    float gain = 0.5f;
    if (streamer.readFloat(gain)) {
        // Convert from linear gain to normalized parameter value
        // gain range: 0.0 to 2.0, normalized = gain / 2.0
        setParamNormalized(kGainId, static_cast<double>(gain / 2.0f));
    }

    Steinberg::int32 bypass = 0;
    if (streamer.readInt32(bypass)) {
        setParamNormalized(kBypassId, bypass ? 1.0 : 0.0);
    }

    // ==========================================================================
    // Sync mode-specific parameters (order MUST match Processor::getState)
    // ==========================================================================

    syncGranularParamsToController(streamer, *this);  // Granular Delay (spec 034)
    syncSpectralParamsToController(streamer, *this);  // Spectral Delay (spec 033)
    syncDuckingParamsToController(streamer, *this);   // Ducking Delay (spec 032)
    syncFreezeParamsToController(streamer, *this);    // Freeze Mode (spec 031)
    syncReverseParamsToController(streamer, *this);   // Reverse Delay (spec 030)
    syncShimmerParamsToController(streamer, *this);   // Shimmer Delay (spec 029)
    syncTapeParamsToController(streamer, *this);      // Tape Delay (spec 024)
    syncBBDParamsToController(streamer, *this);       // BBD Delay (spec 025)
    syncDigitalParamsToController(streamer, *this);   // Digital Delay (spec 026)
    syncPingPongParamsToController(streamer, *this);  // PingPong Delay (spec 027)
    syncMultiTapParamsToController(streamer, *this);  // MultiTap Delay (spec 028)

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

    // =======================================================================
    // Route parameter formatting by ID range
    // =======================================================================

    if (id < kGranularBaseId) {
        // Global parameters (0-99)
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

            default:
                return EditControllerEx1::getParamStringByValue(
                    id, valueNormalized, string);
        }
    }
    else if (id >= kGranularBaseId && id <= kGranularEndId) {
        // Granular Delay parameters (100-199) - spec 034
        return formatGranularParam(id, valueNormalized, string);
    }
    else if (id >= kSpectralBaseId && id <= kSpectralEndId) {
        // Spectral Delay parameters (200-299) - spec 033
        return formatSpectralParam(id, valueNormalized, string);
    }
    else if (id >= kShimmerBaseId && id <= kShimmerEndId) {
        // Shimmer Delay parameters (300-399) - spec 029
        return formatShimmerParam(id, valueNormalized, string);
    }
    else if (id >= kTapeBaseId && id <= kTapeEndId) {
        // Tape Delay parameters (400-499) - spec 024
        return formatTapeParam(id, valueNormalized, string);
    }
    else if (id >= kBBDBaseId && id <= kBBDEndId) {
        // BBD Delay parameters (500-599) - spec 025
        return formatBBDParam(id, valueNormalized, string);
    }
    else if (id >= kDigitalBaseId && id <= kDigitalEndId) {
        // Digital Delay parameters (600-699) - spec 026
        return formatDigitalParam(id, valueNormalized, string);
    }
    else if (id >= kPingPongBaseId && id <= kPingPongEndId) {
        // PingPong Delay parameters (700-799) - spec 027
        return formatPingPongParam(id, valueNormalized, string);
    }
    else if (id >= kReverseBaseId && id <= kReverseEndId) {
        // Reverse Delay parameters (800-899) - spec 030
        return formatReverseParam(id, valueNormalized, string);
    }
    else if (id >= kMultiTapBaseId && id <= kMultiTapEndId) {
        // MultiTap Delay parameters (900-999) - spec 028
        return formatMultiTapParam(id, valueNormalized, string);
    }
    else if (id >= kFreezeBaseId && id <= kFreezeEndId) {
        // Freeze Mode parameters (1000-1099) - spec 031
        return formatFreezeParam(id, valueNormalized, string);
    }
    else if (id >= kDuckingBaseId && id <= kDuckingEndId) {
        // Ducking Delay parameters (1100-1199) - spec 032
        return formatDuckingParam(id, valueNormalized, string);
    }

    return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
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
