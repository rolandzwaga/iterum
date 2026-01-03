# Implementation Plan: UI Control Refactoring

**Branch**: `045-ui-control-refactor` | **Date**: 2026-01-03 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/045-ui-control-refactor/spec.md`

## Summary

Convert 22 COptionMenu dropdown controls to CSegmentButton controls across all delay mode panels in editor.uidesc. This is a UI-only refactoring that improves UX by making all options visible at once for parameters with 2-5 choices.

## Technical Context

**Language/Version**: XML (VSTGUI UI Description format)
**Primary Dependencies**: VSTGUI 4.12+ (CSegmentButton class)
**Storage**: N/A (UI definition only)
**Testing**: Manual UI testing + pluginval validation
**Target Platform**: Windows, macOS, Linux (cross-platform VSTGUI)
**Project Type**: VST3 plugin UI refactoring
**Performance Goals**: N/A (no runtime performance impact)
**Constraints**: Must maintain parameter binding compatibility
**Scale/Scope**: 22 control changes across 10 mode panels in 1 file

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle VI (Cross-Platform Compatibility)**: ✅ PASS
- CSegmentButton is a VSTGUI cross-platform control
- No platform-specific code required

**Principle V (VSTGUI Development)**: ✅ PASS
- Using UIDescription XML as required
- All parameter values remain normalized (0.0-1.0)
- No audio thread involvement

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include manual UI verification steps
- [x] Each phase will end with pluginval validation
- [x] Each phase will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] N/A - No new C++ classes/structs being created
- [x] This is XML-only UI refactoring

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: N/A for this feature - no new C++ code being created.*

This feature modifies only `plugins/iterum/resources/editor.uidesc` (XML). No C++ classes, structs, or functions are being created.

### Mandatory Searches Performed

**Classes/Structs to be created**: NONE

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| — | — | — | N/A (XML-only change) |

**Utility Functions to be created**: NONE

### Existing Components to Reuse

| Component | Location | How It Will Be Used |
|-----------|----------|---------------------|
| CSegmentButton | VSTGUI library | Replace COptionMenu controls |
| Existing control-tag bindings | editor.uidesc | Preserved as-is |
| StringListParameter definitions | *_params.h files | No changes needed |

### ODR Risk Assessment

**Risk Level**: None

**Justification**: This is purely an XML file modification. No C++ code is being created or modified. The parameter handling code already works with normalized 0-1 values and does not need changes.

## Dependency API Contracts (Principle XIV Extension)

### VSTGUI CSegmentButton XML Attributes

| Attribute | Type | Required | Description |
|-----------|------|----------|-------------|
| `class` | string | Yes | Must be "CSegmentButton" |
| `control-tag` | string | Yes | Parameter binding (preserved from COptionMenu) |
| `segment-names` | string | Yes | Comma-separated segment labels |
| `style` | string | No | "horizontal" (default) or "vertical" |
| `selection-mode` | string | No | "kSingle" (default), "kSingleToggle", "kMultiple" |
| `font` | string | No | Font reference |
| `text-color` | string | No | Text color reference |
| `frame-color` | string | No | Border color reference |

### Header Files Read

- [x] `specs/VSTGUI-CONTROLS-REFERENCE.md` - Local documentation created from research
- [x] VSTGUI online docs for CSegmentButton - https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_c_segment_button.html

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| CSegmentButton | segment-names uses comma separator, no spaces | `segment-names="Free,Synced"` |
| CSegmentButton | selection-mode is "kSingle" not "single" | `selection-mode="kSingle"` |
| CSegmentButton | style is "horizontal" not "kHorizontal" in XML | `style="horizontal"` |

## Layer 0 Candidate Analysis

*N/A - No new utility functions being created. UI-only change.*

## Higher-Layer Reusability Analysis

*N/A - No new C++ code being created. UI-only change.*

## Project Structure

### Documentation (this feature)

```text
specs/045-ui-control-refactor/
├── spec.md              # Feature specification (complete)
├── plan.md              # This file
└── (no research.md needed - VSTGUI reference already documented)
```

### Source Code Changes

```text
plugins/iterum/resources/
└── editor.uidesc        # ONLY file being modified (21 control changes)
```

**Structure Decision**: Single-file modification in existing plugin resources directory.

## Implementation Phases

### Phase 1: TimeMode Controls (10 changes)
All identical pattern - 2 segments (Free, Synced).

Files: `editor.uidesc`
Panels: Granular, Spectral, Shimmer, BBD, Digital, PingPong, Reverse, MultiTap, Freeze, Ducking

### Phase 2: FilterType Controls (2 changes)
3 segments (LP, HP, BP).

Files: `editor.uidesc`
Panels: Reverse, Freeze

### Phase 3: Era/Model Controls (2 changes)
3-4 segments (chip models, eras).

Files: `editor.uidesc`
Panels: BBD (4 segments), Digital (3 segments)

### Phase 4: Mode-Specific Controls (7 changes)
Various segment counts (2-5).

Files: `editor.uidesc`
Controls: SpectralFFTSize, SpectralSpreadDirection, SpectralSpreadCurve, GranularEnvelopeType, GranularPitchQuant, DigitalLimiterCharacter, ReversePlaybackMode, DuckingDuckTarget

## Verification Steps (per phase)

1. Build plugin: `cmake --build build --config Release`
2. Load in DAW, navigate to each modified panel
3. Verify all segments visible and labeled correctly
4. Verify clicking segments changes parameter value
5. Verify preset save/load preserves selection
6. Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"`

## Complexity Tracking

No Constitution violations. This is a straightforward UI refactoring with no architectural complexity.
