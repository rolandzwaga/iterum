# Research: UI Control Refactoring

**Date**: 2026-01-03
**Feature**: 045-ui-control-refactor

## Research Summary

Research completed prior to plan creation. VSTGUI control capabilities documented in `specs/VSTGUI-CONTROLS-REFERENCE.md`.

## Decisions Made

### 1. Control Type Selection

**Decision**: Use CSegmentButton for all dropdowns with 2-5 options

**Rationale**:
- CSegmentButton shows all options at once (better UX)
- Reduces clicks from 2 (open dropdown + select) to 1 (direct click)
- VSTGUI natively supports CSegmentButton with up to 5+ segments
- Works with existing StringListParameter bindings (normalized 0-1 values)

**Alternatives Considered**:
- CHorizontalSwitch: Requires bitmap assets, more work for same result
- CAnimKnob: Better for analog "selector" feel, but requires frame graphics
- Keep COptionMenu: Original approach, hides options

### 2. Styling Approach

**Decision**: Use horizontal style with consistent text styling across all controls

**Rationale**:
- Horizontal layout matches existing panel flow
- Consistent styling provides visual coherence
- No custom bitmaps needed - pure text-based segments

**Alternatives Considered**:
- Vertical style: Takes more vertical space, not suitable for panel layout
- Mixed styles: Would be visually inconsistent

### 3. Selection Mode

**Decision**: Use `kSingle` selection mode for all controls

**Rationale**:
- All parameters are mutually exclusive (only one value at a time)
- `kSingle` provides immediate visual feedback
- Matches behavior of original COptionMenu

**Alternatives Considered**:
- `kSingleToggle`: Would allow cycling through options by clicking same segment (unnecessary)
- `kMultiple`: Not appropriate for mutually exclusive parameters

## Technical Findings

### CSegmentButton XML Pattern

```xml
<view class="CSegmentButton"
      control-tag="ControlTagName"
      origin="x, y"
      size="width, height"
      style="horizontal"
      segment-names="Option1,Option2,Option3"
      selection-mode="kSingle"
      font="~ NormalFontSmall"
      text-color="~ WhiteCColor"
      frame-color="~ GreyCColor" />
```

### Parameter Binding Compatibility

CSegmentButton works identically to COptionMenu for parameter binding:
- Uses same `control-tag` attribute
- Receives normalized value (0.0-1.0)
- Maps normalized value to segment index: `index = round(normalized * (segmentCount - 1))`

**No changes needed to C++ parameter handling code.**

### Segment Label Length Recommendations

| Segment Count | Max Label Length | Example |
|---------------|------------------|---------|
| 2 | 8 chars | "Free", "Synced" |
| 3 | 6 chars | "LP", "HP", "BP" |
| 4 | 6 chars | "512", "1024" |
| 5 | 5 chars | "Off", "Semi" |

## References

- VSTGUI Reference: `specs/VSTGUI-CONTROLS-REFERENCE.md`
- Control Inventory: `specs/ui-controls-inventory.md`
- CSegmentButton Docs: https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_c_segment_button.html
