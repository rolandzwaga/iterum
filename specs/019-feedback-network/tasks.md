# Tasks: FeedbackNetwork

**Input**: Design documents from `/specs/019-feedback-network/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

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

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization - verify dependencies exist

- [x] T001 Verify DelayEngine exists in src/dsp/systems/delay_engine.h
- [x] T002 Verify MultimodeFilter exists in src/dsp/processors/multimode_filter.h
- [x] T003 Verify SaturationProcessor exists in src/dsp/processors/saturation_processor.h
- [x] T004 Verify OnePoleSmoother exists in src/dsp/primitives/smoother.h

---

## Phase 2: Foundational - stereoCrossBlend Layer 0 Utility (Blocking Prerequisites)

**Purpose**: Create reusable Layer 0 utility that MUST be complete before US6 (Cross-Feedback) can be implemented

**Note**: This utility is also required by future specs 022-stereo-field and 023-tap-manager

**⚠️ CRITICAL**: No US6 work can begin until this phase is complete

### 2.1 Pre-Implementation (MANDATORY)

- [x] T005 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.2 Tests for stereoCrossBlend (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T006 [P] Create test file tests/unit/core/stereo_utils_test.cpp with test cases:
  - crossAmount=0.0 returns original L/R (no cross)
  - crossAmount=1.0 returns swapped L/R (full ping-pong)
  - crossAmount=0.5 returns mono blend ((L+R)/2 for both)
  - Energy preservation at various crossAmount values
  - constexpr evaluation test (compile-time usage)

### 2.3 Implementation for stereoCrossBlend

- [x] T007 Create src/dsp/core/stereo_utils.h with stereoCrossBlend() function per spec.md API contract
- [x] T008 Add stereo_utils_test.cpp to tests/CMakeLists.txt

### 2.4 Verification

- [x] T009 Verify all stereoCrossBlend tests pass
- [x] T010 **Commit completed Layer 0 utility** (stereoCrossBlend - FR-017)

**Checkpoint**: stereoCrossBlend utility ready - User Story 6 can now proceed

---

## Phase 3: User Story 1 - Basic Feedback Loop (Priority: P1) 🎯 MVP

**Goal**: Create echoes that fade out with controllable feedback amount (0-100%)

**Independent Test**: Process impulse with 50% feedback, verify ~6dB decay per repeat (SC-001)

**FR Coverage**: FR-001, FR-002, FR-007, FR-008, FR-009, FR-010, FR-011, FR-012, FR-013, FR-015

### 3.1 Pre-Implementation (MANDATORY)

- [x] T011 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T012 [P] [US1] Create tests/unit/systems/feedback_network_test.cpp with US1 test cases:
  - FeedbackNetwork default constructor initializes correctly
  - prepare() allocates resources without error
  - reset() clears internal state
  - setFeedbackAmount(0.0) produces single repeat only (SC: no feedback)
  - setFeedbackAmount(0.5) produces ~6dB decay per repeat (SC-001: use ±0.5dB tolerance via Approx().margin())
  - setFeedbackAmount(1.0) maintains signal level indefinitely (SC-002: ±0.1dB over 10 repeats)
  - Feedback values clamped to [0.0, 1.2] (FR-012)
  - NaN feedback values rejected, keep previous (FR-013)
  - process() mono works correctly
  - process() stereo works correctly
  - Parameter smoothing prevents clicks (FR-011, SC-009)

### 3.3 Implementation for User Story 1

- [x] T013 [US1] Create src/dsp/systems/feedback_network.h with:
  - Class declaration per contracts/feedback_network.h
  - Member variables: DelayLine (x2), feedbackSmoother_, delaySmoother_, feedbackAmount_, prepared_
  - Lifecycle methods: prepare(), reset(), isPrepared()
  - Feedback parameter: setFeedbackAmount(), getFeedbackAmount() with clamping/NaN rejection
  - Basic process() mono - delay + feedback loop (read-before-write pattern)
  - Basic process() stereo - dual channel processing
  - 20ms smoothing for feedback changes
  - hasProcessed_ flag for instant parameter setup vs. smooth transitions

- [x] T014 [US1] Add feedback_network_test.cpp to tests/CMakeLists.txt

### 3.4 Verification

- [x] T015 [US1] Verify all US1 tests pass (19 test cases, 99 assertions)
- [x] T016 [US1] **Verify IEEE 754 compliance**: Test file added to `-fno-fast-math` list in CMakeLists.txt

### 3.5 Commit (MANDATORY)

- [x] T017 [US1] **Commit completed User Story 1 work** (Basic Feedback Loop)

**Checkpoint**: FeedbackNetwork with basic 0-100% feedback is functional and tested

---

## Phase 4: User Story 2 - Self-Oscillation Mode (Priority: P1)

**Goal**: Push feedback beyond 100% for drones/texture, with saturation preventing runaway

**Independent Test**: Set feedback to 120%, verify signal grows but is bounded by saturator (SC-003)

**FR Coverage**: FR-002 (extended to 120%), FR-004 (SaturationProcessor integration)

### 4.1 Pre-Implementation (MANDATORY)

- [x] T018 [US2] **Verify TESTING-GUIDE.md is in context** (already loaded from US1)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [x] T019 [P] [US2] Add US2 test cases to tests/unit/systems/feedback_network_test.cpp:
  - setFeedbackAmount(1.2) allows values up to 120%
  - Feedback at 120% with saturation keeps output bounded below 2.0 (SC-003)
  - Self-oscillation builds up over repeats (with small initial signal)
  - Saturation soft-limits signal (no harsh clipping)
  - Output remains bounded after several seconds of 120% feedback
  - Saturation enable/disable control

### 4.3 Implementation for User Story 2

- [x] T020 [US2] Add SaturationProcessor members (L/R) to FeedbackNetwork (done in US1)
- [x] T021 [US2] Wire SaturationProcessor into feedback path (saturationEnabled_ flag controls activation)
- [x] T022 [US2] Update process() to route signal through saturator (done in US1)
- [x] T023 [US2] Configure default saturation (Tape type, 0dB drive) for self-oscillation limiting

### 4.4 Verification

- [x] T024 [US2] Verify all US2 tests pass (6 test cases, 95201 assertions)
- [x] T025 [US2] **Commit completed User Story 2 work** (Self-Oscillation Mode)

**Checkpoint**: FeedbackNetwork supports 0-120% feedback with saturation limiting

---

## Phase 5: User Story 3 - Filtered Feedback (Priority: P1)

**Goal**: Shape tone of delay trails with LP/HP/BP filter in feedback path

**Independent Test**: Process broadband noise with LP filter, verify HF decay faster than LF (SC-004)

**FR Coverage**: FR-003, FR-014 (filter bypass)

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T026 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T027 [P] [US3] Add US3 test cases to tests/unit/systems/feedback_network_test.cpp:
  - setFilterEnabled(true/false) controls filter processing
  - setFilterType() accepts LP/HP/BP
  - setFilterCutoff() and setFilterResonance() work correctly
  - LP filter at 2kHz attenuates 10kHz by additional 6dB per repeat (SC-004)
  - HP filter makes low frequencies decay faster
  - Filter bypass makes all frequencies decay at same rate
  - Filter changes are smoothed (no clicks)

### 5.3 Implementation for User Story 3

- [ ] T028 [US3] Add MultimodeFilter members (L/R) to FeedbackNetwork
- [ ] T029 [US3] Add filter parameters: setFilterEnabled(), setFilterType(), setFilterCutoff(), setFilterResonance()
- [ ] T030 [US3] Wire filter into feedback path: Delay Output → Filter → Saturator → Feedback Input
- [ ] T031 [US3] Implement filter bypass (when disabled, skip filter processing)

### 5.4 Verification

- [ ] T032 [US3] Verify all US3 tests pass
- [ ] T033 [US3] **Commit completed User Story 3 work** (Filtered Feedback)

**Checkpoint**: FeedbackNetwork supports LP/HP/BP filtering in feedback path

---

## Phase 6: User Story 4 - Saturated Feedback (Priority: P2)

**Goal**: Add warmth and compression with controllable saturation in feedback path

**Independent Test**: Process sine wave with saturation, verify harmonics added to delayed signal

**FR Coverage**: FR-004 (user control), FR-014 (saturation bypass)

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T034 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T035 [P] [US4] Add US4 test cases to tests/unit/systems/feedback_network_test.cpp:
  - setSaturationEnabled(true/false) controls saturation processing
  - setSaturationType() accepts different saturation types
  - setSaturationDrive() controls drive amount
  - Saturation adds harmonics to delayed signal (use FFT to detect 3rd/5th harmonic peaks above -40dB from fundamental)
  - Saturation bypass produces pure signal (no harmonics added)
  - Saturation provides dynamic compression (no harsh peaks)
  - Saturation changes are smoothed

### 6.3 Implementation for User Story 4

- [ ] T036 [US4] Add saturation control parameters: setSaturationEnabled(), setSaturationType(), setSaturationDrive()
- [ ] T037 [US4] Implement saturation bypass (set mix=0 when disabled)
- [ ] T038 [US4] Expose saturation type selection to user

### 6.4 Verification

- [ ] T039 [US4] Verify all US4 tests pass
- [ ] T040 [US4] **Commit completed User Story 4 work** (Saturated Feedback)

**Checkpoint**: FeedbackNetwork has user-controllable saturation in feedback path

---

## Phase 7: User Story 5 - Freeze Mode (Priority: P2)

**Goal**: Capture and sustain audio indefinitely with freeze mode

**Independent Test**: Engage freeze, verify buffer loops for 60+ seconds without degradation (SC-005)

**FR Coverage**: FR-005

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T041 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T042 [P] [US5] Add US5 test cases to tests/unit/systems/feedback_network_test.cpp:
  - setFreeze(true) sets feedback to 100% and mutes input
  - setFreeze(true) stores previous feedback amount
  - isFrozen() returns correct state
  - Freeze mode ignores new input audio
  - Freeze mode maintains buffer content for 60+ seconds (SC-005)
  - setFreeze(false) restores previous feedback and unmutes input
  - Freeze transitions are smoothed (no clicks)

### 7.3 Implementation for User Story 5

- [ ] T043 [US5] Add freeze state: frozen_, preFreezeAmount_, inputMuteSmoother_
- [ ] T044 [US5] Implement setFreeze() with smooth transitions per research.md
- [ ] T045 [US5] Implement isFrozen() query
- [ ] T046 [US5] Update process() to apply input mute when frozen

### 7.4 Verification

- [ ] T047 [US5] Verify all US5 tests pass
- [ ] T048 [US5] **Commit completed User Story 5 work** (Freeze Mode)

**Checkpoint**: FeedbackNetwork supports freeze mode for infinite sustain

---

## Phase 8: User Story 6 - Stereo Cross-Feedback (Priority: P3)

**Goal**: Create ping-pong style delays with cross-feedback routing

**Independent Test**: Process mono impulse to L only, verify repeats alternate L/R (SC-006)

**FR Coverage**: FR-006, FR-016, FR-017 (uses stereoCrossBlend from Phase 2)

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T049 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T050 [US6] **Verify stereoCrossBlend utility is complete** (Phase 2)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T051 [P] [US6] Add US6 test cases to tests/unit/systems/feedback_network_test.cpp:
  - setCrossFeedbackAmount() accepts values [0.0, 1.0]
  - getCrossFeedbackAmount() returns current value
  - Cross-feedback at 0% keeps channels independent (normal stereo)
  - Cross-feedback at 100% swaps channels each repeat (ping-pong)
  - Cross-feedback at 50% blends to mono (SC-006: 50% energy transfer)
  - Cross-feedback creates proper L/R alternation pattern
  - Cross-feedback changes are smoothed
  - Cross-feedback interacts correctly with freeze mode

### 8.3 Implementation for User Story 6

- [ ] T052 [US6] Add cross-feedback state: crossFeedbackAmount_, crossFeedbackSmoother_
- [ ] T053 [US6] Implement setCrossFeedbackAmount(), getCrossFeedbackAmount() with clamping
- [ ] T054 [US6] Include stereo_utils.h and use stereoCrossBlend() in stereo process()
- [ ] T055 [US6] Apply cross-blend after filter/saturation, before feeding back to delay

### 8.4 Verification

- [ ] T056 [US6] Verify all US6 tests pass
- [ ] T057 [US6] **Commit completed User Story 6 work** (Stereo Cross-Feedback)

**Checkpoint**: FeedbackNetwork supports stereo cross-feedback / ping-pong mode

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T058 [P] Performance profiling: Verify <1% CPU at 44.1kHz stereo (SC-007)
- [ ] T059 [P] Allocation audit: Verify zero allocations in process() paths (SC-008)
- [ ] T060 Run quickstart.md validation - verify all code examples compile
- [ ] T061 Edge case testing: feedback=0%, feedback>120% clamping, extreme filter values

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T062 **Update ARCHITECTURE.md** with new components:
  - Add `stereoCrossBlend()` to Layer 0 utilities section (SC-010)
  - Add `FeedbackNetwork` to Layer 3 system components section
  - Include: purpose, public API summary, file location, "when to use this"
  - Document that stereoCrossBlend is for use by 022-stereo-field and 023-tap-manager

### 10.2 Final Commit

- [ ] T063 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T064 **Review ALL FR-xxx requirements** (FR-001 through FR-017) from spec.md against implementation
- [ ] T065 **Review ALL SC-xxx success criteria** (SC-001 through SC-010) and verify measurable targets are achieved
- [ ] T066 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T067 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T068 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T069 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [ ] T070 **Commit all spec work** to feature branch
- [ ] T071 **Verify all tests pass** (run full test suite)

### 12.2 Completion Claim

- [ ] T072 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - verification only
- **Foundational (Phase 2)**: stereoCrossBlend - BLOCKS US6 only
- **US1 (Phase 3)**: Can start immediately after Setup - creates core FeedbackNetwork class
- **US2 (Phase 4)**: Depends on US1 - extends feedback range and adds saturation
- **US3 (Phase 5)**: Depends on US1 - adds filter to feedback path
- **US4 (Phase 6)**: Depends on US2 - adds user control over saturation
- **US5 (Phase 7)**: Depends on US1 - adds freeze mode
- **US6 (Phase 8)**: Depends on US1 AND Phase 2 (stereoCrossBlend) - adds cross-feedback
- **Polish (Phase 9)**: Depends on all user stories being complete
- **Documentation (Phase 10)**: Depends on Phase 9
- **Verification (Phase 11)**: Depends on Phase 10
- **Completion (Phase 12)**: Depends on Phase 11

### Recommended Execution Order

Since US1 creates the core class that all other stories extend:

1. **Phase 1**: Setup verification
2. **Phase 2**: stereoCrossBlend utility (can run in parallel with Phase 3)
3. **Phase 3**: US1 - Basic Feedback (creates FeedbackNetwork class) 🎯 MVP
4. **Phase 4**: US2 - Self-Oscillation (builds on US1)
5. **Phase 5**: US3 - Filtered Feedback (builds on US1)
6. **Phase 6**: US4 - Saturated Feedback (builds on US2)
7. **Phase 7**: US5 - Freeze Mode (builds on US1)
8. **Phase 8**: US6 - Cross-Feedback (builds on US1 + Phase 2)
9. **Phase 9-12**: Polish and completion

### Within Each User Story

- **TESTING-GUIDE check**: FIRST task - verify testing guide is in context
- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- **Implementation**: Make tests pass
- **Verification**: Confirm tests pass
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Phase 2 + Phase 3**: stereoCrossBlend and US1 can run in parallel (different files)
- **US3 + US5**: Filtered Feedback and Freeze Mode are independent after US1 completes
- **Within each story**: Tasks marked [P] can run in parallel

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (verification)
2. Complete Phase 3: User Story 1 - Basic Feedback Loop
3. **STOP and VALIDATE**: Test US1 independently with 0%, 50%, 100% feedback
4. This gives a functional FeedbackNetwork with basic echo capability

### Incremental Delivery

1. US1 → Core feedback loop (MVP)
2. US2 → Extended range with self-oscillation protection
3. US3 → Tone shaping with filter
4. US4 → Character with saturation control
5. US5 → Creative freeze mode
6. US6 → Spatial ping-pong effects

Each story adds value and can be tested independently.

---

## Summary

| Phase | Story | Priority | Task Count | Key Deliverable |
|-------|-------|----------|------------|-----------------|
| 1 | - | - | 4 | Dependency verification |
| 2 | Foundational | - | 6 | stereoCrossBlend Layer 0 utility |
| 3 | US1 | P1 | 7 | Basic FeedbackNetwork class |
| 4 | US2 | P1 | 8 | Self-oscillation support |
| 5 | US3 | P1 | 8 | Filter in feedback path |
| 6 | US4 | P2 | 7 | Saturation control |
| 7 | US5 | P2 | 8 | Freeze mode |
| 8 | US6 | P3 | 9 | Cross-feedback routing |
| 9 | Polish | - | 4 | Performance validation |
| 10 | Docs | - | 2 | ARCHITECTURE.md update |
| 11 | Verify | - | 6 | Compliance verification |
| 12 | Complete | - | 3 | Final commit |
| **Total** | | | **72** | |

**MVP Scope**: Phase 1 + Phase 3 (US1) = 11 tasks for basic functional delay with feedback
