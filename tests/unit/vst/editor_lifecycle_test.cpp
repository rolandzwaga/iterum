// ==============================================================================
// Editor Lifecycle Tests
// ==============================================================================
// Tests for safe handling of editor pointer lifecycle in VisibilityController.
//
// BUG BACKGROUND (2025-12-30):
// - VisibilityController stored a direct pointer to VST3Editor (editor_)
// - When editor closed and reopened, the stored pointer became dangling
// - Pending IDependent::update() callbacks would access the dangling pointer
// - CRASH on editor reopen
//
// FIX:
// - Store a pointer-to-pointer (editorPtr_) that points to controller's activeEditor_
// - When editor closes, activeEditor_ is set to nullptr
// - update() checks *editorPtr_ which is now nullptr, safely exits
// - When editor reopens, activeEditor_ points to new editor
// - update() works correctly with new editor
//
// This test verifies the PATTERN that prevents the crash.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

// ==============================================================================
// PATTERN TEST: Direct Pointer vs Indirect Pointer
// ==============================================================================
// This test simulates the difference between the buggy and fixed patterns
// without needing actual VSTGUI infrastructure.
// ==============================================================================

// Simulates a simplified VST3Editor for testing
struct MockEditor {
    bool isValid = true;
    int frameId = 0;  // Simulates getFrame() returning different objects

    // Simulate getFrame() - returns nullptr if editor was destroyed
    void* getFrame() { return isValid ? reinterpret_cast<void*>(frameId) : nullptr; }
};

// ==============================================================================
// BUGGY PATTERN: Direct editor pointer (what we had before)
// ==============================================================================
class BuggyVisibilityController {
public:
    // BUGGY: Stores direct pointer to editor
    explicit BuggyVisibilityController(MockEditor* editor)
        : editor_(editor) {}

    // BUGGY: Uses stored pointer directly - becomes dangling after editor close
    bool canAccessEditor() const {
        // This check passes even if editor_ points to destroyed memory!
        // The pointer value is non-null, but the object is gone.
        return editor_ != nullptr;
    }

    MockEditor* getEditor() const { return editor_; }

private:
    MockEditor* editor_;  // BUGGY: Direct pointer becomes dangling
};

// ==============================================================================
// FIXED PATTERN: Indirect pointer (pointer-to-pointer)
// ==============================================================================
class FixedVisibilityController {
public:
    // FIXED: Stores pointer to controller's activeEditor_ member
    explicit FixedVisibilityController(MockEditor** editorPtr)
        : editorPtr_(editorPtr) {}

    // FIXED: Dereferences to get current editor, which may be nullptr
    bool canAccessEditor() const {
        // This correctly returns false when activeEditor_ is nullptr
        MockEditor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        return editor != nullptr;
    }

    MockEditor* getEditor() const {
        return editorPtr_ ? *editorPtr_ : nullptr;
    }

private:
    MockEditor** editorPtr_;  // FIXED: Points to controller's member
};

// ==============================================================================
// TEST: Buggy pattern fails on editor close/reopen
// ==============================================================================

TEST_CASE("Buggy direct pointer pattern fails on editor close/reopen",
          "[vst][lifecycle][regression]") {

    SECTION("Direct pointer becomes dangling after editor close") {
        // Simulate: controller creates editor, VisibilityController stores pointer
        MockEditor editor1;
        editor1.frameId = 1;

        BuggyVisibilityController buggyController(&editor1);

        // Initial state: can access editor
        REQUIRE(buggyController.canAccessEditor() == true);
        REQUIRE(buggyController.getEditor()->getFrame() != nullptr);

        // Simulate: editor is closed (destroyed in real code)
        // In real code, the MockEditor* would point to deallocated memory
        // We simulate by marking it invalid
        editor1.isValid = false;

        // BUG: canAccessEditor() still returns true!
        // The pointer is non-null, but points to invalid memory.
        // In real code, this would cause undefined behavior or crash.
        REQUIRE(buggyController.canAccessEditor() == true);  // Bug!
        REQUIRE(buggyController.getEditor() == &editor1);    // Still returns old pointer

        // The getFrame() call would crash or return garbage in real code
        // Here we can detect it because our mock tracks validity
        REQUIRE(buggyController.getEditor()->getFrame() == nullptr);  // Invalid!

        // Simulate: new editor is created (controller.createView())
        MockEditor editor2;
        editor2.frameId = 2;

        // BUG: buggyController still points to editor1, not editor2!
        REQUIRE(buggyController.getEditor() != &editor2);  // Wrong editor!
        REQUIRE(buggyController.getEditor() == &editor1);  // Still old one
    }
}

// ==============================================================================
// TEST: Fixed indirect pointer pattern handles editor close/reopen correctly
// ==============================================================================

TEST_CASE("Fixed indirect pointer pattern survives editor close/reopen",
          "[vst][lifecycle][regression]") {

    SECTION("Indirect pointer correctly reflects nullptr after editor close") {
        // Simulate: controller's activeEditor_ member
        MockEditor* activeEditor = nullptr;

        // Simulate: editor is opened
        MockEditor editor1;
        editor1.frameId = 1;
        activeEditor = &editor1;

        // Create visibility controller with pointer to activeEditor_
        FixedVisibilityController fixedController(&activeEditor);

        // Initial state: can access editor
        REQUIRE(fixedController.canAccessEditor() == true);
        REQUIRE(fixedController.getEditor() == &editor1);
        REQUIRE(fixedController.getEditor()->getFrame() != nullptr);

        // Simulate: editor is closed (willClose sets activeEditor_ = nullptr)
        activeEditor = nullptr;

        // FIXED: canAccessEditor() correctly returns false
        REQUIRE(fixedController.canAccessEditor() == false);  // Correct!
        REQUIRE(fixedController.getEditor() == nullptr);       // Correct!

        // update() would safely return early because getEditor() is nullptr
    }

    SECTION("Indirect pointer correctly reflects new editor after reopen") {
        MockEditor* activeEditor = nullptr;

        // First editor
        MockEditor editor1;
        editor1.frameId = 1;
        activeEditor = &editor1;

        FixedVisibilityController fixedController(&activeEditor);
        REQUIRE(fixedController.getEditor() == &editor1);
        REQUIRE(fixedController.getEditor()->frameId == 1);

        // Close editor
        activeEditor = nullptr;
        REQUIRE(fixedController.getEditor() == nullptr);

        // Reopen with NEW editor
        MockEditor editor2;
        editor2.frameId = 2;
        activeEditor = &editor2;

        // FIXED: Controller now sees the new editor
        REQUIRE(fixedController.canAccessEditor() == true);
        REQUIRE(fixedController.getEditor() == &editor2);  // New editor!
        REQUIRE(fixedController.getEditor()->frameId == 2);
    }

    SECTION("Null editorPtr_ is handled safely") {
        // Edge case: what if editorPtr_ itself is nullptr?
        FixedVisibilityController nullController(nullptr);

        REQUIRE(nullController.canAccessEditor() == false);
        REQUIRE(nullController.getEditor() == nullptr);
    }
}

// ==============================================================================
// TEST: Document the lifecycle sequence
// ==============================================================================

TEST_CASE("Editor lifecycle sequence is handled correctly",
          "[vst][lifecycle][documentation]") {

    // This test documents the expected sequence of events

    MockEditor* activeEditor = nullptr;
    FixedVisibilityController controller(&activeEditor);

    SECTION("Full lifecycle: open -> close -> reopen") {
        // 1. Initial state: no editor
        REQUIRE(controller.canAccessEditor() == false);

        // 2. Host calls createView() -> didOpen() sets activeEditor_
        MockEditor editor1;
        editor1.frameId = 100;
        activeEditor = &editor1;

        REQUIRE(controller.canAccessEditor() == true);
        REQUIRE(controller.getEditor()->frameId == 100);

        // 3. VisibilityController receives update() -> can access editor
        // (In real code, this happens via IDependent mechanism)

        // 4. User closes plugin window -> willClose() is called
        // willClose() sets activeEditor_ = nullptr BEFORE editor is destroyed
        activeEditor = nullptr;

        // 5. Any pending update() now safely sees nullptr
        REQUIRE(controller.canAccessEditor() == false);

        // 6. editor1 is destroyed (out of scope in real code)
        // This is safe because controller no longer holds a reference

        // 7. User reopens plugin window -> createView() -> didOpen()
        MockEditor editor2;
        editor2.frameId = 200;
        activeEditor = &editor2;

        // 8. Controller now works with new editor
        REQUIRE(controller.canAccessEditor() == true);
        REQUIRE(controller.getEditor()->frameId == 200);
    }
}

// ==============================================================================
// Manual Testing Requirements (cannot be automated)
// ==============================================================================
// 1. Load plugin in a DAW
// 2. Open the plugin UI
// 3. Close the plugin UI (X button or host close)
// 4. Wait 1-2 seconds (allows pending updates to fire)
// 5. Reopen the plugin UI
// 6. Verify no crash occurs
// 7. Verify UI is responsive and controls work
// 8. Switch between modes (to trigger visibility controller updates)
// 9. Close and reopen again while rapidly switching modes
// ==============================================================================
