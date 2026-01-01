#pragma once

// ==============================================================================
// PresetBrowserView - Modal Popup for Preset Management
// ==============================================================================
// Spec 042: Preset Browser
// Modal overlay containing mode tabs, preset list, search, and action buttons.
//
// Constitution Compliance:
// - Principle V: Uses VSTGUI components only
// - Principle VI: Cross-platform (no native code)
// ==============================================================================

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cframe.h"  // For IKeyboardHook
#include "vstgui/lib/cdatabrowser.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/icontrollistener.h"

namespace Iterum {

class PresetManager;
class PresetDataSource;
class ModeTabBar;

// Button tag constants for IControlListener
enum PresetBrowserButtonTags {
    kSaveButtonTag = 1,
    kImportButtonTag = 3,
    kDeleteButtonTag = 4,
    kCloseButtonTag = 5,
    // Save dialog buttons
    kSaveDialogSaveTag = 10,
    kSaveDialogCancelTag = 11,
    // Save dialog name field (for Enter key detection)
    kSaveDialogNameFieldTag = 12,
    // Delete confirmation dialog buttons
    kDeleteDialogConfirmTag = 20,
    kDeleteDialogCancelTag = 21,
    // Overwrite confirmation dialog buttons
    kOverwriteDialogConfirmTag = 30,
    kOverwriteDialogCancelTag = 31
};

class PresetBrowserView : public VSTGUI::CViewContainer,
                          public VSTGUI::IControlListener,
                          public VSTGUI::IKeyboardHook {
public:
    PresetBrowserView(const VSTGUI::CRect& size, PresetManager* presetManager);
    ~PresetBrowserView() override;

    // Lifecycle
    void open(int currentMode);
    void openWithSaveDialog(int currentMode);  // Opens browser with save dialog immediately visible
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

    // IKeyboardHook - intercepts keyboard events at frame level BEFORE focus view
    void onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* frame) override;

    // Callbacks
    void onModeTabChanged(int newMode);
    void onSearchTextChanged(const std::string& text);
    void onPresetSelected(int rowIndex);
    void onPresetDoubleClicked(int rowIndex);
    void onSaveClicked();
    void onImportClicked();
    void onDeleteClicked();
    void onCloseClicked();

private:
    PresetManager* presetManager_ = nullptr;

    // Child views (owned by CViewContainer)
    ModeTabBar* modeTabBar_ = nullptr;
    VSTGUI::CDataBrowser* presetList_ = nullptr;
    VSTGUI::CTextEdit* searchField_ = nullptr;
    VSTGUI::CTextButton* saveButton_ = nullptr;
    VSTGUI::CTextButton* importButton_ = nullptr;
    VSTGUI::CTextButton* deleteButton_ = nullptr;
    VSTGUI::CTextButton* closeButton_ = nullptr;

    // Data source (owns this)
    PresetDataSource* dataSource_ = nullptr;

    // State
    int currentModeFilter_ = -1;  // -1 = All
    int selectedPresetIndex_ = -1;
    bool isOpen_ = false;

    // Save dialog components (inline overlay)
    VSTGUI::CViewContainer* saveDialogOverlay_ = nullptr;
    VSTGUI::CTextEdit* saveDialogNameField_ = nullptr;
    VSTGUI::CTextButton* saveDialogSaveButton_ = nullptr;
    VSTGUI::CTextButton* saveDialogCancelButton_ = nullptr;
    bool saveDialogVisible_ = false;

    // Delete confirmation dialog components (inline overlay)
    VSTGUI::CViewContainer* deleteDialogOverlay_ = nullptr;
    VSTGUI::CTextLabel* deleteDialogLabel_ = nullptr;
    VSTGUI::CTextButton* deleteDialogConfirmButton_ = nullptr;
    VSTGUI::CTextButton* deleteDialogCancelButton_ = nullptr;

    // Overwrite confirmation dialog components (inline overlay)
    VSTGUI::CViewContainer* overwriteDialogOverlay_ = nullptr;
    VSTGUI::CTextLabel* overwriteDialogLabel_ = nullptr;
    VSTGUI::CTextButton* overwriteDialogConfirmButton_ = nullptr;
    VSTGUI::CTextButton* overwriteDialogCancelButton_ = nullptr;
    int overwriteTargetIndex_ = -1;  // Index of preset to overwrite

    void createChildViews();
    void createDialogViews();
    void refreshPresetList();
    void updateButtonStates();
    void showSaveDialog();
    void hideSaveDialog();
    void onSaveDialogConfirm();
    void showConfirmDelete();
    void hideDeleteDialog();
    void onDeleteDialogConfirm();
    void showConfirmOverwrite();
    void hideOverwriteDialog();
    void onOverwriteDialogConfirm();

    // Keyboard hook registration
    void registerKeyboardHook();
    void unregisterKeyboardHook();
    bool keyboardHookRegistered_ = false;
};

} // namespace Iterum
