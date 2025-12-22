# Tasks: dB/Linear Conversion Utilities (Refactor)

**Input**: Design documents from `/specs/001-db-conversion/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/db_utils.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are organized by user story to enable independent implementation and testing.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

---

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US4)
- Include exact file paths in descriptions

> **Note**: User Story 3 (Handle Silence Safely) from spec.md is integrated into US2 tests (T022-T025) since silence handling is part of the gainToDb implementation. US3 acceptance scenarios are fully covered by the US2 test suite.

---

## Phase 1: Setup

**Purpose**: Create Layer 0 directory structure

- [ ] T001 Create `src/dsp/core/` directory (Layer 0 location)
- [ ] T002 Create `tests/unit/core/` directory for Layer 0 tests

---

## Phase 2: Foundational

**Purpose**: Configure test infrastructure for new module

- [ ] T003 Add `tests/unit/core/db_utils_test.cpp` to CMakeLists.txt test target
- [ ] T004 Verify build configuration compiles empty test file

**Checkpoint**: Build system ready for test-first development

---

## Phase 3: User Story 1 - dbToGain Function (Priority: P1)

**Goal**: Convert decibel values to linear gain multipliers using formula: gain = 10^(dB/20)

**Independent Test**: Verify 0dB = 1.0, -6dB ≈ 0.5, +6dB ≈ 2.0, -20dB = 0.1, NaN → 0.0

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T005 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T006 [US1] Create test file `tests/unit/core/db_utils_test.cpp` with Catch2 setup
- [ ] T007 [US1] Write test: `dbToGain(0.0f)` returns exactly `1.0f` (unity gain)
- [ ] T008 [US1] Write test: `dbToGain(-20.0f)` returns `0.1f`
- [ ] T009 [US1] Write test: `dbToGain(+20.0f)` returns `10.0f`
- [ ] T010 [US1] Write test: `dbToGain(-6.0206f)` returns approximately `0.5f`
- [ ] T011 [US1] Write test: `dbToGain(NaN)` returns `0.0f` (safe fallback)
- [ ] T012 [US1] Write test: Extreme values (+200dB, -200dB) return valid results without overflow

### 3.3 Implementation for User Story 1

- [ ] T013 [US1] Create `src/dsp/core/db_utils.h` with header guard and namespace `Iterum::DSP`
- [ ] T014 [US1] Implement `constexpr float dbToGain(float dB) noexcept` per contract
- [ ] T015 [US1] Verify all US1 tests pass: `ctest --test-dir build/tests -C Debug -R db_utils`

### 3.4 Commit (MANDATORY)

- [ ] T016 [US1] **Commit completed User Story 1 work** (dbToGain function with tests)

**Checkpoint**: dbToGain function fully tested and committed

---

## Phase 4: User Story 2 - gainToDb Function (Priority: P1)

**Goal**: Convert linear gain values to decibels using formula: dB = 20 * log10(gain), with -144dB floor

**Independent Test**: Verify 1.0 = 0dB, 0.5 ≈ -6dB, 0.0 = -144dB, negative → -144dB, NaN → -144dB

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T017 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T018 [US2] Write test: `gainToDb(1.0f)` returns exactly `0.0f` (unity gain)
- [ ] T019 [US2] Write test: `gainToDb(0.1f)` returns `-20.0f`
- [ ] T020 [US2] Write test: `gainToDb(10.0f)` returns `+20.0f`
- [ ] T021 [US2] Write test: `gainToDb(0.5f)` returns approximately `-6.02f`
- [ ] T022 [US2] Write test: `gainToDb(0.0f)` returns `-144.0f` (silence floor)
- [ ] T023 [US2] Write test: `gainToDb(-1.0f)` returns `-144.0f` (negative input)
- [ ] T024 [US2] Write test: `gainToDb(NaN)` returns `-144.0f` (safe fallback)
- [ ] T025 [US2] Write test: `gainToDb(1e-10f)` returns `-144.0f` (below floor)
- [ ] T026 [US2] Write test: `kSilenceFloorDb` constant equals `-144.0f`

### 4.3 Implementation for User Story 2

- [ ] T027 [US2] Add `constexpr float kSilenceFloorDb = -144.0f;` constant to `db_utils.h`
- [ ] T028 [US2] Implement `constexpr float gainToDb(float gain) noexcept` per contract
- [ ] T029 [US2] Verify all US2 tests pass: `ctest --test-dir build/tests -C Debug -R db_utils`

### 4.4 Commit (MANDATORY)

- [ ] T030 [US2] **Commit completed User Story 2 work** (gainToDb function with tests)

**Checkpoint**: gainToDb function fully tested and committed

---

## Phase 5: User Story 4 - Compile-Time Evaluation (Priority: P2)

**Goal**: Verify functions work in constexpr context for compile-time constant initialization

**Independent Test**: Code with `constexpr float x = dbToGain(-6.0f);` compiles successfully

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T031 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T032 [US4] Write test: `constexpr float gain = dbToGain(-6.0f);` compiles and equals runtime result
- [ ] T033 [US4] Write test: `constexpr float dB = gainToDb(0.5f);` compiles and equals runtime result
- [ ] T034 [US4] Write test: Constexpr initialization of `std::array` with converted values compiles

### 5.3 Implementation for User Story 4

- [ ] T035 [US4] Verify constexpr tests compile (implementation already done in US1/US2)
- [ ] T036 [US4] Verify all constexpr tests pass: `ctest --test-dir build/tests -C Debug -R db_utils`

### 5.4 Commit (MANDATORY)

- [ ] T037 [US4] **Commit completed User Story 4 work** (constexpr verification tests)

**Checkpoint**: Constexpr functionality verified and committed

---

## Phase 6: Migration & Integration

**Purpose**: Update existing code to use new utilities

### 6.1 Pre-Migration (MANDATORY)

- [ ] T038 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Migration Tests (MR-004 Compliance)

- [ ] T039 Write migration equivalence test: document old -80dB floor vs new -144dB floor behavior in `tests/unit/core/db_utils_test.cpp`

### 6.3 Migration Tasks

- [ ] T040 Update `src/dsp/dsp_utils.h`: Add `#include "core/db_utils.h"`
- [ ] T041 Update `src/dsp/dsp_utils.h`: Remove old `dBToLinear` function
- [ ] T042 Update `src/dsp/dsp_utils.h`: Remove old `linearToDb` function
- [ ] T043 Update `src/dsp/dsp_utils.h`: Remove old `kSilenceThreshold` constant
- [ ] T044 [P] Search codebase for any `VSTWork::DSP::dBToLinear` usages and update
- [ ] T045 [P] Search codebase for any `VSTWork::DSP::linearToDb` usages and update

### 6.4 Verification

- [ ] T046 Build full project: `cmake --build build --config Debug`
- [ ] T047 Run all tests: `ctest --test-dir build/tests -C Debug --output-on-failure`
- [ ] T048 Verify no compiler warnings related to dB utilities

### 6.5 Commit (MANDATORY)

- [ ] T049 **Commit completed migration work**

**Checkpoint**: All code migrated to new utilities, build passes

---

## Phase 7: Polish & Documentation

**Purpose**: Final cleanup and documentation

- [ ] T050 [P] Verify `contracts/db_utils.h` matches implementation
- [ ] T051 [P] Run `quickstart.md` examples to validate documentation accuracy
- [ ] T052 Update Layer 0 file header comment in `db_utils.h`: `// Layer 0: Core Utilities`
- [ ] T053 **Final commit**: All dB conversion refactor work complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - start immediately
- **Foundational (Phase 2)**: Depends on Setup completion
- **User Story 1 (Phase 3)**: Depends on Foundational - creates test file and dbToGain
- **User Story 2 (Phase 4)**: Depends on US1 (adds to same files, uses same test file)
- **User Story 4 (Phase 5)**: Depends on US1 and US2 (tests constexpr of existing functions)
- **Migration (Phase 6)**: Depends on all user stories complete
- **Polish (Phase 7)**: Depends on Migration complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational - creates core implementation file
- **User Story 2 (P1)**: Depends on US1 (adds to same `db_utils.h` file)
- **User Story 4 (P2)**: Depends on US1 and US2 (tests existing implementations)

### Within Each User Story

- **TESTING-GUIDE check**: FIRST task - verify testing guide is in context
- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation after tests
- **Verify tests pass**: After implementation
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

Within User Story 1 tests (Phase 3.2):
```
Task: T007 Write test: dbToGain(0.0f) returns exactly 1.0f
Task: T008 Write test: dbToGain(-20.0f) returns 0.1f
Task: T009 Write test: dbToGain(+20.0f) returns 10.0f
Task: T010 Write test: dbToGain(-6.0206f) returns approximately 0.5f
```

Within Migration (Phase 6.2):
```
Task: T043 Search codebase for VSTWork::DSP::dBToLinear usages
Task: T044 Search codebase for VSTWork::DSP::linearToDb usages
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (dbToGain)
4. **STOP and VALIDATE**: Test dbToGain independently
5. Can proceed with US2 or validate MVP

### Incremental Delivery

1. Setup + Foundational → Build ready
2. User Story 1 → dbToGain tested and committed
3. User Story 2 → gainToDb tested and committed
4. User Story 4 → constexpr verified
5. Migration → Old code removed
6. Polish → Documentation validated

### Recommended Single-Developer Flow

Execute phases sequentially:
1. Phase 1 (Setup) → Phase 2 (Foundational)
2. Phase 3 (US1: dbToGain) - complete with commit
3. Phase 4 (US2: gainToDb) - complete with commit
4. Phase 5 (US4: constexpr) - complete with commit
5. Phase 6 (Migration) - complete with commit
6. Phase 7 (Polish) - final commit

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Commit work at end of each user story
- Stop at any checkpoint to validate story independently
- This is a REFACTOR task - existing code in `dsp_utils.h` will be replaced
