# Tasks: Pitch Shift Processor

**Input**: Design documents from `/specs/016-pitch-shifter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

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

## Phase 1: Setup ✅

**Purpose**: Project structure verification and file scaffolding

- [X] T001 Verify Layer 1 dependencies exist (DelayLine, STFT, FFT, SpectralBuffer, OnePoleSmoother, WindowFunctions)
- [X] T002 Create test file scaffold in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T003 Create header file scaffold in src/dsp/processors/pitch_shift_processor.h
- [X] T004 [P] Add pitch_shift_processor_test.cpp to tests/CMakeLists.txt

---

## Phase 2: Foundational (Blocking Prerequisites) ✅

**Purpose**: Utility functions and core constants needed by all modes

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T005 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [X] T006 Write tests for pitchRatioFromSemitones() utility in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T007 Implement pitchRatioFromSemitones() in src/dsp/processors/pitch_shift_processor.h
- [X] T008 Write tests for semitonesFromPitchRatio() utility in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T009 Implement semitonesFromPitchRatio() in src/dsp/processors/pitch_shift_processor.h
- [X] T010 Define PitchMode enum (Simple, Granular, PhaseVocoder) in src/dsp/processors/pitch_shift_processor.h
- [X] T011 Verify all foundational tests pass
- [X] T012 **Commit foundational utilities**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Pitch Shifting (Priority: P1) 🎯 MVP ✅

**Goal**: Shift pitch by semitones in a single mode (Simple), maintaining duration

**Independent Test**: Feed 440Hz sine → shift +12 semitones → verify 880Hz output

### 3.1 Pre-Implementation (MANDATORY)

- [X] T013 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T014 [US1] Write test: 440Hz sine + 12 semitones = 880Hz output in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T015 [US1] Write test: 440Hz sine - 12 semitones = 220Hz output in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T016 [US1] Write test: 0 semitones = unity pass-through (input equals output) in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T017 [US1] Write test: prepare()/reset()/isPrepared() lifecycle in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T018 [US1] Write test: in-place processing (input buffer == output buffer) in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T019 [US1] Write test: output sample count equals input sample count (FR-004 duration preservation) in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T020 [US1] Write test: RMS output approximately equals RMS input at 0 semitones (FR-005 unity gain) in tests/unit/processors/pitch_shift_processor_test.cpp

### 3.3 Implementation for User Story 1

- [X] T021 [US1] Implement PitchShiftProcessor class skeleton (prepare, reset, process stubs) in src/dsp/processors/pitch_shift_processor.h
- [X] T022 [US1] Implement SimplePitchShifter internal class with dual-pointer crossfade in src/dsp/processors/pitch_shift_processor.h
- [X] T023 [US1] Implement half-sine crossfade window calculation in SimplePitchShifter
- [X] T024 [US1] Wire SimplePitchShifter to PitchShiftProcessor::process() for Simple mode
- [X] T025 [US1] Implement setSemitones()/getSemitones() parameter methods
- [X] T026 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T027 [US1] **Verify IEEE 754 compliance**: Add pitch_shift_processor_test.cpp to `-fno-fast-math` list in tests/CMakeLists.txt if NaN detection used

### 3.5 Commit (MANDATORY)

- [X] T028 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic pitch shifting works in Simple mode

---

## Phase 4: User Story 2 - Quality Mode Selection (Priority: P1) ✅

**Goal**: Provide three quality modes with different latency/quality trade-offs

**Independent Test**: Verify latency matches spec for each mode (0, <2048, <8192 samples)

### 4.1 Pre-Implementation (MANDATORY)

- [X] T029 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [X] T030 [US2] Write test: Simple mode latency == 0 samples in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T031 [US2] Write test: Granular mode latency < 2048 samples in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T032 [US2] Write test: PhaseVocoder mode latency < 8192 samples in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T033 [US2] Write test: setMode()/getMode() parameter methods in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T034 [US2] Write test: mode switching is click-free (no discontinuities) in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T035 [P] [US2] Write test: Granular mode produces shifted pitch in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T036 [P] [US2] Write test: PhaseVocoder mode produces shifted pitch in tests/unit/processors/pitch_shift_processor_test.cpp

### 4.3 Implementation for User Story 2

- [X] T037 [US2] Implement GranularPitchShifter internal class with OLA grains in src/dsp/processors/pitch_shift_processor.h
- [X] T038 [US2] Implement grain buffer and 4 overlapping GrainState instances in GranularPitchShifter
- [X] T039 [US2] Implement Hann window calculation for grains (use WindowFunctions from Layer 0)
- [X] T040 [US2] Implement grain emission and OLA reconstruction in GranularPitchShifter
- [X] T041 [US2] Implement PhaseVocoderPitchShifter internal class skeleton in src/dsp/processors/pitch_shift_processor.h
- [X] T042 [US2] Integrate STFT analysis in PhaseVocoderPitchShifter (use STFT from Layer 1)
- [X] T043 [US2] Implement phase accumulator and instantaneous frequency estimation
- [X] T044 [US2] Implement scaled phase locking (Laroche & Dolson approach)
- [X] T045 [US2] Implement frequency bin scaling for pitch shift
- [X] T046 [US2] Implement STFT synthesis in PhaseVocoderPitchShifter
- [X] T047 [US2] Wire mode switching in PitchShiftProcessor::process()
- [X] T048 [US2] Implement getLatencySamples() returning mode-specific latency
- [X] T049 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T050 [US2] **Verify IEEE 754 compliance**: Check for NaN/infinity handling in phase vocoder (pitch_shift_processor_test.cpp added to -fno-fast-math list)

### 4.5 Commit (MANDATORY)

- [X] T051 [US2] **Commit completed User Story 2 work** (659ee35)

**Checkpoint**: All three quality modes functional with correct latency ✅

---

## Phase 5: User Story 3 - Fine Pitch Control with Cents (Priority: P2) ✅

**Goal**: Add cent-level precision (1/100th of a semitone) for fine pitch adjustments

**Independent Test**: Verify 0 semitones + 50 cents produces correct quarter-tone shift

### 5.1 Pre-Implementation (MANDATORY)

- [X] T052 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [X] T053 [US3] Write test: 50 cents produces correct pitch ratio, cents affects audio output in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T054 [US3] Write test: +1 semitone - 50 cents = +0.5 semitones total in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T055 [US3] Write test: setCents()/getCents() parameter methods in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T056 [US3] Write test: cents changes apply smoothly (no glitches) in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T057 [US3] Write test: getPitchRatio() combines semitones and cents correctly in tests/unit/processors/pitch_shift_processor_test.cpp

### 5.3 Implementation for User Story 3

- [X] T058 [US3] Add cents_ member variable and setCents()/getCents() methods in src/dsp/processors/pitch_shift_processor.h
- [X] T059 [US3] Update getPitchRatio() to combine semitones + cents/100
- [X] T060 [US3] Add OnePoleSmoother for cents parameter smoothing
- [X] T061 [US3] Verify all US3 tests pass

### 5.4 Commit (MANDATORY)

- [X] T062 [US3] **Commit completed User Story 3 work** (91f04a8)

**Checkpoint**: Fine pitch control with cents works across all modes ✅

---

## Phase 6: User Story 4 - Formant Preservation for Vocals (Priority: P2) ⚠️ PARTIAL

**Goal**: Preserve vocal formants when pitch shifting to avoid "chipmunk" effect

**Independent Test**: Verify formant peaks remain within 10% of original frequencies when shifting

### 6.1 Pre-Implementation (MANDATORY)

- [X] T063 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [X] T064 [US4] Write test: formant preservation enabled keeps formants within 10% in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T065 [US4] Write test: formant preservation disabled shifts formants with pitch in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T066 [US4] Write test: setFormantPreserve()/getFormantPreserve() methods in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T067 [US4] Write test: formant toggle transition is smooth in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T068 [US4] Write test: formant preservation ignored in Simple mode in tests/unit/processors/pitch_shift_processor_test.cpp
- [X] T069 [US4] Write test: formant preservation gracefully degrades at extreme shifts (>1 octave) in tests/unit/processors/pitch_shift_processor_test.cpp

### 6.3 Implementation for User Story 4

**Note**: Parameter interface implemented. Full cepstral formant preservation algorithm deferred to future enhancement.
The setFormantPreserve/getFormantPreserve methods work, but the actual spectral envelope
preservation requires complex cepstral analysis that is out of scope for initial implementation.

- [ ] T070 [US4] Implement FormantPreserver internal class in src/dsp/processors/pitch_shift_processor.h ⏸️ DEFERRED
- [ ] T071 [US4] Implement cepstrum calculation (log magnitude → IFFT) in FormantPreserver ⏸️ DEFERRED
- [ ] T072 [US4] Implement quefrency liftering (low-pass in cepstral domain) in FormantPreserver ⏸️ DEFERRED
- [ ] T073 [US4] Implement spectral envelope estimation (FFT → exp) in FormantPreserver ⏸️ DEFERRED
- [ ] T074 [US4] Implement envelope removal and reapplication in FormantPreserver ⏸️ DEFERRED
- [ ] T075 [US4] Integrate FormantPreserver into GranularPitchShifter ⏸️ DEFERRED
- [ ] T076 [US4] Integrate FormantPreserver into PhaseVocoderPitchShifter ⏸️ DEFERRED
- [X] T077 [US4] Add setFormantPreserve()/getFormantPreserve() to PitchShiftProcessor
- [X] T078 [US4] Verify all US4 tests pass (parameter interface tests)

### 6.4 Commit (MANDATORY)

- [X] T079 [US4] **Commit completed User Story 4 work** (2b9a290) - Tests + parameter interface; algorithm deferred

**Checkpoint**: Formant preservation parameter interface complete. Algorithm implementation deferred.

---

## Phase 7: User Story 5 - Feedback Path Integration (Priority: P2)

**Goal**: Enable stable operation in feedback loops for Shimmer delay effects

**Independent Test**: Verify stability after 1000 iterations at 80% feedback

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T080 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T081 [US5] Write test: 80% feedback loop decays naturally without instability in tests/unit/processors/pitch_shift_processor_test.cpp
- [ ] T082 [US5] Write test: multiple iterations maintain pitch accuracy (no cumulative drift) in tests/unit/processors/pitch_shift_processor_test.cpp
- [ ] T083 [US5] Write test: no DC offset after extended feedback processing in tests/unit/processors/pitch_shift_processor_test.cpp
- [ ] T084 [US5] Write test: stable after 1000 iterations at 80% feedback in tests/unit/processors/pitch_shift_processor_test.cpp

### 7.3 Implementation for User Story 5

- [ ] T085 [US5] Add DC blocking filter to pitch shifter output path in src/dsp/processors/pitch_shift_processor.h
- [ ] T086 [US5] Implement energy preservation checks in all three modes
- [ ] T087 [US5] Add soft limiting for feedback protection in extreme cases
- [ ] T088 [US5] Verify deterministic behavior (same input = same output, no random elements)
- [ ] T089 [US5] Verify all US5 tests pass

### 7.4 Commit (MANDATORY)

- [ ] T090 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Pitch shifter is stable for Shimmer/feedback use cases

---

## Phase 8: User Story 6 - Real-Time Parameter Automation (Priority: P3)

**Goal**: Enable smooth parameter automation for live performance and creative effects

**Independent Test**: Sweep pitch from -24 to +24 semitones and verify no clicks

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T091 [US6] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T092 [US6] Write test: sweep from -24 to +24 semitones is smooth in tests/unit/processors/pitch_shift_processor_test.cpp
- [ ] T093 [US6] Write test: rapid parameter changes (>100/sec) cause no clicks in tests/unit/processors/pitch_shift_processor_test.cpp
- [ ] T094 [US6] Write test: parameter reaches target within 50ms in tests/unit/processors/pitch_shift_processor_test.cpp

### 8.3 Implementation for User Story 6

- [ ] T095 [US6] Add OnePoleSmoother for semitones parameter in src/dsp/processors/pitch_shift_processor.h
- [ ] T096 [US6] Configure smoothing time (~20ms) for glitch-free transitions
- [ ] T097 [US6] Ensure smoothed pitch ratio is used in all three modes
- [ ] T098 [US6] Verify all US6 tests pass

### 8.4 Commit (MANDATORY)

- [ ] T099 [US6] **Commit completed User Story 6 work**

**Checkpoint**: All parameter automation is smooth and click-free

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T100 [P] Add edge case tests: extreme values ±24 semitones in tests/unit/processors/pitch_shift_processor_test.cpp
- [ ] T101 [P] Add edge case tests: silence and very quiet signals in tests/unit/processors/pitch_shift_processor_test.cpp
- [ ] T102 [P] Add edge case tests: NaN/infinity input handling in tests/unit/processors/pitch_shift_processor_test.cpp
- [ ] T103 [P] Add edge case tests: sample rate change handling in tests/unit/processors/pitch_shift_processor_test.cpp
- [ ] T104 Verify pitch accuracy meets SC-001 (±10 cents Simple, ±5 cents Granular/PhaseVocoder)
- [ ] T105 Verify CPU usage meets SC-005 (<2% Simple, <5% Granular, <10% PhaseVocoder)
- [ ] T106 Run quickstart.md validation - verify all examples work
- [ ] T107 Code cleanup and inline documentation

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T108 **Update ARCHITECTURE.md** with PitchShiftProcessor:
  - Add entry to Layer 2: DSP Processors section
  - Include: purpose (pitch shifting without time stretch), public API summary, file location
  - Document three quality modes and their trade-offs
  - Add usage examples for common scenarios (vocal tuning, shimmer, monitoring)
  - Verify no duplicate functionality was introduced

### 10.2 Commit

- [ ] T109 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects PitchShiftProcessor functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T110 **Review ALL FR-xxx requirements** (FR-001 to FR-031) from spec.md against implementation
- [ ] T111 **Review ALL SC-xxx success criteria** (SC-001 to SC-008) and verify measurable targets are achieved
- [ ] T112 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T113 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T114 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

- [ ] T115 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [ ] T116 **Commit all spec work** to feature branch
- [ ] T117 **Verify all tests pass**

### 12.2 Completion Claim

- [ ] T118 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - US1 (P1): Core pitch shifting with Simple mode
  - US2 (P1): Add Granular and PhaseVocoder modes (extends US1)
  - US3 (P2): Cents support (builds on any mode)
  - US4 (P2): Formant preservation (requires US2 for Granular/PhaseVocoder)
  - US5 (P2): Feedback stability (builds on all modes)
  - US6 (P3): Parameter smoothing (enhances all)
- **Polish (Phase 9)**: Depends on all user stories being complete
- **Documentation (Phase 10)**: Depends on Polish
- **Verification (Phase 11)**: Depends on Documentation
- **Final (Phase 12)**: Depends on Verification

### User Story Dependencies

```
Foundational
     │
     ▼
   US1 (Basic Pitch Shifting - Simple mode)
     │
     ▼
   US2 (Quality Mode Selection - adds Granular, PhaseVocoder)
     │
     ├────────────────┬────────────────┐
     ▼                ▼                ▼
   US3 (Cents)     US4 (Formant)    US5 (Feedback)
     │                │                │
     └────────────────┴────────────────┘
                      │
                      ▼
                   US6 (Automation)
```

### Within Each User Story

1. **TESTING-GUIDE check**: FIRST task
2. **Tests FIRST**: Must FAIL before implementation
3. **Implementation**: Make tests pass
4. **Verify**: All tests pass
5. **Commit**: LAST task

### Parallel Opportunities

**Within Phase 2 (Foundational):**
- T006 and T008 can run in parallel (different utility functions)

**Within Phase 4 (US2):**
- T033 and T034 can run in parallel (different mode tests)

**Within Phase 9 (Polish):**
- T097, T098, T099, T100 can all run in parallel (different edge case tests)

---

## Parallel Example: User Story 2

```bash
# Launch tests for different modes together:
Task: "T033 [P] [US2] Write test: Granular mode produces shifted pitch"
Task: "T034 [P] [US2] Write test: PhaseVocoder mode produces shifted pitch"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (Basic pitch shifting with Simple mode)
4. **STOP and VALIDATE**: Test +/-12 semitone shifts work correctly
5. Can be used immediately for zero-latency monitoring

### Incremental Delivery

1. **MVP (US1)**: Basic pitch shifting works with Simple mode → Deploy for monitoring use
2. **+US2**: Add Granular and PhaseVocoder for quality options → Deploy for mixing use
3. **+US3**: Add cents for fine tuning → Deploy for tuning correction
4. **+US4**: Add formant preservation → Deploy for vocal processing
5. **+US5**: Add feedback stability → Deploy for Shimmer effects
6. **+US6**: Add parameter automation → Deploy for live performance

### Recommended Order

Given US4 (Formant) depends on US2 (Granular/PhaseVocoder modes), and US5 (Feedback) benefits from all modes:

1. US1 → US2 → US3 → US4 → US5 → US6

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- All three internal pitch shifter classes are in the same header file (pattern from DiffusionNetwork)
- FormantPreserver is only used by Granular and PhaseVocoder modes
- Simple mode has unique characteristics: zero latency, no formant preservation possible
- PhaseVocoder implementation is the most complex (phase locking algorithm)
- Layer 1 dependencies: DelayLine, STFT, FFT, SpectralBuffer, OnePoleSmoother, WindowFunctions
