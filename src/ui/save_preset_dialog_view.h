#pragma once

// ==============================================================================
// SavePresetDialogView - Standalone Save Preset Dialog
// ==============================================================================
// Spec 042: Preset Browser
// Standalone modal overlay for quick preset saving from the main UI.
// This is a simplified version of the save dialog in PresetBrowserView.
//
// Constitution Compliance:
// - Principle V: Uses VSTGUI components only
// - Principle VI: Cross-platform (no native code)
// ==============================================================================

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/icontrollistener.h"

namespace Iterum {

class PresetManager;

// Button tag constants
enum SavePresetDialogTags {
    kSavePresetDialogSaveTag = 100,
    kSavePresetDialogCancelTag = 101,
    kSavePresetDialogNameFieldTag = 102
};

class SavePresetDialogView : public VSTGUI::CViewContainer,
                              public VSTGUI::IControlListener,
                              public VSTGUI::IKeyboardHook {
public:
    SavePresetDialogView(const VSTGUI::CRect& size, PresetManager* presetManager);
    ~SavePresetDialogView() override;

    // Lifecycle
    void open(int currentMode);
    void close();
    bool isOpen() const { return isOpen_; }

    // CView overrides
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons
    ) override;

    // IControlListener
    void valueChanged(VSTGUI::CControl* control) override;

    // IKeyboardHook
    void onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* frame) override;

private:
    PresetManager* presetManager_ = nullptr;

    // Dialog components
    VSTGUI::CViewContainer* dialogBox_ = nullptr;
    VSTGUI::CTextLabel* titleLabel_ = nullptr;
    VSTGUI::CTextEdit* nameField_ = nullptr;
    VSTGUI::CTextButton* saveButton_ = nullptr;
    VSTGUI::CTextButton* cancelButton_ = nullptr;

    // State
    int currentMode_ = 0;
    bool isOpen_ = false;

    void createDialogViews();
    void onSaveConfirm();

    // Keyboard hook registration
    void registerKeyboardHook();
    void unregisterKeyboardHook();
    bool keyboardHookRegistered_ = false;
};

} // namespace Iterum
