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
