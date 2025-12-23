# Tasks: Saturation Processor

**Input**: Design documents from `/specs/009-saturation-processor/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## User Story Summary

| Story | Title | Priority | Dependencies |
|-------|-------|----------|--------------|
| US1 | Apply Basic Saturation | P1 | Foundation |
| US2 | Select Saturation Type | P1 | US1 |
| US3 | Control Input/Output Gain | P2 | US1 |
| US4 | Blend Dry/Wet Mix | P2 | US1 |
| US5 | Prevent Aliasing with Oversampling | P1 | Foundation |
| US6 | Automatic DC Blocking | P2 | US2 (for Tube/Diode) |
| US7 | Real-Time Safety | P1 | All implementation |

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

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, or NaN detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create file structure and basic project setup

- [ ] T001 Create saturation processor header file skeleton at src/dsp/processors/saturation_processor.h
- [ ] T002 Create saturation processor test file at tests/unit/processors/saturation_processor_test.cpp
- [ ] T003 Add saturation_processor_test.cpp to tests/CMakeLists.txt in dsp_tests target

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core class structure that MUST be complete before ANY user story implementation

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 Implement SaturationType enum (Tape, Tube, Transistor, Digital, Diode) in src/dsp/processors/saturation_processor.h
- [ ] T005 Implement SaturationProcessor class skeleton with member variables in src/dsp/processors/saturation_processor.h
- [ ] T006 Implement prepare() method with buffer allocation and smoother initialization in src/dsp/processors/saturation_processor.h
- [ ] T007 Implement reset() method to clear filter and smoother states in src/dsp/processors/saturation_processor.h
- [ ] T008 Implement parameter getters (getType, getInputGain, getOutputGain, getMix, getLatency) in src/dsp/processors/saturation_processor.h
  - Note: getLatency() returns 0 until Oversampler integrated in US5 (T024)

**Checkpoint**: Foundation ready - class compiles, prepare/reset work, user story implementation can begin

---

## Phase 3: User Story 1 - Apply Basic Saturation (Priority: P1) 🎯 MVP

**Goal**: Process audio through Tape saturation algorithm with measurable harmonic content

**Independent Test**: Feed 1kHz sine through Tape saturation with +12dB drive, verify 3rd harmonic > -40dB

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T009 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T010 [US1] Write test: Tape saturation produces odd harmonics (SC-001) in tests/unit/processors/saturation_processor_test.cpp
- [ ] T011 [US1] Write test: Processing silence outputs silence in tests/unit/processors/saturation_processor_test.cpp
- [ ] T012 [US1] Write test: Low-level audio is nearly linear (< 1% THD) in tests/unit/processors/saturation_processor_test.cpp

### 3.3 Implementation for User Story 1

- [ ] T013 [US1] Implement saturateTape() private method using std::tanh in src/dsp/processors/saturation_processor.h
- [ ] T014 [US1] Implement applySaturation() dispatcher method in src/dsp/processors/saturation_processor.h
- [ ] T015 [US1] Implement processSample() for per-sample processing in src/dsp/processors/saturation_processor.h
- [ ] T016 [US1] Implement process() for block processing in src/dsp/processors/saturation_processor.h
- [ ] T017 [US1] Verify all US1 tests pass

### 3.4 Commit (MANDATORY)

- [ ] T018 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic Tape saturation working - can process audio and produce harmonics

---

## Phase 4: User Story 5 - Prevent Aliasing with Oversampling (Priority: P1)

**Goal**: Integrate 2x oversampling to prevent aliasing from nonlinear processing

**Independent Test**: Process 10kHz sine at 44.1kHz with heavy drive, verify no aliased frequencies below fundamental

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T019 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T020 [US5] Write test: Alias rejection > 48dB for 10kHz sine with heavy saturation (SC-003) in tests/unit/processors/saturation_processor_test.cpp
- [ ] T021 [US5] Write test: getLatency() returns correct oversampler latency in tests/unit/processors/saturation_processor_test.cpp

### 4.3 Implementation for User Story 5

- [ ] T022 [US5] Integrate Oversampler<2,1> member into prepare() initialization in src/dsp/processors/saturation_processor.h
- [ ] T023 [US5] Update process() to upsample -> saturate -> downsample in src/dsp/processors/saturation_processor.h
- [ ] T024 [US5] Implement getLatency() returning oversampler latency in src/dsp/processors/saturation_processor.h
- [ ] T025 [US5] Update reset() to reset oversampler state in src/dsp/processors/saturation_processor.h
- [ ] T026 [US5] Verify all US5 tests pass

### 4.4 Commit (MANDATORY)

- [ ] T027 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Oversampling integrated - saturation is alias-free

---

## Phase 5: User Story 2 - Select Saturation Type (Priority: P1)

**Goal**: Implement all 5 saturation algorithms with distinct harmonic characteristics

**Independent Test**: Process same sine through each type, verify distinct harmonic profiles via FFT

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T028 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T029 [P] [US2] Write test: Tube saturation produces 2nd harmonic > -50dB (SC-002) in tests/unit/processors/saturation_processor_test.cpp
- [ ] T030 [P] [US2] Write test: Transistor shows hard-knee clipping behavior in tests/unit/processors/saturation_processor_test.cpp
- [ ] T031 [P] [US2] Write test: Digital hard-clips at threshold in tests/unit/processors/saturation_processor_test.cpp
- [ ] T032 [P] [US2] Write test: Diode shows soft asymmetric compression in tests/unit/processors/saturation_processor_test.cpp
- [ ] T033 [US2] Write test: setType() changes saturation algorithm in tests/unit/processors/saturation_processor_test.cpp

### 5.3 Implementation for User Story 2

- [ ] T034 [P] [US2] Implement saturateTube() asymmetric polynomial in src/dsp/processors/saturation_processor.h
- [ ] T035 [P] [US2] Implement saturateTransistor() hard-knee soft clip in src/dsp/processors/saturation_processor.h
- [ ] T036 [P] [US2] Implement saturateDigital() using std::clamp in src/dsp/processors/saturation_processor.h
- [ ] T037 [P] [US2] Implement saturateDiode() soft asymmetric in src/dsp/processors/saturation_processor.h
- [ ] T038 [US2] Implement setType() setter in src/dsp/processors/saturation_processor.h
- [ ] T039 [US2] Update applySaturation() to dispatch to correct algorithm in src/dsp/processors/saturation_processor.h
- [ ] T040 [US2] Verify all US2 tests pass

### 5.4 Commit (MANDATORY)

- [ ] T041 [US2] **Commit completed User Story 2 work**

**Checkpoint**: All 5 saturation types working with distinct characteristics

---

## Phase 6: User Story 6 - Automatic DC Blocking (Priority: P2)

**Goal**: Apply DC blocking filter after saturation to remove offset from asymmetric curves

**Independent Test**: Process sine through Tube saturation, verify mean of output < 0.001

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T042 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T043 [US6] Write test: Tube saturation has DC offset < 0.001 after blocking (SC-004) in tests/unit/processors/saturation_processor_test.cpp
- [ ] T044 [US6] Write test: Tape saturation also passes through DC blocker (consistent behavior) in tests/unit/processors/saturation_processor_test.cpp
- [ ] T045 [US6] Write test: DC blocker attenuates sub-20Hz content in tests/unit/processors/saturation_processor_test.cpp

### 6.3 Implementation for User Story 6

- [ ] T046 [US6] Add Biquad dcBlocker_ member to class in src/dsp/processors/saturation_processor.h
- [ ] T047 [US6] Initialize dcBlocker_ as Highpass @ 10Hz in prepare() in src/dsp/processors/saturation_processor.h
- [ ] T048 [US6] Add dcBlocker_ reset to reset() method in src/dsp/processors/saturation_processor.h
- [ ] T049 [US6] Add dcBlocker_ processing after saturation in process() in src/dsp/processors/saturation_processor.h
- [ ] T050 [US6] Verify all US6 tests pass

### 6.4 Commit (MANDATORY)

- [ ] T051 [US6] **Commit completed User Story 6 work**

**Checkpoint**: DC blocking integrated - asymmetric saturation no longer causes DC offset

---

## Phase 7: User Story 3 - Control Input/Output Gain (Priority: P2)

**Goal**: Implement input/output gain staging with parameter smoothing

**Independent Test**: Verify input gain increases saturation intensity, output gain scales final level

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T052 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T053 [US3] Write test: Input gain +12dB amplifies signal before saturation in tests/unit/processors/saturation_processor_test.cpp
- [ ] T054 [US3] Write test: Output gain -6dB attenuates post-saturation signal in tests/unit/processors/saturation_processor_test.cpp
- [ ] T055 [US3] Write test: Gain changes are smoothed (no clicks) (SC-005 partial) in tests/unit/processors/saturation_processor_test.cpp
- [ ] T056 [US3] Write test: setInputGain/setOutputGain clamp to [-24, +24] dB range in tests/unit/processors/saturation_processor_test.cpp

### 7.3 Implementation for User Story 3

- [ ] T057 [US3] Add OnePoleSmoother members for input/output gain in src/dsp/processors/saturation_processor.h
- [ ] T058 [US3] Initialize gain smoothers in prepare() with 5ms time constant in src/dsp/processors/saturation_processor.h
- [ ] T059 [US3] Implement setInputGain() with dB to linear conversion in src/dsp/processors/saturation_processor.h
- [ ] T060 [US3] Implement setOutputGain() with dB to linear conversion in src/dsp/processors/saturation_processor.h
- [ ] T061 [US3] Add gain smoothers reset to reset() method in src/dsp/processors/saturation_processor.h
- [ ] T062 [US3] Apply smoothed input gain before oversampling in process() in src/dsp/processors/saturation_processor.h
- [ ] T063 [US3] Apply smoothed output gain after DC blocking in process() in src/dsp/processors/saturation_processor.h
- [ ] T064 [US3] Verify all US3 tests pass

### 7.4 Commit (MANDATORY)

- [ ] T065 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Gain staging working with smooth parameter changes

---

## Phase 8: User Story 4 - Blend Dry/Wet Mix (Priority: P2)

**Goal**: Implement dry/wet mix for parallel saturation effects

**Independent Test**: Set mix to 50%, verify output is equal parts dry and wet

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T066 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 8.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T067 [US4] Write test: Mix 0% outputs dry signal only (bypass) in tests/unit/processors/saturation_processor_test.cpp
- [ ] T068 [US4] Write test: Mix 100% outputs wet signal only in tests/unit/processors/saturation_processor_test.cpp
- [ ] T069 [US4] Write test: Mix 50% blends dry and wet within 0.5dB (SC-008) in tests/unit/processors/saturation_processor_test.cpp
- [ ] T070 [US4] Write test: Mix changes are smoothed (no clicks) in tests/unit/processors/saturation_processor_test.cpp
- [ ] T071 [US4] Write test: Mix 0% bypasses saturation processing (efficiency) in tests/unit/processors/saturation_processor_test.cpp

### 8.3 Implementation for User Story 4

- [ ] T072 [US4] Add OnePoleSmoother member for mix parameter in src/dsp/processors/saturation_processor.h
- [ ] T073 [US4] Initialize mix smoother in prepare() with 5ms time constant in src/dsp/processors/saturation_processor.h
- [ ] T074 [US4] Implement setMix() with [0.0, 1.0] clamping in src/dsp/processors/saturation_processor.h
- [ ] T075 [US4] Add mix smoother reset to reset() method in src/dsp/processors/saturation_processor.h
- [ ] T076 [US4] Store dry signal before processing in process() in src/dsp/processors/saturation_processor.h
- [ ] T077 [US4] Implement dry/wet blending: output = dry*(1-mix) + wet*mix in process() in src/dsp/processors/saturation_processor.h
- [ ] T078 [US4] Add early exit optimization when mix is 0.0 (bypass) in process() in src/dsp/processors/saturation_processor.h
- [ ] T079 [US4] Verify all US4 tests pass

### 8.4 Commit (MANDATORY)

- [ ] T080 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Mix control working - parallel saturation possible

---

## Phase 9: User Story 7 - Real-Time Safety (Priority: P1)

**Goal**: Verify all methods are noexcept and allocation-free in process path

**Independent Test**: Code inspection verifies noexcept and no allocations in process()

### 9.1 Pre-Implementation (MANDATORY)

- [ ] T081 [US7] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 9.2 Tests for User Story 7 (Write FIRST - Must FAIL)

- [ ] T082 [US7] Write test: All public methods are noexcept (SC-006) via static_assert in tests/unit/processors/saturation_processor_test.cpp
- [ ] T083 [US7] Write test: NaN input handling outputs 0.0f safely in tests/unit/processors/saturation_processor_test.cpp
- [ ] T084 [US7] Write test: Infinity input handling clips to safe range in tests/unit/processors/saturation_processor_test.cpp
- [ ] T084a [US7] Write test: Denormal input does not cause CPU spike in tests/unit/processors/saturation_processor_test.cpp
- [ ] T084b [US7] Write test: Maximum drive (+24dB) produces heavy saturation without overflow in tests/unit/processors/saturation_processor_test.cpp

### 9.3 Implementation for User Story 7

- [ ] T085 [US7] Add noexcept specifier to all public methods in src/dsp/processors/saturation_processor.h
- [ ] T086 [US7] Add NaN input check at start of processSample() returning 0.0f in src/dsp/processors/saturation_processor.h
- [ ] T087 [US7] Add infinity clamping at start of processSample() in src/dsp/processors/saturation_processor.h
- [ ] T088 [US7] Verify all US7 tests pass

### 9.4 Cross-Platform Verification (MANDATORY)

- [ ] T089 [US7] **Verify IEEE 754 compliance**: Add saturation_processor_test.cpp to `-fno-fast-math` list in tests/CMakeLists.txt (for NaN tests)

### 9.5 Commit (MANDATORY)

- [ ] T090 [US7] **Commit completed User Story 7 work**

**Checkpoint**: All methods noexcept, edge cases handled safely

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Final integration, success criteria verification, and documentation

- [ ] T091 [P] Run all success criteria tests (SC-001 through SC-008) and document results
- [ ] T092 [P] Verify process() has no memory allocations (SC-007) via code inspection
- [ ] T093 Run quickstart.md examples to validate API usage
- [ ] T094 [P] Add Doxygen documentation to all public methods in src/dsp/processors/saturation_processor.h
- [ ] T094a [P] (Optional) Benchmark CPU usage to verify NFR-003 (< 0.5% at 44.1kHz mono)

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 11.1 Architecture Documentation Update

- [ ] T095 **Update ARCHITECTURE.md** with SaturationProcessor component:
  - Add entry to Layer 2 (DSP Processors) section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage example
  - Verify no duplicate functionality was introduced

### 11.2 Final Commit

- [ ] T096 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects SaturationProcessor functionality

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T097 **Review ALL FR-xxx requirements** (FR-001 to FR-028) from spec.md against implementation
- [ ] T098 **Review ALL SC-xxx success criteria** (SC-001 to SC-008) and verify measurable targets are achieved
- [ ] T099 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [ ] T100 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T101 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T102 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [ ] T103 **Commit all spec work** to feature branch
- [ ] T104 **Verify all tests pass** with full test suite

### 13.2 Completion Claim

- [ ] T105 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
    │
    ▼
Phase 2 (Foundational) ─── BLOCKS ALL USER STORIES
    │
    ├──► Phase 3 (US1: Basic Saturation) 🎯 MVP
    │         │
    │         ▼
    │    Phase 4 (US5: Oversampling)
    │         │
    │         ▼
    │    Phase 5 (US2: All Types)
    │         │
    │         ▼
    │    Phase 6 (US6: DC Blocking)
    │
    ├──► Phase 7 (US3: Gain Controls) ─────────────┐
    │                                              │
    ├──► Phase 8 (US4: Mix Control) ───────────────┤
    │                                              │
    └──► Phase 9 (US7: Real-Time Safety) ──────────┤
                                                   │
                                                   ▼
                                            Phase 10 (Polish)
                                                   │
                                                   ▼
                                            Phase 11 (Architecture)
                                                   │
                                                   ▼
                                            Phase 12 (Verification)
                                                   │
                                                   ▼
                                            Phase 13 (Completion)
```

### User Story Dependencies

| Story | Can Start After | Integrates With |
|-------|-----------------|-----------------|
| US1 | Phase 2 (Foundational) | None (MVP) |
| US5 | US1 | US1 processing |
| US2 | US1 | US1 algorithm dispatcher |
| US6 | US2 | Needed for Tube/Diode |
| US3 | Phase 2 | US1 processing |
| US4 | Phase 2 | US1 processing |
| US7 | All implementation | Cross-cutting |

### Parallel Opportunities

Within Phase 5 (US2):
```bash
# Saturation algorithms can be implemented in parallel:
T034: saturateTube()
T035: saturateTransistor()
T036: saturateDigital()
T037: saturateDiode()
```

Within Phase 5 (US2) Tests:
```bash
# Tests for each algorithm can be written in parallel:
T029: Tube test
T030: Transistor test
T031: Digital test
T032: Diode test
```

US3, US4 can run in parallel after Phase 2 (both depend only on foundational work).

---

## Implementation Strategy

### MVP First (Phase 1-4)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: US1 (Basic Tape Saturation)
4. Complete Phase 4: US5 (Oversampling)
5. **STOP and VALIDATE**: Test basic saturation with oversampling
6. This delivers a working saturation processor with Tape type only

### Full Feature Set

1. Continue with Phase 5: US2 (All 5 saturation types)
2. Add Phase 6: US6 (DC blocking - essential for Tube/Diode)
3. Add Phase 7: US3 (Gain controls)
4. Add Phase 8: US4 (Mix control)
5. Verify Phase 9: US7 (Real-time safety)
6. Polish and document

### Incremental Delivery

| Checkpoint | Functionality |
|------------|---------------|
| After Phase 4 | Basic Tape saturation with oversampling |
| After Phase 5 | All 5 saturation types |
| After Phase 6 | DC-blocked asymmetric saturation |
| After Phase 7 | Full gain staging |
| After Phase 8 | Parallel saturation (dry/wet) |
| After Phase 9 | Production-ready (real-time safe) |

---

## Task Count Summary

| Phase | Story | Task Count |
|-------|-------|------------|
| Phase 1 | Setup | 3 |
| Phase 2 | Foundational | 5 |
| Phase 3 | US1 | 10 |
| Phase 4 | US5 | 9 |
| Phase 5 | US2 | 14 |
| Phase 6 | US6 | 10 |
| Phase 7 | US3 | 14 |
| Phase 8 | US4 | 15 |
| Phase 9 | US7 | 12 |
| Phase 10 | Polish | 5 |
| Phase 11 | Architecture | 2 |
| Phase 12 | Verification | 6 |
| Phase 13 | Completion | 3 |
| **Total** | | **108** |

---

## Notes

- [P] tasks = different files, no dependencies - can run in parallel
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
