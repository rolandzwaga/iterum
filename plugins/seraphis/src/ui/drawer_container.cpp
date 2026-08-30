// ==============================================================================
// DrawerContainer - implementation (Seraphis Phase 11, T016)
// ==============================================================================
// Spec:  specs/seraphis-phase11-ui/spec.md  (FR-022 - FR-025, SC-020)
// Plan:  specs/seraphis-phase11-ui/plan.md  (section 8.3)
// ==============================================================================

#include "ui/drawer_container.h"

#include <cstdint>

namespace Seraphis::UI {

// ==============================================================================
// Construction
// ==============================================================================

DrawerContainer::DrawerContainer(const VSTGUI::CRect& collapsedRect)
    : VSTGUI::CViewContainer(collapsedRect) {}

DrawerContainer::DrawerContainer(const DrawerContainer& other)
    : VSTGUI::CViewContainer(other), open_(other.open_), activeTab_(other.activeTab_) {}

// ==============================================================================
// Open / collapsed - the two rects, and there are no others (FR-023)
// ==============================================================================

void DrawerContainer::setOpen(bool open) noexcept {
    if (open_ == open) {
        return;  // idempotent: a redundant toggle cannot invent a third rect
    }
    open_ = open;

    // CViewContainer::setViewSize runs the autosize layouter over the children
    // and then stores the rect verbatim (cviewcontainer.cpp:336-355), so
    // getViewSize() is byte-equal to the constant afterwards - which is what
    // SC-020(e) compares against.
    const VSTGUI::CRect& target = open_ ? kOpenRect : kCollapsedRect;
    setViewSize(target);
    setMouseableArea(getViewSize());
}

// ==============================================================================
// Tabs (FR-022, FR-025)
// ==============================================================================

int DrawerContainer::tabPageCount() const noexcept {
    int count = 0;
    const std::uint32_t viewCount = getNbViews();
    for (std::uint32_t i = 0; i < viewCount; ++i) {
        const VSTGUI::CView* child = getView(i);
        if (child != nullptr && child->asViewContainer() != nullptr) {
            ++count;
        }
    }
    return count;
}

VSTGUI::CViewContainer* DrawerContainer::tabPage(int index) const noexcept {
    if (index < 0) {
        return nullptr;
    }

    int seen = 0;
    const std::uint32_t viewCount = getNbViews();
    for (std::uint32_t i = 0; i < viewCount; ++i) {
        VSTGUI::CView* child = getView(i);
        if (child == nullptr) {
            continue;
        }
        VSTGUI::CViewContainer* page = child->asViewContainer();
        if (page == nullptr) {
            continue;  // the drawer handle or one of the seven tab buttons
        }
        if (seen == index) {
            return page;
        }
        ++seen;
    }
    return nullptr;
}

void DrawerContainer::setActiveTab(int index) noexcept {
    if (index < 0 || index >= kTabCount) {
        return;  // rejected, never clamped onto a neighbouring page
    }
    activeTab_ = index;
    applyTabVisibility();
}

void DrawerContainer::applyTabVisibility() noexcept {
    // Every page stays MOUNTED; only its visibility moves. That is the whole
    // difference from a UIViewSwitchContainer, and it is what keeps all seven
    // tabs' parameter bindings present in the built tree (C-3's empty
    // unreachable-parameter allowlist).
    int page = 0;
    const std::uint32_t viewCount = getNbViews();
    for (std::uint32_t i = 0; i < viewCount; ++i) {
        VSTGUI::CView* child = getView(i);
        if (child == nullptr || child->asViewContainer() == nullptr) {
            continue;
        }
        child->setVisible(page == activeTab_);
        ++page;
    }
}

// ==============================================================================
// Teardown (C-7c)
// ==============================================================================

bool DrawerContainer::removed(VSTGUI::CView* parent) {
    // Nothing to cancel: the instant toggle owns no timer (see the header).
    return VSTGUI::CViewContainer::removed(parent);
}

}  // namespace Seraphis::UI
