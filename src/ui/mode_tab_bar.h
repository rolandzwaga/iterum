#pragma once

// ==============================================================================
// ModeTabBar - Vertical Tab Bar for Mode Filtering
// ==============================================================================
// Spec 042: Preset Browser
// Displays 12 tabs (All + 11 modes) for filtering presets by delay mode.
// ==============================================================================

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include <functional>
#include <string>
#include <vector>

namespace Iterum {

class ModeTabBar : public VSTGUI::CView {
public:
    explicit ModeTabBar(const VSTGUI::CRect& size);
    ~ModeTabBar() override = default;

    // Tab selection
    int getSelectedTab() const { return selectedTab_; }
    void setSelectedTab(int tab);

    // Callback when selection changes
    using SelectionCallback = std::function<void(int)>;
    void setSelectionCallback(SelectionCallback cb) { selectionCallback_ = std::move(cb); }

    // CView overrides
    void draw(VSTGUI::CDrawContext* context) override;
    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons
    ) override;

private:
    static constexpr int kNumTabs = 12; // All + 11 modes

    int selectedTab_ = 0; // 0 = All, 1-11 = modes
    SelectionCallback selectionCallback_;

    // Tab labels (All + 11 mode names)
    static const std::vector<std::string>& getTabLabels();

    // Get tab bounds for a given index
    VSTGUI::CRect getTabRect(int index) const;
};

} // namespace Iterum
