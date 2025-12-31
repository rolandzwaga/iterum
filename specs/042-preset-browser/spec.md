# Feature Specification: Preset Browser

**Feature Branch**: `042-preset-browser`
**Created**: 2025-12-31
**Status**: Draft
**Input**: Full-featured preset browser with popup overlay, mode-based organization, save/load/import/delete, search, factory presets, cross-platform VSTGUI

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Load Existing Preset (Priority: P1)

A producer working on a track wants to quickly audition different delay sounds. They click the "Presets" button, browse presets filtered by the current delay mode (e.g., Tape), and double-click a preset to load it instantly. All parameters update and the delay sound changes immediately.

**Why this priority**: This is the core value proposition - users need to load presets before anything else matters. Without loading, saving and organizing are pointless.

**Independent Test**: Can be fully tested by creating a test preset file, opening the browser, and verifying double-click loads all parameters correctly and mode switches if needed.

**Acceptance Scenarios**:

1. **Given** the plugin is open with Digital mode active, **When** user clicks "Presets" button, **Then** a popup overlay appears showing presets filtered to Digital mode
2. **Given** the preset browser is open, **When** user double-clicks a preset named "Vintage Slap", **Then** all parameters are restored from the preset and the browser closes
3. **Given** a preset targets Tape mode but user is in Digital mode, **When** user loads that preset, **Then** the plugin switches to Tape mode with crossfade and all Tape parameters are restored
4. **Given** a preset file is corrupted, **When** user attempts to load it, **Then** an error message appears and the current state is preserved

---

### User Story 2 - Save Current Settings as Preset (Priority: P1)

A sound designer has crafted a unique delay sound and wants to save it for later use. They click "Save As", enter a name and category, and the preset is saved to their user preset folder organized by mode.

**Why this priority**: Equal to loading - users need both to have a functional preset system. Saving enables users to build their personal library.

**Independent Test**: Can be tested by modifying parameters, clicking Save As, entering details, and verifying the preset file exists and can be reloaded with correct values.

**Acceptance Scenarios**:

1. **Given** user has modified parameters, **When** they click "Save As" button, **Then** a save dialog appears with name field, category dropdown, and optional description
2. **Given** save dialog is open, **When** user enters "My Cool Delay" and selects "Ambient" category, **Then** preset is saved to user folder under current mode subfolder
3. **Given** a preset with that name already exists, **When** user tries to save, **Then** a confirmation prompt asks to overwrite or rename
4. **Given** user enters invalid characters in name (e.g., `/`, `\`, `:`), **When** they try to save, **Then** validation error is shown

---

### User Story 3 - Filter Presets by Mode (Priority: P2)

A user wants to explore all available Granular delay presets. They open the browser and click the "Granular" tab in the mode filter. Only Granular presets appear in the list.

**Why this priority**: Mode filtering is essential for organization but load/save must work first. Users with many presets need this to find sounds quickly.

**Independent Test**: Can be tested by having presets in multiple mode folders, clicking different mode tabs, and verifying the list updates correctly.

**Acceptance Scenarios**:

1. **Given** browser is open, **When** user clicks "Granular" mode tab, **Then** only presets with Granular mode metadata are shown
2. **Given** "All" tab is selected, **When** viewing the list, **Then** all presets from all modes are shown with mode indicated in a column
3. **Given** browser opens while plugin is in Shimmer mode, **When** browser appears, **Then** Shimmer mode tab is selected by default

---

### User Story 4 - Search Presets by Name (Priority: P2)

A user remembers they have a preset with "pad" in the name but can't remember the exact name or mode. They type "pad" in the search field and see all matching presets across modes.

**Why this priority**: Search is crucial for users with large preset collections but mode filtering handles most navigation needs.

**Independent Test**: Can be tested by typing search terms and verifying the list filters in real-time to show only matching presets.

**Acceptance Scenarios**:

1. **Given** browser is open with presets listed, **When** user types "ambient" in search field, **Then** list filters in real-time showing only presets with "ambient" in name
2. **Given** search field has text, **When** user clicks clear button, **Then** search is cleared and full list is restored
3. **Given** search is "xyz123" with no matches, **When** viewing list, **Then** empty state message is shown

---

### User Story 5 - Import External Preset (Priority: P3)

A user downloads a preset pack from a sound designer's website. They click "Import", select the .vstpreset files, and they're copied to their user preset folder.

**Why this priority**: Import enables sharing and community presets but is not essential for basic workflow.

**Independent Test**: Can be tested by placing a valid .vstpreset file externally, using Import, and verifying it appears in the user preset list.

**Acceptance Scenarios**:

1. **Given** user clicks "Import", **When** file dialog opens, **Then** it filters for .vstpreset files
2. **Given** user selects valid preset file, **When** confirming import, **Then** file is copied to user presets under detected mode folder
3. **Given** imported preset name conflicts with existing, **When** import proceeds, **Then** user is prompted to rename or overwrite

---

### User Story 6 - Delete User Preset (Priority: P3)

A user wants to clean up presets they no longer use. They select a preset and click "Delete". After confirmation, the preset is removed.

**Why this priority**: Deletion is a maintenance task, not core workflow. Users can work without it initially.

**Independent Test**: Can be tested by selecting a user preset, clicking Delete, confirming, and verifying the file is removed and list updates.

**Acceptance Scenarios**:

1. **Given** user preset is selected, **When** user clicks "Delete", **Then** confirmation dialog appears
2. **Given** confirmation dialog is shown, **When** user confirms, **Then** preset file is deleted and removed from list
3. **Given** factory preset is selected, **When** viewing Delete button, **Then** button is disabled or hidden

---

### User Story 7 - Browse Factory Presets (Priority: P3)

A new user wants to explore the sounds that come with the plugin. They open the browser and see factory presets clearly distinguished from user presets.

**Why this priority**: Factory presets enhance first-run experience but don't block core functionality.

**Independent Test**: Can be tested by verifying factory presets appear in list with visual distinction and cannot be modified/deleted.

**Acceptance Scenarios**:

1. **Given** browser is open, **When** viewing preset list, **Then** factory presets show visual indicator (icon or label)
2. **Given** factory preset is selected, **When** user tries to delete, **Then** action is blocked
3. **Given** factory preset is selected, **When** user modifies parameters and clicks Save, **Then** Save As dialog opens (cannot overwrite factory)

---

### Edge Cases

- What happens when preset folder doesn't exist on first run? System creates it automatically.
- What happens when user has no write permissions to preset folder? Show clear error message with path.
- What happens when disk is full during save? Show error, preserve current state.
- What happens when preset file is locked by another process? Show error, suggest closing other applications.
- What happens when mode in preset metadata doesn't match folder location? Trust metadata over folder.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST display preset browser as modal popup overlay triggered by "Presets" button
- **FR-002**: System MUST use standard .vstpreset file format for preset storage
- **FR-003**: System MUST store all 11 modes' parameters in each preset (consistent with project save)
- **FR-004**: System MUST store preset metadata including name, category, target mode, and optional description
- **FR-005**: System MUST scan and display presets from both user and factory preset directories
- **FR-006**: System MUST organize presets by delay mode with a vertical tab bar filter
- **FR-007**: System MUST provide "All" filter option showing presets from all modes
- **FR-008**: System MUST display presets in a scrollable list with Name and Category columns
- **FR-009**: System MUST load preset on double-click, restoring all parameters
- **FR-010**: System MUST switch plugin mode automatically when loading preset targeting different mode
- **FR-011**: System MUST provide "Save As" dialog for saving new presets with name and category
- **FR-012**: System MUST validate preset names and reject invalid filesystem characters
- **FR-013**: System MUST prevent overwriting factory presets
- **FR-014**: System MUST provide real-time search filtering by preset name (within 100ms per SC-004)
- **FR-015**: System MUST provide Import function using native file dialog
- **FR-016**: System MUST provide Delete function with confirmation for user presets only
- **FR-017**: System MUST visually distinguish factory presets from user presets (e.g., different text color, icon, or "[Factory]" label)
- **FR-018**: System MUST close browser on Escape key or click outside overlay
- **FR-019**: System MUST default mode filter to current plugin mode when browser opens
- **FR-020**: System MUST handle missing/corrupted preset files gracefully with error message
- **FR-021**: System MUST prompt for confirmation when saving would overwrite an existing preset

### Key Entities

- **Preset**: A saved plugin state including all parameters for all 11 modes, plus metadata (name, category, target mode, description, author)
- **Preset Category**: A grouping label for presets (e.g., "Ambient", "Rhythmic", "Classic", "Experimental")
- **Mode Filter**: One of 12 options: "All" plus the 11 delay modes (Granular, Spectral, Shimmer, Tape, BBD, Digital, PingPong, Reverse, MultiTap, Freeze, Ducking)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can save and load presets with 100% parameter accuracy (all values match after round-trip)
- **SC-002**: Preset browser opens and displays presets within 500ms for collections up to 1000 presets
- **SC-003**: Preset load completes within 100ms including mode switch crossfade
- **SC-004**: Search filtering updates the list within 100ms of keystroke
- **SC-005**: Factory presets are visible on first plugin launch (at least 22 presets: 2 per mode)
- **SC-006**: Plugin passes pluginval level 5 with preset operations (load, save, rapid switching)
- **SC-007**: Feature works identically on Windows, macOS, and Linux (cross-platform verification)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Plugin state serialization (getState/setState) already works correctly for all 11 modes
- Mode switching with crossfade (spec 041) is already implemented
- Users have write access to their home directory for user presets
- VST3 SDK PresetFile class is available and functional
- VSTGUI CDataBrowser and CNewFileSelector are available and cross-platform

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| State serialization | processor.cpp getState/setState | Reuse for preset content |
| Mode enum | plugin_ids.h DelayMode | Use for mode filtering |
| Controller state sync | controller.cpp setComponentState | Reuse for preset loading |
| Parameter structs | src/parameters/*.h | Already handle save/load |
| Custom view creation | controller.cpp createCustomView | Extend for preset browser |

**Initial codebase search for key terms:**

```bash
grep -r "PresetFile" src/
grep -r "preset" src/
grep -r "CDataBrowser" src/
```

**Search Results Summary**: No existing preset implementation found. PresetFile usage will be new. CDataBrowser not currently used.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- Future: Preset exchange/sharing feature could reuse PresetManager
- Future: A/B comparison feature could use preset load/save infrastructure

**Potential shared components** (preliminary, refined in plan.md):
- PresetManager class could be reused for any preset-related features
- Platform path abstraction could be reused for other file operations

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| FR-017 | | |
| FR-018 | | |
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
