# Tasks: Layer 0 Utilities (BlockContext, FastMath, Interpolation)

**Input**: Design documents from `/specs/017-layer0-utilities/`
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

**Purpose**: Project initialization - create file structure and update build system

- [ ] T001 Create src/dsp/core/note_value.h with NoteValue and NoteModifier enums (extracted from lfo.h pattern)
- [ ] T002 Add new test files to tests/CMakeLists.txt build configuration
- [ ] T003 Update src/dsp/primitives/lfo.h to include and use Layer 0 note_value.h enums

**Checkpoint**: Build system ready, NoteValue enums in Layer 0

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: NoteValue enums must be tested before BlockContext can use them

**⚠️ CRITICAL**: BlockContext depends on NoteValue - this phase BLOCKS User Story 1

- [ ] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T005 [P] Write failing tests for NoteValue enum in tests/unit/core/note_value_test.cpp
- [ ] T006 [P] Write failing tests for NoteModifier enum in tests/unit/core/note_value_test.cpp
- [ ] T007 [P] Write failing tests for getBeatsForNote() helper in tests/unit/core/note_value_test.cpp
- [ ] T008 Implement note_value.h to make all tests pass (copy from contracts/note_value.h)
- [ ] T009 Verify all NoteValue tests pass
- [ ] T010 Verify lfo.h still compiles after migration to Layer 0 NoteValue
- [ ] T011 **Verify IEEE 754 compliance**: Add tests/unit/core/note_value_test.cpp to -fno-fast-math list in tests/CMakeLists.txt (if NaN tests exist)
- [ ] T012 **Commit Foundational work**: NoteValue/NoteModifier extracted to Layer 0

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - BlockContext for Tempo Sync (Priority: P1) 🎯 MVP

**Goal**: Provide tempo and transport context for Layer 3 Delay Engine tempo sync

**Independent Test**: Create BlockContext with tempo data, verify tempoToSamples() produces correct sample counts for quarter notes, dotted eighths, triplets at various BPM/sample rate combinations.

**Requirements Covered**: FR-001 to FR-008, SC-004

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T013 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T014 [P] [US1] Write tests for BlockContext default values (FR-007) in tests/unit/core/block_context_test.cpp
- [ ] T015 [P] [US1] Write tests for BlockContext member access (FR-001 to FR-006) in tests/unit/core/block_context_test.cpp
- [ ] T016 [P] [US1] Write tests for tempoToSamples() basic calculations (FR-008) in tests/unit/core/block_context_test.cpp
- [ ] T017 [P] [US1] Write tests for tempoToSamples() with modifiers (dotted, triplet) in tests/unit/core/block_context_test.cpp
- [ ] T018 [P] [US1] Write tests for tempoToSamples() edge cases (tempo=0, sample rate=0) in tests/unit/core/block_context_test.cpp
- [ ] T019 [P] [US1] Write tests for samplesPerBeat() and samplesPerBar() helpers in tests/unit/core/block_context_test.cpp
- [ ] T020 [P] [US1] Write tests for constexpr usage of tempoToSamples() (SC-004) in tests/unit/core/block_context_test.cpp

### 3.3 Implementation for User Story 1

- [ ] T021 [US1] Create src/dsp/core/block_context.h with BlockContext struct (use contracts/block_context.h as reference)
- [ ] T022 [US1] Implement tempoToSamples() with NoteValue/NoteModifier support
- [ ] T023 [US1] Implement samplesPerBeat() and samplesPerBar() helper methods
- [ ] T024 [US1] Implement edge case handling (tempo clamping, sample rate zero check)
- [ ] T025 [US1] Verify all BlockContext tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T026 [US1] **Verify IEEE 754 compliance**: Add tests/unit/core/block_context_test.cpp to -fno-fast-math list in tests/CMakeLists.txt

### 3.5 Commit (MANDATORY)

- [ ] T027 [US1] **Commit completed User Story 1 work**: BlockContext with tempoToSamples()

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - FastMath for CPU-Critical Paths (Priority: P2)

**Goal**: Provide optimized approximations of sin, cos, tanh, exp for real-time audio processing

**Independent Test**: Compare fastTanh(x) against std::tanh(x) for range of inputs, verify within 0.5% error for |x|<3, and measure performance to confirm 2x speedup.

**Requirements Covered**: FR-009 to FR-016, SC-001 to SC-003

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T028 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T029 [P] [US2] Write tests for fastSin() accuracy (0.1% error over [-2π, 2π]) in tests/unit/core/fast_math_test.cpp
- [ ] T030 [P] [US2] Write tests for fastCos() accuracy (0.1% error over [-2π, 2π]) in tests/unit/core/fast_math_test.cpp
- [ ] T031 [P] [US2] Write tests for fastTanh() accuracy (0.5% for |x|<3, 1% for larger) in tests/unit/core/fast_math_test.cpp
- [ ] T032 [P] [US2] Write tests for fastExp() accuracy (0.5% for x in [-10, 10]) in tests/unit/core/fast_math_test.cpp
- [ ] T033 [P] [US2] Write tests for NaN handling in all FastMath functions (FR-015) in tests/unit/core/fast_math_test.cpp
- [ ] T034 [P] [US2] Write tests for infinity handling in all FastMath functions (FR-016) in tests/unit/core/fast_math_test.cpp
- [ ] T035 [P] [US2] Write tests for noexcept compliance (FR-013) in tests/unit/core/fast_math_test.cpp
- [ ] T036 [P] [US2] Write tests for constexpr usage (FR-014) in tests/unit/core/fast_math_test.cpp
- [ ] T037 [P] [US2] Write performance benchmark for fastTanh vs std::tanh (SC-001: 2x faster) in tests/unit/core/fast_math_test.cpp

### 4.3 Implementation for User Story 2

- [ ] T038 [P] [US2] Create src/dsp/core/fast_math.h with FastMath namespace structure
- [ ] T039 [P] [US2] Implement fastSin() with 5th-order minimax polynomial (use contracts/fast_math.h as reference)
- [ ] T040 [P] [US2] Implement fastCos() using fastSin(x + π/2)
- [ ] T041 [P] [US2] Implement fastTanh() with Padé approximant and saturation handling
- [ ] T042 [P] [US2] Implement fastExp() with range-reduced Taylor series
- [ ] T043 [US2] Implement NaN and infinity handling for all functions
- [ ] T044 [US2] Verify all FastMath tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T045 [US2] **Verify IEEE 754 compliance**: Add tests/unit/core/fast_math_test.cpp to -fno-fast-math list in tests/CMakeLists.txt

### 4.5 Commit (MANDATORY)

- [ ] T046 [US2] **Commit completed User Story 2 work**: FastMath utilities

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Standalone Interpolation Utilities (Priority: P3)

**Goal**: Extract and provide standalone interpolation functions for reuse across DSP components

**Independent Test**: Interpolate between known sample values, verify linearInterpolate(0, 1, 0.5) = 0.5, and that cubicHermite produces more accurate sine wave interpolation than linear.

**Requirements Covered**: FR-017 to FR-022, SC-005

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T047 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T048 [P] [US3] Write tests for linearInterpolate() basic cases in tests/unit/core/interpolation_test.cpp
- [ ] T049 [P] [US3] Write tests for linearInterpolate() boundary cases (t=0, t=1) in tests/unit/core/interpolation_test.cpp
- [ ] T050 [P] [US3] Write tests for cubicHermiteInterpolate() known values in tests/unit/core/interpolation_test.cpp
- [ ] T051 [P] [US3] Write tests for cubicHermiteInterpolate() sine wave accuracy in tests/unit/core/interpolation_test.cpp
- [ ] T052 [P] [US3] Write tests for lagrangeInterpolate() polynomial accuracy in tests/unit/core/interpolation_test.cpp
- [ ] T053 [P] [US3] Write tests for lagrangeInterpolate() linear data (should be exact) in tests/unit/core/interpolation_test.cpp
- [ ] T054 [P] [US3] Write tests for noexcept and constexpr compliance (FR-020, FR-021) in tests/unit/core/interpolation_test.cpp
- [ ] T055 [P] [US3] Write tests for boundary return values (FR-022: exact at t=0/1) in tests/unit/core/interpolation_test.cpp

### 5.3 Implementation for User Story 3

- [ ] T056 [P] [US3] Create src/dsp/core/interpolation.h with Interpolation namespace
- [ ] T057 [P] [US3] Implement linearInterpolate() (use contracts/interpolation.h as reference)
- [ ] T058 [P] [US3] Implement cubicHermiteInterpolate() with Catmull-Rom spline
- [ ] T059 [P] [US3] Implement lagrangeInterpolate() with 4-point polynomial
- [ ] T060 [US3] Verify all Interpolation tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T061 [US3] **Verify IEEE 754 compliance**: Add tests/unit/core/interpolation_test.cpp to -fno-fast-math list in tests/CMakeLists.txt

### 5.5 Commit (MANDATORY)

- [ ] T062 [US3] **Commit completed User Story 3 work**: Interpolation utilities

**Checkpoint**: All interpolation functions complete and tested

---

## Phase 6: User Story 4 - Constexpr Math Constants and Calculations (Priority: P3)

**Goal**: Verify all utilities work at compile-time for zero-cost abstractions

**Independent Test**: Use constexpr functions in static_assert or constexpr array initialization, verify compilation succeeds.

**Requirements Covered**: FR-014, FR-021, SC-002 (constexpr aspects)

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T063 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T064 [P] [US4] Write constexpr tests for fastSin/fastCos lookup table generation in tests/unit/core/fast_math_test.cpp
- [ ] T065 [P] [US4] Write constexpr tests for tempoToSamples() compile-time usage in tests/unit/core/block_context_test.cpp
- [ ] T066 [P] [US4] Write static_assert tests for interpolation constexpr compliance in tests/unit/core/interpolation_test.cpp

### 6.3 Implementation for User Story 4

- [ ] T067 [US4] Verify fastSin/fastCos work in constexpr context (may need algorithm adjustments)
- [ ] T068 [US4] Verify tempoToSamples() works in constexpr context
- [ ] T069 [US4] Verify all interpolation functions work in constexpr context
- [ ] T070 [US4] Verify all constexpr tests pass

### 6.4 Commit (MANDATORY)

- [ ] T071 [US4] **Commit completed User Story 4 work**: Constexpr verification

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Layer 0 compliance verification and integration validation

- [ ] T072 [P] Verify no #include of Layer 1+ headers in any new core files (FR-023)
- [ ] T073 [P] Verify all components use Iterum::DSP namespace (FR-024)
- [ ] T074 [P] Verify all headers have #pragma once (FR-025)
- [ ] T075 [P] Verify no allocations, blocking, or exceptions (FR-026)
- [ ] T076 Run all tests together: dsp_tests with [US1] [US2] [US3] [US4] tags
- [ ] T077 Run quickstart.md example scenarios manually to validate integration

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 8.1 Architecture Documentation Update

- [ ] T078 **Update ARCHITECTURE.md** with new Layer 0 components:
  - Add note_value.h: NoteValue/NoteModifier enums for tempo sync
  - Add block_context.h: BlockContext struct with tempoToSamples()
  - Add fast_math.h: fastSin, fastCos, fastTanh, fastExp
  - Add interpolation.h: linearInterpolate, cubicHermite, lagrange
  - Document "when to use this" for each component

### 8.2 Final Commit

- [ ] T079 **Commit ARCHITECTURE.md updates**
- [ ] T080 Verify all spec work is committed to feature branch

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T081 **Review ALL FR-xxx requirements** from spec.md against implementation:
  - [ ] FR-001 to FR-008: BlockContext verified
  - [ ] FR-009 to FR-016: FastMath verified
  - [ ] FR-017 to FR-022: Interpolation verified
  - [ ] FR-023 to FR-026: Layer 0 compliance verified
- [ ] T082 **Review ALL SC-xxx success criteria** and verify measurable targets achieved:
  - [ ] SC-001: fastTanh 2x faster than std::tanh (performance test)
  - [ ] SC-002: fastSin/fastCos 0.1% max error (accuracy test)
  - [ ] SC-003: fastTanh 0.5% max error for |x|<3 (accuracy test)
  - [ ] SC-004: tempoToSamples accurate to 1 sample (calculation test)
  - [ ] SC-005: Interpolation mathematically correct (precision test)
  - [ ] SC-006: Cross-platform (CI passes on Windows/macOS/Linux)
  - [ ] SC-007: No Layer 1+ dependencies (header audit)
- [ ] T083 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [ ] T084 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T085 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T086 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [ ] T087 **Commit all spec work** to feature branch
- [ ] T088 **Verify all tests pass**

### 10.2 Completion Claim

- [ ] T089 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories (NoteValue must exist)
- **User Stories (Phase 3+)**: All depend on Foundational phase completion
  - US1 (BlockContext) needs NoteValue from Foundational
  - US2 (FastMath) can proceed independently after Foundational
  - US3 (Interpolation) can proceed independently after Foundational
  - US4 (Constexpr) depends on US1, US2, US3 implementations
- **Polish (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Depends on Polish
- **Verification (Phase 9-10)**: Depends on Documentation

### User Story Dependencies

- **User Story 1 (P1)**: Depends on Foundational (NoteValue enums)
- **User Story 2 (P2)**: Can start after Foundational - No dependencies on US1
- **User Story 3 (P3)**: Can start after Foundational - No dependencies on US1/US2
- **User Story 4 (P3)**: Depends on US1, US2, US3 (verifies constexpr of all components)

### Within Each User Story

- **TESTING-GUIDE check**: FIRST task - verify testing guide is in context
- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- All test tasks marked [P] can run in parallel
- Implementation tasks follow tests
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Phase 2**: T005, T006, T007 can run in parallel (different test cases)
- **Phase 3 (US1)**: T014-T020 can run in parallel (different test categories)
- **Phase 4 (US2)**: T029-T037 can run in parallel, T038-T042 can run in parallel
- **Phase 5 (US3)**: T048-T055 can run in parallel, T056-T059 can run in parallel
- **Phase 6 (US4)**: T064-T066 can run in parallel
- **Phase 7**: T072-T075 can run in parallel (different verification aspects)

---

## Parallel Example: User Story 2 (FastMath)

```bash
# Launch all tests for FastMath together:
Task: "T029 [P] [US2] Write tests for fastSin() accuracy"
Task: "T030 [P] [US2] Write tests for fastCos() accuracy"
Task: "T031 [P] [US2] Write tests for fastTanh() accuracy"
Task: "T032 [P] [US2] Write tests for fastExp() accuracy"

# After tests written, launch implementations in parallel:
Task: "T039 [P] [US2] Implement fastSin()"
Task: "T040 [P] [US2] Implement fastCos()"
Task: "T041 [P] [US2] Implement fastTanh()"
Task: "T042 [P] [US2] Implement fastExp()"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (NoteValue - CRITICAL)
3. Complete Phase 3: User Story 1 (BlockContext)
4. **STOP and VALIDATE**: Test BlockContext independently
5. Deploy/demo if ready - Layer 3 Delay Engine can now use tempo sync

### Incremental Delivery

1. Complete Setup + Foundational → NoteValue ready
2. Add User Story 1 → BlockContext ready → Layer 3 can start
3. Add User Story 2 → FastMath ready → Saturation optimization possible
4. Add User Story 3 → Interpolation ready → Code deduplication complete
5. Add User Story 4 → Constexpr verified → Zero-cost abstractions confirmed
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers after Foundational phase:

- Developer A: User Story 1 (BlockContext)
- Developer B: User Story 2 (FastMath)
- Developer C: User Story 3 (Interpolation)
- All: User Story 4 (verification of their components)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Contracts in `specs/017-layer0-utilities/contracts/` provide reference implementations
