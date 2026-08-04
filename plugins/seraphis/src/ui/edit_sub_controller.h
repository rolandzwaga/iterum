#pragma once

// ==============================================================================
// SeraphisEditSubController - the listener for every TAG-LESS control (Phase 11)
// ==============================================================================
// Spec:  specs/seraphis-phase11-ui/spec.md  (FR-007, FR-045, C-7a, C-7b, SC-022)
// Plan:  specs/seraphis-phase11-ui/plan.md  (section 8.4)
// Tasks: specs/seraphis-phase11-ui/tasks.md (T017)
//
// IT IS NOT A CView. It is a VSTGUI::DelegationController bound from the uidesc
// by `sub-controller="SeraphisEdit"` on the TEMPLATE ROOT (D-4), so every view
// in the document - including the header's preset button - is inside its
// sub-tree. Tagged controls are unaffected: DelegationController forwards
// getControlListener / valueChanged to the parent controller for anything this
// class does not claim (vstgui/uidescription/delegationcontroller.h:26).
//
// HOW A TAG-LESS CONTROL ACQUIRES A TAG AND A LISTENER - verifyView, not the
// uidesc. VSTGUI's control creator installs a listener ONLY when a `control-tag`
// attribute is present (vstgui/uidescription/viewcreator/controlcreator.cpp:
// 75-100); a control with no `control-tag` keeps tag -1 and listener nullptr.
// So each session control carries a CUSTOM attribute `session-tag="<name>"`,
// which UIAttributes preserves and the view factory ignores (the same trick as
// Disrumpo's `menu-items`, plugins/disrumpo/src/controller/sub_controllers.h:
// 194-203), and verifyView() below turns it into setTag() + setListener(this).
//
// SESSION TAGS LIVE OUTSIDE THE REGISTERED ID SPACE (>= 9000) and are NEVER
// written as `control-tag`, so they can never collide with a ParamID and can
// never be picked up by extractControlTagMap - which is what keeps the phase's
// binding count at exactly 110 (SC-002).
//
// `getTagForName` is deliberately NOT overridden: Seraphis has no repeated
// template needing per-instance ParamIDs, so DelegationController's forwarding
// default is correct. Only valueChanged / controlBeginEdit / controlEndEdit and
// verifyView are overridden.
//
// THREADING. UI thread only. Nothing here is reachable from process().
// ==============================================================================

#include "vstgui/lib/vstguifwd.h"
#include "vstgui/uidescription/delegationcontroller.h"

#include <cstdint>
#include <string_view>

namespace Seraphis {
class Controller;
}  // namespace Seraphis

namespace Seraphis::UI {

// ==============================================================================
// The session-tag namespace (C-7b)
// ==============================================================================

/// The custom uidesc attribute a tag-less control carries. Non-standard, so the
/// view factory leaves it alone and it survives into UIAttributes.
inline constexpr const char* kSessionTagAttribute = "session-tag";

/// EVERY session tag is >= this. The registered surface tops out at 1443
/// (plugin_ids.h), so a session tag can never be mistaken for a ParamID, and a
/// control carrying one is never counted as a parameter binding.
inline constexpr std::int32_t kSessionTagBase = 9000;

inline constexpr std::int32_t kPresetButtonTag = 9000;  ///< header preset button (FR-007)
inline constexpr std::int32_t kModeToggleTag   = 9001;  ///< Obs | Edit
inline constexpr std::int32_t kDrawerHandleTag = 9002;  ///< drawer pull-up handle
inline constexpr std::int32_t kBlendTag        = 9003;  ///< Blend A->B slider
inline constexpr std::int32_t kTiltTag         = 9004;  ///< Tilt dB/oct control

/// Seven drawer tab buttons: kTabBaseTag + 0 .. + 6 (FR-022's order).
inline constexpr std::int32_t kTabBaseTag = 9100;
inline constexpr int kSessionTabCount = 7;

/// Four morph slot selector buttons: kSlotBaseTag + 0 .. + 3.
inline constexpr std::int32_t kSlotBaseTag = 9200;
inline constexpr int kSessionSlotCount = 4;

/// What sessionTagForName() returns for an unrecognised name.
inline constexpr std::int32_t kInvalidSessionTag = -1;

/// Map a `session-tag` attribute value onto its tag, or kInvalidSessionTag.
/// Recognised names: "preset", "mode", "drawerHandle", "blend", "tilt",
/// "tab0".."tab6", "slot0".."slot3".
[[nodiscard]] std::int32_t sessionTagForName(std::string_view name) noexcept;

// ==============================================================================
// SeraphisEditSubController
// ==============================================================================

class SeraphisEditSubController : public VSTGUI::DelegationController {
public:
    /// `parent` is the IController this one delegates to - in production the
    /// VST3Editor itself. It MAY be null in a headless test that constructs the
    /// sub-controller directly; every delegation below is null-guarded for
    /// exactly that case, because DelegationController's own forwarders
    /// dereference it unconditionally.
    SeraphisEditSubController(Controller* owner, VSTGUI::IController* parent);

    void valueChanged(VSTGUI::CControl* control) override;
    void controlBeginEdit(VSTGUI::CControl* control) override;
    void controlEndEdit(VSTGUI::CControl* control) override;

    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                              const VSTGUI::IUIDescription* description) override;

private:
    [[nodiscard]] static bool isSessionTag(std::int32_t tag) noexcept {
        return tag >= kSessionTagBase;
    }

    Seraphis::Controller* owner_ = nullptr;
};

}  // namespace Seraphis::UI
