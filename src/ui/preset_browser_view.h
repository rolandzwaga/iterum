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
#include "vstgui/lib/cdatabrowser.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/cbuttons.h"

namespace Iterum {

class PresetManager;
class PresetDataSource;
class ModeTabBar;

class PresetBrowserView : public VSTGUI::CViewContainer {
public:
    PresetBrowserView(const VSTGUI::CRect& size, PresetManager* presetManager);
    ~PresetBrowserView() override;

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
    void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override;

    // Callbacks
    void onModeTabChanged(int newMode);
    void onSearchTextChanged(const std::string& text);
    void onPresetSelected(int rowIndex);
    void onPresetDoubleClicked(int rowIndex);
    void onSaveClicked();
    void onSaveAsClicked();
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
    VSTGUI::CTextButton* saveAsButton_ = nullptr;
    VSTGUI::CTextButton* importButton_ = nullptr;
    VSTGUI::CTextButton* deleteButton_ = nullptr;
    VSTGUI::CTextButton* closeButton_ = nullptr;

    // Data source (owns this)
    PresetDataSource* dataSource_ = nullptr;

    // State
    int currentModeFilter_ = -1;  // -1 = All
    int selectedPresetIndex_ = -1;
    bool isOpen_ = false;

    void createChildViews();
    void refreshPresetList();
    void updateButtonStates();
    void showSaveDialog();
    void showConfirmDelete();
};

} // namespace Iterum
