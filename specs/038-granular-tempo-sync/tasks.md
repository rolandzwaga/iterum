# Tasks: Granular Delay Tempo Sync

**Input**: Design documents from `/specs/038-granular-tempo-sync/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` and `specs/VST-GUIDE.md` are in context
2. **Write Failing Tests**: Create test file and write tests that FAIL
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

---

## Phase 1: Setup (Parameter IDs)

**Purpose**: Add new parameter identifiers for tempo sync controls

- [ ] T001 Add kGranularTimeModeId (113) to src/plugin_ids.h
- [ ] T002 Add kGranularNoteValueId (114) to src/plugin_ids.h

---

## Phase 2: Foundational (Parameter Infrastructure)

**Purpose**: Core parameter storage, handling, and persistence - MUST complete before DSP or UI work

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T003 **Verify TESTING-GUIDE.md and VST-GUIDE.md are in context** (ingest if needed)
- [ ] T004 [P] Add timeMode atomic<int> to GranularParams struct in src/parameters/granular_params.h
- [ ] T005 [P] Add noteValue atomic<int> to GranularParams struct in src/parameters/granular_params.h
- [ ] T006 Add handleGranularParamChange cases for kGranularTimeModeId in src/parameters/granular_params.h
- [ ] T007 Add handleGranularParamChange cases for kGranularNoteValueId in src/parameters/granular_params.h
- [ ] T008 [P] Register TimeMode dropdown parameter using createDropdownParameter in registerGranularParams()
- [ ] T009 [P] Register NoteValue dropdown parameter using createDropdownParameterWithDefault (default index 4 = 1/8 note)
- [ ] T010 Add timeMode and noteValue to saveGranularParams() in src/parameters/granular_params.h
- [ ] T011 Add timeMode and noteValue to loadGranularParams() in src/parameters/granular_params.h
- [ ] T012 Add timeMode and noteValue to syncGranularParamsToController() in src/parameters/granular_params.h
- [ ] T013 **Commit foundational parameter infrastructure**

**Checkpoint**: Parameter infrastructure ready - DSP and UI implementation can now begin

---

## Phase 3: User Story 1 & 2 - Tempo Sync and Free Mode (Priority: P1)

**Goal**: Implement core TimeMode functionality - Synced mode calculates position from tempo, Free mode uses milliseconds directly

**Independent Test**: Set Time Mode to "Synced", select 1/4 note at 120 BPM, verify position is 500ms. Set Time Mode to "Free", set 350ms, verify position is 350ms regardless of tempo.

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T014 [US1] **Verify TESTING-GUIDE.md and VST-GUIDE.md are in context** (ingest if needed)

### 3.2 Tests for User Story 1 & 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T015 [P] [US1] Write DSP unit test: synced mode at 120 BPM with 1/4 note = 500ms in tests/unit/features/granular_delay_tempo_sync_test.cpp
- [ ] T016 [P] [US1] Write DSP unit test: synced mode at 120 BPM with 1/8 note = 250ms
- [ ] T017 [P] [US1] Write DSP unit test: synced mode at 60 BPM with 1/4 note = 1000ms
- [ ] T018 [P] [US2] Write DSP unit test: free mode ignores tempo (350ms stays 350ms at any BPM)
- [ ] T019 [P] [US2] Write DSP unit test: mode switching is smooth (no clicks)
- [ ] T020 [P] [US1] Write DSP unit test: position clamped to max 2000ms
- [ ] T021 [P] [US1] Write DSP unit test: fallback to 120 BPM when tempo unavailable (0 or negative)
- [ ] T022 Register new test file in tests/CMakeLists.txt under dsp_tests target

### 3.3 Implementation for User Story 1 & 2

- [ ] T023 [US1] Add #include "dsp/core/note_value.h" to src/dsp/features/granular_delay.h
- [ ] T024 [US1] Add #include "dsp/systems/delay_engine.h" (for TimeMode enum) to src/dsp/features/granular_delay.h
- [ ] T025 [P] [US1] Add timeMode_ member (TimeMode::Free default) to GranularDelay class
- [ ] T026 [P] [US1] Add noteValueIndex_ member (int, default 4 = 1/8 note) to GranularDelay class
- [ ] T027 [US1] Implement setTimeMode(int mode) method in GranularDelay
- [ ] T028 [US1] Implement setNoteValue(int index) method in GranularDelay
- [ ] T029 [US1] Update process() signature to accept BlockContext parameter for tempo access
- [ ] T030 [US1] Implement tempo-synced position calculation in process() using dropdownToDelayMs()
- [ ] T031 [US1] Add position clamping to 0-2000ms in synced mode
- [ ] T032 [US1] Verify all DSP tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T033 [US1] **Verify IEEE 754 compliance**: Check if test file uses std::isnan/std::isfinite → add to -fno-fast-math list if needed

### 3.5 Commit (MANDATORY)

- [ ] T034 [US1] **Commit completed User Story 1 & 2 DSP work**

**Checkpoint**: Core tempo sync and free mode DSP is functional and tested

---

## Phase 4: User Story 3 - Note Value Selection (Priority: P2)

**Goal**: Verify all 10 note values (1/32 through 1/1, including triplets) produce mathematically correct durations

**Independent Test**: Cycle through all note value options at 120 BPM, verify each produces correct duration per SC-001 (within 0.1ms)

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T035 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T036 [P] [US3] Write DSP unit test: 1/32 note at 120 BPM = 62.5ms
- [ ] T037 [P] [US3] Write DSP unit test: 1/16T triplet at 120 BPM = 83.33ms
- [ ] T038 [P] [US3] Write DSP unit test: 1/16 note at 120 BPM = 125ms
- [ ] T039 [P] [US3] Write DSP unit test: 1/8T triplet at 120 BPM = 166.67ms
- [ ] T040 [P] [US3] Write DSP unit test: 1/4T triplet at 120 BPM = 333.33ms
- [ ] T041 [P] [US3] Write DSP unit test: 1/2T triplet at 120 BPM = 666.67ms
- [ ] T042 [P] [US3] Write DSP unit test: 1/2 note at 120 BPM = 1000ms
- [ ] T043 [P] [US3] Write DSP unit test: 1/1 whole note at 120 BPM = 2000ms
- [ ] T044 [P] [US3] Write DSP unit test: accuracy within 0.1ms across 20-300 BPM range (SC-001)

### 4.3 Implementation for User Story 3

- [ ] T045 [US3] Verify dropdownToDelayMs() handles all 10 note value indices correctly (already implemented in note_value.h)
- [ ] T046 [US3] Verify all note value tests pass

### 4.4 Commit (MANDATORY)

- [ ] T047 [US3] **Commit completed User Story 3 work**

**Checkpoint**: All note values verified mathematically accurate

---

## Phase 5: UI Integration

**Goal**: Add UI controls for TimeMode and NoteValue dropdowns, verify parameter binding

**Independent Test**: Open plugin UI, verify TimeMode and NoteValue dropdowns appear and function correctly

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T048 **Verify VST-GUIDE.md is in context** (ingest if needed)

### 5.2 UI E2E Tests (Write FIRST - Must FAIL)

- [ ] T049 [P] Write UI test: parameter ID 113 (kGranularTimeModeId) is defined in tests/unit/vst/granular_tempo_sync_ui_test.cpp
- [ ] T050 [P] Write UI test: parameter ID 114 (kGranularNoteValueId) is defined
- [ ] T051 [P] Write UI test: TimeMode dropdown has 2 options ("Free", "Synced")
- [ ] T052 [P] Write UI test: NoteValue dropdown has 10 options (1/32 through 1/1)
- [ ] T053 [P] Write UI test: NoteValue default is index 4 (1/8 note)
- [ ] T054 [P] Write UI test: TimeMode default is index 0 (Free)
- [ ] T055 [P] Write UI test: state persistence roundtrip preserves both parameters
- [ ] T056 [P] Write UI test: NoteValue dropdown visible only when TimeMode is Synced (FR-009, SC-004)
- [ ] T057 Register new UI test file in tests/CMakeLists.txt under vst_tests target

### 5.3 UI Implementation

- [ ] T058 Add TimeMode dropdown control to granular panel in resources/editor.uidesc
- [ ] T059 Add NoteValue dropdown control to granular panel in resources/editor.uidesc
- [ ] T060 Configure control-tag bindings for kGranularTimeModeId and kGranularNoteValueId
- [ ] T061 Implement conditional visibility: NoteValue dropdown shown only when TimeMode is Synced (FR-009)
- [ ] T062 Verify all UI E2E tests pass

### 5.4 Commit (MANDATORY)

- [ ] T063 **Commit completed UI integration work**

**Checkpoint**: UI controls functional and tested

---

## Phase 6: Processor Integration

**Goal**: Wire parameter changes to DSP layer in processor

### 6.1 Implementation

- [ ] T064 Add parameter routing for kGranularTimeModeId in processor's parameter change handler
- [ ] T065 Add parameter routing for kGranularNoteValueId in processor's parameter change handler
- [ ] T066 Pass BlockContext to GranularDelay::process() call in processor
- [ ] T067 Verify end-to-end: UI parameter change → DSP behavior change

### 6.2 Commit (MANDATORY)

- [ ] T068 **Commit processor integration work**

---

## Phase 7: Polish & Validation

**Purpose**: Final validation and cross-cutting concerns

- [ ] T069 Run pluginval at strictness level 5 against built plugin
- [ ] T070 Test in DAW: verify tempo sync responds to tempo changes
- [ ] T071 Test in DAW: verify mode switching is click-free (SC-002)
- [ ] T072 Test Position Spray interaction with tempo sync (FR-008, SC-005)

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 8.1 Architecture Documentation Update

- [ ] T073 **Update ARCHITECTURE.md** with tempo sync additions to GranularDelay:
  - Document setTimeMode(), setNoteValue() methods
  - Document BlockContext parameter requirement for process()
  - Reference existing TimeMode/NoteValue from note_value.h and delay_engine.h

### 8.2 Final Commit

- [ ] T074 **Commit ARCHITECTURE.md updates**

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed

### 9.1 Requirements Verification

- [ ] T075 **Review ALL FR-xxx requirements** from spec.md:
  - [ ] FR-001: Time Mode selector with "Free" and "Synced"
  - [ ] FR-002: Note Value selector with 10 options (matching existing delay modes)
  - [ ] FR-003: Synced mode calculates from tempo
  - [ ] FR-004: Free mode uses milliseconds directly
  - [ ] FR-005: Smooth transition between modes
  - [ ] FR-006: Position clamped to 0-2000ms
  - [ ] FR-007: Fallback tempo of 120 BPM
  - [ ] FR-008: Position Spray works with synced mode
  - [ ] FR-009: UI shows appropriate controls per mode (conditional visibility)

- [ ] T076 **Review ALL SC-xxx success criteria**:
  - [ ] SC-001: Position accurate within 0.1ms (20-300 BPM)
  - [ ] SC-002: Mode switching without clicks
  - [ ] SC-003: Free mode identical to existing behavior
  - [ ] SC-004: UI shows correct controls per mode (NoteValue visible only in Synced mode)
  - [ ] SC-005: Position Spray works in both modes

- [ ] T077 **Search for cheating patterns**:
  - [ ] No placeholder or TODO comments in new code
  - [ ] No test thresholds relaxed from spec
  - [ ] No features quietly removed

### 9.2 Fill Compliance Table

- [ ] T078 **Update spec.md Implementation Verification section** with status for each requirement

### 9.3 Final Commit

- [ ] T079 **Commit all spec work to feature branch**
- [ ] T080 **Verify all tests pass** (dsp_tests and vst_tests)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 - BLOCKS all user stories
- **Phase 3 (US1 & US2)**: Depends on Phase 2
- **Phase 4 (US3)**: Depends on Phase 3 (builds on same test file)
- **Phase 5 (UI)**: Depends on Phase 2 (needs parameters registered)
- **Phase 6 (Processor)**: Depends on Phase 3 and Phase 5
- **Phase 7-9 (Polish/Docs/Verification)**: Depends on all prior phases

### User Story Dependencies

- **US1 & US2 (P1)**: Core tempo sync and free mode - can start after Phase 2
- **US3 (P2)**: Note value accuracy - builds on US1/US2 foundation

### Parallel Opportunities

Within Phase 2:
- T004, T005 (add atomics) - parallel
- T008, T009 (register parameters) - parallel

Within Phase 3:
- T015-T021 (all DSP tests) - parallel
- T025, T026 (add members) - parallel

Within Phase 4:
- T036-T044 (all note value tests) - parallel

Within Phase 5:
- T049-T055 (all UI tests) - parallel

---

## Implementation Strategy

### MVP First (User Story 1 & 2)

1. Complete Phase 1: Setup (parameter IDs)
2. Complete Phase 2: Foundational (parameter infrastructure)
3. Complete Phase 3: User Story 1 & 2 (core tempo sync)
4. **STOP and VALIDATE**: Test DSP layer independently
5. Plugin works with TimeMode and NoteValue parameters

### Incremental Delivery

1. Setup + Foundational → Parameters ready
2. Add US1 & US2 → Core tempo sync works (MVP!)
3. Add US3 → All note values verified
4. Add UI → Full user experience
5. Processor integration → End-to-end functional
6. Validation → Production ready

---

## Summary

| Metric | Count |
|--------|-------|
| Total tasks | 80 |
| Phase 1 (Setup) | 2 |
| Phase 2 (Foundational) | 11 |
| Phase 3 (US1 & US2) | 21 |
| Phase 4 (US3) | 13 |
| Phase 5 (UI) | 16 |
| Phase 6 (Processor) | 5 |
| Phase 7-9 (Polish/Docs/Verify) | 12 |
| Parallel opportunities | 25+ tasks can run in parallel |

**MVP Scope**: Phases 1-3 (34 tasks) - Core tempo sync functional and tested
