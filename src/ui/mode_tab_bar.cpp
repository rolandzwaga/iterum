#include "mode_tab_bar.h"

namespace Iterum {

ModeTabBar::ModeTabBar(const VSTGUI::CRect& size)
    : CView(size)
{
}

const std::vector<std::string>& ModeTabBar::getTabLabels() {
    static const std::vector<std::string> labels = {
        "All",
        "Granular",
        "Spectral",
        "Shimmer",
        "Tape",
        "BBD",
        "Digital",
        "PingPong",
        "Reverse",
        "MultiTap",
        "Freeze",
        "Ducking"
    };
    return labels;
}

void ModeTabBar::setSelectedTab(int tab) {
    if (tab >= 0 && tab < kNumTabs && tab != selectedTab_) {
        selectedTab_ = tab;
        invalid(); // Request redraw

        if (selectionCallback_) {
            // Convert to mode filter: 0 = All (-1), 1-11 = modes (0-10)
            int modeFilter = (tab == 0) ? -1 : (tab - 1);
            selectionCallback_(modeFilter);
        }
    }
}

VSTGUI::CRect ModeTabBar::getTabRect(int index) const {
    auto viewSize = getViewSize();
    auto tabHeight = viewSize.getHeight() / kNumTabs;

    return VSTGUI::CRect(
        viewSize.left,
        viewSize.top + tabHeight * index,
        viewSize.right,
        viewSize.top + tabHeight * (index + 1)
    );
}

void ModeTabBar::draw(VSTGUI::CDrawContext* context) {
    const auto& labels = getTabLabels();

    // Set font for text rendering - CRITICAL: must set font before drawString
    auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
    context->setFont(font);

    for (int i = 0; i < kNumTabs; ++i) {
        auto tabRect = getTabRect(i);

        // Background
        if (i == selectedTab_) {
            context->setFillColor(VSTGUI::CColor(60, 100, 160)); // Selected
        } else {
            context->setFillColor(VSTGUI::CColor(50, 50, 50)); // Normal
        }
        context->drawRect(tabRect, VSTGUI::kDrawFilled);

        // Border
        context->setFrameColor(VSTGUI::CColor(80, 80, 80));
        context->drawRect(tabRect, VSTGUI::kDrawStroked);

        // Text
        context->setFontColor(VSTGUI::CColor(255, 255, 255));
        auto textRect = tabRect;
        textRect.inset(8, 0);
        context->drawString(labels[static_cast<size_t>(i)].c_str(), textRect, VSTGUI::kLeftText);
    }
}

VSTGUI::CMouseEventResult ModeTabBar::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& /*buttons*/
) {
    // Find which tab was clicked
    for (int i = 0; i < kNumTabs; ++i) {
        auto tabRect = getTabRect(i);
        if (tabRect.pointInside(where)) {
            setSelectedTab(i);
            return VSTGUI::kMouseEventHandled;
        }
    }

    return VSTGUI::kMouseEventNotHandled;
}

} // namespace Iterum
