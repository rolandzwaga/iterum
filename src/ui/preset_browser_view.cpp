#include "preset_browser_view.h"
#include "preset_data_source.h"
#include "mode_tab_bar.h"
#include "../preset/preset_manager.h"

namespace Iterum {

PresetBrowserView::PresetBrowserView(const VSTGUI::CRect& size, PresetManager* presetManager)
    : CViewContainer(size)
    , presetManager_(presetManager)
{
    setBackgroundColor(VSTGUI::CColor(40, 40, 40, 220)); // Semi-transparent dark
}

PresetBrowserView::~PresetBrowserView() {
    // Child views are owned by CViewContainer and cleaned up automatically
    // DataSource needs explicit cleanup if not owned by browser
}

void PresetBrowserView::open(int currentMode) {
    currentModeFilter_ = currentMode;
    isOpen_ = true;
    setVisible(true);
    refreshPresetList();
}

void PresetBrowserView::close() {
    isOpen_ = false;
    setVisible(false);
}

void PresetBrowserView::draw(VSTGUI::CDrawContext* context) {
    // Draw semi-transparent background overlay
    CViewContainer::draw(context);

    // TODO: Draw title bar, borders
}

VSTGUI::CMouseEventResult PresetBrowserView::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons
) {
    // Check if click is outside the browser content area (click to close)
    // TODO: Implement proper hit testing

    return CViewContainer::onMouseDown(where, buttons);
}

void PresetBrowserView::onKeyboardEvent(VSTGUI::KeyboardEvent& event) {
    // Handle Escape key to close (FR-018)
    if (event.type == VSTGUI::EventType::KeyDown &&
        event.virt == VSTGUI::VirtualKey::Escape) {
        close();
        event.consumed = true;
        return;
    }

    CViewContainer::onKeyboardEvent(event);
}

void PresetBrowserView::onModeTabChanged(int newMode) {
    currentModeFilter_ = newMode;
    refreshPresetList();
}

void PresetBrowserView::onSearchTextChanged(const std::string& /*text*/) {
    // TODO: Filter presets in data source
    refreshPresetList();
}

void PresetBrowserView::onPresetSelected(int rowIndex) {
    selectedPresetIndex_ = rowIndex;
    updateButtonStates();
}

void PresetBrowserView::onPresetDoubleClicked(int /*rowIndex*/) {
    // TODO: Load preset and close browser
    // Must trigger mode switch with crossfade per FR-010 if preset targets different mode
}

void PresetBrowserView::onSaveClicked() {
    // TODO: Overwrite currently loaded user preset
}

void PresetBrowserView::onSaveAsClicked() {
    showSaveDialog();
}

void PresetBrowserView::onImportClicked() {
    // TODO: Show file dialog, import preset
}

void PresetBrowserView::onDeleteClicked() {
    showConfirmDelete();
}

void PresetBrowserView::onCloseClicked() {
    close();
}

void PresetBrowserView::createChildViews() {
    // TODO: Create mode tab bar, data browser, search field, buttons
    // Layout per contracts/preset_browser_view.h
}

void PresetBrowserView::refreshPresetList() {
    if (!presetManager_) return;

    // TODO: Refresh data source with filtered presets
}

void PresetBrowserView::updateButtonStates() {
    // TODO: Enable/disable buttons based on selection
    // - Delete disabled for factory presets
    // - Save disabled if no preset loaded
}

void PresetBrowserView::showSaveDialog() {
    // TODO: Show save dialog with name/category fields
}

void PresetBrowserView::showConfirmDelete() {
    // TODO: Show confirmation dialog
}

} // namespace Iterum
