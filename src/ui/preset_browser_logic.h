#pragma once

// =============================================================================
// PresetBrowserLogic - Pure Functions for Keyboard Handling
// =============================================================================
// Extracted from PresetBrowserView for testability (humble object pattern).
// These functions have no VSTGUI dependencies and can be unit tested.
// =============================================================================

namespace Iterum {

/// Simplified key codes for testable logic (maps from VSTGUI::VirtualKey)
enum class KeyCode {
    Escape,
    Enter,  // Return or Enter key
    Other
};

/// Actions that can result from keyboard input
enum class KeyAction {
    None,                   // No action, pass to parent
    CloseBrowser,           // Close the entire preset browser
    ConfirmSaveDialog,      // Confirm the save dialog
    CancelSaveDialog,       // Cancel the save dialog
    ConfirmDeleteDialog,    // Confirm the delete dialog
    CancelDeleteDialog,     // Cancel the delete dialog
    ConfirmOverwriteDialog, // Confirm the overwrite dialog
    CancelOverwriteDialog   // Cancel the overwrite dialog
};

/// Determine the action to take based on keyboard input and dialog state.
/// This is a pure function with no side effects - easy to test.
///
/// @param key The key that was pressed
/// @param saveDialogVisible Whether the save dialog is currently visible
/// @param deleteDialogVisible Whether the delete dialog is currently visible
/// @param overwriteDialogVisible Whether the overwrite dialog is currently visible
/// @return The action to perform
inline KeyAction determineKeyAction(
    KeyCode key,
    bool saveDialogVisible,
    bool deleteDialogVisible,
    bool overwriteDialogVisible = false
) {
    switch (key) {
        case KeyCode::Escape:
            // Priority: save dialog > delete dialog > overwrite dialog > close browser
            if (saveDialogVisible) {
                return KeyAction::CancelSaveDialog;
            }
            if (deleteDialogVisible) {
                return KeyAction::CancelDeleteDialog;
            }
            if (overwriteDialogVisible) {
                return KeyAction::CancelOverwriteDialog;
            }
            return KeyAction::CloseBrowser;

        case KeyCode::Enter:
            // Priority: save dialog > delete dialog > overwrite dialog > nothing
            if (saveDialogVisible) {
                return KeyAction::ConfirmSaveDialog;
            }
            if (deleteDialogVisible) {
                return KeyAction::ConfirmDeleteDialog;
            }
            if (overwriteDialogVisible) {
                return KeyAction::ConfirmOverwriteDialog;
            }
            return KeyAction::None;

        case KeyCode::Other:
        default:
            return KeyAction::None;
    }
}

} // namespace Iterum
