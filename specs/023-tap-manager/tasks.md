# Tasks: TapManager

**Input**: Design documents from `/specs/023-tap-manager/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

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

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create file structure and enums/constants needed by all user stories

- [ ] T001 Create `src/dsp/systems/tap_manager.h` with header guard and includes
- [ ] T002 Create `tests/unit/systems/tap_manager_test.cpp` with Catch2 setup
- [ ] T003 [P] Define TapPattern enum in `src/dsp/systems/tap_manager.h`
- [ ] T004 [P] Define TapTimeMode enum in `src/dsp/systems/tap_manager.h`
- [ ] T005 [P] Define TapFilterMode enum in `src/dsp/systems/tap_manager.h`
- [ ] T006 [P] Define constants (kMaxTaps, kDefaultSmoothingMs, level/filter ranges) in `src/dsp/systems/tap_manager.h`
- [ ] T007 Add test file to `tests/CMakeLists.txt`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core Tap struct and TapManager skeleton that ALL user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T008 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T009 Write foundational tests: TapManager default construction, prepare() validates inputs in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T010 Define Tap struct with all fields (enabled, timeMs, levelDb, pan, filterMode, cutoff, Q, feedback, timeMode, noteValue) in `src/dsp/systems/tap_manager.h`
- [ ] T011 Define TapManager class skeleton with member variables (taps array, DelayLine, smoothers, sample rate, BPM) in `src/dsp/systems/tap_manager.h`
- [ ] T012 Implement TapManager::prepare() - allocate DelayLine, initialize all 16 taps and smoothers in `src/dsp/systems/tap_manager.h`
- [ ] T013 Implement TapManager::reset() - clear delay line, snap smoothers in `src/dsp/systems/tap_manager.h`
- [ ] T014 Verify foundational tests pass
- [ ] T015 **Commit Phase 2 work** (foundational TapManager structure)

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Multi-Tap Delay (Priority: P1) MVP

**Goal**: Enable multiple taps with independent delay times and levels

**Independent Test**: Create taps at different delay times, verify output contains correctly-timed echoes at correct levels

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T016 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T017 [US1] Write tests: setTapEnabled() enables/disables tap output in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T018 [US1] Write tests: setTapTimeMs() sets delay time, output delayed correctly in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T018a [US1] Write tests: delay time accuracy within 1 sample of target (SC-003) in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T019 [US1] Write tests: setTapLevelDb() sets output level, gain applied correctly in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T019a [US1] Write tests: level at -96dB produces silence (FR-010) in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T020 [US1] Write tests: multiple active taps produce combined output in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T021 [US1] Write tests: 16 taps max, out-of-range index (≥16) silently ignored in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T022 [US1] Write tests: level/time changes are smoothed (no clicks) in `tests/unit/systems/tap_manager_test.cpp`

### 3.3 Implementation for User Story 1

- [ ] T023 [US1] Implement setTapEnabled() with smooth fade in `src/dsp/systems/tap_manager.h`
- [ ] T024 [US1] Implement setTapTimeMs() with time smoother in `src/dsp/systems/tap_manager.h`
- [ ] T025 [US1] Implement setTapLevelDb() with level smoother in `src/dsp/systems/tap_manager.h`
- [ ] T026 [US1] Implement process() - write input to delay line, read from each enabled tap, sum outputs in `src/dsp/systems/tap_manager.h`
- [ ] T027 [US1] Implement isTapEnabled(), getTapTimeMs(), getTapLevelDb() query methods in `src/dsp/systems/tap_manager.h`
- [ ] T028 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T029 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite` -> add to `-fno-fast-math` list in tests/CMakeLists.txt if needed

### 3.5 Commit (MANDATORY)

- [ ] T030 [US1] **Commit completed User Story 1 work** (basic multi-tap delay)

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Per-Tap Spatial Positioning (Priority: P2)

**Goal**: Add pan control with constant-power pan law

**Independent Test**: Create taps with different pan positions, verify output channel balance matches expected pan law

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T031 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T032 [US2] Write tests: setTapPan(-100) outputs only to left channel in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T033 [US2] Write tests: setTapPan(+100) outputs only to right channel in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T034 [US2] Write tests: setTapPan(0) outputs equal to both channels in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T035 [US2] Write tests: constant-power pan law (0dB sum at all positions) in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T036 [US2] Write tests: pan changes are smoothed (no clicks) in `tests/unit/systems/tap_manager_test.cpp`

### 4.3 Implementation for User Story 2

- [ ] T037 [US2] Implement setTapPan() with pan smoother in `src/dsp/systems/tap_manager.h`
- [ ] T038 [US2] Implement constant-power pan law (cos/sin) in process() in `src/dsp/systems/tap_manager.h`
- [ ] T039 [US2] Implement getTapPan() query method in `src/dsp/systems/tap_manager.h`
- [ ] T040 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T041 [US2] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 4.5 Commit (MANDATORY)

- [ ] T042 [US2] **Commit completed User Story 2 work** (per-tap pan)

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Per-Tap Filtering (Priority: P3)

**Goal**: Add LP/HP filter per tap using Biquad

**Independent Test**: Set different filter cutoffs per tap, measure frequency response of each tap's output

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T043 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T044 [US3] Write tests: setTapFilterMode(Lowpass) attenuates high frequencies (>12dB at cutoff×2) in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T045 [US3] Write tests: setTapFilterMode(Highpass) attenuates low frequencies (>12dB at cutoff/2) in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T046 [US3] Write tests: setTapFilterMode(Bypass) passes full spectrum in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T047 [US3] Write tests: setTapFilterCutoff() changes cutoff frequency in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T048 [US3] Write tests: setTapFilterQ() changes resonance in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T049 [US3] Write tests: filter parameter changes are smoothed in `tests/unit/systems/tap_manager_test.cpp`

### 5.3 Implementation for User Story 3

- [ ] T050 [US3] Add Biquad instance per tap in Tap struct in `src/dsp/systems/tap_manager.h`
- [ ] T051 [US3] Implement setTapFilterMode() in `src/dsp/systems/tap_manager.h`
- [ ] T052 [US3] Implement setTapFilterCutoff() with cutoff smoother in `src/dsp/systems/tap_manager.h`
- [ ] T053 [US3] Implement setTapFilterQ() in `src/dsp/systems/tap_manager.h`
- [ ] T054 [US3] Integrate Biquad processing into process() after delay read in `src/dsp/systems/tap_manager.h`
- [ ] T055 [US3] Verify all US3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T056 [US3] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 5.5 Commit (MANDATORY)

- [ ] T057 [US3] **Commit completed User Story 3 work** (per-tap filtering)

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently

---

## Phase 6: User Story 4 - Per-Tap Feedback Routing (Priority: P4)

**Goal**: Add feedback amount per tap routing to master input

**Independent Test**: Enable feedback on specific taps, verify decay behavior

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T058 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T059 [US4] Write tests: setTapFeedback(50) causes 50% decay per iteration in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T060 [US4] Write tests: setTapFeedback(0) produces single echo (no repetition) in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T061 [US4] Write tests: multiple taps with feedback combine correctly in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T062 [US4] Write tests: total feedback > 100% is limited (no runaway) in `tests/unit/systems/tap_manager_test.cpp`

### 6.3 Implementation for User Story 4

- [ ] T063 [US4] Implement setTapFeedback() with validation (0-100%) in `src/dsp/systems/tap_manager.h`
- [ ] T064 [US4] Implement feedback summing in process() - accumulate tap outputs × feedback, add to delay input in `src/dsp/systems/tap_manager.h`
- [ ] T065 [US4] Implement feedback limiter (soft clip if total > 1.0) in `src/dsp/systems/tap_manager.h`
- [ ] T066 [US4] Verify all US4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T067 [US4] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 6.5 Commit (MANDATORY)

- [ ] T068 [US4] **Commit completed User Story 4 work** (per-tap feedback)

**Checkpoint**: User Stories 1-4 should all work independently

---

## Phase 7: User Story 5 - Preset Tap Patterns (Priority: P5)

**Goal**: Implement pattern presets (Quarter, Dotted Eighth, Triplet, Golden Ratio, Fibonacci)

**Independent Test**: Load preset patterns, verify tap times match expected mathematical relationships

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T069 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T070 [US5] Write tests: loadPattern(QuarterNote, 4) at 120 BPM creates taps at 500, 1000, 1500, 2000ms in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T071 [US5] Write tests: loadPattern(DottedEighth, 4) at 120 BPM creates taps at 375, 750, 1125, 1500ms in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T072 [US5] Write tests: loadPattern(Triplet, 4) at 120 BPM creates taps at ~333, 667, 1000, 1333ms in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T073 [US5] Write tests: loadPattern(GoldenRatio, 4) creates taps at t, t×1.618, t×2.618, t×4.236 in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T074 [US5] Write tests: loadPattern(Fibonacci, 6) creates taps following 1, 1, 2, 3, 5, 8 sequence in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T075 [US5] Write tests: loadPattern() disables all taps first, then enables pattern taps in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T076 [US5] Write tests: getPattern() returns current pattern type in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T077 [US5] Write tests: modifying tap after loadPattern() marks pattern as Custom in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T077a [US5] Write tests: loadPattern() completes within 1ms (SC-008) in `tests/unit/systems/tap_manager_test.cpp`

### 7.3 Implementation for User Story 5

- [ ] T078 [US5] Implement loadPattern() - disable all, calculate times (1-based indexing), enable count taps in `src/dsp/systems/tap_manager.h`
- [ ] T079 [US5] Implement QuarterNote pattern: tap[i] = (i+1) × (60000/bpm), i=0..count-1 in `src/dsp/systems/tap_manager.h`
- [ ] T080 [US5] Implement DottedEighth pattern: tap[i] = (i+1) × (60000/bpm × 0.75) in `src/dsp/systems/tap_manager.h`
- [ ] T081 [US5] Implement Triplet pattern: tap[i] = (i+1) × (60000/bpm × 0.667) in `src/dsp/systems/tap_manager.h`
- [ ] T082 [US5] Implement GoldenRatio pattern: tap[0] = quarter, tap[i] = tap[i-1] × kGoldenRatio in `src/dsp/systems/tap_manager.h`
- [ ] T083 [US5] Implement Fibonacci pattern: tap[i] = fib(i+1) × baseMs, fib = 1,1,2,3,5,8... in `src/dsp/systems/tap_manager.h`
- [ ] T084 [US5] Implement getPattern() query method in `src/dsp/systems/tap_manager.h`
- [ ] T085 [US5] Mark pattern as Custom when tap modified manually in `src/dsp/systems/tap_manager.h`
- [ ] T086 [US5] Verify all US5 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T087 [US5] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 7.5 Commit (MANDATORY)

- [ ] T088 [US5] **Commit completed User Story 5 work** (preset patterns)

**Checkpoint**: User Stories 1-5 should all work independently

---

## Phase 8: User Story 6 - Tempo Sync (Priority: P6)

**Goal**: Support tempo-synced delay times via NoteValue

**Independent Test**: Set tempo, enable sync on taps with note values, verify delay times update when tempo changes

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T089 [US6] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T090 [US6] Write tests: setTapNoteValue() sets time mode to TempoSynced in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T091 [US6] Write tests: synced tap at 120 BPM quarter note = 500ms, at 140 BPM = 428.57ms in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T092 [US6] Write tests: free-running tap ignores tempo changes in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T093 [US6] Write tests: setTempo() updates all synced tap times in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T093a [US6] Write tests: tempo sync updates within 1 audio block (SC-006) in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T094 [US6] Write tests: dotted eighth = 0.75 × quarter, triplet = 0.667 × quarter in `tests/unit/systems/tap_manager_test.cpp`

### 8.3 Implementation for User Story 6

- [ ] T095 [US6] Implement setTapNoteValue() - set timeMode to TempoSynced, store note value in `src/dsp/systems/tap_manager.h`
- [ ] T096 [US6] Implement setTempo() - store BPM, recalculate all synced tap times in `src/dsp/systems/tap_manager.h`
- [ ] T097 [US6] Implement getEffectiveDelayMs() - return ms for free-running, calculate from BPM for synced in `src/dsp/systems/tap_manager.h`
- [ ] T098 [US6] Update process() to use getEffectiveDelayMs() for delay read position in `src/dsp/systems/tap_manager.h`
- [ ] T099 [US6] Verify all US6 tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T100 [US6] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 8.5 Commit (MANDATORY)

- [ ] T101 [US6] **Commit completed User Story 6 work** (tempo sync)

**Checkpoint**: All 6 user stories should work independently

---

## Phase 9: Master Controls & Polish

**Purpose**: Master output level, dry/wet mix, performance verification, and final improvements

- [ ] T102 Write tests: setMasterLevel() applies gain to all tap outputs in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T103 Write tests: setDryWetMix() blends dry input with wet tap output in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T104 Write tests: getActiveTapCount() returns number of enabled taps in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T104a Write tests: 16 active taps process without audio dropouts at 44.1kHz stereo (SC-001) in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T104b Write tests: CPU usage < 2% for 16 active taps at 44.1kHz stereo (SC-007) in `tests/unit/systems/tap_manager_test.cpp`
- [ ] T105 Implement setMasterLevel() with master level smoother in `src/dsp/systems/tap_manager.h`
- [ ] T106 Implement setDryWetMix() with dry/wet smoother in `src/dsp/systems/tap_manager.h`
- [ ] T107 Implement getActiveTapCount() query in `src/dsp/systems/tap_manager.h`
- [ ] T108 Update process() to apply master level and dry/wet mix in `src/dsp/systems/tap_manager.h`
- [ ] T109 Verify all master control tests pass
- [ ] T109a Verify performance tests pass (SC-001, SC-007)
- [ ] T110 Run quickstart.md validation - verify all examples compile/work
- [ ] T111 **Commit Polish phase work**

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T112 **Update ARCHITECTURE.md** with TapManager component:
  - Add to Layer 3: System Components section
  - Include: purpose, public API summary, file location
  - Document: TapPattern, TapTimeMode, TapFilterMode enums
  - Add usage examples

### 10.2 Final Commit

- [ ] T113 **Commit ARCHITECTURE.md updates**
- [ ] T114 Verify all spec work is committed to feature branch

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed

### 11.1 Requirements Verification

- [ ] T115 **Review ALL FR-xxx requirements** from spec.md against implementation (FR-001 to FR-033, FR-004a)
- [ ] T115a **Verify real-time safety (FR-031, FR-032)**:
  - Confirm all process() methods are noexcept
  - Confirm no allocations in process() (grep for new/delete/malloc/vector resize)
  - Confirm no blocking calls (mutex, file I/O, etc.)
- [ ] T116 **Review ALL SC-xxx success criteria** (SC-001 to SC-008):
  - SC-001: 16 taps active without dropouts
  - SC-002: Parameter changes smooth within 20ms
  - SC-003: Delay time accuracy within 1 sample
  - SC-004: Constant-power pan law (0dB sum)
  - SC-005: Filter >12dB/octave attenuation
  - SC-006: Tempo sync updates within 1 block
  - SC-007: CPU < 2% for 16 taps at 44.1kHz
  - SC-008: Patterns load within 1ms
- [ ] T117 **Search for cheating patterns** in implementation:
  - No `// placeholder` or `// TODO` comments
  - No relaxed test thresholds
  - No quietly removed features

### 11.2 Fill Compliance Table

- [ ] T118 **Update spec.md "Implementation Verification" section** with compliance status for each FR and SC

### 11.3 Honest Self-Check

- [ ] T119 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 12: Final Completion

- [ ] T120 **Commit all spec work** to feature branch
- [ ] T121 **Verify all tests pass**
- [ ] T122 **Claim completion ONLY if all requirements are MET**

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational completion
  - US1 can start immediately after Phase 2
  - US2-US6 can proceed in parallel after Phase 2 (or sequentially in priority order)
- **Polish (Phase 9)**: After all user stories complete
- **Documentation (Phase 10)**: After Polish
- **Verification (Phase 11-12)**: After Documentation

### User Story Dependencies

All user stories are independently implementable after Phase 2:

- **US1 (P1)**: Core delay - no dependencies on other stories
- **US2 (P2)**: Pan - independent of other stories
- **US3 (P3)**: Filter - independent of other stories
- **US4 (P4)**: Feedback - independent of other stories
- **US5 (P5)**: Patterns - uses tempo (can start after Phase 2)
- **US6 (P6)**: Tempo sync - independent of other stories

### Parallel Opportunities

**Within Phase 1 (Setup)**:
- T003, T004, T005, T006 can run in parallel

**Within Each User Story**:
- All test tasks can run in parallel within their story
- Implementation follows tests sequentially

**Across User Stories** (after Phase 2):
- All 6 user stories can be worked on in parallel by different developers

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (basic multi-tap delay)
4. **STOP and VALIDATE**: Test US1 independently
5. Functional multi-tap delay with time/level controls

### Incremental Delivery

1. Setup + Foundational -> Foundation ready
2. Add US1 -> Basic delay working (MVP!)
3. Add US2 -> Pan control added
4. Add US3 -> Filtering added
5. Add US4 -> Feedback added
6. Add US5 -> Preset patterns added
7. Add US6 -> Tempo sync added
8. Each story adds value without breaking previous stories

---

## Summary

| Phase | Description | Task Count |
|-------|-------------|------------|
| 1 | Setup | 7 |
| 2 | Foundational | 8 |
| 3 | US1 - Basic Multi-Tap | 17 |
| 4 | US2 - Pan Control | 12 |
| 5 | US3 - Per-Tap Filtering | 15 |
| 6 | US4 - Feedback Routing | 11 |
| 7 | US5 - Preset Patterns | 21 |
| 8 | US6 - Tempo Sync | 14 |
| 9 | Master Controls & Polish | 13 |
| 10 | Documentation | 3 |
| 11 | Verification | 6 |
| 12 | Final Completion | 3 |
| **Total** | | **130** |

### Tasks Per User Story

| Story | Priority | Tasks |
|-------|----------|-------|
| US1 | P1 | 17 |
| US2 | P2 | 12 |
| US3 | P3 | 15 |
| US4 | P4 | 11 |
| US5 | P5 | 21 |
| US6 | P6 | 14 |

### Suggested MVP Scope

**User Story 1 (Basic Multi-Tap Delay)** - Phases 1-3 only:
- 30 tasks total
- Delivers: functional multi-tap delay with independent time/level per tap
- Foundation for all other stories
