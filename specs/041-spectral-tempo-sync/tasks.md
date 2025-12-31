# Tasks: Spectral Delay Tempo Sync

**Input**: Design documents from `/specs/041-spectral-tempo-sync/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` and `specs/VST-GUIDE.md` are in context window. If not, READ THEM FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Verify prerequisites and load context

- [x] T001 Verify on branch `041-spectral-tempo-sync`
- [x] T002 Verify TESTING-GUIDE.md is in context (ingest `specs/TESTING-GUIDE.md` if needed)
- [x] T003 Verify VST-GUIDE.md is in context (ingest `specs/VST-GUIDE.md` if needed)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Add parameter IDs that all user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

- [x] T004 Add `kSpectralTimeModeId = 211` to `src/plugin_ids.h`
- [x] T005 [P] Add `kSpectralNoteValueId = 212` to `src/plugin_ids.h`
- [x] T006 Build to verify no compilation errors

**Checkpoint**: Parameter IDs ready - user story implementation can now begin

---

## Phase 3: User Story 1+2 - Tempo Sync DSP (Priority: P1)

**Goal**: SpectralDelay calculates base delay from tempo when Synced, uses ms when Free

**Independent Test**: Set Time Mode to "Synced", verify delay matches note value at known tempo. Set to "Free", verify delay matches ms value.

**Requirements Covered**: FR-001, FR-003, FR-004, FR-005, FR-006, FR-007, SC-001, SC-002, SC-003

### 3.1 Tests for User Story 1+2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T007 [US1] Write failing test: synced mode at 120 BPM with 1/4 note = 500ms in `tests/unit/features/spectral_delay_test.cpp`
- [ ] T008 [P] [US1] Write failing test: synced mode at 120 BPM with 1/8 note = 250ms in `tests/unit/features/spectral_delay_test.cpp`
- [ ] T009 [P] [US2] Write failing test: free mode uses setBaseDelayMs() value in `tests/unit/features/spectral_delay_test.cpp`
- [ ] T010 [P] [US1] Write failing test: fallback to 120 BPM when tempo is 0 in `tests/unit/features/spectral_delay_test.cpp`
- [ ] T011 [P] [US1] Write failing test: delay clamped to 2000ms maximum in `tests/unit/features/spectral_delay_test.cpp`
- [ ] T012 [US1] Build tests to verify they compile but FAIL

### 3.2 Implementation for User Story 1+2

- [ ] T013 [US1] Add `#include "dsp/systems/delay_engine.h"` for TimeMode enum to `src/dsp/features/spectral_delay.h`
- [ ] T014 [US1] Add `TimeMode timeMode_ = TimeMode::Free` member to SpectralDelay class in `src/dsp/features/spectral_delay.h`
- [ ] T015 [P] [US1] Add `int noteValueIndex_ = 4` member (default 1/8 note) to SpectralDelay class in `src/dsp/features/spectral_delay.h`
- [ ] T016 [US1] Add `setTimeMode(int mode)` method to SpectralDelay class in `src/dsp/features/spectral_delay.h`
- [ ] T017 [P] [US1] Add `setNoteValue(int index)` method to SpectralDelay class in `src/dsp/features/spectral_delay.h`
- [ ] T018 [US1] Modify `process()` to calculate base delay from tempo when timeMode_ == Synced in `src/dsp/features/spectral_delay.h`
- [ ] T019 [US1] Verify all tests pass
- [ ] T020 [US1] Build and run tests: `cmake --build build --config Debug && ctest --test-dir build -C Debug --output-on-failure`

### 3.3 Commit (MANDATORY)

- [ ] T021 [US1] **Commit completed DSP layer work**: "feat(spectral): add tempo sync to SpectralDelay DSP"

**Checkpoint**: SpectralDelay tempo sync works at DSP level

---

## Phase 4: Parameter Layer (User Story 1+2 continued)

**Goal**: Add Time Mode and Note Value parameters to spectral_params.h with full VST3 integration

**Requirements Covered**: FR-001, FR-002, FR-011, SC-006

### 4.1 Implementation for Parameter Layer

- [ ] T022 [US1] Add `std::atomic<int> timeMode{0}` to SpectralParams struct in `src/parameters/spectral_params.h`
- [ ] T023 [P] [US1] Add `std::atomic<int> noteValue{4}` to SpectralParams struct in `src/parameters/spectral_params.h`
- [ ] T024 [US1] Add handleSpectralParamChange case for kSpectralTimeModeId in `src/parameters/spectral_params.h`
- [ ] T025 [P] [US1] Add handleSpectralParamChange case for kSpectralNoteValueId in `src/parameters/spectral_params.h`
- [ ] T026 [US1] Add registerSpectralParams entry for Time Mode dropdown (Free, Synced) in `src/parameters/spectral_params.h`
- [ ] T027 [P] [US1] Add registerSpectralParams entry for Note Value dropdown (10 options) in `src/parameters/spectral_params.h`
- [ ] T028 [US1] Add saveSpectralParams entries for timeMode and noteValue in `src/parameters/spectral_params.h`
- [ ] T029 [P] [US1] Add loadSpectralParams entries for timeMode and noteValue in `src/parameters/spectral_params.h`
- [ ] T030 [US1] Add syncSpectralParamsToController entries for timeMode and noteValue in `src/parameters/spectral_params.h`

### 4.2 Processor Connection

- [ ] T031 [US1] Add spectralDelay_.setTimeMode() call in Spectral mode parameter handling in `src/processor/processor.cpp`
- [ ] T032 [P] [US1] Add spectralDelay_.setNoteValue() call in Spectral mode parameter handling in `src/processor/processor.cpp`
- [ ] T033 [US1] Build and verify no compilation errors
- [ ] T034 [US1] Run pluginval to verify parameter changes don't break plugin: `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"`

### 4.3 Commit (MANDATORY)

- [ ] T035 [US1] **Commit completed parameter layer work**: "feat(spectral): add tempo sync parameters"

**Checkpoint**: Parameters registered and connected to DSP

---

## Phase 5: User Story 3 - Conditional UI Visibility (Priority: P1)

**Goal**: Base Delay control hidden when Synced, Note Value dropdown hidden when Free

**Independent Test**: Toggle Time Mode and observe Base Delay label/slider appear/disappear while Note Value dropdown has opposite behavior

**Requirements Covered**: FR-008, FR-009, SC-004

### 5.1 Implementation for UI Layer

- [ ] T036 [US3] Add control-tag `SpectralTimeMode` (211) to `resources/editor.uidesc`
- [ ] T037 [P] [US3] Add control-tag `SpectralNoteValue` (212) to `resources/editor.uidesc`
- [ ] T038 [P] [US3] Add control-tag `SpectralBaseDelayLabel` (9912) to `resources/editor.uidesc`
- [ ] T039 [US3] Add control-tag to Base Delay label in SpectralPanel in `resources/editor.uidesc`
- [ ] T040 [P] [US3] Add Time Mode COptionMenu control to SpectralPanel in `resources/editor.uidesc`
- [ ] T041 [P] [US3] Add Note Value COptionMenu control to SpectralPanel in `resources/editor.uidesc`
- [ ] T042 [US3] Add `spectralBaseDelayVisibilityController_` member to `src/controller/controller.h`
- [ ] T043 [US3] Create VisibilityController for spectral base delay in didOpen() in `src/controller/controller.cpp`
- [ ] T044 [P] [US3] Null out spectralBaseDelayVisibilityController_ in willClose() in `src/controller/controller.cpp`
- [ ] T045 [US3] Verify Note Value dropdown hidden when Time Mode is Free (FR-009 explicit test)
- [ ] T046 [US3] Build and test UI in DAW
- [ ] T047 [US3] Run pluginval to verify UI changes don't break plugin

### 5.2 Commit (MANDATORY)

- [ ] T048 [US3] **Commit completed UI layer work**: "feat(spectral): add tempo sync UI with visibility controller"

**Checkpoint**: UI visibility toggles correctly

---

## Phase 6: User Story 4 - Note Value Selection (Priority: P2)

**Goal**: All 10 note values produce mathematically correct durations

**Independent Test**: Cycle through all note value options and verify each produces correct duration at 120 BPM

**Requirements Covered**: FR-002, SC-001

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T049 [US4] Write test: all 10 note values produce correct delay at 120 BPM in `tests/unit/features/spectral_delay_test.cpp`
- [ ] T050 [P] [US4] Write test: 1/8T triplet at 120 BPM = ~166.67ms in `tests/unit/features/spectral_delay_test.cpp`
- [ ] T051 [P] [US4] Write test: 1/2T triplet at 120 BPM = ~666.67ms in `tests/unit/features/spectral_delay_test.cpp`

### 6.2 Verification

- [ ] T052 [US4] Run tests to verify all note values work: `ctest --test-dir build -C Debug --output-on-failure`
- [ ] T053 [US4] Verify Spread parameter works with both Free and Synced modes (FR-010, SC-005)

### 6.3 Commit (MANDATORY)

- [ ] T054 [US4] **Commit completed note value tests**: "test(spectral): add comprehensive note value tests"

**Checkpoint**: All note values verified

---

## Phase 7: Polish & Validation

**Purpose**: Final validation across all user stories

- [ ] T055 Build Release configuration: `cmake --build build --config Release`
- [ ] T056 Run all tests: `ctest --test-dir build -C Release --output-on-failure`
- [ ] T057 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"`
- [ ] T058 Manual testing in DAW: verify all acceptance scenarios from spec.md
- [ ] T059 [P] Verify state persistence: save preset, reload, verify Time Mode and Note Value restored

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIV**: Every spec implementation MUST update ARCHITECTURE.md as a final task

- [ ] T060 **Update ARCHITECTURE.md** with tempo sync addition to SpectralDelay:
  - Document new setTimeMode() and setNoteValue() methods
  - Note that SpectralDelay now supports TimeMode::Synced like other delay modes

- [ ] T061 **Commit ARCHITECTURE.md updates**

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed

### 9.1 Requirements Verification

- [ ] T062 **Review ALL FR-xxx requirements** from spec.md against implementation
- [ ] T063 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved
- [ ] T064 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [ ] T065 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T066 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Final Commit

- [ ] T067 **Commit all spec work** to feature branch
- [ ] T068 **Verify all tests pass**
- [ ] T069 **Claim completion ONLY if all requirements are MET**

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **DSP Layer (Phase 3)**: Depends on Foundational
- **Parameter Layer (Phase 4)**: Depends on Phase 3 (uses DSP methods)
- **UI Layer (Phase 5)**: Depends on Phase 4 (uses parameter IDs)
- **Note Values (Phase 6)**: Depends on Phase 3 (DSP must work first)
- **Polish (Phase 7)**: Depends on Phases 3-6
- **Documentation (Phase 8)**: Depends on Phase 7
- **Completion (Phase 9)**: Depends on Phase 8

### User Story Dependencies

- **US1+US2 (P1)**: Core tempo sync - can start after Foundational
- **US3 (P1)**: UI visibility - requires parameters from US1+US2
- **US4 (P2)**: Note value verification - requires DSP from US1

### Parallel Opportunities

- T004, T005 can run in parallel (different IDs)
- T007-T011 test writing can run in parallel (same file, different sections)
- T014, T015 member additions can run in parallel
- T016, T017 method additions can run in parallel
- T036-T041 UI control-tags can run in parallel
- T049-T051 note value tests can run in parallel

---

## Parallel Example: Phase 3 Tests

```bash
# Launch all tests in parallel (different test cases):
Task: "Write failing test: synced mode at 120 BPM with 1/4 note = 500ms"
Task: "Write failing test: synced mode at 120 BPM with 1/8 note = 250ms"
Task: "Write failing test: free mode uses setBaseDelayMs() value"
```

---

## Implementation Strategy

### MVP First (User Stories 1+2+3)

1. Complete Phase 1: Setup (context verification)
2. Complete Phase 2: Foundational (parameter IDs)
3. Complete Phase 3: DSP Layer (tempo sync working)
4. Complete Phase 4: Parameter Layer (VST3 integration)
5. Complete Phase 5: UI Layer (visibility toggles)
6. **STOP and VALIDATE**: Test core tempo sync independently
7. Deploy/demo if ready

### Incremental Delivery

1. Setup + Foundational → Infrastructure ready
2. Add DSP Layer → Tempo sync works at code level
3. Add Parameter Layer → Plugin responds to parameter changes
4. Add UI Layer → Users can interact with tempo sync
5. Add Note Value Tests → Comprehensive verification
6. Each phase adds value without breaking previous work

---

## Notes

- All existing components reused (TimeMode, dropdownToDelayMs, VisibilityController)
- Pattern follows granular_delay.h exactly (4th implementation of same pattern)
- No new classes or utilities needed - low ODR risk
- Tests use Catch2 framework (existing test infrastructure)
- pluginval validation required after each major phase
- **MANDATORY**: Commit work at end of each phase
- **MANDATORY**: Update ARCHITECTURE.md before completion
- **MANDATORY**: Fill compliance table in spec.md honestly
