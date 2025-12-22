# Tasks: LFO DSP Primitive

**Input**: Design documents from `/specs/003-lfo/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

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

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Project structure and test file initialization

- [ ] T001 Create primitives directory if needed at src/dsp/primitives/
- [ ] T002 Create test file stub at tests/unit/primitives/lfo_test.cpp
- [ ] T003 Update tests/CMakeLists.txt to include lfo_test.cpp in dsp_tests target

---

## Phase 2: Foundational (Enumerations & Class Structure)

**Purpose**: Core infrastructure that MUST be complete before user story implementation

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T005 [P] Write tests for Waveform enum in tests/unit/primitives/lfo_test.cpp
- [ ] T006 [P] Write tests for NoteValue enum in tests/unit/primitives/lfo_test.cpp
- [ ] T007 [P] Write tests for NoteModifier enum in tests/unit/primitives/lfo_test.cpp
- [ ] T008 Create header file with enums and class declaration in src/dsp/primitives/lfo.h
- [ ] T009 Implement private state variables per data-model.md in src/dsp/primitives/lfo.h
- [ ] T010 Implement prepare() method (wavetable generation) in src/dsp/primitives/lfo.h
- [ ] T011 Implement reset() method in src/dsp/primitives/lfo.h
- [ ] T012 Verify enum tests pass
- [ ] T013 **Commit foundational implementation**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Sine LFO (Priority: P1) 🎯 MVP

**Goal**: Core sine wave oscillation with frequency control and real-time safety

**Independent Test**: Generate one complete sine cycle and verify output matches reference sine wave within 0.001% error

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T014 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T015 [P] [US1] Test sine wavetable generation in tests/unit/primitives/lfo_test.cpp
- [ ] T016 [P] [US1] Test process() returns values in [-1, +1] range in tests/unit/primitives/lfo_test.cpp
- [ ] T017 [P] [US1] Test 1 Hz LFO completes one cycle in 44100 samples in tests/unit/primitives/lfo_test.cpp
- [ ] T018 [P] [US1] Test sine starts at 0.0 (zero crossing) at phase 0 in tests/unit/primitives/lfo_test.cpp
- [ ] T019 [P] [US1] Test setFrequency() clamps to [0.01, 20.0] Hz in tests/unit/primitives/lfo_test.cpp
- [ ] T020 [P] [US1] Test processBlock() generates correct samples in tests/unit/primitives/lfo_test.cpp

### 3.3 Implementation for User Story 1

- [ ] T021 [US1] Implement readWavetable() with linear interpolation in src/dsp/primitives/lfo.h
- [ ] T022 [US1] Implement process() method for sine waveform in src/dsp/primitives/lfo.h
- [ ] T023 [US1] Implement processBlock() method in src/dsp/primitives/lfo.h
- [ ] T024 [US1] Implement setFrequency() with clamping in src/dsp/primitives/lfo.h
- [ ] T025 [US1] Implement query methods (waveform(), frequency(), sampleRate()) in src/dsp/primitives/lfo.h
- [ ] T026 [US1] Verify all US1 tests pass

### 3.4 Commit (MANDATORY)

- [ ] T027 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic sine LFO functional with real-time safe process()

---

## Phase 4: User Story 2 - Multiple Waveforms (Priority: P2)

**Goal**: Support all 6 waveform types (sine, triangle, sawtooth, square, sample & hold, smoothed random)

**Independent Test**: Generate one cycle of each waveform and verify characteristic shapes

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T028 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T029 [P] [US2] Test triangle wavetable generation (0->1->-1->0 shape) in tests/unit/primitives/lfo_test.cpp
- [ ] T030 [P] [US2] Test sawtooth wavetable generation (-1 to +1 ramp) in tests/unit/primitives/lfo_test.cpp
- [ ] T031 [P] [US2] Test square wavetable generation (+1/-1 alternation) in tests/unit/primitives/lfo_test.cpp
- [ ] T032 [P] [US2] Test sample & hold outputs in [-1, +1] and changes at cycle boundaries in tests/unit/primitives/lfo_test.cpp
- [ ] T033 [P] [US2] Test smoothed random outputs in [-1, +1] with smooth transitions in tests/unit/primitives/lfo_test.cpp
- [ ] T034 [P] [US2] Test setWaveform() changes active waveform in tests/unit/primitives/lfo_test.cpp

### 4.3 Implementation for User Story 2

- [ ] T035 [US2] Generate triangle wavetable in prepare() in src/dsp/primitives/lfo.h
- [ ] T036 [US2] Generate sawtooth wavetable in prepare() in src/dsp/primitives/lfo.h
- [ ] T037 [US2] Generate square wavetable in prepare() in src/dsp/primitives/lfo.h
- [ ] T038 [US2] Implement PRNG for random waveforms (nextRandomValue()) in src/dsp/primitives/lfo.h
- [ ] T039 [US2] Implement sample & hold logic in process() in src/dsp/primitives/lfo.h
- [ ] T040 [US2] Implement smoothed random logic in process() in src/dsp/primitives/lfo.h
- [ ] T041 [US2] Implement setWaveform() in src/dsp/primitives/lfo.h
- [ ] T042 [US2] Verify all US2 tests pass

### 4.4 Commit (MANDATORY)

- [ ] T043 [US2] **Commit completed User Story 2 work**

**Checkpoint**: All 6 waveforms functional

---

## Phase 5: User Story 3 - Tempo Sync (Priority: P2)

**Goal**: Synchronize LFO to host BPM with note value divisions

**Independent Test**: Set 1/4 note at 120 BPM and verify cycle completes every 500ms

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T044 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T045 [P] [US3] Test 1/4 note at 120 BPM = 2 Hz in tests/unit/primitives/lfo_test.cpp
- [ ] T046 [P] [US3] Test dotted 1/8 note at 120 BPM = 1.333 Hz in tests/unit/primitives/lfo_test.cpp
- [ ] T047 [P] [US3] Test triplet 1/4 note at 120 BPM = 3 Hz in tests/unit/primitives/lfo_test.cpp
- [ ] T048 [P] [US3] Test all 6 note values with normal modifier in tests/unit/primitives/lfo_test.cpp
- [ ] T049 [P] [US3] Test setTempoSync() enables/disables tempo mode in tests/unit/primitives/lfo_test.cpp
- [ ] T050 [P] [US3] Test tempo change updates frequency smoothly in tests/unit/primitives/lfo_test.cpp

### 5.3 Implementation for User Story 3

- [ ] T051 [US3] Implement getBeatsForNoteValue() helper function in src/dsp/primitives/lfo.h
- [ ] T052 [US3] Implement calculateTempoSyncFrequency() in src/dsp/primitives/lfo.h
- [ ] T053 [US3] Implement setTempoSync() in src/dsp/primitives/lfo.h
- [ ] T054 [US3] Implement setTempo() with BPM clamping in src/dsp/primitives/lfo.h
- [ ] T055 [US3] Implement setNoteValue() in src/dsp/primitives/lfo.h
- [ ] T056 [US3] Implement tempoSyncEnabled() query in src/dsp/primitives/lfo.h
- [ ] T057 [US3] Verify all US3 tests pass

### 5.4 Commit (MANDATORY)

- [ ] T058 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Tempo sync functional with all note values and modifiers

---

## Phase 6: User Story 4 - Phase Control (Priority: P3)

**Goal**: Adjustable phase offset for stereo effects and multi-voice modulation

**Independent Test**: Set 90° phase offset on sine and verify first sample is 1.0 (peak)

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T059 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T060 [P] [US4] Test 90° offset sine starts at 1.0 (peak) in tests/unit/primitives/lfo_test.cpp
- [ ] T061 [P] [US4] Test 180° offset sine is inverted vs 0° in tests/unit/primitives/lfo_test.cpp
- [ ] T062 [P] [US4] Test phase offset wraps values >= 360° in tests/unit/primitives/lfo_test.cpp
- [ ] T063 [P] [US4] Test phaseOffset() returns current offset in degrees in tests/unit/primitives/lfo_test.cpp

### 6.3 Implementation for User Story 4

- [ ] T064 [US4] Implement setPhaseOffset() with degree-to-normalized conversion in src/dsp/primitives/lfo.h
- [ ] T065 [US4] Integrate phase offset into process() phase calculation in src/dsp/primitives/lfo.h
- [ ] T066 [US4] Implement phaseOffset() query in src/dsp/primitives/lfo.h
- [ ] T067 [US4] Verify all US4 tests pass

### 6.4 Commit (MANDATORY)

- [ ] T068 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Phase offset functional for stereo LFO configurations

---

## Phase 7: User Story 5 - Retrigger (Priority: P3)

**Goal**: Reset phase on demand for note-on synchronization

**Independent Test**: Call retrigger() mid-cycle and verify phase resets to start phase

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T069 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T070 [P] [US5] Test retrigger() resets phase to 0 (default) in tests/unit/primitives/lfo_test.cpp
- [ ] T071 [P] [US5] Test retrigger() respects phase offset in tests/unit/primitives/lfo_test.cpp
- [ ] T072 [P] [US5] Test retrigger disabled (free-running) ignores retrigger() call in tests/unit/primitives/lfo_test.cpp
- [ ] T073 [P] [US5] Test setRetriggerEnabled() toggles retrigger mode in tests/unit/primitives/lfo_test.cpp

### 7.3 Implementation for User Story 5

- [ ] T074 [US5] Implement retrigger() method in src/dsp/primitives/lfo.h
- [ ] T075 [US5] Implement setRetriggerEnabled() in src/dsp/primitives/lfo.h
- [ ] T076 [US5] Implement retriggerEnabled() query in src/dsp/primitives/lfo.h
- [ ] T077 [US5] Verify all US5 tests pass

### 7.4 Commit (MANDATORY)

- [ ] T078 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Retrigger functional for note-on sync

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Edge cases, validation, cross-story integration, and success criteria verification

### 8.1 Edge Case Tests

- [ ] T079 Test edge case: 0 Hz frequency outputs DC at current phase in tests/unit/primitives/lfo_test.cpp
- [ ] T080 Test edge case: 0 BPM in sync mode falls back to minimum frequency in tests/unit/primitives/lfo_test.cpp

### 8.2 Real-Time Safety Tests (US6)

- [ ] T081 [US6] Test noexcept guarantee on all public methods in tests/unit/primitives/lfo_test.cpp
- [ ] T082 [US6] Test output range [-1, +1] for all waveforms (fuzz test) in tests/unit/primitives/lfo_test.cpp

### 8.3 Success Criteria Verification

- [ ] T083 [P] Test waveform transition produces no discontinuities (SC-008) in tests/unit/primitives/lfo_test.cpp
- [ ] T084 [P] Benchmark process() execution time < 50ns/sample in Release build (SC-005) in tests/unit/primitives/lfo_test.cpp

### 8.4 Final Validation

- [ ] T085 Verify all edge case and SC tests pass
- [ ] T086 Run quickstart.md code examples as validation
- [ ] T087 **Commit polish work**

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 9.1 Architecture Documentation Update

- [ ] T088 **Update ARCHITECTURE.md** with LFO component:
  - Add LFO entry to Layer 1 DSP Primitives section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add Component Index entries
  - Update "Last Updated" to 003-lfo

### 9.2 Final Verification

- [ ] T089 **Commit ARCHITECTURE.md updates**
- [ ] T090 Verify all spec work is committed to feature branch
- [ ] T091 Run full test suite and verify all tests pass

**Checkpoint**: Spec implementation complete - ARCHITECTURE.md reflects LFO component

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - US1 (P1): No dependencies - MVP baseline
  - US2 (P2): No dependencies on other stories
  - US3 (P2): No dependencies on other stories
  - US4 (P3): No dependencies on other stories
  - US5 (P3): No dependencies on other stories
- **Polish (Phase 8)**: Depends on all user stories complete
- **Documentation (Phase 9)**: Final phase - depends on all work complete

### User Story Independence

All user stories are **independently testable** after foundational phase:

| Story | Independent Test |
|-------|------------------|
| US1 (Sine LFO) | Generate sine cycle, verify [-1,+1] output |
| US2 (Waveforms) | Generate each waveform, verify shapes |
| US3 (Tempo Sync) | Set 1/4@120BPM, verify 2Hz cycle |
| US4 (Phase) | Set 90° offset, verify sine starts at peak |
| US5 (Retrigger) | Call retrigger mid-cycle, verify phase reset |
| US6 (Real-Time Safety) | Verify noexcept, no allocations in process() |

**Note on US6 (Real-Time Safety)**: US6 from spec.md is a cross-cutting concern integrated throughout the implementation rather than a separate phase. Real-time safety is enforced via:
- T081: noexcept guarantee tests
- T082: Output range fuzz testing (validates no exceptions under stress)
- All process() implementations must be allocation-free (verified by code review and constitution compliance)

### Parallel Opportunities

Within each phase, tasks marked [P] can run in parallel:
- All test writing tasks [P] within a phase
- All wavetable generation tasks [P] in Phase 4

---

## Parallel Example: User Story 2 Tests

```bash
# Launch all US2 tests in parallel (they touch same file but are independent test cases):
Task: "[US2] Test triangle wavetable generation"
Task: "[US2] Test sawtooth wavetable generation"
Task: "[US2] Test square wavetable generation"
Task: "[US2] Test sample & hold outputs"
Task: "[US2] Test smoothed random outputs"
Task: "[US2] Test setWaveform() changes waveform"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (enums, class structure, prepare/reset)
3. Complete Phase 3: User Story 1 (sine LFO with frequency control)
4. **STOP and VALIDATE**: Test sine LFO independently
5. This provides a working LFO for basic chorus/vibrato effects

### Incremental Delivery

1. **MVP (US1)**: Basic sine LFO → Can modulate DelayLine for simple chorus
2. **+US2**: All waveforms → Full creative palette
3. **+US3**: Tempo sync → Musical integration with DAW
4. **+US4**: Phase offset → Stereo width capabilities
5. **+US5**: Retrigger → Note-on sync for synced effects
6. Each increment adds value without breaking previous functionality

---

## Notes

- Total Tasks: 91
- Test Tasks: ~42 (following test-first methodology)
- Implementation Tasks: ~35
- Documentation/Commit Tasks: ~14
- All tasks follow strict checklist format with [TaskID] [P?] [Story] pattern
- File paths: src/dsp/primitives/lfo.h (implementation), tests/unit/primitives/lfo_test.cpp (tests)
- Single header implementation pattern (same as DelayLine)
- US6 (Real-Time Safety) is integrated as cross-cutting concern in Phase 8 (T081-T082)
