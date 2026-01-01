#include "save_preset_dialog_view.h"
#include "../preset/preset_manager.h"
#include "../preset/preset_info.h"

#include "vstgui/lib/cfont.h"

namespace Iterum {

// =============================================================================
// DialogButton - Custom button that doesn't consume Enter/Escape events
// =============================================================================
class SaveDialogButton : public VSTGUI::CTextButton {
public:
    using CTextButton::CTextButton;

    void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override {
        if (event.virt == VSTGUI::VirtualKey::Return ||
            event.virt == VSTGUI::VirtualKey::Enter ||
            event.virt == VSTGUI::VirtualKey::Escape) {
            return;  // Skip, don't consume - let parent handle
        }
        CTextButton::onKeyboardEvent(event);
    }
};

// =============================================================================
// Layout Constants
// =============================================================================
namespace {
    constexpr float kDialogWidth = 300.0f;
    constexpr float kDialogHeight = 140.0f;
    constexpr float kPadding = 16.0f;
    constexpr float kFieldHeight = 26.0f;
    constexpr float kButtonWidth = 80.0f;
    constexpr float kButtonHeight = 28.0f;
    constexpr float kButtonGap = 12.0f;
}

// =============================================================================
// Construction / Destruction
// =============================================================================

SavePresetDialogView::SavePresetDialogView(const VSTGUI::CRect& size, PresetManager* presetManager)
    : CViewContainer(size)
    , presetManager_(presetManager)
{
    setBackgroundColor(VSTGUI::CColor(0, 0, 0, 180)); // Semi-transparent overlay
    setVisible(false);  // Start hidden, shown via open()
    createDialogViews();
}

SavePresetDialogView::~SavePresetDialogView() {
    unregisterKeyboardHook();
}

// =============================================================================
// Lifecycle
// =============================================================================

void SavePresetDialogView::open(int currentMode) {
    currentMode_ = currentMode;
    isOpen_ = true;
    setVisible(true);

    registerKeyboardHook();

    // Reset name field and focus it
    if (nameField_) {
        nameField_->setText("New Preset");
    }

    if (auto frame = getFrame()) {
        frame->setFocusView(nameField_);
    }

    invalid();
}

void SavePresetDialogView::close() {
    unregisterKeyboardHook();
    isOpen_ = false;
    setVisible(false);
}

// =============================================================================
// Drawing
// =============================================================================

void SavePresetDialogView::draw(VSTGUI::CDrawContext* context) {
    CViewContainer::draw(context);
}

VSTGUI::CMouseEventResult SavePresetDialogView::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons
) {
    // Click outside dialog closes it
    if (dialogBox_) {
        auto dialogBounds = dialogBox_->getViewSize();
        if (!dialogBounds.pointInside(where)) {
            close();
            return VSTGUI::kMouseEventHandled;
        }
    }

    return CViewContainer::onMouseDown(where, buttons);
}

// =============================================================================
// IKeyboardHook
// =============================================================================

void SavePresetDialogView::onKeyboardEvent(VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* /*frame*/) {
    if (!isOpen_) return;
    if (event.type != VSTGUI::EventType::KeyDown) return;

    if (event.virt == VSTGUI::VirtualKey::Escape) {
        close();
        event.consumed = true;
    } else if (event.virt == VSTGUI::VirtualKey::Return ||
               event.virt == VSTGUI::VirtualKey::Enter) {
        onSaveConfirm();
        event.consumed = true;
    }
}

// =============================================================================
// IControlListener
// =============================================================================

void SavePresetDialogView::valueChanged(VSTGUI::CControl* control) {
    if (!control) return;

    auto tag = control->getTag();
    switch (tag) {
        case kSavePresetDialogSaveTag:
            onSaveConfirm();
            break;
        case kSavePresetDialogCancelTag:
            close();
            break;
        default:
            break;
    }
}

// =============================================================================
// Dialog Creation
// =============================================================================

void SavePresetDialogView::createDialogViews() {
    auto viewSize = getViewSize();

    // Center the dialog
    auto dialogLeft = (viewSize.getWidth() - kDialogWidth) / 2.0f;
    auto dialogTop = (viewSize.getHeight() - kDialogHeight) / 2.0f;

    auto dialogRect = VSTGUI::CRect(
        dialogLeft, dialogTop,
        dialogLeft + kDialogWidth, dialogTop + kDialogHeight
    );

    dialogBox_ = new VSTGUI::CViewContainer(dialogRect);
    dialogBox_->setBackgroundColor(VSTGUI::CColor(45, 45, 50));
    addView(dialogBox_);

    // Title
    auto titleRect = VSTGUI::CRect(kPadding, kPadding, kDialogWidth - kPadding, kPadding + 24.0f);
    titleLabel_ = new VSTGUI::CTextLabel(titleRect, "Save Preset");
    titleLabel_->setFont(VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 14, VSTGUI::kBoldFace));
    titleLabel_->setFontColor(VSTGUI::CColor(255, 255, 255));
    titleLabel_->setBackColor(VSTGUI::CColor(0, 0, 0, 0));
    titleLabel_->setFrameColor(VSTGUI::CColor(0, 0, 0, 0));
    titleLabel_->setHoriAlign(VSTGUI::kLeftText);
    dialogBox_->addView(titleLabel_);

    // Name field
    auto fieldY = kPadding + 32.0f;
    auto fieldRect = VSTGUI::CRect(
        kPadding, fieldY,
        kDialogWidth - kPadding, fieldY + kFieldHeight
    );
    nameField_ = new VSTGUI::CTextEdit(fieldRect, this, kSavePresetDialogNameFieldTag, "New Preset");
    nameField_->setFont(VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12));
    nameField_->setFontColor(VSTGUI::CColor(255, 255, 255));
    nameField_->setBackColor(VSTGUI::CColor(30, 30, 35));
    nameField_->setFrameColor(VSTGUI::CColor(80, 80, 85));
    nameField_->setTextInset(VSTGUI::CPoint(6, 0));
    dialogBox_->addView(nameField_);

    // Buttons
    auto buttonY = kDialogHeight - kPadding - kButtonHeight;
    auto buttonsWidth = kButtonWidth * 2 + kButtonGap;
    auto buttonsLeft = (kDialogWidth - buttonsWidth) / 2.0f;

    auto saveRect = VSTGUI::CRect(
        buttonsLeft, buttonY,
        buttonsLeft + kButtonWidth, buttonY + kButtonHeight
    );
    saveButton_ = new SaveDialogButton(saveRect, this, kSavePresetDialogSaveTag, "Save");
    saveButton_->setFrameColor(VSTGUI::CColor(60, 120, 180));
    saveButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
    dialogBox_->addView(saveButton_);

    auto cancelRect = VSTGUI::CRect(
        buttonsLeft + kButtonWidth + kButtonGap, buttonY,
        buttonsLeft + kButtonWidth * 2 + kButtonGap, buttonY + kButtonHeight
    );
    cancelButton_ = new SaveDialogButton(cancelRect, this, kSavePresetDialogCancelTag, "Cancel");
    cancelButton_->setFrameColor(VSTGUI::CColor(80, 80, 85));
    cancelButton_->setTextColor(VSTGUI::CColor(255, 255, 255));
    dialogBox_->addView(cancelButton_);
}

// =============================================================================
// Save Logic
// =============================================================================

void SavePresetDialogView::onSaveConfirm() {
    if (!presetManager_ || !nameField_) {
        close();
        return;
    }

    // Commit text from platform control
    if (auto frame = getFrame()) {
        frame->setFocusView(nullptr);
    }

    // Get preset name
    const auto& text = nameField_->getText();
    std::string name = text.empty() ? "New Preset" : text.getString();

    // Trim whitespace
    size_t start = name.find_first_not_of(" \t");
    size_t end = name.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos) {
        name = name.substr(start, end - start + 1);
    }

    if (name.empty()) {
        name = "New Preset";
    }

    // Get mode
    DelayMode mode = (currentMode_ >= 0)
        ? static_cast<DelayMode>(currentMode_)
        : DelayMode::Digital;

    // Save
    presetManager_->savePreset(name, "", mode, "");

    close();
}

// =============================================================================
// Keyboard Hook Registration
// =============================================================================

void SavePresetDialogView::registerKeyboardHook() {
    if (keyboardHookRegistered_) return;

    if (auto frame = getFrame()) {
        frame->registerKeyboardHook(this);
        keyboardHookRegistered_ = true;
    }
}

void SavePresetDialogView::unregisterKeyboardHook() {
    if (!keyboardHookRegistered_) return;

    if (auto frame = getFrame()) {
        frame->unregisterKeyboardHook(this);
    }
    keyboardHookRegistered_ = false;
}

} // namespace Iterum
