# Tasks: Mode Switch Click-Free Transitions

**Input**: Design documents from `/specs/041-mode-switch-clicks/`
**Prerequisites**: plan.md, spec.md, research.md, quickstart.md

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

---

## Phase 1: Setup (Layer 0 Extraction)

**Purpose**: Create shared crossfade utility in Layer 0 before any consumer code

- [X] T001 **Verify TESTING-GUIDE.md and VST-GUIDE.md are in context** (ingest if needed)

---

## Phase 2: Foundational - Layer 0 Crossfade Utility

**Purpose**: Create and test the shared `equalPowerGains()` utility that all crossfade consumers will use

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Layer 0 Utility (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T002 [P] Write unit tests for `equalPowerGains()` boundary conditions in tests/unit/core/crossfade_utils_tests.cpp
- [X] T003 [P] Write unit tests for constant-power property (fadeOut² + fadeIn² ≈ 1) in tests/unit/core/crossfade_utils_tests.cpp
- [X] T004 [P] Write unit tests for `crossfadeIncrement()` calculation in tests/unit/core/crossfade_utils_tests.cpp

### 2.2 Implementation for Layer 0 Utility

- [X] T005 Create crossfade_utils.h with `kHalfPi`, `equalPowerGains()`, `crossfadeIncrement()` in src/dsp/core/crossfade_utils.h
- [X] T006 Register crossfade_utils_tests.cpp in tests/CMakeLists.txt
- [X] T007 Build and verify all Layer 0 tests pass
- [X] T008 **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list if needed

### 2.3 Refactor Existing Consumers

- [X] T009 Refactor CharacterProcessor to use `equalPowerGains()` from crossfade_utils.h in src/dsp/systems/character_processor.h
- [X] T010 Build and verify CharacterProcessor tests still pass
- [X] T011 **Commit completed Layer 0 utility and CharacterProcessor refactor**

**Checkpoint**: Layer 0 utility ready - User Story implementation can now begin

---

## Phase 3: User Story 1 - Click-Free Mode Switching (Priority: P1) 🎯 MVP

**Goal**: Eliminate audible clicks when switching between delay modes by implementing 50ms equal-power crossfade

**Independent Test**: Play continuous audio through plugin, switch modes, verify no audible clicks/pops

**Acceptance Criteria**:
- FR-001: Mode switching produces no audible clicks
- FR-002: Crossfade applied to prevent discontinuities
- FR-003: Fade duration under 50ms
- FR-004: Safe at any point during audio processing
- FR-006: Rapid switching produces no cumulative artifacts
- FR-008: All 11 modes support click-free transitions
- SC-001: Zero audible clicks in any mode-to-mode switch
- SC-002: Transition completes under 50ms
- SC-005: Rapid switching (10/sec) stable

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T012 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T013 [P] [US1] Write unit tests for crossfade state initialization in tests/unit/processor/mode_crossfade_tests.cpp
- [ ] T014 [P] [US1] Write unit tests for crossfade increment calculation in tests/unit/processor/mode_crossfade_tests.cpp
- [ ] T015 [P] [US1] Write unit tests verifying crossfade completes in ~2205 samples at 44.1kHz in tests/unit/processor/mode_crossfade_tests.cpp
- [ ] T016 [P] [US1] Write unit tests for rapid mode switching stability in tests/unit/processor/mode_crossfade_tests.cpp

### 3.3 Implementation for User Story 1

- [ ] T017 [US1] Add crossfade state variables to Processor class in src/processor/processor.h:
  - `currentProcessingMode_`, `previousMode_`
  - `crossfadePosition_`, `crossfadeIncrement_`
  - `crossfadeBufferL_`, `crossfadeBufferR_`
  - `kCrossfadeTimeMs = 50.0f`
- [ ] T018 [US1] Add `processMode()` helper method declaration to src/processor/processor.h
- [ ] T019 [US1] Allocate crossfade work buffers in `setupProcessing()` in src/processor/processor.cpp
- [ ] T020 [US1] Calculate crossfade increment using `DSP::crossfadeIncrement()` in setupProcessing()
- [ ] T021 [US1] Extract `processMode()` helper method from existing switch statement in src/processor/processor.cpp
- [ ] T022 [US1] Implement mode change detection and crossfade initiation in `process()` in src/processor/processor.cpp
- [ ] T023 [US1] Implement dual-mode processing during crossfade (both modes process simultaneously)
- [ ] T024 [US1] Implement equal-power crossfade blend using `DSP::equalPowerGains()` in process loop
- [ ] T025 [US1] Register mode_crossfade_tests.cpp in tests/CMakeLists.txt

### 3.4 Verification

- [ ] T026 [US1] Build plugin and verify no compiler warnings
- [ ] T027 [US1] Run all unit tests and verify they pass
- [ ] T028 [US1] Run pluginval at strictness level 5
- [ ] T029 [US1] **Manual test**: Load plugin in DAW, play audio, switch modes rapidly - verify no clicks

### 3.5 Cross-Platform Verification (MANDATORY)

- [ ] T030 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 3.6 Commit (MANDATORY)

- [ ] T031 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 (P1) complete - Click-free mode switching works for all modes

---

## Phase 4: User Story 2 - Preserve Audio Continuity (Priority: P2)

**Goal**: Ensure delay tail fades out naturally when switching modes rather than cutting off abruptly

**Independent Test**: Create long delay tail, switch modes, verify tail fades smoothly

**Acceptance Criteria**:
- FR-005: Wet signal smoothly transitioned; dry signal unaffected
- FR-007: Handle different buffer sizes/structures between modes
- SC-003: Audio RMS level does not spike more than 3dB during transition
- SC-004: All 110 mode-to-mode combinations pass click-free test

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T032 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T033 [P] [US2] Write unit tests verifying RMS level stability during crossfade (no >3dB spike) in tests/unit/processor/mode_crossfade_tests.cpp
- [ ] T034 [P] [US2] Write unit tests verifying dry signal unaffected during crossfade in tests/unit/processor/mode_crossfade_tests.cpp

### 4.3 Implementation for User Story 2

- [ ] T035 [US2] Review crossfade implementation for RMS stability - adjust if needed in src/processor/processor.cpp
- [ ] T036 [US2] Verify dry signal path is not affected by crossfade logic in src/processor/processor.cpp
- [ ] T037 [US2] **Manual test**: Test all 11 modes with long delay tails - verify smooth fade-out

### 4.4 Verification

- [ ] T038 [US2] Run all unit tests and verify they pass
- [ ] T039 [US2] Run pluginval at strictness level 5

### 4.5 Cross-Platform Verification (MANDATORY)

- [ ] T040 [US2] **Verify IEEE 754 compliance**: Check if any new test code uses IEEE 754 functions

### 4.6 Commit (MANDATORY)

- [ ] T041 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 complete - Click-free and smooth transitions work

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Optional improvements and edge case handling

- [ ] T042 [P] (Optional) Upgrade CrossfadingDelayLine from linear to equal-power crossfade in src/dsp/primitives/crossfading_delay_line.h
- [ ] T043 [P] (Optional) Add tests for CrossfadingDelayLine equal-power upgrade in tests/unit/primitives/crossfading_delay_line_tests.cpp
- [ ] T044 Run full test suite across all platforms (Windows, macOS, Linux)
- [ ] T045 Performance profiling - verify crossfade adds minimal CPU overhead

---

## Phase 6: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 6.1 Architecture Documentation Update

- [ ] T046 **Update ARCHITECTURE.md** with new components added by this spec:
  - Add `crossfade_utils.h` to Layer 0 Core Utilities section
  - Document `equalPowerGains()` and `crossfadeIncrement()` APIs
  - Note that CharacterProcessor now uses shared utility
  - Document crossfade state in Processor class

### 6.2 Final Commit

- [ ] T047 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 7: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed

### 7.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T048 **Review ALL FR-xxx requirements** from spec.md against implementation:
  - [ ] FR-001: Mode switching produces no clicks ✓/✗
  - [ ] FR-002: Crossfade applied ✓/✗
  - [ ] FR-003: Fade under 50ms ✓/✗
  - [ ] FR-004: Safe at any processing point ✓/✗
  - [ ] FR-005: Wet signal transitioned, dry unaffected ✓/✗
  - [ ] FR-006: Rapid switching stable ✓/✗
  - [ ] FR-007: Different buffer sizes handled ✓/✗ (verify MultiTap↔Digital and Granular↔Spectral transitions work correctly - crossfade isolates buffer differences)
  - [ ] FR-008: All 11 modes supported ✓/✗

- [ ] T049 **Review ALL SC-xxx success criteria**:
  - [ ] SC-001: Zero audible clicks ✓/✗
  - [ ] SC-002: Transition under 50ms ✓/✗
  - [ ] SC-003: RMS level stable (no >3dB spike) ✓/✗
  - [ ] SC-004: All 110 combinations pass ✓/✗
  - [ ] SC-005: Rapid switching (10/sec) stable ✓/✗

- [ ] T050 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 7.2 Fill Compliance Table in spec.md

- [ ] T051 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T052 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 7.3 Honest Self-Check

- [ ] T053 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 8: Final Completion

**Purpose**: Final commit and completion claim

- [ ] T054 **Verify all tests pass**
- [ ] T055 **Run pluginval final validation**
- [ ] T056 **Commit all spec work** to feature branch
- [ ] T057 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
    ↓
Phase 2 (Layer 0 Utility) ← BLOCKS ALL USER STORIES
    ↓
Phase 3 (US1: Click-Free) ← MVP
    ↓
Phase 4 (US2: Audio Continuity)
    ↓
Phase 5 (Polish - Optional)
    ↓
Phase 6 (Documentation)
    ↓
Phase 7 (Verification)
    ↓
Phase 8 (Completion)
```

### User Story Dependencies

- **User Story 1 (P1)**: Depends on Phase 2 (Layer 0) completion - Core click-free functionality
- **User Story 2 (P2)**: Depends on US1 - Builds on crossfade to add smooth fade-out quality

### Within Each Phase

- **TESTING-GUIDE check**: FIRST task
- **Tests FIRST**: Tests MUST be written and FAIL before implementation
- **Build & Verify**: After implementation
- **Cross-platform check**: Verify IEEE 754 compliance
- **Commit**: LAST task

### Parallel Opportunities

**Phase 2 (Layer 0)**:
- T002, T003, T004 can run in parallel (different test cases)

**Phase 3 (US1)**:
- T013, T014, T015, T016 can run in parallel (different test cases)

**Phase 4 (US2)**:
- T033, T034 can run in parallel (different test cases)

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Layer 0 Utility (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test click-free switching in DAW
5. Deploy/demo if ready

### Full Implementation

1. Complete MVP (Phases 1-3)
2. Add User Story 2 (Phase 4) for professional fade-out quality
3. Optional polish (Phase 5) for CrossfadingDelayLine upgrade
4. Documentation and verification (Phases 6-8)

---

## Summary

| Phase | Tasks | Description |
|-------|-------|-------------|
| 1 | T001 | Setup / Context verification |
| 2 | T002-T011 | Layer 0 crossfade utility (10 tasks) |
| 3 | T012-T031 | US1: Click-free switching (20 tasks) |
| 4 | T032-T041 | US2: Audio continuity (10 tasks) |
| 5 | T042-T045 | Polish (4 tasks) |
| 6 | T046-T047 | Documentation (2 tasks) |
| 7 | T048-T053 | Verification (6 tasks) |
| 8 | T054-T057 | Completion (4 tasks) |

**Total**: 57 tasks

---

## Notes

- [P] tasks = different files, no dependencies
- [US1/US2] label maps task to specific user story
- **MANDATORY**: Tests MUST FAIL before implementation (Principle XII)
- **MANDATORY**: Update ARCHITECTURE.md before completion (Principle XIII)
- **MANDATORY**: Honest verification before claiming complete (Principle XV)
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly
