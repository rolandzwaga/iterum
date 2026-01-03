# Tasks: UI Control Refactoring

**Feature**: 045-ui-control-refactor
**Generated**: 2026-01-03
**Total Tasks**: 28
**Total Controls**: 22

---

## Phase 1: Setup

- [X] T001 Create feature branch `045-ui-control-refactor` from main
- [X] T002 Verify build works before changes: `cmake --build build --config Release`
- [X] T003 Verify pluginval passes before changes: `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"`

---

## Phase 2: TimeMode Controls (10 changes)

**Goal**: Convert all TimeMode dropdowns to CSegmentButton with 2 segments (Free, Synced)

**Pattern**: `segment-names="Free,Synced"`

- [X] T004 [P] Convert GranularTimeMode to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (GranularPanel)
- [X] T005 [P] Convert SpectralTimeMode to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (SpectralPanel)
- [X] T006 [P] Convert ShimmerTimeMode to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (ShimmerPanel)
- [X] T007 [P] Convert BBDTimeMode to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (BBDPanel)
- [X] T008 [P] Convert DigitalTimeMode to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (DigitalPanel)
- [X] T009 [P] Convert PingPongTimeMode to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (PingPongPanel)
- [X] T010 [P] Convert ReverseTimeMode to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (ReversePanel)
- [X] T011 [P] Convert MultiTapTimeMode to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (MultiTapPanel)
- [X] T012 [P] Convert FreezeTimeMode to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (FreezePanel)
- [X] T013 [P] Convert DuckingTimeMode to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (DuckingPanel)
- [X] T014 Build and verify Phase 2: `cmake --build build --config Release`
- [ ] T015 Manual test Phase 2: Load plugin, verify all 10 TimeMode controls show Free/Synced segments
- [X] T016 Run pluginval after Phase 2: `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"`

---

## Phase 3: FilterType Controls (2 changes)

**Goal**: Convert FilterType dropdowns to CSegmentButton with 3 segments (LP, HP, BP)

**Pattern**: `segment-names="LP,HP,BP"`

- [X] T017 [P] Convert ReverseFilterType to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (ReversePanel)
- [X] T018 [P] Convert FreezeFilterType to CSegmentButton in `plugins/iterum/resources/editor.uidesc` (FreezePanel)
- [X] T019 Build and verify Phase 3: `cmake --build build --config Release`
- [ ] T020 Manual test Phase 3: Load plugin, verify Reverse and Freeze FilterType controls show LP/HP/BP segments

---

## Phase 4: Era/Model Controls (2 changes)

**Goal**: Convert Era selector dropdowns to CSegmentButton

- [X] T021 Convert BBDEra to CSegmentButton with `segment-names="MN3005,MN3007,MN3205,SAD1024"` in `plugins/iterum/resources/editor.uidesc` (BBDPanel)
- [X] T022 Convert DigitalEra to CSegmentButton with `segment-names="Pristine,80s,Lo-Fi"` in `plugins/iterum/resources/editor.uidesc` (DigitalPanel)
- [X] T023 Build and verify Phase 4: `cmake --build build --config Release`
- [ ] T024 Manual test Phase 4: Load plugin, verify BBD Era (4 segments) and Digital Era (3 segments) controls

---

## Phase 5: Mode-Specific Controls (8 changes)

**Goal**: Convert remaining mode-specific dropdowns to CSegmentButton

- [X] T025 Convert SpectralFFTSize to CSegmentButton with `segment-names="512,1024,2048,4096"` in `plugins/iterum/resources/editor.uidesc` (SpectralPanel)
- [X] T026 Convert SpectralSpreadDirection to CSegmentButton with `segment-names="Lo→Hi,Hi→Lo,Center"` in `plugins/iterum/resources/editor.uidesc` (SpectralPanel)
- [X] T027 Convert SpectralSpreadCurve to CSegmentButton with `segment-names="Linear,Log"` in `plugins/iterum/resources/editor.uidesc` (SpectralPanel)
- [X] T028 Convert GranularEnvelopeType to CSegmentButton with `segment-names="Hann,Trapezoid,Sine,Blackman"` in `plugins/iterum/resources/editor.uidesc` (GranularPanel)
- [X] T029 Convert GranularPitchQuant to CSegmentButton with `segment-names="Off,Semi,Oct,5th,Scale"` in `plugins/iterum/resources/editor.uidesc` (GranularPanel)
- [X] T030 Convert DigitalLimiterCharacter to CSegmentButton with `segment-names="Soft,Medium,Hard"` in `plugins/iterum/resources/editor.uidesc` (DigitalPanel)
- [X] T031 Convert ReversePlaybackMode to CSegmentButton with `segment-names="Full,Alt,Random"` in `plugins/iterum/resources/editor.uidesc` (ReversePanel)
- [X] T032 Convert DuckingDuckTarget to CSegmentButton with `segment-names="Output,Feedback,Both"` in `plugins/iterum/resources/editor.uidesc` (DuckingPanel)
- [X] T033 Build and verify Phase 5: `cmake --build build --config Release`
- [ ] T034 Manual test Phase 5: Load plugin, verify all mode-specific controls show correct segments

---

## Phase 6: Final Validation

- [X] T035 Run full test suite: `ctest --preset windows-x64-release`
- [X] T036 Run pluginval final validation: `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"`
- [ ] T037 Manual regression test: Verify all 11 delay modes function correctly
- [ ] T038 Verify preset save/load preserves all segment selections
- [X] T039 Update spec.md Implementation Verification section with compliance status
- [X] T040 Commit all changes with message: `refactor(ui): convert dropdowns to segmented buttons`

---

## Implementation Pattern Reference

For each control conversion, replace:

```xml
<!-- BEFORE -->
<view class="COptionMenu"
      control-tag="ControlTagName"
      origin="x, y"
      size="w, h"
      ... />

<!-- AFTER -->
<view class="CSegmentButton"
      control-tag="ControlTagName"
      origin="x, y"
      size="w, h"
      style="horizontal"
      segment-names="Option1,Option2,..."
      selection-mode="kSingle"
      font="~ NormalFontSmall"
      text-color="~ WhiteCColor"
      frame-color="~ GreyCColor"
      ... />
```

**Key attributes to preserve**: `control-tag`, `origin`, `size`
**Key attributes to add**: `style`, `segment-names`, `selection-mode`

---

## Parallel Execution Opportunities

Tasks marked with [P] can be executed in parallel as they modify different sections of the same file without dependencies:

- **Phase 2**: T004-T013 (10 TimeMode controls) - all parallelizable
- **Phase 3**: T017-T018 (2 FilterType controls) - parallelizable
- **Phase 5**: T025-T032 (8 mode-specific controls) - all parallelizable

---

## Dependencies

```
T001 → T002 → T003 (setup must complete first)
     ↓
T004-T013 [parallel] → T014 → T015 → T016
     ↓
T017-T018 [parallel] → T019 → T020
     ↓
T021-T022 → T023 → T024
     ↓
T025-T032 [parallel] → T033 → T034
     ↓
T035 → T036 → T037 → T038 → T039 → T040
```

---

## Task Summary

| Phase | Task Range | Description | Count |
|-------|------------|-------------|-------|
| 1 | T001-T003 | Setup | 3 |
| 2 | T004-T016 | TimeMode Controls | 13 |
| 3 | T017-T020 | FilterType Controls | 4 |
| 4 | T021-T024 | Era/Model Controls | 4 |
| 5 | T025-T034 | Mode-Specific Controls (8 changes) | 10 |
| 6 | T035-T040 | Final Validation | 6 |
| **Total** | | | **40** |

---

## Controls Checklist

| Control | Panel | Segments | segment-names | Task |
|---------|-------|----------|---------------|------|
| GranularTimeMode | Granular | 2 | `Free,Synced` | T004 |
| GranularEnvelopeType | Granular | 4 | `Hann,Trapezoid,Sine,Blackman` | T028 |
| GranularPitchQuant | Granular | 5 | `Off,Semi,Oct,5th,Scale` | T029 |
| SpectralTimeMode | Spectral | 2 | `Free,Synced` | T005 |
| SpectralFFTSize | Spectral | 4 | `512,1024,2048,4096` | T025 |
| SpectralSpreadDirection | Spectral | 3 | `Lo→Hi,Hi→Lo,Center` | T026 |
| SpectralSpreadCurve | Spectral | 2 | `Linear,Log` | T027 |
| ShimmerTimeMode | Shimmer | 2 | `Free,Synced` | T006 |
| BBDTimeMode | BBD | 2 | `Free,Synced` | T007 |
| BBDEra | BBD | 4 | `MN3005,MN3007,MN3205,SAD1024` | T021 |
| DigitalTimeMode | Digital | 2 | `Free,Synced` | T008 |
| DigitalEra | Digital | 3 | `Pristine,80s,Lo-Fi` | T022 |
| DigitalLimiterCharacter | Digital | 3 | `Soft,Medium,Hard` | T030 |
| PingPongTimeMode | PingPong | 2 | `Free,Synced` | T009 |
| ReverseTimeMode | Reverse | 2 | `Free,Synced` | T010 |
| ReversePlaybackMode | Reverse | 3 | `Full,Alt,Random` | T031 |
| ReverseFilterType | Reverse | 3 | `LP,HP,BP` | T017 |
| MultiTapTimeMode | MultiTap | 2 | `Free,Synced` | T011 |
| FreezeTimeMode | Freeze | 2 | `Free,Synced` | T012 |
| FreezeFilterType | Freeze | 3 | `LP,HP,BP` | T018 |
| DuckingTimeMode | Ducking | 2 | `Free,Synced` | T013 |
| DuckingDuckTarget | Ducking | 3 | `Output,Feedback,Both` | T032 |

**Total: 22 controls** (10 TimeMode + 2 FilterType + 2 Era + 8 Mode-Specific)
