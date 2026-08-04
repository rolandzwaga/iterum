#pragma once

// ==============================================================================
// DrawerContainer - the pull-up deep-parameter drawer (Seraphis Phase 11)
// ==============================================================================
// Spec:  specs/seraphis-phase11-ui/spec.md  (FR-022 - FR-025, SC-004, SC-020)
// Plan:  specs/seraphis-phase11-ui/plan.md  (section 8.3)
// Tasks: specs/seraphis-phase11-ui/tasks.md (T016)
//
// A plain CViewContainer that lives along the bottom edge of the editor and
// occupies exactly TWO rects and no others: collapsed (tab strip only) and open
// (the active tab's knob page). It owns no DSP, no parameter and no timer; its
// children are plain uidesc controls - ArcKnob / CSlider / COptionMenu /
// ToggleButton (FR-025, as amended by the 2026-08-04 consistency pass) plus
// the IconSegmentButton tab bar, the handle and the seven page containers.
//
// D-4 - THIS VIEW MUST BE A DIRECT CHILD OF THE 1000 x 700 TEMPLATE ROOT.
// getViewSize() is in PARENT coordinates, so the two rects below are the
// absolute window rects FR-023 names only while the drawer is a direct child of
// the root. Nested one level deeper under, say, a (0, 32, 1000, 700) container,
// the declared collapsed rect is absolute (0, 702, 1000, 732) - below the window
// and clipped invisible - and SC-020(c)/(e) could never pass. This is a hard
// constraint, not a layout preference; the uidesc places it accordingly.
//
// NEVER A UIViewSwitchContainer (R-10). All seven pages are present in the XML
// as child containers of this container, exactly one visible at a time. A view
// switch realises only the active template, which would make unreachableParams
// report six tabs' worth of ParamIDs as unreachable while C-3 requires an EMPTY
// allowlist.
//
// THREADING. Constructed, sized, clicked and drawn on the UI thread only.
// Nothing here is reachable from process(); SC-011's audio-thread source corpus
// deliberately does not include drawer_container.cpp.
// ==============================================================================

#include "vstgui/lib/crect.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"

namespace Seraphis::UI {

/// Height of the always-visible tab strip: the collapsed drawer is exactly this
/// tall (kCollapsedRect below), and the pages start immediately beneath it.
inline constexpr VSTGUI::CCoord kDrawerTabStripHeight = 30.0;

// ==============================================================================
// DrawerContainer
// ==============================================================================

class DrawerContainer : public VSTGUI::CViewContainer {
public:
    /// Cloud, Morph, Body, Atmos, Aether, FX, Life/Env (FR-022, in that order).
    /// The titles themselves live in the uidesc; SC-004 arm 2 reads them back
    /// off the built tree, because this count alone cannot detect a wrong or
    /// swapped label.
    static constexpr int kTabCount = 7;

    /// THE TWO RECTS, AND THERE ARE NO OTHERS (FR-023, C-1). They live in the
    /// header so the tests compare against the same constants setOpen() toggles
    /// between rather than against re-typed literals.
    static constexpr VSTGUI::CRect kCollapsedRect{0.0, 670.0, 1000.0, 700.0};
    static constexpr VSTGUI::CRect kOpenRect{0.0, 420.0, 1000.0, 700.0};

    /// `collapsedRect` is what the uidesc declares (origin 0,670 size 1000,30)
    /// and is what the base container starts at. The two constants above remain
    /// the authority for every subsequent toggle.
    explicit DrawerContainer(const VSTGUI::CRect& collapsedRect);

    /// Copy constructor for CLASS_METHODS' newCopy(). The base copies the child
    /// views; the open/active-tab state is carried across so a duplicated
    /// drawer does not silently reset to collapsed-on-tab-0.
    DrawerContainer(const DrawerContainer& other);

    DrawerContainer& operator=(const DrawerContainer&) = delete;

    ~DrawerContainer() override = default;

    /// Toggles between the two EXACT rects above. Opening it never removes,
    /// hides, unmounts or resizes the cloud view (FR-024) - the drawer simply
    /// grows upward OVER it.
    void setOpen(bool open) noexcept;

    [[nodiscard]] bool isOpen() const noexcept { return open_; }

    /// Makes page `index` the only visible one. Out-of-range indices are
    /// rejected outright rather than clamped onto a neighbouring page.
    /// This is also the entry point that establishes the initial "exactly one
    /// visible" state: the pages arrive from the uidesc AFTER construction, so
    /// the owning controller calls setActiveTab(0) once the tree is built.
    void setActiveTab(int index) noexcept;

    [[nodiscard]] int activeTab() const noexcept { return activeTab_; }

    /// The pages are this container's child CViewContainers, in document order.
    /// The tab strip's own children - the handle and the seven tab buttons -
    /// are CControls, so they are not pages and are never hidden.
    [[nodiscard]] int tabPageCount() const noexcept;

    /// The `index`-th page container, or nullptr if there is no such page.
    [[nodiscard]] VSTGUI::CViewContainer* tabPage(int index) const noexcept;

    /// C-7c's teardown hook. The instant toggle is the plan's sanctioned
    /// non-animated fallback (section 8.3), so this view owns NO timer and there
    /// is nothing to cancel here; if a slide animation is ever added, its
    /// CVSTGUITimer is cancelled HERE and nowhere else, so that no tick can
    /// dereference a view that has left the frame.
    bool removed(VSTGUI::CView* parent) override;

    CLASS_METHODS(DrawerContainer, VSTGUI::CViewContainer)

private:
    void applyTabVisibility() noexcept;

    bool open_ = false;
    int activeTab_ = 0;
};

}  // namespace Seraphis::UI
