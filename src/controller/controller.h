#pragma once

// ==============================================================================
// Edit Controller
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation
// - This is the Controller component (IEditController)
// - MUST be completely separate from Processor
// - Runs on UI thread, NOT audio thread
//
// Constitution Principle V: VSTGUI Development
// - Use UIDescription for UI layout
// - Implement VST3EditorDelegate for custom views
// - UI thread MUST NEVER directly access audio data
// ==============================================================================

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "preset/preset_manager.h"

#include <memory>

namespace Iterum {

// Forward declarations
class PresetBrowserView;
class SavePresetDialogView;

// ==============================================================================
// Controller Class
// ==============================================================================

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate {
public:
    Controller() = default;
    ~Controller() override;  // Defined in cpp to allow unique_ptr with forward declaration

    // ===========================================================================
    // IPluginBase
    // ===========================================================================

    /// Called when the controller is first loaded
    Steinberg::tresult PLUGIN_API initialize(FUnknown* context) override;

    /// Called when the controller is unloaded
    Steinberg::tresult PLUGIN_API terminate() override;

    // ===========================================================================
    // IEditController
    // ===========================================================================

    /// Receive processor state and synchronize controller
    /// Constitution Principle I: Controller syncs TO processor state
    Steinberg::tresult PLUGIN_API setComponentState(
        Steinberg::IBStream* state) override;

    /// Save controller-specific state (UI settings, etc.)
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    /// Restore controller-specific state
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;

    /// Create the plugin editor (UI)
    /// Constitution Principle V: Use VSTGUI UIDescription
    Steinberg::IPlugView* PLUGIN_API createView(
        Steinberg::FIDString name) override;

    /// Convert normalized parameter value to string for display
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;

    /// Convert string to normalized parameter value
    Steinberg::tresult PLUGIN_API getParamValueByString(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::TChar* string,
        Steinberg::Vst::ParamValue& valueNormalized) override;

    /// Handle parameter changes - DEBUG: logs all Mode parameter changes
    Steinberg::tresult PLUGIN_API setParamNormalized(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue value) override;

    // ===========================================================================
    // VST3EditorDelegate (VSTGUI)
    // ===========================================================================

    /// Create custom views based on view name
    /// Constitution Principle V: Implement for custom view creation
    VSTGUI::CView* createCustomView(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;

    /// Called when the editor is opened
    void didOpen(VSTGUI::VST3Editor* editor) override;

    /// Called when the editor is about to close
    void willClose(VSTGUI::VST3Editor* editor) override;

    // ===========================================================================
    // Preset Browser (Spec 042)
    // ===========================================================================

    /// Open the preset browser modal
    void openPresetBrowser();

    /// Close the preset browser modal
    void closePresetBrowser();

    /// Open standalone save preset dialog (quick save from main UI)
    void openSavePresetDialog();

    /// Get the preset manager instance
    PresetManager* getPresetManager() const { return presetManager_.get(); }

    /// Create a memory stream containing the current component state
    /// Used for preset saving - serializes controller's parameter values
    /// in the same format as Processor::getState()
    /// @return New MemoryStream (caller must release), or nullptr on failure
    Steinberg::MemoryStream* createComponentStateStream();

    // ===========================================================================
    // Factory
    // ===========================================================================

    static FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    // ===========================================================================
    // Interface Support
    // ===========================================================================

    // Declare COM-style interface support
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IEditController)
        DEF_INTERFACE(Steinberg::Vst::IEditController2)
    END_DEFINE_INTERFACES(EditController)

    DELEGATE_REFCOUNT(EditController)

private:
    // ==========================================================================
    // UI State
    // ==========================================================================

    // Active editor instance
    VSTGUI::VST3Editor* activeEditor_ = nullptr;

    // Visibility controllers for conditional control visibility (thread-safe)
    // Uses IDependent mechanism to receive parameter changes on UI thread
    Steinberg::IPtr<Steinberg::FObject> digitalDelayTimeVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> digitalAgeVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> pingPongDelayTimeVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> granularDelayTimeVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> spectralBaseDelayVisibilityController_;  // spec 041

    // Tempo sync visibility controllers (hide delay time when synced)
    Steinberg::IPtr<Steinberg::FObject> shimmerDelayTimeVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> bbdDelayTimeVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> reverseChunkSizeVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> multitapBaseTimeVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> freezeDelayTimeVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> duckingDelayTimeVisibilityController_;

    // ==========================================================================
    // Preset Browser (Spec 042)
    // ==========================================================================

    std::unique_ptr<PresetManager> presetManager_;
    PresetBrowserView* presetBrowserView_ = nullptr;  // Owned by frame
    SavePresetDialogView* savePresetDialogView_ = nullptr;  // Owned by frame

    // ==========================================================================
    // Preset Loading Helpers
    // ==========================================================================

    /// Edit a parameter with full notification (beginEdit + setParamNormalized + performEdit + endEdit)
    /// Used when loading presets to notify the host of parameter changes
    void editParamWithNotify(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

    /// Load component state from stream with host notification
    /// Same parsing as setComponentState(), but calls performEdit to propagate changes to processor
    /// @param state Stream containing component state in Processor::getState() format
    /// @return true on success
    bool loadComponentStateWithNotify(Steinberg::IBStream* state);
};

} // namespace Iterum
