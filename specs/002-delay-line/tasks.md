# Tasks: Delay Line DSP Primitive

**Input**: Design documents from `/specs/002-delay-line/`
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

**Purpose**: Project initialization and directory structure

- [x] T001 Create primitives directory at src/dsp/primitives/
- [x] T002 Create test directory at tests/unit/primitives/
- [x] T003 Add delay_line_test.cpp to tests/CMakeLists.txt (dsp_tests target)

**Checkpoint**: Directory structure ready for implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core class structure that MUST be complete before user stories

**CRITICAL**: No user story work can begin until this phase is complete

- [x] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [x] T005 Create initial DelayLine class skeleton in src/dsp/primitives/delay_line.h
  - Include Layer 1 header comment
  - Add namespace Iterum::DSP
  - Add class with default constructor/destructor
  - Add prepare(), reset(), write() method declarations
  - Mark all methods noexcept
- [x] T006 Write basic prepare/reset tests in tests/unit/primitives/delay_line_test.cpp
  - Test prepare() allocates buffer
  - Test reset() clears buffer to silence
  - Test prepare() with different sample rates
- [x] T007 Implement prepare() and reset() methods
  - Power-of-2 buffer sizing with nextPowerOf2()
  - std::vector resize and fill with zeros
  - Store mask_, sampleRate_, maxDelaySamples_
- [x] T008 Verify foundational tests pass
- [x] T009 **Commit foundational work**

**Checkpoint**: Foundation ready - DelayLine can prepare and reset

---

## Phase 3: User Story 1 - Basic Fixed Delay (Priority: P1) MVP

**Goal**: DSP developer can create a delay line with integer sample delay

**Independent Test**: Write known signal, read at fixed offset, verify output matches delayed input

### 3.1 Pre-Implementation (MANDATORY)

- [x] T010 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T011 [US1] Write tests for write() method in tests/unit/primitives/delay_line_test.cpp
  - Test write advances writeIndex
  - Test buffer wraps correctly at boundary
- [x] T012 [US1] Write tests for read() integer delay in tests/unit/primitives/delay_line_test.cpp
  - Test read(0) returns current sample (just written)
  - Test read(N) returns sample written N samples ago
  - Test read at maximum delay returns oldest sample
- [x] T013 [US1] Write edge case tests in tests/unit/primitives/delay_line_test.cpp
  - Test delay clamped to [0, maxDelay]
  - Test negative delay clamped to 0
  - Test delay > maxDelay clamped to maxDelay
- [x] T013a [US1] Write mono operation test (FR-011) in tests/unit/primitives/delay_line_test.cpp
  - Verify DelayLine handles single channel only
  - Document that stereo requires two DelayLine instances

### 3.3 Implementation for User Story 1

- [x] T014 [US1] Implement write() method in src/dsp/primitives/delay_line.h
  - Store sample at writeIndex_
  - Advance writeIndex with bitwise AND wrap
- [x] T015 [US1] Implement read(size_t) method in src/dsp/primitives/delay_line.h
  - Calculate read index: (writeIndex_ - delaySamples) & mask_
  - Clamp delay to valid range
  - Return buffer sample
- [x] T016 [US1] Verify all US1 tests pass
- [x] T017 [US1] Run full test suite to ensure no regressions

### 3.4 Commit (MANDATORY)

- [x] T018 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic fixed delay works - can write and read at integer offsets

---

## Phase 4: User Story 2 - Linear Interpolation (Priority: P2)

**Goal**: DSP developer can read at fractional delay positions with linear interpolation for modulated delays

**Also covers**: User Story 4 (Modulated Delay Time) - same mechanism, T021 tests modulation scenarios

**Independent Test**: Read at fractional position, verify output matches linear interpolation of adjacent samples

### 4.1 Pre-Implementation (MANDATORY)

- [x] T019 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [x] T020 [US2] Write tests for readLinear() in tests/unit/primitives/delay_line_test.cpp
  - Test readLinear(0.5) between samples [0.0, 1.0] returns 0.5
  - Test readLinear(1.25) interpolates correctly
  - Test readLinear at integer position matches read()
- [x] T021 [US2] Write modulation tests (US4 coverage) in tests/unit/primitives/delay_line_test.cpp
  - Test smooth output when delay time changes gradually
  - Test no discontinuities during delay sweep
  - Test with LFO-like delay time modulation

### 4.3 Implementation for User Story 2

- [x] T022 [US2] Implement readLinear(float) method in src/dsp/primitives/delay_line.h
  - Split delay into integer and fractional parts
  - Read two adjacent samples
  - Apply linear interpolation: y0 + frac * (y1 - y0)
- [x] T023 [US2] Verify all US2 tests pass
- [x] T024 [US2] Run full test suite to ensure no regressions

### 4.4 Commit (MANDATORY)

- [x] T025 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Fractional delay with linear interpolation works - ready for chorus/flanger effects

---

## Phase 5: User Story 3 - Allpass Interpolation (Priority: P3)

**Goal**: DSP developer can use allpass interpolation for feedback loops with unity gain at all frequencies

**Independent Test**: Process sinusoid through allpass-interpolated delay, verify output amplitude equals input

### 5.1 Pre-Implementation (MANDATORY)

- [x] T026 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [x] T027 [US3] Write tests for readAllpass() in tests/unit/primitives/delay_line_test.cpp
  - Test allpass at integer position matches read()
  - Test allpass preserves amplitude (unity gain)
  - Test allpass coefficient calculation: (1-frac)/(1+frac)
- [x] T028 [US3] Write unity gain verification tests in tests/unit/primitives/delay_line_test.cpp
  - Process sine wave at multiple frequencies
  - Verify output RMS matches input RMS (within tolerance)

### 5.3 Implementation for User Story 3

- [x] T029 [US3] Implement readAllpass(float) method in src/dsp/primitives/delay_line.h
  - Calculate allpass coefficient: a = (1 - frac) / (1 + frac)
  - Apply allpass formula: y = x0 + a * (allpassState_ - x1)
  - Update allpassState_ with output
- [x] T030 [US3] Add allpassState_ member and reset in reset() method
- [x] T031 [US3] Verify all US3 tests pass
- [x] T032 [US3] Run full test suite to ensure no regressions

### 5.4 Commit (MANDATORY)

- [x] T033 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Allpass interpolation works - ready for feedback delay networks

---

## Phase 6: User Story 5 - Real-Time Safety (Priority: P1)

**Goal**: Verify all methods are real-time safe (noexcept, no allocations in process path)

**Independent Test**: Code inspection and memory profiler analysis confirm zero allocations

### 6.1 Pre-Implementation (MANDATORY)

- [x] T034 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 5 (Verification)

- [x] T035 [US5] Write real-time safety tests in tests/unit/primitives/delay_line_test.cpp
  - Verify all public methods are noexcept (static_assert or compile test)
  - Test no allocations during write/read sequence (if allocation detector available)
- [x] T036 [US5] Add query method tests
  - Test maxDelaySamples() returns correct value
  - Test sampleRate() returns configured rate
- [x] T036a [US5] Add constexpr compile-time test (NFR-003) in tests/unit/primitives/delay_line_test.cpp
  - Verify constexpr construction where applicable
  - Test compile-time evaluation of utility functions

### 6.3 Implementation for User Story 5

- [x] T037 [US5] Implement maxDelaySamples() query method in src/dsp/primitives/delay_line.h
- [x] T038 [US5] Implement sampleRate() query method in src/dsp/primitives/delay_line.h
- [x] T039 [US5] Verify all noexcept specifications are correct
- [x] T040 [US5] Verify all US5 tests pass

### 6.4 Commit (MANDATORY)

- [x] T041 [US5] **Commit completed User Story 5 work**

**Checkpoint**: All methods verified real-time safe

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Final verification and documentation

- [x] T042 Run complete test suite and verify all tests pass
- [x] T043 Verify quickstart.md examples compile and work
- [x] T044 [P] Add additional edge case tests if any gaps identified
  - No gaps identified - comprehensive test coverage achieved (436 assertions)
- [x] T044a [P] Add O(1) performance benchmark test (NFR-001) in tests/unit/primitives/delay_line_test.cpp
  - Verify read/write time is constant regardless of buffer size
  - Measure time for small (1K) vs large (1M) buffer operations
  - Document results (not strict pass/fail, informational)
- [x] T045 Code review for constitution compliance (Principles II, III, IX, X, XII)
  - Principle II (RT Safety): All methods noexcept, no allocations in process path ✓
  - Principle III (Modern C++): RAII, value semantics, C++20 ✓
  - Principle IX (Layered Architecture): Layer 1 primitive, depends only on Layer 0/stdlib ✓
  - Principle X (Testing): Comprehensive tests with 436 assertions ✓
  - Principle XII (Test-First): Tests written before implementation ✓

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 8.1 Architecture Documentation Update

- [x] T046 **Update ARCHITECTURE.md** with DelayLine component:
  - Add to Layer 1 (DSP Primitives) section
  - Include: purpose, public API summary, file location
  - Add "when to use this" guidance (fixed delay, modulated delay, feedback loops)
  - Add usage examples for each interpolation mode
  - Verify no duplicate functionality was introduced

### 8.2 Final Commit

- [x] T047 **Commit ARCHITECTURE.md updates**
- [x] T048 Verify all spec work is committed to feature branch
  - 6 commits on 002-delay-line branch
  - Working tree clean, no uncommitted changes
- [x] T049 Run final CI verification (build + test on all platforms)
  - Build: Success (Debug configuration)
  - Tests: 436 assertions in 50 test cases - ALL PASSED

**Checkpoint**: Spec implementation complete - ARCHITECTURE.md reflects all new functionality

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **US1 (Phase 3)**: Depends on Foundational - MVP, complete first
- **US2 (Phase 4)**: Depends on US1 (uses write/read infrastructure)
- **US3 (Phase 5)**: Depends on US1 (uses write/read infrastructure)
- **US5 (Phase 6)**: Can run after US1-US3, verifies RT safety
- **Polish (Phase 7)**: Depends on all user stories
- **Documentation (Phase 8)**: Final phase

### User Story Dependencies

```
Phase 1 (Setup)
     │
     ▼
Phase 2 (Foundational) ──────────────────────────────┐
     │                                                │
     ▼                                                │
Phase 3 (US1: Basic Fixed Delay) ◄── MVP            │
     │                                                │
     ├────────────────┬───────────────┐              │
     ▼                ▼               ▼              │
Phase 4 (US2)    Phase 5 (US3)   Phase 6 (US5)      │
(Linear Interp)  (Allpass)       (RT Safety)        │
     │                │               │              │
     └────────────────┴───────────────┘              │
                      │                              │
                      ▼                              │
              Phase 7 (Polish)                       │
                      │                              │
                      ▼                              │
              Phase 8 (Documentation) ◄──────────────┘
```

### Parallel Opportunities

After Phase 2 (Foundational) completes:
- US2 (Linear Interpolation) and US3 (Allpass) can run in parallel
- Both depend only on US1 infrastructure

Within each user story:
- Tests marked [P] can be written in parallel
- Implementation tasks must be sequential within story

---

## Parallel Example: After US1 Complete

```bash
# These can run in parallel after US1 is done:
Agent A: "Implement US2 - Linear Interpolation"
Agent B: "Implement US3 - Allpass Interpolation"

# Both use the same write/read infrastructure from US1
# No file conflicts (all implementation in same header, but different methods)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Basic Fixed Delay)
4. **STOP and VALIDATE**: Test with simple delay scenario
5. This alone enables basic delay effects

### Incremental Delivery

1. **MVP (US1)**: Basic fixed delay → enables simple delay effects
2. **+US2**: Linear interpolation → enables chorus/flanger/vibrato
3. **+US3**: Allpass interpolation → enables feedback delay networks
4. **+US5**: RT safety verification → production-ready
5. Each increment adds capability without breaking previous functionality

---

## Summary

| Phase | User Story | Tasks | Priority | Parallel |
|-------|------------|-------|----------|----------|
| 1 | Setup | T001-T003 | - | Yes |
| 2 | Foundational | T004-T009 | - | No |
| 3 | US1: Fixed Delay | T010-T018 (+T013a) | P1 (MVP) | No |
| 4 | US2: Linear Interp | T019-T025 | P2 | After US1 |
| 5 | US3: Allpass | T026-T033 | P3 | After US1 |
| 6 | US5: RT Safety | T034-T041 (+T036a) | P1 | After US1-3 |
| 7 | Polish | T042-T045 (+T044a) | - | Partial |
| 8 | Documentation | T046-T049 | - | No |

**Total Tasks**: 52 (49 original + 3 NFR coverage tasks)
**MVP Tasks**: 19 (Phases 1-3, including T013a)
**Independent Test Points**: After each user story phase

---

## Notes

- All implementation in single header: `src/dsp/primitives/delay_line.h`
- All tests in single file: `tests/unit/primitives/delay_line_test.cpp`
- No external dependencies beyond standard library
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
