# Tasks: Envelope Follower

**Input**: Design documents from `/specs/010-envelope-follower/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/

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

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Project initialization and test file structure

- [ ] T001 Create test file `tests/unit/processors/envelope_follower_test.cpp` with initial includes and test structure
- [ ] T002 Add `envelope_follower_test.cpp` to `tests/CMakeLists.txt` in dsp_tests target
- [ ] T003 Verify API contract matches plan.md in `specs/010-envelope-follower/contracts/envelope_follower.h`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.1 Foundational Tests (Write FIRST - Must FAIL)

- [ ] T005 [P] Write tests for DetectionMode enum values in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T006 [P] Write tests for EnvelopeFollower constants (kMinAttackMs, kMaxAttackMs, etc.) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T007 [P] Write tests for prepare() and reset() lifecycle methods in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T008 [P] Write tests for parameter getters/setters (setMode, setAttackTime, setReleaseTime) with clamping behavior in `tests/unit/processors/envelope_follower_test.cpp`

### 2.2 Foundational Implementation

- [ ] T009 Create header file `src/dsp/processors/envelope_follower.h` with includes and namespace
- [ ] T010 Implement DetectionMode enum (Amplitude=0, RMS=1, Peak=2) in `src/dsp/processors/envelope_follower.h`
- [ ] T011 Implement constants (kMinAttackMs=0.1f, kMaxAttackMs=500.0f, kMinReleaseMs=1.0f, kMaxReleaseMs=5000.0f, kDefaultAttackMs=10.0f, kDefaultReleaseMs=100.0f, kMinSidechainHz=20.0f, kMaxSidechainHz=500.0f, kDefaultSidechainHz=80.0f) in `src/dsp/processors/envelope_follower.h`
- [ ] T012 Implement EnvelopeFollower class skeleton with member variables (mode_, attackTimeMs_, releaseTimeMs_, envelope_, attackCoeff_, releaseCoeff_, sampleRate_, sidechainEnabled_, sidechainCutoffHz_, sidechainFilter_) in `src/dsp/processors/envelope_follower.h` — NOTE: sidechain members declared here for header-only design; functionality implemented in US5
- [ ] T013 Implement prepare(double sampleRate, size_t maxBlockSize) noexcept in `src/dsp/processors/envelope_follower.h`
- [ ] T014 Implement reset() noexcept in `src/dsp/processors/envelope_follower.h`
- [ ] T015 Implement all parameter setters with clamping (setMode, setAttackTime, setReleaseTime) and coefficient recalculation in `src/dsp/processors/envelope_follower.h`
- [ ] T016 Implement all parameter getters (getMode, getAttackTime, getReleaseTime, getCurrentValue) in `src/dsp/processors/envelope_follower.h`
- [ ] T017 Verify all foundational tests pass
- [ ] T018 **Verify IEEE 754 compliance**: Check if test file uses NaN functions, add to `-fno-fast-math` list in `tests/CMakeLists.txt` if needed
- [ ] T019 **Commit completed Foundational phase work**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Envelope Tracking (Priority: P1) MVP

**Goal**: Track amplitude envelope with configurable attack/release times using Amplitude mode (full-wave rectification + asymmetric smoothing)

**Independent Test**: Feed step input (0→1→0), verify envelope rises within attack time constant (63% in attack time) and decays within release time constant (37% in release time)

**Requirements Covered**: FR-001, FR-005, FR-006, FR-007, FR-011, FR-012, FR-016, FR-017, FR-018, FR-019, FR-020, FR-021, SC-001, SC-006

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T020 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T021 [P] [US1] Write test for Amplitude mode attack time constant accuracy (63% of target within attack time) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T022 [P] [US1] Write test for Amplitude mode release time constant accuracy (37% within release time) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T023 [P] [US1] Write test for processSample() returns envelope value and advances state in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T024 [P] [US1] Write test for process(input, output, numSamples) block processing in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T025 [P] [US1] Write test for process(buffer, numSamples) in-place processing in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T026 [P] [US1] Write test for getCurrentValue() returns current envelope without advancing in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T027 [P] [US1] Write test for time constant scaling across sample rates (44.1kHz, 96kHz, 192kHz) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T028 [P] [US1] Write test for envelope settles to zero within 10x release time after silence (SC-006) in `tests/unit/processors/envelope_follower_test.cpp`

### 3.3 Implementation for User Story 1

- [ ] T029 [US1] Implement calculateCoefficient(float timeMs) helper using formula: `exp(-1.0 / (timeMs * 0.001 * sampleRate))` in `src/dsp/processors/envelope_follower.h`
- [ ] T030 [US1] Implement processSample(float input) for Amplitude mode with asymmetric smoothing (attack coeff when rising, release when falling) in `src/dsp/processors/envelope_follower.h`
- [ ] T031 [US1] Implement process(const float* input, float* output, size_t numSamples) calling processSample in loop in `src/dsp/processors/envelope_follower.h`
- [ ] T032 [US1] Implement process(float* buffer, size_t numSamples) in-place variant in `src/dsp/processors/envelope_follower.h`
- [ ] T033 [US1] Add denormal flushing using detail::flushDenormal() from db_utils.h in processSample output in `src/dsp/processors/envelope_follower.h`
- [ ] T034 [US1] Verify all User Story 1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T035 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [ ] T036 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - RMS Level Detection (Priority: P2)

**Goal**: Implement RMS detection mode that tracks perceptually-meaningful level (squared → smooth → sqrt)

**Independent Test**: Process 0dB sine wave, verify output stabilizes to ~0.707; process 0dB square wave, verify output stabilizes to ~1.0

**Requirements Covered**: FR-002, SC-002

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T037 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T038 [P] [US2] Write test for RMS mode with 0dB sine wave outputs ~0.707 (within 1% per SC-002) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T039 [P] [US2] Write test for RMS mode with 0dB square wave outputs ~1.0 in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T040 [P] [US2] Write test for RMS mode with pink noise shows smooth tracking (minimal fluctuation) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T041 [P] [US2] Write test for RMS mode attack/release behavior in `tests/unit/processors/envelope_follower_test.cpp`

### 4.3 Implementation for User Story 2

- [ ] T042 [US2] Extend processSample() to handle DetectionMode::RMS (square input, smooth, sqrt output) in `src/dsp/processors/envelope_follower.h`
- [ ] T043 [US2] Add squaredEnvelope_ member variable for RMS mode state in `src/dsp/processors/envelope_follower.h`
- [ ] T044 [US2] Update reset() to clear squaredEnvelope_ in `src/dsp/processors/envelope_follower.h`
- [ ] T045 [US2] Verify all User Story 2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T046 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 4.5 Commit (MANDATORY)

- [ ] T047 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Peak Level Detection (Priority: P3)

**Goal**: Implement Peak detection mode that captures fast transients (instant attack option, configurable release)

**Independent Test**: Send single-sample impulse of 1.0, verify envelope immediately captures 1.0; verify release decays appropriately

**Requirements Covered**: FR-003, SC-003

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T048 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T049 [P] [US3] Write test for Peak mode with single-sample impulse captures peak immediately (SC-003) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T050 [P] [US3] Write test for Peak mode release behavior (envelope decays after peak) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T051 [P] [US3] Write test for Peak mode with sharp transients (no missed peaks - output >= input magnitude) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T052 [P] [US3] Write test for Peak mode attack time behavior (when attack > 0) in `tests/unit/processors/envelope_follower_test.cpp`

### 5.3 Implementation for User Story 3

- [ ] T053 [US3] Extend processSample() to handle DetectionMode::Peak with instant attack when attackTimeMs_ <= kMinAttackMs in `src/dsp/processors/envelope_follower.h`
- [ ] T054 [US3] Implement Peak mode logic: if |input| > envelope_, capture immediately; else apply release smoothing in `src/dsp/processors/envelope_follower.h`
- [ ] T055 [US3] Verify all User Story 3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T056 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 5.5 Commit (MANDATORY)

- [ ] T057 [US3] **Commit completed User Story 3 work**

**Checkpoint**: All detection modes (Amplitude, RMS, Peak) should now work independently

---

## Phase 6: User Story 4 - Smooth Parameter Changes (Priority: P4)

**Goal**: Ensure attack/release time changes and mode changes during processing produce no audible clicks or discontinuities

**Independent Test**: Change parameters during sustained audio, verify envelope output has no discontinuities > 0.01

**Requirements Covered**: FR-004, SC-008

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T058 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T059 [P] [US4] Write test for attack time change during processing produces no discontinuity > 0.01 (SC-008) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T060 [P] [US4] Write test for release time change during processing produces no discontinuity > 0.01 in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T061 [P] [US4] Write test for mode change (Amplitude → RMS) during processing produces smooth transition in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T062 [P] [US4] Write test for mode change (RMS → Peak) during processing produces smooth transition in `tests/unit/processors/envelope_follower_test.cpp`

### 6.3 Implementation for User Story 4

- [ ] T063 [US4] Ensure setAttackTime/setReleaseTime update coefficients without resetting envelope state in `src/dsp/processors/envelope_follower.h`
- [ ] T064 [US4] Ensure setMode() preserves envelope_ value (no discontinuity) in `src/dsp/processors/envelope_follower.h`
- [ ] T065 [US4] Add smooth transition logic for mode changes that affect state representation (RMS squaredEnvelope_ vs envelope_) in `src/dsp/processors/envelope_follower.h`
- [ ] T066 [US4] Verify all User Story 4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T067 [US4] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 6.5 Commit (MANDATORY)

- [ ] T068 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Parameter changes should now be smooth and click-free

---

## Phase 7: User Story 5 - Pre-filtering Option (Priority: P5)

**Goal**: Add optional highpass sidechain filter to prevent low-frequency content from dominating envelope

**Independent Test**: Process bass-heavy material with filter enabled, verify envelope responds primarily to mid/high content; disable filter and verify bass transients trigger envelope

**Requirements Covered**: FR-008, FR-009, FR-010, FR-013 (latency), SC-005

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T069 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T070 [P] [US5] Write test for sidechain filter enabled attenuates bass (<cutoff) before detection in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T071 [P] [US5] Write test for sidechain filter disabled passes all frequencies in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T072 [P] [US5] Write test for setSidechainCutoff() clamps to [20, 500] Hz and reconfigures filter in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T073 [P] [US5] Write test for setSidechainEnabled() toggles filter without clicks in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T074 [P] [US5] Write test for getLatency() returns 0 when sidechain disabled, appropriate value when enabled (SC-005) in `tests/unit/processors/envelope_follower_test.cpp`

### 7.3 Implementation for User Story 5

- [ ] T075 [US5] Add Biquad sidechainFilter_ member configured as FilterType::Highpass in `src/dsp/processors/envelope_follower.h`
- [ ] T076 [US5] Implement setSidechainEnabled(bool enabled) noexcept in `src/dsp/processors/envelope_follower.h`
- [ ] T077 [US5] Implement setSidechainCutoff(float hz) noexcept with clamping and filter reconfiguration in `src/dsp/processors/envelope_follower.h`
- [ ] T078 [US5] Implement isSidechainEnabled() and getSidechainCutoff() getters in `src/dsp/processors/envelope_follower.h`
- [ ] T079 [US5] Update processSample() to apply sidechainFilter_ before detection when enabled in `src/dsp/processors/envelope_follower.h`
- [ ] T080 [US5] Update prepare() to configure sidechainFilter_ with sample rate in `src/dsp/processors/envelope_follower.h`
- [ ] T081 [US5] Update reset() to reset sidechainFilter_ state in `src/dsp/processors/envelope_follower.h`
- [ ] T082 [US5] Implement getLatency() noexcept returning 0 (Biquad is zero-latency) in `src/dsp/processors/envelope_follower.h`
- [ ] T083 [US5] Verify all User Story 5 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T084 [US5] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 7.5 Commit (MANDATORY)

- [ ] T085 [US5] **Commit completed User Story 5 work**

**Checkpoint**: All user stories complete - full EnvelopeFollower functionality implemented

---

## Phase 8: Edge Cases & Robustness

**Purpose**: Handle edge cases and ensure robustness across all modes

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T086 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 8.2 Edge Case Tests (Write FIRST - Must FAIL)

- [ ] T087 [P] Write test for NaN input handling (output remains valid) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T088 [P] Write test for Inf input handling (clamped appropriately) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T089 [P] Write test for denormalized numbers flushed to zero (SC-007) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T090 [P] Write test for silent input (all zeros) envelope decays to zero and remains stable in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T091 [P] Write test for extreme attack time (0.1ms minimum) behavior in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T092 [P] Write test for extreme release time (5000ms maximum) behavior in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T093 [P] Write test for output range [0.0, 1.0+] with >0dBFS input (FR-011) in `tests/unit/processors/envelope_follower_test.cpp`
- [ ] T093b [P] Write test for output stability (FR-012): envelope does not oscillate or ring after step response in `tests/unit/processors/envelope_follower_test.cpp`

### 8.3 Edge Case Implementation

- [ ] T094 Add NaN input detection using `detail::isNaN()` from `dsp/core/db_utils.h` and substitute with 0.0 in processSample() in `src/dsp/processors/envelope_follower.h`
- [ ] T095 Add Inf input clamping in processSample() in `src/dsp/processors/envelope_follower.h`
- [ ] T096 Verify denormal flushing is applied in all code paths in `src/dsp/processors/envelope_follower.h`
- [ ] T097 Verify all edge case tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T098 **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 8.5 Commit (MANDATORY)

- [ ] T099 **Commit completed Edge Cases work**

**Checkpoint**: Edge cases handled, robustness verified

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final quality improvements

- [ ] T100 [P] Run quickstart.md test scenarios and verify behavior matches documentation
- [ ] T101 [P] Verify all 24 functional requirements (FR-001 to FR-024) are implemented
- [ ] T102 [P] Verify all 8 success criteria (SC-001 to SC-008) are met
- [ ] T103 Performance check: verify < 0.1% CPU at 44.1kHz stereo (SC-004)
- [ ] T104 Code review: verify noexcept on all process methods (FR-019)
- [ ] T105 Code review: verify no memory allocations in process path (FR-020)
- [ ] T106 Code review: verify O(N) complexity (FR-021)
- [ ] T107 Verify Layer 2 placement in src/dsp/processors/ (FR-022)
- [ ] T108 Verify only Layer 0/1 dependencies (FR-023)
- [ ] T109 Verify independently testable without VST infrastructure (FR-024)

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T110 **Update ARCHITECTURE.md** with EnvelopeFollower component:
  - Add entry to Layer 2 (DSP Processors) section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples showing basic envelope tracking
  - Verify no duplicate functionality was introduced

### 10.2 Final Commit

- [ ] T111 **Commit ARCHITECTURE.md updates**
- [ ] T112 Verify all spec work is committed to feature branch

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T113 **Review ALL FR-xxx requirements** from spec.md against implementation
- [ ] T114 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved
- [ ] T115 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T116 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T117 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T118 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [ ] T119 **Commit all spec work** to feature branch
- [ ] T120 **Verify all tests pass**

### 12.2 Completion Claim

- [ ] T121 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories can proceed in parallel or sequentially (P1 → P2 → P3 → P4 → P5)
  - Recommended: Complete P1 first as MVP, then add P2-P5 incrementally
- **Edge Cases (Phase 8)**: Depends on all user stories being implemented
- **Polish (Phase 9)**: Depends on Edge Cases
- **Documentation (Phase 10)**: Depends on Polish
- **Verification (Phase 11)**: Depends on Documentation
- **Completion (Phase 12)**: Depends on Verification

### User Story Dependencies

| Story | Depends On | Can Run In Parallel With |
|-------|------------|--------------------------|
| US1 (P1) | Foundational | - |
| US2 (P2) | Foundational | US1, US3, US4, US5 |
| US3 (P3) | Foundational | US1, US2, US4, US5 |
| US4 (P4) | US1 | US2, US3, US5 (after US1 complete) |
| US5 (P5) | Foundational | US1, US2, US3, US4 |

### Within Each User Story

1. **TESTING-GUIDE check**: FIRST task - verify testing guide is in context
2. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
3. Implementation tasks
4. **Verify tests pass**: After implementation
5. **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
6. **Commit**: LAST task - commit completed work

### Parallel Opportunities

**Within Phase 2 (Foundational)**:
- T005, T006, T007, T008 can run in parallel (all test writing)
- T010, T011 can run in parallel (enum and constants)

**Within each User Story**:
- All test tasks marked [P] can run in parallel
- Implementation proceeds after tests are written

**Across User Stories** (after Foundational complete):
- US1, US2, US3, US5 can all start in parallel (independent)
- US4 requires US1 basic implementation first (tests parameter changes)

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task: "Write test for Amplitude mode attack time constant" [T021]
Task: "Write test for Amplitude mode release time constant" [T022]
Task: "Write test for processSample() returns envelope" [T023]
Task: "Write test for process() block processing" [T024]
Task: "Write test for process() in-place processing" [T025]
Task: "Write test for getCurrentValue()" [T026]
Task: "Write test for time constant scaling" [T027]
Task: "Write test for envelope settles to zero" [T028]

# After tests written, implement sequentially:
Task: "Implement calculateCoefficient()" [T029]
Task: "Implement processSample() Amplitude mode" [T030]
Task: "Implement process() block" [T031]
Task: "Implement process() in-place" [T032]
Task: "Add denormal flushing" [T033]
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (Basic Envelope Tracking)
4. **STOP and VALIDATE**: Test US1 independently
5. This delivers a working envelope follower with Amplitude mode

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 (Amplitude mode) → Test → Commit (MVP!)
3. Add User Story 2 (RMS mode) → Test → Commit
4. Add User Story 3 (Peak mode) → Test → Commit
5. Add User Story 4 (Smooth changes) → Test → Commit
6. Add User Story 5 (Sidechain filter) → Test → Commit
7. Edge cases → Polish → Documentation → Verification → Complete

### Story Value Progression

| After Story | Capabilities Added | Value |
|-------------|-------------------|-------|
| US1 | Basic envelope with attack/release | Core dynamics detection |
| US2 | RMS level detection | Perceptually-meaningful level for compressors |
| US3 | Peak level detection | Fast transient capture for limiters/gates |
| US4 | Smooth parameter changes | Automation-safe parameters |
| US5 | Sidechain filtering | Anti-pumping for bass-heavy material |

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
