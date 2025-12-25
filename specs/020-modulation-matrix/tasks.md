# Tasks: Modulation Matrix

**Input**: Design documents from `/specs/020-modulation-matrix/`
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

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `tests/CMakeLists.txt`
2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Create test infrastructure for ModulationMatrix

- [ ] T001 Create test file `tests/unit/systems/modulation_matrix_test.cpp` with Catch2 includes and test tags
- [ ] T002 Register test file in `tests/CMakeLists.txt` for dsp_tests target

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core types and infrastructure that ALL user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Pre-Implementation (MANDATORY)

- [ ] T003 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.2 Tests for Foundational Types (Write FIRST - Must FAIL)

- [ ] T004 [P] Write tests for ModulationMode enum (Bipolar, Unipolar values) in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T005 [P] Write tests for ModulationSource interface (abstract, getCurrentValue, getSourceRange) in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T006 [P] Write tests for ModulationDestination struct (id, minValue, maxValue, label) in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T007 [P] Write tests for ModulationRoute struct (sourceId, destinationId, depth, mode, enabled) in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T008 Write tests for ModulationMatrix prepare(), reset(), registerSource(), registerDestination() in `tests/unit/systems/modulation_matrix_test.cpp`

### 2.3 Implementation for Foundational Types

- [ ] T009 Create header file `src/dsp/systems/modulation_matrix.h` with file header comment (Layer 3, Constitution compliance)
- [ ] T010 [P] Implement ModulationMode enum (Bipolar=0, Unipolar=1) in `src/dsp/systems/modulation_matrix.h`
- [ ] T011 [P] Implement ModulationSource abstract interface in `src/dsp/systems/modulation_matrix.h`
- [ ] T012 [P] Implement ModulationDestination struct in `src/dsp/systems/modulation_matrix.h`
- [ ] T013 [P] Implement ModulationRoute struct (without smoother yet) in `src/dsp/systems/modulation_matrix.h`
- [ ] T014 Implement ModulationMatrix class shell with constants (kMaxSources=16, kMaxDestinations=16, kMaxRoutes=32) in `src/dsp/systems/modulation_matrix.h`
- [ ] T015 Implement prepare(sampleRate, maxBlockSize, maxRoutes) with pre-allocation in `src/dsp/systems/modulation_matrix.h` (FR-015)
- [ ] T016 Implement reset() to clear modulation state in `src/dsp/systems/modulation_matrix.h` (FR-016)
- [ ] T017 Implement registerSource(id, source) in `src/dsp/systems/modulation_matrix.h` (FR-001)
- [ ] T018 Implement registerDestination(id, minValue, maxValue, label) in `src/dsp/systems/modulation_matrix.h` (FR-002)
- [ ] T019 Verify all foundational tests pass

### 2.4 Cross-Platform Verification (MANDATORY)

- [ ] T020 **Verify IEEE 754 compliance**: Check if test file uses `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 2.5 Commit (MANDATORY)

- [ ] T021 **Commit completed Foundational work**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Route LFO to Delay Time (Priority: P1) 🎯 MVP

**Goal**: Enable basic source-to-destination modulation routing with depth control and bipolar mode

**Independent Test**: Create an LFO source, a delay time destination, connect them with a route, and verify the delay time varies according to LFO output scaled by depth

**Covers**: FR-003, FR-004, FR-008, FR-009, FR-014, FR-017, FR-018

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T022 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

- [ ] T023 [P] [US1] Write test: createRoute returns valid index for valid source/destination in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T024 [P] [US1] Write test: createRoute returns -1 for invalid source/destination in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T025 [P] [US1] Write test: process() reads source value and applies depth in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T026 [P] [US1] Write test: getModulatedValue returns base + modulation offset in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T027 [P] [US1] Write test: depth=0.0 results in no modulation in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T028 [P] [US1] Write test: depth=1.0 with bipolar source +1.0 gives full range modulation in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T029 [P] [US1] Write test: NaN source value treated as 0.0 (FR-018) in `tests/unit/systems/modulation_matrix_test.cpp`

### 3.3 Implementation for User Story 1

- [ ] T030 [US1] Implement createRoute(sourceId, destinationId, depth, mode) returning route index in `src/dsp/systems/modulation_matrix.h` (FR-003, FR-004)
- [ ] T031 [US1] Implement process(numSamples) to read sources and calculate modulation sums in `src/dsp/systems/modulation_matrix.h` (FR-008, FR-014)
- [ ] T032 [US1] Implement getModulatedValue(destinationId, baseValue) in `src/dsp/systems/modulation_matrix.h` (FR-009)
- [ ] T033 [US1] Add NaN handling for source values using detail::isNaN() in `src/dsp/systems/modulation_matrix.h` (FR-018)
- [ ] T034 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T035 [US1] **Verify IEEE 754 compliance**: If tests use NaN detection, add test file to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [ ] T036 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Multiple Routes to Same Destination (Priority: P2)

**Goal**: Enable multiple modulation sources to affect the same parameter, with proper summation and clamping

**Independent Test**: Create two sources, one destination, two routes, and verify the destination receives the sum of both modulations clamped to range

**Covers**: FR-006, FR-007

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T037 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T038 [P] [US2] Write test: two routes to same destination sum their contributions in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T039 [P] [US2] Write test: modulation clamped to destination min/max range in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T040 [P] [US2] Write test: opposing polarity routes partially cancel in `tests/unit/systems/modulation_matrix_test.cpp`

### 4.3 Implementation for User Story 2

- [ ] T041 [US2] Implement modulation accumulation array per destination in `src/dsp/systems/modulation_matrix.h` (FR-006)
- [ ] T042 [US2] Implement range clamping in getModulatedValue() in `src/dsp/systems/modulation_matrix.h` (FR-007)
- [ ] T043 [US2] Verify all US2 tests pass

### 4.4 Commit (MANDATORY)

- [ ] T044 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently

---

## Phase 5: User Story 3 - Unipolar Modulation Mode (Priority: P2)

**Goal**: Enable unipolar mode where bipolar [-1,+1] source is mapped to [0,1] before applying depth

**Independent Test**: Create a unipolar route and verify -1.0 source maps to 0.0 modulation, +1.0 maps to depth

**Covers**: FR-005

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T045 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T046 [P] [US3] Write test: unipolar mode with source -1.0 gives modulation 0.0 in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T047 [P] [US3] Write test: unipolar mode with source +1.0 gives modulation = depth in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T048 [P] [US3] Write test: unipolar mode with source 0.0 gives modulation = 0.5 * depth in `tests/unit/systems/modulation_matrix_test.cpp`

### 5.3 Implementation for User Story 3

- [ ] T049 [US3] Implement bipolar-to-unipolar conversion `(x + 1.0f) * 0.5f` in process() in `src/dsp/systems/modulation_matrix.h` (FR-005)
- [ ] T050 [US3] Verify all US3 tests pass

### 5.4 Commit (MANDATORY)

- [ ] T051 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Bipolar and unipolar modes both work

---

## Phase 6: User Story 4 - Smooth Depth Changes (Priority: P3)

**Goal**: Smooth depth parameter changes to prevent zipper noise using OnePoleSmoother

**Independent Test**: Change depth from 0.0 to 1.0 instantly and verify actual applied depth reaches 0.95 within 50ms

**Covers**: FR-011

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T052 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T053 [P] [US4] Write test: depth reaches 95% of target within 50ms (SC-003) in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T054 [P] [US4] Write test: smoothed depth applied sample-accurately during block in `tests/unit/systems/modulation_matrix_test.cpp`

### 6.3 Implementation for User Story 4

- [ ] T055 [US4] Add OnePoleSmoother to ModulationRoute struct (include smoother.h) in `src/dsp/systems/modulation_matrix.h`
- [ ] T056 [US4] Configure smoothers in prepare() with 20ms smoothing time in `src/dsp/systems/modulation_matrix.h`
- [ ] T057 [US4] Implement setRouteDepth(routeIndex, depth) that sets smoother target in `src/dsp/systems/modulation_matrix.h`
- [ ] T058 [US4] Implement getRouteDepth(routeIndex) returning current smoothed depth in `src/dsp/systems/modulation_matrix.h`
- [ ] T059 [US4] Update process() to advance depth smoothers and use smoothed depth in `src/dsp/systems/modulation_matrix.h` (FR-011)
- [ ] T060 [US4] Verify all US4 tests pass

### 6.4 Commit (MANDATORY)

- [ ] T061 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Depth changes are now smooth

---

## Phase 7: User Story 5 - Enable/Disable Individual Routes (Priority: P3)

**Goal**: Allow routes to be temporarily disabled without removing their configuration

**Independent Test**: Disable a route and verify it produces no modulation output

**Covers**: FR-010

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T062 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T063 [P] [US5] Write test: disabled route produces no modulation in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T064 [P] [US5] Write test: re-enabled route produces modulation with smoothing in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T065 [P] [US5] Write test: only enabled routes contribute to destination sum in `tests/unit/systems/modulation_matrix_test.cpp`

### 7.3 Implementation for User Story 5

- [ ] T066 [US5] Implement setRouteEnabled(routeIndex, enabled) in `src/dsp/systems/modulation_matrix.h` (FR-010)
- [ ] T067 [US5] Implement isRouteEnabled(routeIndex) in `src/dsp/systems/modulation_matrix.h`
- [ ] T068 [US5] Update process() to skip disabled routes in `src/dsp/systems/modulation_matrix.h`
- [ ] T069 [US5] Verify all US5 tests pass

### 7.4 Commit (MANDATORY)

- [ ] T070 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Routes can be enabled/disabled

---

## Phase 8: User Story 6 - Query Applied Modulation (Priority: P3)

**Goal**: Provide getCurrentModulation() for UI feedback

**Independent Test**: Query modulation for a destination and verify it matches expected calculation

**Covers**: FR-012

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T071 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T072 [P] [US6] Write test: getCurrentModulation returns expected value for single route in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T073 [P] [US6] Write test: getCurrentModulation returns sum for multiple routes in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T074 [P] [US6] Write test: getCurrentModulation returns 0.0 for destination with no routes in `tests/unit/systems/modulation_matrix_test.cpp`

### 8.3 Implementation for User Story 6

- [ ] T075 [US6] Implement getCurrentModulation(destinationId) in `src/dsp/systems/modulation_matrix.h` (FR-012)
- [ ] T076 [US6] Verify all US6 tests pass

### 8.4 Commit (MANDATORY)

- [ ] T077 [US6] **Commit completed User Story 6 work**

**Checkpoint**: UI can query modulation amounts

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Performance validation and edge case handling

### 9.1 Pre-Implementation (MANDATORY)

- [ ] T078 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 9.2 Additional Tests

- [ ] T079 [P] Write performance test: 16 routes process in <1% CPU at 44.1kHz (SC-001) in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T080 [P] Write test: 32 routes can be created (SC-007) in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T081 [P] Write test: depth clamped to [0, 1] range on setRouteDepth in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T082 [P] Write test: getModulatedValue accuracy within 0.0001 tolerance (SC-006) in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T083 [P] Write test: zero allocations during process() using allocation counter (SC-005) in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T084 [P] Write test: registerSource/registerDestination after prepare() is documented behavior (FR-013) in `tests/unit/systems/modulation_matrix_test.cpp`
- [ ] T085 [P] Write test: depth changes produce no audible clicks (SC-004 glitch-free) in `tests/unit/systems/modulation_matrix_test.cpp`

### 9.3 Implementation

- [ ] T086 Add query methods: getSourceCount(), getDestinationCount(), getRouteCount(), getSampleRate() in `src/dsp/systems/modulation_matrix.h`
- [ ] T087 Verify all tests pass

### 9.4 Commit (MANDATORY)

- [ ] T088 **Commit completed Polish work**

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T089 **Update ARCHITECTURE.md** with ModulationMatrix component:
  - Add to Layer 3: System Components section
  - Include: purpose, public API summary, file location (`src/dsp/systems/modulation_matrix.h`)
  - Add "when to use this" guidance
  - Add usage example

### 10.2 Final Commit

- [ ] T090 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed

### 11.1 Requirements Verification

- [ ] T091 **Review ALL FR-xxx requirements** (FR-001 through FR-018) from spec.md against implementation
- [ ] T092 **Review ALL SC-xxx success criteria** (SC-001 through SC-007) and verify measurable targets are achieved
- [ ] T093 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T094 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T095 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Final Commit

- [ ] T096 **Commit all spec work** to feature branch
- [ ] T097 **Verify all tests pass**: Run `dsp_tests.exe "[modulation]" --reporter compact`

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Stories (Phases 3-8)**: All depend on Foundational phase completion
  - US1 (P1): Can start immediately after Foundational
  - US2 (P2): Can start immediately after Foundational (adds to US1 functionality)
  - US3 (P2): Can start immediately after Foundational (independent of US2)
  - US4 (P3): Depends on US1 route creation
  - US5 (P3): Depends on US1 route creation
  - US6 (P3): Depends on US1 process/modulation calculation
- **Polish (Phase 9)**: Depends on all user stories
- **Documentation (Phase 10)**: Depends on Polish
- **Verification (Phase 11)**: Depends on Documentation

### Within Each User Story

1. **TESTING-GUIDE check**: FIRST task
2. **Tests FIRST**: Write failing tests before implementation
3. Implementation tasks (models → logic → integration)
4. **Verify tests pass**
5. **Commit**: LAST task

### Parallel Opportunities

- **Phase 2**: T004-T008 (tests) can run in parallel; T010-T013 (types) can run in parallel
- **Phase 3**: T023-T029 (tests) can run in parallel
- **Phase 4**: T038-T040 (tests) can run in parallel
- **Phase 5**: T046-T048 (tests) can run in parallel
- **Phase 6**: T053-T054 (tests) can run in parallel
- **Phase 7**: T063-T065 (tests) can run in parallel
- **Phase 8**: T072-T074 (tests) can run in parallel
- **Phase 9**: T079-T082 (tests) can run in parallel

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Single route modulation works
5. Commit and test before continuing

### Recommended Order

1. Setup + Foundational → Foundation ready
2. US1 (P1) → Basic routing works → Commit
3. US2 (P2) → Multi-route summation → Commit
4. US3 (P2) → Unipolar mode → Commit
5. US4 (P3) → Depth smoothing → Commit
6. US5 (P3) → Enable/disable → Commit
7. US6 (P3) → UI query → Commit
8. Polish → Performance verified → Commit
9. Documentation → ARCHITECTURE.md updated → Commit
10. Verification → All requirements verified → Claim completion

---

## Notes

- **Total Tasks**: 97
- **User Story Task Counts**:
  - Foundational: 19 tasks (T003-T021)
  - US1: 15 tasks (T022-T036)
  - US2: 8 tasks (T037-T044)
  - US3: 7 tasks (T045-T051)
  - US4: 10 tasks (T052-T061)
  - US5: 9 tasks (T062-T070)
  - US6: 7 tasks (T071-T077)
  - Polish: 11 tasks (T078-T088) - includes SC-005/FR-013/SC-004 tests
  - Documentation: 2 tasks (T089-T090)
  - Verification: 7 tasks (T091-T097)
- **Parallel Opportunities**: Tests within each phase can run in parallel
- **MVP Scope**: Complete through US1 for minimum viable modulation routing
- **Test Tags**: Use `[modulation]` tag for all tests, add `[US1]`, `[US2]`, etc. for story-specific filtering
