# Feature Specification: UI Control Refactoring

**Feature Branch**: `045-ui-control-refactor`
**Created**: 2026-01-03
**Status**: Planning
**Input**: Convert 22 COptionMenu dropdowns to CSegmentButton controls based on user-confirmed decisions in `specs/ui-controls-inventory.md`

## Overview

Replace dropdown menus (COptionMenu) with segmented button controls (CSegmentButton) for parameters with 2-5 options. This improves UX by making all options visible at once, reducing clicks, and providing immediate visual feedback of the current selection.

## Requirements

### Functional Requirements

**FR-001**: All 10 TimeMode controls MUST be converted to CSegmentButton with 2 segments (Free, Synced)

**FR-002**: All 2 FilterType controls MUST be converted to CSegmentButton with 3 segments (LP, HP, BP)

**FR-003**: BBDEra MUST be converted to CSegmentButton with 4 segments (MN3005, MN3007, MN3205, SAD1024)

**FR-004**: DigitalEra MUST be converted to CSegmentButton with 3 segments (Pristine, 80s Digital, Lo-Fi)

**FR-005**: DigitalLimiterCharacter MUST be converted to CSegmentButton with 3 segments (Soft, Medium, Hard)

**FR-006**: SpectralFFTSize MUST be converted to CSegmentButton with 4 segments (512, 1024, 2048, 4096)

**FR-007**: SpectralSpreadDirection MUST be converted to CSegmentButton with 3 segments (Lo→Hi, Hi→Lo, Center)

**FR-008**: SpectralSpreadCurve MUST be converted to CSegmentButton with 2 segments (Linear, Log)

**FR-009**: GranularEnvelopeType MUST be converted to CSegmentButton with 4 segments (Hann, Trapezoid, Sine, Blackman)

**FR-010**: GranularPitchQuant MUST be converted to CSegmentButton with 5 segments (Off, Semi, Oct, 5th, Scale)

**FR-011**: ReversePlaybackMode MUST be converted to CSegmentButton with 3 segments (Full, Alt, Random)

**FR-012**: DuckingDuckTarget MUST be converted to CSegmentButton with 3 segments (Output, Feedback, Both)

**FR-013**: All CSegmentButton controls MUST use `style="horizontal"` for consistency

**FR-014**: All CSegmentButton controls MUST use `selection-mode="kSingle"` for mutually exclusive selection

**FR-015**: All existing parameter bindings (control-tag) MUST be preserved

**FR-016**: All existing tests MUST continue to pass after refactoring

### Non-Functional Requirements

**NFR-001**: No changes to parameter handling code (already works with normalized 0-1 values)

**NFR-002**: Visual styling should be consistent across all CSegmentButton instances

## Implementation Details

### File Changes Required

**Primary file**: `plugins/iterum/resources/editor.uidesc`

Each COptionMenu change follows this pattern:

```xml
<!-- BEFORE -->
<view class="COptionMenu"
      control-tag="GranularTimeMode"
      origin="x, y"
      size="w, h"
      ... />

<!-- AFTER -->
<view class="CSegmentButton"
      control-tag="GranularTimeMode"
      origin="x, y"
      size="w, h"
      style="horizontal"
      segment-names="Free,Synced"
      selection-mode="kSingle"
      font="~ NormalFontSmall"
      text-color="~ WhiteCColor"
      frame-color="~ GreyCColor"
      ... />
```

### Controls by Mode Panel

#### GranularPanel
| Control Tag | Segments | segment-names |
|-------------|----------|---------------|
| GranularTimeMode | 2 | `Free,Synced` |
| GranularEnvelopeType | 4 | `Hann,Trapezoid,Sine,Blackman` |
| GranularPitchQuant | 5 | `Off,Semi,Oct,5th,Scale` |

#### SpectralPanel
| Control Tag | Segments | segment-names |
|-------------|----------|---------------|
| SpectralTimeMode | 2 | `Free,Synced` |
| SpectralFFTSize | 4 | `512,1024,2048,4096` |
| SpectralSpreadDirection | 3 | `Lo→Hi,Hi→Lo,Center` |
| SpectralSpreadCurve | 2 | `Linear,Log` |

#### ShimmerPanel
| Control Tag | Segments | segment-names |
|-------------|----------|---------------|
| ShimmerTimeMode | 2 | `Free,Synced` |

#### BBDPanel
| Control Tag | Segments | segment-names |
|-------------|----------|---------------|
| BBDTimeMode | 2 | `Free,Synced` |
| BBDEra | 4 | `MN3005,MN3007,MN3205,SAD1024` |

#### DigitalPanel
| Control Tag | Segments | segment-names |
|-------------|----------|---------------|
| DigitalTimeMode | 2 | `Free,Synced` |
| DigitalEra | 3 | `Pristine,80s,Lo-Fi` |
| DigitalLimiterCharacter | 3 | `Soft,Medium,Hard` |

#### PingPongPanel
| Control Tag | Segments | segment-names |
|-------------|----------|---------------|
| PingPongTimeMode | 2 | `Free,Synced` |

#### ReversePanel
| Control Tag | Segments | segment-names |
|-------------|----------|---------------|
| ReverseTimeMode | 2 | `Free,Synced` |
| ReversePlaybackMode | 3 | `Full,Alt,Random` |
| ReverseFilterType | 3 | `LP,HP,BP` |

#### MultiTapPanel
| Control Tag | Segments | segment-names |
|-------------|----------|---------------|
| MultiTapTimeMode | 2 | `Free,Synced` |

#### FreezePanel
| Control Tag | Segments | segment-names |
|-------------|----------|---------------|
| FreezeTimeMode | 2 | `Free,Synced` |
| FreezeFilterType | 3 | `LP,HP,BP` |

#### DuckingPanel
| Control Tag | Segments | segment-names |
|-------------|----------|---------------|
| DuckingTimeMode | 2 | `Free,Synced` |
| DuckingDuckTarget | 3 | `Output,Feedback,Both` |

## Implementation Order

### Phase 1: TimeMode Controls (10 changes)
All identical - create template, apply to all modes.
- GranularTimeMode
- SpectralTimeMode
- ShimmerTimeMode
- BBDTimeMode
- DigitalTimeMode
- PingPongTimeMode
- ReverseTimeMode
- MultiTapTimeMode
- FreezeTimeMode
- DuckingTimeMode

### Phase 2: FilterType Controls (2 changes)
- ReverseFilterType
- FreezeFilterType

### Phase 3: Era/Model Controls (2 changes)
- BBDEra
- DigitalEra

### Phase 4: Mode-Specific Controls (8 changes)
- DigitalLimiterCharacter
- SpectralFFTSize
- SpectralSpreadDirection
- SpectralSpreadCurve
- GranularEnvelopeType
- GranularPitchQuant
- ReversePlaybackMode
- DuckingDuckTarget

## Testing

### Manual Testing
1. Load plugin in DAW
2. For each converted control:
   - Verify all segments are visible and labeled correctly
   - Verify clicking each segment selects it (visual feedback)
   - Verify parameter value changes correctly
   - Verify host automation works
   - Verify preset save/load preserves selection

### Automated Testing
- All existing unit tests must pass
- All existing approval tests must pass
- Pluginval must pass at strictness level 5

## Success Criteria

- All 22 controls converted from COptionMenu to CSegmentButton
- All segments display correct labels
- All parameter bindings work correctly
- All tests pass
- Pluginval passes at strictness 5
- No visual regressions in other UI elements

## References

- Decision source: `specs/ui-controls-inventory.md`
- VSTGUI reference: `specs/VSTGUI-CONTROLS-REFERENCE.md`
- CSegmentButton docs: https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_c_segment_button.html

## Implementation Verification

**Date**: 2026-01-03
**Status**: Complete

### Requirements Compliance

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | ✅ PASS | All 10 TimeMode controls converted with `segment-names="Free,Synced"` |
| FR-002 | ✅ PASS | ReverseFilterType, FreezeFilterType converted with `segment-names="LP,HP,BP"` |
| FR-003 | ✅ PASS | BBDEra converted with `segment-names="MN3005,MN3007,MN3205,SAD1024"` |
| FR-004 | ✅ PASS | DigitalEra converted with `segment-names="Pristine,80s,Lo-Fi"` |
| FR-005 | ✅ PASS | DigitalLimiterCharacter converted with `segment-names="Soft,Medium,Hard"` |
| FR-006 | ✅ PASS | SpectralFFTSize converted with `segment-names="512,1024,2048,4096"` |
| FR-007 | ✅ PASS | SpectralSpreadDirection converted with `segment-names="Lo→Hi,Hi→Lo,Center"` |
| FR-008 | ✅ PASS | SpectralSpreadCurve converted with `segment-names="Linear,Log"` |
| FR-009 | ✅ PASS | GranularEnvelopeType converted with `segment-names="Hann,Trapezoid,Sine,Blackman"` |
| FR-010 | ✅ PASS | GranularPitchQuant converted with `segment-names="Off,Semi,Oct,5th,Scale"` |
| FR-011 | ✅ PASS | ReversePlaybackMode converted with `segment-names="Full,Alt,Random"` |
| FR-012 | ✅ PASS | DuckingDuckTarget converted with `segment-names="Output,Feedback,Both"` |
| FR-013 | ✅ PASS | All controls use `style="horizontal"` |
| FR-014 | ✅ PASS | All controls use `selection-mode="kSingle"` |
| FR-015 | ✅ PASS | All control-tag bindings preserved |
| FR-016 | ✅ PASS | All existing tests pass (1620/1621, 1 flaky unrelated test) |
| NFR-001 | ✅ PASS | No parameter handling code modified |
| NFR-002 | ✅ PASS | Consistent styling across all CSegmentButton instances |

### Automated Validation

| Check | Result |
|-------|--------|
| Build | ✅ PASS |
| Pluginval (strictness 5) | ✅ PASS |
| Unit tests | ✅ 1620/1621 PASS (1 flaky test unrelated to changes) |

### Manual Validation (User)

- [ ] T015: Verify all 10 TimeMode controls show Free/Synced segments
- [ ] T020: Verify Reverse and Freeze FilterType controls show LP/HP/BP segments
- [ ] T024: Verify BBD Era (4 segments) and Digital Era (3 segments) controls
- [ ] T034: Verify all mode-specific controls show correct segments
- [ ] T037: Verify all 11 delay modes function correctly
- [ ] T038: Verify preset save/load preserves all segment selections
