// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "version.h"

#include "public.sdk/source/vst/vstparameters.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"

#include "base/source/fobject.h"

#include <vector>

#if defined(_DEBUG) && defined(_WIN32)
#include "vstgui/lib/platform/win32/win32factory.h"
#include "vstgui/uidescription/uiattributes.h"
#include <windows.h>
#include <fstream>
#include <sstream>

// Debug helper to log view hierarchy
static void logViewHierarchy(VSTGUI::CView* view, std::ofstream& log, int depth = 0) {
    if (!view) return;

    std::string indent(depth * 2, ' ');

    // Get class name
    const char* className = "Unknown";
    if (dynamic_cast<VSTGUI::UIViewSwitchContainer*>(view)) {
        className = "UIViewSwitchContainer";
    } else if (dynamic_cast<VSTGUI::COptionMenu*>(view)) {
        className = "COptionMenu";
    } else if (dynamic_cast<VSTGUI::CControl*>(view)) {
        className = "CControl";
    } else if (view->asViewContainer()) {
        className = "CViewContainer";
    } else {
        className = "CView";
    }

    // Get control tag if it's a control
    int32_t tag = -1;
    if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
        tag = control->getTag();
    }

    auto size = view->getViewSize();
    log << indent << className;
    if (tag >= 0) {
        log << " [tag=" << tag << "]";
    }
    log << " size=" << size.getWidth() << "x" << size.getHeight();
    log << "\n";

    // Recurse into containers
    if (auto* container = view->asViewContainer()) {
        VSTGUI::ViewIterator it(container);
        while (*it) {
            logViewHierarchy(*it, log, depth + 1);
            ++it;
        }
    }
}

// Debug helper to find control by tag
static VSTGUI::CControl* findControlByTag(VSTGUI::CViewContainer* container, int32_t tag) {
    if (!container) return nullptr;

    VSTGUI::ViewIterator it(container);
    while (*it) {
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
            if (control->getTag() == tag) {
                return control;
            }
        }
        if (auto* childContainer = (*it)->asViewContainer()) {
            if (auto* found = findControlByTag(childContainer, tag)) {
                return found;
            }
        }
        ++it;
    }
    return nullptr;
}

#endif

// ==============================================================================
// VisibilityController: Thread-safe control visibility manager
// ==============================================================================
// Uses IDependent mechanism to receive parameter change notifications on UI thread.
// This is the CORRECT pattern for updating VSTGUI controls based on parameter values.
//
// CRITICAL Threading Rules:
// - setParamNormalized() can be called from ANY thread (automation, state load, etc.)
// - VSTGUI controls MUST only be manipulated on the UI thread
// - Solution: Use Parameter::addDependent() + deferred updates via UpdateHandler
//
// CRITICAL View Switching:
// - UIViewSwitchContainer DESTROYS and RECREATES controls when switching templates
// - DO NOT cache control pointers - they become invalid (dangling) after view switch
// - MUST look up control DYNAMICALLY on each update using control tag
// - Control tag remains constant, pointer changes on every view switch
// ==============================================================================
class VisibilityController : public Steinberg::FObject {
public:
    VisibilityController(
        VSTGUI::VST3Editor* editor,
        Steinberg::Vst::Parameter* watchedParam,
        std::initializer_list<Steinberg::int32> controlTags,
        float visibilityThreshold = 0.5f,
        bool showWhenBelow = true)
    : editor_(editor)
    , watchedParam_(watchedParam)
    , controlTags_(controlTags)
    , visibilityThreshold_(visibilityThreshold)
    , showWhenBelow_(showWhenBelow)
    {
        if (watchedParam_) {
            watchedParam_->addRef();
            watchedParam_->addDependent(this);  // Register for parameter change notifications
            // Trigger initial update on UI thread
            watchedParam_->deferUpdate();
        }
    }

    ~VisibilityController() override {
        if (watchedParam_) {
            watchedParam_->removeDependent(this);
            watchedParam_->release();
        }
    }

    // IDependent::update - called on UI thread via deferred update mechanism
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override {
        if (message == IDependent::kChanged && watchedParam_ && editor_) {
            // Get current parameter value (normalized: 0.0 to 1.0)
            float normalizedValue = watchedParam_->getNormalized();

            // Determine visibility based on threshold and direction
            bool shouldBeVisible = showWhenBelow_ ?
                (normalizedValue < visibilityThreshold_) :
                (normalizedValue >= visibilityThreshold_);

            // Update visibility for all associated controls (label + slider)
            for (Steinberg::int32 tag : controlTags_) {
                // CRITICAL: Look up control DYNAMICALLY on each update
                // UIViewSwitchContainer destroys/recreates controls on view switch,
                // so cached pointers become dangling references
                auto* control = findControlByTag(tag);

                if (control) {
                    // SAFE: This is called on UI thread via UpdateHandler::deferedUpdate()
                    control->setVisible(shouldBeVisible);

                    // Trigger redraw if needed
                    if (control->getFrame()) {
                        control->invalid();
                    }
                }
            }
        }
    }

    OBJ_METHODS(VisibilityController, FObject)

private:
    // Find control by tag in current view hierarchy
    VSTGUI::CControl* findControlByTag(Steinberg::int32 tag) {
        if (!editor_) return nullptr;
        auto* frame = editor_->getFrame();
        if (!frame) return nullptr;

#if defined(_DEBUG) && defined(_WIN32)
        // Use debug helper in debug builds
        return ::findControlByTag(frame, tag);
#else
        // Manual traversal in release builds
        std::function<VSTGUI::CControl*(VSTGUI::CViewContainer*)> search;
        search = [tag, &search](VSTGUI::CViewContainer* container) -> VSTGUI::CControl* {
            if (!container) return nullptr;
            VSTGUI::ViewIterator it(container);
            while (*it) {
                if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                    if (control->getTag() == tag) {
                        return control;
                    }
                }
                if (auto* childContainer = (*it)->asViewContainer()) {
                    if (auto* found = search(childContainer)) {
                        return found;
                    }
                }
                ++it;
            }
            return nullptr;
        };
        return search(frame);
#endif
    }

    VSTGUI::VST3Editor* editor_;
    Steinberg::Vst::Parameter* watchedParam_;
    std::vector<Steinberg::int32> controlTags_;
    float visibilityThreshold_;
    bool showWhenBelow_;
};

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
#if defined(_DEBUG) && defined(_WIN32)
    // Debug: Write to log file during initialization
    {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "iterum_debug.log";
        std::ofstream log(logPath, std::ios::app);
        log << "=== Iterum Controller::initialize called ===\n";
        log.flush();
    }
#endif

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

    // Mode parameter (selects active delay mode)
    // MUST use StringListParameter for proper toPlain() scaling!
    // Basic Parameter::toPlain() just returns normalized value unchanged.
    auto* modeParam = new Steinberg::Vst::StringListParameter(
        STR16("Mode"),             // title
        kModeId,                    // parameter ID
        nullptr,                    // units
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );
    modeParam->appendString(STR16("Granular"));
    modeParam->appendString(STR16("Spectral"));
    modeParam->appendString(STR16("Shimmer"));
    modeParam->appendString(STR16("Tape"));
    modeParam->appendString(STR16("BBD"));
    modeParam->appendString(STR16("Digital"));
    modeParam->appendString(STR16("PingPong"));
    modeParam->appendString(STR16("Reverse"));
    modeParam->appendString(STR16("MultiTap"));
    modeParam->appendString(STR16("Freeze"));
    modeParam->appendString(STR16("Ducking"));
    // Set default to Digital (index 5) - normalized value = 5/10 = 0.5
    modeParam->setNormalized(0.5);
    parameters.addParameter(modeParam);

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

    Steinberg::int32 mode = 0;
    if (streamer.readInt32(mode)) {
        // Convert mode index (0-10) to normalized (0.0-1.0)
        setParamNormalized(kModeId, static_cast<double>(mode) / 10.0);
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
#if defined(_DEBUG) && defined(_WIN32)
        // Debug: Write info to log file for easy diagnosis
        {
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            std::string logPath = std::string(tempPath) + "iterum_debug.log";
            std::ofstream log(logPath, std::ios::app);

            log << "=== Iterum createView called ===\n";

            if (auto* factory = VSTGUI::getPlatformFactory().asWin32Factory()) {
                log << "Got Win32Factory: OK\n";

                if (auto path = factory->getResourceBasePath()) {
                    std::string basePath(*path);
                    std::string fullPath = basePath + "\\editor.uidesc";
                    log << "Resource base path: " << basePath << "\n";

                    // Check if file actually exists
                    DWORD attribs = GetFileAttributesA(fullPath.c_str());
                    if (attribs != INVALID_FILE_ATTRIBUTES) {
                        log << "editor.uidesc EXISTS at path: OK\n";
                    } else {
                        log << "ERROR: editor.uidesc NOT FOUND at: " << fullPath << "\n";
                        log << "GetLastError: " << GetLastError() << "\n";
                    }
                } else {
                    log << "ERROR: Resource base path is NOT SET!\n";
                    log << "This means setupVSTGUIBundleSupport was not called.\n";
                }
            } else {
                log << "ERROR: Cannot get Win32Factory!\n";
            }

            log << "Creating VST3Editor with editor.uidesc...\n";
            log.flush();
        }
#endif

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

            // kModeId is handled by StringListParameter automatically

            default:
                return EditControllerEx1::getParamStringByValue(
                    id, valueNormalized, string);
        }
    }
    // =========================================================================
    // Mode-Specific Parameter Formatting
    // =========================================================================
    // Each formatXxxParam function handles continuous parameters but returns
    // kResultFalse for StringListParameters (dropdowns), which must be handled
    // by the base class EditControllerEx1::getParamStringByValue().
    // =========================================================================

    Steinberg::tresult result = Steinberg::kResultFalse;

    if (id >= kGranularBaseId && id <= kGranularEndId) {
        result = formatGranularParam(id, valueNormalized, string);
    }
    else if (id >= kSpectralBaseId && id <= kSpectralEndId) {
        result = formatSpectralParam(id, valueNormalized, string);
    }
    else if (id >= kShimmerBaseId && id <= kShimmerEndId) {
        result = formatShimmerParam(id, valueNormalized, string);
    }
    else if (id >= kTapeBaseId && id <= kTapeEndId) {
        result = formatTapeParam(id, valueNormalized, string);
    }
    else if (id >= kBBDBaseId && id <= kBBDEndId) {
        result = formatBBDParam(id, valueNormalized, string);
    }
    else if (id >= kDigitalBaseId && id <= kDigitalEndId) {
        result = formatDigitalParam(id, valueNormalized, string);
    }
    else if (id >= kPingPongBaseId && id <= kPingPongEndId) {
        result = formatPingPongParam(id, valueNormalized, string);
    }
    else if (id >= kReverseBaseId && id <= kReverseEndId) {
        result = formatReverseParam(id, valueNormalized, string);
    }
    else if (id >= kMultiTapBaseId && id <= kMultiTapEndId) {
        result = formatMultiTapParam(id, valueNormalized, string);
    }
    else if (id >= kFreezeBaseId && id <= kFreezeEndId) {
        result = formatFreezeParam(id, valueNormalized, string);
    }
    else if (id >= kDuckingBaseId && id <= kDuckingEndId) {
        result = formatDuckingParam(id, valueNormalized, string);
    }

    // If the mode-specific formatter didn't handle it (returns kResultFalse),
    // fall back to base class. This is essential for StringListParameters
    // (dropdowns) which use their own toString() method.
    if (result != Steinberg::kResultOk) {
        return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
    }

    return result;
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
// IEditController - Parameter Changes (DEBUG LOGGING)
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setParamNormalized(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

#if defined(_DEBUG) && defined(_WIN32)
    if (id == kModeId) {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "iterum_debug.log";
        std::ofstream log(logPath, std::ios::app);

        log << "\n=== setParamNormalized kModeId ===\n";
        log << "  Input normalized value: " << value << "\n";

        // Use SDK toPlain() to get the mode index
        if (auto* param = getParameterObject(kModeId)) {
            auto plainValue = param->toPlain(value);
            log << "  SDK toPlain() result: " << plainValue << "\n";
            log << "  As integer index: " << static_cast<int>(plainValue) << "\n";

            // Log parameter info using the ParameterInfo struct
            log << "  Parameter stepCount: " << param->getInfo().stepCount << "\n";
            log << "  Parameter defaultNormalized: " << param->getInfo().defaultNormalizedValue << "\n";
        } else {
            log << "  ERROR: getParameterObject(kModeId) returned nullptr!\n";
        }

        // Log the current state of the UI if editor is open
        if (activeEditor_) {
            if (auto* frame = activeEditor_->getFrame()) {
                // Find the Mode COptionMenu
                auto* modeControl = findControlByTag(frame, kModeId);
                if (modeControl) {
                    log << "  COptionMenu state BEFORE update:\n";
                    log << "    getValue(): " << modeControl->getValue() << "\n";
                    log << "    getValueNormalized(): " << modeControl->getValueNormalized() << "\n";
                    if (auto* optMenu = dynamic_cast<VSTGUI::COptionMenu*>(modeControl)) {
                        log << "    getCurrentIndex(): " << optMenu->getCurrentIndex() << "\n";
                        log << "    getNbEntries(): " << optMenu->getNbEntries() << "\n";
                    }
                }

                // Find UIViewSwitchContainer and log its state
                VSTGUI::ViewIterator it(frame);
                while (*it) {
                    if (auto* viewSwitch = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(*it)) {
                        log << "  UIViewSwitchContainer state BEFORE update:\n";
                        log << "    currentViewIndex: " << viewSwitch->getCurrentViewIndex() << "\n";
                        break;
                    }
                    // Check child containers
                    if (auto* container = (*it)->asViewContainer()) {
                        VSTGUI::ViewIterator childIt(container);
                        while (*childIt) {
                            if (auto* viewSwitch = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(*childIt)) {
                                log << "  UIViewSwitchContainer state BEFORE update:\n";
                                log << "    currentViewIndex: " << viewSwitch->getCurrentViewIndex() << "\n";
                                break;
                            }
                            ++childIt;
                        }
                    }
                    ++it;
                }
            }
        }

        log << "  Calling EditControllerEx1::setParamNormalized...\n";
        log.flush();
    }
#endif

    // Call base class - this is the ONLY thing that actually happens
    auto result = EditControllerEx1::setParamNormalized(id, value);

    // NOTE: Conditional visibility for delay time controls is handled by
    // VisibilityController instances via IDependent mechanism (see didOpen).
    // DO NOT manipulate UI controls here - setParamNormalized can be called
    // from non-UI threads (automation, state loading).

#if defined(_DEBUG) && defined(_WIN32)
    if (id == kModeId) {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "iterum_debug.log";
        std::ofstream log(logPath, std::ios::app);

        log << "  Base class returned: " << (result == Steinberg::kResultTrue ? "kResultTrue" : "other") << "\n";

        // Log state AFTER the base class update
        if (activeEditor_) {
            if (auto* frame = activeEditor_->getFrame()) {
                auto* modeControl = findControlByTag(frame, kModeId);
                if (modeControl) {
                    log << "  COptionMenu state AFTER update:\n";
                    log << "    getValue(): " << modeControl->getValue() << "\n";
                    log << "    getValueNormalized(): " << modeControl->getValueNormalized() << "\n";
                    if (auto* optMenu = dynamic_cast<VSTGUI::COptionMenu*>(modeControl)) {
                        log << "    getCurrentIndex(): " << optMenu->getCurrentIndex() << "\n";
                    }
                }

                // Find UIViewSwitchContainer AFTER update
                VSTGUI::ViewIterator it(frame);
                while (*it) {
                    if (auto* viewSwitch = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(*it)) {
                        log << "  UIViewSwitchContainer state AFTER update:\n";
                        log << "    currentViewIndex: " << viewSwitch->getCurrentViewIndex() << "\n";
                        break;
                    }
                    if (auto* container = (*it)->asViewContainer()) {
                        VSTGUI::ViewIterator childIt(container);
                        while (*childIt) {
                            if (auto* viewSwitch = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(*childIt)) {
                                log << "  UIViewSwitchContainer state AFTER update:\n";
                                log << "    currentViewIndex: " << viewSwitch->getCurrentViewIndex() << "\n";
                                break;
                            }
                            ++childIt;
                        }
                    }
                    ++it;
                }
            }
        }

        log << "=== END setParamNormalized ===\n\n";
        log.flush();
    }
#endif

    return result;
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
    // Store editor reference for manual UI control
    activeEditor_ = editor;

    // =========================================================================
    // Option Menu Configuration
    //
    // Native Windows popup (setupGenericOptionMenu false):
    //   + Click to open, click to select (standard behavior)
    //   - May have WM_COMMAND message issues in some hosts
    //
    // Generic VSTGUI menu (setupGenericOptionMenu true):
    //   + Works reliably across all hosts
    //   - Uses hold-to-select behavior (hold mouse, drag to item, release)
    //
    // Currently: Using native Windows popup for standard click behavior
    // If selection doesn't work in your host, enable the generic menu below
    // =========================================================================
    if (editor) {
        if (auto* frame = editor->getFrame()) {
            if (auto* platformFrame = frame->getPlatformFrame()) {
                // Use generic VSTGUI menu for reliable cross-host behavior
                platformFrame->setupGenericOptionMenu(true);
            }
            // UIViewSwitchContainer is automatically controlled via
            // template-switch-control="Mode" in editor.uidesc

            // =====================================================================
            // Conditional Visibility: Delay Time Controls
            // =====================================================================
            // Digital and PingPong modes have a delay time control that should be
            // hidden when time mode is "Synced" (since time value is ignored).
            //
            // Thread-Safe Pattern:
            // - Create VisibilityController instances that register as IDependent
            // - Parameter changes trigger IDependent::update() on UI thread
            // - UpdateHandler automatically defers updates to UI thread
            // - VSTGUI controls are ONLY manipulated on UI thread
            //
            // Dynamic Lookup Pattern:
            // - UIViewSwitchContainer destroys/recreates controls on view switch
            // - DO NOT cache control pointers - they become dangling after switch
            // - VisibilityController uses control TAG for dynamic lookup
            // - Each update() looks up current control by tag (survives view switch)
            // =====================================================================

            // Create visibility controllers for Digital mode
            // Hide delay time label + control when time mode is "Synced" (>= 0.5)
            if (auto* digitalTimeMode = getParameterObject(kDigitalTimeModeId)) {
                digitalDelayTimeVisibilityController_ = new VisibilityController(
                    editor, digitalTimeMode, {9901, kDigitalDelayTimeId}, 0.5f, true);
            }

            // Hide Age label + control when Era is "Pristine" (< 0.25)
            // Era values: 0 = Pristine (0.0), 1 = 80s (0.5), 2 = LoFi (1.0)
            // Show Age when Era >= 0.25 (80s or LoFi)
            if (auto* digitalEra = getParameterObject(kDigitalEraId)) {
                digitalAgeVisibilityController_ = new VisibilityController(
                    editor, digitalEra, {9902, kDigitalAgeId}, 0.25f, false);
            }

            // Create visibility controllers for PingPong mode
            // Hide delay time label + control when time mode is "Synced" (>= 0.5)
            if (auto* pingPongTimeMode = getParameterObject(kPingPongTimeModeId)) {
                pingPongDelayTimeVisibilityController_ = new VisibilityController(
                    editor, pingPongTimeMode, {9903, kPingPongDelayTimeId}, 0.5f, true);
            }

            // =====================================================================
            // Dynamic Version Label
            // =====================================================================
            // Set version label text from version.json instead of hardcoded string
            // Tag 9999 is assigned to the version label in editor.uidesc
            // =====================================================================
            std::function<VSTGUI::CTextLabel*(VSTGUI::CViewContainer*, int32_t)> findTextLabel;
            findTextLabel = [&findTextLabel](VSTGUI::CViewContainer* container, int32_t tag) -> VSTGUI::CTextLabel* {
                if (!container) return nullptr;
                VSTGUI::ViewIterator it(container);
                while (*it) {
                    if (auto* label = dynamic_cast<VSTGUI::CTextLabel*>(*it)) {
                        if (label->getTag() == tag) {
                            return label;
                        }
                    }
                    if (auto* childContainer = (*it)->asViewContainer()) {
                        if (auto* found = findTextLabel(childContainer, tag)) {
                            return found;
                        }
                    }
                    ++it;
                }
                return nullptr;
            };

            // Find and update version label (tag 9999)
            if (auto* versionLabel = findTextLabel(frame, 9999)) {
                versionLabel->setText(UI_VERSION_STR);
            }
        }
    }

#if defined(_DEBUG) && defined(_WIN32)
    {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "iterum_debug.log";
        std::ofstream log(logPath, std::ios::app);

        log << "\n========================================\n";
        log << "=== didOpen called ===\n";
        log << "========================================\n";

        if (editor) {
            log << "Editor pointer: OK\n";

            if (auto* frame = editor->getFrame()) {
                log << "Frame exists\n";
                auto size = frame->getViewSize();
                log << "Frame size: " << size.getWidth() << "x" << size.getHeight() << "\n";
                log << "Frame has " << frame->getNbViews() << " child views\n";

                // Log full view hierarchy
                log << "\n--- VIEW HIERARCHY ---\n";
                logViewHierarchy(frame, log, 0);

                // Find and log Mode control (tag 2)
                log << "\n--- MODE CONTROL SEARCH ---\n";
                auto* modeControl = findControlByTag(frame, kModeId);
                if (modeControl) {
                    log << "Found Mode control at tag " << kModeId << "\n";
                    log << "  Value: " << modeControl->getValue() << "\n";
                    log << "  ValueNormalized: " << modeControl->getValueNormalized() << "\n";
                    if (auto* optMenu = dynamic_cast<VSTGUI::COptionMenu*>(modeControl)) {
                        log << "  Type: COptionMenu\n";
                        log << "  Current index: " << optMenu->getCurrentIndex() << "\n";
                        log << "  Nb entries: " << optMenu->getNbEntries() << "\n";
                    }
                } else {
                    log << "ERROR: Mode control (tag " << kModeId << ") NOT FOUND!\n";
                }

                log << "\n--- END OF DIDOPEN LOG ---\n";
            } else {
                log << "ERROR: Frame is NULL!\n";
            }
        } else {
            log << "ERROR: Editor pointer is NULL!\n";
        }

        log.flush();
    }
#endif
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    // Called before editor closes
    (void)editor;

    // Clean up visibility controllers (automatically removes dependents and releases refs)
    digitalDelayTimeVisibilityController_ = nullptr;
    digitalAgeVisibilityController_ = nullptr;
    pingPongDelayTimeVisibilityController_ = nullptr;

    activeEditor_ = nullptr;
}

} // namespace Iterum
