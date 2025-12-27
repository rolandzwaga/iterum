# Tasks: Spectral Delay

**Input**: Design documents from `/specs/033-spectral-delay/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [x] T001 Verify TESTING-GUIDE.md is in context (ingest `specs/TESTING-GUIDE.md` if needed)
- [x] T002 Add spectral_delay_test.cpp to tests/CMakeLists.txt
- [x] T003 Create empty SpectralDelay class skeleton in src/dsp/features/spectral_delay.h

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core SpectralDelay class structure that MUST be complete before ANY user story

**CRITICAL**: No user story work can begin until this phase is complete

- [x] T004 Write foundational tests in tests/unit/features/spectral_delay_test.cpp:
  - Default construction
  - prepare() at various sample rates (44.1/48/96/192 kHz)
  - reset() clears state
  - isPrepared() returns correct state
  - setFFTSize() for 512/1024/2048/4096
  - getLatencySamples() returns FFT size
- [x] T005 Implement SpreadDirection enum in src/dsp/features/spectral_delay.h
- [x] T006 Implement SpectralDelay class skeleton with:
  - Constants (kMinFFTSize, kMaxFFTSize, kDefaultFFTSize, kMinDelayMs, kMaxDelayMs, etc.)
  - Member variables (STFT L/R, OverlapAdd L/R, SpectralBuffer instances, DelayLine vectors)
  - prepare()/reset()/isPrepared() lifecycle
  - setFFTSize()/getFFTSize()
  - getLatencySamples()
- [x] T007 Verify foundational tests pass
- [x] T008 Add spectral_delay_test.cpp to -fno-fast-math list in tests/CMakeLists.txt
- [x] T009 Commit foundational SpectralDelay structure

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Spectral Delay (Priority: P1)

**Goal**: Apply delay to all frequency bands uniformly (0% spread baseline)

**Independent Test**: Feed impulse, verify delayed output appears at configured delay time with spectral characteristics preserved

### 3.1 Pre-Implementation (MANDATORY)

- [x] T010 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

- [x] T011 [P] [US1] Write tests for uniform delay (0% spread) in tests/unit/features/spectral_delay_test.cpp:
  - Delayed output appears after configured delay time
  - All frequency bands have same delay (coherent echo)
- [x] T012 [P] [US1] Write tests for dry/wet mix control in tests/unit/features/spectral_delay_test.cpp:
  - 0% wet = only dry signal
  - 100% wet = only delayed signal
  - 50% mix blends both
- [x] T013 [P] [US1] Write tests for output gain control in tests/unit/features/spectral_delay_test.cpp:
  - +6dB boosts output
  - -96dB effectively mutes
  - 0dB = unity gain

### 3.3 Implementation for User Story 1

- [x] T014 [US1] Implement per-bin delay line allocation in prepare() in src/dsp/features/spectral_delay.h:
  - Allocate numBins * 2 DelayLine instances (stereo)
  - Configure each for max delay time
- [x] T015 [US1] Implement spectral processing loop in process() in src/dsp/features/spectral_delay.h:
  - Push samples to STFT L/R
  - While canAnalyze(): analyze -> per-bin delay -> synthesize
  - Pull samples from OverlapAdd L/R
- [x] T016 [US1] Implement per-bin delay read/write logic in src/dsp/features/spectral_delay.h:
  - Read delayed magnitude from each bin's delay line
  - Write input magnitude to delay line
  - Pass through input phase (preserves transients per research.md Decision 4)
  - Handle DC bin (bin 0) same as other bins
- [x] T017 [US1] Implement setBaseDelayMs()/getBaseDelayMs() with clamping in src/dsp/features/spectral_delay.h
- [x] T018 [US1] Implement setDryWetMix()/getDryWetMix() with OnePoleSmoother in src/dsp/features/spectral_delay.h
- [x] T019 [US1] Implement setOutputGainDb()/getOutputGainDb() with OnePoleSmoother in src/dsp/features/spectral_delay.h
- [x] T020 [US1] Implement snapParameters() for click-free preset changes in src/dsp/features/spectral_delay.h
- [x] T021 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [x] T022 [US1] **Verify IEEE 754 compliance**: Confirm spectral_delay_test.cpp is in -fno-fast-math list

### 3.5 Commit (MANDATORY)

- [x] T023 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic spectral delay with uniform delay, dry/wet, and output gain working

---

## Phase 4: User Story 2 - Delay Spread Control (Priority: P1)

**Goal**: Distribute delay times across frequency bands based on spread amount and direction

**Independent Test**: Set spread > 0, verify high/low frequency bands have different delay times

### 4.1 Pre-Implementation (MANDATORY)

- [x] T024 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [x] T025 [P] [US2] Write tests for LowToHigh spread in tests/unit/features/spectral_delay_test.cpp:
  - High frequencies delayed more than low frequencies
  - Spread of 0% = all same delay (coherent)
- [x] T026 [P] [US2] Write tests for HighToLow spread in tests/unit/features/spectral_delay_test.cpp:
  - Low frequencies delayed more than high frequencies
- [x] T027 [P] [US2] Write tests for CenterOut spread in tests/unit/features/spectral_delay_test.cpp:
  - Edge frequencies delayed more, center frequencies at base delay
- [x] T028 [P] [US2] Write tests for spread amount range in tests/unit/features/spectral_delay_test.cpp:
  - Spread 0-2000ms clamping
  - Delay range = baseDelay to baseDelay + spread

### 4.3 Implementation for User Story 2

- [x] T029 [US2] Implement setSpreadMs()/getSpreadMs() with clamping in src/dsp/features/spectral_delay.h
- [x] T030 [US2] Implement setSpreadDirection()/getSpreadDirection() in src/dsp/features/spectral_delay.h
- [x] T031 [US2] Implement calculateBinDelayTime() helper in src/dsp/features/spectral_delay.h:
  - LowToHigh: delayOffset = normalizedBin * spreadMs
  - HighToLow: delayOffset = (1.0f - normalizedBin) * spreadMs
  - CenterOut: delayOffset = abs(normalizedBin - 0.5f) * 2.0f * spreadMs
- [x] T032 [US2] Update per-bin delay read to use calculateBinDelayTime() in src/dsp/features/spectral_delay.h
- [x] T033 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [x] T034 [US2] **Verify IEEE 754 compliance**: Check test file still in -fno-fast-math list

### 4.5 Commit (MANDATORY)

- [x] T035 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Spread control working - frequency-dependent delays functional

---

## Phase 5: User Story 3 - Spectral Freeze (Priority: P2)

**Goal**: Hold current spectrum indefinitely for drone/pad textures

**Independent Test**: Enable freeze during audio, stop input, verify output continues with frozen spectrum

### 5.1 Pre-Implementation (MANDATORY)

- [x] T036 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [x] T037 [P] [US3] Write tests for freeze enable/disable in tests/unit/features/spectral_delay_test.cpp:
  - setFreezeEnabled()/isFreezeEnabled() work correctly
- [x] T038 [P] [US3] Write tests for freeze holds spectrum in tests/unit/features/spectral_delay_test.cpp:
  - Output continues after input stops when frozen
  - New input is ignored while frozen
  - Freeze sustains for at least 60 seconds without decay (SC-003)
- [x] T039 [P] [US3] Write tests for freeze transitions in tests/unit/features/spectral_delay_test.cpp:
  - Freeze on/off transitions are click-free (crossfade)
- [x] T040 [P] [US3] Write tests for unfreeze resumes in tests/unit/features/spectral_delay_test.cpp:
  - After disabling freeze, new input appears in output

### 5.3 Implementation for User Story 3

- [x] T041 [US3] Add frozen SpectralBuffer and freeze state variables in src/dsp/features/spectral_delay.h:
  - frozenSpectrum_ SpectralBuffer
  - freezeEnabled_ bool
  - freezeCrossfade_ float (0.0 to 1.0)
- [x] T042 [US3] Implement setFreezeEnabled()/isFreezeEnabled() in src/dsp/features/spectral_delay.h
- [x] T043 [US3] Implement freeze capture logic in process() in src/dsp/features/spectral_delay.h:
  - When freeze enabled, copy current spectrum to frozenSpectrum_
  - Use frozenSpectrum_ for output instead of live spectrum
- [x] T044 [US3] Implement freeze crossfade (50-100ms transition) in src/dsp/features/spectral_delay.h:
  - Smooth transition between live and frozen spectrum
  - Prevents clicks on enable/disable
- [x] T045 [US3] Verify all US3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [x] T046 [US3] **Verify IEEE 754 compliance**: Check test file still in -fno-fast-math list

### 5.5 Commit (MANDATORY)

- [x] T047 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Freeze mode working - infinite sustain textures possible

---

## Phase 6: User Story 4 - Frequency-Dependent Feedback (Priority: P2)

**Goal**: Different feedback amounts per frequency band for evolving textures

**Independent Test**: Set feedback tilt, verify some frequencies sustain longer than others

### 6.1 Pre-Implementation (MANDATORY)

- [x] T048 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [x] T049 [P] [US4] Write tests for global feedback in tests/unit/features/spectral_delay_test.cpp:
  - 0% feedback = no repeats
  - 50% feedback = decaying repeats
  - setFeedback()/getFeedback() with 0-120% range
- [x] T050 [P] [US4] Write tests for feedback tilt in tests/unit/features/spectral_delay_test.cpp:
  - Negative tilt: lows sustain longer
  - Positive tilt: highs sustain longer
  - Zero tilt: uniform decay
- [x] T051 [P] [US4] Write tests for feedback limiting in tests/unit/features/spectral_delay_test.cpp:
  - Feedback > 100% is soft-limited
  - No runaway oscillation

### 6.3 Implementation for User Story 4

- [x] T052 [US4] Implement setFeedback()/getFeedback() with 0-120% range in src/dsp/features/spectral_delay.h
- [x] T053 [US4] Implement setFeedbackTilt()/getFeedbackTilt() with -1.0 to +1.0 range in src/dsp/features/spectral_delay.h
- [x] T054 [US4] Implement calculateTiltedFeedback() helper in src/dsp/features/spectral_delay.h:
  - tiltFactor = 1.0f + tilt * (normalizedBin - 0.5f) * 2.0f
  - return clamp(globalFeedback * tiltFactor, 0.0f, 1.5f)
- [x] T055 [US4] Update per-bin delay write to include feedback in src/dsp/features/spectral_delay.h:
  - write(inputMag + delayedMag * tiltedFeedback)
- [x] T056 [US4] Add soft limiting for feedback > 100% (tanh saturation) in src/dsp/features/spectral_delay.h
- [x] T057 [US4] Verify all US4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [x] T058 [US4] **Verify IEEE 754 compliance**: Check test file still in -fno-fast-math list

### 6.5 Commit (MANDATORY)

- [x] T059 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Feedback with tilt working - frequency-sculpted decay possible

---

## Phase 7: User Story 5 - Spectral Diffusion/Smear (Priority: P3)

**Goal**: Blur spectrum over time for soft, evolving textures

**Independent Test**: Set diffusion > 0, verify spectral content spreads to neighboring bins

### 7.1 Pre-Implementation (MANDATORY)

- [x] T060 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [x] T061 [P] [US5] Write tests for diffusion amount in tests/unit/features/spectral_delay_test.cpp:
  - 0% diffusion = clean spectrum
  - 100% diffusion = maximum blur
  - setDiffusion()/getDiffusion() with 0-100% range
- [x] T062 [P] [US5] Write tests for spectral blurring in tests/unit/features/spectral_delay_test.cpp:
  - Pure tone spreads to neighboring bins with diffusion
  - Transients are softened

### 7.3 Implementation for User Story 5

- [x] T063 [US5] Implement setDiffusion()/getDiffusion() with 0-100% range in src/dsp/features/spectral_delay.h
- [x] T064 [US5] Add blurredMag_ buffer for diffusion processing in src/dsp/features/spectral_delay.h
- [x] T065 [US5] Implement applyDiffusion() with 3-tap blur kernel in src/dsp/features/spectral_delay.h:
  - kernel = [diffusion * 0.25f, 1.0f - diffusion * 0.5f, diffusion * 0.25f]
  - Apply to magnitude before output
- [x] T066 [US5] Integrate diffusion into processing loop in src/dsp/features/spectral_delay.h
- [x] T067 [US5] Verify all US5 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [x] T068 [US5] **Verify IEEE 754 compliance**: Check test file still in -fno-fast-math list

### 7.5 Commit (MANDATORY)

- [x] T069 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Diffusion working - spectral smearing for soft textures

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Edge cases, optimization, and cleanup

- [x] T070 [P] Add edge case tests in tests/unit/features/spectral_delay_test.cpp:
  - Silent input handling
  - Full-scale input handling
  - Sample rate change (re-prepare)
- [x] T071 [P] Add performance test (SC-005: < 3% CPU) in tests/unit/features/spectral_delay_test.cpp
- [x] T072 Optimize per-bin processing loop (SIMD opportunities) in src/dsp/features/spectral_delay.h
- [x] T073 Verify all tests pass across all user stories
- [x] T074 Run quickstart.md scenarios for validation

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 9.1 Architecture Documentation Update

- [x] T075 **Update ARCHITECTURE.md** with SpectralDelay (Layer 4):
  - Add entry to Layer 4: User Features section
  - Include: purpose, public API summary, file location
  - Document SpreadDirection enum
  - Add usage example
  - Verify no duplicate functionality introduced

### 9.2 Final Commit

- [x] T076 **Commit ARCHITECTURE.md updates**
- [x] T077 Verify all spec work is committed to feature branch

**Checkpoint**: ARCHITECTURE.md reflects SpectralDelay feature

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 10.1 Requirements Verification

- [x] T078 **Review ALL FR-xxx requirements** (FR-001 to FR-026) from spec.md against implementation
- [x] T079 **Review ALL SC-xxx success criteria** (SC-001 to SC-008) and verify measurable targets
- [x] T080 **Search for cheating patterns** in implementation:
  - [x] No `// placeholder` or `// TODO` comments in new code
  - [x] No test thresholds relaxed from spec requirements
  - [x] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [x] T081 **Update spec.md "Implementation Verification" section** with compliance status for each FR and SC
- [x] T082 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

- [x] T083 **All self-check questions answered "no"** (or gaps documented honestly):
  1. Did I change ANY test threshold from spec requirements?
  2. Are there ANY "placeholder"/"TODO" comments in new code?
  3. Did I remove ANY features from scope without user approval?
  4. Would the spec author consider this "done"?
  5. If I were the user, would I feel cheated?

**Checkpoint**: Honest assessment complete

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

- [x] T084 **Commit all spec work** to feature branch
- [x] T085 **Verify all tests pass** (run full test suite)
- [x] T086 **Claim completion ONLY if all requirements are MET**

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Story 1+2 (Phases 3-4)**: Both P1, can run after Foundational
- **User Story 3+4 (Phases 5-6)**: Both P2, can run after Foundational (or after US1+2)
- **User Story 5 (Phase 7)**: P3, can run after Foundational
- **Polish (Phase 8)**: After all user stories complete
- **Documentation (Phase 9)**: After Polish
- **Verification (Phase 10-11)**: After Documentation

### User Story Dependencies

| Story | Priority | Dependencies | Notes |
|-------|----------|--------------|-------|
| US1 | P1 | Foundational | Core delay processing |
| US2 | P1 | US1 (extends delay logic) | Spread builds on base delay |
| US3 | P2 | Foundational | Independent freeze feature |
| US4 | P2 | US1 (uses delay lines) | Feedback extends delay logic |
| US5 | P3 | Foundational | Independent diffusion |

### Parallel Opportunities

Within each user story phase:
- All tests marked [P] can run in parallel
- Test writing parallelizes with no dependencies

Across phases:
- US3 and US5 are independent from US1/US2/US4
- With multiple developers: split by user story

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task: "Write tests for uniform delay in tests/unit/features/spectral_delay_test.cpp"
Task: "Write tests for dry/wet mix in tests/unit/features/spectral_delay_test.cpp"
Task: "Write tests for output gain in tests/unit/features/spectral_delay_test.cpp"
```

---

## Implementation Strategy

### MVP First (User Stories 1+2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (Basic Spectral Delay)
4. Complete Phase 4: User Story 2 (Spread Control)
5. **STOP and VALIDATE**: Test spectral delay with spread
6. Deploy/demo if ready

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add US1 (Basic Delay) → Core effect working
3. Add US2 (Spread) → Frequency-dependent delays
4. Add US3 (Freeze) → Infinite sustain
5. Add US4 (Feedback) → Evolving textures
6. Add US5 (Diffusion) → Spectral smearing
7. Each story adds value without breaking previous stories

---

## Notes

- **Total Tasks**: 86
- **US1 Tasks**: 14 (Basic Spectral Delay)
- **US2 Tasks**: 12 (Spread Control)
- **US3 Tasks**: 12 (Spectral Freeze)
- **US4 Tasks**: 12 (Feedback)
- **US5 Tasks**: 10 (Diffusion)
- **Foundational**: 9
- **Setup**: 3
- **Polish/Doc/Verify**: 14

- [P] tasks = can run in parallel
- [Story] label maps task to specific user story
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming complete (Principle XV)
