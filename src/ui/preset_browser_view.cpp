#include "preset_browser_view.h"
#include "preset_browser_logic.h"
#include "preset_data_source.h"
#include "mode_tab_bar.h"
#include "../preset/preset_manager.h"

#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/controls/ctextedit.h"

namespace Iterum {

// =============================================================================
// DialogButton - Custom button that doesn't consume Enter/Escape events
// =============================================================================
// CTextButton's onKeyboardEvent() consumes Enter/Return events, preventing
// them from reaching PresetBrowserView::onKeyboardEvent(). This subclass
// skips handling for Enter and Escape, letting the parent dialog handle them.
// =============================================================================

class DialogButton : public VSTGUI::CTextButton {
public:
    using CTextButton::CTextButton;

    void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override {
        // Don't consume Enter/Escape - let parent handle dialog confirmation
        if (event.virt == VSTGUI::VirtualKey::Return ||
            event.virt == VSTGUI::VirtualKey::Enter ||
            event.virt == VSTGUI::VirtualKey::Escape) {
            return;  // Skip, don't consume
        }
        CTextButton::onKeyboardEvent(event);
    }
};

// =============================================================================
// Layout Constants
// =============================================================================

namespace Layout {
    // Content area (centered within full view)
    constexpr float kContentMargin = 40.0f;
    constexpr float kTitleBarHeight = 32.0f;
    constexpr float kSearchHeight = 28.0f;
    constexpr float kButtonBarHeight = 36.0f;
    constexpr float kModeTabWidth = 100.0f;
    constexpr float kButtonWidth = 80.0f;
    constexpr float kButtonSpacing = 8.0f;
    constexpr float kInnerPadding = 8.0f;
}

// =============================================================================
// Construction / Destruction
// =============================================================================

PresetBrowserView::PresetBrowserView(const VSTGUI::CRect& size, PresetManager* presetManager)
    : CViewContainer(size)
    , presetManager_(presetManager)
{
    setBackgroundColor(VSTGUI::CColor(0, 0, 0, 180)); // Semi-transparent overlay
    createChildViews();
}

PresetBrowserView::~PresetBrowserView() {
    // Ensure keyboard hook is unregistered
    unregisterKeyboardHook();
    // DataSource is owned by us, not by CDataBrowser
    delete dataSource_;
}

// =============================================================================
// Lifecycle
// =============================================================================

void PresetBrowserView::open(int currentMode) {
    currentModeFilter_ = currentMode;
    isOpen_ = true;
    setVisible(true);

    // Register keyboard hook to intercept Enter/Escape at frame level
    registerKeyboardHook();

    // Set mode tab to current mode (0 = All, 1+ = modes)
    if (modeTabBar_) {
        int tabIndex = (currentMode < 0) ? 0 : (currentMode + 1);
        modeTabBar_->setSelectedTab(tabIndex);
    }

    refreshPresetList();
    updateButtonStates();
}

void PresetBrowserView::openWithSaveDialog(int currentMode) {
    // Open the browser normally first
    open(currentMode);
    // Then immediately show the save dialog
    showSaveDialog();
}

void PresetBrowserView::close() {
    // Unregister keyboard hook
    unregisterKeyboardHook();

    isOpen_ = false;
    setVisible(false);
}

// =============================================================================
// Drawing
// =============================================================================

void PresetBrowserView::draw(VSTGUI::CDrawContext* context) {
    // Draw semi-transparent background overlay
    CViewContainer::draw(context);

    // Calculate content rect (centered popup area)
    auto viewSize = getViewSize();
    auto contentRect = VSTGUI::CRect(
        viewSize.left + Layout::kContentMargin,
        viewSize.top + Layout::kContentMargin,
        viewSize.right - Layout::kContentMargin,
        viewSize.bottom - Layout::kContentMargin
    );

    // Draw content background
    context->setFillColor(VSTGUI::CColor(50, 50, 55));
    context->drawRect(contentRect, VSTGUI::kDrawFilled);

    // Draw border
    context->setFrameColor(VSTGUI::CColor(80, 80, 85));
    context->setLineWidth(1.0);
    context->drawRect(contentRect, VSTGUI::kDrawStroked);

    // Draw title bar
    auto titleRect = contentRect;
    titleRect.bottom = titleRect.top + Layout::kTitleBarHeight;
    context->setFillColor(VSTGUI::CColor(35, 35, 40));
    context->drawRect(titleRect, VSTGUI::kDrawFilled);

    // Draw title text
    context->setFontColor(VSTGUI::CColor(255, 255, 255));
    auto titleTextRect = titleRect;
    titleTextRect.inset(12, 0);
    context->drawString("Preset Browser", titleTextRect, VSTGUI::kLeftText);
}

// =============================================================================
// Event Handling
// =============================================================================

VSTGUI::CMouseEventResult PresetBrowserView::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons
) {
    // Calculate content area
    auto viewSize = getViewSize();
    auto contentRect = VSTGUI::CRect(
        viewSize.left + Layout::kContentMargin,
        viewSize.top + Layout::kContentMargin,
        viewSize.right - Layout::kContentMargin,
        viewSize.bottom - Layout::kContentMargin
    );

    // Click outside content area closes the browser (FR-018)
    // But only if no dialog is open (dialogs should handle their own dismissal)
    bool anyDialogVisible = (saveDialogOverlay_ && saveDialogOverlay_->isVisible()) ||
                            (deleteDialogOverlay_ && deleteDialogOverlay_->isVisible()) ||
                            (overwriteDialogOverlay_ && overwriteDialogOverlay_->isVisible());

    if (!anyDialogVisible && !contentRect.pointInside(where)) {
        close();
        return VSTGUI::kMouseEventHandled;
    }

    // Check if click is in empty space of the preset list (below all rows)
    // CDataBrowser consumes empty space clicks without calling delegate
    // Skip this check when a dialog is visible - let the dialog receive the click
    if (!anyDialogVisible && presetList_ && dataSource_) {
        auto listBounds = presetList_->getViewSize();
        if (listBounds.pointInside(where)) {
            // Convert to list-local coordinates
            auto localY = where.y - listBounds.top;

            // Calculate content height
            auto numRows = dataSource_->dbGetNumRows(presetList_);
            auto rowHeight = dataSource_->dbGetRowHeight(presetList_);
            auto contentHeight = numRows * rowHeight;

            // Click is in empty space if below all rows
            if (localY >= contentHeight) {
                presetList_->unselectAll();
                selectedPresetIndex_ = -1;
                updateButtonStates();
                return VSTGUI::kMouseEventHandled;
            }
        }
    }

    // Let child views handle the click
    return CViewContainer::onMouseDown(where, buttons);
}

// IKeyboardHook implementation - intercepts keyboard events at frame level
// BEFORE they reach the focus view. This ensures Enter/Escape work regardless
// of which control has focus.
void PresetBrowserView::onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* /*frame*/) {
    // Only handle when browser is open
    if (!isOpen_) {
        return;
    }

    // Only handle key down events
    if (event.type != VSTGUI::EventType::KeyDown) {
        return;
    }

    // Map VSTGUI virtual key to our testable KeyCode
    KeyCode keyCode = KeyCode::Other;
    if (event.virt == VSTGUI::VirtualKey::Escape) {
        keyCode = KeyCode::Escape;
    } else if (event.virt == VSTGUI::VirtualKey::Return ||
               event.virt == VSTGUI::VirtualKey::Enter) {
        keyCode = KeyCode::Enter;
    }

    // Determine dialog visibility state
    bool saveVisible = (saveDialogOverlay_ && saveDialogOverlay_->isVisible());
    bool deleteVisible = (deleteDialogOverlay_ && deleteDialogOverlay_->isVisible());
    bool overwriteVisible = (overwriteDialogOverlay_ && overwriteDialogOverlay_->isVisible());

    // Use pure function to determine action
    KeyAction action = determineKeyAction(keyCode, saveVisible, deleteVisible, overwriteVisible);

    // Execute the action
    switch (action) {
        case KeyAction::CloseBrowser:
            close();
            event.consumed = true;
            return;

        case KeyAction::ConfirmSaveDialog:
            onSaveDialogConfirm();
            event.consumed = true;
            return;

        case KeyAction::CancelSaveDialog:
            hideSaveDialog();
            event.consumed = true;
            return;

        case KeyAction::ConfirmDeleteDialog:
            onDeleteDialogConfirm();
            event.consumed = true;
            return;

        case KeyAction::CancelDeleteDialog:
            hideDeleteDialog();
            event.consumed = true;
            return;

        case KeyAction::ConfirmOverwriteDialog:
            onOverwriteDialogConfirm();
            event.consumed = true;
            return;

        case KeyAction::CancelOverwriteDialog:
            hideOverwriteDialog();
            event.consumed = true;
            return;

        case KeyAction::None:
        default:
            // Don't consume - let normal focus-based handling occur
            break;
    }
}

// =============================================================================
// IControlListener
// =============================================================================

void PresetBrowserView::valueChanged(VSTGUI::CControl* control) {
    if (!control) return;

    // Route button clicks to appropriate handlers
    switch (control->getTag()) {
        case kSaveButtonTag:
            onSaveClicked();
            break;
        case kImportButtonTag:
            onImportClicked();
            break;
        case kDeleteButtonTag:
            onDeleteClicked();
            break;
        case kCloseButtonTag:
            onCloseClicked();
            break;
        case kSaveDialogSaveTag:
            onSaveDialogConfirm();
            break;
        case kSaveDialogCancelTag:
            hideSaveDialog();
            break;
        // Note: kSaveDialogNameFieldTag intentionally not handled here.
        // CTextEdit's valueChanged fires on ANY focus loss (including clicking Cancel),
        // not just Enter key. Enter key confirmation is handled via IKeyboardHook.
        case kDeleteDialogConfirmTag:
            onDeleteDialogConfirm();
            break;
        case kDeleteDialogCancelTag:
            hideDeleteDialog();
            break;
        case kOverwriteDialogConfirmTag:
            onOverwriteDialogConfirm();
            break;
        case kOverwriteDialogCancelTag:
            hideOverwriteDialog();
            break;
        default:
            break;
    }
}

// =============================================================================
// Callbacks
// =============================================================================

void PresetBrowserView::onModeTabChanged(int newMode) {
    currentModeFilter_ = newMode;
    if (dataSource_) {
        dataSource_->setModeFilter(newMode);
    }
    if (presetList_) {
        presetList_->recalculateLayout(true);
        presetList_->invalid();
    }
    selectedPresetIndex_ = -1;
    updateButtonStates();
}

void PresetBrowserView::onSearchTextChanged(const std::string& text) {
    if (dataSource_) {
        dataSource_->setSearchFilter(text);
    }
    if (presetList_) {
        presetList_->recalculateLayout(true);
        presetList_->invalid();
    }
    selectedPresetIndex_ = -1;
    updateButtonStates();
}

void PresetBrowserView::onPresetSelected(int rowIndex) {
    selectedPresetIndex_ = rowIndex;
    updateButtonStates();
}

void PresetBrowserView::onPresetDoubleClicked(int rowIndex) {
    if (!presetManager_ || !dataSource_) return;

    const PresetInfo* preset = dataSource_->getPresetAtRow(rowIndex);
    if (!preset) return;

    // Load the preset
    if (presetManager_->loadPreset(*preset)) {
        // Close browser on successful load
        close();
    }
}

void PresetBrowserView::onSaveClicked() {
    // Check if a user preset is selected - if so, offer to overwrite it
    if (selectedPresetIndex_ >= 0 && dataSource_) {
        const PresetInfo* preset = dataSource_->getPresetAtRow(selectedPresetIndex_);
        if (preset && !preset->isFactory) {
            showConfirmOverwrite();
            return;
        }
    }
    // No user preset selected - show save-as dialog
    showSaveDialog();
}

void PresetBrowserView::onImportClicked() {
    // Use VSTGUI file selector for cross-platform compatibility
    auto frame = getFrame();
    if (!frame) return;

    auto selector = VSTGUI::owned(VSTGUI::CNewFileSelector::create(
        frame,
        VSTGUI::CNewFileSelector::kSelectFile
    ));

    if (!selector) return;

    selector->setTitle("Import Preset");
    VSTGUI::CFileExtension vstPresetExt("VST3 Preset", "vstpreset");
    selector->setDefaultExtension(vstPresetExt);
    selector->addFileExtension(vstPresetExt);

    // Run the file selector
    selector->run([this](VSTGUI::CNewFileSelector* sel) {
        if (sel->getNumSelectedFiles() > 0) {
            auto path = std::filesystem::path(sel->getSelectedFile(0));
            if (presetManager_ && presetManager_->importPreset(path)) {
                refreshPresetList();
            }
        }
    });
}

void PresetBrowserView::onDeleteClicked() {
    showConfirmDelete();
}

void PresetBrowserView::onCloseClicked() {
    close();
}

// =============================================================================
// Private Methods
// =============================================================================

void PresetBrowserView::createChildViews() {
    auto viewSize = getViewSize();

    // Calculate content area (inside the overlay margin)
    auto contentRect = VSTGUI::CRect(
        viewSize.left + Layout::kContentMargin,
        viewSize.top + Layout::kContentMargin,
        viewSize.right - Layout::kContentMargin,
        viewSize.bottom - Layout::kContentMargin
    );

    // Inner content (after title bar)
    auto innerTop = contentRect.top + Layout::kTitleBarHeight + Layout::kInnerPadding;
    auto innerBottom = contentRect.bottom - Layout::kButtonBarHeight - Layout::kInnerPadding;

    // ==========================================================================
    // Mode Tab Bar (left side)
    // ==========================================================================
    auto modeTabRect = VSTGUI::CRect(
        contentRect.left + Layout::kInnerPadding,
        innerTop,
        contentRect.left + Layout::kInnerPadding + Layout::kModeTabWidth,
        innerBottom
    );
    modeTabBar_ = new ModeTabBar(modeTabRect);
    modeTabBar_->setSelectionCallback([this](int mode) {
        onModeTabChanged(mode);
    });
    addView(modeTabBar_);

    // ==========================================================================
    // Search Field (above preset list)
    // ==========================================================================
    auto browserLeft = modeTabRect.right + Layout::kInnerPadding;
    auto browserRight = contentRect.right - Layout::kInnerPadding;

    auto searchRect = VSTGUI::CRect(
        browserLeft,
        innerTop,
        browserRight,
        innerTop + Layout::kSearchHeight
    );
    searchField_ = new VSTGUI::CTextEdit(searchRect, nullptr, -1, "Search presets...");
    searchField_->setBackColor(VSTGUI::CColor(35, 35, 40));
    searchField_->setFontColor(VSTGUI::CColor(200, 200, 200));
    searchField_->setFrameColor(VSTGUI::CColor(70, 70, 75));
    searchField_->setStyle(VSTGUI::CTextEdit::kRoundRectStyle);
    // Note: Text change callback would be set via ITextEditListener in a full implementation
    addView(searchField_);

    // ==========================================================================
    // Preset List (CDataBrowser)
    // ==========================================================================
    auto listRect = VSTGUI::CRect(
        browserLeft,
        searchRect.bottom + Layout::kInnerPadding,
        browserRight,
        innerBottom
    );

    // Create data source
    dataSource_ = new PresetDataSource();
    dataSource_->setSelectionCallback([this](int row) {
        onPresetSelected(row);
    });
    dataSource_->setDoubleClickCallback([this](int row) {
        onPresetDoubleClicked(row);
    });

    // Create browser with data source
    presetList_ = new VSTGUI::CDataBrowser(
        listRect,
        dataSource_,
        VSTGUI::CDataBrowser::kDrawRowLines | VSTGUI::CDataBrowser::kDrawColumnLines,
        VSTGUI::CScrollView::kAutoHideScrollbars
    );
    presetList_->setBackgroundColor(VSTGUI::CColor(40, 40, 45));
    addView(presetList_);

    // ==========================================================================
    // Button Bar (bottom)
    // ==========================================================================
    auto buttonY = contentRect.bottom - Layout::kButtonBarHeight;
    auto buttonHeight = Layout::kButtonBarHeight - Layout::kInnerPadding;
    auto currentX = contentRect.left + Layout::kInnerPadding;

    // Save button
    auto saveRect = VSTGUI::CRect(
        currentX, buttonY,
        currentX + Layout::kButtonWidth, buttonY + buttonHeight
    );
    saveButton_ = new VSTGUI::CTextButton(saveRect, this, kSaveButtonTag, "Save");
    saveButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
    saveButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
    addView(saveButton_);
    currentX += Layout::kButtonWidth + Layout::kButtonSpacing;

    // Import button
    auto importRect = VSTGUI::CRect(
        currentX, buttonY,
        currentX + Layout::kButtonWidth, buttonY + buttonHeight
    );
    importButton_ = new VSTGUI::CTextButton(importRect, this, kImportButtonTag, "Import...");
    importButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
    importButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
    addView(importButton_);
    currentX += Layout::kButtonWidth + Layout::kButtonSpacing;

    // Delete button
    auto deleteRect = VSTGUI::CRect(
        currentX, buttonY,
        currentX + Layout::kButtonWidth, buttonY + buttonHeight
    );
    deleteButton_ = new VSTGUI::CTextButton(deleteRect, this, kDeleteButtonTag, "Delete");
    deleteButton_->setFrameColor(VSTGUI::CColor(120, 60, 60));
    deleteButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
    addView(deleteButton_);

    // Close button (right-aligned)
    auto closeRect = VSTGUI::CRect(
        contentRect.right - Layout::kInnerPadding - Layout::kButtonWidth, buttonY,
        contentRect.right - Layout::kInnerPadding, buttonY + buttonHeight
    );
    closeButton_ = new VSTGUI::CTextButton(closeRect, this, kCloseButtonTag, "Close");
    closeButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
    closeButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
    addView(closeButton_);

    // ==========================================================================
    // Dialog Overlays (Save and Delete confirmation)
    // ==========================================================================
    createDialogViews();

    // Initialize with hidden state
    setVisible(false);
}

void PresetBrowserView::refreshPresetList() {
    if (!presetManager_ || !dataSource_) return;

    // Scan presets from manager
    auto presets = presetManager_->scanPresets();

    // Update data source
    dataSource_->setPresets(presets);
    dataSource_->setModeFilter(currentModeFilter_);

    // Refresh browser display
    if (presetList_) {
        presetList_->recalculateLayout(true);
        presetList_->invalid();
    }
}

void PresetBrowserView::updateButtonStates() {
    // Check if we have a valid selection
    bool hasSelection = (selectedPresetIndex_ >= 0);
    bool isFactoryPreset = false;

    if (hasSelection && dataSource_) {
        const PresetInfo* preset = dataSource_->getPresetAtRow(selectedPresetIndex_);
        if (preset) {
            isFactoryPreset = preset->isFactory;
        }
    }

    // Update button enabled states
    // Note: VSTGUI CTextButton doesn't have setEnabled, so we use mouse enable
    if (deleteButton_) {
        // Disable delete for factory presets or no selection
        bool canDelete = hasSelection && !isFactoryPreset;
        deleteButton_->setMouseEnabled(canDelete);
        deleteButton_->setAlphaValue(canDelete ? 1.0f : 0.4f);
    }

    if (saveButton_) {
        // Save is enabled when we have a loaded user preset
        // For now, always enabled
        saveButton_->setMouseEnabled(true);
    }
}

void PresetBrowserView::createDialogViews() {
    auto viewSize = getViewSize();
    auto centerX = viewSize.getWidth() / 2.0f;
    auto centerY = viewSize.getHeight() / 2.0f;

    // Layout constants
    constexpr float kDialogWidth = 300.0f;
    constexpr float kPadding = 12.0f;
    constexpr float kButtonHeight = 28.0f;
    constexpr float kButtonWidth = 80.0f;
    constexpr float kButtonGap = 10.0f;

    // =========================================================================
    // Save Dialog
    // =========================================================================
    {
        constexpr float kDialogHeight = 120.0f;
        constexpr float kFieldHeight = 28.0f;

        auto dialogRect = VSTGUI::CRect(
            centerX - kDialogWidth / 2.0f,
            centerY - kDialogHeight / 2.0f,
            centerX + kDialogWidth / 2.0f,
            centerY + kDialogHeight / 2.0f
        );

        saveDialogOverlay_ = new VSTGUI::CViewContainer(dialogRect);
        saveDialogOverlay_->setBackgroundColor(VSTGUI::CColor(50, 50, 55));
        saveDialogOverlay_->setVisible(false);

        // Title label
        auto titleRect = VSTGUI::CRect(kPadding, 8.0f, kDialogWidth - kPadding, 26.0f);
        auto* titleLabel = new VSTGUI::CTextLabel(titleRect, "Save Preset");
        titleLabel->setFontColor(VSTGUI::CColor(255, 255, 255));
        titleLabel->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        titleLabel->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        auto titleFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12, VSTGUI::kBoldFace);
        titleLabel->setFont(titleFont);
        saveDialogOverlay_->addView(titleLabel);

        // Name input field (listener for Enter key detection)
        auto fieldRect = VSTGUI::CRect(
            kPadding, 32.0f,
            kDialogWidth - kPadding, 32.0f + kFieldHeight
        );
        saveDialogNameField_ = new VSTGUI::CTextEdit(fieldRect, this, kSaveDialogNameFieldTag, "New Preset");
        saveDialogNameField_->setBackColor(VSTGUI::CColor(35, 35, 40));
        saveDialogNameField_->setFontColor(VSTGUI::CColor(220, 220, 220));
        saveDialogNameField_->setFrameColor(VSTGUI::CColor(80, 80, 85));
        saveDialogNameField_->setStyle(VSTGUI::CTextEdit::kRoundRectStyle);
        saveDialogOverlay_->addView(saveDialogNameField_);

        // Button row
        auto buttonY = kDialogHeight - kPadding - kButtonHeight;
        auto buttonsWidth = kButtonWidth * 2 + kButtonGap;
        auto buttonsLeft = (kDialogWidth - buttonsWidth) / 2.0f;

        auto saveRect = VSTGUI::CRect(
            buttonsLeft, buttonY,
            buttonsLeft + kButtonWidth, buttonY + kButtonHeight
        );
        saveDialogSaveButton_ = new VSTGUI::CTextButton(saveRect, this, kSaveDialogSaveTag, "Save");
        saveDialogSaveButton_->setFrameColor(VSTGUI::CColor(60, 120, 180));
        saveDialogSaveButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        saveDialogOverlay_->addView(saveDialogSaveButton_);

        auto cancelRect = VSTGUI::CRect(
            buttonsLeft + kButtonWidth + kButtonGap, buttonY,
            buttonsLeft + kButtonWidth * 2 + kButtonGap, buttonY + kButtonHeight
        );
        saveDialogCancelButton_ = new VSTGUI::CTextButton(cancelRect, this, kSaveDialogCancelTag, "Cancel");
        saveDialogCancelButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
        saveDialogCancelButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        saveDialogOverlay_->addView(saveDialogCancelButton_);

        addView(saveDialogOverlay_);
    }

    // =========================================================================
    // Delete Confirmation Dialog
    // =========================================================================
    {
        constexpr float kDialogHeight = 100.0f;

        auto dialogRect = VSTGUI::CRect(
            centerX - kDialogWidth / 2.0f,
            centerY - kDialogHeight / 2.0f,
            centerX + kDialogWidth / 2.0f,
            centerY + kDialogHeight / 2.0f
        );

        deleteDialogOverlay_ = new VSTGUI::CViewContainer(dialogRect);
        deleteDialogOverlay_->setBackgroundColor(VSTGUI::CColor(50, 50, 55));
        deleteDialogOverlay_->setVisible(false);

        // Title label
        auto titleRect = VSTGUI::CRect(kPadding, 8.0f, kDialogWidth - kPadding, 26.0f);
        auto* titleLabel = new VSTGUI::CTextLabel(titleRect, "Delete Preset?");
        titleLabel->setFontColor(VSTGUI::CColor(255, 200, 200));
        titleLabel->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        titleLabel->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        auto titleFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12, VSTGUI::kBoldFace);
        titleLabel->setFont(titleFont);
        deleteDialogOverlay_->addView(titleLabel);

        // Preset name label (will be updated dynamically)
        auto labelRect = VSTGUI::CRect(kPadding, 32.0f, kDialogWidth - kPadding, 50.0f);
        deleteDialogLabel_ = new VSTGUI::CTextLabel(labelRect, "");
        deleteDialogLabel_->setFontColor(VSTGUI::CColor(200, 200, 200));
        deleteDialogLabel_->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        deleteDialogLabel_->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        auto labelFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
        deleteDialogLabel_->setFont(labelFont);
        deleteDialogOverlay_->addView(deleteDialogLabel_);

        // Button row
        auto buttonY = kDialogHeight - kPadding - kButtonHeight;
        auto buttonsWidth = kButtonWidth * 2 + kButtonGap;
        auto buttonsLeft = (kDialogWidth - buttonsWidth) / 2.0f;

        auto deleteRect = VSTGUI::CRect(
            buttonsLeft, buttonY,
            buttonsLeft + kButtonWidth, buttonY + kButtonHeight
        );
        deleteDialogConfirmButton_ = new DialogButton(deleteRect, this, kDeleteDialogConfirmTag, "Delete");
        deleteDialogConfirmButton_->setFrameColor(VSTGUI::CColor(180, 60, 60));
        deleteDialogConfirmButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        deleteDialogOverlay_->addView(deleteDialogConfirmButton_);

        auto cancelRect = VSTGUI::CRect(
            buttonsLeft + kButtonWidth + kButtonGap, buttonY,
            buttonsLeft + kButtonWidth * 2 + kButtonGap, buttonY + kButtonHeight
        );
        deleteDialogCancelButton_ = new DialogButton(cancelRect, this, kDeleteDialogCancelTag, "Cancel");
        deleteDialogCancelButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
        deleteDialogCancelButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        deleteDialogOverlay_->addView(deleteDialogCancelButton_);

        addView(deleteDialogOverlay_);
    }

    // =========================================================================
    // Overwrite Confirmation Dialog
    // =========================================================================
    {
        constexpr float kDialogHeight = 100.0f;

        auto dialogRect = VSTGUI::CRect(
            centerX - kDialogWidth / 2.0f,
            centerY - kDialogHeight / 2.0f,
            centerX + kDialogWidth / 2.0f,
            centerY + kDialogHeight / 2.0f
        );

        overwriteDialogOverlay_ = new VSTGUI::CViewContainer(dialogRect);
        overwriteDialogOverlay_->setBackgroundColor(VSTGUI::CColor(50, 50, 55));
        overwriteDialogOverlay_->setVisible(false);

        // Title label
        auto titleRect = VSTGUI::CRect(kPadding, 8.0f, kDialogWidth - kPadding, 26.0f);
        auto* titleLabel = new VSTGUI::CTextLabel(titleRect, "Overwrite Preset?");
        titleLabel->setFontColor(VSTGUI::CColor(255, 220, 150));
        titleLabel->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        titleLabel->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        auto titleFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12, VSTGUI::kBoldFace);
        titleLabel->setFont(titleFont);
        overwriteDialogOverlay_->addView(titleLabel);

        // Preset name label (will be updated dynamically)
        auto labelRect = VSTGUI::CRect(kPadding, 32.0f, kDialogWidth - kPadding, 50.0f);
        overwriteDialogLabel_ = new VSTGUI::CTextLabel(labelRect, "");
        overwriteDialogLabel_->setFontColor(VSTGUI::CColor(200, 200, 200));
        overwriteDialogLabel_->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
        overwriteDialogLabel_->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
        auto labelFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
        overwriteDialogLabel_->setFont(labelFont);
        overwriteDialogOverlay_->addView(overwriteDialogLabel_);

        // Button row
        auto buttonY = kDialogHeight - kPadding - kButtonHeight;
        auto buttonsWidth = kButtonWidth * 2 + kButtonGap;
        auto buttonsLeft = (kDialogWidth - buttonsWidth) / 2.0f;

        auto confirmRect = VSTGUI::CRect(
            buttonsLeft, buttonY,
            buttonsLeft + kButtonWidth, buttonY + kButtonHeight
        );
        overwriteDialogConfirmButton_ = new DialogButton(confirmRect, this, kOverwriteDialogConfirmTag, "Overwrite");
        overwriteDialogConfirmButton_->setFrameColor(VSTGUI::CColor(180, 140, 60));
        overwriteDialogConfirmButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        overwriteDialogOverlay_->addView(overwriteDialogConfirmButton_);

        auto cancelRect = VSTGUI::CRect(
            buttonsLeft + kButtonWidth + kButtonGap, buttonY,
            buttonsLeft + kButtonWidth * 2 + kButtonGap, buttonY + kButtonHeight
        );
        overwriteDialogCancelButton_ = new DialogButton(cancelRect, this, kOverwriteDialogCancelTag, "Cancel");
        overwriteDialogCancelButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
        overwriteDialogCancelButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
        overwriteDialogOverlay_->addView(overwriteDialogCancelButton_);

        addView(overwriteDialogOverlay_);
    }
}

void PresetBrowserView::showSaveDialog() {
    if (saveDialogOverlay_) {
        // Reset the name field
        if (saveDialogNameField_) {
            saveDialogNameField_->setText("New Preset");
        }
        saveDialogOverlay_->setVisible(true);
        saveDialogVisible_ = true;

        // Auto-focus the text field so user can start typing immediately.
        // IKeyboardHook intercepts Enter/Escape at frame level, so focus
        // on the text field won't prevent dialog keyboard shortcuts.
        if (auto frame = getFrame()) {
            frame->setFocusView(saveDialogNameField_);
        }

        invalid();
    }
}

void PresetBrowserView::hideSaveDialog() {
    if (saveDialogOverlay_) {
        saveDialogOverlay_->setVisible(false);
        saveDialogVisible_ = false;
        invalid();
    }
}

void PresetBrowserView::onSaveDialogConfirm() {
    if (!presetManager_ || !saveDialogNameField_) {
        hideSaveDialog();
        return;
    }

    // IMPORTANT: Clear focus to commit text from platform control to model.
    // CTextEdit's getText() returns the committed value, not live editing text.
    // looseFocus() commits the text, so we trigger it by clearing focus first.
    if (auto frame = getFrame()) {
        frame->setFocusView(nullptr);
    }

    // Get the preset name from the text field
    const auto& text = saveDialogNameField_->getText();
    std::string name = text.empty() ? "New Preset" : text.getString();

    // Trim whitespace
    size_t start = name.find_first_not_of(" \t");
    size_t end = name.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos) {
        name = name.substr(start, end - start + 1);
    }

    // Don't save with empty name
    if (name.empty()) {
        name = "New Preset";
    }

    // Get current mode
    DelayMode mode = (currentModeFilter_ >= 0)
        ? static_cast<DelayMode>(currentModeFilter_)
        : DelayMode::Digital;

    // Save via preset manager
    presetManager_->savePreset(name, "", mode, "");

    // Hide dialog and refresh list
    hideSaveDialog();
    refreshPresetList();
}

void PresetBrowserView::showConfirmDelete() {
    if (selectedPresetIndex_ < 0 || !dataSource_ || !presetManager_) return;

    const PresetInfo* preset = dataSource_->getPresetAtRow(selectedPresetIndex_);
    if (!preset || preset->isFactory) return;

    // Update the dialog label with preset name
    if (deleteDialogLabel_) {
        std::string labelText = "\"" + preset->name + "\"";
        deleteDialogLabel_->setText(labelText.c_str());
    }

    // Show the confirmation dialog
    if (deleteDialogOverlay_) {
        deleteDialogOverlay_->setVisible(true);
        invalid();
    }
}

void PresetBrowserView::hideDeleteDialog() {
    if (deleteDialogOverlay_) {
        deleteDialogOverlay_->setVisible(false);
        invalid();
    }
}

void PresetBrowserView::onDeleteDialogConfirm() {
    if (selectedPresetIndex_ < 0 || !dataSource_ || !presetManager_) {
        hideDeleteDialog();
        return;
    }

    const PresetInfo* preset = dataSource_->getPresetAtRow(selectedPresetIndex_);
    if (preset && !preset->isFactory) {
        if (presetManager_->deletePreset(*preset)) {
            refreshPresetList();
            selectedPresetIndex_ = -1;
            updateButtonStates();
        }
    }

    hideDeleteDialog();
}

void PresetBrowserView::showConfirmOverwrite() {
    if (selectedPresetIndex_ < 0 || !dataSource_ || !presetManager_) return;

    const PresetInfo* preset = dataSource_->getPresetAtRow(selectedPresetIndex_);
    if (!preset || preset->isFactory) return;

    // Store target index for confirmation handler
    overwriteTargetIndex_ = selectedPresetIndex_;

    // Update the dialog label with preset name
    if (overwriteDialogLabel_) {
        std::string labelText = "\"" + preset->name + "\"";
        overwriteDialogLabel_->setText(labelText.c_str());
    }

    // Show the confirmation dialog
    if (overwriteDialogOverlay_) {
        overwriteDialogOverlay_->setVisible(true);
        invalid();
    }
}

void PresetBrowserView::hideOverwriteDialog() {
    if (overwriteDialogOverlay_) {
        overwriteDialogOverlay_->setVisible(false);
        overwriteTargetIndex_ = -1;
        invalid();
    }
}

void PresetBrowserView::onOverwriteDialogConfirm() {
    if (overwriteTargetIndex_ < 0 || !dataSource_ || !presetManager_) {
        hideOverwriteDialog();
        return;
    }

    const PresetInfo* preset = dataSource_->getPresetAtRow(overwriteTargetIndex_);
    if (preset && !preset->isFactory) {
        if (presetManager_->overwritePreset(*preset)) {
            refreshPresetList();
            // Keep the selection on the overwritten preset
        }
    }

    hideOverwriteDialog();
}

void PresetBrowserView::registerKeyboardHook() {
    if (keyboardHookRegistered_) {
        return;
    }

    auto frame = getFrame();
    if (frame) {
        frame->registerKeyboardHook(this);
        keyboardHookRegistered_ = true;
    }
}

void PresetBrowserView::unregisterKeyboardHook() {
    if (!keyboardHookRegistered_) {
        return;
    }

    auto frame = getFrame();
    if (frame) {
        frame->unregisterKeyboardHook(this);
    }
    keyboardHookRegistered_ = false;
}

} // namespace Iterum
