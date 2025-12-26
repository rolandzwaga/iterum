# Tasks: Shimmer Delay Mode

**Input**: Design documents from `/specs/029-shimmer-delay/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, quickstart.md
**Branch**: `029-shimmer-delay`

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**Feature Type**: Layer 4 Composition - composes existing Layer 2-3 components (PitchShiftProcessor, DiffusionNetwork, FeedbackNetwork, DelayEngine, ModulationMatrix).

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

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: File structure initialization

- [ ] T001 Create feature header file skeleton at `src/dsp/features/shimmer_delay.h`
- [ ] T002 Create test file skeleton at `tests/unit/features/shimmer_delay_test.cpp`
- [ ] T003 Add shimmer_delay_test.cpp to `tests/CMakeLists.txt`

---

## Phase 2: Foundational (Core Structure)

**Purpose**: ShimmerDelay class shell with composed components - MUST complete before user stories

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T005 Write failing tests for prepare()/reset()/isPrepared() lifecycle in `tests/unit/features/shimmer_delay_test.cpp`
- [ ] T006 Implement ShimmerDelay class shell with composed member components in `src/dsp/features/shimmer_delay.h`:
  - DelayEngine delayEngine_
  - FeedbackNetwork feedbackNetwork_
  - PitchShiftProcessor pitchShiftL_, pitchShiftR_ (stereo)
  - DiffusionNetwork diffusion_
  - ModulationMatrix* modulationMatrix_ (optional pointer)
  - OnePoleSmoother for shimmerMix_, dryWetMix_, outputGain_
- [ ] T007 Implement prepare() to initialize all composed components with correct parameters
- [ ] T008 Implement reset() to clear all component states
- [ ] T009 Verify all lifecycle tests pass
- [ ] T010 Commit foundational structure

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Classic Shimmer Reverb Sound (Priority: P1) MVP

**Goal**: Create pitch-shifted cascading delays with +12 semitone octave-up in feedback path

**Independent Test**: Set pitch to +12, feedback to 50%+, process audio, verify successive repeats are pitched up

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T011 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T012 [P] [US1] Write tests for pitch shift parameter setters (FR-002) in `tests/unit/features/shimmer_delay_test.cpp`:
  - setPitchSemitones() range [-24, +24]
  - setPitchCents() range [-100, +100]
  - getPitchRatio() returns correct value
- [ ] T012a [P] [US1] Write test for pitch shift accuracy (SC-001) in `tests/unit/features/shimmer_delay_test.cpp`:
  - Verify pitch shift accuracy within ±5 cents of target
  - Test at multiple points across ±24 semitone range
  - Use frequency analysis to measure actual pitch ratio
- [ ] T012b [P] [US1] Write edge case test for 0-semitone pitch shift in `tests/unit/features/shimmer_delay_test.cpp`:
  - Verify pitch shift = 0 behaves as standard delay (no pitch change in feedback)
  - Verify getPitchRatio() returns 1.0
- [ ] T013 [P] [US1] Write tests for feedback amount parameter (FR-005) in `tests/unit/features/shimmer_delay_test.cpp`:
  - setFeedbackAmount() range [0, 1.2]
  - Verify FeedbackNetwork saturation enabled for >100% stability (FR-019)
- [ ] T014 [P] [US1] Write tests for core process() signal flow (FR-001) in `tests/unit/features/shimmer_delay_test.cpp`:
  - Verify pitch shifter operates in feedback path
  - Verify frequency doubles with +12 semitone pitch shift (SC-002)
  - Verify stereo processing (FR-004)
- [ ] T015 [US1] Write test for feedback stability (SC-005) in `tests/unit/features/shimmer_delay_test.cpp`:
  - 120% feedback + 12 semitone shift for 10 seconds
  - Verify output never exceeds +6 dBFS

### 3.3 Implementation for User Story 1

- [ ] T016 [P] [US1] Implement pitch parameter setters in `src/dsp/features/shimmer_delay.h`:
  - setPitchSemitones(float) with clamping [-24, +24]
  - setPitchCents(float) with clamping [-100, +100]
  - getPitchRatio() to calculate ratio from semitones+cents
- [ ] T017 [P] [US1] Implement feedback parameter setter in `src/dsp/features/shimmer_delay.h`:
  - setFeedbackAmount(float) with clamping [0, 1.2]
  - Enable FeedbackNetwork saturation when feedback > 1.0
- [ ] T018 [US1] Implement core process() with pitch-in-feedback signal flow in `src/dsp/features/shimmer_delay.h`:
  - Delay input signal via DelayEngine
  - Read feedback from FeedbackNetwork
  - Apply pitch shift to feedback (L and R channels separately - PitchShiftProcessor is mono)
  - Write pitch-shifted feedback back to FeedbackNetwork
  - Mix dry/wet output
- [ ] T019 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T020 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` - add to `-fno-fast-math` list in `tests/CMakeLists.txt` if needed

### 3.5 Commit (MANDATORY)

- [ ] T021 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Classic shimmer with pitch-shifted feedback is functional

---

## Phase 4: User Story 2 - Diffused Shimmer Texture (Priority: P2)

**Goal**: Add temporal smearing via DiffusionNetwork for reverb-like texture

**Independent Test**: Set diffusion to 100%, verify smeared output rather than discrete taps

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T022 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T023 [P] [US2] Write tests for diffusion parameters (FR-011, FR-013) in `tests/unit/features/shimmer_delay_test.cpp`:
  - setDiffusionAmount() range [0, 100]
  - setDiffusionSize() range [0, 100]
- [ ] T024 [P] [US2] Write tests for diffusion effect (SC-009) in `tests/unit/features/shimmer_delay_test.cpp`:
  - Diffusion at 0% = discrete echo taps
  - Diffusion at 100% = smeared output
  - Diffusion applies AFTER pitch shift (FR-012)

### 4.3 Implementation for User Story 2

- [ ] T025 [P] [US2] Implement diffusion parameter setters in `src/dsp/features/shimmer_delay.h`:
  - setDiffusionAmount(float) maps to DiffusionNetwork::setDensity()
  - setDiffusionSize(float) maps to DiffusionNetwork::setSize()
- [ ] T026 [US2] Integrate DiffusionNetwork into process() signal flow in `src/dsp/features/shimmer_delay.h`:
  - Apply diffusion AFTER pitch shift in feedback path (FR-012)
  - Use DiffusionNetwork::process() for stereo diffusion
- [ ] T027 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T028 [US2] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 4.5 Commit (MANDATORY)

- [ ] T029 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Shimmer with diffusion creates lush reverb-like textures

---

## Phase 5: User Story 3 - Blend Control (Priority: P2)

**Goal**: Shimmer mix control to blend pitched and unpitched feedback

**Independent Test**: Set shimmer mix to 50%, verify output contains both pitched and unpitched components

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T030 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T031 [P] [US3] Write tests for shimmer mix parameter (FR-003) in `tests/unit/features/shimmer_delay_test.cpp`:
  - setShimmerMix() range [0, 100]
  - Parameter smoothing (FR-009)
- [ ] T032 [P] [US3] Write tests for shimmer mix behavior in `tests/unit/features/shimmer_delay_test.cpp`:
  - 0% shimmer mix = no pitch shifting (SC-003)
  - 100% shimmer mix = full pitch shift (SC-002)
  - 50% shimmer mix = blended signal
  - Smooth transitions without clicks (SC-004)

### 5.3 Implementation for User Story 3

- [ ] T033 [P] [US3] Implement shimmer mix parameter with smoothing in `src/dsp/features/shimmer_delay.h`:
  - setShimmerMix(float) with clamping [0, 100]
  - OnePoleSmoother for smooth transitions (10ms)
- [ ] T034 [US3] Modify process() to blend pitched/unpitched feedback in `src/dsp/features/shimmer_delay.h`:
  - Crossfade between original feedback and pitch-shifted feedback
  - shimmerMix at 0% bypasses pitch shifter entirely
  - shimmerMix at 100% uses only pitch-shifted feedback
- [ ] T035 [US3] Verify all US3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T036 [US3] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 5.5 Commit (MANDATORY)

- [ ] T037 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Shimmer mix allows subtle to full shimmer effect

---

## Phase 6: User Story 4 - Tempo-Synced Shimmer Delay (Priority: P2)

**Goal**: Synchronize delay times to host tempo via note values

**Independent Test**: Set tempo to 120 BPM, quarter note, verify 500ms delay time

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T038 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T039 [P] [US4] Write tests for delay time parameters (FR-014, FR-015, FR-016) in `tests/unit/features/shimmer_delay_test.cpp`:
  - setDelayTimeMs() range [10, 5000]
  - setTimeMode(Free/Synced)
  - setNoteValue() with NoteValue and NoteModifier
- [ ] T040 [P] [US4] Write tests for tempo sync accuracy (SC-007) in `tests/unit/features/shimmer_delay_test.cpp`:
  - 120 BPM quarter note = 500ms
  - 120 BPM dotted eighth = 375ms
  - Tempo change during playback updates delay time
  - Accuracy within 1 sample

### 6.3 Implementation for User Story 4

- [ ] T041 [P] [US4] Implement delay time parameter setters in `src/dsp/features/shimmer_delay.h`:
  - setDelayTimeMs(float) with clamping [10, maxDelayMs_]
  - setTimeMode(TimeMode) to switch Free/Synced
  - setNoteValue(NoteValue, NoteModifier) for tempo sync
- [ ] T042 [US4] Wire delay parameters to DelayEngine in `src/dsp/features/shimmer_delay.h`:
  - Pass BlockContext to DelayEngine::process() for tempo info
  - DelayEngine handles tempo sync internally
- [ ] T043 [US4] Implement delay time smoothing (FR-017) in `src/dsp/features/shimmer_delay.h`:
  - 20ms smoothing for delay time changes
- [ ] T044 [US4] Verify all US4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T045 [US4] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 6.5 Commit (MANDATORY)

- [ ] T046 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Tempo-synced shimmer delay locks to host BPM

---

## Phase 7: User Story 5 - Modulated Shimmer (Priority: P3)

**Goal**: Connect ModulationMatrix for LFO modulation of parameters

**Independent Test**: Route LFO to pitch, verify audible modulation

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T047 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T048 [P] [US5] Write tests for modulation connection (FR-022) in `tests/unit/features/shimmer_delay_test.cpp`:
  - connectModulationMatrix(ModulationMatrix*) stores pointer
  - Null matrix pointer = no modulation applied
- [ ] T049 [P] [US5] Write tests for modulatable parameters (FR-023, FR-024) in `tests/unit/features/shimmer_delay_test.cpp`:
  - Pitch modulation (additive to base pitch)
  - Shimmer mix modulation
  - Delay time modulation
  - Feedback modulation
  - Diffusion modulation

### 7.3 Implementation for User Story 5

- [ ] T050 [P] [US5] Implement modulation connection in `src/dsp/features/shimmer_delay.h`:
  - connectModulationMatrix(ModulationMatrix*)
  - registerDestination() for each modulatable parameter
- [ ] T051 [US5] Integrate modulation into process() in `src/dsp/features/shimmer_delay.h`:
  - Read modulation values from matrix each block
  - Apply additively to base parameter values
  - Clamp final values to valid ranges
- [ ] T052 [US5] Verify all US5 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T053 [US5] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 7.5 Commit (MANDATORY)

- [ ] T054 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Modulated shimmer adds movement and character

---

## Phase 8: User Story 6 - Pitch Shift Quality Selection (Priority: P3)

**Goal**: Allow selection between Simple/Granular/PhaseVocoder pitch shift modes

**Independent Test**: Switch modes, verify different latency values returned

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T055 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T056 [P] [US6] Write tests for pitch mode selection (FR-007, FR-008) in `tests/unit/features/shimmer_delay_test.cpp`:
  - setPitchMode(PitchMode) with Simple/Granular/PhaseVocoder
  - Default mode is Granular
- [ ] T057 [P] [US6] Write tests for latency reporting (FR-028, SC-008) in `tests/unit/features/shimmer_delay_test.cpp`:
  - getLatencySamples() returns correct value for each mode
  - Simple = 0 samples
  - Granular = ~46ms worth of samples
  - PhaseVocoder = ~116ms worth of samples
  - Accuracy within 1 sample
- [ ] T057a [P] [US6] Write test for pitch mode change during playback (edge case) in `tests/unit/features/shimmer_delay_test.cpp`:
  - Switch between pitch modes while processing audio
  - Verify no clicks or artifacts during transition
  - Verify latency reporting updates correctly

### 8.3 Implementation for User Story 6

- [ ] T058 [P] [US6] Implement pitch mode selection in `src/dsp/features/shimmer_delay.h`:
  - setPitchMode(PitchMode) calls PitchShiftProcessor::setMode() on both L/R
- [ ] T059 [US6] Implement latency reporting in `src/dsp/features/shimmer_delay.h`:
  - getLatencySamples() returns max latency of both pitch shifters
  - Cache latency on mode change
- [ ] T060 [US6] Verify all US6 tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T061 [US6] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage

### 8.5 Commit (MANDATORY)

- [ ] T062 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Quality/latency trade-off options available

---

## Phase 9: Output Controls & Polish

**Purpose**: Final output parameters and cleanup

### 9.1 Output Parameters

- [ ] T063 [P] Write tests for output parameters (FR-025, FR-026) in `tests/unit/features/shimmer_delay_test.cpp`:
  - setDryWetMix() range [0, 100]
  - setOutputGainDb() range [-12, +12]
- [ ] T064 [P] Write tests for feedback filter (FR-020, FR-021) in `tests/unit/features/shimmer_delay_test.cpp`:
  - setFilterEnabled(bool)
  - setFilterCutoff(float) range [20, 20000]
- [ ] T065 [P] Implement dry/wet mix with smoothing in `src/dsp/features/shimmer_delay.h`
- [ ] T066 [P] Implement output gain with dB conversion in `src/dsp/features/shimmer_delay.h`
- [ ] T067 Implement filter controls via FeedbackNetwork in `src/dsp/features/shimmer_delay.h`
- [ ] T068 Implement snapParameters() for initialization in `src/dsp/features/shimmer_delay.h`
- [ ] T069 Implement getCurrentDelayMs() query in `src/dsp/features/shimmer_delay.h`
- [ ] T070 Verify all output parameter tests pass

### 9.2 Performance Verification

- [ ] T070a Profile and verify CPU usage <1% at 44.1kHz stereo (SC-006):
  - Run shimmer with typical settings (pitch +12, feedback 50%, diffusion 50%)
  - Measure CPU in Release build
  - Document measurement methodology and results

### 9.3 Quickstart Validation

- [ ] T071 Validate quickstart.md examples work correctly:
  - Basic usage example compiles and runs
  - Tempo-synced example works
  - Modulation example works
  - Test scenarios SC-001, SC-003, SC-005 pass

### 9.4 Cross-Platform Verification (MANDATORY)

- [ ] T072 **Verify IEEE 754 compliance**: Final check of all test files

### 9.5 Commit (MANDATORY)

- [ ] T073 **Commit output controls and polish**

**Checkpoint**: All parameters functional with proper smoothing

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T074 **Update ARCHITECTURE.md** with ShimmerDelay component:
  - Add entry to Layer 4 (Features) section
  - Document: purpose, composed components, public API summary
  - File location: `src/dsp/features/shimmer_delay.h`
  - When to use: ambient/ethereal pitch-shifted delay textures

### 10.2 Final Commit

- [ ] T075 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects ShimmerDelay functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T076 **Review ALL FR-xxx requirements** (FR-001 to FR-028) from spec.md against implementation
- [ ] T077 **Review ALL SC-xxx success criteria** (SC-001 to SC-009) and verify measurable targets are achieved
- [ ] T078 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T079 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T080 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T081 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [ ] T082 **Commit all spec work** to feature branch
- [ ] T083 **Verify all tests pass**: Build and run `dsp_tests [shimmer]`

### 12.2 Completion Claim

- [ ] T084 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - US1 (P1): Core shimmer - no dependencies on other stories
  - US2 (P2): Diffusion - can run parallel to US1 after foundation
  - US3 (P2): Shimmer mix - depends on US1 (modifies feedback path)
  - US4 (P2): Tempo sync - can run parallel to US1-US3
  - US5 (P3): Modulation - depends on US1-US4 (modulates their parameters)
  - US6 (P3): Quality modes - can run parallel to US1-US4
- **Polish (Phase 9)**: Depends on US1-US4 minimum
- **Documentation (Phase 10)**: Depends on all user stories complete
- **Verification (Phase 11-12)**: Depends on all previous phases

### Recommended Execution Order

1. Phase 1 (Setup) → Phase 2 (Foundation)
2. **MVP**: Phase 3 (US1 - Classic Shimmer) - STOP and VALIDATE
3. Phase 4 (US2) + Phase 5 (US3) - can interleave
4. Phase 6 (US4) - tempo sync
5. Phase 7 (US5) + Phase 8 (US6) - advanced features
6. Phase 9 (Polish) → Phase 10 (Docs) → Phase 11-12 (Verification)

### Parallel Opportunities

Within each user story phase, tasks marked [P] can run in parallel:
- Test writing tasks for same story
- Independent parameter implementations

---

## Summary

| Phase | User Story | Priority | Task Count | Description |
|-------|------------|----------|------------|-------------|
| 1 | Setup | - | 3 | File structure |
| 2 | Foundation | - | 7 | Core class with composed components |
| 3 | US1 | P1 | 13 | Classic shimmer with pitch-in-feedback |
| 4 | US2 | P2 | 8 | Diffusion for reverb texture |
| 5 | US3 | P2 | 8 | Shimmer mix blend control |
| 6 | US4 | P2 | 9 | Tempo-synced delay |
| 7 | US5 | P3 | 8 | Modulation matrix integration |
| 8 | US6 | P3 | 9 | Pitch quality mode selection |
| 9 | Polish | - | 12 | Output controls, performance, quickstart validation |
| 10 | Docs | - | 2 | ARCHITECTURE.md update |
| 11 | Verify | - | 6 | Compliance verification |
| 12 | Final | - | 3 | Final commit and completion |

**Total Tasks**: 88

**MVP Scope**: Phases 1-3 (US1 only) = 23 tasks

**Independent Test Criteria per Story**:
- US1: Process audio with +12 pitch, 50% feedback → verify cascading octaves
- US2: Set diffusion to 100% → verify smeared (not discrete) output
- US3: Set shimmer mix to 50% → verify both pitched and unpitched components
- US4: Set 120 BPM quarter note → verify 500ms delay
- US5: Route LFO to pitch → verify audible modulation
- US6: Switch pitch modes → verify different latency values
