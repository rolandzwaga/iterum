# Tasks: Biquad Filter Primitive

**Input**: Design documents from `/specs/004-biquad-filter/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/biquad.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Create foundational structures and test file skeleton

- [ ] T001 Create header file skeleton in `src/dsp/primitives/biquad.h` with include guards and namespace
- [ ] T002 Create test file skeleton in `tests/unit/primitives/biquad_test.cpp` with Catch2 includes
- [ ] T003 Add biquad_test.cpp to `tests/CMakeLists.txt` build configuration

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core structures that ALL user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 Implement FilterType enum (Lowpass, Highpass, Bandpass, Notch, Allpass, LowShelf, HighShelf, Peak) in `src/dsp/primitives/biquad.h`
- [ ] T005 Implement BiquadCoefficients struct (b0, b1, b2, a1, a2) in `src/dsp/primitives/biquad.h`
- [ ] T006 Add constexpr helper functions for sin/cos using Taylor series in `src/dsp/primitives/biquad.h` (for MSVC constexpr compatibility)
- [ ] T007 Add parameter clamping utility functions (frequency, Q, gain) in `src/dsp/primitives/biquad.h`
- [ ] T008 Build and verify foundational code compiles on all platforms

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Apply Basic Lowpass/Highpass Filter (Priority: P1)

**Goal**: Implement core TDF2 biquad filter with lowpass and highpass coefficient calculation

**Independent Test**: Process white noise through LP/HP filters, verify 12 dB/oct rolloff and correct cutoff frequency behavior

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T009 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T010 [P] [US1] Write tests for Lowpass coefficient calculation (1000Hz at 44100Hz sample rate) in `tests/unit/primitives/biquad_test.cpp`
- [ ] T011 [P] [US1] Write tests for Highpass coefficient calculation (100Hz at 44100Hz sample rate) in `tests/unit/primitives/biquad_test.cpp`
- [ ] T012 [P] [US1] Write tests for TDF2 process() with known input/output pairs in `tests/unit/primitives/biquad_test.cpp`
- [ ] T013 [P] [US1] Write tests for 0dB at cutoff (-3dB point) verification in `tests/unit/primitives/biquad_test.cpp`
- [ ] T014 [P] [US1] Write tests for Q/resonance affecting peak at cutoff in `tests/unit/primitives/biquad_test.cpp`
- [ ] T015 [P] [US1] Write tests for reset() clearing filter state in `tests/unit/primitives/biquad_test.cpp`
- [ ] T016 [P] [US1] Write tests for processBlock() matching sample-by-sample in `tests/unit/primitives/biquad_test.cpp`

### 3.3 Implementation for User Story 1

- [ ] T017 [US1] Implement BiquadCoefficients::calculate() for FilterType::Lowpass in `src/dsp/primitives/biquad.h`
- [ ] T018 [US1] Implement BiquadCoefficients::calculate() for FilterType::Highpass in `src/dsp/primitives/biquad.h`
- [ ] T019 [US1] Implement Biquad class with TDF2 process(float) in `src/dsp/primitives/biquad.h`
- [ ] T020 [US1] Implement Biquad::configure() method in `src/dsp/primitives/biquad.h`
- [ ] T021 [US1] Implement Biquad::reset() method in `src/dsp/primitives/biquad.h`
- [ ] T022 [US1] Implement Biquad::processBlock() for buffer processing in `src/dsp/primitives/biquad.h`
- [ ] T023 [US1] Verify all US1 tests pass

### 3.4 Commit (MANDATORY)

- [ ] T024 [US1] **Commit completed User Story 1 work** (LP/HP filtering with TDF2)

**Checkpoint**: Basic lowpass/highpass filtering fully functional and tested

---

## Phase 4: User Story 2 - Configure Multiple Filter Types (Priority: P1)

**Goal**: Add coefficient calculation for all 8 filter types (BP, Notch, Allpass, LowShelf, HighShelf, Peak)

**Independent Test**: Configure each filter type and verify characteristic frequency response matches Audio EQ Cookbook reference

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T025 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T026 [P] [US2] Write tests for Bandpass coefficient calculation and frequency response in `tests/unit/primitives/biquad_test.cpp`
- [ ] T027 [P] [US2] Write tests for Notch coefficient calculation (verify null at center frequency) in `tests/unit/primitives/biquad_test.cpp`
- [ ] T028 [P] [US2] Write tests for Allpass coefficient calculation (flat magnitude, phase shift) in `tests/unit/primitives/biquad_test.cpp`
- [ ] T029 [P] [US2] Write tests for LowShelf coefficient calculation (+/- dB gain below cutoff) in `tests/unit/primitives/biquad_test.cpp`
- [ ] T030 [P] [US2] Write tests for HighShelf coefficient calculation (+/- dB gain above cutoff) in `tests/unit/primitives/biquad_test.cpp`
- [ ] T031 [P] [US2] Write tests for Peak/Parametric EQ coefficient calculation in `tests/unit/primitives/biquad_test.cpp`
- [ ] T032 [P] [US2] Write tests for gain=0dB producing bypass-like behavior for shelf/peak in `tests/unit/primitives/biquad_test.cpp`

### 4.3 Implementation for User Story 2

- [ ] T033 [US2] Implement BiquadCoefficients::calculate() for FilterType::Bandpass in `src/dsp/primitives/biquad.h`
- [ ] T034 [US2] Implement BiquadCoefficients::calculate() for FilterType::Notch in `src/dsp/primitives/biquad.h`
- [ ] T035 [US2] Implement BiquadCoefficients::calculate() for FilterType::Allpass in `src/dsp/primitives/biquad.h`
- [ ] T036 [US2] Implement BiquadCoefficients::calculate() for FilterType::LowShelf in `src/dsp/primitives/biquad.h`
- [ ] T037 [US2] Implement BiquadCoefficients::calculate() for FilterType::HighShelf in `src/dsp/primitives/biquad.h`
- [ ] T038 [US2] Implement BiquadCoefficients::calculate() for FilterType::Peak in `src/dsp/primitives/biquad.h`
- [ ] T039 [US2] Verify all US2 tests pass

### 4.4 Commit (MANDATORY)

- [ ] T040 [US2] **Commit completed User Story 2 work** (all 8 filter types)

**Checkpoint**: All filter types functional - complete filtering capability available

---

## Phase 5: User Story 3 - Cascade Filters for Steeper Slopes (Priority: P2)

**Goal**: Implement BiquadCascade template for 24/36/48 dB/oct slopes with Butterworth alignment

**Independent Test**: Cascade 2/4 stages and verify correct slope (24/48 dB/oct) at cutoff frequency

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T041 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T042 [P] [US3] Write tests for butterworthQ() helper function returning correct Q values in `tests/unit/primitives/biquad_test.cpp`
- [ ] T043 [P] [US3] Write tests for linkwitzRileyQ() helper function returning correct Q values in `tests/unit/primitives/biquad_test.cpp`
- [ ] T044 [P] [US3] Write tests for BiquadCascade<2> achieving 24 dB/oct slope in `tests/unit/primitives/biquad_test.cpp`
- [ ] T045 [P] [US3] Write tests for BiquadCascade<4> achieving 48 dB/oct slope in `tests/unit/primitives/biquad_test.cpp`
- [ ] T046 [P] [US3] Write tests for setButterworth() producing flat passband (no peaking) in `tests/unit/primitives/biquad_test.cpp`
- [ ] T047 [P] [US3] Write tests for setLinkwitzRiley() producing flat sum at crossover in `tests/unit/primitives/biquad_test.cpp`
- [ ] T048 [P] [US3] Write tests for cascade reset() clearing all stages in `tests/unit/primitives/biquad_test.cpp`

### 5.3 Implementation for User Story 3

- [ ] T049 [US3] Implement constexpr butterworthQ(stageIndex, totalStages) helper in `src/dsp/primitives/biquad.h`
- [ ] T050 [US3] Implement constexpr linkwitzRileyQ(stageIndex, totalStages) helper in `src/dsp/primitives/biquad.h`
- [ ] T051 [US3] Implement BiquadCascade<N> template class in `src/dsp/primitives/biquad.h`
- [ ] T052 [US3] Implement BiquadCascade::setButterworth() method in `src/dsp/primitives/biquad.h`
- [ ] T053 [US3] Implement BiquadCascade::setLinkwitzRiley() method in `src/dsp/primitives/biquad.h`
- [ ] T054 [US3] Implement BiquadCascade::process() and processBlock() in `src/dsp/primitives/biquad.h`
- [ ] T055 [US3] Add type aliases (Biquad24dB, Biquad36dB, Biquad48dB) in `src/dsp/primitives/biquad.h`
- [ ] T056 [US3] Verify all US3 tests pass

### 5.4 Commit (MANDATORY)

- [ ] T057 [US3] **Commit completed User Story 3 work** (cascaded filters)

**Checkpoint**: Steep filter slopes available for aggressive filtering needs

---

## Phase 6: User Story 4 - Smooth Coefficient Updates (Priority: P2)

**Goal**: Implement SmoothedBiquad class for click-free parameter modulation

**Independent Test**: Sweep cutoff frequency rapidly while processing audio, verify no clicks or pops

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T058 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T059 [P] [US4] Write tests for SmoothedBiquad coefficient interpolation in `tests/unit/primitives/biquad_test.cpp`
- [ ] T060 [P] [US4] Write tests for setSmoothingTime() affecting transition speed in `tests/unit/primitives/biquad_test.cpp`
- [ ] T061 [P] [US4] Write tests for snapToTarget() immediately applying coefficients in `tests/unit/primitives/biquad_test.cpp`
- [ ] T062 [P] [US4] Write tests for isSmoothing() returning correct state in `tests/unit/primitives/biquad_test.cpp`
- [ ] T063 [P] [US4] Write tests for no discontinuity when filter type changes in `tests/unit/primitives/biquad_test.cpp`

### 6.3 Implementation for User Story 4

- [ ] T064 [US4] Implement SmoothedBiquad class skeleton in `src/dsp/primitives/biquad.h`
- [ ] T065 [US4] Implement setSmoothingTime() using OnePoleSmoother from dsp_utils.h in `src/dsp/primitives/biquad.h`
- [ ] T066 [US4] Implement setTarget() for coefficient target setting in `src/dsp/primitives/biquad.h`
- [ ] T067 [US4] Implement SmoothedBiquad::process() with per-sample coefficient interpolation in `src/dsp/primitives/biquad.h`
- [ ] T068 [US4] Implement snapToTarget() and isSmoothing() methods in `src/dsp/primitives/biquad.h`
- [ ] T069 [US4] Verify all US4 tests pass

### 6.4 Commit (MANDATORY)

- [ ] T070 [US4] **Commit completed User Story 4 work** (smoothed coefficients)

**Checkpoint**: Filter modulation is click-free - ready for LFO-driven filter sweeps

---

## Phase 7: User Story 5 - Use in Feedback Path (Priority: P2)

**Goal**: Ensure filter stability with denormal handling and NaN protection

**Independent Test**: Run filter in 99% feedback loop for 10 seconds, verify no NaN/infinity and state decays to zero

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T071 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T072 [P] [US5] Write tests for denormal flushing (state < 1e-15 becomes zero) in `tests/unit/primitives/biquad_test.cpp`
- [ ] T073 [P] [US5] Write tests for NaN input returning 0 and resetting state in `tests/unit/primitives/biquad_test.cpp`
- [ ] T074 [P] [US5] Write tests for filter stability in simulated feedback loop in `tests/unit/primitives/biquad_test.cpp`
- [ ] T075 [P] [US5] Write tests for isStable() coefficient validation in `tests/unit/primitives/biquad_test.cpp`
- [ ] T076 [P] [US5] Write tests for isBypass() detecting unity gain coefficients in `tests/unit/primitives/biquad_test.cpp`
- [ ] T077 [P] [US5] Write tests for extreme Q values being clamped in `tests/unit/primitives/biquad_test.cpp`
- [ ] T078 [P] [US5] Write tests for near-Nyquist frequency clamping in `tests/unit/primitives/biquad_test.cpp`
- [ ] T079 [P] [US5] Write tests for constraint functions (minQ, maxQ, minFilterFrequency, maxFilterFrequency) in `tests/unit/primitives/biquad_test.cpp`

### 7.3 Implementation for User Story 5

- [ ] T080 [US5] Add flushDenormal() helper function in `src/dsp/primitives/biquad.h`
- [ ] T081 [US5] Add isFinite() helper using bit-level check (for -ffast-math compatibility) in `src/dsp/primitives/biquad.h`
- [ ] T082 [US5] Add denormal flushing to Biquad::process() in `src/dsp/primitives/biquad.h`
- [ ] T083 [US5] Add NaN input detection and state reset to Biquad::process() in `src/dsp/primitives/biquad.h`
- [ ] T084 [US5] Implement BiquadCoefficients::isStable() method in `src/dsp/primitives/biquad.h`
- [ ] T085 [US5] Implement BiquadCoefficients::isBypass() method in `src/dsp/primitives/biquad.h`
- [ ] T086 [US5] Add frequency/Q clamping in coefficient calculation in `src/dsp/primitives/biquad.h`
- [ ] T087 [US5] Verify all US5 tests pass

### 7.4 Commit (MANDATORY)

- [ ] T088 [US5] **Commit completed User Story 5 work** (stability & robustness)

**Checkpoint**: Filter is safe for feedback paths - no risk of runaway oscillation or CPU spikes

---

## Phase 8: User Story 6 - Compile-Time Coefficient Calculation (Priority: P3)

**Goal**: Enable constexpr coefficient calculation for static EQ configurations

**Independent Test**: Declare constexpr coefficients, verify they compile and match runtime equivalents

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T089 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T090 [P] [US6] Write tests for constexpr coefficient calculation compiling in `tests/unit/primitives/biquad_test.cpp`
- [ ] T091 [P] [US6] Write tests for constexpr coefficients matching runtime values in `tests/unit/primitives/biquad_test.cpp`
- [ ] T092 [P] [US6] Write tests for constexpr array initialization in `tests/unit/primitives/biquad_test.cpp`

### 8.3 Implementation for User Story 6

- [ ] T093 [US6] Mark BiquadCoefficients::calculate() as constexpr in `src/dsp/primitives/biquad.h`
- [ ] T094 [US6] Implement calculateConstexpr() using Taylor series for sin/cos in `src/dsp/primitives/biquad.h`
- [ ] T095 [US6] Ensure all coefficient paths are constexpr-compatible in `src/dsp/primitives/biquad.h`
- [ ] T096 [US6] Verify all US6 tests pass

### 8.4 Commit (MANDATORY)

- [ ] T097 [US6] **Commit completed User Story 6 work** (constexpr support)

**Checkpoint**: Compile-time coefficient calculation available for static configurations

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T098 [P] Add comprehensive docstrings/comments to all public API in `src/dsp/primitives/biquad.h`
- [ ] T099 [P] Verify all edge cases from spec are covered by tests
- [ ] T100 Run quickstart.md examples to validate documentation accuracy
- [ ] T101 Performance validation - verify < 0.1% CPU per instance at 44.1kHz

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T102 **Update ARCHITECTURE.md** with Biquad filter components:
  - Add Biquad to Layer 1 Primitives section
  - Add SmoothedBiquad to Layer 1 Primitives section
  - Add BiquadCascade<N> to Layer 1 Primitives section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples from quickstart.md

### 10.2 Final Commit

- [ ] T103 **Commit ARCHITECTURE.md updates**
- [ ] T104 Verify all spec work is committed to feature branch

**Checkpoint**: Spec implementation complete - ARCHITECTURE.md reflects all new functionality

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **US1 (Phase 3)**: Depends on Foundational - Core filter, MVP
- **US2 (Phase 4)**: Depends on US1 (uses same Biquad class)
- **US3 (Phase 5)**: Depends on US1 (cascades Biquad instances)
- **US4 (Phase 6)**: Depends on US1 (wraps Biquad with smoothing)
- **US5 (Phase 7)**: Depends on US1 (adds safety to Biquad)
- **US6 (Phase 8)**: Depends on Foundational (constexpr math helpers)
- **Polish (Phase 9)**: Depends on all user stories
- **Final (Phase 10)**: Depends on Polish

### User Story Dependencies

```
Foundational
    │
    ├──► US1 (LP/HP) ───► US2 (All Types) ──┐
    │         │                              │
    │         ├──────► US3 (Cascade) ────────┼──► Polish ──► Final
    │         │                              │
    │         ├──────► US4 (Smoothed) ───────┤
    │         │                              │
    │         └──────► US5 (Stability) ──────┘
    │
    └──► US6 (Constexpr) ────────────────────┘
```

### Parallel Opportunities

**Within User Story Tests**: All test tasks marked [P] within a story can run in parallel
**Between Stories**: US3, US4, US5 can run in parallel after US1 completes
**US6**: Can run in parallel with US1-US5 (only depends on Foundational)

---

## Summary

| Phase | User Story | Priority | Task Count | Description |
|-------|------------|----------|------------|-------------|
| 1 | Setup | - | 3 | File skeletons and build config |
| 2 | Foundational | - | 5 | FilterType, BiquadCoefficients, helpers |
| 3 | US1 | P1 | 16 | LP/HP filtering with TDF2 |
| 4 | US2 | P1 | 16 | All 8 filter types |
| 5 | US3 | P2 | 17 | Cascaded filters + Linkwitz-Riley (24/36/48 dB/oct) |
| 6 | US4 | P2 | 13 | Smoothed coefficient updates |
| 7 | US5 | P2 | 18 | Stability, denormal handling, constraint functions |
| 8 | US6 | P3 | 9 | Constexpr coefficient calculation |
| 9 | Polish | - | 4 | Documentation & validation |
| 10 | Final | - | 3 | ARCHITECTURE.md update |

**Total Tasks**: 104
**MVP Scope**: Phase 1-3 (US1 only) = 24 tasks for basic filtering capability
