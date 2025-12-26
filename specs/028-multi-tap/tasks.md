# Tasks: Multi-Tap Delay Mode

**Input**: Design documents from `/specs/028-multi-tap/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Project initialization and test file structure

- [ ] T001 Create test file structure in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T002 Add multi_tap_delay_test.cpp to tests/CMakeLists.txt

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T003 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T004 Write tests for TimingPattern enum in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T005 Write tests for SpatialPattern enum in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T006 Write tests for TapConfiguration struct in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T007 Create multi_tap_delay.h header with TimingPattern enum (20 values) in src/dsp/features/multi_tap_delay.h
- [ ] T008 Add SpatialPattern enum (7 values: Cascade, Alternating, Centered, WideningStereo, DecayingLevel, FlatLevel, Custom) to src/dsp/features/multi_tap_delay.h
- [ ] T009 Add TapConfiguration struct to src/dsp/features/multi_tap_delay.h
- [ ] T010 Create MultiTapDelay class skeleton with prepare/reset/process in src/dsp/features/multi_tap_delay.h
- [ ] T011 Verify foundational tests pass
- [ ] T012 **Commit foundational structure**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Multi-Tap Rhythmic Delay (Priority: P1) 🎯 MVP

**Goal**: Load timing and spatial patterns, process audio with multiple taps at correct intervals

**Independent Test**: Load a pattern, process impulse, verify taps at expected timing intervals with correct levels

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T013 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T014 [P] [US1] Write tests for loadTimingPattern() with rhythmic patterns (QuarterNote, DottedEighth, Triplet) in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T015 [P] [US1] Write tests for loadTimingPattern() with mathematical patterns (GoldenRatio, Fibonacci, Exponential, PrimeNumbers, LinearSpread) in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T016 [P] [US1] Write tests for loadSpatialPattern() with all 6 spatial patterns (Cascade, Alternating, Centered, WideningStereo, DecayingLevel, FlatLevel) in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T017 [P] [US1] Write tests for process() producing delays at correct intervals in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T018 [P] [US1] Write tests for combined timing + spatial patterns in tests/unit/features/multi_tap_delay_test.cpp

### 3.3 Implementation for User Story 1

- [ ] T019 [US1] Implement loadTimingPattern() - delegate to TapManager for existing patterns in src/dsp/features/multi_tap_delay.h
- [ ] T020 [US1] Implement applyExponentialPattern() for Exponential timing in src/dsp/features/multi_tap_delay.h
- [ ] T021 [US1] Implement applyPrimeNumbersPattern() for PrimeNumbers timing in src/dsp/features/multi_tap_delay.h
- [ ] T022 [US1] Implement applyLinearSpreadPattern() for LinearSpread timing in src/dsp/features/multi_tap_delay.h
- [ ] T023 [US1] Implement loadSpatialPattern() with all 6 patterns in src/dsp/features/multi_tap_delay.h
- [ ] T024 [US1] Implement process() composing TapManager output in src/dsp/features/multi_tap_delay.h
- [ ] T025 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T026 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 3.5 Commit (MANDATORY)

- [ ] T027 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic multi-tap delay with patterns fully functional

---

## Phase 4: User Story 6 - Tempo Sync with Note Values (Priority: P2)

**Goal**: Enable tempo synchronization so patterns align to musical time divisions

**Independent Test**: Set tempo and note value, verify delay time matches expected ms for BPM

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T028 [US6] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T029 [P] [US6] Write tests for setTempo() and time calculation in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T030 [P] [US6] Write tests for setBaseNoteValue() with NoteValue + NoteModifier in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T031 [P] [US6] Write tests for tempo change updating all synced tap times in tests/unit/features/multi_tap_delay_test.cpp

### 4.3 Implementation for User Story 6

- [ ] T032 [US6] Implement setTempo() delegating to TapManager in src/dsp/features/multi_tap_delay.h
- [ ] T033 [US6] Implement setBaseNoteValue() for tempo-synced patterns in src/dsp/features/multi_tap_delay.h
- [ ] T034 [US6] Implement setBaseTimeMs() for free-running mode in src/dsp/features/multi_tap_delay.h
- [ ] T035 [US6] Verify all US6 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T036 [US6] **Verify IEEE 754 compliance** in tests/CMakeLists.txt

### 4.5 Commit (MANDATORY)

- [ ] T037 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Tempo sync fully functional

---

## Phase 5: User Story 2 - Per-Tap Level and Pan Control (Priority: P2)

**Goal**: Expose per-tap configuration API for independent level and pan control

**Independent Test**: Set different level/pan per tap, verify stereo output matches expectations

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T038 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T039 [P] [US2] Write tests for setTapConfiguration() in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T040 [P] [US2] Write tests for getTapConfiguration() in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T041 [P] [US2] Write tests for per-tap pan producing correct stereo output in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T042 [P] [US2] Write tests for per-tap level accuracy (SC-002: +/- 0.1dB) in tests/unit/features/multi_tap_delay_test.cpp

### 5.3 Implementation for User Story 2

- [ ] T043 [US2] Implement setTapConfiguration() delegating to TapManager in src/dsp/features/multi_tap_delay.h
- [ ] T044 [US2] Implement getTapConfiguration() reading from TapManager in src/dsp/features/multi_tap_delay.h
- [ ] T045 [US2] Verify all US2 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T046 [US2] **Verify IEEE 754 compliance** in tests/CMakeLists.txt

### 5.5 Commit (MANDATORY)

- [ ] T047 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Per-tap control fully functional

---

## Phase 6: User Story 3 - Master Feedback with Filtering (Priority: P2)

**Goal**: Integrate FeedbackNetwork for master feedback with filtering

**Independent Test**: Set feedback > 0, play impulse, verify multiple delay generations with progressive filtering

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T048 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T049 [P] [US3] Write tests for setMasterFeedback() range 0-110% in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T050 [P] [US3] Write tests for feedback creating multiple generations in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T051 [P] [US3] Write tests for setFeedbackLowpassCutoff() and setFeedbackHighpassCutoff() in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T052 [P] [US3] Write tests for feedback stability (SC-006: 110% feedback < +6dBFS) in tests/unit/features/multi_tap_delay_test.cpp

### 6.3 Implementation for User Story 3

- [ ] T053 [US3] Implement setMasterFeedback() configuring FeedbackNetwork in src/dsp/features/multi_tap_delay.h
- [ ] T054 [US3] Implement setFeedbackLowpassCutoff() and setFeedbackHighpassCutoff() in src/dsp/features/multi_tap_delay.h
- [ ] T055 [US3] Integrate FeedbackNetwork into process() loop in src/dsp/features/multi_tap_delay.h
- [ ] T056 [US3] Verify all US3 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T057 [US3] **Verify IEEE 754 compliance** in tests/CMakeLists.txt

### 6.5 Commit (MANDATORY)

- [ ] T058 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Master feedback fully functional

---

## Phase 7: User Story 4 - Pattern Morphing Between Presets (Priority: P3)

**Goal**: Smooth transitions between patterns during processing

**Independent Test**: Trigger morph, measure intermediate tap positions, verify smooth interpolation

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T059 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T060 [P] [US4] Write tests for morphToTimingPattern() starting morph state in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T061 [P] [US4] Write tests for isMorphing() and getMorphProgress() in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T062 [P] [US4] Write tests for smooth interpolation during morph (SC-008) in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T063 [P] [US4] Write tests for morphToSpatialPattern() in tests/unit/features/multi_tap_delay_test.cpp

### 7.3 Implementation for User Story 4

- [ ] T064 [US4] Add MorphState struct to track morph progress in src/dsp/features/multi_tap_delay.h
- [ ] T065 [US4] Implement morphToTimingPattern() capturing from/to values in src/dsp/features/multi_tap_delay.h
- [ ] T066 [US4] Implement morphToSpatialPattern() in src/dsp/features/multi_tap_delay.h
- [ ] T067 [US4] Implement updateMorph() called from process() in src/dsp/features/multi_tap_delay.h
- [ ] T068 [US4] Implement isMorphing() and getMorphProgress() query methods in src/dsp/features/multi_tap_delay.h
- [ ] T069 [US4] Verify all US4 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T070 [US4] **Verify IEEE 754 compliance** in tests/CMakeLists.txt

### 7.5 Commit (MANDATORY)

- [ ] T071 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Pattern morphing fully functional

---

## Phase 8: User Story 5 - Per-Tap Modulation (Priority: P3)

**Goal**: Integrate ModulationMatrix for per-tap parameter modulation

**Independent Test**: Route LFO to specific tap's time parameter, verify only that tap is modulated

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T072 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 8.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T073 [P] [US5] Write tests for setModulationMatrix() accepting external pointer in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T074 [P] [US5] Write tests for registerModulationDestinations() creating 64 destinations in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T075 [P] [US5] Write tests for per-tap modulation being applied additively in tests/unit/features/multi_tap_delay_test.cpp

### 8.3 Implementation for User Story 5

- [ ] T076 [US5] Implement setModulationMatrix() storing external pointer in src/dsp/features/multi_tap_delay.h
- [ ] T077 [US5] Implement registerModulationDestinations() registering 64 destinations in src/dsp/features/multi_tap_delay.h
- [ ] T078 [US5] Implement applyModulation() called from process() in src/dsp/features/multi_tap_delay.h
- [ ] T079 [US5] Verify all US5 tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T080 [US5] **Verify IEEE 754 compliance** in tests/CMakeLists.txt

### 8.5 Commit (MANDATORY)

- [ ] T081 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Per-tap modulation fully functional

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Output controls, edge cases, custom patterns, and cleanup

- [ ] T082 [P] Write tests for setDryWetMix() and setOutputLevel() in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T083 [P] Write tests for edge cases in tests/unit/features/multi_tap_delay_test.cpp:
  - Single tap (tap count = 1) functions as single-tap delay
  - All taps muted produces dry signal only (mix < 100%) or silence (mix = 100%)
  - Maximum feedback (110%) remains stable
  - Pattern scaling when tap count differs from pattern default
- [ ] T083a [P] Write tests for custom user-defined patterns (FR-003) in tests/unit/features/multi_tap_delay_test.cpp
- [ ] T083b Implement setCustomTimingPattern() for user-defined tap time arrays in src/dsp/features/multi_tap_delay.h
- [ ] T084 Implement setDryWetMix() (0-100%) in src/dsp/features/multi_tap_delay.h
- [ ] T085 Implement setOutputLevel() (+/- 12dB) in src/dsp/features/multi_tap_delay.h
- [ ] T086 Implement reset() clearing all buffers in src/dsp/features/multi_tap_delay.h
- [ ] T087 Verify all edge case tests pass
- [ ] T088 Run quickstart.md validation scenarios
- [ ] T089 **Commit polish work**

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T090 **Update ARCHITECTURE.md** with MultiTapDelay component:
  - Add to Layer 4 (User Features) section
  - Include: purpose, public API summary, file location
  - Add usage example
  - Document composed components (TapManager, FeedbackNetwork, ModulationMatrix)

### 10.2 Final Commit

- [ ] T091 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T092 **Review ALL FR-xxx requirements** from spec.md (FR-001 to FR-030) against implementation
- [ ] T093 **Review ALL SC-xxx success criteria** (SC-001 to SC-009) and verify measurable targets are achieved
- [ ] T094 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T095 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T096 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T097 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

- [ ] T098 **Commit all spec work** to feature branch
- [ ] T099 **Verify all tests pass**
- [ ] T100 **Claim completion ONLY if all requirements are MET**

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Stories (Phases 3-8)**: All depend on Foundational completion
- **Polish (Phase 9)**: Depends on all user stories complete
- **Documentation (Phase 10)**: Depends on Polish
- **Verification (Phase 11-12)**: Final phases

### User Story Dependencies

- **US1 (P1)**: Can start after Foundational - No dependencies
- **US6 (P2)**: Can start after Foundational - May use patterns from US1
- **US2 (P2)**: Can start after Foundational - Independent of other stories
- **US3 (P2)**: Can start after Foundational - Adds FeedbackNetwork
- **US4 (P3)**: Should complete after US1 (needs patterns to morph between)
- **US5 (P3)**: Should complete after US2 (needs per-tap params to modulate)

### Parallel Opportunities

**Within User Story 1:**
- T014, T015, T016, T017, T018 (all tests) can run in parallel
- After tests: T019-T024 (implementation) are sequential

**Across User Stories (after Foundational):**
- US1, US6, US2, US3 can potentially run in parallel
- US4 depends on US1 patterns
- US5 depends on US2 per-tap API

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (enums, struct, class skeleton)
3. Complete Phase 3: User Story 1 (basic patterns + processing)
4. **STOP and VALIDATE**: Test loading patterns and hearing multi-tap delays
5. This is a working multi-tap delay!

### Incremental Delivery

1. Setup + Foundational → Core structure ready
2. Add US1 → Basic multi-tap patterns work (MVP!)
3. Add US6 → Tempo sync works
4. Add US2 → Per-tap control works
5. Add US3 → Master feedback works
6. Add US4 → Pattern morphing works
7. Add US5 → Per-tap modulation works
8. Each story adds value without breaking previous stories

---

## Summary

| Phase | User Story | Tasks | Priority |
|-------|------------|-------|----------|
| 1 | Setup | T001-T002 | - |
| 2 | Foundational | T003-T012 | - |
| 3 | US1: Basic Patterns | T013-T027 | P1 (MVP) |
| 4 | US6: Tempo Sync | T028-T037 | P2 |
| 5 | US2: Per-Tap Control | T038-T047 | P2 |
| 6 | US3: Master Feedback | T048-T058 | P2 |
| 7 | US4: Pattern Morphing | T059-T071 | P3 |
| 8 | US5: Per-Tap Modulation | T072-T081 | P3 |
| 9 | Polish | T082-T089 (incl. T083a-b) | - |
| 10 | Documentation | T090-T091 | - |
| 11 | Verification | T092-T097 | - |
| 12 | Completion | T098-T100 | - |

**Total Tasks**: 102
**MVP Scope**: Phases 1-3 (27 tasks)
