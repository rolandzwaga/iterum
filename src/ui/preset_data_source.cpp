#include "preset_data_source.h"
#include "vstgui/lib/cdatabrowser.h"
#include "vstgui/lib/cfont.h"
#include <algorithm>
#include <cctype>

namespace Iterum {

void PresetDataSource::setPresets(const std::vector<PresetInfo>& presets) {
    allPresets_ = presets;
    applyFilters();
}

void PresetDataSource::setModeFilter(int mode) {
    modeFilter_ = mode;
    applyFilters();
}

void PresetDataSource::setSearchFilter(const std::string& query) {
    searchFilter_ = query;
    applyFilters();
}

const PresetInfo* PresetDataSource::getPresetAtRow(int row) const {
    if (row >= 0 && row < static_cast<int>(filteredPresets_.size())) {
        return &filteredPresets_[static_cast<size_t>(row)];
    }
    return nullptr;
}

void PresetDataSource::applyFilters() {
    filteredPresets_.clear();

    // Convert search to lowercase
    std::string lowerSearch = searchFilter_;
    std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto& preset : allPresets_) {
        // Apply mode filter
        if (modeFilter_ >= 0 && static_cast<int>(preset.mode) != modeFilter_) {
            continue;
        }

        // Apply search filter
        if (!lowerSearch.empty()) {
            std::string lowerName = preset.name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (lowerName.find(lowerSearch) == std::string::npos) {
                continue;
            }
        }

        filteredPresets_.push_back(preset);
    }
}

// =============================================================================
// IDataBrowserDelegate Implementation
// =============================================================================

int32_t PresetDataSource::dbGetNumRows(VSTGUI::CDataBrowser* /*browser*/) {
    return static_cast<int32_t>(filteredPresets_.size());
}

int32_t PresetDataSource::dbGetNumColumns(VSTGUI::CDataBrowser* /*browser*/) {
    // Name, Category (optionally Mode when "All" filter)
    return modeFilter_ < 0 ? 3 : 2;
}

VSTGUI::CCoord PresetDataSource::dbGetRowHeight(VSTGUI::CDataBrowser* /*browser*/) {
    return 24.0; // Standard row height
}

VSTGUI::CCoord PresetDataSource::dbGetCurrentColumnWidth(int32_t index, VSTGUI::CDataBrowser* browser) {
    auto totalWidth = browser->getWidth();

    if (modeFilter_ < 0) {
        // Three columns: Name, Category, Mode
        switch (index) {
            case 0: return totalWidth * 0.45; // Name
            case 1: return totalWidth * 0.30; // Category
            case 2: return totalWidth * 0.25; // Mode
            default: return 100.0;
        }
    } else {
        // Two columns: Name, Category
        switch (index) {
            case 0: return totalWidth * 0.60; // Name
            case 1: return totalWidth * 0.40; // Category
            default: return 100.0;
        }
    }
}

void PresetDataSource::dbDrawCell(
    VSTGUI::CDrawContext* context,
    const VSTGUI::CRect& size,
    int32_t row,
    int32_t column,
    int32_t flags,
    VSTGUI::CDataBrowser* /*browser*/
) {
    if (row < 0 || row >= static_cast<int32_t>(filteredPresets_.size())) {
        return;
    }

    const auto& preset = filteredPresets_[static_cast<size_t>(row)];

    // Set colors based on selection and factory status
    VSTGUI::CColor textColor = preset.isFactory
        ? VSTGUI::CColor(150, 200, 255)  // Light blue for factory
        : VSTGUI::CColor(255, 255, 255); // White for user

    if (flags & VSTGUI::IDataBrowserDelegate::kRowSelected) {
        context->setFillColor(VSTGUI::CColor(60, 100, 160));
        context->drawRect(size, VSTGUI::kDrawFilled);
    }

    // Set font before drawing text (required for VSTGUI)
    auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
    context->setFont(font);
    context->setFontColor(textColor);

    // Get text for column
    std::string text;
    switch (column) {
        case 0:
            text = preset.name;
            if (preset.isFactory) {
                text += " [Factory]";
            }
            break;
        case 1:
            text = preset.category;
            break;
        case 2:
            if (modeFilter_ < 0) {
                // Show mode name
                static const char* modeNames[] = {
                    "Granular", "Spectral", "Shimmer", "Tape", "BBD",
                    "Digital", "PingPong", "Reverse", "MultiTap", "Freeze", "Ducking"
                };
                int modeIndex = static_cast<int>(preset.mode);
                if (modeIndex >= 0 && modeIndex < 11) {
                    text = modeNames[modeIndex];
                }
            }
            break;
    }

    // Draw text
    auto textRect = size;
    textRect.inset(4, 0);
    context->drawString(text.c_str(), textRect, VSTGUI::kLeftText);
}

void PresetDataSource::dbSelectionChanged(VSTGUI::CDataBrowser* browser) {
    if (selectionCallback_) {
        selectionCallback_(browser->getSelectedRow());
    }
}

VSTGUI::CMouseEventResult PresetDataSource::dbOnMouseDown(
    const VSTGUI::CPoint& /*where*/,
    const VSTGUI::CButtonState& buttons,
    int32_t row,
    int32_t /*column*/,
    VSTGUI::CDataBrowser* /*browser*/
) {
    // Note: CDataBrowser only calls this delegate for valid row clicks.
    // Empty space clicks are consumed by CDataBrowser without calling delegate.
    // Empty space deselection is handled in PresetBrowserView::onMouseDown instead.

    // Handle double-click on valid rows
    if (buttons.isDoubleClick() && doubleClickCallback_) {
        doubleClickCallback_(row);
        return VSTGUI::kMouseEventHandled;
    }

    return VSTGUI::kMouseEventNotHandled;
}

} // namespace Iterum
