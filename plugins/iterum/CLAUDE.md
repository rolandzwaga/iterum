# plugins/iterum/ — Iterum Delay Plugin

Auto-loads when working under `plugins/iterum/`. Root `CLAUDE.md` still applies.

- **Type:** delay effect (Fx). **Version:** see `version.json` (edit ONLY that for bumps).
- **src skeleton:** `controller/ parameters/ preset/ processor/ ui/ update/`
  (`parameters/` holds per-mode registration helpers; `ui/` holds the tap-pattern editor custom view).
- **Param IDs:** mode-prefixed enum scheme `k{Mode}{Parameter}Id` (Granular base 100, Spectral, Shimmer,
  Tape, BBD, Digital, PingPong, Reverse, MultiTap, Freeze). Pipeline to add one:
  `plugin_ids.h → parameters/ → processor → controller → resources/editor.uidesc`.

## Parameter ID Naming Convention (moved from root CLAUDE.md)

All parameter IDs in `plugin_ids.h` MUST follow `k{Mode}{Parameter}Id` — `Mode` is the delay-mode
prefix above, `Parameter` in PascalCase.

**Standard Parameter Names (use these exact names):**

| Parameter | ID Suffix | Description |
|-----------|-----------|-------------|
| Delay Time | `DelayTimeId` | Main delay time in ms |
| Feedback | `FeedbackId` | Feedback amount (0-120%) |
| Mix | `MixId` | Dry/Wet mix (NOT "DryWet") |
| Time Mode | `TimeModeId` | Free/Synced selector |
| Note Value | `NoteValueId` | Tempo sync note value |
| Mod Depth | `ModDepthId` | Modulation depth (NOT "ModulationDepth") |
| Mod Rate | `ModRateId` | Modulation rate (NOT "ModulationRate") |
| Stereo Width | `StereoWidthId` | Stereo decorrelation amount |
| Width | `WidthId` | Pan width (use when NOT stereo decorrelation) |
| Age | `AgeId` | Component aging amount |
| Era | `EraId` | Era/model selector |
| Freeze | `FreezeId` | Freeze toggle |
| Filter Enabled | `FilterEnabledId` | Filter on/off toggle |
| Filter Cutoff | `FilterCutoffId` | Filter cutoff frequency |
| Filter Type | `FilterTypeId` | Filter type selector |
| Diffusion | `DiffusionId` | Diffusion amount |

**Compound Parameters:** descriptive sub-component names — `kTapeHead1EnabledId`,
`kTapeHead1LevelId`, `kTapeHead1PanId`, `kShimmerPitchSemitonesId`, `kShimmerPitchCentsId`,
`kSpectralFeedbackTiltId`.

**AVOID:** redundant prefixes (`kShimmerShimmerMixId` → `kShimmerMixId`); inconsistent
abbreviations (`Mod` not `Modulation`); inconsistent terms (`Mix` not `DryWet`).
- **Tests:** two targets — `plugin_tests` (unit) **and** `approval_tests` (golden-reference approvals).
  Run BOTH for any DSP/output change:
  ```bash
  build/windows-x64-release/bin/Release/plugin_tests.exe    2>&1 | tail -5
  build/windows-x64-release/bin/Release/approval_tests.exe  2>&1 | tail -5
  ```
- **pluginval:** `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Iterum.vst3"`
- **UI:** `resources/editor.uidesc`.
