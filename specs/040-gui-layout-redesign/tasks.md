# Tasks: GUI Layout Redesign with Grouped Controls

**Input**: Design documents from `/specs/040-gui-layout-redesign/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, quickstart.md

**Tests**: This feature modifies only XML UI definitions (no C++ code). Visual verification and pluginval validation replace unit tests. No unit tests are possible for XML layout changes.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ Verification Approach for XML-Only Changes

Since this feature modifies only `editor.uidesc` (XML), traditional unit tests are not applicable. Verification uses:

1. **Visual Verification**: Load plugin and visually inspect each mode panel
2. **Pluginval Validation**: Ensure plugin loads correctly with restructured UI
3. **Functional Testing**: Verify all controls still work (parameter binding intact)

---

## Phase 1: Setup (Preparation)

**Purpose**: Prepare for XML modifications and create backup

- [ ] T001 Create backup of resources/editor.uidesc before modifications
- [ ] T002 Review quickstart.md and data-model.md to understand group structure patterns
- [ ] T003 Verify existing color and font definitions in resources/editor.uidesc meet requirements

---

## Phase 2: User Story 1 - Primary Delay Controls (Priority: P1) 🎯 MVP

**Goal**: Add "TIME & MIX" group to all 11 mode panels containing primary delay controls (time, feedback, dry/wet)

**Independent Test**: Load any mode in plugin and verify TIME & MIX group is visible at top with correctly grouped controls

### 2.1 Implementation for User Story 1

> **Group Structure**: TIME & MIX group at (10, 30) containing Delay Time, Time Mode, Note Value, Feedback, Dry/Wet

- [ ] T004 [P] [US1] Add TIME & MIX group container to GranularPanel in resources/editor.uidesc
- [ ] T005 [P] [US1] Add TIME & MIX group container to SpectralPanel in resources/editor.uidesc
- [ ] T006 [P] [US1] Add TIME & MIX group container to ShimmerPanel in resources/editor.uidesc
- [ ] T007 [P] [US1] Add TIME & MIX group container to TapePanel in resources/editor.uidesc
- [ ] T008 [P] [US1] Add TIME & MIX group container to BBDPanel in resources/editor.uidesc
- [ ] T009 [P] [US1] Add TIME & SYNC group container to DigitalPanel in resources/editor.uidesc
- [ ] T010 [P] [US1] Add TIME & SYNC group container to PingPongPanel in resources/editor.uidesc
- [ ] T011 [P] [US1] Add TIME & MIX group container to ReversePanel in resources/editor.uidesc
- [ ] T012 [P] [US1] Add TIME group container to MultiTapPanel in resources/editor.uidesc
- [ ] T013 [P] [US1] Add TIME & MIX group container to FreezePanel in resources/editor.uidesc
- [ ] T014 [P] [US1] Add DELAY group container to DuckingPanel in resources/editor.uidesc

### 2.2 Verification for User Story 1

- [ ] T015 [US1] Build plugin: cmake --build build --config Debug --target Iterum
- [ ] T016 [US1] Visual verification: Load plugin and check TIME & MIX groups in all 11 modes
- [ ] T017 [US1] Functional verification: Test that time/feedback/mix controls still work in each mode
- [ ] T018 [US1] **Commit completed User Story 1 work**

**Checkpoint**: All 11 modes display TIME & MIX (or equivalent) group at top of panel

---

## Phase 3: User Story 2 - Character Controls (Priority: P2)

**Goal**: Add "CHARACTER" group to Tape, BBD, and Digital modes containing character/coloration controls

**Independent Test**: Load Tape, BBD, or Digital mode and verify CHARACTER group contains appropriate controls

### 3.1 Implementation for User Story 2

> **Group Structure**: CHARACTER group containing Age, Era, Saturation, Wear, Motor Speed, Inertia, Limiter as applicable

- [ ] T019 [P] [US2] Add CHARACTER group (Motor Speed, Inertia, Wear, Saturation, Age) to TapePanel in resources/editor.uidesc
- [ ] T020 [P] [US2] Add CHARACTER group (Age, Era) to BBDPanel in resources/editor.uidesc
- [ ] T021 [P] [US2] Add CHARACTER group (Era, Age, Limiter) to DigitalPanel in resources/editor.uidesc

### 3.2 Verification for User Story 2

- [ ] T022 [US2] Build plugin: cmake --build build --config Debug --target Iterum
- [ ] T023 [US2] Visual verification: Check CHARACTER groups in Tape, BBD, Digital modes
- [ ] T024 [US2] Functional verification: Test character controls still work
- [ ] T025 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Tape, BBD, Digital modes display CHARACTER group with appropriate controls

---

## Phase 4: User Story 3 - Modulation Controls (Priority: P2)

**Goal**: Add "MODULATION" group to BBD, Digital, and PingPong modes containing mod depth, rate, waveform

**Independent Test**: Load BBD, Digital, or PingPong mode and verify MODULATION group contains mod controls

### 4.1 Implementation for User Story 3

> **Group Structure**: MODULATION group containing Mod Depth, Mod Rate, Waveform as applicable

- [ ] T026 [P] [US3] Add MODULATION group (Mod Depth, Mod Rate) to BBDPanel in resources/editor.uidesc
- [ ] T027 [P] [US3] Add MODULATION group (Mod Depth, Mod Rate, Waveform) to DigitalPanel in resources/editor.uidesc
- [ ] T028 [P] [US3] Add MODULATION group (Mod Depth, Mod Rate) to PingPongPanel in resources/editor.uidesc

### 4.2 Verification for User Story 3

- [ ] T029 [US3] Build plugin: cmake --build build --config Debug --target Iterum
- [ ] T030 [US3] Visual verification: Check MODULATION groups in BBD, Digital, PingPong modes
- [ ] T031 [US3] Functional verification: Test modulation controls still work
- [ ] T032 [US3] **Commit completed User Story 3 work**

**Checkpoint**: BBD, Digital, PingPong modes display MODULATION group

---

## Phase 5: User Story 4 - Specialized Mode Controls (Priority: P3)

**Goal**: Add mode-specific specialized groups (Grain, Spray, Tape Heads, Spectral, Pitch Shift, etc.)

**Independent Test**: Load each mode and verify specialized controls are in appropriately named groups

### 5.1 Granular Mode Specialized Groups

- [ ] T033 [P] [US4] Add GRAIN PARAMETERS group to GranularPanel in resources/editor.uidesc
- [ ] T034 [P] [US4] Add SPRAY & RANDOMIZATION group to GranularPanel in resources/editor.uidesc
- [ ] T035 [P] [US4] Add GRAIN OPTIONS group to GranularPanel in resources/editor.uidesc

### 5.2 Spectral Mode Specialized Groups

- [ ] T036 [P] [US4] Add SPECTRAL ANALYSIS group to SpectralPanel in resources/editor.uidesc
- [ ] T037 [P] [US4] Add SPECTRAL CHARACTER group to SpectralPanel in resources/editor.uidesc

### 5.3 Shimmer Mode Specialized Groups

- [ ] T038 [P] [US4] Add PITCH SHIFT group to ShimmerPanel in resources/editor.uidesc
- [ ] T039 [P] [US4] Add DIFFUSION & FILTER group to ShimmerPanel in resources/editor.uidesc

### 5.4 Tape Mode Specialized Groups

- [ ] T040 [P] [US4] Add SPLICE group to TapePanel in resources/editor.uidesc
- [ ] T041 [P] [US4] Add TAPE HEADS group to TapePanel in resources/editor.uidesc

### 5.5 Reverse Mode Specialized Groups

- [ ] T042 [P] [US4] Add CHUNK group to ReversePanel in resources/editor.uidesc
- [ ] T043 [P] [US4] Add FILTER group to ReversePanel in resources/editor.uidesc

### 5.6 PingPong Mode Specialized Groups

- [ ] T044 [P] [US4] Add STEREO group to PingPongPanel in resources/editor.uidesc

### 5.7 MultiTap Mode Specialized Groups

- [ ] T045 [P] [US4] Add PATTERN group to MultiTapPanel in resources/editor.uidesc
- [ ] T046 [P] [US4] Add MIX group to MultiTapPanel in resources/editor.uidesc
- [ ] T047 [P] [US4] Add FEEDBACK FILTERS group to MultiTapPanel in resources/editor.uidesc
- [ ] T048 [P] [US4] Add MORPHING group to MultiTapPanel in resources/editor.uidesc

### 5.8 Freeze Mode Specialized Groups

- [ ] T049 [P] [US4] Add FREEZE CONTROL group to FreezePanel in resources/editor.uidesc
- [ ] T050 [P] [US4] Add PITCH & SHIMMER group to FreezePanel in resources/editor.uidesc
- [ ] T051 [P] [US4] Add DIFFUSION & FILTER group to FreezePanel in resources/editor.uidesc

### 5.9 Ducking Mode Specialized Groups

- [ ] T052 [P] [US4] Add DUCKER DYNAMICS group to DuckingPanel in resources/editor.uidesc
- [ ] T053 [P] [US4] Add SIDECHAIN group to DuckingPanel in resources/editor.uidesc

### 5.10 OUTPUT Groups (All Modes)

- [ ] T054 [P] [US4] Add OUTPUT group to all 11 mode panels in resources/editor.uidesc

### 5.11 Verification for User Story 4

- [ ] T055 [US4] Build plugin: cmake --build build --config Debug --target Iterum
- [ ] T056 [US4] Visual verification: Check all specialized groups in all 11 modes
- [ ] T057 [US4] Functional verification: Test all controls still work in all modes
- [ ] T058 [US4] **Commit completed User Story 4 work**

**Checkpoint**: All 11 modes display all specified groups with proper labeling

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Final validation and consistency checks

- [ ] T059 [P] Verify consistent 10px spacing between all groups across all modes in resources/editor.uidesc
- [ ] T060 [P] Verify all group headers use section-font and accent color consistently
- [ ] T061 [P] Verify panel dimensions remain 860x400 in all modes
- [ ] T062 Run pluginval validation: tools/pluginval.exe --strictness-level 5 --validate build/VST3/Debug/Iterum.vst3
- [ ] T063 Test plugin in DAW: Load each mode and verify visual layout

---

## Phase 7: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 7.1 Architecture Documentation Update

- [ ] T064 **Update ARCHITECTURE.md** with UI grouping pattern documentation:
  - Add UI section describing group container pattern
  - Document color and font conventions for groups
  - Add "when to use this pattern" guidance for future modes

### 7.2 Final Commit

- [ ] T065 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects UI grouping patterns

---

## Phase 8: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 8.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T066 **Review ALL FR-001 through FR-012** from spec.md against implementation
- [ ] T067 **Review ALL SC-001 through SC-007** and verify measurable targets are achieved
- [ ] T068 **Search for issues** in implementation:
  - [ ] All controls visible in all modes
  - [ ] No overlapping groups or controls
  - [ ] All parameter bindings intact

### 8.2 Fill Compliance Table in spec.md

- [ ] T069 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T070 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 8.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I skip any mode panels?
2. Are any groups missing their header labels?
3. Are any controls orphaned (not in a group)?
4. Would the user feel the layout is "cluttered" rather than "organized"?

- [ ] T071 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 9: Final Completion

**Purpose**: Final commit and completion claim

### 9.1 Final Commit

- [ ] T072 **Commit all spec work** to feature branch
- [ ] T073 **Verify pluginval passes**

### 9.2 Completion Claim

- [ ] T074 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **US1 (Phase 2)**: Depends on Setup - creates foundation groups
- **US2 (Phase 3)**: Can run in parallel with US1 (different modes, but same file)
- **US3 (Phase 4)**: Can run in parallel with US1/US2 (different groups in same modes)
- **US4 (Phase 5)**: Depends on US1 being done first (builds on TIME & MIX structure)
- **Polish (Phase 6)**: Depends on all user stories being complete
- **Documentation (Phase 7)**: Depends on Polish phase
- **Verification (Phase 8)**: Depends on Documentation phase
- **Final (Phase 9)**: Depends on all prior phases

### User Story Dependencies

- **User Story 1 (P1)**: Foundation - must complete first to establish TIME & MIX pattern
- **User Story 2 (P2)**: Independent - CHARACTER groups in specific modes
- **User Story 3 (P2)**: Independent - MODULATION groups in specific modes
- **User Story 4 (P3)**: Builds on US1 structure - adds remaining specialized groups

### Parallel Opportunities

Since all tasks modify the same file (editor.uidesc), true parallelism is limited. However:

- Tasks within a single mode panel (e.g., T004-T014) can be done sequentially or as one batch per mode
- Different developers could work on different mode panels
- Verification tasks can start as soon as a batch of mode panels is complete

### Recommended Execution: Mode-by-Mode

For simplicity, implement one mode at a time:

1. **Granular**: T004, T033, T034, T035, T054 (partial)
2. **Spectral**: T005, T036, T037, T054 (partial)
3. **Shimmer**: T006, T038, T039, T054 (partial)
4. **Tape**: T007, T019, T040, T041, T054 (partial)
5. **BBD**: T008, T020, T026, T054 (partial)
6. **Digital**: T009, T021, T027, T054 (partial)
7. **PingPong**: T010, T028, T044, T054 (partial)
8. **Reverse**: T011, T042, T043, T054 (partial)
9. **MultiTap**: T012, T045, T046, T047, T048, T054 (partial)
10. **Freeze**: T013, T049, T050, T051, T054 (partial)
11. **Ducking**: T014, T052, T053, T054 (partial)

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T003)
2. Complete Phase 2: User Story 1 - TIME & MIX groups (T004-T018)
3. **STOP and VALIDATE**: Test all modes have TIME & MIX group
4. Demo if ready - primary controls now grouped

### Incremental Delivery

1. Complete US1 → All modes have primary controls grouped (MVP)
2. Add US2 → Tape, BBD, Digital have CHARACTER groups
3. Add US3 → BBD, Digital, PingPong have MODULATION groups
4. Add US4 → All modes fully organized with all groups
5. Each story adds organization without breaking previous work

---

## Summary

| Phase | Tasks | Purpose |
|-------|-------|---------|
| Setup | T001-T003 | Preparation and backup |
| US1 (P1) | T004-T018 | TIME & MIX groups (all 11 modes) |
| US2 (P2) | T019-T025 | CHARACTER groups (Tape, BBD, Digital) |
| US3 (P2) | T026-T032 | MODULATION groups (BBD, Digital, PingPong) |
| US4 (P3) | T033-T058 | Specialized groups (all modes) |
| Polish | T059-T063 | Validation and consistency |
| Documentation | T064-T065 | ARCHITECTURE.md update |
| Verification | T066-T071 | Requirements compliance |
| Final | T072-T074 | Completion |

**Total Tasks**: 74
**Tasks per User Story**: US1=15, US2=7, US3=7, US4=26
**Parallel Opportunities**: Limited (single file), but mode-by-mode batching recommended
**MVP Scope**: User Story 1 (T001-T018) - 18 tasks

---

## Notes

- This is an **XML-only feature** - no C++ code changes
- All tasks modify `resources/editor.uidesc`
- Visual verification replaces unit tests
- Pluginval validation ensures plugin still loads correctly
- Control bindings (control-tag) must remain unchanged
- Panel dimensions must remain 860x400
