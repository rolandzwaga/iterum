// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "version.h"

#include "preset/preset_manager.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"

#include "public.sdk/source/vst/vstparameters.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"
#include "vstgui/uidescription/uiattributes.h"

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
    // editorPtr: Pointer to the controller's activeEditor_ member (NOT the editor itself!)
    // This allows us to always get the CURRENT editor, or nullptr if closed.
    VisibilityController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* watchedParam,
        std::initializer_list<Steinberg::int32> controlTags,
        float visibilityThreshold = 0.5f,
        bool showWhenBelow = true)
    : editorPtr_(editorPtr)
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
        // Get current editor from controller's member - may be nullptr if editor closed
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message == IDependent::kChanged && watchedParam_ && editor) {
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
        // Get current editor - may be nullptr if closed
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (!editor) return nullptr;
        auto* frame = editor->getFrame();
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

    VSTGUI::VST3Editor** editorPtr_;  // Pointer to controller's activeEditor_ member
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
// Destructor
// ==============================================================================
// Defined here so unique_ptr<PresetManager> can use the complete type

Controller::~Controller() = default;

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

    // Note: Bypass parameter removed - DAWs provide their own bypass functionality

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

    // ==========================================================================
    // Preset Manager (Spec 042)
    // ==========================================================================
    // Create PresetManager for preset browsing/scanning.
    // Note: We pass nullptr for processor since the controller doesn't have
    // direct access to it. We provide a state provider callback for saving.
    presetManager_ = std::make_unique<PresetManager>(nullptr, this);

    // Set state provider callback for preset saving
    presetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
        return this->createComponentStateStream();
    });

    // Set load provider callback for preset loading
    presetManager_->setLoadProvider([this](Steinberg::IBStream* state) -> bool {
        return this->loadComponentStateWithNotify(state);
    });

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

    // Note: bypass removed - DAWs provide their own bypass functionality

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

            // Note: kBypassId removed - DAWs provide their own bypass functionality

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

// =============================================================================
// PresetBrowserButton: Button that opens the preset browser
// =============================================================================
class PresetBrowserButton : public VSTGUI::CTextButton {
public:
    PresetBrowserButton(const VSTGUI::CRect& size, Controller* controller)
        : CTextButton(size, nullptr, -1, "Presets")
        , controller_(controller)
    {
        setFrameColor(VSTGUI::CColor(80, 80, 85));
        setTextColor(VSTGUI::CColor(255, 255, 255));
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (buttons.isLeftButton() && controller_) {
            controller_->openPresetBrowser();
            return VSTGUI::kMouseEventHandled;
        }
        return CTextButton::onMouseDown(where, buttons);
    }

private:
    Controller* controller_ = nullptr;
};

// =============================================================================
// SavePresetButton: Button that opens standalone save dialog (Spec 042)
// =============================================================================
class SavePresetButton : public VSTGUI::CTextButton {
public:
    SavePresetButton(const VSTGUI::CRect& size, Controller* controller)
        : CTextButton(size, nullptr, -1, "Save Preset")
        , controller_(controller)
    {
        setFrameColor(VSTGUI::CColor(80, 80, 85));
        setTextColor(VSTGUI::CColor(255, 255, 255));
    }

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override
    {
        if (buttons.isLeftButton() && controller_) {
            controller_->openSavePresetDialog();
            return VSTGUI::kMouseEventHandled;
        }
        return CTextButton::onMouseDown(where, buttons);
    }

private:
    Controller* controller_ = nullptr;
};

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
    (void)description;
    (void)editor;

    // Preset Browser Button (Spec 042)
    if (VSTGUI::UTF8StringView(name) == "PresetBrowserButton") {
        // Read origin and size from UIAttributes to get correct positioning
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(80, 24);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new PresetBrowserButton(rect, this);
    }

    // Save Preset Button (Spec 042) - Quick save shortcut
    if (VSTGUI::UTF8StringView(name) == "SavePresetButton") {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(60, 24);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new SavePresetButton(rect, this);
    }

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
            // NOTE: Pass &activeEditor_ (pointer to member) so VisibilityController
            // always gets the CURRENT editor, avoiding dangling pointer crashes
            // when the editor is closed and reopened.
            if (auto* digitalTimeMode = getParameterObject(kDigitalTimeModeId)) {
                digitalDelayTimeVisibilityController_ = new VisibilityController(
                    &activeEditor_, digitalTimeMode, {9901, kDigitalDelayTimeId}, 0.5f, true);
            }

            // Hide Age label + control when Era is "Pristine" (< 0.25)
            // Era values: 0 = Pristine (0.0), 1 = 80s (0.5), 2 = LoFi (1.0)
            // Show Age when Era >= 0.25 (80s or LoFi)
            if (auto* digitalEra = getParameterObject(kDigitalEraId)) {
                digitalAgeVisibilityController_ = new VisibilityController(
                    &activeEditor_, digitalEra, {9902, kDigitalAgeId}, 0.25f, false);
            }

            // Create visibility controllers for PingPong mode
            // Hide delay time label + control when time mode is "Synced" (>= 0.5)
            if (auto* pingPongTimeMode = getParameterObject(kPingPongTimeModeId)) {
                pingPongDelayTimeVisibilityController_ = new VisibilityController(
                    &activeEditor_, pingPongTimeMode, {9903, kPingPongDelayTimeId}, 0.5f, true);
            }

            // Create visibility controllers for Granular mode
            // Hide delay time label + control when time mode is "Synced" (>= 0.5)
            if (auto* granularTimeMode = getParameterObject(kGranularTimeModeId)) {
                granularDelayTimeVisibilityController_ = new VisibilityController(
                    &activeEditor_, granularTimeMode, {9904, kGranularDelayTimeId}, 0.5f, true);
            }

            // Create visibility controllers for Spectral mode (spec 041)
            // Hide base delay label + control when time mode is "Synced" (>= 0.5)
            if (auto* spectralTimeMode = getParameterObject(kSpectralTimeModeId)) {
                spectralBaseDelayVisibilityController_ = new VisibilityController(
                    &activeEditor_, spectralTimeMode, {9912, kSpectralBaseDelayId}, 0.5f, true);
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

            // =====================================================================
            // Preset Browser View (Spec 042)
            // =====================================================================
            // Create the preset browser view as an overlay covering the full frame.
            // The view is initially hidden and shown via openPresetBrowser().
            // =====================================================================
            if (presetManager_) {
                auto frameSize = frame->getViewSize();
                presetBrowserView_ = new PresetBrowserView(frameSize, presetManager_.get());
                frame->addView(presetBrowserView_);

                // Save Preset Dialog - standalone dialog for quick save from main UI
                savePresetDialogView_ = new SavePresetDialogView(frameSize, presetManager_.get());
                frame->addView(savePresetDialogView_);
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
    granularDelayTimeVisibilityController_ = nullptr;
    spectralBaseDelayVisibilityController_ = nullptr;  // spec 041

    // Preset browser view is owned by the frame and will be cleaned up automatically
    presetBrowserView_ = nullptr;

    activeEditor_ = nullptr;
}

// ==============================================================================
// Preset Browser (Spec 042)
// ==============================================================================

void Controller::openPresetBrowser() {
    if (presetBrowserView_ && !presetBrowserView_->isOpen()) {
        // Get current mode from parameter
        int currentMode = -1;  // Default to "All"
        if (auto* modeParam = getParameterObject(kModeId)) {
            currentMode = static_cast<int>(modeParam->toPlain(modeParam->getNormalized()));
        }

        presetBrowserView_->open(currentMode);
    }
}

void Controller::openSavePresetDialog() {
    if (savePresetDialogView_ && !savePresetDialogView_->isOpen()) {
        // Get current mode from parameter
        int currentMode = -1;  // Default to "All"
        if (auto* modeParam = getParameterObject(kModeId)) {
            currentMode = static_cast<int>(modeParam->toPlain(modeParam->getNormalized()));
        }

        savePresetDialogView_->open(currentMode);
    }
}

void Controller::closePresetBrowser() {
    if (presetBrowserView_ && presetBrowserView_->isOpen()) {
        presetBrowserView_->close();
    }
}

// ==============================================================================
// State Serialization for Preset Saving
// ==============================================================================

Steinberg::MemoryStream* Controller::createComponentStateStream() {
    // Create a memory stream and serialize current parameter values
    // in the same format as Processor::getState()
    auto* stream = new Steinberg::MemoryStream();
    Steinberg::IBStreamer streamer(stream, kLittleEndian);

    // Helper to get denormalized float from controller parameter
    auto getFloat = [this](Steinberg::Vst::ParamID id, float defaultVal, float scale = 1.0f) -> float {
        if (auto* param = getParameterObject(id)) {
            return static_cast<float>(param->toPlain(param->getNormalized())) * scale;
        }
        return defaultVal;
    };

    // Helper to get int32 from controller parameter
    auto getInt32 = [this](Steinberg::Vst::ParamID id, Steinberg::int32 defaultVal) -> Steinberg::int32 {
        if (auto* param = getParameterObject(id)) {
            return static_cast<Steinberg::int32>(param->toPlain(param->getNormalized()));
        }
        return defaultVal;
    };

    // Write global parameters (must match Processor::getState order)
    // Gain: normalized 0-1 maps to 0-2 linear
    float gain = static_cast<float>(getParamNormalized(kGainId) * 2.0);
    streamer.writeFloat(gain);

    // Mode (0-10)
    Steinberg::int32 mode = getInt32(kModeId, 0);
    streamer.writeInt32(mode);

    // Granular params - must match saveGranularParams order exactly
    streamer.writeFloat(getFloat(kGranularGrainSizeId, 100.0f));
    streamer.writeFloat(getFloat(kGranularDensityId, 10.0f));
    streamer.writeFloat(getFloat(kGranularDelayTimeId, 500.0f));
    streamer.writeFloat(getFloat(kGranularPitchId, 0.0f));
    streamer.writeFloat(getFloat(kGranularPitchSprayId, 0.0f));
    streamer.writeFloat(getFloat(kGranularPositionSprayId, 0.0f));
    streamer.writeFloat(getFloat(kGranularPanSprayId, 0.0f));
    streamer.writeFloat(getFloat(kGranularReverseProbId, 0.0f));
    streamer.writeInt32(getInt32(kGranularFreezeId, 0));
    streamer.writeFloat(getFloat(kGranularFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kGranularMixId, 0.5f));
    streamer.writeInt32(getInt32(kGranularEnvelopeTypeId, 0));
    streamer.writeInt32(getInt32(kGranularTimeModeId, 0));
    streamer.writeInt32(getInt32(kGranularNoteValueId, 4));
    streamer.writeFloat(getFloat(kGranularJitterId, 0.0f));
    streamer.writeInt32(getInt32(kGranularPitchQuantId, 0));
    streamer.writeFloat(getFloat(kGranularTextureId, 0.0f));
    streamer.writeFloat(getFloat(kGranularStereoWidthId, 0.0f));

    // Spectral params - must match saveSpectralParams order exactly
    streamer.writeInt32(getInt32(kSpectralFFTSizeId, 2048));
    streamer.writeFloat(getFloat(kSpectralBaseDelayId, 250.0f));
    streamer.writeFloat(getFloat(kSpectralSpreadId, 500.0f));
    streamer.writeInt32(getInt32(kSpectralSpreadDirectionId, 0));
    streamer.writeFloat(getFloat(kSpectralFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kSpectralFeedbackTiltId, 0.0f));
    streamer.writeInt32(getInt32(kSpectralFreezeId, 0));
    streamer.writeFloat(getFloat(kSpectralDiffusionId, 0.5f));
    streamer.writeFloat(getFloat(kSpectralMixId, 50.0f));
    streamer.writeInt32(getInt32(kSpectralSpreadCurveId, 0));
    streamer.writeFloat(getFloat(kSpectralStereoWidthId, 0.5f));
    streamer.writeInt32(getInt32(kSpectralTimeModeId, 0));
    streamer.writeInt32(getInt32(kSpectralNoteValueId, 4));

    // Ducking params - must match saveDuckingParams order exactly
    streamer.writeInt32(getInt32(kDuckingEnabledId, 0));
    streamer.writeFloat(getFloat(kDuckingThresholdId, -30.0f));
    streamer.writeFloat(getFloat(kDuckingDuckAmountId, 50.0f));
    streamer.writeFloat(getFloat(kDuckingAttackTimeId, 10.0f));
    streamer.writeFloat(getFloat(kDuckingReleaseTimeId, 200.0f));
    streamer.writeFloat(getFloat(kDuckingHoldTimeId, 50.0f));
    streamer.writeInt32(getInt32(kDuckingDuckTargetId, 0));
    streamer.writeInt32(getInt32(kDuckingSidechainFilterEnabledId, 0));
    streamer.writeFloat(getFloat(kDuckingSidechainFilterCutoffId, 80.0f));
    streamer.writeFloat(getFloat(kDuckingDelayTimeId, 500.0f));
    streamer.writeFloat(getFloat(kDuckingFeedbackId, 0.0f));
    streamer.writeFloat(getFloat(kDuckingMixId, 50.0f));

    // Freeze params - must match saveFreezeParams order exactly
    streamer.writeInt32(getInt32(kFreezeEnabledId, 0));
    streamer.writeFloat(getFloat(kFreezeDelayTimeId, 500.0f));
    streamer.writeFloat(getFloat(kFreezeFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kFreezePitchSemitonesId, 0.0f));
    streamer.writeFloat(getFloat(kFreezePitchCentsId, 0.0f));
    streamer.writeFloat(getFloat(kFreezeShimmerMixId, 0.0f));
    streamer.writeFloat(getFloat(kFreezeDecayId, 0.5f));
    streamer.writeFloat(getFloat(kFreezeDiffusionAmountId, 0.3f));
    streamer.writeFloat(getFloat(kFreezeDiffusionSizeId, 0.5f));
    streamer.writeInt32(getInt32(kFreezeFilterEnabledId, 0));
    streamer.writeInt32(getInt32(kFreezeFilterTypeId, 0));
    streamer.writeFloat(getFloat(kFreezeFilterCutoffId, 1000.0f));
    streamer.writeFloat(getFloat(kFreezeMixId, 0.5f));

    // Reverse params - must match saveReverseParams order exactly
    streamer.writeFloat(getFloat(kReverseChunkSizeId, 500.0f));
    streamer.writeFloat(getFloat(kReverseCrossfadeId, 50.0f));
    streamer.writeInt32(getInt32(kReversePlaybackModeId, 0));
    streamer.writeFloat(getFloat(kReverseFeedbackId, 0.0f));
    streamer.writeInt32(getInt32(kReverseFilterEnabledId, 0));
    streamer.writeFloat(getFloat(kReverseFilterCutoffId, 4000.0f));
    streamer.writeInt32(getInt32(kReverseFilterTypeId, 0));
    streamer.writeFloat(getFloat(kReverseMixId, 0.5f));

    // Shimmer params - must match saveShimmerParams order exactly
    streamer.writeFloat(getFloat(kShimmerDelayTimeId, 500.0f));
    streamer.writeFloat(getFloat(kShimmerPitchSemitonesId, 12.0f));
    streamer.writeFloat(getFloat(kShimmerPitchCentsId, 0.0f));
    streamer.writeFloat(getFloat(kShimmerPitchBlendId, 100.0f));
    streamer.writeFloat(getFloat(kShimmerFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kShimmerDiffusionAmountId, 50.0f));
    streamer.writeFloat(getFloat(kShimmerDiffusionSizeId, 50.0f));
    streamer.writeInt32(getInt32(kShimmerFilterEnabledId, 0));
    streamer.writeFloat(getFloat(kShimmerFilterCutoffId, 4000.0f));
    streamer.writeFloat(getFloat(kShimmerMixId, 50.0f));

    // Tape params - must match saveTapeParams order exactly
    streamer.writeFloat(getFloat(kTapeMotorSpeedId, 500.0f));
    streamer.writeFloat(getFloat(kTapeMotorInertiaId, 300.0f));
    streamer.writeFloat(getFloat(kTapeWearId, 0.3f));
    streamer.writeFloat(getFloat(kTapeSaturationId, 0.5f));
    streamer.writeFloat(getFloat(kTapeAgeId, 0.3f));
    streamer.writeInt32(getInt32(kTapeSpliceEnabledId, 0));
    streamer.writeFloat(getFloat(kTapeSpliceIntensityId, 0.5f));
    streamer.writeFloat(getFloat(kTapeFeedbackId, 0.4f));
    streamer.writeFloat(getFloat(kTapeMixId, 0.5f));
    streamer.writeInt32(getInt32(kTapeHead1EnabledId, 1));
    streamer.writeInt32(getInt32(kTapeHead2EnabledId, 0));
    streamer.writeInt32(getInt32(kTapeHead3EnabledId, 0));
    streamer.writeFloat(getFloat(kTapeHead1LevelId, 1.0f));
    streamer.writeFloat(getFloat(kTapeHead2LevelId, 1.0f));
    streamer.writeFloat(getFloat(kTapeHead3LevelId, 1.0f));
    streamer.writeFloat(getFloat(kTapeHead1PanId, 0.0f));
    streamer.writeFloat(getFloat(kTapeHead2PanId, 0.0f));
    streamer.writeFloat(getFloat(kTapeHead3PanId, 0.0f));

    // BBD params - must match saveBBDParams order exactly
    streamer.writeFloat(getFloat(kBBDDelayTimeId, 300.0f));
    streamer.writeFloat(getFloat(kBBDFeedbackId, 0.4f));
    streamer.writeFloat(getFloat(kBBDModDepthId, 0.0f));
    streamer.writeFloat(getFloat(kBBDModRateId, 0.5f));
    streamer.writeFloat(getFloat(kBBDAgeId, 0.2f));
    streamer.writeInt32(getInt32(kBBDEraId, 0));
    streamer.writeFloat(getFloat(kBBDMixId, 0.5f));

    // Digital params - must match saveDigitalParams order exactly
    streamer.writeFloat(getFloat(kDigitalDelayTimeId, 500.0f));
    streamer.writeInt32(getInt32(kDigitalTimeModeId, 0));
    streamer.writeInt32(getInt32(kDigitalNoteValueId, 4));
    streamer.writeFloat(getFloat(kDigitalFeedbackId, 0.5f));
    streamer.writeInt32(getInt32(kDigitalLimiterCharacterId, 0));
    streamer.writeInt32(getInt32(kDigitalEraId, 0));
    streamer.writeFloat(getFloat(kDigitalAgeId, 0.0f));
    streamer.writeFloat(getFloat(kDigitalModDepthId, 0.0f));
    streamer.writeFloat(getFloat(kDigitalModRateId, 0.5f));
    streamer.writeInt32(getInt32(kDigitalModWaveformId, 0));
    streamer.writeFloat(getFloat(kDigitalMixId, 0.5f));
    streamer.writeFloat(getFloat(kDigitalWidthId, 100.0f));

    // PingPong params - must match savePingPongParams order exactly
    streamer.writeFloat(getFloat(kPingPongDelayTimeId, 500.0f));
    streamer.writeInt32(getInt32(kPingPongTimeModeId, 1));
    streamer.writeInt32(getInt32(kPingPongNoteValueId, 4));
    streamer.writeInt32(getInt32(kPingPongLRRatioId, 0));
    streamer.writeFloat(getFloat(kPingPongFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kPingPongCrossFeedbackId, 1.0f));
    streamer.writeFloat(getFloat(kPingPongWidthId, 100.0f));
    streamer.writeFloat(getFloat(kPingPongModDepthId, 0.0f));
    streamer.writeFloat(getFloat(kPingPongModRateId, 1.0f));
    streamer.writeFloat(getFloat(kPingPongMixId, 0.5f));

    // MultiTap params - must match saveMultiTapParams order exactly
    streamer.writeInt32(getInt32(kMultiTapTimingPatternId, 2));
    streamer.writeInt32(getInt32(kMultiTapSpatialPatternId, 2));
    streamer.writeInt32(getInt32(kMultiTapTapCountId, 4));
    streamer.writeFloat(getFloat(kMultiTapBaseTimeId, 500.0f));
    streamer.writeFloat(getFloat(kMultiTapTempoId, 120.0f));
    streamer.writeFloat(getFloat(kMultiTapFeedbackId, 0.5f));
    streamer.writeFloat(getFloat(kMultiTapFeedbackLPCutoffId, 20000.0f));
    streamer.writeFloat(getFloat(kMultiTapFeedbackHPCutoffId, 20.0f));
    streamer.writeFloat(getFloat(kMultiTapMorphTimeId, 500.0f));
    streamer.writeFloat(getFloat(kMultiTapMixId, 50.0f));

    // Seek to beginning so the stream can be read
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);

    return stream;
}

// ==============================================================================
// Preset Loading Helpers
// ==============================================================================

void Controller::editParamWithNotify(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    using namespace Steinberg::Vst;

    // Clamp value to valid range
    value = std::max(0.0, std::min(1.0, value));

    // Full edit cycle to notify host of parameter change
    beginEdit(id);
    setParamNormalized(id, value);
    performEdit(id, value);
    endEdit(id);
}

bool Controller::loadComponentStateWithNotify(Steinberg::IBStream* state) {
    // ==========================================================================
    // Load component state with host notification
    // Same parsing as setComponentState(), but uses editParamWithNotify()
    // to propagate changes to the processor via the host
    // Order MUST match Processor::getState() and createComponentStateStream()
    // ==========================================================================

    if (!state) {
        return false;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    // Global parameters
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGainId, static_cast<double>(floatVal / 2.0f));
    }

    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kModeId, static_cast<double>(intVal) / 10.0);
    }

    // ==========================================================================
    // Granular params (must match saveGranularParams order)
    // ==========================================================================
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularGrainSizeId, (floatVal - 10.0f) / 490.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularDensityId, (floatVal - 1.0f) / 49.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularDelayTimeId, floatVal / 2000.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularPitchId, (floatVal + 24.0f) / 48.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularPitchSprayId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularPositionSprayId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularPanSprayId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularReverseProbId, static_cast<double>(floatVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kGranularFreezeId, static_cast<double>(intVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularFeedbackId, static_cast<double>(floatVal / 1.2f));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularMixId, static_cast<double>(floatVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kGranularEnvelopeTypeId, static_cast<double>(intVal) / 3.0);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kGranularTimeModeId, static_cast<double>(intVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kGranularNoteValueId, static_cast<double>(intVal) / 9.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularJitterId, static_cast<double>(floatVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kGranularPitchQuantId, static_cast<double>(intVal) / 4.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularTextureId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kGranularStereoWidthId, static_cast<double>(floatVal));
    }

    // ==========================================================================
    // Spectral params (must match saveSpectralParams order)
    // ==========================================================================
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kSpectralFFTSizeId, static_cast<double>(intVal) / 3.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kSpectralBaseDelayId, floatVal / 2000.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kSpectralSpreadId, floatVal / 2000.0f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kSpectralSpreadDirectionId, static_cast<double>(intVal) / 2.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kSpectralFeedbackId, static_cast<double>(floatVal / 1.2f));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kSpectralFeedbackTiltId, (floatVal + 1.0f) / 2.0f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kSpectralFreezeId, static_cast<double>(intVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kSpectralDiffusionId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kSpectralMixId, floatVal / 100.0f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kSpectralSpreadCurveId, static_cast<double>(intVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kSpectralStereoWidthId, static_cast<double>(floatVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kSpectralTimeModeId, static_cast<double>(intVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kSpectralNoteValueId, static_cast<double>(intVal) / 9.0);
    }

    // ==========================================================================
    // Ducking params (must match saveDuckingParams order)
    // ==========================================================================
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kDuckingEnabledId, static_cast<double>(intVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDuckingThresholdId, (floatVal + 60.0f) / 60.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDuckingDuckAmountId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDuckingAttackTimeId, (floatVal - 0.1f) / 99.9f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDuckingReleaseTimeId, (floatVal - 10.0f) / 1990.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDuckingHoldTimeId, floatVal / 500.0f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kDuckingDuckTargetId, static_cast<double>(intVal) / 2.0);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kDuckingSidechainFilterEnabledId, static_cast<double>(intVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDuckingSidechainFilterCutoffId, (floatVal - 20.0f) / 480.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDuckingDelayTimeId, (floatVal - 10.0f) / 4990.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDuckingFeedbackId, static_cast<double>(floatVal / 1.2f));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDuckingMixId, floatVal / 100.0f);
    }

    // ==========================================================================
    // Freeze params (must match saveFreezeParams order)
    // ==========================================================================
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kFreezeEnabledId, static_cast<double>(intVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kFreezeDelayTimeId, (floatVal - 10.0f) / 4990.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kFreezeFeedbackId, static_cast<double>(floatVal / 1.2f));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kFreezePitchSemitonesId, (floatVal + 24.0f) / 48.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kFreezePitchCentsId, (floatVal + 100.0f) / 200.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kFreezeShimmerMixId, floatVal / 100.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kFreezeDecayId, floatVal / 100.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kFreezeDiffusionAmountId, floatVal / 100.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kFreezeDiffusionSizeId, floatVal / 100.0f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kFreezeFilterEnabledId, static_cast<double>(intVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kFreezeFilterTypeId, static_cast<double>(intVal) / 2.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kFreezeFilterCutoffId, (floatVal - 20.0f) / 19980.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kFreezeMixId, floatVal / 100.0f);
    }

    // ==========================================================================
    // Reverse params (must match saveReverseParams order)
    // ==========================================================================
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kReverseChunkSizeId, (floatVal - 10.0f) / 1990.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kReverseCrossfadeId, floatVal / 100.0f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kReversePlaybackModeId, static_cast<double>(intVal) / 2.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kReverseFeedbackId, static_cast<double>(floatVal / 1.2f));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kReverseFilterEnabledId, static_cast<double>(intVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kReverseFilterCutoffId, (floatVal - 20.0f) / 19980.0f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kReverseFilterTypeId, static_cast<double>(intVal) / 2.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kReverseMixId, floatVal / 100.0f);
    }

    // ==========================================================================
    // Shimmer params (must match saveShimmerParams order)
    // ==========================================================================
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kShimmerDelayTimeId, (floatVal - 10.0f) / 4990.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kShimmerPitchSemitonesId, (floatVal + 24.0f) / 48.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kShimmerPitchCentsId, (floatVal + 100.0f) / 200.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kShimmerPitchBlendId, floatVal / 100.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kShimmerFeedbackId, static_cast<double>(floatVal / 1.2f));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kShimmerDiffusionAmountId, floatVal / 100.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kShimmerDiffusionSizeId, floatVal / 100.0f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kShimmerFilterEnabledId, static_cast<double>(intVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kShimmerFilterCutoffId, (floatVal - 20.0f) / 19980.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kShimmerMixId, floatVal / 100.0f);
    }

    // ==========================================================================
    // Tape params (must match saveTapeParams order)
    // ==========================================================================
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeMotorSpeedId, (floatVal - 20.0f) / 1980.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeMotorInertiaId, (floatVal - 100.0f) / 900.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeWearId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeSaturationId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeAgeId, static_cast<double>(floatVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kTapeSpliceEnabledId, static_cast<double>(intVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeSpliceIntensityId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeFeedbackId, static_cast<double>(floatVal / 1.2f));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeMixId, static_cast<double>(floatVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kTapeHead1EnabledId, static_cast<double>(intVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kTapeHead2EnabledId, static_cast<double>(intVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kTapeHead3EnabledId, static_cast<double>(intVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeHead1LevelId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeHead2LevelId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeHead3LevelId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeHead1PanId, (floatVal + 1.0f) / 2.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeHead2PanId, (floatVal + 1.0f) / 2.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kTapeHead3PanId, (floatVal + 1.0f) / 2.0f);
    }

    // ==========================================================================
    // BBD params (must match saveBBDParams order)
    // ==========================================================================
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kBBDDelayTimeId, (floatVal - 20.0f) / 980.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kBBDFeedbackId, static_cast<double>(floatVal / 1.2f));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kBBDModDepthId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kBBDModRateId, (floatVal - 0.1f) / 9.9f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kBBDAgeId, static_cast<double>(floatVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kBBDEraId, static_cast<double>(intVal) / 3.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kBBDMixId, static_cast<double>(floatVal));
    }

    // ==========================================================================
    // Digital params (must match saveDigitalParams order)
    // ==========================================================================
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDigitalDelayTimeId, (floatVal - 1.0f) / 4999.0f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kDigitalTimeModeId, static_cast<double>(intVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kDigitalNoteValueId, static_cast<double>(intVal) / 9.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDigitalFeedbackId, static_cast<double>(floatVal / 1.2f));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kDigitalLimiterCharacterId, static_cast<double>(intVal) / 2.0);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kDigitalEraId, static_cast<double>(intVal) / 3.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDigitalAgeId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDigitalModDepthId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDigitalModRateId, (floatVal - 0.1f) / 9.9f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kDigitalModWaveformId, static_cast<double>(intVal) / 2.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDigitalMixId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kDigitalWidthId, floatVal / 200.0f);
    }

    // ==========================================================================
    // PingPong params (must match savePingPongParams order)
    // ==========================================================================
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kPingPongDelayTimeId, (floatVal - 1.0f) / 4999.0f);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kPingPongTimeModeId, static_cast<double>(intVal));
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kPingPongNoteValueId, static_cast<double>(intVal) / 9.0);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kPingPongLRRatioId, static_cast<double>(intVal) / 4.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kPingPongFeedbackId, static_cast<double>(floatVal / 1.2f));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kPingPongCrossFeedbackId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kPingPongWidthId, floatVal / 200.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kPingPongModDepthId, static_cast<double>(floatVal));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kPingPongModRateId, (floatVal - 0.1f) / 9.9f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kPingPongMixId, static_cast<double>(floatVal));
    }

    // ==========================================================================
    // MultiTap params (must match saveMultiTapParams order)
    // ==========================================================================
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kMultiTapTimingPatternId, static_cast<double>(intVal) / 19.0);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kMultiTapSpatialPatternId, static_cast<double>(intVal) / 6.0);
    }
    if (streamer.readInt32(intVal)) {
        editParamWithNotify(kMultiTapTapCountId, static_cast<double>(intVal - 2) / 14.0);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kMultiTapBaseTimeId, (floatVal - 1.0f) / 4999.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kMultiTapTempoId, (floatVal - 20.0f) / 280.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kMultiTapFeedbackId, static_cast<double>(floatVal / 1.1f));
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kMultiTapFeedbackLPCutoffId, (floatVal - 20.0f) / 19980.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kMultiTapFeedbackHPCutoffId, (floatVal - 20.0f) / 480.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kMultiTapMorphTimeId, (floatVal - 50.0f) / 1950.0f);
    }
    if (streamer.readFloat(floatVal)) {
        editParamWithNotify(kMultiTapMixId, floatVal / 100.0f);
    }

    return true;
}

} // namespace Iterum
