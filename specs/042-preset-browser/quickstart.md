# Quickstart: Preset Browser Implementation

## Prerequisites

- Read `specs/TESTING-GUIDE.md` for test patterns
- Read `specs/VST-GUIDE.md` Section 8 for cross-platform rules
- Understand existing state serialization in `processor.cpp`

## Implementation Order

### Phase 1: Core Infrastructure (P1 Stories: Load/Save)

#### 1.1 Platform Path Abstraction

**Create files:**
- `src/platform/preset_paths.h`
- `src/platform/preset_paths.cpp` (single file with platform #ifdefs)

**Test first:**
```cpp
TEST_CASE("Platform::getUserPresetDirectory returns valid path") {
    auto path = Platform::getUserPresetDirectory();
    REQUIRE(!path.empty());
    // Path should end with Iterum/Iterum
}
```

**Implementation:**
```cpp
namespace Platform {
    std::filesystem::path getUserPresetDirectory() {
        #if defined(_WIN32)
            const char* userProfile = std::getenv("USERPROFILE");
            return fs::path(userProfile) / "Documents" / "VST3 Presets" / "Iterum" / "Iterum";
        #elif defined(__APPLE__)
            const char* home = std::getenv("HOME");
            return fs::path(home) / "Library" / "Audio" / "Presets" / "Iterum" / "Iterum";
        #else
            const char* home = std::getenv("HOME");
            return fs::path(home) / ".vst3" / "presets" / "Iterum" / "Iterum";
        #endif
    }
}
```

#### 1.2 PresetInfo Struct

**Create file:** `src/preset/preset_info.h`

```cpp
#pragma once
#include <string>
#include <filesystem>
#include "../plugin_ids.h"  // For DelayMode

namespace Iterum {

struct PresetInfo {
    std::string name;
    std::string category;
    DelayMode mode = DelayMode::Digital;
    std::filesystem::path path;
    bool isFactory = false;
    std::string description;
    std::string author;

    bool isValid() const { return !name.empty() && !path.empty(); }
};

} // namespace Iterum
```

#### 1.3 PresetManager Core

**Create files:**
- `src/preset/preset_manager.h`
- `src/preset/preset_manager.cpp`

**Test first:**
```cpp
TEST_CASE("PresetManager scans preset directories") {
    // Create temp directory with test preset files
    // Scan and verify PresetInfo populated correctly
}

TEST_CASE("PresetManager loads preset restoring parameters") {
    // Create mock IComponent/IEditController
    // Load preset file
    // Verify state was applied
}

TEST_CASE("PresetManager saves preset with metadata") {
    // Save preset with name/category
    // Verify file exists and metadata readable
}
```

**Key implementation notes:**
- Use VST3 SDK `PresetFile::savePreset()` and `loadPreset()` static methods
- Store metadata in XML chunk via `writeMetaInfo()`
- Parse metadata XML on load to extract mode/category

### Phase 2: UI Components (P2 Stories: Filter/Search)

#### 2.1 PresetDataSource (IDataBrowserDelegate)

**Create files:**
- `src/ui/preset_data_source.h`
- `src/ui/preset_data_source.cpp`

**Key methods to implement:**
```cpp
class PresetDataSource : public VSTGUI::DataBrowserDelegateAdapter {
public:
    void setPresets(const std::vector<PresetInfo>& presets);
    void setModeFilter(int mode);  // -1 = All
    void setSearchFilter(const std::string& query);

    // Required overrides
    int32_t dbGetNumRows(CDataBrowser*) override;
    int32_t dbGetNumColumns(CDataBrowser*) override;
    CCoord dbGetRowHeight(CDataBrowser*) override;
    CCoord dbGetCurrentColumnWidth(int32_t, CDataBrowser*) override;
    void dbDrawCell(CDrawContext*, const CRect&, int32_t, int32_t, int32_t, CDataBrowser*) override;
    void dbSelectionChanged(CDataBrowser*) override;
    CMouseEventResult dbOnMouseDown(const CPoint&, const CButtonState&, int32_t, int32_t, CDataBrowser*) override;
};
```

#### 2.2 ModeTabBar

**Create files:**
- `src/ui/mode_tab_bar.h`
- `src/ui/mode_tab_bar.cpp`

**Simple implementation:**
- Subclass `CView`
- Draw 12 labeled buttons vertically (All + 11 modes)
- Track selected tab
- Callback on selection change

#### 2.3 PresetBrowserView

**Create files:**
- `src/ui/preset_browser_view.h`
- `src/ui/preset_browser_view.cpp`

**Layout:**
```
+----------------------------------------------+
| [X] Preset Browser                           |
+----------+-----------------------------------+
|          | Preset List                       |
| All      | Name           | Category         |
| Granular | --------------|------------------|
| Spectral | Dreamy Echoes | Ambient          |
| ...      | Vintage Slap  | Classic          |
|          |               |                  |
+----------+-----------------------------------+
|          | [Search: ______________] [Clear]  |
+----------+-----------------------------------+
| [Save] [Save As] [Import] [Delete] | [Close] |
+----------------------------------------------+
```

**Key behaviors:**
- Modal overlay with semi-transparent background
- Click background to close
- Escape key to close
- Double-click row to load preset

### Phase 3: Integration (P3 Stories: Import/Delete/Factory)

#### 3.1 Controller Integration

**Modify:** `src/controller/controller.cpp`

```cpp
CView* Controller::createCustomView(UTF8StringPtr name, ...) {
    if (UTF8StringView(name) == "PresetBrowser") {
        return new PresetBrowserView(presetManager_.get(), this);
    }
    return nullptr;
}
```

#### 3.2 Editor Integration

**Modify:** `resources/editor.uidesc`

Add "Presets" button to main UI that triggers browser popup.

#### 3.3 Import/Delete Functions

Already planned in PresetManager:
- `importPreset()` - Copy file to user directory
- `deletePreset()` - Remove file (user presets only)

### Phase 4: Polish

#### 4.1 Factory Presets

**Create:** `resources/presets/[Mode]/[Category]/[Name].vstpreset`

At least 2 presets per mode = 22 minimum.

#### 4.2 Installer Integration

Update CMake/installer scripts to copy `resources/presets/` to factory location.

## Testing Checklist

- [ ] Unit tests for PresetManager (scan, load, save, delete)
- [ ] Unit tests for Platform paths
- [ ] Integration test for full browser workflow (if possible without DAW)
- [ ] pluginval level 5 with preset operations
- [ ] Manual test on Windows
- [ ] Manual test on macOS
- [ ] Manual test on Linux

## Common Pitfalls

1. **CNewFileSelector memory management**: Call `forget()` after `run()`
2. **CDataBrowser delegate lifetime**: Ensure delegate outlives browser
3. **Path encoding on Windows**: Use `path.wstring()` for Windows API calls
4. **PresetFile class ID**: Must match `Processor::cid`
5. **XML parsing**: Handle malformed metadata gracefully
