# Tasks: Ping-Pong Delay Mode

**Input**: Design documents from `/specs/027-ping-pong-delay/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create file structure and header skeleton

- [x] T001 Create test file skeleton in tests/unit/features/ping_pong_delay_test.cpp
- [x] T002 Create header file skeleton with includes in src/dsp/features/ping_pong_delay.h
- [x] T003 Add ping_pong_delay.h to CMakeLists.txt source list

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [x] T004 Define LRRatio enum in src/dsp/features/ping_pong_delay.h (OneToOne, TwoToOne, ThreeToTwo, FourToThree, OneToTwo, TwoToThree, ThreeToFour)
- [x] T005 Define PingPongDelay class skeleton with member variables per data-model.md in src/dsp/features/ping_pong_delay.h
- [x] T006 Implement prepare() and reset() lifecycle methods in src/dsp/features/ping_pong_delay.h
- [x] T007 Implement getRatioMultipliers() helper function for LRRatio in src/dsp/features/ping_pong_delay.h
- [x] T008 Write foundational tests for lifecycle and enum in tests/unit/features/ping_pong_delay_test.cpp
- [x] T009 Verify foundational tests pass
- [x] T010 Commit foundational infrastructure

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Classic Ping-Pong Delay (Priority: P1) MVP

**Goal**: Alternating L/R delays with 1:1 ratio, feedback, and mix control

**Independent Test**: Process mono impulse and verify alternating L/R repeats at correct timing

**Requirements Covered**: FR-001, FR-004, FR-011, FR-013, FR-024, FR-027, SC-001, SC-007

### 3.1 Pre-Implementation (MANDATORY)

- [x] T011 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T012 [US1] Write test for alternating L/R pattern with 1:1 ratio in tests/unit/features/ping_pong_delay_test.cpp
- [x] T013 [US1] Write test for feedback decay (~6dB per repeat at 50%) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T014 [US1] Write test for dry/wet mix control in tests/unit/features/ping_pong_delay_test.cpp
- [x] T015 [US1] Write test for 100% wet outputs only delayed signal in tests/unit/features/ping_pong_delay_test.cpp

### 3.3 Implementation for User Story 1

- [x] T016 [US1] Implement setDelayTimeMs(), setFeedback(), setCrossFeedback(1.0), setMix() setters in src/dsp/features/ping_pong_delay.h
- [x] T017 [US1] Implement process() stereo method with basic ping-pong routing in src/dsp/features/ping_pong_delay.h
- [x] T018 [US1] Integrate DelayLine (x2) for L/R channels in process() in src/dsp/features/ping_pong_delay.h
- [x] T019 [US1] Implement stereoCrossBlend for cross-feedback in process() in src/dsp/features/ping_pong_delay.h
- [x] T020 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [x] T021 [US1] **Verify IEEE 754 compliance**: Check if test file uses std::isnan/isfinite/isinf - add to -fno-fast-math list in tests/CMakeLists.txt if needed

### 3.5 Commit (MANDATORY)

- [x] T022 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Classic ping-pong (MVP) is fully functional and tested

---

## Phase 4: User Story 2 - Asymmetric Stereo Timing (Priority: P2)

**Goal**: L/R timing ratios (2:1, 3:2, etc.) for polyrhythmic delays

**Independent Test**: Verify delay times match specified ratios within 1% tolerance

**Requirements Covered**: FR-005, FR-006, FR-007, FR-008, SC-002

### 4.1 Pre-Implementation (MANDATORY)

- [x] T023 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [x] T024 [US2] Write test for 2:1 ratio timing (L=500ms, R=250ms at base 500ms) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T025 [US2] Write test for 3:2 ratio timing (L=600ms, R=400ms at base 600ms) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T026 [US2] Write test for inverse ratios (1:2, 2:3, 3:4) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T027 [US2] Write test for ratio accuracy within 1% tolerance (SC-002) in tests/unit/features/ping_pong_delay_test.cpp

### 4.3 Implementation for User Story 2

- [x] T028 [US2] Implement setLRRatio() setter in src/dsp/features/ping_pong_delay.h
- [x] T029 [US2] Update process() to calculate L/R delay times from ratio in src/dsp/features/ping_pong_delay.h
- [x] T030 [US2] Verify all US2 tests pass

### 4.4 Commit (MANDATORY)

- [x] T031 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Asymmetric ratios working with all 7 ratio presets

---

## Phase 5: User Story 3 - Tempo-Synced Ping-Pong (Priority: P2)

**Goal**: Lock delay times to host tempo with note values and modifiers

**Independent Test**: Change tempo and verify delay times update correctly

**Requirements Covered**: FR-002, FR-003, SC-003

### 5.1 Pre-Implementation (MANDATORY)

- [x] T032 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [x] T033 [US3] Write test for quarter note at 120 BPM = 500ms in tests/unit/features/ping_pong_delay_test.cpp
- [x] T034 [US3] Write test for dotted eighth with 2:1 ratio at 120 BPM in tests/unit/features/ping_pong_delay_test.cpp
- [x] T035 [US3] Write test for tempo sync accuracy within 1 sample (SC-003) in tests/unit/features/ping_pong_delay_test.cpp

### 5.3 Implementation for User Story 3

- [x] T036 [US3] Implement setTimeMode(), setNoteValue() setters in src/dsp/features/ping_pong_delay.h
- [x] T037 [US3] Update process() to use BlockContext::tempoToSamples() for synced mode in src/dsp/features/ping_pong_delay.h
- [x] T038 [US3] Verify all US3 tests pass

### 5.4 Commit (MANDATORY)

- [x] T039 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Tempo sync working with all note values and modifiers

---

## Phase 6: User Story 4 - Stereo Width Control (Priority: P3)

**Goal**: Width control from 0% (mono) to 200% (ultra-wide) using M/S technique

**Independent Test**: Measure correlation coefficient at width 0%, 100%, 200%

**Requirements Covered**: FR-014, FR-015, FR-016, FR-017, FR-018, SC-004, SC-005

### 6.1 Pre-Implementation (MANDATORY)

- [x] T040 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [x] T041 [US4] Write test for width 0% = mono (correlation >0.99) (SC-004) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T042 [US4] Write test for width 100% = natural stereo in tests/unit/features/ping_pong_delay_test.cpp
- [x] T043 [US4] Write test for width 200% = wide (correlation <0.5) (SC-005) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T044 [US4] Write test for width consistency across feedback iterations in tests/unit/features/ping_pong_delay_test.cpp

### 6.3 Implementation for User Story 4

- [x] T045 [US4] Implement setWidth() setter in src/dsp/features/ping_pong_delay.h
- [x] T046 [US4] Implement M/S width processing in process() in src/dsp/features/ping_pong_delay.h
- [x] T047 [US4] Verify all US4 tests pass

### 6.4 Commit (MANDATORY)

- [x] T048 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Stereo width control working from mono to ultra-wide

---

## Phase 7: User Story 5 - Cross-Feedback Control (Priority: P3)

**Goal**: Adjustable cross-feedback from 0% (dual mono) to 100% (full ping-pong)

**Independent Test**: Measure channel isolation at 0% cross-feedback

**Requirements Covered**: FR-009, FR-010, FR-012, SC-006, SC-009

### 7.1 Pre-Implementation (MANDATORY)

- [x] T049 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [x] T050 [US5] Write test for 0% cross-feedback = channel isolation >60dB (SC-006) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T051 [US5] Write test for 50% cross-feedback = hybrid pattern in tests/unit/features/ping_pong_delay_test.cpp
- [x] T052 [US5] Write test for feedback 120% with limiter = stable output (SC-009) in tests/unit/features/ping_pong_delay_test.cpp

### 7.3 Implementation for User Story 5

- [x] T053 [US5] Update setCrossFeedback() to accept variable amounts 0-100% in src/dsp/features/ping_pong_delay.h
- [x] T054 [US5] Integrate DynamicsProcessor for feedback limiting in src/dsp/features/ping_pong_delay.h
- [x] T055 [US5] Verify all US5 tests pass

### 7.4 Commit (MANDATORY)

- [x] T056 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Cross-feedback and limiting working for creative control

---

## Phase 8: User Story 6 - Modulated Ping-Pong (Priority: P4)

**Goal**: Optional LFO modulation of delay time with independent L/R phase offset

**Independent Test**: Measure pitch deviation with modulation enabled

**Requirements Covered**: FR-019, FR-020, FR-021, FR-022, FR-023

### 8.1 Pre-Implementation (MANDATORY)

- [x] T057 [US6] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [x] T058 [US6] Write test for 0% modulation = zero pitch variation (FR-022) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T059 [US6] Write test for modulation depth and rate settings in tests/unit/features/ping_pong_delay_test.cpp
- [x] T060 [US6] Write test for independent L/R modulation (90 phase offset) in tests/unit/features/ping_pong_delay_test.cpp

### 8.3 Implementation for User Story 6

- [x] T061 [US6] Integrate LFO (x2) for L/R modulation in src/dsp/features/ping_pong_delay.h
- [x] T062 [US6] Implement setModulationDepth(), setModulationRate() setters in src/dsp/features/ping_pong_delay.h
- [x] T063 [US6] Apply modulation to delay times in process() in src/dsp/features/ping_pong_delay.h
- [x] T064 [US6] Verify all US6 tests pass

### 8.4 Commit (MANDATORY)

- [x] T065 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Modulation working for organic delay character

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Edge cases, output level, parameter smoothing verification, and real-time safety

- [x] T066 [P] Write edge case tests (min/max delay, feedback >100%, ratio switching) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T067 [P] Write test for output level dB-to-gain conversion (FR-025: -inf to +12dB range) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T068 Implement setOutputLevel() with dB-to-gain conversion in src/dsp/features/ping_pong_delay.h
- [x] T069 Verify all parameter smoothers are configured (20ms) for zipper-free changes (SC-008) in src/dsp/features/ping_pong_delay.h
- [x] T070 Verify mono input handling (FR-029) in tests/unit/features/ping_pong_delay_test.cpp
- [x] T071 **Real-time safety verification** (FR-032, FR-033, FR-034):
  - [x] Verify all process() methods are marked noexcept
  - [x] Verify no memory allocations in process() path (grep for new/delete/malloc/vector::push_back)
  - [x] Verify no mutex/lock usage in process() path
- [x] T072 [P] **Performance benchmark** (SC-010): Measure CPU usage at 44.1kHz stereo, verify <1% per instance in tests/unit/features/ping_pong_delay_test.cpp
- [x] T073 Run full test suite and verify all tests pass
- [x] T074 Commit polish work

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T075 **Update ARCHITECTURE.md** with new components:
  - Add PingPongDelay to Layer 4 section
  - Add LRRatio enum documentation
  - Include public API summary and usage example
  - Verify no duplicate functionality was introduced

### 10.2 Final Commit

- [ ] T076 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T077 **Review ALL FR-xxx requirements** (FR-001 to FR-034) from spec.md against implementation
- [ ] T078 **Review ALL SC-xxx success criteria** (SC-001 to SC-013) and verify measurable targets are achieved
- [ ] T079 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T080 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T081 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T082 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [ ] T083 **Commit all spec work** to feature branch
- [ ] T084 **Verify all tests pass** (run full test suite)

### 12.2 Completion Claim

- [ ] T085 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - US1 (P1) MUST complete before other stories (it's the MVP)
  - US2-US6 can proceed after US1
- **Polish (Phase 9)**: Depends on all user stories
- **Final Phases (10-12)**: Depend on Polish

### User Story Dependencies

| Story | Priority | Dependencies | Notes |
|-------|----------|--------------|-------|
| US1 - Classic Ping-Pong | P1 | Foundational | MVP - must complete first |
| US2 - Asymmetric Ratios | P2 | US1 | Extends ratio handling |
| US3 - Tempo Sync | P2 | US1 | Adds time mode |
| US4 - Stereo Width | P3 | US1 | Independent width control |
| US5 - Cross-Feedback | P3 | US1 | Extends feedback system |
| US6 - Modulation | P4 | US1 | Optional enhancement |

### Parallel Opportunities

After US1 (MVP) is complete, US2-US6 can be implemented in any order. Within each story, tests marked [P] can run in parallel.

---

## Parallel Example: After MVP

```bash
# Once US1 is complete, these can run in parallel:
Task: "[US2] Implement ratio timing"
Task: "[US3] Implement tempo sync"
Task: "[US4] Implement width control"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (Classic Ping-Pong)
4. **STOP and VALIDATE**: Test US1 independently
5. Deploy/demo if ready

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add US1 → Classic ping-pong working (MVP!)
3. Add US2 → Asymmetric ratios for polyrhythms
4. Add US3 → Tempo sync for DAW integration
5. Add US4 → Width control for mixing flexibility
6. Add US5 → Cross-feedback for creative control
7. Add US6 → Modulation for organic character

Each story adds value without breaking previous stories.

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
