# Tasks: Stereo Field

**Input**: Design documents from `/specs/022-stereo-field/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and test file creation

- [ ] T001 Verify TESTING-GUIDE.md is in context (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T002 Create test file `tests/unit/systems/stereo_field_test.cpp`
- [ ] T003 Add stereo_field_test.cpp to `tests/CMakeLists.txt`
- [ ] T004 Create header file `src/dsp/systems/stereo_field.h` with StereoMode enum and class skeleton

**Checkpoint**: Project structure ready for implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T005 Implement StereoMode enum (Mono, Stereo, PingPong, DualMono, MidSide) in `src/dsp/systems/stereo_field.h`
- [ ] T006 Implement StereoField class skeleton with lifecycle methods: constructor, prepare(), reset()
- [ ] T007 Add member variables: DelayEngine (x2), MidSideProcessor, OnePoleSmoother (for parameters)
- [ ] T008 Implement prepare() - allocate delay buffers, configure smoothers
- [ ] T008a Implement setDelayTimeMs()/getDelayTimeMs() for base delay time (FR-021)
- [ ] T009 Implement reset() - clear delay buffers and smoother states
- [ ] T010 Write basic lifecycle tests (prepare, reset) in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T011 Verify foundational tests pass
- [ ] T012 Commit foundational infrastructure

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Select Stereo Processing Mode (Priority: P1)

**Goal**: Enable mode selection between Mono, Stereo, PingPong, DualMono, and MidSide modes

**Independent Test**: Set each mode and verify correct stereo behavior on known input signal

**Requirements Covered**: FR-001, FR-002, FR-007, FR-008, FR-009, FR-010, FR-011, SC-001

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T013 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

- [ ] T014 [P] [US1] Write Mono mode tests: L+R summed, identical outputs in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T015 [P] [US1] Write Stereo mode tests: independent L/R processing in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T016 [P] [US1] Write PingPong mode tests: alternating L/R delays in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T017 [P] [US1] Write DualMono mode tests: same delay time both channels in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T018 [P] [US1] Write MidSide mode tests: M/S encode, delay, decode in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T019 [US1] Write SC-001 test: verify modes produce distinct outputs in `tests/unit/systems/stereo_field_test.cpp`

### 3.3 Implementation for User Story 1

- [ ] T020 [US1] Implement setMode()/getMode() in `src/dsp/systems/stereo_field.h`
- [ ] T021 [US1] Implement Mono mode processing in process() - sum L+R, output to both
- [ ] T022 [US1] Implement Stereo mode processing in process() - independent L/R delays
- [ ] T023 [US1] Implement PingPong mode processing in process() - alternating L/R with cross-feedback
- [ ] T024 [US1] Implement DualMono mode processing in process() - shared delay time
- [ ] T025 [US1] Implement MidSide mode processing in process() - use MidSideProcessor
- [ ] T026 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T027 [US1] **Verify IEEE 754 compliance**: Check if test file uses `std::isnan` → add to `-fno-fast-math` list

### 3.5 Commit (MANDATORY)

- [ ] T028 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 fully functional - all 5 stereo modes working

---

## Phase 4: User Story 2 - Adjust Stereo Width (Priority: P1)

**Goal**: Enable width control from 0% (mono) to 200% (enhanced stereo)

**Independent Test**: Process stereo signal and measure L/R correlation at different width settings

**Requirements Covered**: FR-012, SC-005, SC-006

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T029 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T030 [P] [US2] Write Width 0% test: output correlation = 1.0 (mono) in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T031 [P] [US2] Write Width 100% test: original stereo preserved in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T032 [P] [US2] Write Width 200% test: Side component at 2x level in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T033 [US2] Write SC-005 test: verify correlation at 0% width in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T034 [US2] Write SC-006 test: verify Side at 2x at 200% width in `tests/unit/systems/stereo_field_test.cpp`

### 4.3 Implementation for User Story 2

- [ ] T035 [US2] Add width_ member variable and smoother in `src/dsp/systems/stereo_field.h`
- [ ] T036 [US2] Implement setWidth()/getWidth() with clamping [0, 200] in `src/dsp/systems/stereo_field.h`
- [ ] T037 [US2] Integrate width control with MidSideProcessor in process()
- [ ] T038 [US2] Verify all US2 tests pass

### 4.4 Commit (MANDATORY)

- [ ] T039 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Width control working across all modes

---

## Phase 5: User Story 3 - Control Output Panning (Priority: P2)

**Goal**: Enable constant-power panning from -100 (left) to +100 (right)

**Independent Test**: Process mono signal and measure L/R output levels at different pan positions

**Requirements Covered**: FR-013, FR-020, SC-007

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T040 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T041 [P] [US3] Write Pan center test: L and R levels equal in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T042 [P] [US3] Write Pan full left test: output only in L channel in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T043 [P] [US3] Write Pan full right test: output only in R channel in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T044 [US3] Write SC-007 test: 40dB channel separation at extreme pan in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T045 [US3] Write constant-power pan test: total power unchanged across pan range in `tests/unit/systems/stereo_field_test.cpp`

### 5.3 Implementation for User Story 3

- [ ] T046 [US3] Add pan_ member variable and smoother in `src/dsp/systems/stereo_field.h`
- [ ] T047 [US3] Implement constantPowerPan() inline function (sin/cos law) in `src/dsp/systems/stereo_field.h`
- [ ] T048 [US3] Implement setPan()/getPan() with clamping [-100, +100] in `src/dsp/systems/stereo_field.h`
- [ ] T049 [US3] Apply pan to output in process()
- [ ] T050 [US3] Verify all US3 tests pass

### 5.4 Commit (MANDATORY)

- [ ] T051 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Constant-power panning working

---

## Phase 6: User Story 4 - Create Timing Offset Between Channels (Priority: P2)

**Goal**: Enable L/R timing offset (±50ms) for Haas-style widening effects

**Independent Test**: Process impulse and measure timing difference between L and R outputs

**Requirements Covered**: FR-014, SC-008

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T052 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T053 [P] [US4] Write offset 0ms test: L and R time-aligned in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T054 [P] [US4] Write offset +10ms test: R delayed 10ms relative to L in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T055 [P] [US4] Write offset -10ms test: L delayed 10ms relative to R in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T056 [US4] Write SC-008 test: offset accuracy within ±1 sample in `tests/unit/systems/stereo_field_test.cpp`

### 6.3 Implementation for User Story 4

- [ ] T057 [US4] Add offsetDelayL_ and offsetDelayR_ DelayLine members (50ms max at 192kHz) in `src/dsp/systems/stereo_field.h`
- [ ] T058 [US4] Add lrOffset_ member variable and smoother in `src/dsp/systems/stereo_field.h`
- [ ] T059 [US4] Implement setLROffset()/getLROffset() with clamping [-50, +50] ms in `src/dsp/systems/stereo_field.h`
- [ ] T060 [US4] Allocate offset delay buffers in prepare()
- [ ] T061 [US4] Apply L/R offset in process() - positive delays R, negative delays L
- [ ] T062 [US4] Verify all US4 tests pass

### 6.4 Commit (MANDATORY)

- [ ] T063 [US4] **Commit completed User Story 4 work**

**Checkpoint**: L/R offset working for Haas-style widening

---

## Phase 7: User Story 5 - Create Polyrhythmic Delays with L/R Ratio (Priority: P3)

**Goal**: Enable L/R ratio for polyrhythmic patterns (e.g., 3:4, 2:3)

**Independent Test**: Set ratio and verify L and R delay times maintain specified relationship

**Requirements Covered**: FR-015, FR-016, SC-009

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T064 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T065 [P] [US5] Write ratio 1:1 test: equal L and R delay times in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T066 [P] [US5] Write ratio 3:4 test: L = 75% of R delay time in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T067 [P] [US5] Write ratio 2:3 test: L = 67% of R delay time in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T068 [US5] Write SC-009 test: ratio accuracy within ±1% in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T069 [US5] Write ratio clamping test: values outside [0.1, 10.0] clamped in `tests/unit/systems/stereo_field_test.cpp`

### 7.3 Implementation for User Story 5

- [ ] T070 [US5] Add lrRatio_ member variable and smoother in `src/dsp/systems/stereo_field.h`
- [ ] T071 [US5] Implement setLRRatio()/getLRRatio() with clamping [0.1, 10.0] in `src/dsp/systems/stereo_field.h`
- [ ] T072 [US5] Apply ratio to delay times: L = base × ratio, R = base in process()
- [ ] T073 [US5] Verify all US5 tests pass

### 7.4 Commit (MANDATORY)

- [ ] T074 [US5] **Commit completed User Story 5 work**

**Checkpoint**: L/R ratio working for polyrhythmic delays

---

## Phase 8: User Story 6 - Smooth Mode Transitions (Priority: P3)

**Goal**: Enable smooth 50ms crossfade transitions between modes

**Independent Test**: Switch modes while processing audio and verify no discontinuities

**Requirements Covered**: FR-003, FR-017, SC-002, SC-004

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T075 [US6] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T076 [P] [US6] Write mode transition test: no clicks during mode change in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T077 [P] [US6] Write rapid mode switching test: 10 changes/sec no artifacts in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T078 [US6] Write SC-002 test: transition completes within 50ms in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T079 [US6] Write SC-004 test: parameter automation at 100Hz glitch-free in `tests/unit/systems/stereo_field_test.cpp`

### 8.3 Implementation for User Story 6

- [ ] T080 [US6] Add mode transition state (previous mode, crossfade progress) in `src/dsp/systems/stereo_field.h`
- [ ] T081 [US6] Add crossfade smoother (50ms transition time) in `src/dsp/systems/stereo_field.h`
- [ ] T082 [US6] Implement mode crossfade in process(): blend old and new mode outputs
- [ ] T083 [US6] Ensure all parameter smoothers use 20ms smoothing time (FR-017)
- [ ] T084 [US6] Verify all US6 tests pass

### 8.4 Commit (MANDATORY)

- [ ] T085 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Smooth mode transitions working

---

## Phase 9: Edge Cases and Safety (FR-018, FR-019)

**Purpose**: Handle edge cases and ensure real-time safety

- [ ] T086 Write NaN input handling test: NaN treated as 0.0 in `tests/unit/systems/stereo_field_test.cpp`
- [ ] T087 Implement NaN input handling in process() using isNaN() from db_utils.h
- [ ] T088 Write real-time safety test: verify noexcept on process methods
- [ ] T089 Add static_assert for noexcept on process()
- [ ] T090 Write parameter clamping edge case tests (width > 200%, ratio extremes)
- [ ] T091 Verify all edge case tests pass
- [ ] T092 Commit edge case handling

**Checkpoint**: All edge cases handled

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Final optimizations and documentation

- [ ] T093 [P] Add Doxygen documentation to StereoField class in `src/dsp/systems/stereo_field.h`
- [ ] T094 Verify SC-003: CPU usage < 1% at 44.1kHz stereo
- [ ] T095 Run all tests and verify 100% pass rate
- [ ] T096 Verify quickstart.md examples work correctly

---

## Phase 11: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md

- [ ] T097 **Update ARCHITECTURE.md** with StereoField component:
  - Add to Layer 3 (System Components) section
  - Include: purpose, public API summary, file location
  - Add usage examples
  - Document stereo modes available
- [ ] T098 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects StereoField component

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 12.1 Requirements Verification

- [ ] T099 **Review ALL FR-xxx requirements** (FR-001 through FR-021) against implementation
- [ ] T100 **Review ALL SC-xxx success criteria** (SC-001 through SC-009) and verify targets achieved
- [ ] T101 **Search for cheating patterns**:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [ ] T102 **Update spec.md "Implementation Verification" section** with compliance status
- [ ] T103 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Final Commit

- [ ] T104 **Commit all spec work** to feature branch
- [ ] T105 **Verify all tests pass**
- [ ] T106 **Claim completion ONLY if all requirements are MET**

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 - BLOCKS all user stories
- **Phase 3-8 (User Stories)**: All depend on Phase 2 completion
  - US1 (Modes): Core functionality, should complete first
  - US2 (Width): Can run parallel with US1, but tests assume modes work
  - US3 (Pan): Can start after US1/US2
  - US4 (Offset): Can run parallel with US3
  - US5 (Ratio): Can run parallel with US3/US4
  - US6 (Transitions): Should run after US1 (needs modes to transition between)
- **Phase 9 (Edge Cases)**: Depends on all user stories
- **Phase 10-12 (Polish/Docs/Verify)**: Depends on Phase 9

### Parallel Opportunities

Within each user story phase, tests marked [P] can run in parallel.

---

## Implementation Strategy

### MVP First (User Stories 1 + 2)

1. Complete Setup + Foundational
2. Complete US1 (Modes) + US2 (Width)
3. **STOP and VALIDATE**: Test all 5 stereo modes with width control
4. This delivers functional stereo processing

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add US1 (Modes) → 5 stereo modes working
3. Add US2 (Width) → Stereo width control
4. Add US3 (Pan) → Output positioning
5. Add US4 (Offset) → Haas-style widening
6. Add US5 (Ratio) → Polyrhythmic delays
7. Add US6 (Transitions) → Smooth mode changes
8. Edge cases + polish → Production ready

---

## Summary

| Metric | Count |
|--------|-------|
| **Total Tasks** | 107 |
| **User Stories** | 6 |
| **Tasks per US1 (Modes)** | 16 |
| **Tasks per US2 (Width)** | 11 |
| **Tasks per US3 (Pan)** | 12 |
| **Tasks per US4 (Offset)** | 12 |
| **Tasks per US5 (Ratio)** | 11 |
| **Tasks per US6 (Transitions)** | 11 |
| **Parallel Opportunities** | 25 tasks marked [P] |

**MVP Scope**: User Stories 1 + 2 (Modes + Width) = 27 tasks
