# Tasks: BBD Delay

**Input**: Design documents from `/specs/025-bbd-delay/`
**Prerequisites**: plan.md, spec.md, research.md, quickstart.md

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

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Project structure and file creation

- [ ] T001 Create bbd_delay.h header file in src/dsp/features/bbd_delay.h with file header comments
- [ ] T002 Create bbd_delay_test.cpp test file in tests/unit/features/bbd_delay_test.cpp
- [ ] T003 Add bbd_delay_test.cpp to tests/CMakeLists.txt

---

## Phase 2: Foundational (BBDDelay Class Skeleton)

**Purpose**: Core infrastructure that MUST be complete before user stories

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 Define BBDChipModel enum (MN3005, MN3007, MN3205, SAD1024) in src/dsp/features/bbd_delay.h
- [ ] T005 Create BBDDelay class skeleton with prepare/reset/process interface in src/dsp/features/bbd_delay.h
- [ ] T006 Add member variables for composed Layer 3 components (CharacterProcessor, FeedbackNetwork, LFO) in src/dsp/features/bbd_delay.h
- [ ] T007 Implement prepare() method to initialize all composed components in src/dsp/features/bbd_delay.h
- [ ] T008 Implement reset() method to clear all state in src/dsp/features/bbd_delay.h
- [ ] T009 Add parameter smoothers (OnePoleSmoother) for all user-facing parameters in src/dsp/features/bbd_delay.h

**Checkpoint**: Foundation ready - BBDDelay class compiles with CharacterProcessor in BBD mode

---

## Phase 3: User Story 1 - Basic BBD Echo (Priority: P1) MVP

**Goal**: Warm, dark analog delay echoes with characteristic limited bandwidth and subtle artifacts

**Independent Test**: Process audio through BBD delay at default settings; output contains delayed repeats with bandwidth-limited, dark character distinct from clean digital delay

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T010 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T011 [P] [US1] Test prepare/reset lifecycle in tests/unit/features/bbd_delay_test.cpp
- [ ] T012 [P] [US1] Test setTime() parameter range (20-1000ms) and clamping in tests/unit/features/bbd_delay_test.cpp
- [ ] T013 [P] [US1] Test setFeedback() parameter range (0-120%) and clamping in tests/unit/features/bbd_delay_test.cpp
- [ ] T014 [P] [US1] Test setMix() dry/wet range (0-100%) in tests/unit/features/bbd_delay_test.cpp
- [ ] T015 [P] [US1] Test process() produces delayed output (not passthrough) in tests/unit/features/bbd_delay_test.cpp
- [ ] T016 [P] [US1] Test feedback creates multiple echoes that darken over time in tests/unit/features/bbd_delay_test.cpp

### 3.3 Implementation for User Story 1

- [ ] T017 [US1] Implement setTime() with parameter smoothing (FR-001 to FR-004) in src/dsp/features/bbd_delay.h
- [ ] T018 [US1] Implement setFeedback() with soft limiting >100% (FR-005 to FR-008) in src/dsp/features/bbd_delay.h
- [ ] T019 [US1] Implement setMix() and setOutputLevel() (FR-036 to FR-038) in src/dsp/features/bbd_delay.h
- [ ] T020 [US1] Implement process() with FeedbackNetwork integration in src/dsp/features/bbd_delay.h
- [ ] T021 [US1] Configure CharacterProcessor in BBD mode for bandwidth limiting in src/dsp/features/bbd_delay.h
- [ ] T022 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T023 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 3.5 Commit (MANDATORY)

- [ ] T024 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic BBD delay produces warm, dark echoes with natural decay

---

## Phase 4: User Story 2 - Modulation/Chorus Effect (Priority: P2)

**Goal**: Classic "chorus-y" delay sound where triangle LFO modulates delay time, creating subtle pitch wobble

**Independent Test**: Enable modulation at 50% depth and verify pitch variation in delayed output

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T025 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T026 [P] [US2] Test setModulation() depth range (0-100%) in tests/unit/features/bbd_delay_test.cpp
- [ ] T027 [P] [US2] Test setModulationRate() range (0.1-10 Hz) in tests/unit/features/bbd_delay_test.cpp
- [ ] T028 [P] [US2] Test modulation=0% produces no pitch variation in tests/unit/features/bbd_delay_test.cpp
- [ ] T029 [P] [US2] Test modulation>0% produces measurable pitch variation in tests/unit/features/bbd_delay_test.cpp

### 4.3 Implementation for User Story 2

- [ ] T030 [US2] Add LFO member with Triangle waveform (FR-011) in src/dsp/features/bbd_delay.h
- [ ] T031 [US2] Implement setModulation() depth control (FR-009) in src/dsp/features/bbd_delay.h
- [ ] T032 [US2] Implement setModulationRate() rate control (FR-010) in src/dsp/features/bbd_delay.h
- [ ] T033 [US2] Wire LFO to delay time modulation (FR-012) in src/dsp/features/bbd_delay.h
- [ ] T034 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T035 [US2] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 4.5 Commit (MANDATORY)

- [ ] T036 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Modulation creates characteristic BBD chorus effect

---

## Phase 5: User Story 3 - Bandwidth Tracking (Priority: P3)

**Goal**: Authentic BBD behavior where longer delay times result in more limited bandwidth

**Independent Test**: Compare frequency response at 50ms vs 500ms delay times; longer delay should have lower bandwidth

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T037 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T038 [P] [US3] Test bandwidth at 20ms is approximately 15kHz (FR-015) in tests/unit/features/bbd_delay_test.cpp
- [ ] T039 [P] [US3] Test bandwidth at 1000ms is approximately 2.5kHz (FR-016) in tests/unit/features/bbd_delay_test.cpp
- [ ] T040 [P] [US3] Test bandwidth scales inversely with delay time (FR-014) in tests/unit/features/bbd_delay_test.cpp

### 5.3 Implementation for User Story 3

- [ ] T041 [US3] Implement calculateBandwidth(delayMs) based on BBD clock physics (FR-017) in src/dsp/features/bbd_delay.h
- [ ] T042 [US3] Update CharacterProcessor.setBBDBandwidth() per-sample based on current delay time in src/dsp/features/bbd_delay.h
- [ ] T043 [US3] Configure lowpass anti-aliasing filter characteristic (FR-018) in src/dsp/features/bbd_delay.h
- [ ] T044 [US3] Verify all US3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T045 [US3] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 5.5 Commit (MANDATORY)

- [ ] T046 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Bandwidth tracking produces authentic "longer = darker" BBD behavior

---

## Phase 6: User Story 4 - BBD Chip Era Selection (Priority: P4)

**Goal**: Switch between different BBD chip characteristics (MN3005, MN3007, MN3205, SAD1024)

**Independent Test**: Switch between Era presets and verify audible differences in character

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T047 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T048 [P] [US4] Test setEra() accepts all 4 chip models in tests/unit/features/bbd_delay_test.cpp
- [ ] T049 [P] [US4] Test MN3005 has widest bandwidth (FR-025) in tests/unit/features/bbd_delay_test.cpp
- [ ] T050 [P] [US4] Test MN3007 has medium-dark character (FR-026) in tests/unit/features/bbd_delay_test.cpp
- [ ] T051 [P] [US4] Test MN3205 has darker, more limited bandwidth than MN3005 (FR-027) in tests/unit/features/bbd_delay_test.cpp
- [ ] T052 [P] [US4] Test SAD1024 has most noise and limited bandwidth (FR-028) in tests/unit/features/bbd_delay_test.cpp
- [ ] T053 [P] [US4] Test default Era is MN3005 (FR-029) in tests/unit/features/bbd_delay_test.cpp

### 6.3 Implementation for User Story 4

- [ ] T054 [US4] Define era characteristic constants (bandwidthFactor, noiseFactor) per chip in src/dsp/features/bbd_delay.h
- [ ] T055 [US4] Implement setEra() to select chip model (FR-024) in src/dsp/features/bbd_delay.h
- [ ] T056 [US4] Apply era factors to bandwidth calculation in src/dsp/features/bbd_delay.h
- [ ] T057 [US4] Apply era factors to clock noise level via CharacterProcessor in src/dsp/features/bbd_delay.h
- [ ] T058 [US4] Verify all US4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T059 [US4] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 6.5 Commit (MANDATORY)

- [ ] T060 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Era selector provides distinct vintage chip characters

---

## Phase 7: User Story 5 - Age/Degradation Character (Priority: P5)

**Goal**: Add analog degradation artifacts - clock noise, compander pumping, bandwidth reduction

**Independent Test**: Set Age to 100% and verify increased noise, pumping artifacts, and bandwidth reduction compared to Age at 0%

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T061 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T062 [P] [US5] Test setAge() range (0-100%) in tests/unit/features/bbd_delay_test.cpp
- [ ] T063 [P] [US5] Test Age=0% produces minimal artifacts (FR-019) in tests/unit/features/bbd_delay_test.cpp
- [ ] T064 [P] [US5] Test Age=100% produces audible clock noise (FR-020) in tests/unit/features/bbd_delay_test.cpp
- [ ] T065 [P] [US5] Test Age affects bandwidth reduction (FR-021) in tests/unit/features/bbd_delay_test.cpp
- [ ] T066 [P] [US5] Test default Age is 20% (FR-023) in tests/unit/features/bbd_delay_test.cpp
- [ ] T067 [P] [US5] Test clock noise increases with delay time (FR-033, FR-034) in tests/unit/features/bbd_delay_test.cpp
- [ ] T068 [P] [US5] Test clock noise is controllable via Age parameter (FR-035) in tests/unit/features/bbd_delay_test.cpp

### 7.3 Implementation for User Story 5

- [ ] T069 [US5] Implement setAge() parameter (FR-019) in src/dsp/features/bbd_delay.h
- [ ] T070 [US5] Scale clock noise level with Age via CharacterProcessor (FR-020, FR-035) in src/dsp/features/bbd_delay.h
- [ ] T071 [US5] Implement clock noise proportional to delay time (FR-033, FR-034) in src/dsp/features/bbd_delay.h
- [ ] T072 [US5] Apply additional bandwidth reduction based on Age (FR-021) in src/dsp/features/bbd_delay.h
- [ ] T073 [US5] Verify all US5 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T074 [US5] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 7.5 Commit (MANDATORY)

- [ ] T075 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Age control provides vintage degradation character

---

## Phase 8: User Story 6 - Compander Artifacts (Priority: P6)

**Goal**: Emphasize the "pumping" and "breathing" artifacts caused by compander circuits

**Independent Test**: Enable compander emulation and verify dynamic artifacts on transient material

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T076 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T077 [P] [US6] Test compander affects transient attacks (attack softening) (FR-030) in tests/unit/features/bbd_delay_test.cpp
- [ ] T078 [P] [US6] Test compander creates release pumping on decays (FR-030) in tests/unit/features/bbd_delay_test.cpp
- [ ] T079 [P] [US6] Test compander intensity scales with Age parameter (FR-031) in tests/unit/features/bbd_delay_test.cpp
- [ ] T080 [P] [US6] Test compander affects dynamics in feedback path (FR-032) in tests/unit/features/bbd_delay_test.cpp

### 8.3 Implementation for User Story 6

- [ ] T081 [US6] Create simple Compander class with envelope follower in src/dsp/features/bbd_delay.h
- [ ] T082 [US6] Implement compression stage (attack softening) in src/dsp/features/bbd_delay.h
- [ ] T083 [US6] Implement expansion stage (release pumping) in src/dsp/features/bbd_delay.h
- [ ] T084 [US6] Scale compander intensity with Age parameter (FR-022) in src/dsp/features/bbd_delay.h
- [ ] T085 [US6] Integrate compander into feedback path in src/dsp/features/bbd_delay.h
- [ ] T086 [US6] Verify all US6 tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T087 [US6] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 8.5 Commit (MANDATORY)

- [ ] T088 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Compander provides authentic vintage dynamic artifacts

---

## Phase 9: Edge Cases & Polish

**Purpose**: Handle edge cases, default values, and cross-cutting concerns

### 9.1 Edge Case Tests

- [ ] T089 [P] Test minimum delay time (20ms) has maximum bandwidth in tests/unit/features/bbd_delay_test.cpp
- [ ] T090 [P] Test maximum delay time (1000ms) has minimum bandwidth in tests/unit/features/bbd_delay_test.cpp
- [ ] T091 [P] Test feedback at 100%+ creates controlled self-oscillation (SC-006) in tests/unit/features/bbd_delay_test.cpp
- [ ] T092 [P] Test modulation at 100% depth with fast rate produces no audio artifacts in tests/unit/features/bbd_delay_test.cpp
- [ ] T093 Test modulation interacts correctly with bandwidth tracking in tests/unit/features/bbd_delay_test.cpp

### 9.2 Default Value Tests

- [ ] T094 [P] Test default delay time is 300ms (FR-004) in tests/unit/features/bbd_delay_test.cpp
- [ ] T095 [P] Test default feedback is 40% (FR-008) in tests/unit/features/bbd_delay_test.cpp
- [ ] T096 [P] Test default modulation depth is 0%, rate is 0.5Hz (FR-013) in tests/unit/features/bbd_delay_test.cpp
- [ ] T097 [P] Test default mix is 50%, output level is 0dB (FR-038) in tests/unit/features/bbd_delay_test.cpp

### 9.3 Real-Time Safety Verification (FR-039 to FR-041)

- [ ] T098 Verify all process() methods are declared noexcept (FR-039) in src/dsp/features/bbd_delay.h
- [ ] T099 Verify no memory allocation in process() path - static analysis check (FR-040)
- [ ] T100 Test CPU usage remains stable across parameter ranges (FR-041) in tests/unit/features/bbd_delay_test.cpp

### 9.4 Success Criteria Verification

- [ ] T101 [P] Test parameter changes are click-free (SC-007) in tests/unit/features/bbd_delay_test.cpp
- [ ] T102 [P] Test all parameters support automation without artifacts (SC-010) in tests/unit/features/bbd_delay_test.cpp

### 9.5 Implementation & Validation

- [ ] T103 Implement any missing edge case handling in src/dsp/features/bbd_delay.h
- [ ] T104 Verify all edge case tests pass
- [ ] T105 Run quickstart.md validation - verify all code examples compile

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T106 **Update ARCHITECTURE.md** with new components added by this spec:
  - Add BBDDelay entry to Layer 4 (User Features) section
  - Add BBDChipModel enum documentation
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples

### 10.2 Final Commit

- [ ] T107 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T108 **Review ALL FR-xxx requirements** (FR-001 to FR-041) from spec.md against implementation
- [ ] T109 **Review ALL SC-xxx success criteria** (SC-001 to SC-010) and verify measurable targets are achieved
- [ ] T110 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T111 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T112 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

- [ ] T113 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [ ] T114 **Commit all spec work** to feature branch
- [ ] T115 **Verify all tests pass**

### 12.2 Completion Claim

- [ ] T116 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - User stories should be completed in priority order (P1 → P2 → P3 → P4 → P5 → P6)
  - Each story builds on previous for cumulative testing
- **Edge Cases (Phase 9)**: Depends on all user stories
- **Documentation (Phase 10)**: Depends on Edge Cases
- **Verification (Phase 11-12)**: Depends on Documentation

### User Story Dependencies

| Story | Priority | Dependencies | Can Start After |
|-------|----------|--------------|-----------------|
| US1 - Basic BBD Echo | P1 | Foundational | Phase 2 |
| US2 - Modulation | P2 | US1 (modulates delay) | Phase 3 |
| US3 - Bandwidth Tracking | P3 | US1 (extends basic delay) | Phase 3 |
| US4 - Era Selection | P4 | US3 (era affects bandwidth) | Phase 5 |
| US5 - Age/Degradation | P5 | US4 (era + age interact) | Phase 6 |
| US6 - Compander | P6 | US5 (compander scales with age) | Phase 7 |

### Within Each User Story

1. **TESTING-GUIDE check**: FIRST task
2. **Tests FIRST**: Write tests that FAIL before implementing
3. **Implementation**: Make tests pass
4. **Verify**: All tests pass
5. **Cross-platform**: Check IEEE 754 compliance
6. **Commit**: LAST task

### Parallel Opportunities

Within each user story phase, test tasks marked [P] can run in parallel:

```bash
# Phase 3: User Story 1 tests (parallel)
T011, T012, T013, T014, T015, T016 can all start together

# Phase 4: User Story 2 tests (parallel)
T026, T027, T028, T029 can all start together

# Phase 9: Edge cases, defaults, and verification (parallel groups)
T089-T093, T094-T097, T101-T102 can run in parallel groups
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T003)
2. Complete Phase 2: Foundational (T004-T009)
3. Complete Phase 3: User Story 1 (T010-T024)
4. **STOP and VALIDATE**: Basic BBD delay works with dark, bandwidth-limited echoes
5. Demo/test if ready

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add US1 (Basic Echo) → Test → Demo (MVP!)
3. Add US2 (Modulation) → Test → Demo
4. Add US3 (Bandwidth Tracking) → Test → Demo
5. Add US4 (Era Selection) → Test → Demo
6. Add US5 (Age/Degradation) → Test → Demo
7. Add US6 (Compander) → Test → Demo
8. Edge cases + Polish → Final testing

### Task Summary

| Phase | Tasks | Description |
|-------|-------|-------------|
| 1: Setup | T001-T003 | 3 tasks |
| 2: Foundational | T004-T009 | 6 tasks |
| 3: US1 Basic Echo | T010-T024 | 15 tasks |
| 4: US2 Modulation | T025-T036 | 12 tasks |
| 5: US3 Bandwidth | T037-T046 | 10 tasks |
| 6: US4 Era | T047-T060 | 14 tasks (+1 for FR-027) |
| 7: US5 Age | T061-T075 | 15 tasks (+3 for FR-033/034/035) |
| 8: US6 Compander | T076-T088 | 13 tasks |
| 9: Edge Cases & Verification | T089-T105 | 17 tasks (+9 for defaults, RT safety, SC-007/010) |
| 10: Documentation | T106-T107 | 2 tasks |
| 11: Verification | T108-T113 | 6 tasks |
| 12: Completion | T114-T116 | 3 tasks |
| **Total** | | **116 tasks** |

---

## Notes

- [P] tasks = different files, no dependencies within phase
- [Story] label maps task to specific user story for traceability
- Each user story should be testable independently after completion
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
