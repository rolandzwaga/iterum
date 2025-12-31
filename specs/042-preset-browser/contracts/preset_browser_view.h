// Contract: PresetBrowserView Interface
// This is a design contract, not actual implementation code.

#pragma once

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cdatabrowser.h"

namespace Iterum {

class PresetManager;
class PresetDataSource;
class ModeTabBar;

/**
 * PresetBrowserView is a modal popup overlay for preset management.
 *
 * Layout:
 * +----------------------------------------------+
 * | [X] Preset Browser                           |
 * +----------+-----------------------------------+
 * | Mode     | Preset List (CDataBrowser)        |
 * | Tabs     | Name         | Category           |
 * +----------+-----------------------------------+
 * |          | [Search: ___________] [Clear]     |
 * +----------+-----------------------------------+
 * | [Save] [Save As] [Import] [Delete]  [Close]  |
 * +----------------------------------------------+
 *
 * Constitution Compliance:
 * - Principle V: Uses VSTGUI components only
 * - Principle VI: Cross-platform (no native code)
 */
class PresetBrowserView : public VSTGUI::CViewContainer {
public:
    /**
     * Constructor.
     * @param size View bounds
     * @param presetManager Reference to preset manager
     */
    PresetBrowserView(
        const VSTGUI::CRect& size,
        PresetManager* presetManager
    );

    ~PresetBrowserView() override;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * Open the browser, refreshing preset list.
     * @param currentMode Current plugin mode (for default filter)
     */
    void open(int currentMode);

    /**
     * Close the browser.
     */
    void close();

    /**
     * Check if browser is currently open.
     */
    bool isOpen() const;

    // ========================================================================
    // CView Overrides
    // ========================================================================

    void draw(VSTGUI::CDrawContext* context) override;

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons
    ) override;

    bool onKeyDown(VSTGUI::VstKeyCode& keyCode) override;

    // ========================================================================
    // Callbacks (internal use)
    // ========================================================================

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
    PresetManager* presetManager_;

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
