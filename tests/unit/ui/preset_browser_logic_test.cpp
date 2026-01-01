// =============================================================================
// PresetBrowserLogic Unit Tests
// =============================================================================
// Tests for pure keyboard action determination logic
// Extracted from UI code for testability (humble object pattern)
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include "ui/preset_browser_logic.h"

using namespace Iterum;

TEST_CASE("PresetBrowserLogic: determineKeyAction", "[ui][preset-browser]") {

    SECTION("Escape key closes browser when no dialogs open") {
        auto action = determineKeyAction(KeyCode::Escape, false, false);
        REQUIRE(action == KeyAction::CloseBrowser);
    }

    SECTION("Escape key closes save dialog when open") {
        auto action = determineKeyAction(KeyCode::Escape, true, false);
        REQUIRE(action == KeyAction::CancelSaveDialog);
    }

    SECTION("Escape key closes delete dialog when open") {
        auto action = determineKeyAction(KeyCode::Escape, false, true);
        REQUIRE(action == KeyAction::CancelDeleteDialog);
    }

    SECTION("Escape key prioritizes save dialog over delete dialog") {
        // Edge case: both dialogs open (shouldn't happen, but handle gracefully)
        auto action = determineKeyAction(KeyCode::Escape, true, true);
        REQUIRE(action == KeyAction::CancelSaveDialog);
    }

    SECTION("Enter key confirms save dialog when open") {
        auto action = determineKeyAction(KeyCode::Enter, true, false);
        REQUIRE(action == KeyAction::ConfirmSaveDialog);
    }

    SECTION("Enter key confirms delete dialog when open") {
        auto action = determineKeyAction(KeyCode::Enter, false, true);
        REQUIRE(action == KeyAction::ConfirmDeleteDialog);
    }

    SECTION("Enter key does nothing when no dialogs open") {
        auto action = determineKeyAction(KeyCode::Enter, false, false);
        REQUIRE(action == KeyAction::None);
    }

    SECTION("Enter key prioritizes save dialog over delete dialog") {
        auto action = determineKeyAction(KeyCode::Enter, true, true);
        REQUIRE(action == KeyAction::ConfirmSaveDialog);
    }

    SECTION("Other keys do nothing") {
        auto action = determineKeyAction(KeyCode::Other, false, false);
        REQUIRE(action == KeyAction::None);

        action = determineKeyAction(KeyCode::Other, true, false);
        REQUIRE(action == KeyAction::None);

        action = determineKeyAction(KeyCode::Other, false, true);
        REQUIRE(action == KeyAction::None);
    }

    // Overwrite dialog tests
    SECTION("Escape key closes overwrite dialog when open") {
        auto action = determineKeyAction(KeyCode::Escape, false, false, true);
        REQUIRE(action == KeyAction::CancelOverwriteDialog);
    }

    SECTION("Enter key confirms overwrite dialog when open") {
        auto action = determineKeyAction(KeyCode::Enter, false, false, true);
        REQUIRE(action == KeyAction::ConfirmOverwriteDialog);
    }

    SECTION("Escape key prioritizes save dialog over overwrite dialog") {
        auto action = determineKeyAction(KeyCode::Escape, true, false, true);
        REQUIRE(action == KeyAction::CancelSaveDialog);
    }

    SECTION("Escape key prioritizes delete dialog over overwrite dialog") {
        auto action = determineKeyAction(KeyCode::Escape, false, true, true);
        REQUIRE(action == KeyAction::CancelDeleteDialog);
    }

    SECTION("Enter key prioritizes save dialog over overwrite dialog") {
        auto action = determineKeyAction(KeyCode::Enter, true, false, true);
        REQUIRE(action == KeyAction::ConfirmSaveDialog);
    }

    SECTION("Enter key prioritizes delete dialog over overwrite dialog") {
        auto action = determineKeyAction(KeyCode::Enter, false, true, true);
        REQUIRE(action == KeyAction::ConfirmDeleteDialog);
    }

    SECTION("Other keys do nothing with overwrite dialog open") {
        auto action = determineKeyAction(KeyCode::Other, false, false, true);
        REQUIRE(action == KeyAction::None);
    }
}
