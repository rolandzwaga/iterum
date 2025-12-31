#pragma once

// ==============================================================================
// PresetDataSource - CDataBrowser Delegate for Preset List
// ==============================================================================
// Spec 042: Preset Browser
// Implements IDataBrowserDelegate to provide data for the preset list.
// ==============================================================================

#include "vstgui/lib/idatabrowserdelegate.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "../preset/preset_info.h"
#include <vector>
#include <string>

namespace Iterum {

class PresetDataSource : public VSTGUI::DataBrowserDelegateAdapter {
public:
    PresetDataSource() = default;
    ~PresetDataSource() override = default;

    // Data management
    void setPresets(const std::vector<PresetInfo>& presets);
    void setModeFilter(int mode);  // -1 = All
    void setSearchFilter(const std::string& query);
    const PresetInfo* getPresetAtRow(int row) const;

    // IDataBrowserDelegate overrides
    int32_t dbGetNumRows(VSTGUI::CDataBrowser* browser) override;
    int32_t dbGetNumColumns(VSTGUI::CDataBrowser* browser) override;
    VSTGUI::CCoord dbGetRowHeight(VSTGUI::CDataBrowser* browser) override;
    VSTGUI::CCoord dbGetCurrentColumnWidth(int32_t index, VSTGUI::CDataBrowser* browser) override;

    void dbDrawCell(
        VSTGUI::CDrawContext* context,
        const VSTGUI::CRect& size,
        int32_t row,
        int32_t column,
        int32_t flags,
        VSTGUI::CDataBrowser* browser
    ) override;

    void dbSelectionChanged(VSTGUI::CDataBrowser* browser) override;

    VSTGUI::CMouseEventResult dbOnMouseDown(
        const VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons,
        int32_t row,
        int32_t column,
        VSTGUI::CDataBrowser* browser
    ) override;

    // Selection callback
    using SelectionCallback = std::function<void(int)>;
    using DoubleClickCallback = std::function<void(int)>;

    void setSelectionCallback(SelectionCallback cb) { selectionCallback_ = std::move(cb); }
    void setDoubleClickCallback(DoubleClickCallback cb) { doubleClickCallback_ = std::move(cb); }

private:
    std::vector<PresetInfo> allPresets_;
    std::vector<PresetInfo> filteredPresets_;
    int modeFilter_ = -1;
    std::string searchFilter_;

    SelectionCallback selectionCallback_;
    DoubleClickCallback doubleClickCallback_;

    void applyFilters();
};

} // namespace Iterum
