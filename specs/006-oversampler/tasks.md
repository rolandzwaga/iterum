# Tasks: Oversampler

**Input**: Design documents from `/specs/006-oversampler/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/oversampler.h

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

**Purpose**: Create file structure and establish base framework

- [ ] T001 Create header file stub at src/dsp/primitives/oversampler.h with namespace and include guards
- [ ] T002 Create test file stub at tests/unit/primitives/oversampler_test.cpp with Catch2 includes
- [ ] T003 [P] Add oversampler_test.cpp to tests/CMakeLists.txt with -fno-fast-math flag (NaN detection)
- [ ] T004 [P] Add oversampler.h include to src/dsp/dsp.h aggregation header (if exists)

---

## Phase 2: Foundational (Core Enums and Base Structure)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T005 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T006 Define OversamplingFactor enum (x2, x4) in src/dsp/primitives/oversampler.h
- [ ] T007 Define OversamplingQuality enum (Economy, Standard, High) in src/dsp/primitives/oversampler.h
- [ ] T008 Define OversamplingMode enum (ZeroLatency, LinearPhase) in src/dsp/primitives/oversampler.h
- [ ] T009 Define Oversampler class template skeleton with factor() and numChannels() constexpr methods
- [ ] T010 Build and verify compilation succeeds

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic 2x Oversampling for Saturation (Priority: P1) MVP

**Goal**: Provide 2x upsampling with IIR anti-aliasing filter using existing BiquadCascade<4>

**Independent Test**: Process a high-frequency sine wave through saturation with oversampling, verify aliased frequency content is reduced by at least 60dB

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T011 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T012 [P] [US1] Write test: Oversampler2x default constructs without crash in tests/unit/primitives/oversampler_test.cpp
- [ ] T013 [P] [US1] Write test: prepare() configures for given sample rate and block size
- [ ] T014 [P] [US1] Write test: upsample() doubles buffer size (N samples → 2N samples)
- [ ] T015 [P] [US1] Write test: downsample() halves buffer size (2N samples → N samples)
- [ ] T016 [P] [US1] Write test: process() with callback applies function at 2x rate
- [ ] T017 [P] [US1] Write test: output buffer size equals input buffer size after full process cycle
- [ ] T018 [P] [US1] Write test: impulse response shows lowpass anti-aliasing (no high-frequency imaging)
- [ ] T019 [US1] Verify tests compile but FAIL (no implementation yet)

### 3.3 Implementation for User Story 1

- [ ] T020 [US1] Implement Oversampler internal buffer allocation in prepare() in src/dsp/primitives/oversampler.h
- [ ] T021 [US1] Add BiquadCascade<4> upsample/downsample filter members (include biquad.h)
- [ ] T022 [US1] Implement upsample() with zero-stuffing and IIR lowpass filtering
- [ ] T023 [US1] Implement downsample() with IIR lowpass filtering and decimation
- [ ] T024 [US1] Implement process() stereo overload with callback at oversampled rate
- [ ] T025 [US1] Implement process() mono overload
- [ ] T026 [US1] Implement reset() to clear all filter states
- [ ] T027 [US1] Add denormal flushing using existing kDenormalThreshold from biquad.h
- [ ] T028 [US1] Verify all tests pass: run `build\bin\Debug\dsp_tests.exe "[US1]"`
- [ ] T029 [US1] Add noexcept to all process-path methods per Constitution Principle II

### 3.4 Commit (MANDATORY)

- [ ] T030 [US1] **Commit completed User Story 1 work** with message "feat(dsp): add Oversampler 2x IIR mode (US1 - 006-oversampler)"

**Checkpoint**: User Story 1 should be fully functional - 2x oversampling works with IIR filters

---

## Phase 4: User Story 2 - 4x Oversampling for Heavy Distortion (Priority: P2)

**Goal**: Extend to 4x oversampling by cascading two 2x stages

**Independent Test**: Process swept sine through hard clipping, verify alias frequencies are below -80dB

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T031 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T032 [P] [US2] Write test: Oversampler4x default constructs without crash
- [ ] T033 [P] [US2] Write test: upsample() quadruples buffer size (N → 4N)
- [ ] T034 [P] [US2] Write test: downsample() quarters buffer size (4N → N)
- [ ] T035 [P] [US2] Write test: 4x process cycle returns correct output size
- [ ] T036 [P] [US2] Write test: numStages() returns 2 for 4x factor
- [ ] T037 [US2] Verify tests compile but FAIL

### 4.3 Implementation for User Story 2

- [ ] T038 [US2] Add second stage filters for 4x mode (NumStages = 2)
- [ ] T039 [US2] Implement cascaded upsample: first 2x stage, then second 2x stage
- [ ] T040 [US2] Implement cascaded downsample: first 2x stage, then second 2x stage
- [ ] T041 [US2] Update buffer allocation in prepare() for 4x factor
- [ ] T042 [US2] Verify all tests pass: run `build\bin\Debug\dsp_tests.exe "[US2]"`

### 4.4 Commit (MANDATORY)

- [ ] T043 [US2] **Commit completed User Story 2 work** with message "feat(dsp): add Oversampler 4x cascaded mode (US2 - 006-oversampler)"

**Checkpoint**: 4x oversampling works via cascaded 2x stages

---

## Phase 5: User Story 3 - Configurable Filter Quality (Priority: P3)

**Goal**: Add HalfbandFilter FIR implementation for Standard and High quality linear-phase modes

**Independent Test**: Compare frequency response flatness and stopband rejection across quality settings

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T044 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T045 [P] [US3] Write test: HalfbandFilter constructs with tap count
- [ ] T046 [P] [US3] Write test: HalfbandFilter processes without crash
- [ ] T047 [P] [US3] Write test: HalfbandFilter reset() clears delay line
- [ ] T048 [P] [US3] Write test: Economy quality uses IIR (0 latency)
- [ ] T049 [P] [US3] Write test: Standard quality uses FIR 31-tap (15 sample latency at 2x)
- [ ] T050 [P] [US3] Write test: High quality uses FIR 63-tap (31 sample latency at 2x)
- [ ] T051 [P] [US3] Write test: getLatency() returns correct value for each quality
- [ ] T052 [US3] Verify tests compile but FAIL

### 5.3 Implementation for User Story 3

- [ ] T053 [US3] Add HalfbandFilter class in src/dsp/primitives/oversampler.h (internal helper)
- [ ] T054 [US3] Implement HalfbandFilter coefficients for 15-tap, 31-tap, 63-tap
- [ ] T055 [US3] Implement HalfbandFilter::process() with polyphase optimization
- [ ] T056 [US3] Implement HalfbandFilter::reset() delay line clearing
- [ ] T057 [US3] Add FIR filter member arrays to Oversampler for Standard/High quality
- [ ] T058 [US3] Implement quality-dependent filter selection in prepare()
- [ ] T059 [US3] Implement getLatency() calculation based on quality and factor
- [ ] T060 [US3] Implement getQuality() accessor
- [ ] T061 [US3] Verify all tests pass: run `build\bin\Debug\dsp_tests.exe "[US3]"`

### 5.4 Commit (MANDATORY)

- [ ] T062 [US3] **Commit completed User Story 3 work** with message "feat(dsp): add HalfbandFilter and quality levels (US3 - 006-oversampler)"

**Checkpoint**: Three quality levels available - Economy (IIR), Standard (31-tap FIR), High (63-tap FIR)

---

## Phase 6: User Story 4 - Zero-Latency Mode (Priority: P4)

**Goal**: Allow explicit ZeroLatency mode selection (forces IIR even with Standard/High quality requested)

**Independent Test**: Verify latency is 0 samples when ZeroLatency mode enabled

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T063 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T064 [P] [US4] Write test: ZeroLatency mode forces IIR filters
- [ ] T065 [P] [US4] Write test: ZeroLatency mode getLatency() returns 0
- [ ] T066 [P] [US4] Write test: LinearPhase mode uses FIR for Standard quality
- [ ] T067 [P] [US4] Write test: getMode() accessor returns current mode
- [ ] T068 [US4] Verify tests compile but FAIL

### 6.3 Implementation for User Story 4

- [ ] T069 [US4] Add mode parameter to prepare() signature
- [ ] T070 [US4] Implement mode override logic: ZeroLatency forces IIR regardless of quality
- [ ] T071 [US4] Implement getMode() accessor
- [ ] T072 [US4] Update latency calculation for mode
- [ ] T073 [US4] Verify all tests pass: run `build\bin\Debug\dsp_tests.exe "[US4]"`

### 6.4 Commit (MANDATORY)

- [ ] T074 [US4] **Commit completed User Story 4 work** with message "feat(dsp): add ZeroLatency mode override (US4 - 006-oversampler)"

**Checkpoint**: ZeroLatency mode can be explicitly selected for monitoring use cases

---

## Phase 7: User Story 5 - Sample Rate Changes (Priority: P5)

**Goal**: Properly reconfigure filters when sample rate changes via prepare()

**Independent Test**: Call prepare() with different sample rates and verify filter coefficients update correctly

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T075 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T076 [P] [US5] Write test: prepare() at 44.1kHz then 96kHz recalculates IIR coefficients
- [ ] T077 [P] [US5] Write test: prepare() at 192kHz handles 4x (768kHz internal rate)
- [ ] T078 [P] [US5] Write test: First block after prepare() produces valid output (no garbage)
- [ ] T079 [P] [US5] Write test: isPrepared() returns true after prepare(), false before
- [ ] T080 [US5] Verify tests compile but FAIL

### 7.3 Implementation for User Story 5

- [ ] T081 [US5] Implement isPrepared() accessor
- [ ] T082 [US5] Ensure prepare() recalculates BiquadCascade coefficients for new sample rate
- [ ] T083 [US5] Ensure prepare() clears filter state (calls reset() internally)
- [ ] T084 [US5] Add sample rate validation (reject 0 or negative)
- [ ] T085 [US5] Verify all tests pass: run `build\bin\Debug\dsp_tests.exe "[US5]"`

### 7.4 Commit (MANDATORY)

- [ ] T086 [US5] **Commit completed User Story 5 work** with message "feat(dsp): handle sample rate changes correctly (US5 - 006-oversampler)"

**Checkpoint**: Sample rate changes handled gracefully

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Edge cases, documentation, and integration

- [ ] T087 Write test: Block size of 1 sample works correctly in tests/unit/primitives/oversampler_test.cpp
- [ ] T088 Write test: Block size at maximum (8192) works correctly
- [ ] T089 Write test: DC offset passes through unchanged (filters have 0dB DC gain)
- [ ] T090 Write test: NaN input produces 0 output (safety)
- [ ] T091 Write test: process() before prepare() returns silence or passthrough (no crash)
- [ ] T092 Write test: prepare() at low sample rate (22.05kHz) functions correctly
- [ ] T093 Implement edge case handling for T087-T092
- [ ] T094 Verify all tests pass: run `build\bin\Debug\dsp_tests.exe "[oversampler]"`
- [ ] T095 Add Doxygen documentation to all public methods in src/dsp/primitives/oversampler.h
- [ ] T096 Verify quickstart.md examples compile and work

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 9.1 Architecture Documentation Update

- [ ] T097 **Update ARCHITECTURE.md** with Oversampler component:
  - Add entry under Layer 1 (DSP Primitives) section
  - Include: purpose, public API summary, file location
  - Add "when to use this" guidance
  - Document dependency on BiquadCascade
  - Add usage example

### 9.2 Final Commit

- [ ] T098 **Commit ARCHITECTURE.md updates**
- [ ] T099 Verify all spec work is committed to feature branch

**Checkpoint**: Spec implementation complete - ARCHITECTURE.md reflects all new functionality

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories CAN proceed in parallel after Phase 2
  - Recommended: Sequential in priority order (US1 → US2 → US3 → US4 → US5)
  - US3 (HalfbandFilter) is more complex, may benefit from US1/US2 experience first
- **Polish (Phase 8)**: Depends on all user stories being complete
- **Documentation (Phase 9)**: Final phase, depends on Polish

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational - No dependencies on other stories
- **User Story 2 (P2)**: Depends on US1 (extends upsample/downsample logic)
- **User Story 3 (P3)**: Can start after Foundational - Independent FIR implementation
- **User Story 4 (P4)**: Depends on US3 (needs quality enum to override)
- **User Story 5 (P5)**: Can start after US1 (tests sample rate on existing implementation)

### Within Each User Story

1. **TESTING-GUIDE check**: FIRST task - verify testing guide is in context
2. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
3. Implementation tasks in dependency order
4. **Verify tests pass**: After implementation
5. **Commit**: LAST task - commit completed work

### Parallel Opportunities

**Within Phase 3 (US1)**:
```bash
# Launch all tests together:
T012, T013, T014, T015, T016, T017, T018 - different test cases, no dependencies
```

**Within Phase 5 (US3)**:
```bash
# Launch all HalfbandFilter tests together:
T045, T046, T047, T048, T049, T050, T051 - different test cases, no dependencies
```

---

## Parallel Example: User Story 1

```bash
# After T011 (context check), launch all tests in parallel:
Task: T012 "[US1] Write test: Oversampler2x default constructs"
Task: T013 "[US1] Write test: prepare() configures..."
Task: T014 "[US1] Write test: upsample() doubles buffer size"
Task: T015 "[US1] Write test: downsample() halves buffer size"
# ... all T012-T018 can run in parallel

# Then implementation sequentially (dependencies exist):
T020 → T021 → T022 → T023 → T024 → T025 → T026 → T027 → T028 → T029
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test 2x oversampling independently
5. Deploy/demo if ready - have working 2x IIR oversampler

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → **MVP!** (2x IIR oversampling)
3. Add User Story 2 → Test independently → 4x support
4. Add User Story 3 → Test independently → Quality levels with FIR
5. Add User Story 4 → Test independently → Explicit latency mode
6. Add User Story 5 → Test independently → Sample rate robustness
7. Each story adds value without breaking previous stories

### Recommended Implementation Order

For maximum efficiency and learning curve:

1. **US1 (2x IIR)**: Foundation - learn the API patterns
2. **US2 (4x)**: Extend to cascaded stages
3. **US3 (FIR)**: Add HalfbandFilter - most complex new code
4. **US4 (Mode)**: Simple API extension
5. **US5 (Sample Rate)**: Robustness testing

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- Stop at any checkpoint to validate story independently
- Header-only implementation: all code in src/dsp/primitives/oversampler.h
- Test file: tests/unit/primitives/oversampler_test.cpp
- Use [oversampler] tag for all tests for easy filtering
