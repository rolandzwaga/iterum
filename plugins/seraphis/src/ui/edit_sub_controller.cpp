// ==============================================================================
// SeraphisEditSubController implementation (Seraphis Phase 11, T017)
// ==============================================================================

#include "ui/edit_sub_controller.h"

#include "controller/controller.h"
#include "ui/edit_message.h"
#include "ui/macro_ring_knob.h"

#include <krate/dsp/processors/spectral_state.h>

#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/uidescription/uiattributes.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>

namespace Seraphis::UI {

namespace {

/// Segment index of a segment-bar control: its normalized value spread over
/// `count` segments (IconSegmentButton's own convention,
/// plugins/shared/src/ui/icon_segment_button.h getSelectedSegment()).
[[nodiscard]] int segmentIndex(const VSTGUI::CControl* control, int count) noexcept {
    const float norm = control->getValueNormalized();
    const int index =
        static_cast<int>(std::lround(norm * static_cast<float>(count - 1)));
    return std::clamp(index, 0, count - 1);
}

}  // namespace

std::int32_t sessionTagForName(std::string_view name) noexcept {
    if (name == "preset") {
        return kPresetButtonTag;
    }
    if (name == "mode") {
        return kModeToggleTag;
    }
    if (name == "drawerHandle") {
        return kDrawerHandleTag;
    }
    if (name == "blend") {
        return kBlendTag;
    }
    if (name == "tilt") {
        return kTiltTag;
    }
    if (name == "tabs") {
        return kTabBarTag;
    }
    if (name == "slots") {
        return kSlotBarTag;
    }
    return kInvalidSessionTag;
}

SeraphisEditSubController::SeraphisEditSubController(Controller* owner,
                                                     VSTGUI::IController* parent)
    : VSTGUI::DelegationController(parent), owner_(owner) {}

// ==============================================================================
// verifyView - where a tag-less control acquires its session tag and listener
// ==============================================================================
VSTGUI::CView* SeraphisEditSubController::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* description) {

    // The five macro rings pass through here too (they are created by the
    // MacroRingKnob ViewCreatorAdapter, not by createCustomView, C-7a), which is
    // the one place the controller can cache them without a second override.
    if (auto* ring = dynamic_cast<MacroRingKnob*>(view); ring != nullptr && owner_ != nullptr) {
        owner_->registerMacroRing(ring);
    }

    if (auto* control = dynamic_cast<VSTGUI::CControl*>(view); control != nullptr) {
        if (const std::string* attribute = attributes.getAttributeValue(kSessionTagAttribute);
            attribute != nullptr) {
            const std::int32_t tag = sessionTagForName(*attribute);
            // An UNRECOGNISED session-tag is a hard failure, not a silent pass:
            // it would leave the control with tag -1 and no listener, i.e. a
            // dead control that every other criterion in this phase ignores.
            // SC-022(b) is the release-build detector (it asserts getListener()
            // on every control in C-7b's table); this is the debug one.
            assert(tag != kInvalidSessionTag && "unrecognised session-tag attribute");
            if (tag != kInvalidSessionTag) {
                control->setTag(tag);
                control->setListener(this);
            }
        }
    }

    // DelegationController::verifyView dereferences its parent unconditionally.
    return (controller != nullptr)
               ? VSTGUI::DelegationController::verifyView(view, attributes, description)
               : view;
}

// ==============================================================================
// valueChanged - C-7b's table, and nothing else
// ==============================================================================
void SeraphisEditSubController::valueChanged(VSTGUI::CControl* control) {
    if (control == nullptr) {
        return;
    }

    const std::int32_t tag = control->getTag();
    if (!isSessionTag(tag)) {
        // A tagged (ParamID) control: the parent controller owns it.
        if (controller != nullptr) {
            VSTGUI::DelegationController::valueChanged(control);
        }
        return;
    }

    if (owner_ == nullptr) {
        return;
    }

    const float value = control->getValueNormalized();

    if (tag == kPresetButtonTag) {
        owner_->togglePresetBrowser();
        return;
    }
    if (tag == kModeToggleTag) {
        owner_->setCloudViewEditMode(value >= 0.5f);
        return;
    }
    if (tag == kDrawerHandleTag) {
        owner_->toggleDrawer();
        return;
    }
    if (tag == kTabBarTag) {
        owner_->setDrawerTab(segmentIndex(control, kSessionTabCount));
        return;
    }
    if (tag == kSlotBarTag) {
        // kind 6 is sent by the controller, which owns the selection.
        owner_->setSelectedSlot(segmentIndex(control, kSessionSlotCount));
        return;
    }
    if (tag == kBlendTag) {
        // kind 4: `a` = t, `b` = slot B as float, `slot` = destination (the
        // selected slot, which is also the pristine-A source latched by the
        // kind 7 that controlBeginEdit sent, Q2).
        EditMessage message{};
        message.kind = static_cast<std::uint8_t>(4);
        message.slot = static_cast<std::uint8_t>(owner_->selectedSlot());
        message.a = value;
        message.b = static_cast<float>(owner_->blendSourceSlot());
        owner_->sendEditMessage(message);
        return;
    }
    if (tag == kTiltTag) {
        // kind 5 carries an ABSOLUTE dB/oct (C-6), never a delta, so the
        // normalized control value maps straight onto the state's own range.
        constexpr float kMinDb = Krate::DSP::SpectralState::kMinStateTiltDbPerOct;
        constexpr float kMaxDb = Krate::DSP::SpectralState::kMaxStateTiltDbPerOct;
        EditMessage message{};
        message.kind = static_cast<std::uint8_t>(5);
        message.slot = static_cast<std::uint8_t>(owner_->selectedSlot());
        message.a = kMinDb + value * (kMaxDb - kMinDb);
        owner_->sendEditMessage(message);
        return;
    }
}

// ==============================================================================
// Gesture boundaries
// ==============================================================================
void SeraphisEditSubController::controlBeginEdit(VSTGUI::CControl* control) {
    if (control != nullptr && owner_ != nullptr && control->getTag() == kBlendTag) {
        // kind 7 (BlendBegin, Q2): snapshot the destination slot as the pristine
        // A BEFORE any t arrives, so the gesture is absolute rather than
        // compounding. `a` is unused and reserved 0.
        EditMessage message{};
        message.kind = static_cast<std::uint8_t>(7);
        message.slot = static_cast<std::uint8_t>(owner_->selectedSlot());
        message.b = static_cast<float>(owner_->blendSourceSlot());
        owner_->sendEditMessage(message);
    }

    if (control != nullptr && isSessionTag(control->getTag())) {
        return;  // session controls drive no ParamID: nothing to delegate
    }
    if (controller != nullptr) {
        VSTGUI::DelegationController::controlBeginEdit(control);
    }
}

void SeraphisEditSubController::controlEndEdit(VSTGUI::CControl* control) {
    // T018 OWNS THE TERMINAL FLUSH (FR-048, SC-027): the per-gesture EditThrottle
    // and its unconditional gesture-end send land there, on the controller. This
    // override exists now only so a session control never reaches the parent
    // controller's ParamID edit path.
    if (control != nullptr && isSessionTag(control->getTag())) {
        return;
    }
    if (controller != nullptr) {
        VSTGUI::DelegationController::controlEndEdit(control);
    }
}

}  // namespace Seraphis::UI
