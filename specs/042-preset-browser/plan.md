# Implementation Plan: Preset Browser

**Branch**: `042-preset-browser` | **Date**: 2025-12-31 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/042-preset-browser/spec.md`

## Summary

Implement a full-featured preset browser for the Iterum delay plugin with:
- Popup overlay UI using VSTGUI custom views (CDataBrowser for list, CNewFileSelector for dialogs)
- Mode-based preset organization (11 delay modes + "All" filter)
- Standard .vstpreset file format via VST3 SDK PresetFile class
- Save/Load/Import/Delete operations with search filtering
- Factory presets bundled with installer
- Strict cross-platform compatibility (VSTGUI only, no native code)

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK (PresetFile), VSTGUI (CDataBrowser, CNewFileSelector, CView)
**Storage**: .vstpreset files in platform-specific preset directories
**Testing**: Catch2 (unit tests for PresetManager), pluginval (integration)
**Target Platform**: Windows 10+, macOS 11+, Linux (VST3 only)
**Project Type**: Single VST3 plugin
**Performance Goals**: <500ms browser open, <100ms preset load, <100ms search filter
**Constraints**: Cross-platform only (no Win32/Cocoa), UI thread only (no audio thread involvement)
**Scale/Scope**: Support 1000+ presets, 22+ factory presets (2 per mode)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | PresetManager runs on UI thread, uses IComponent/IEditController for state |
| II. Real-Time Thread Safety | PASS | No audio thread involvement - all preset ops on UI thread |
| V. VSTGUI Development | PASS | Using CDataBrowser, CNewFileSelector, custom CView subclasses |
| VI. Cross-Platform Compatibility | PASS | VSTGUI only, std::filesystem for paths, see VST-GUIDE.md Section 8 |
| VIII. Testing Discipline | PASS | Unit tests for PresetManager, pluginval for integration |
| XIII. Test-First Development | PASS | Tests before implementation |
| XIV. Living Architecture | PASS | Will update ARCHITECTURE.md on completion |
| XV. ODR Prevention | PASS | No existing Preset classes found (see below) |
| XVII. Framework Knowledge | PASS | VST-GUIDE.md Section 8 documents cross-platform rules |

**Required Check - Principle XIII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: PresetInfo, PresetManager, PresetBrowserView, PresetDataSource, ModeTabBar

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| PresetInfo | `grep -r "struct PresetInfo" src/` | No | Create New |
| PresetManager | `grep -r "class PresetManager" src/` | No | Create New |
| PresetBrowserView | `grep -r "class PresetBrowserView" src/` | No | Create New |
| PresetDataSource | `grep -r "class PresetDataSource" src/` | No | Create New |
| ModeTabBar | `grep -r "class ModeTabBar" src/` | No | Create New |

**Utility Functions to be created**: Platform path helpers

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| getUserPresetDirectory | `grep -r "getUserPresetDirectory" src/` | No | — | Create New in platform/ |
| getFactoryPresetDirectory | `grep -r "getFactoryPresetDirectory" src/` | No | — | Create New in platform/ |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayMode enum | plugin_ids.h | — | Mode filtering |
| Processor::getState/setState | processor.cpp | — | Preset content via PresetFile |
| Controller::setComponentState | controller.cpp | — | Apply loaded preset |
| Controller::createCustomView | controller.cpp | — | Register PresetBrowserView |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No preset-related code
- [x] `src/controller/controller.h` - Has createCustomView ready to extend
- [x] `ARCHITECTURE.md` - No PresetManager or browser components

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. PresetInfo, PresetManager, and UI components are entirely new.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PresetFile | loadPreset | `static bool loadPreset(IBStream*, const FUID&, IComponent*, IEditController*, std::vector<FUID>*)` | ✓ |
| PresetFile | savePreset | `static bool savePreset(IBStream*, const FUID&, IComponent*, IEditController*, const char*, int32)` | ✓ |
| CDataBrowser | constructor | `CDataBrowser(const CRect&, IDataBrowserDelegate*, int32_t, CCoord, CBitmap*)` | ✓ |
| CNewFileSelector | create | `static CNewFileSelector* create(CFrame*, Style)` | ✓ |
| IDataBrowserDelegate | dbGetNumRows | `virtual int32_t dbGetNumRows(CDataBrowser*) = 0` | ✓ |
| IDataBrowserDelegate | dbDrawCell | `virtual void dbDrawCell(CDrawContext*, const CRect&, int32_t, int32_t, int32_t, CDataBrowser*) = 0` | ✓ |

### Header Files Read

- [x] `extern/vst3sdk/public.sdk/source/vst/vstpresetfile.h` - PresetFile class
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/cdatabrowser.h` - CDataBrowser class
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/idatabrowserdelegate.h` - IDataBrowserDelegate interface
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/cfileselector.h` - CNewFileSelector class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| CDataBrowser | Delegate methods are pure virtual | Must implement ALL IDataBrowserDelegate methods or use DataBrowserDelegateAdapter |
| CNewFileSelector | Must call forget() after run() | `selector->run([](...){}); selector->forget();` |
| PresetFile | Needs class ID from processor | Use `Processor::cid` static member |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

This feature creates no reusable DSP utilities. All code is UI/file-system related.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| scanPresetDirectory | PresetManager-specific, uses std::filesystem |
| filterPresetsByMode | PresetDataSource-specific |

**Decision**: No Layer 0 extraction needed. This is a UI feature, not DSP.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: UI/Controller Layer (not DSP layers 0-4)

**Related features at same layer** (from ROADMAP.md or known plans):
- Future: Preset exchange/sharing feature
- Future: A/B comparison feature
- Future: Undo/redo system

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| PresetManager | HIGH | Preset exchange, A/B compare | Keep in src/preset/, well-abstracted API |
| Platform::PresetPaths | HIGH | Any file-based feature | Keep in src/platform/ |
| PresetBrowserView | LOW | Specific to this UI | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| PresetManager as standalone class | Clean API for future preset-related features |
| Platform paths isolated | Reusable for any cross-platform file operations |

## Project Structure

### Documentation (this feature)

```text
specs/042-preset-browser/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 - already complete (preset-browser-plan.md has content)
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (internal interfaces)
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
src/
├── preset/                         # NEW: Preset management
│   ├── preset_info.h               # PresetInfo struct
│   ├── preset_manager.h            # PresetManager class
│   └── preset_manager.cpp
├── platform/                       # NEW: Platform-specific code
│   ├── preset_paths.h              # Platform::PresetPaths interface
│   └── preset_paths.cpp            # Single file with platform #ifdefs (#if defined(_WIN32), __APPLE__, else)
├── ui/                             # NEW: Custom VSTGUI views
│   ├── preset_browser_view.h       # PresetBrowserView (popup container)
│   ├── preset_browser_view.cpp
│   ├── preset_data_source.h        # IDataBrowserDelegate implementation
│   ├── preset_data_source.cpp
│   ├── mode_tab_bar.h              # Mode filter tabs
│   └── mode_tab_bar.cpp
├── controller/
│   ├── controller.h                # MODIFY: Add preset browser support
│   └── controller.cpp              # MODIFY: createCustomView, button handler
└── resources/
    ├── editor.uidesc               # MODIFY: Add "Presets" button
    └── presets/                    # NEW: Factory presets
        ├── Granular/
        ├── Spectral/
        └── ... (11 mode folders)

tests/
├── unit/
│   └── preset/
│       ├── preset_manager_test.cpp
│       └── preset_info_test.cpp
└── integration/
    └── preset_browser_test.cpp     # If feasible without DAW
```

**Structure Decision**: New `src/preset/`, `src/platform/`, and `src/ui/` directories for clean separation. Factory presets in `resources/presets/` copied to install location.

## Complexity Tracking

No Constitution violations requiring justification.

---

## Phase 0: Research Summary

*Research completed during initial planning. See `specs/preset-browser-plan.md` for detailed findings.*

### Key Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Preset file format | .vstpreset via PresetFile class | Standard VST3 format, host compatibility |
| Preset list UI | CDataBrowser with custom delegate | Built into VSTGUI, cross-platform, handles scroll/selection |
| File dialogs | CNewFileSelector | Cross-platform abstraction over native dialogs |
| Path handling | std::filesystem + Platform:: namespace | Platform code isolated, UI code portable |
| Mode filtering | Vertical tab bar (custom CView) | Clear visual organization |
| Metadata storage | XML chunk in .vstpreset | Standard PresetFile mechanism |

### Alternatives Considered

| Topic | Alternative | Why Rejected |
|-------|-------------|--------------|
| Preset format | Custom binary | No host integration, harder to debug |
| Preset format | JSON | Not standard, larger files, slower |
| List UI | Custom CView from scratch | Reinvents wheel, CDataBrowser handles scroll/selection |
| File dialogs | Native dialogs | Violates cross-platform principle |
| Mode filtering | Dropdown menu | Less visual, harder to scan 12 options |

---

## Phase 1: Design Artifacts

### Data Model

See [data-model.md](data-model.md) for entity definitions.

**Key Entities:**

1. **PresetInfo** - Metadata for a single preset
   - name: string
   - category: string
   - mode: DelayMode enum
   - path: std::filesystem::path
   - isFactory: bool
   - description: string (optional)
   - author: string (optional)

2. **PresetManager** - Handles all preset file operations
   - scanPresets() → vector<PresetInfo>
   - loadPreset(PresetInfo) → bool
   - savePreset(name, category) → bool
   - deletePreset(PresetInfo) → bool
   - importPreset(path) → bool

3. **PresetBrowserView** - Popup overlay container
   - Contains: ModeTabBar, CDataBrowser, search field, buttons
   - Modal behavior with background dim
   - Escape/click-outside to close

### Contracts

Internal interfaces (not REST APIs):

```cpp
// src/preset/preset_info.h
struct PresetInfo {
    std::string name;
    std::string category;
    DelayMode mode;
    std::filesystem::path path;
    bool isFactory;
    std::string description;
    std::string author;
};

// src/preset/preset_manager.h
// Ownership: Controller creates and owns PresetManager instance as a member variable.
// Lifetime: Created in Controller::initialize(), destroyed with Controller.
class PresetManager {
public:
    using PresetList = std::vector<PresetInfo>;

    explicit PresetManager(IComponent* processor, IEditController* controller);

    PresetList scanPresets();
    PresetList getPresetsForMode(DelayMode mode) const;
    PresetList searchPresets(std::string_view query) const;

    bool loadPreset(const PresetInfo& preset);
    bool savePreset(const std::string& name, const std::string& category,
                    DelayMode mode, const std::string& description = "");
    bool deletePreset(const PresetInfo& preset);
    bool importPreset(const std::filesystem::path& sourcePath);

    std::filesystem::path getUserPresetDirectory() const;
    std::filesystem::path getFactoryPresetDirectory() const;
};

// src/platform/preset_paths.h
namespace Platform {
    std::filesystem::path getUserPresetDirectory();
    std::filesystem::path getFactoryPresetDirectory();
    bool ensureDirectoryExists(const std::filesystem::path& path);
}
```

### Quickstart

See [quickstart.md](quickstart.md) for implementation sequence.

**Implementation Order:**

1. **Platform paths** - Foundation for all file operations
2. **PresetInfo struct** - Data model
3. **PresetManager** - Core logic with unit tests
4. **PresetDataSource** - CDataBrowser delegate
5. **ModeTabBar** - Mode filter UI
6. **PresetBrowserView** - Main popup container
7. **Controller integration** - createCustomView, button handler
8. **Factory presets** - Content creation
9. **Installer integration** - Bundle presets
