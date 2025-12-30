# Implementation Plan: GUI Layout Redesign with Grouped Controls

**Branch**: `040-gui-layout-redesign` | **Date**: 2025-12-30 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/040-gui-layout-redesign/spec.md`

## Summary

Redesign all 11 mode panel layouts in `editor.uidesc` to organize controls into visually distinct, labeled groups. Groups are ordered by usage frequency (Time & Mix first, specialized controls later). Implementation uses VSTGUI's cross-platform `CViewContainer` for grouping with `CTextLabel` headers.

## Technical Context

**Language/Version**: VSTGUI XML (UIDescription format) - no C++ changes required
**Primary Dependencies**: VSTGUI 4.12+ (included with VST3 SDK)
**Storage**: N/A
**Testing**: Visual verification + pluginval validation *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows, macOS, Linux (cross-platform VSTGUI)
**Project Type**: Single project - UI resource modification
**Performance Goals**: No performance impact (layout only, not runtime behavior)
**Constraints**: Panel dimensions must remain 860x400 pixels
**Scale/Scope**: 11 mode panels, 3-6 groups per panel, ~80 control relocations

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| Principle VI (Cross-Platform) | ✅ PASS | Uses VSTGUI components only (CViewContainer, CTextLabel) |
| Principle V (VSTGUI) | ✅ PASS | Uses UIDescription XML format, no custom C++ views |
| Principle XII (Test-First) | ✅ PASS | Visual tests verify layout; pluginval validates plugin |
| Principle XIV (ODR Prevention) | ✅ PASS | No new C++ classes - XML only |

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include visual verification step
- [x] Each panel redesign can be tested independently
- [x] Final task includes pluginval validation

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No C++ classes/functions will be created - XML modification only

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None - this is XML-only modification

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| N/A | — | — | XML modification only |

**Utility Functions to be created**: None

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | — | — | — | XML modification only |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| CViewContainer | VSTGUI lib | N/A | Group containers with background-color |
| CTextLabel | VSTGUI lib | N/A | Group headers with section-font |
| section-font | editor.uidesc | N/A | Group header labels (Arial 14 bold) |
| section color | editor.uidesc | N/A | Group background (#3a3a3a) |
| accent color | editor.uidesc | N/A | Group header text (#4a90d9) |

### Files Checked for Conflicts

- [x] `resources/editor.uidesc` - Current UI definition (will be modified)
- [x] `resources/editor_minimal.uidesc` - Minimal UI (may need similar updates if active)
- [x] No C++ files affected

### ODR Risk Assessment

**Risk Level**: None

**Justification**: This feature modifies only XML UI descriptions. No C++ code will be created, modified, or compiled. ODR violations are impossible for XML-only changes.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

This section documents VSTGUI XML attributes used for grouping:

| Component | Attribute | Value/Format | Purpose |
|-----------|-----------|--------------|---------|
| CViewContainer | class | `"CViewContainer"` | Container type |
| CViewContainer | origin | `"x, y"` | Position within parent |
| CViewContainer | size | `"width, height"` | Group dimensions |
| CViewContainer | background-color | color name | Group background |
| CViewContainer | transparent | `"true/false"` | Show/hide background |
| CTextLabel | class | `"CTextLabel"` | Label type |
| CTextLabel | title | string | Group name text |
| CTextLabel | font | font name | Typography |
| CTextLabel | font-color | color name | Text color |

### Header Files Read

- [x] `extern/vst3sdk/vstgui4/vstgui/lib/cviewcontainer.h` - CViewContainer class
- [x] `extern/vst3sdk/vstgui4/vstgui/uidescription/viewcreator/viewcontainercreator.cpp` - XML attributes
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/crowcolumnview.h` - CRowColumnView (optional)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| CViewContainer | Nested containers need explicit size | Always specify `size="w, h"` |
| CTextLabel | Font must exist in fonts section | Use pre-defined `section-font` |
| Colors | Must be defined in colors section | Use pre-defined or add new |

## Layer 0 Candidate Analysis

*Not applicable - this feature is UI/XML only, no DSP code.*

**Decision**: No Layer 0 extraction needed. This feature creates no reusable DSP utilities.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What patterns from THIS feature could be reused?*

**This feature's layer**: UI/Resources (not in DSP layer hierarchy)

**Related features at same layer** (from ROADMAP.md or known plans):
- Future preset browser UI
- Future waveform visualization
- Any future mode additions

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Group container pattern | HIGH | Any new mode panels | Document pattern |
| Group header style | HIGH | All future UI additions | Already using section-font |
| New "group" color (if added) | MEDIUM | Future grouped sections | Add to colors section |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Use existing section color for groups | Already defined, appropriate contrast |
| Use existing section-font for headers | Already styled appropriately |
| Document group pattern in comments | Future mode additions can follow same structure |

## Project Structure

### Documentation (this feature)

```text
specs/040-gui-layout-redesign/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # VSTGUI research findings
├── data-model.md        # Group structure definitions
├── quickstart.md        # Quick reference for implementation
├── checklists/          # Quality checklists
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Implementation tasks (created by /speckit.tasks)
```

### Source Code (repository root)

```text
resources/
├── editor.uidesc        # Main UI definition (TO BE MODIFIED)
└── editor_minimal.uidesc # Minimal UI (MAY NEED UPDATES)
```

**Structure Decision**: This feature modifies existing XML files only. No new files or directories are created in the source tree.

## Complexity Tracking

No constitution violations - this feature uses standard VSTGUI patterns.

## Design Decisions

### Group Visual Styling

**Background Colors**:
- Panel background: #353535 (`panel` color)
- Group background: #3a3a3a (`section` color) - provides subtle contrast
- Alternative: Add new `group` color (#404040) for stronger separation

**Group Header Styling**:
- Font: section-font (Arial 14 bold)
- Color: accent (#4a90d9) - matches mode titles
- Position: Top-left of group container
- Height: 22px (matches current mode title labels)

**Spacing**:
- Between groups: 10px vertical spacing
- Group padding: 10px internal margin
- Control spacing within groups: 8px horizontal, maintain vertical alignment

### Group Container Structure

Each group will be implemented as:

```xml
<!-- Group: TIME & MIX -->
<view class="CViewContainer" origin="10, 30" size="280, 180" background-color="section" transparent="false">
    <view class="CTextLabel" origin="5, 5" size="270, 18" title="TIME & MIX" font="section-font" font-color="accent" transparent="true"/>
    <!-- Controls positioned relative to group container -->
    <view class="CTextLabel" origin="10, 28" size="80, 16" ... />
    <view class="CSlider" origin="10, 45" size="120, 20" ... />
    <!-- ... more controls ... -->
</view>
```

### Layout Grid

Each mode panel (860x400) will use a layout grid:
- Left column: 10-290 (280px) - Primary group (Time & Mix)
- Center column: 300-580 (280px) - Secondary groups
- Right column: 590-850 (260px) - Tertiary/specialized groups
- Top section: 30-210 (~180px per group row)
- Bottom section: 220-390 (~170px per group row)
- Mode title: 10, 5 (already in place)
