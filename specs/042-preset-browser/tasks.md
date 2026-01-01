# Tasks: Preset Browser

**Input**: Design documents from `/specs/042-preset-browser/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## User Stories Summary

| ID | Title | Priority | Independent Test |
|----|-------|----------|------------------|
| US1 | Load Existing Preset | P1 | Create test preset, verify double-click loads parameters |
| US2 | Save Current Settings | P1 | Modify parameters, save, verify file exists and reloads |
| US3 | Filter Presets by Mode | P2 | Multiple mode folders, click tabs, verify list filters |
| US4 | Search Presets by Name | P2 | Type search term, verify real-time filtering |
| US5 | Import External Preset | P3 | External .vstpreset, import, verify appears in list |
| US6 | Delete User Preset | P3 | Select preset, delete, verify removed |
| US7 | Browse Factory Presets | P3 | Verify factory presets visible and protected |

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` and `specs/VST-GUIDE.md` are in context
2. **Write Failing Tests**: Create test file and write tests that FAIL
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

---

## Phase 1: Setup (Project Structure)

**Purpose**: Create directory structure for new components

- [x] T001 Create `src/preset/` directory for preset management code
- [x] T002 Create `src/platform/` directory for platform-specific code
- [x] T003 Create `src/ui/` directory for custom VSTGUI views
- [x] T004 Create `tests/unit/preset/` directory for preset unit tests
- [x] T005 Create `resources/presets/` directory structure with 11 mode subfolders (Granular/, Spectral/, Shimmer/, Tape/, BBD/, Digital/, PingPong/, Reverse/, MultiTap/, Freeze/, Ducking/)
- [x] T006 Update `src/CMakeLists.txt` to include new source directories

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Pre-Implementation (MANDATORY)

- [x] T007 **Verify TESTING-GUIDE.md and VST-GUIDE.md are in context** (ingest if needed)

### 2.2 Platform Path Abstraction

- [x] T008 [P] Write failing tests for Platform path helpers in `tests/unit/preset/platform_paths_test.cpp`
- [x] T009 [P] Create `src/platform/preset_paths.h` with Platform::getUserPresetDirectory() and Platform::getFactoryPresetDirectory() declarations
- [x] T010 Create `src/platform/preset_paths.cpp` with platform-specific implementations (#if defined(_WIN32), __APPLE__, else)
- [x] T011 Verify platform path tests pass

### 2.3 PresetInfo Struct

- [x] T012 [P] Write failing tests for PresetInfo in `tests/unit/preset/preset_info_test.cpp`
- [x] T013 Create `src/preset/preset_info.h` with PresetInfo struct (name, category, mode, path, isFactory, description, author, isValid())
- [x] T014 Verify PresetInfo tests pass

### 2.4 Commit Foundational Work

- [x] T015 **Commit completed foundational infrastructure**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1+2 - Load and Save Presets (Priority: P1) 🎯 MVP

**Goal**: Users can save current settings as a preset and load existing presets

**Independent Test**:
- US1: Create test preset file, open browser, double-click loads all parameters
- US2: Modify parameters, click Save As, verify file exists and can be reloaded

### 3.1 Pre-Implementation (MANDATORY)

- [x] T016 [US1+2] **Verify TESTING-GUIDE.md and VST-GUIDE.md are in context** (ingest if needed)

### 3.2 Tests for PresetManager Core (Write FIRST - Must FAIL)

- [x] T017 [P] [US1+2] Write failing tests for PresetManager::scanPresets() in `tests/unit/preset/preset_manager_test.cpp`
- [x] T018 [P] [US1+2] Write failing tests for PresetManager::loadPreset() in `tests/unit/preset/preset_manager_test.cpp`
- [x] T019 [P] [US1+2] Write failing tests for PresetManager::savePreset() in `tests/unit/preset/preset_manager_test.cpp`
- [x] T020 [P] [US1+2] Write failing tests for metadata XML read/write in `tests/unit/preset/preset_manager_test.cpp`

### 3.3 Implementation for PresetManager

- [x] T021 [US1+2] Create `src/preset/preset_manager.h` with class declaration per contracts/preset_manager.h
- [x] T022 [US1+2] Implement PresetManager constructor in `src/preset/preset_manager.cpp`
- [x] T023 [US1+2] Implement PresetManager::scanPresets() - scan user and factory directories recursively
- [x] T024 [US1+2] Implement PresetManager::loadPreset() using PresetFile::loadPreset()
- [x] T025 [US1+2] Implement PresetManager::savePreset() using PresetFile::savePreset() with metadata XML
- [x] T026 [US1+2] Implement PresetManager::isValidPresetName() for filename validation
- [x] T027 [US1+2] Verify all PresetManager tests pass (42 assertions in 6 test cases)

### 3.4 Tests for UI Components (Write FIRST - Must FAIL)

- [x] T028 [P] [US1+2] Write failing tests for PresetDataSource in `tests/unit/preset/preset_data_source_test.cpp` (50 assertions pass)

### 3.5 Implementation for UI Components

- [x] T029 [US1+2] Create `src/ui/preset_data_source.h` extending DataBrowserDelegateAdapter
- [x] T030 [US1+2] Implement PresetDataSource in `src/ui/preset_data_source.cpp` (dbGetNumRows, dbGetNumColumns, dbGetRowHeight, dbGetCurrentColumnWidth, dbDrawCell, dbSelectionChanged, dbOnMouseDown for double-click)
- [x] T031 [US1+2] Create `src/ui/preset_browser_view.h` extending CViewContainer per contracts/preset_browser_view.h
- [x] T032 [US1+2] Implement PresetBrowserView::createChildViews() in `src/ui/preset_browser_view.cpp` - create CDataBrowser, buttons
- [x] T033 [US1+2] Implement PresetBrowserView::open() and close() for popup behavior
- [x] T034 [US1+2] Implement PresetBrowserView::onPresetDoubleClicked() to load preset (must trigger mode switch with crossfade per FR-010 if preset targets different mode)
- [x] T035 [US1+2] Implement PresetBrowserView::onSaveAsClicked() with CNewFileSelector
- [x] T035a [US1+2] Implement PresetBrowserView::onSaveClicked() - overwrite currently loaded user preset with confirmation prompt (disable for factory presets)
- [x] T036 [US1+2] Implement PresetBrowserView modal overlay behavior (background dim, click outside to close, Escape key per FR-018)
- [x] T037 [US1+2] Verify PresetDataSource tests pass (92 total assertions in 12 test cases)

### 3.6 Controller Integration

- [x] T038 [US1+2] Modify `src/controller/controller.h` to add PresetManager member and include preset browser headers
- [x] T039 [US1+2] Modify `src/controller/controller.cpp` createCustomView() to return PresetBrowserView for "PresetBrowser" name
- [x] T040 [US1+2] Add "Presets" button to `resources/editor.uidesc` that triggers preset browser popup

### 3.7 Cross-Platform Verification (MANDATORY)

- [x] T041 [US1+2] **Verify IEEE 754 compliance**: Check test files for std::isnan/isfinite usage, add to `-fno-fast-math` list if needed (no floating-point comparison in preset tests)

### 3.8 Build and Test

- [x] T042 [US1+2] Build plugin and verify no compilation errors
- [x] T043 [US1+2] Run pluginval level 5 to verify preset load/save doesn't break plugin
- [ ] T044 [US1+2] Manual test: Open browser, save preset, reload preset, verify parameters match

### 3.9 Commit (MANDATORY)

- [ ] T045 [US1+2] **Commit completed User Story 1+2 work**

**Checkpoint**: Users can save and load presets - MVP complete

---

## Phase 4: User Story 3 - Filter Presets by Mode (Priority: P2)

**Goal**: Users can filter the preset list by delay mode using a vertical tab bar

**Independent Test**: Have presets in multiple mode folders, click different mode tabs, verify list updates correctly

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T046 [US3] **Verify TESTING-GUIDE.md and VST-GUIDE.md are in context** (ingest if needed)

### 4.2 Tests for Mode Filtering (Write FIRST - Must FAIL)

- [ ] T047 [P] [US3] Write failing tests for PresetDataSource::setModeFilter() in `tests/unit/preset/preset_data_source_test.cpp`
- [ ] T048 [P] [US3] Write failing tests for ModeTabBar in `tests/unit/preset/mode_tab_bar_test.cpp`

### 4.3 Implementation for Mode Filtering

- [ ] T049 [US3] Create `src/ui/mode_tab_bar.h` extending CView
- [ ] T050 [US3] Implement ModeTabBar::draw() in `src/ui/mode_tab_bar.cpp` - draw 12 labeled buttons (All + 11 modes)
- [ ] T051 [US3] Implement ModeTabBar selection handling with callback
- [ ] T052 [US3] Implement PresetDataSource::setModeFilter() to filter cached presets by mode
- [ ] T053 [US3] Implement PresetDataSource::getPresetsForMode() with -1 for "All"
- [ ] T054 [US3] Integrate ModeTabBar into PresetBrowserView layout
- [ ] T055 [US3] Implement PresetBrowserView::onModeTabChanged() to update data source filter
- [ ] T056 [US3] Implement default mode selection based on current plugin mode when browser opens
- [ ] T057 [US3] Verify mode filtering tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T058 [US3] **Verify IEEE 754 compliance**: Check new test files, add to `-fno-fast-math` list if needed

### 4.5 Commit (MANDATORY)

- [ ] T059 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Mode filtering works independently

---

## Phase 5: User Story 4 - Search Presets by Name (Priority: P2)

**Goal**: Users can search presets by name with real-time filtering

**Independent Test**: Type search terms, verify list filters in real-time showing only matching presets

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T060 [US4] **Verify TESTING-GUIDE.md and VST-GUIDE.md are in context** (ingest if needed)

### 5.2 Tests for Search (Write FIRST - Must FAIL)

- [ ] T061 [P] [US4] Write failing tests for PresetDataSource::setSearchFilter() in `tests/unit/preset/preset_data_source_test.cpp`
- [ ] T062 [P] [US4] Write failing tests for case-insensitive search matching

### 5.3 Implementation for Search

- [ ] T063 [US4] Implement PresetDataSource::setSearchFilter() with case-insensitive name matching
- [ ] T064 [US4] Add CTextEdit search field to PresetBrowserView layout
- [ ] T065 [US4] Implement PresetBrowserView::onSearchTextChanged() to update data source filter
- [ ] T066 [US4] Add clear button next to search field
- [ ] T067 [US4] Implement empty state message when no presets match search
- [ ] T068 [US4] Verify search tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T069 [US4] **Verify IEEE 754 compliance**: Check new test files, add to `-fno-fast-math` list if needed

### 5.5 Commit (MANDATORY)

- [ ] T070 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Search filtering works independently

---

## Phase 6: User Story 5 - Import External Preset (Priority: P3)

**Goal**: Users can import .vstpreset files from external locations

**Independent Test**: Place valid .vstpreset externally, use Import, verify appears in user preset list

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T071 [US5] **Verify TESTING-GUIDE.md and VST-GUIDE.md are in context** (ingest if needed)

### 6.2 Tests for Import (Write FIRST - Must FAIL)

- [ ] T072 [P] [US5] Write failing tests for PresetManager::importPreset() in `tests/unit/preset/preset_manager_test.cpp`

### 6.3 Implementation for Import

- [ ] T073 [US5] Implement PresetManager::importPreset() - copy file to user directory, detect mode from metadata
- [ ] T074 [US5] Implement PresetBrowserView::onImportClicked() using CNewFileSelector
- [ ] T075 [US5] Handle name conflict during import (prompt to rename or overwrite)
- [ ] T076 [US5] Refresh preset list after successful import
- [ ] T077 [US5] Verify import tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T078 [US5] **Verify IEEE 754 compliance**: Check new test files, add to `-fno-fast-math` list if needed

### 6.5 Commit (MANDATORY)

- [ ] T079 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Import works independently

---

## Phase 7: User Story 6 - Delete User Preset (Priority: P3)

**Goal**: Users can delete their own presets (not factory presets)

**Independent Test**: Select user preset, click Delete, confirm, verify file removed and list updates

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T080 [US6] **Verify TESTING-GUIDE.md and VST-GUIDE.md are in context** (ingest if needed)

### 7.2 Tests for Delete (Write FIRST - Must FAIL)

- [ ] T081 [P] [US6] Write failing tests for PresetManager::deletePreset() in `tests/unit/preset/preset_manager_test.cpp`
- [ ] T082 [P] [US6] Write failing tests verifying factory presets cannot be deleted

### 7.3 Implementation for Delete

- [ ] T083 [US6] Implement PresetManager::deletePreset() - remove file, return false for factory presets
- [ ] T084 [US6] Implement PresetBrowserView::onDeleteClicked() with confirmation dialog
- [ ] T085 [US6] Disable Delete button when factory preset is selected
- [ ] T086 [US6] Refresh preset list after successful delete
- [ ] T087 [US6] Verify delete tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T088 [US6] **Verify IEEE 754 compliance**: Check new test files, add to `-fno-fast-math` list if needed

### 7.5 Commit (MANDATORY)

- [ ] T089 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Delete works independently

---

## Phase 8: User Story 7 - Browse Factory Presets (Priority: P3)

**Goal**: Factory presets are visible, visually distinct, and protected from modification

**Independent Test**: Verify factory presets appear with visual indicator, cannot be deleted/overwritten

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T090 [US7] **Verify TESTING-GUIDE.md and VST-GUIDE.md are in context** (ingest if needed)

### 8.2 Factory Preset Content Creation

- [ ] T091 [P] [US7] Create 2 factory presets for Granular mode in `resources/presets/Granular/`
- [ ] T092 [P] [US7] Create 2 factory presets for Spectral mode in `resources/presets/Spectral/`
- [ ] T093 [P] [US7] Create 2 factory presets for Shimmer mode in `resources/presets/Shimmer/`
- [ ] T094 [P] [US7] Create 2 factory presets for Tape mode in `resources/presets/Tape/`
- [ ] T095 [P] [US7] Create 2 factory presets for BBD mode in `resources/presets/BBD/`
- [ ] T096 [P] [US7] Create 2 factory presets for Digital mode in `resources/presets/Digital/`
- [ ] T097 [P] [US7] Create 2 factory presets for PingPong mode in `resources/presets/PingPong/`
- [ ] T098 [P] [US7] Create 2 factory presets for Reverse mode in `resources/presets/Reverse/`
- [ ] T099 [P] [US7] Create 2 factory presets for MultiTap mode in `resources/presets/MultiTap/`
- [ ] T100 [P] [US7] Create 2 factory presets for Freeze mode in `resources/presets/Freeze/`
- [ ] T101 [P] [US7] Create 2 factory presets for Ducking mode in `resources/presets/Ducking/`

### 8.3 Visual Distinction Implementation

- [ ] T102 [US7] Modify PresetDataSource::dbDrawCell() to show visual indicator for factory presets (different text color or icon)
- [ ] T103 [US7] Add optional Mode column when "All" filter is selected

### 8.4 Installer Integration

- [ ] T104 [US7] Update CMake to copy `resources/presets/` to factory preset location during install
- [ ] T105 [US7] Verify factory presets appear in browser on fresh install

### 8.5 Commit (MANDATORY)

- [ ] T106 [US7] **Commit completed User Story 7 work**

**Checkpoint**: Factory presets visible and protected

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T107 [P] Add keyboard navigation for preset list (arrow keys, Enter to load)
- [ ] T108 [P] Add error handling for corrupted preset files with user-friendly messages
- [ ] T110 [P] Performance optimization: cache preset list, refresh on demand
- [ ] T111 Run pluginval level 5 with comprehensive preset operations
- [ ] T112 Manual cross-platform verification (test on each available platform):
  - [ ] Windows: Open browser, save preset, load preset, verify paths use Documents/VST3 Presets
  - [ ] macOS: Open browser, save preset, load preset, verify paths use ~/Library/Audio/Presets
  - [ ] Linux: Open browser, save preset, load preset, verify paths use ~/.vst3/presets

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIV**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T115 **Update ARCHITECTURE.md** with new components:
  - Add PresetManager to appropriate section (UI/Controller layer)
  - Add PresetInfo struct documentation
  - Add Platform::PresetPaths utilities
  - Add PresetBrowserView, PresetDataSource, ModeTabBar custom views
  - Include public API summaries and "when to use this"

### 10.2 Documentation Commit

- [ ] T116 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met

> **Constitution Principle XVI**: Claiming "done" when requirements are not met is a violation of trust

### 11.1 Requirements Verification

- [ ] T117 **Verify CI multi-platform builds pass** (Windows, macOS, Linux) - SC-007 compliance
- [ ] T118 **Review ALL FR-001 through FR-021** from spec.md against implementation
- [ ] T119 **Review ALL SC-001 through SC-007** success criteria and verify measurable targets achieved
- [ ] T120 **Search for cheating patterns**:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table

- [ ] T121 **Update spec.md "Implementation Verification" section** with compliance status for each FR and SC
- [ ] T122 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

- [ ] T123 **All self-check questions answered "no"** (or gaps documented honestly):
  1. Did I change ANY test threshold from what the spec required?
  2. Are there ANY "placeholder", "stub", or "TODO" comments?
  3. Did I remove ANY features without telling the user?
  4. Would the spec author consider this "done"?
  5. If I were the user, would I feel cheated?

### 11.4 Final Commit

- [ ] T124 **Commit all remaining spec work**
- [ ] T125 **Verify all tests pass**
- [ ] T126 **Claim completion ONLY if all requirements are MET**

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup) ────┐
                    ├──> Phase 2 (Foundational) ──> All User Stories
Phase 2 blocks all user stories

Phase 3 (US1+2) ──┬──> Phase 4 (US3) ──> Phase 5 (US4)
                  │
                  └──> Phase 6 (US5) ──> Phase 7 (US6) ──> Phase 8 (US7)

All user stories ──> Phase 9 (Polish) ──> Phase 10 (Docs) ──> Phase 11 (Verify)
```

### User Story Dependencies

| Story | Depends On | Can Start After |
|-------|------------|-----------------|
| US1+2 | Foundational (Phase 2) | T015 complete |
| US3 | US1+2 (needs browser view) | T045 complete |
| US4 | US3 (needs data source filtering) | T059 complete |
| US5 | US1+2 (needs PresetManager) | T045 complete |
| US6 | US1+2 (needs PresetManager) | T045 complete |
| US7 | US1+2 (needs browser, data source) | T045 complete |

### Parallel Opportunities per Phase

**Phase 2 (Foundational)**:
- T008 + T012 can run in parallel (different test files)
- T009 + T013 can run in parallel (different source files)

**Phase 3 (US1+2)**:
- T017 + T018 + T019 + T020 can run in parallel (same test file, different test cases)
- T028 can start while T021-T027 are in progress

**Phase 8 (US7 - Factory Presets)**:
- T091 through T101 can ALL run in parallel (different preset files)

---

## Implementation Strategy

### MVP First (User Story 1+2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1+2 (Load + Save)
4. **STOP and VALIDATE**: Test save/load independently
5. Deploy/demo if ready - **this is MVP**

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add US1+2 (Load/Save) → **MVP!**
3. Add US3 (Mode Filter) → Better organization
4. Add US4 (Search) → Large collection support
5. Add US5 (Import) → Community presets
6. Add US6 (Delete) → Maintenance
7. Add US7 (Factory) → First-run experience

---

## Summary

| Metric | Count |
|--------|-------|
| Total Tasks | 124 |
| Setup Tasks | 6 |
| Foundational Tasks | 9 |
| US1+2 Tasks (P1 MVP) | 31 |
| US3 Tasks (P2) | 14 |
| US4 Tasks (P2) | 11 |
| US5 Tasks (P3) | 9 |
| US6 Tasks (P3) | 10 |
| US7 Tasks (P3) | 17 |
| Polish Tasks | 6 |
| Documentation Tasks | 2 |
| Verification Tasks | 10 |

**Parallel Opportunities**: 46 tasks marked [P]
**MVP Scope**: Phase 1-3 (T001-T045a) = 46 tasks
