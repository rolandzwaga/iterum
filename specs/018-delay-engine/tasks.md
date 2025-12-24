# Tasks: DelayEngine

**Input**: Design documents from `/specs/018-delay-engine/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/delay_engine.h
**Layer**: 3 (System Component)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check

**CRITICAL for VST3**: The VST3 SDK enables `-ffast-math` globally. If test files use `std::isnan`/`std::isfinite`/`std::isinf`:
- Add to `-fno-fast-math` list in `tests/CMakeLists.txt`

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Project Structure)

**Purpose**: Create directory structure and base test file

- [X] T001 Create Layer 3 systems directory at `src/dsp/systems/`
- [X] T002 Create Layer 3 tests directory at `tests/unit/systems/`
- [X] T003 Add systems directory to `tests/CMakeLists.txt` include paths (already included via ${CMAKE_SOURCE_DIR}/src)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Create the core class skeleton that all user stories depend on

- [X] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [X] T005 Create `delay_engine_test.cpp` in `tests/unit/systems/` with Catch2 includes
- [X] T006 Add `delay_engine_test.cpp` to `tests/CMakeLists.txt`
- [X] T007 Create `delay_engine.h` skeleton in `src/dsp/systems/` with:
  - TimeMode enum (Free, Synced)
  - DelayEngine class declaration per contracts/delay_engine.h
  - Member variables (DelayLine, smoothers, config state)
  - FR-001: Wrap DelayLine primitive

**Checkpoint**: Class compiles and can be instantiated in tests

---

## Phase 3: User Story 1 - Free Time Mode (Priority: P1)

**Goal**: Set delay time in milliseconds with smooth transitions

**Independent Test**: Impulse response verifies delay timing; parameter changes are click-free

**Requirements**: FR-002, FR-004, FR-007, FR-008, FR-009, FR-010, FR-011, FR-012

### 3.1 Pre-Implementation (MANDATORY)

- [X] T008 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

- [X] T009 [P] [US1] Test: `prepare()` allocates buffers for sample rate in `tests/unit/systems/delay_engine_test.cpp`
- [X] T010 [P] [US1] Test: `setDelayTimeMs(250)` at 44.1kHz produces 11025 samples delay
- [X] T011 [P] [US1] Test: Delay time change is smoothed (no clicks)
- [X] T012 [P] [US1] Test: Delay time clamped to [0, maxDelayMs] (FR-010)
- [X] T013 [P] [US1] Test: NaN delay time rejected, keeps previous value (FR-011)
- [X] T014 [P] [US1] Test: Linear interpolation for sub-sample delays (FR-012)
- [X] T015 [P] [US1] Test: `reset()` clears buffer to silence

### 3.3 Implementation for User Story 1

- [X] T016 [US1] Implement `prepare(sampleRate, maxBlockSize, maxDelayMs)` - allocate DelayLine
- [X] T017 [US1] Implement `setDelayTimeMs(float ms)` with clamping and NaN handling
- [X] T018 [US1] Implement delay time smoother configuration (20ms smoothing)
- [X] T019 [US1] Implement `process()` mono version with:
  - Read current delay time from smoother
  - Use `delayLine_.readLinear()` for sub-sample accuracy
  - Write input, read delayed output
- [X] T020 [US1] Implement `reset()` - clear DelayLine and smoothers
- [X] T021 [US1] Implement `getCurrentDelayMs()` query method
- [X] T022 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T023 [US1] **Verify IEEE 754 compliance**: Check if `std::isnan` used in test file -> add to `-fno-fast-math` list

### 3.5 Commit (MANDATORY)

- [X] T024 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Free mode delay works with smooth transitions - SC-001, SC-002 verified

---

## Phase 4: User Story 2 - Synced Time Mode (Priority: P1)

**Goal**: Delay time calculated from NoteValue + host tempo via BlockContext

**Independent Test**: BlockContext with known tempo produces expected delay samples

**Requirements**: FR-003, FR-004

### 4.1 Pre-Implementation (MANDATORY)

- [X] T025 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [X] T026 [P] [US2] Test: `setTimeMode(TimeMode::Synced)` switches mode
- [X] T027 [P] [US2] Test: Quarter note at 120 BPM = 500ms = 22050 samples at 44.1kHz
- [X] T028 [P] [US2] Test: Dotted eighth at 100 BPM = 450ms
- [X] T029 [P] [US2] Test: All NoteValue types produce correct times (SC-005)
- [X] T030 [P] [US2] Test: Tempo change updates delay smoothly (no clicks)
- [X] T031 [P] [US2] Test: Triplet modifier works correctly

### 4.3 Implementation for User Story 2

- [X] T032 [US2] Implement `setTimeMode(TimeMode mode)` setter
- [X] T033 [US2] Implement `setNoteValue(NoteValue, NoteModifier)` setter
- [X] T034 [US2] Implement `updateDelayTarget(BlockContext)` helper:
  - If TimeMode::Synced: use `ctx.tempoToSamples(noteValue_, noteModifier_)`
  - Convert samples to ms for smoother target
- [X] T035 [US2] Update `process()` to call `updateDelayTarget()` at block start
- [X] T036 [US2] Implement `getTimeMode()` query method
- [X] T037 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T038 [US2] **Verify IEEE 754 compliance**: No additional checks expected for US2

### 4.5 Commit (MANDATORY)

- [X] T039 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Synced mode works with all note values - SC-005 verified

---

## Phase 5: User Story 3 - Dry/Wet Mix Control (Priority: P2)

**Goal**: Blend dry and delayed signals with kill-dry option

**Independent Test**: Output levels match expected mix ratios

**Requirements**: FR-005, FR-006

### 5.1 Pre-Implementation (MANDATORY)

- [X] T040 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [X] T041 [P] [US3] Test: Mix 0% = 100% dry (output equals input)
- [X] T042 [P] [US3] Test: Mix 100% = 100% wet (only delayed signal)
- [X] T043 [P] [US3] Test: Mix 50% = equal blend of dry and wet
- [X] T044 [P] [US3] Test: Kill-dry mode outputs only wet signal
- [X] T045 [P] [US3] Test: Mix changes are smoothed (no clicks)

### 5.3 Implementation for User Story 3

- [X] T046 [US3] Implement `setMix(float wetRatio)` with clamping to [0, 1]
- [X] T047 [US3] Implement `setKillDry(bool killDry)` setter
- [X] T048 [US3] Add mix smoother to smooth mix transitions
- [X] T049 [US3] Update `process()` mono with dry/wet mixing:
  - `dryCoeff = killDry ? 0.0f : (1.0f - mix)`
  - `wetCoeff = mix`
  - `output = dry * dryCoeff + wet * wetCoeff`
- [X] T050 [US3] Verify all US3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T051 [US3] **Verify IEEE 754 compliance**: No additional checks expected for US3

### 5.5 Commit (MANDATORY)

- [X] T052 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Mix control works with kill-dry option

---

## Phase 6: User Story 4 - State Management (Priority: P2)

**Goal**: Proper lifecycle with prepare/reset and stereo support

**Independent Test**: Lifecycle methods behave correctly; stereo processing works

**Requirements**: FR-007, FR-008, FR-009, SC-003

### 6.1 Pre-Implementation (MANDATORY)

- [X] T053 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [X] T054 [P] [US4] Test: `isPrepared()` returns false before prepare, true after
- [X] T055 [P] [US4] Test: `getMaxDelayMs()` returns configured value
- [X] T056 [P] [US4] Test: Stereo process() applies same delay to both channels
- [X] T057 [P] [US4] Test: Variable block sizes (up to maxBlockSize) work correctly
- [X] T058 [P] [US4] Test: Process is real-time safe (SC-003 - no allocations)

### 6.3 Implementation for User Story 4

- [X] T059 [US4] Implement `isPrepared()` and `getMaxDelayMs()` query methods
- [X] T060 [US4] Add second DelayLine for stereo right channel
- [X] T061 [US4] Implement stereo `process(left, right, numSamples, ctx)`:
  - Process left with main delay line
  - Process right with stereo delay line
  - Same delay/mix settings for both
- [X] T062 [US4] Update `prepare()` to initialize both delay lines
- [X] T063 [US4] Update `reset()` to clear both delay lines
- [X] T064 [US4] Verify all US4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T065 [US4] **Verify IEEE 754 compliance**: No additional checks expected for US4

### 6.5 Commit (MANDATORY)

- [X] T066 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Full lifecycle and stereo support working

---

## Phase 7: Edge Cases & Polish

**Purpose**: Handle edge cases documented in spec

- [X] T067 Test: 0ms delay outputs immediate signal
- [X] T068 Test: Negative delay time clamps to 0
- [X] T069 Test: Infinity delay time clamps to maxDelayMs
- [X] T070 Test: BlockContext with tempo=0 handled (clamps to 20 BPM minimum)
- [X] T071 Implement edge case handling if any tests fail
- [X] T072 Verify all edge case tests pass
- [X] T073 **Commit edge case handling**

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

### 8.1 Architecture Documentation Update

- [X] T074 **Update ARCHITECTURE.md** with DelayEngine:
  - Add to Layer 3 (Systems) section
  - Include: purpose, public API summary, file location
  - Add usage examples (from quickstart.md)
  - Document TimeMode enum

### 8.2 Final Commit

- [X] T075 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects DelayEngine

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

### 9.1 Requirements Verification

- [X] T076 **Review ALL FR-xxx requirements** (FR-001 through FR-012) against implementation
- [X] T077 **Review ALL SC-xxx success criteria** (SC-001 through SC-006):
  - SC-001: Timing accuracy within 1 sample
  - SC-002: No audible artifacts during transitions
  - SC-003: Zero allocations in process()
  - SC-004: 90% test coverage (verify with coverage tool or manual review)
  - SC-005: All NoteValue types correct
  - SC-006: CPU < 1% at 44.1kHz stereo (Layer 3 budget per Constitution XI)
- [X] T078 **Search for cheating patterns**:
  - [X] No `// placeholder` or `// TODO` comments
  - [X] No relaxed test thresholds
  - [X] No removed features

### 9.2 Fill Compliance Table in spec.md

- [X] T079 **Update spec.md "Implementation Verification" section** with status for each FR and SC
- [X] T080 **Mark overall status**: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

- [X] T081 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 10: Final Completion

- [X] T082 **Verify all tests pass**: Run `dsp_tests.exe "[delay]"` or full test suite
- [X] T083 **Final commit** of all spec work
- [X] T084 **Claim completion** (only if all requirements MET)

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup) → Phase 2 (Foundational) → All User Stories can proceed
                                         ├── Phase 3 (US1 - Free Mode) ──┐
                                         ├── Phase 4 (US2 - Synced Mode)─┼── Phase 7 (Polish)
                                         ├── Phase 5 (US3 - Mix Control)─┤
                                         └── Phase 6 (US4 - Lifecycle) ──┘
                                                                         ↓
                                                Phase 8 (Documentation) → Phase 9 (Verification) → Phase 10 (Complete)
```

### User Story Dependencies

| Story | Depends On | Blocks |
|-------|------------|--------|
| US1 (Free Mode) | Foundational | US2, US3, US4 can start in parallel but may reuse US1 process() |
| US2 (Synced Mode) | Foundational, benefits from US1 process() | None |
| US3 (Mix Control) | Foundational, uses US1 process() | None |
| US4 (Lifecycle) | Foundational | None |

### Recommended Order (Single Developer)

1. Setup → Foundational
2. US1 (Free Mode) - establishes process() pattern
3. US2 (Synced Mode) - extends time calculation
4. US3 (Mix Control) - adds output mixing
5. US4 (Lifecycle) - adds stereo + queries
6. Edge Cases → Documentation → Verification

### Parallel Opportunities

All tests within a user story marked [P] can run in parallel:
```
# Example: Launch all US1 tests together
T009, T010, T011, T012, T013, T014, T015 (all [P] [US1])
```

---

## Summary

| Phase | Tasks | Purpose |
|-------|-------|---------|
| 1 - Setup | 3 | Create directory structure |
| 2 - Foundational | 4 | Class skeleton, basic compile |
| 3 - US1 Free Mode | 17 | Millisecond delay with smoothing |
| 4 - US2 Synced Mode | 15 | Tempo-synced delay |
| 5 - US3 Mix Control | 13 | Dry/wet mixing |
| 6 - US4 Lifecycle | 14 | Stereo + lifecycle methods |
| 7 - Edge Cases | 7 | Handle boundary conditions |
| 8 - Documentation | 2 | Update ARCHITECTURE.md |
| 9 - Verification | 6 | Honest requirement check |
| 10 - Completion | 3 | Final commit |
| **Total** | **84** | |

**MVP Scope**: Complete through Phase 3 (US1) for minimal working delay.

**Independent Tests per Story**:
- US1: Impulse at 250ms appears at sample 11025
- US2: Quarter note at 120 BPM = 22050 samples
- US3: Mix 50% = 0.5 dry + 0.5 wet
- US4: Stereo process applies to both channels
