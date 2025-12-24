# Tasks: NoiseGenerator

**Input**: Design documents from `/specs/013-noise-generator/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/noise_generator.h

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

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: No new setup required - uses existing DSP project structure

*No tasks in this phase - project structure already established.*

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Pre-Implementation

- [ ] T001 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.2 Tests for Foundation (Write FIRST - Must FAIL)

- [ ] T002 [P] Write unit tests for Xorshift32 PRNG in tests/unit/core/random_test.cpp
  - Test: period does not repeat within reasonable sample count
  - Test: nextFloat() returns values in [-1.0, 1.0] range
  - Test: different seeds produce different sequences
  - Test: seed of 0 is handled (should not produce all zeros)

### 2.3 Implementation

- [ ] T003 [P] Create Xorshift32 PRNG class in src/dsp/core/random.h
  - Implement xorshift32 algorithm per research.md
  - Add next() returning uint32_t
  - Add nextFloat() returning [-1.0, 1.0]
  - Add nextUnipolar() returning [0.0, 1.0]
  - Add seed() method for reseeding

- [ ] T004 [P] Create NoiseType enum in src/dsp/processors/noise_generator.h
  - Define: White, Pink, TapeHiss, VinylCrackle, Asperity
  - Add kNumNoiseTypes constant

- [ ] T005 Create NoiseGenerator skeleton in src/dsp/processors/noise_generator.h
  - Add prepare(sampleRate, maxBlockSize) method
  - Add reset() method
  - Add internal Xorshift32 RNG member
  - Add sample rate storage
  - Include necessary headers (db_utils.h, smoother.h, biquad.h)

### 2.4 Verification

- [ ] T006 Verify all foundational tests pass
- [ ] T007 Add tests/unit/core/random_test.cpp to tests/CMakeLists.txt

### 2.5 Cross-Platform Verification

- [ ] T008 **Verify IEEE 754 compliance**: Check if random_test.cpp uses NaN detection → add to `-fno-fast-math` list if needed

### 2.6 Commit

- [ ] T009 **Commit completed Foundation work** (Xorshift32 + NoiseGenerator skeleton)

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - White Noise Generation (Priority: P1) 🎯 MVP

**Goal**: Generate flat-spectrum white noise with level control

**Independent Test**: Generate noise and verify samples are in [-1.0, 1.0] with roughly equal energy across frequency bands

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T010 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T011 [P] [US1] Unit tests for white noise generation in tests/unit/processors/noise_generator_test.cpp
  - Test: process() outputs samples in [-1.0, 1.0] range
  - Test: output is non-zero when white noise enabled
  - Test: output is zero when white noise disabled
  - Test: setNoiseLevel() affects output amplitude
  - Test: level at -20dB produces ~0.1 amplitude
  - Test: level at 0dB produces ~1.0 amplitude

- [ ] T012 [P] [US1] Spectral test for white noise flatness
  - Test: energy at 1kHz ≈ energy at 4kHz (within 3dB) over 10-second sample

### 3.3 Implementation for User Story 1

- [ ] T013 [US1] Add white noise channel to NoiseGenerator
  - Add white noise level member (float, dB)
  - Add white noise enabled flag
  - Add setNoiseLevel(NoiseType::White, dB) implementation
  - Add setNoiseEnabled(NoiseType::White, enabled) implementation
  - Add OnePoleSmoother for level smoothing

- [ ] T014 [US1] Implement white noise generation in process()
  - Generate white noise samples using Xorshift32::nextFloat()
  - Apply smoothed level gain
  - Handle enabled/disabled state with smooth fade

- [ ] T015 [US1] Implement process() and processMix() overloads
  - process(output, numSamples) - noise only output
  - processMix(input, output, numSamples) - add noise to input

### 3.4 Verification

- [ ] T016 [US1] Verify all US1 tests pass

### 3.5 Cross-Platform Verification (MANDATORY)

- [ ] T017 [US1] **Verify IEEE 754 compliance**: Check if noise_generator_test.cpp uses NaN detection → add to `-fno-fast-math` list if needed

### 3.6 Commit (MANDATORY)

- [ ] T018 [US1] **Commit completed User Story 1 work** (white noise generation)

**Checkpoint**: White noise (MVP) should be fully functional and testable

---

## Phase 4: User Story 2 - Pink Noise Generation (Priority: P2)

**Goal**: Generate pink noise with -3dB/octave spectral rolloff

**Independent Test**: Generate pink noise and verify energy at 2kHz is ~3dB lower than at 1kHz

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T019 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T020 [P] [US2] Unit tests for PinkNoiseFilter in tests/unit/processors/noise_generator_test.cpp
  - Test: filter state initializes to zero
  - Test: process() returns valid samples
  - Test: reset() clears filter state

- [ ] T021 [P] [US2] Spectral test for pink noise slope
  - Test: energy at 2kHz is ~3dB lower than 1kHz (within 1dB tolerance)
  - Test: energy at 4kHz is ~6dB lower than 1kHz (within 2dB tolerance)
  - Test: output samples in [-1.0, 1.0] range

### 4.3 Implementation for User Story 2

- [ ] T022 [US2] Create PinkNoiseFilter class (internal to noise_generator.h)
  - Add 7 filter state variables (b0-b6) per Paul Kellet's algorithm
  - Add process(float white) method returning filtered pink sample
  - Add reset() method to clear state

- [ ] T023 [US2] Add pink noise channel to NoiseGenerator
  - Add PinkNoiseFilter member
  - Add pink noise level and enabled members
  - Implement setNoiseLevel/setNoiseEnabled for Pink type
  - Add OnePoleSmoother for pink level

- [ ] T024 [US2] Integrate pink noise into process()
  - Generate white noise → filter through PinkNoiseFilter
  - Apply smoothed level gain
  - Mix with white noise if both enabled

### 4.4 Verification

- [ ] T025 [US2] Verify all US2 tests pass

### 4.5 Cross-Platform Verification (MANDATORY)

- [ ] T026 [US2] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 4.6 Commit (MANDATORY)

- [ ] T027 [US2] **Commit completed User Story 2 work** (pink noise generation)

**Checkpoint**: White + Pink noise should both work independently

---

## Phase 5: User Story 3 - Tape Hiss Generation (Priority: P3)

**Goal**: Generate signal-dependent tape hiss with characteristic spectral shape

**Independent Test**: Provide varying input levels and verify hiss modulates accordingly

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T028 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T029 [P] [US3] Unit tests for tape hiss in tests/unit/processors/noise_generator_test.cpp
  - Test: hiss output when input signal present
  - Test: hiss reduced when input signal is silent (floor level)
  - Test: setTapeHissParams() configures floor and sensitivity
  - Test: high-shelf spectral shaping (brighter than pink)

- [ ] T030 [P] [US3] Signal-dependent modulation test
  - Test: loud input → louder hiss
  - Test: quiet input → quieter hiss (above floor)
  - Test: smooth modulation (no sudden jumps)

### 5.3 Implementation for User Story 3

- [ ] T031 [US3] Add tape hiss parameters to NoiseGenerator
  - Add tapeHissFloorDb_ member
  - Add tapeHissSensitivity_ member
  - Implement setTapeHissParams(floorDb, sensitivity)

- [ ] T032 [US3] Add envelope follower for signal detection
  - Include EnvelopeFollower from dsp/processors/envelope_follower.h
  - Configure for appropriate attack/release (~10ms/100ms)
  - Store envelope follower member

- [ ] T033 [US3] Add high-shelf filter for tape hiss spectrum
  - Add Biquad member for hiss shaping
  - Configure high-shelf at ~5kHz, +3dB gain
  - Apply in prepare() when sample rate known

- [ ] T034 [US3] Implement tape hiss generation
  - Generate pink noise base
  - Apply high-shelf filter
  - Modulate level by envelope follower output
  - Apply floor level minimum
  - Mix with other enabled noise types

### 5.4 Verification

- [ ] T035 [US3] Verify all US3 tests pass

### 5.5 Cross-Platform Verification (MANDATORY)

- [ ] T036 [US3] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 5.6 Commit (MANDATORY)

- [ ] T037 [US3] **Commit completed User Story 3 work** (tape hiss)

**Checkpoint**: Tape hiss should modulate with signal level

---

## Phase 6: User Story 4 - Vinyl Crackle Generation (Priority: P4)

**Goal**: Generate random clicks/pops at configurable density with surface noise

**Independent Test**: Generate crackle and count impulses over time, verify density matches configuration

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T038 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T039 [P] [US4] Unit tests for vinyl crackle in tests/unit/processors/noise_generator_test.cpp
  - Test: crackle output contains impulses (peak detection)
  - Test: setCrackleParams() configures density and surface noise
  - Test: density of 1.0Hz produces ~8-12 clicks per 10 seconds (Poisson variance)
  - Test: density of 10Hz produces ~100 clicks per 10 seconds (approximate)

- [ ] T040 [P] [US4] Impulse amplitude distribution test
  - Test: impulses have varying amplitudes (not all same)
  - Test: most impulses are small, few are large (exponential-ish)

- [ ] T041 [P] [US4] Surface noise test
  - Test: continuous low-level noise between impulses when enabled
  - Test: surface noise level configurable

### 6.3 Implementation for User Story 4

- [ ] T042 [US4] Add crackle parameters to NoiseGenerator
  - Add crackleDensity_ member (clicks per second)
  - Add surfaceNoiseDb_ member
  - Implement setCrackleParams(density, surfaceDb)

- [ ] T043 [US4] Create CrackleState struct (internal)
  - Add amplitude, decay, active members
  - Track current click envelope state

- [ ] T044 [US4] Implement Poisson-distributed click timing
  - Per-sample probability = density / sampleRate
  - Use Xorshift32::nextUnipolar() for random check
  - Trigger click when random < probability

- [ ] T045 [US4] Implement exponential amplitude distribution
  - Use -log(random) * scale for amplitude
  - Clamp maximum amplitude
  - Apply fast attack, medium decay envelope to click

- [ ] T046 [US4] Implement surface noise
  - Filter white noise for "dusty" character
  - Apply surface noise level
  - Mix with crackle impulses

### 6.4 Verification

- [ ] T047 [US4] Verify all US4 tests pass

### 6.5 Cross-Platform Verification (MANDATORY)

- [ ] T048 [US4] **Verify IEEE 754 compliance**: Check test files for NaN detection → add logarithm tests to `-fno-fast-math` if std::log used in tests

### 6.6 Commit (MANDATORY)

- [ ] T049 [US4] **Commit completed User Story 4 work** (vinyl crackle)

**Checkpoint**: Vinyl crackle should produce audible clicks at configured density

---

## Phase 7: User Story 5 - Asperity Noise Generation (Priority: P5)

**Goal**: Generate tape head contact noise that follows signal envelope

**Independent Test**: Vary input signal and verify asperity noise intensity follows envelope

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T050 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T051 [P] [US5] Unit tests for asperity noise in tests/unit/processors/noise_generator_test.cpp
  - Test: asperity output when input signal present
  - Test: minimal asperity when input is silent (requires signal)
  - Test: setAsperityParams() configures floor and sensitivity
  - Test: broadband spectral character

- [ ] T052 [P] [US5] Signal-following test
  - Test: asperity intensity tracks input envelope
  - Test: faster response than tape hiss (different character)

### 7.3 Implementation for User Story 5

- [ ] T053 [US5] Add asperity parameters to NoiseGenerator
  - Add asperityFloorDb_ member
  - Add asperitySensitivity_ member
  - Implement setAsperityParams(floorDb, sensitivity)

- [ ] T054 [US5] Implement asperity noise generation
  - Generate white noise base (broadband character)
  - Modulate by envelope follower with higher sensitivity
  - Apply floor level minimum
  - Slightly different envelope settings than tape hiss

### 7.4 Verification

- [ ] T055 [US5] Verify all US5 tests pass

### 7.5 Cross-Platform Verification (MANDATORY)

- [ ] T056 [US5] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 7.6 Commit (MANDATORY)

- [ ] T057 [US5] **Commit completed User Story 5 work** (asperity noise)

**Checkpoint**: Asperity noise should follow signal envelope with broadband character

---

## Phase 8: User Story 6 - Multi-Noise Mixing (Priority: P6)

**Goal**: Blend multiple noise types simultaneously with independent level controls

**Independent Test**: Enable multiple noise types and verify blended output contains characteristics of each

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T058 [US6] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T059 [P] [US6] Multi-noise mixing tests in tests/unit/processors/noise_generator_test.cpp
  - Test: white + pink enabled produces output with both characteristics
  - Test: tape hiss + crackle produces continuous hiss with impulses
  - Test: all 5 types enabled simultaneously
  - Test: isAnyEnabled() returns correct state

- [ ] T060 [P] [US6] Master level control test
  - Test: setMasterLevel() affects final output
  - Test: getMasterLevel() returns current value
  - Test: master level smoothing (no clicks)

- [ ] T061 [P] [US6] Edge case tests
  - Test: all levels at 0 → silence
  - Test: all types disabled → silence
  - Test: NaN input to signal-dependent types → safe output

### 8.3 Implementation for User Story 6

- [ ] T062 [US6] Add master level control
  - Add masterLevelDb_ member
  - Add masterSmoother_ OnePoleSmoother
  - Implement setMasterLevel(dB), getMasterLevel()

- [ ] T063 [US6] Implement channel mixing in process()
  - Sum all enabled noise channels
  - Apply per-channel smoothed gains
  - Apply master level gain
  - Implement isAnyEnabled() for early-out optimization

- [ ] T064 [US6] Handle edge cases
  - Check for NaN in sidechain input → treat as silence
  - Clamp final output to [-1.0, 1.0] if boost applied
  - Ensure silence when all disabled

### 8.4 Verification

- [ ] T065 [US6] Verify all US6 tests pass

### 8.5 Cross-Platform Verification (MANDATORY)

- [ ] T066 [US6] **Verify IEEE 754 compliance**: NaN detection used → add to `-fno-fast-math` list

### 8.6 Commit (MANDATORY)

- [ ] T067 [US6] **Commit completed User Story 6 work** (multi-noise mixing)

**Checkpoint**: All noise types can be mixed with independent control

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T068 [P] Performance verification: profile process() at 44.1kHz stereo, verify < 0.5% CPU (SC-005)
- [ ] T069 [P] Run quickstart.md validation - verify all code examples compile
- [ ] T070 Code cleanup: ensure consistent naming, remove dead code
- [ ] T071 [P] Verify real-time safety (FR-012): Run with AddressSanitizer or confirm no heap allocations in process() via code review
- [ ] T072 [P] Test maxBlockSize=8192 (FR-014): Add test case verifying process() handles 8192-sample blocks
- [ ] T073 [P] Composability test (FR-020): Verify NoiseGenerator chains correctly with another Layer 2 processor (e.g., Saturator or Filter)

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T074 **Update ARCHITECTURE.md** with new components:
  - Add Xorshift32 to Layer 0 (Core Utilities) section
  - Add NoiseGenerator to Layer 2 (DSP Processors) section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage example

### 10.2 Final Commit

- [ ] T075 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

- [ ] T076 **Review ALL FR-xxx requirements** (FR-001 through FR-020) from spec.md against implementation
- [ ] T077 **Review ALL SC-xxx success criteria** (SC-001 through SC-008) and verify measurable targets
- [ ] T078 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T079 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T080 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Final Commit

- [ ] T081 **Commit all spec work** to feature branch
- [ ] T082 **Verify all tests pass** with `dsp_tests.exe [noise]`

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup) → Phase 2 (Foundation) → User Stories (3-8) → Phase 9 (Polish) → Phase 10-11 (Docs/Verify)
                         ↓
                    BLOCKS ALL
                   USER STORIES
```

### User Story Dependencies

| Story | Depends On | Reason |
|-------|------------|--------|
| US1 (White) | Foundation only | Base noise generation |
| US2 (Pink) | US1 | Uses white noise as input to filter |
| US3 (Tape Hiss) | US2 | Uses pink noise + shaping |
| US4 (Crackle) | Foundation only | Independent algorithm |
| US5 (Asperity) | US1 | Uses white noise + envelope |
| US6 (Mixing) | US1-US5 | Combines all noise types |

### Parallel Opportunities

**After Foundation (Phase 2) completes:**
- US1 (White) and US4 (Crackle) can run in parallel (independent)
- US2 (Pink) must wait for US1
- US3 (Tape Hiss) must wait for US2
- US5 (Asperity) must wait for US1

**Optimal parallel strategy:**
```
Foundation → [US1 parallel with US4] → [US2 parallel with US5] → US3 → US6
```

---

## Parallel Example: Foundation Phase

```bash
# Launch all foundation tests together:
Task: "Write tests for Xorshift32 in tests/unit/core/random_test.cpp"

# Launch all foundation implementations together:
Task: "Create Xorshift32 in src/dsp/core/random.h"
Task: "Create NoiseType enum in src/dsp/processors/noise_generator.h"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 2: Foundation (Xorshift32, skeleton)
2. Complete Phase 3: User Story 1 (white noise)
3. **STOP and VALIDATE**: Test white noise independently
4. This delivers basic noise generation capability

### Incremental Delivery

1. Foundation → White Noise (MVP) → Demo
2. Add Pink Noise → Demo enhanced audio character
3. Add Tape Hiss → Demo signal-dependent behavior
4. Add Vinyl Crackle → Demo lo-fi effects
5. Add Asperity → Demo full tape character
6. Add Mixing → Demo composite effects

### Test Tags

Use Catch2 tags for selective testing:
- `[noise]` - all noise tests
- `[US1]` - white noise tests
- `[US2]` - pink noise tests
- `[US3]` - tape hiss tests
- `[US4]` - crackle tests
- `[US5]` - asperity tests
- `[US6]` - mixing tests
- `[random]` - RNG tests

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

---

# PHASE 2: Extended Noise Types (US7-US15)

> **Context**: Phase 1 (US1-US6) is complete with 41 tests and 229,772 assertions passing.
> Phase 2 adds 9 additional noise types based on deep research into analog character.

---

## Phase 12: User Story 7 - Brown/Red Noise (Priority: P7)

**Goal**: Generate brown noise with -6dB/octave spectral rolloff (1/f² spectrum)

**Independent Test**: Generate brown noise and verify energy at 2kHz is ~6dB lower than at 1kHz

### 12.1 Pre-Implementation (MANDATORY)

- [ ] T083 [US7] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 12.2 Tests for User Story 7 (Write FIRST - Must FAIL)

- [ ] T084 [P] [US7] Unit tests for brown noise in tests/unit/processors/noise_generator_test.cpp
  - Test: process() outputs samples in [-1.0, 1.0] range when brown noise enabled
  - Test: output is non-zero when brown noise enabled
  - Test: output is zero when brown noise disabled
  - Test: setNoiseLevel() affects brown noise amplitude

- [ ] T085 [P] [US7] Spectral test for brown noise slope
  - Test: energy at 2kHz is ~6dB lower than 1kHz (within 1.5dB tolerance)
  - Test: energy at 4kHz is ~12dB lower than 1kHz (within 2dB tolerance)

### 12.3 Implementation for User Story 7

- [ ] T086 [US7] Add Brown to NoiseType enum in src/dsp/processors/noise_generator.h
  - Update kNumNoiseTypes constant

- [ ] T087 [US7] Implement brown noise filter (leaky integrator)
  - Add brownPrevious_ state variable for integration
  - Brown = previous + (white * leak); previous = brown
  - Leak coefficient ~0.02 for -6dB/octave slope
  - Add reset handling for brown state

- [ ] T088 [US7] Add brown noise channel to NoiseGenerator
  - Add brownLevelDb_ and brownEnabled_ members
  - Add brownLevelSmoother_ OnePoleSmoother
  - Integrate into generateNoiseSample()

### 12.4 Verification

- [ ] T089 [US7] Verify all US7 tests pass

### 12.5 Cross-Platform Verification (MANDATORY)

- [ ] T090 [US7] **Verify IEEE 754 compliance**: Add test file to `-fno-fast-math` list if NaN detection used

### 12.6 Commit (MANDATORY)

- [ ] T091 [US7] **Commit completed User Story 7 work** (brown noise)

---

## Phase 13: User Story 8 - Blue Noise (Priority: P8)

**Goal**: Generate blue noise with +3dB/octave spectral rise

**Independent Test**: Generate blue noise and verify energy at 2kHz is ~3dB higher than at 1kHz

### 13.1 Pre-Implementation (MANDATORY)

- [ ] T092 [US8] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 13.2 Tests for User Story 8 (Write FIRST - Must FAIL)

- [ ] T093 [P] [US8] Unit tests for blue noise in tests/unit/processors/noise_generator_test.cpp
  - Test: process() outputs samples in [-1.0, 1.0] range when blue noise enabled
  - Test: output is non-zero when blue noise enabled
  - Test: setNoiseLevel() affects blue noise amplitude

- [ ] T094 [P] [US8] Spectral test for blue noise slope
  - Test: energy at 2kHz is ~3dB higher than 1kHz (within 1.5dB tolerance)
  - Test: energy at 4kHz is ~6dB higher than 1kHz (within 2dB tolerance)

### 13.3 Implementation for User Story 8

- [ ] T095 [US8] Add Blue to NoiseType enum in src/dsp/processors/noise_generator.h

- [ ] T096 [US8] Implement blue noise filter (first-order differentiator)
  - Blue noise = pink noise + differentiation
  - Use high-pass filter on pink noise
  - Or: first-order differentiator on white noise
  - Add bluePrevious_ state for differentiation

- [ ] T097 [US8] Add blue noise channel to NoiseGenerator
  - Add blueLevelDb_ and blueEnabled_ members
  - Add blueLevelSmoother_ OnePoleSmoother
  - Integrate into generateNoiseSample()

### 13.4 Verification

- [ ] T098 [US8] Verify all US8 tests pass

### 13.5 Cross-Platform Verification (MANDATORY)

- [ ] T099 [US8] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 13.6 Commit (MANDATORY)

- [ ] T100 [US8] **Commit completed User Story 8 work** (blue noise)

---

## Phase 14: User Story 9 - Violet Noise (Priority: P9)

**Goal**: Generate violet noise with +6dB/octave spectral rise (differentiated white noise)

**Independent Test**: Generate violet noise and verify energy at 2kHz is ~6dB higher than at 1kHz

### 14.1 Pre-Implementation (MANDATORY)

- [ ] T101 [US9] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 14.2 Tests for User Story 9 (Write FIRST - Must FAIL)

- [ ] T102 [P] [US9] Unit tests for violet noise in tests/unit/processors/noise_generator_test.cpp
  - Test: process() outputs samples in [-1.0, 1.0] range when violet noise enabled
  - Test: output is non-zero when violet noise enabled
  - Test: setNoiseLevel() affects violet noise amplitude

- [ ] T103 [P] [US9] Spectral test for violet noise slope
  - Test: energy at 2kHz is ~6dB higher than 1kHz (within 1.5dB tolerance)
  - Test: energy at 4kHz is ~12dB higher than 1kHz (within 2dB tolerance)
  - Test: violet noise sounds brighter than blue noise

### 14.3 Implementation for User Story 9

- [ ] T104 [US9] Add Violet to NoiseType enum in src/dsp/processors/noise_generator.h

- [ ] T105 [US9] Implement violet noise (differentiated white noise)
  - Violet = current_white - previous_white
  - This is first-order differentiator on white noise
  - Add violetPrevious_ state variable
  - Normalize output to [-1, 1] range

- [ ] T106 [US9] Add violet noise channel to NoiseGenerator
  - Add violetLevelDb_ and violetEnabled_ members
  - Add violetLevelSmoother_ OnePoleSmoother
  - Integrate into generateNoiseSample()

### 14.4 Verification

- [ ] T107 [US9] Verify all US9 tests pass

### 14.5 Cross-Platform Verification (MANDATORY)

- [ ] T108 [US9] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 14.6 Commit (MANDATORY)

- [ ] T109 [US9] **Commit completed User Story 9 work** (violet noise)

---

## Phase 15: User Story 10 - Grey Noise (Priority: P10)

**Goal**: Generate grey noise (inverse A-weighting) for perceptually flat loudness

**Independent Test**: Generate grey noise and verify perceived loudness is equal at 100Hz, 1kHz, and 10kHz

### 15.1 Pre-Implementation (MANDATORY)

- [ ] T110 [US10] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 15.2 Tests for User Story 10 (Write FIRST - Must FAIL)

- [ ] T111 [P] [US10] Unit tests for grey noise in tests/unit/processors/noise_generator_test.cpp
  - Test: process() outputs samples in [-1.0, 1.0] range when grey noise enabled
  - Test: output is non-zero when grey noise enabled
  - Test: setNoiseLevel() affects grey noise amplitude

- [ ] T112 [P] [US10] Perceptual test for grey noise
  - Test: energy at 100Hz is boosted relative to white noise
  - Test: energy at 3-4kHz (hearing sensitivity peak) is reduced
  - Test: energy at 10kHz follows inverse A-weighting curve

### 15.3 Implementation for User Story 10

- [ ] T113 [US10] Add Grey to NoiseType enum in src/dsp/processors/noise_generator.h

- [ ] T114 [US10] Implement grey noise EQ filter (inverse A-weighting)
  - Add greyFilter_ Biquad (or cascaded biquads) for inverse A-weighting
  - Configure filter coefficients based on sample rate
  - Boost lows (~100Hz), cut 3-4kHz, adjust highs per ISO 226

- [ ] T115 [US10] Add grey noise channel to NoiseGenerator
  - Add greyLevelDb_ and greyEnabled_ members
  - Add greyLevelSmoother_ OnePoleSmoother
  - Integrate into generateNoiseSample()

### 15.4 Verification

- [ ] T116 [US10] Verify all US10 tests pass

### 15.5 Cross-Platform Verification (MANDATORY)

- [ ] T117 [US10] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 15.6 Commit (MANDATORY)

- [ ] T118 [US10] **Commit completed User Story 10 work** (grey noise)

---

## Phase 16: User Story 11 - Velvet Noise (Priority: P11)

**Goal**: Generate velvet noise (sparse random impulses) for smooth character

**Independent Test**: Generate velvet noise at 1000 impulses/sec and verify approximately 1000 non-zero samples

### 16.1 Pre-Implementation (MANDATORY)

- [ ] T119 [US11] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 16.2 Tests for User Story 11 (Write FIRST - Must FAIL)

- [ ] T120 [P] [US11] Unit tests for velvet noise in tests/unit/processors/noise_generator_test.cpp
  - Test: process() outputs samples when velvet noise enabled
  - Test: most samples are zero (sparse)
  - Test: setNoiseLevel() affects velvet noise amplitude
  - Test: setVelvetDensity() changes impulse count

- [ ] T121 [P] [US11] Impulse distribution tests
  - Test: at 1000 impulses/sec for 1 second, ~1000 non-zero samples (±10%)
  - Test: impulse polarity is ~50% positive, ~50% negative
  - Test: impulses are randomly distributed (not periodic)

- [ ] T122 [P] [US11] Density range test
  - Test: density 100 impulses/sec produces sparse output
  - Test: density 10000 impulses/sec sounds similar to white noise
  - Test: density 20000 impulses/sec (max) approaches continuous noise

### 16.3 Implementation for User Story 11

- [ ] T123 [US11] Add Velvet to NoiseType enum in src/dsp/processors/noise_generator.h

- [ ] T124 [US11] Add velvet noise parameters
  - Add velvetDensity_ member (impulses per second, range 100-20000)
  - Add setVelvetDensity(float density) method
  - Add getVelvetDensity() method

- [ ] T125 [US11] Implement velvet noise generation
  - Calculate probability per sample = density / sampleRate
  - For each sample: if random < probability, output ±1.0 (random polarity)
  - Otherwise output 0.0
  - Apply level gain to non-zero samples

- [ ] T126 [US11] Add velvet noise channel to NoiseGenerator
  - Add velvetLevelDb_ and velvetEnabled_ members
  - Add velvetLevelSmoother_ OnePoleSmoother
  - Integrate into generateNoiseSample()

### 16.4 Verification

- [ ] T127 [US11] Verify all US11 tests pass

### 16.5 Cross-Platform Verification (MANDATORY)

- [ ] T128 [US11] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 16.6 Commit (MANDATORY)

- [ ] T129 [US11] **Commit completed User Story 11 work** (velvet noise)

---

## Phase 17: User Story 12 - Vinyl Rumble (Priority: P12)

**Goal**: Generate vinyl rumble (low-frequency motor/platter noise) concentrated below 100Hz

**Independent Test**: Generate rumble and verify >90% energy is below 100Hz

### 17.1 Pre-Implementation (MANDATORY)

- [ ] T130 [US12] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 17.2 Tests for User Story 12 (Write FIRST - Must FAIL)

- [ ] T131 [P] [US12] Unit tests for vinyl rumble in tests/unit/processors/noise_generator_test.cpp
  - Test: process() outputs samples when vinyl rumble enabled
  - Test: output is non-zero when rumble enabled
  - Test: setNoiseLevel() affects rumble amplitude
  - Test: setRumbleSpeed() accepts 33, 45, 78 RPM values

- [ ] T132 [P] [US12] Spectral tests for vinyl rumble
  - Test: >90% energy is below 100Hz
  - Test: fundamental frequency matches motor speed (33 RPM = 0.55Hz)
  - Test: harmonics of rotation frequency are present

- [ ] T133 [P] [US12] Motor speed tests
  - Test: 33 RPM produces ~0.55Hz fundamental
  - Test: 45 RPM produces ~0.75Hz fundamental
  - Test: 78 RPM produces ~1.3Hz fundamental

### 17.3 Implementation for User Story 12

- [ ] T134 [US12] Add VinylRumble to NoiseType enum in src/dsp/processors/noise_generator.h

- [ ] T135 [US12] Add vinyl rumble parameters
  - Add rumbleSpeed_ member (33.0f, 45.0f, or 78.0f RPM)
  - Add setRumbleSpeed(float rpm) method
  - Add getRumbleSpeed() method

- [ ] T136 [US12] Implement vinyl rumble generation
  - Generate low-frequency oscillation at rotation frequency (rpm/60 Hz)
  - Add harmonics (2nd, 3rd, 4th) at decreasing amplitudes
  - Add small random variation to simulate motor irregularity
  - Apply low-pass filter at ~100Hz to ensure subsonic character

- [ ] T137 [US12] Add rumble filter (lowpass at ~100Hz)
  - Add rumbleFilter_ Biquad configured as lowpass
  - Update filter coefficients in prepare() based on sample rate

- [ ] T138 [US12] Add vinyl rumble channel to NoiseGenerator
  - Add rumbleLevelDb_ and rumbleEnabled_ members
  - Add rumbleLevelSmoother_ OnePoleSmoother
  - Add rumblePhase_ for oscillator state
  - Integrate into generateNoiseSample()

### 17.4 Verification

- [ ] T139 [US12] Verify all US12 tests pass

### 17.5 Cross-Platform Verification (MANDATORY)

- [ ] T140 [US12] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 17.6 Commit (MANDATORY)

- [ ] T141 [US12] **Commit completed User Story 12 work** (vinyl rumble)

---

## Phase 18: User Story 13 - Wow & Flutter (Priority: P13)

**Goal**: Generate pitch modulation effects for tape speed variation

**Independent Test**: Process a test tone and verify pitch deviation matches configured depth

### 18.1 Pre-Implementation (MANDATORY)

- [ ] T142 [US13] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 18.2 Tests for User Story 13 (Write FIRST - Must FAIL)

- [ ] T143 [P] [US13] Unit tests for wow in tests/unit/processors/noise_generator_test.cpp
  - Test: wow at 0.5Hz rate, 0.1% depth modulates pitch
  - Test: setWowParams() configures rate and depth
  - Test: wow is smooth, low-frequency modulation

- [ ] T144 [P] [US13] Unit tests for flutter
  - Test: flutter at 10Hz rate, 0.05% depth modulates pitch
  - Test: setFlutterParams() configures rate and depth
  - Test: flutter is faster, more rapid modulation

- [ ] T145 [P] [US13] Combined wow/flutter tests
  - Test: wow and flutter can be enabled simultaneously
  - Test: randomization varies rate and depth naturally
  - Test: no audible clicks or discontinuities

- [ ] T146 [P] [US13] Pitch deviation measurement
  - Test: 1kHz tone with 0.1% wow depth shows ±1Hz variation
  - Test: modulation rate matches configured rate within 10%

### 18.3 Implementation for User Story 13

- [ ] T147 [US13] Add WowFlutter to NoiseType enum in src/dsp/processors/noise_generator.h
  - Note: This is a modulation effect, different from additive noise

- [ ] T148 [US13] Add wow/flutter parameters
  - Add wowRate_ (0.1-4Hz), wowDepth_ (0-1%)
  - Add flutterRate_ (4-100Hz), flutterDepth_ (0-0.5%)
  - Add wowEnabled_, flutterEnabled_ flags
  - Add setWowParams(rate, depth), setFlutterParams(rate, depth)
  - Add randomization amount parameters

- [ ] T149 [US13] Implement wow modulation LFO
  - Use existing LFO primitive or create internal oscillator
  - Triangle/sine wave at wow rate
  - Apply random variation to rate (±20%)
  - Output normalized modulation signal

- [ ] T150 [US13] Implement flutter modulation
  - Higher-frequency oscillator (4-100Hz)
  - Sum of multiple frequencies for realistic flutter
  - Apply random variation to depth

- [ ] T151 [US13] Implement modulated delay for pitch variation
  - Create short delay line (few ms max delay)
  - Modulate delay time by wow/flutter signal
  - Pitch shift = delay_time_change * sample_rate / delay_length
  - Use linear interpolation for smooth modulation

- [ ] T152 [US13] Add wow/flutter processing path
  - process() applies modulation to input signal (if passed)
  - Or: provide separate processWowFlutter(input, output, numSamples)
  - Ensure no allocations, use pre-allocated delay buffer

### 18.4 Verification

- [ ] T153 [US13] Verify all US13 tests pass

### 18.5 Cross-Platform Verification (MANDATORY)

- [ ] T154 [US13] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 18.6 Commit (MANDATORY)

- [ ] T155 [US13] **Commit completed User Story 13 work** (wow & flutter)

---

## Phase 19: User Story 14 - Modulation Noise (Priority: P14)

**Goal**: Generate signal-correlated noise that scales with input level (no floor)

**Independent Test**: Process varying signal levels and verify noise amplitude correlates with input

### 19.1 Pre-Implementation (MANDATORY)

- [ ] T156 [US14] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 19.2 Tests for User Story 14 (Write FIRST - Must FAIL)

- [ ] T157 [P] [US14] Unit tests for modulation noise in tests/unit/processors/noise_generator_test.cpp
  - Test: modulation noise is zero when input is silent
  - Test: modulation noise increases with input level
  - Test: setModulationNoiseParams() configures sensitivity and roughness

- [ ] T158 [P] [US14] Signal correlation tests
  - Test: loud input produces more modulation noise than quiet input
  - Test: noise amplitude correlates with signal amplitude (r > 0.8)
  - Test: no floor noise when input is zero (unlike tape hiss)

- [ ] T159 [P] [US14] Roughness parameter tests
  - Test: low roughness produces smoother noise
  - Test: high roughness produces more granular/gritty noise
  - Test: roughness parameter range [0, 1]

### 19.3 Implementation for User Story 14

- [ ] T160 [US14] Add ModulationNoise to NoiseType enum in src/dsp/processors/noise_generator.h

- [ ] T161 [US14] Add modulation noise parameters
  - Add modulationSensitivity_ (0-2, default 1.0)
  - Add modulationRoughness_ (0-1, default 0.5)
  - Add setModulationNoiseParams(sensitivity, roughness)
  - Add getModulationSensitivity(), getModulationRoughness()

- [ ] T162 [US14] Implement modulation noise generation
  - Use envelope follower on input signal (fast attack, medium release)
  - Multiply white noise by envelope (no floor!)
  - Apply sensitivity scaling
  - For roughness: blend between filtered and unfiltered noise

- [ ] T163 [US14] Add modulation noise channel to NoiseGenerator
  - Add modulationLevelDb_ and modulationEnabled_ members
  - Add modulationLevelSmoother_ OnePoleSmoother
  - Add modulationEnvelope_ EnvelopeFollower (separate from tape hiss)
  - Integrate into generateNoiseSample()

### 19.4 Verification

- [ ] T164 [US14] Verify all US14 tests pass

### 19.5 Cross-Platform Verification (MANDATORY)

- [ ] T165 [US14] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 19.6 Commit (MANDATORY)

- [ ] T166 [US14] **Commit completed User Story 14 work** (modulation noise)

---

## Phase 20: User Story 15 - Radio Static (Priority: P15)

**Goal**: Generate band-limited atmospheric noise with interference and fading

**Independent Test**: Generate radio static and verify bandwidth matches configured mode

### 20.1 Pre-Implementation (MANDATORY)

- [ ] T167 [US15] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 20.2 Tests for User Story 15 (Write FIRST - Must FAIL)

- [ ] T168 [P] [US15] Unit tests for radio static in tests/unit/processors/noise_generator_test.cpp
  - Test: process() outputs samples when radio static enabled
  - Test: setNoiseLevel() affects radio static amplitude
  - Test: setRadioStaticParams() configures bandwidth, interference, fading

- [ ] T169 [P] [US15] Bandwidth tests
  - Test: AM mode limits bandwidth to ~5kHz
  - Test: FM mode limits bandwidth to ~15kHz
  - Test: shortwave mode has variable bandwidth

- [ ] T170 [P] [US15] Interference and fading tests
  - Test: interference adds crackle/pops when enabled
  - Test: fading produces slow amplitude modulation when enabled
  - Test: interference density is configurable

### 20.3 Implementation for User Story 15

- [ ] T171 [US15] Add RadioStatic to NoiseType enum in src/dsp/processors/noise_generator.h

- [ ] T172 [US15] Add RadioStaticMode enum
  - Define: AM (~5kHz), FM (~15kHz), Shortwave (variable)
  - Add staticMode_ member
  - Add setRadioStaticMode(RadioStaticMode mode)

- [ ] T173 [US15] Add radio static parameters
  - Add staticInterference_ (0-1, amount of crackle)
  - Add staticFading_ (0-1, amount of amplitude fading)
  - Add staticFadingRate_ (0.1-2Hz, speed of fading)
  - Add setRadioStaticParams(interference, fading, fadingRate)

- [ ] T174 [US15] Implement band-limited noise generation
  - Generate white noise
  - Apply low-pass filter based on mode (5kHz, 15kHz, or variable)
  - Add staticFilter_ Biquad for band limiting

- [ ] T175 [US15] Implement atmospheric interference
  - Add random crackle impulses (similar to vinyl but less dense)
  - Scale by interference parameter
  - Create "ionospheric" bursts of static

- [ ] T176 [US15] Implement signal fading
  - Add low-frequency oscillator for amplitude modulation
  - Random variation in fading rate
  - Simulate ionospheric propagation effects

- [ ] T177 [US15] Add radio static channel to NoiseGenerator
  - Add staticLevelDb_ and staticEnabled_ members
  - Add staticLevelSmoother_ OnePoleSmoother
  - Add staticFadeLFO_ for fading modulation
  - Integrate into generateNoiseSample()

### 20.4 Verification

- [ ] T178 [US15] Verify all US15 tests pass

### 20.5 Cross-Platform Verification (MANDATORY)

- [ ] T179 [US15] **Verify IEEE 754 compliance**: Check test files for NaN detection

### 20.6 Commit (MANDATORY)

- [ ] T180 [US15] **Commit completed User Story 15 work** (radio static)

---

## Phase 21: Polish & Cross-Cutting Concerns (Phase 2)

**Purpose**: Improvements that affect Phase 2 user stories

- [ ] T181 [P] Performance verification: profile process() with all 14 noise types enabled, verify < 1% CPU at 44.1kHz
- [ ] T182 [P] Code cleanup: ensure consistent naming for new noise types, remove dead code
- [ ] T183 [P] Verify real-time safety for Phase 2: confirm no heap allocations in new process() paths
- [ ] T184 [P] Test maxBlockSize=8192 with all noise types enabled
- [ ] T185 [P] Integration test: verify new noise types compose correctly with existing processors

---

## Phase 22: Documentation Update (Phase 2)

**Purpose**: Update living architecture documentation

### 22.1 Architecture Documentation Update

- [ ] T186 **Update ARCHITECTURE.md** with Phase 2 components:
  - Document new noise types in Layer 2 section
  - Add usage examples for colored noise, velvet noise
  - Add usage examples for wow/flutter modulation
  - Document radio static bandwidth modes

### 22.2 Update Contracts

- [ ] T187 **Update contracts/noise_generator.h** with new API:
  - Add new NoiseType enum values
  - Add new parameter methods (setVelvetDensity, setWowParams, etc.)
  - Add RadioStaticMode enum

### 22.3 Final Commit

- [ ] T188 **Commit documentation updates**

---

## Phase 23: Completion Verification (Phase 2)

**Purpose**: Honestly verify all Phase 2 requirements are met

### 23.1 Requirements Verification

- [ ] T189 **Review ALL Phase 2 FR-xxx requirements** (FR-021 through FR-040) from spec.md
- [ ] T190 **Review ALL Phase 2 SC-xxx success criteria** (SC-009 through SC-018)
- [ ] T191 **Search for cheating patterns** in Phase 2 implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 23.2 Fill Compliance Table in spec.md

- [ ] T192 **Update spec.md compliance table** for FR-021 through FR-040
- [ ] T193 **Update spec.md success criteria** for SC-009 through SC-018
- [ ] T194 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 23.3 Final Commit

- [ ] T195 **Commit all Phase 2 spec work** to feature branch
- [ ] T196 **Verify all tests pass** with `dsp_tests.exe [noise]`

---

## Phase 2 Dependencies & Execution Order

### Phase Dependencies

```
Phase 12-16 (Colored + Velvet) → Can run in parallel (independent algorithms)
Phase 17 (Vinyl Rumble) → Depends on Phase 12 (shares filtering patterns)
Phase 18 (Wow/Flutter) → Independent (modulation effect, not additive noise)
Phase 19 (Modulation Noise) → Depends on Phase 3 (US3 tape hiss patterns)
Phase 20 (Radio Static) → Depends on Phase 6 (US4 crackle patterns)
Phase 21-23 → After all US phases complete
```

### User Story Dependencies (Phase 2)

| Story | Depends On | Reason |
|-------|------------|--------|
| US7 (Brown) | Foundation | Simple integration filter |
| US8 (Blue) | US2 (Pink) | Uses pink noise + differentiation |
| US9 (Violet) | Foundation | Simple differentiation of white |
| US10 (Grey) | Foundation | EQ filter on white noise |
| US11 (Velvet) | Foundation | Independent sparse impulses |
| US12 (Rumble) | Foundation | Low-frequency oscillator + filter |
| US13 (Wow/Flutter) | Layer 1 DelayLine | Modulated delay for pitch shift |
| US14 (ModNoise) | US3 (Tape Hiss) | Similar envelope following pattern |
| US15 (Radio) | US4 (Crackle) | Similar impulse generation |

### Parallel Opportunities

**Colored noise (US7-US10) can all run in parallel:**
```
[US7 Brown || US8 Blue || US9 Violet || US10 Grey] → verify together
```

**Independent effects:**
```
[US11 Velvet || US12 Rumble || US13 Wow/Flutter] → verify together
```

**Signal-dependent types:**
```
US14 ModNoise → US15 Radio (sequential, share patterns)
```

---

## Test Tags (Phase 2)

Use Catch2 tags for selective testing:
- `[US7]` - brown noise tests
- `[US8]` - blue noise tests
- `[US9]` - violet noise tests
- `[US10]` - grey noise tests
- `[US11]` - velvet noise tests
- `[US12]` - rumble tests
- `[US13]` - wow/flutter tests
- `[US14]` - modulation noise tests
- `[US15]` - radio static tests
- `[phase2]` - all Phase 2 tests

---

## Summary

**Phase 2 Total Tasks**: 114 tasks (T083-T196)
**New Noise Types**: 9 (Brown, Blue, Violet, Grey, Velvet, Rumble, Wow/Flutter, ModNoise, Radio)
**New FRs**: 20 (FR-021 through FR-040)
**New SCs**: 10 (SC-009 through SC-018)
