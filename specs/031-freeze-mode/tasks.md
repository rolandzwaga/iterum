# Tasks: Freeze Mode

**Input**: Design documents from `/specs/031-freeze-mode/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and file scaffolding

- [x] T001 Create feature header file scaffold in src/dsp/features/freeze_mode.h
- [x] T002 Create test file scaffold in tests/unit/features/freeze_mode_test.cpp
- [x] T003 Add freeze_mode_test.cpp to tests/CMakeLists.txt

---

## Phase 2: Foundational (FreezeFeedbackProcessor - Blocking Prerequisites)

**Purpose**: The IFeedbackProcessor implementation that all user stories depend on

**CRITICAL**: No user story work can begin until FreezeFeedbackProcessor is complete

### 2.1 Pre-Implementation (MANDATORY)

- [x] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.2 Tests for FreezeFeedbackProcessor (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T005 [P] Write tests for FreezeFeedbackProcessor::prepare() in tests/unit/features/freeze_mode_test.cpp
- [x] T006 [P] Write tests for FreezeFeedbackProcessor::process() basic passthrough in tests/unit/features/freeze_mode_test.cpp
- [x] T007 [P] Write tests for FreezeFeedbackProcessor::reset() in tests/unit/features/freeze_mode_test.cpp
- [x] T008 [P] Write tests for FreezeFeedbackProcessor::getLatencySamples() in tests/unit/features/freeze_mode_test.cpp

### 2.3 Implementation

- [x] T009 Implement FreezeFeedbackProcessor class declaration in src/dsp/features/freeze_mode.h (IFeedbackProcessor interface)
- [x] T010 Implement FreezeFeedbackProcessor::prepare() in src/dsp/features/freeze_mode.h
- [x] T011 Implement FreezeFeedbackProcessor::process() basic structure in src/dsp/features/freeze_mode.h
- [x] T012 Implement FreezeFeedbackProcessor::reset() in src/dsp/features/freeze_mode.h
- [x] T013 Implement FreezeFeedbackProcessor::getLatencySamples() in src/dsp/features/freeze_mode.h
- [x] T014 Verify all foundational tests pass
- [ ] T015 **Commit completed FreezeFeedbackProcessor foundation**

**Checkpoint**: FreezeFeedbackProcessor ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Freeze Capture (Priority: P1) - MVP

**Goal**: Engage freeze to sustain delay buffer content infinitely. Input is muted, output loops continuously. Disengage returns to normal operation with natural decay.

**Independent Test**: Process audio into delay, engage freeze, verify output sustains indefinitely with no input bleed-through.

### 3.1 Pre-Implementation (MANDATORY)

- [x] T016 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T017 [P] [US1] Write tests for FreezeMode::prepare()/reset()/snapParameters() lifecycle in tests/unit/features/freeze_mode_test.cpp
- [x] T018 [P] [US1] Write tests for FreezeMode::setFreezeEnabled()/isFreezeEnabled() toggle in tests/unit/features/freeze_mode_test.cpp
- [x] T019 [P] [US1] Write test: freeze captures current delay buffer content (FR-001) in tests/unit/features/freeze_mode_test.cpp
- [x] T020 [P] [US1] Write test: input is muted when freeze engaged (FR-002, SC-004: -96dB) in tests/unit/features/freeze_mode_test.cpp
- [x] T021 [P] [US1] Write test: frozen content sustains at full level (FR-003, SC-002: <0.01dB loss/sec) in tests/unit/features/freeze_mode_test.cpp
- [x] T022 [P] [US1] Write test: freeze transitions are click-free (FR-004, FR-005, FR-007, SC-001) in tests/unit/features/freeze_mode_test.cpp
- [x] T023 [P] [US1] Write test: freeze disengage returns to normal feedback decay (FR-005) in tests/unit/features/freeze_mode_test.cpp
- [x] T023a [P] [US1] Write test: FreezeMode reports freeze state to host for automation (FR-008) in tests/unit/features/freeze_mode_test.cpp
- [x] T023b [P] [US1] Write test: dry/wet mix control works from 0% to 100% (FR-024) in tests/unit/features/freeze_mode_test.cpp
- [x] T023c [P] [US1] Write test: output gain control works from -infinity to +6dB (FR-025) in tests/unit/features/freeze_mode_test.cpp
- [x] T023d [P] [US1] Write test: FreezeMode reports latency to host for PDC (FR-029) in tests/unit/features/freeze_mode_test.cpp

### 3.3 Implementation for User Story 1

- [x] T024 [US1] Implement FreezeMode class declaration with constants in src/dsp/features/freeze_mode.h
- [x] T025 [US1] Implement FreezeMode::prepare() composing FlexibleFeedbackNetwork in src/dsp/features/freeze_mode.h
- [x] T026 [US1] Implement FreezeMode::reset() and snapParameters() in src/dsp/features/freeze_mode.h
- [x] T027 [US1] Implement FreezeMode::setFreezeEnabled()/isFreezeEnabled() delegating to FFN in src/dsp/features/freeze_mode.h
- [x] T028 [US1] Implement FreezeMode::setDelayTimeMs() and tempo sync in src/dsp/features/freeze_mode.h
- [x] T029 [US1] Implement FreezeMode::setFeedbackAmount() in src/dsp/features/freeze_mode.h
- [x] T030 [US1] Implement FreezeMode::setDryWetMix() and setOutputGainDb() in src/dsp/features/freeze_mode.h
- [x] T031 [US1] Implement FreezeMode::process() with dry/wet mixing in src/dsp/features/freeze_mode.h
- [x] T032 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [x] T033 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 3.5 Commit (MANDATORY)

- [x] T034 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic freeze capture fully functional - MVP complete

---

## Phase 4: User Story 2 - Shimmer Freeze (Priority: P2)

**Goal**: Freeze with pitch shifting enabled creates evolving, ethereal textures. Each feedback iteration shifts pitch further.

**Independent Test**: Freeze content with pitch shift at +12 semitones, verify frequency content evolves upward over iterations.

### 4.1 Pre-Implementation (MANDATORY)

- [x] T035 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T036 [P] [US2] Write tests for FreezeFeedbackProcessor pitch shift integration in tests/unit/features/freeze_mode_test.cpp
- [x] T037 [P] [US2] Write test: +12 semitones shifts up one octave per iteration (FR-009, FR-010) in tests/unit/features/freeze_mode_test.cpp
- [x] T038 [P] [US2] Write test: -7 semitones shifts down a fifth per iteration in tests/unit/features/freeze_mode_test.cpp
- [x] T039 [P] [US2] Write test: pitch shift accuracy within +/- 5 cents (SC-005) in tests/unit/features/freeze_mode_test.cpp
- [x] T040 [P] [US2] Write test: shimmer mix blends pitched/unpitched (FR-011) in tests/unit/features/freeze_mode_test.cpp
- [x] T041 [P] [US2] Write test: pitch shift is modulatable without artifacts (FR-012) in tests/unit/features/freeze_mode_test.cpp

### 4.3 Implementation for User Story 2

- [x] T042 [US2] Add PitchShiftProcessor members to FreezeFeedbackProcessor in src/dsp/features/freeze_mode.h
- [x] T043 [US2] Implement FreezeFeedbackProcessor::setPitchSemitones()/setPitchCents() in src/dsp/features/freeze_mode.h
- [x] T044 [US2] Implement FreezeFeedbackProcessor::setShimmerMix() in src/dsp/features/freeze_mode.h
- [x] T045 [US2] Update FreezeFeedbackProcessor::process() to apply pitch shifting and shimmer mix blend in src/dsp/features/freeze_mode.h
- [x] T046 [US2] Add FreezeMode::setPitchSemitones()/setPitchCents()/setShimmerMix() delegating to processor in src/dsp/features/freeze_mode.h
- [x] T047 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [x] T048 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 4.5 Commit (MANDATORY)

- [x] T049 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Shimmer freeze fully functional

---

## Phase 5: User Story 3 - Evolving Textures with Decay (Priority: P3)

**Goal**: Decay control allows frozen content to gradually fade rather than sustain forever. 0% = infinite, 100% = fast fade.

**Independent Test**: Engage freeze with 50% decay, measure time for content to decay to -60dB.

### 5.1 Pre-Implementation (MANDATORY)

- [x] T050 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T051 [P] [US3] Write test: decay 0% results in infinite sustain (FR-013, FR-014, SC-002) in tests/unit/features/freeze_mode_test.cpp
- [x] T052 [P] [US3] Write test: decay 100% reaches -60dB within 500ms (FR-015, SC-003) in tests/unit/features/freeze_mode_test.cpp
- [x] T053 [P] [US3] Write test: decay 50% fades to half amplitude over ~2 seconds in tests/unit/features/freeze_mode_test.cpp
- [x] T054 [P] [US3] Write test: decay parameter changes are smoothed (FR-016, SC-007) in tests/unit/features/freeze_mode_test.cpp

### 5.3 Implementation for User Story 3

- [x] T055 [US3] Implement decay gain calculation in FreezeFeedbackProcessor (per-sample gain reduction) in src/dsp/features/freeze_mode.h
- [x] T056 [US3] Implement FreezeFeedbackProcessor::setDecayAmount() with coefficient calculation in src/dsp/features/freeze_mode.h
- [x] T057 [US3] Update FreezeFeedbackProcessor::process() to apply decay gain per sample in src/dsp/features/freeze_mode.h
- [x] T058 [US3] Add FreezeMode::setDecay() delegating to processor in src/dsp/features/freeze_mode.h
- [x] T059 [US3] Verify all US3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [x] T060 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 5.5 Commit (MANDATORY)

- [x] T061 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Decay control fully functional

---

## Phase 6: User Story 4 - Diffused Pad Textures (Priority: P4)

**Goal**: Diffusion smears transients in frozen content, creating smooth pad-like textures.

**Independent Test**: Freeze transient-heavy material, enable diffusion, measure reduction in peak-to-RMS ratio.

### 6.1 Pre-Implementation (MANDATORY)

- [x] T062 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T063 [P] [US4] Write test: diffusion 0% preserves transients (FR-017, FR-018) in tests/unit/features/freeze_mode_test.cpp
- [x] T064 [P] [US4] Write test: diffusion 100% smears transients into smooth texture in tests/unit/features/freeze_mode_test.cpp
- [x] T065 [P] [US4] Write test: diffusion preserves stereo width (FR-019, SC-006: within 5%) in tests/unit/features/freeze_mode_test.cpp
- [x] T066 [P] [US4] Write test: diffusion amount change is smooth in tests/unit/features/freeze_mode_test.cpp

### 6.3 Implementation for User Story 4

- [x] T067 [US4] Add DiffusionNetwork member to FreezeFeedbackProcessor in src/dsp/features/freeze_mode.h
- [x] T068 [US4] Implement FreezeFeedbackProcessor::setDiffusionAmount()/setDiffusionSize() in src/dsp/features/freeze_mode.h
- [x] T069 [US4] Update FreezeFeedbackProcessor::process() to apply diffusion in src/dsp/features/freeze_mode.h
- [x] T070 [US4] Add FreezeMode::setDiffusionAmount()/setDiffusionSize() delegating to processor in src/dsp/features/freeze_mode.h
- [x] T071 [US4] Verify all US4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [x] T072 [US4] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 6.5 Commit (MANDATORY)

- [x] T073 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Diffusion fully functional

---

## Phase 7: User Story 5 - Tonal Shaping (Priority: P5)

**Goal**: Filter in feedback path progressively darkens/brightens frozen content for tonal control.

**Independent Test**: Engage freeze with lowpass at 2kHz, measure frequency content above 2kHz decreases over iterations.

### 7.1 Pre-Implementation (MANDATORY)

- [x] T074 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T075 [P] [US5] Write test: lowpass filter attenuates above cutoff (FR-020, FR-021) in tests/unit/features/freeze_mode_test.cpp
- [x] T076 [P] [US5] Write test: highpass filter attenuates below cutoff in tests/unit/features/freeze_mode_test.cpp
- [x] T076a [P] [US5] Write test: bandpass filter attenuates above and below cutoff (FR-021 bandpass mode) in tests/unit/features/freeze_mode_test.cpp
- [x] T076b [P] [US5] Write test: filter cutoff works across full range 20Hz to 20kHz (FR-022) in tests/unit/features/freeze_mode_test.cpp
- [x] T077 [P] [US5] Write test: filter disabled preserves full frequency range in tests/unit/features/freeze_mode_test.cpp
- [x] T078 [P] [US5] Write test: filter cutoff change is smooth (FR-023, SC-007: within 20ms) in tests/unit/features/freeze_mode_test.cpp

### 7.3 Implementation for User Story 5

- [x] T079 [US5] Implement FreezeMode::setFilterEnabled()/setFilterType()/setFilterCutoff() delegating to FFN in src/dsp/features/freeze_mode.h
- [x] T080 [US5] Verify all US5 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [x] T081 [US5] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 7.5 Commit (MANDATORY)

- [x] T082 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Tonal shaping fully functional

---

## Phase 8: Edge Cases and Integration

**Purpose**: Test edge cases and verify full feature integration

### 8.1 Edge Case Tests

- [x] T083 Write test: freeze with empty delay buffer produces silence in tests/unit/features/freeze_mode_test.cpp
- [x] T084 Write test: delay time change deferred when frozen (per spec edge case) in tests/unit/features/freeze_mode_test.cpp
- [x] T085 Write test: short delay (<50ms) adapts transition time in tests/unit/features/freeze_mode_test.cpp
- [x] T086 Write test: multiple parameter changes while frozen apply smoothly in tests/unit/features/freeze_mode_test.cpp
- [x] T087 Write test: real-time safety - no allocations in process() in tests/unit/features/freeze_mode_test.cpp
- [x] T088 Write test: CPU usage below 1% at 44.1kHz stereo (SC-008) in tests/unit/features/freeze_mode_test.cpp

### 8.2 Implementation

- [x] T089 Fix any edge case test failures
- [x] T090 Verify all tests pass
- [x] T091 **Commit edge case fixes**

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 9.1 Architecture Documentation Update

- [x] T092 **Update ARCHITECTURE.md** with new components:
  - Add FreezeMode to Layer 4 features section
  - Add FreezeFeedbackProcessor to Layer 2 processors section (or note it's internal to FreezeMode)
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples

### 9.2 Final Commit

- [x] T093 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 10.1 Requirements Verification

- [x] T094 **Review ALL FR-xxx requirements** from spec.md against implementation (29 FRs)
- [x] T095 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved (8 SCs)
- [x] T096 **Search for cheating patterns** in implementation:
  - [x] No `// placeholder` or `// TODO` comments in new code
  - [x] No test thresholds relaxed from spec requirements
  - [x] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [x] T097 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [x] T098 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

- [x] T099 **All self-check questions answered "no"** (or gaps documented honestly):
  1. Did I change ANY test threshold from what the spec originally required? **No**
  2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **No**
  3. Did I remove ANY features from scope without telling the user? **No**
  4. Would the spec author consider this "done"? **Yes**
  5. If I were the user, would I feel cheated? **No**

**Checkpoint**: Honest assessment complete

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

- [x] T100 **Commit all spec work** to feature branch
- [x] T101 **Verify all tests pass** - All 1195 tests pass (4.6M+ assertions)
- [x] T102 **Claim completion ONLY if all requirements are MET** - All 29 FRs and 8 SCs verified MET

**Checkpoint**: Spec implementation honestly complete ✅

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - start immediately
- **Phase 2 (Foundational)**: Depends on Setup - BLOCKS all user stories
- **Phases 3-7 (User Stories)**: All depend on Phase 2 completion
  - US1 (Basic Freeze) can start immediately after Phase 2
  - US2-5 can proceed independently but build on US1 concepts
- **Phase 8 (Edge Cases)**: Depends on all user stories
- **Phases 9-11 (Documentation/Verification)**: Depend on all implementation

### User Story Dependencies

| Story | Depends On | Can Parallelize With |
|-------|------------|----------------------|
| US1 (Basic Freeze) | Phase 2 only | Independent (MVP) |
| US2 (Shimmer) | US1 (uses FreezeMode API) | US3, US4, US5 after US1 |
| US3 (Decay) | US1 | US2, US4, US5 after US1 |
| US4 (Diffusion) | US1 | US2, US3, US5 after US1 |
| US5 (Filter) | US1 | US2, US3, US4 after US1 |

### Parallel Opportunities

**Within Phase 2 (Foundational)**:
```
T005, T006, T007, T008 can run in parallel (different test sections)
```

**Within Each User Story**:
```
All [P]-marked test tasks can run in parallel
```

**After US1 Completion**:
```
US2, US3, US4, US5 can all proceed in parallel (different functionality)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (FreezeFeedbackProcessor)
3. Complete Phase 3: User Story 1 (Basic Freeze)
4. **STOP and VALIDATE**: Test basic freeze independently
5. Deploy/demo if ready - this is functional freeze mode

### Incremental Delivery

| Increment | Delivers | User Value |
|-----------|----------|------------|
| MVP (US1) | Basic freeze on/off | Infinite sustain |
| +US2 | Shimmer freeze | Evolving textures |
| +US3 | Decay control | Natural fade |
| +US4 | Diffusion | Pad textures |
| +US5 | Filter | Tonal shaping |

Each increment adds value without breaking previous functionality.

---

## Task Summary

| Phase | Task Count | Description |
|-------|------------|-------------|
| Setup | 3 | File scaffolding |
| Foundational | 12 | FreezeFeedbackProcessor |
| US1 (Basic Freeze) | 23 | MVP - core freeze functionality (+4 remediation: FR-008, FR-024, FR-025, FR-029) |
| US2 (Shimmer) | 15 | Pitch shifting in freeze |
| US3 (Decay) | 12 | Decay control |
| US4 (Diffusion) | 12 | Diffusion in freeze |
| US5 (Filter) | 11 | Filter in feedback (+2 remediation: FR-021 bandpass, FR-022 range) |
| Edge Cases | 9 | Edge case testing |
| Documentation | 2 | ARCHITECTURE.md |
| Verification | 9 | Requirement verification |
| **Total** | **108** | |

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST for each phase
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- FlexibleFeedbackNetwork already has freeze built-in - FreezeMode primarily composes and extends it
- FreezeFeedbackProcessor adds decay, pitch shift, and diffusion on top of FFN's freeze
