# Feature Specification: GUI Layout Redesign with Grouped Controls

**Feature Branch**: `040-gui-layout-redesign`
**Created**: 2025-12-30
**Status**: Draft
**Input**: User description: "Analyze all GUIs for each mode and design a better layout with controls grouped by functionality, most common functions first and more specialized later. Research VSTGUI layout components. Name groups and show names in UI."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Quickly Adjust Primary Delay Settings (Priority: P1)

A musician using Iterum wants to quickly adjust the most important delay parameters without hunting through a cluttered interface. They should immediately see and access the primary controls (delay time, feedback, mix) in a prominent "Time & Mix" group at the top of each mode panel.

**Why this priority**: The primary delay controls (time, feedback, dry/wet) are adjusted most frequently. Placing them in a clearly labeled group at the top reduces cognitive load and speeds up workflow.

**Independent Test**: Can be fully tested by loading any mode and verifying the "Time & Mix" group contains the essential delay controls in the expected position.

**Acceptance Scenarios**:

1. **Given** the user loads any delay mode, **When** the mode panel appears, **Then** the top section displays a clearly labeled "TIME & MIX" group containing delay time, feedback, and dry/wet controls.
2. **Given** the user is in Digital mode with tempo sync enabled, **When** they look at the Time & Mix group, **Then** the tempo sync controls (Time Mode, Note Value) are visible alongside Delay Time.
3. **Given** the user switches between different modes, **When** each mode panel loads, **Then** the primary controls are always in the same relative position (top area).

---

### User Story 2 - Access Character/Coloration Controls in a Dedicated Group (Priority: P2)

A sound designer wants to shape the tonal character of the delay (age, wear, saturation, era) and expects these related parameters to be grouped together in a "Character" section, separate from the time and mix controls.

**Why this priority**: Character controls affect the sonic quality but are adjusted less frequently than primary time/mix controls. Grouping them together helps users understand their purpose.

**Independent Test**: Can be tested by loading Tape, BBD, or Digital modes and verifying character-related controls are grouped under a "CHARACTER" label.

**Acceptance Scenarios**:

1. **Given** the user loads Tape mode, **When** looking at the panel, **Then** Motor Speed, Inertia, Wear, Saturation, and Age controls are grouped under a "CHARACTER" label.
2. **Given** the user loads BBD mode, **When** looking at the panel, **Then** Age and Era controls are grouped under a "CHARACTER" label.
3. **Given** the user loads Digital mode, **When** looking at the panel, **Then** Era, Age, and Limiter Character controls are grouped under a "CHARACTER" label.

---

### User Story 3 - Find Modulation Controls Easily (Priority: P2)

A user wants to add movement to their delay sound using modulation and expects all modulation-related controls (depth, rate, waveform) to be clearly grouped together in a "Modulation" section.

**Why this priority**: Modulation is a common creative parameter set that should be logically grouped for quick access when designing evolving delay textures.

**Independent Test**: Can be tested by loading BBD, Digital, or PingPong modes and verifying modulation controls are grouped under a "MODULATION" label.

**Acceptance Scenarios**:

1. **Given** the user loads BBD mode, **When** looking at the panel, **Then** Mod Depth and Mod Rate are grouped under a "MODULATION" label.
2. **Given** the user loads Digital mode, **When** looking at the panel, **Then** Mod Depth, Mod Rate, and Waveform are grouped under a "MODULATION" label.
3. **Given** the user loads PingPong mode, **When** looking at the panel, **Then** Mod Depth and Mod Rate are grouped under a "MODULATION" label.

---

### User Story 4 - Access Specialized Mode-Specific Controls (Priority: P3)

A power user exploring Granular mode wants to find the spray parameters (pitch spray, position spray, pan spray) in a dedicated "Spray" group, and head controls in Tape mode grouped under "Heads."

**Why this priority**: Specialized controls are used less frequently but should still be logically organized for discoverability.

**Independent Test**: Can be tested by loading specific modes and verifying mode-specific controls are in appropriately named groups.

**Acceptance Scenarios**:

1. **Given** the user loads Granular mode, **When** looking at the panel, **Then** Pitch Spray, Position Spray, Pan Spray, and Reverse % are grouped under "SPRAY & RANDOMIZATION."
2. **Given** the user loads Tape mode, **When** looking at the panel, **Then** Head 1/2/3 enable, level, and pan controls are grouped under "TAPE HEADS."
3. **Given** the user loads Ducking mode, **When** looking at the panel, **Then** Threshold, Duck Amount, Attack, Release, Hold, and Target are grouped under "DUCKER DYNAMICS."

---

### Edge Cases

- What happens when a group has only one control? The group should still display with its label for consistency.
- How does the layout handle very narrow plugin window widths? Groups maintain minimum width; controls within groups scale or maintain fixed sizes.
- What happens if future modes have more controls? The panel height (400px) can accommodate additional rows; groups stack vertically.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Each mode panel MUST organize controls into visually distinct groups with labeled headers.
- **FR-002**: Group headers MUST use a consistent visual style (font, color, positioning) across all modes.
- **FR-003**: Each group MUST be visually separated from adjacent groups using spacing, borders, or background colors.
- **FR-004**: Groups MUST be ordered by frequency of use: primary controls (Time & Mix) at top, specialized controls lower.
- **FR-005**: The "Time & Mix" group MUST appear first in every mode that has delay time, feedback, and dry/wet controls. **Exception**: Modes with a dominant primary function (Freeze, Ducking, MultiTap) may place their primary control group first, with TIME & MIX in the second position.
- **FR-006**: The "Output" group containing Output Gain/Level MUST appear last or in a consistent "footer" position.
- **FR-007**: Controls within each group MUST be logically arranged (e.g., related controls adjacent, enable toggles before their dependent parameters).
- **FR-008**: Group labels MUST be displayed using uppercase text for visual hierarchy (e.g., "TIME & MIX", "CHARACTER").
- **FR-009**: Groups MUST use CViewContainer with distinct background-color to visually separate from the panel background.
- **FR-010**: The implementation MUST use VSTGUI's cross-platform components (CViewContainer, CTextLabel) - no platform-native code.
- **FR-011**: Group spacing MUST be consistent (8-12 pixels between groups).
- **FR-012**: Each mode MUST maintain its existing control functionality - only layout/organization changes.

### Proposed Group Structure by Mode

#### Common Groups (used across multiple modes):

| Group Name | Controls | Used In |
|------------|----------|---------|
| TIME & MIX | Delay Time, Time Mode, Note Value, Feedback, Dry/Wet | All modes |
| MODULATION | Mod Depth, Mod Rate, Waveform | BBD, Digital, PingPong |
| CHARACTER | Age, Era, Saturation, Wear, etc. | Tape, BBD, Digital |
| FILTER | Filter Enable, Type, Cutoff | Shimmer, Reverse, Freeze |
| DIFFUSION | Diffusion Amount, Size | Shimmer, Freeze |
| OUTPUT | Output Gain/Level | All modes |

#### Mode-Specific Groups:

| Mode | Specialized Groups |
|------|-------------------|
| Granular | GRAIN PARAMETERS (Size, Density, Pitch), SPRAY & RANDOMIZATION (Pitch/Pos/Pan Spray, Reverse %), GRAIN OPTIONS (Envelope, Freeze, Time Mode, Note Value) |
| Spectral | SPECTRAL ANALYSIS (FFT Size, Spread, Direction), SPECTRAL CHARACTER (FB Tilt, Diffusion, Freeze) |
| Shimmer | PITCH SHIFT (Semitones, Cents, Shimmer Mix), DIFFUSION, FILTER |
| Tape | CHARACTER (Motor Speed, Inertia, Wear, Saturation, Age), SPLICE (Enable, Intensity), TAPE HEADS (Head 1/2/3 enable, level, pan) |
| BBD | CHARACTER (Age, Era), MODULATION |
| Digital | TIME & SYNC (Delay Time, Time Mode, Note Value), CHARACTER (Era, Age, Limiter), MODULATION, STEREO (Width) |
| PingPong | TIME & SYNC, STEREO (L/R Ratio, Width, Cross FB), MODULATION |
| Reverse | CHUNK (Size, Crossfade, Mode), FILTER |
| MultiTap | PATTERN (Timing, Spatial, Tap Count), TIME (Base Time, Tempo), FEEDBACK FILTERS (LP, HP), MORPHING (Morph Time) |
| Freeze | FREEZE CONTROL (Enable, Delay, Feedback, Decay), PITCH & SHIMMER (Semitones, Cents, Shimmer Mix), DIFFUSION, FILTER |
| Ducking | DUCKER DYNAMICS (Threshold, Amount, Attack, Release, Hold, Target), SIDECHAIN (Filter Enable, Cutoff), DELAY (Time, Feedback) |

### Key Entities

- **Control Group**: A visual container holding related controls with a header label. Contains: name, background color, controls list, position.
- **Mode Panel**: A container for all groups in a specific delay mode. Each mode has a unique arrangement of groups.
- **Group Header**: A text label displayed at the top of each group, styled consistently.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can identify which group a control belongs to within 2 seconds by visual inspection.
- **SC-002**: All 11 mode panels display controls organized into 3-6 labeled groups each.
- **SC-003**: Primary delay controls (Time, Feedback, Mix) are located in the top third of the panel in all modes.
- **SC-004**: 100% of controls have a visible group header identifying their functional category.
- **SC-005**: Group visual styling is consistent across all modes (same font, colors, spacing).
- **SC-006**: The redesigned layout maintains the same panel dimensions (860x400) and does not increase plugin window size.
- **SC-007**: No existing parameter functionality is altered - only visual layout changes.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The existing editor.uidesc XML format will be extended with additional CViewContainer elements for groups.
- VSTGUI's built-in CViewContainer and CTextLabel are sufficient for grouping without custom view code.
- The current 860x400 pixel panel size is adequate for the grouped layout.
- Users are familiar with standard audio plugin UI conventions (groups with headers).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| editor.uidesc | resources/editor.uidesc | Current UI definition - will be heavily modified |
| Mode panel templates | resources/editor.uidesc (GranularPanel, etc.) | Templates to be reorganized into grouped layouts |
| Color definitions | resources/editor.uidesc (colors section) | May need additional group background colors |
| Font definitions | resources/editor.uidesc (fonts section) | May need group-header-font definition |

**Initial codebase search for key terms:**

```bash
grep -r "CRowColumnView" resources/
grep -r "background-color" resources/editor.uidesc
grep -r "section-font" resources/editor.uidesc
```

**Search Results Summary**:
- Current editor.uidesc uses CViewContainer for panels but not CRowColumnView
- Existing colors include: background (#252525), header (#1a1a1a), sidebar (#2d2d2d), panel (#353535), section (#3a3a3a)
- Existing fonts include: section-font (Arial 14 bold) - suitable for group headers
- No existing group/frame styling in use

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- Future UI improvements (preset browser, visualizations) will benefit from established grouping patterns
- Any new delay modes will follow the same group structure conventions

**Potential shared components** (preliminary, refined in plan.md):
- Group container template could be reusable across modes
- Group header style definition will be shared

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is ❌ NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | ⬜ | Pending implementation |
| FR-002 | ⬜ | Pending implementation |
| FR-003 | ⬜ | Pending implementation |
| FR-004 | ⬜ | Pending implementation |
| FR-005 | ⬜ | Pending implementation |
| FR-006 | ⬜ | Pending implementation |
| FR-007 | ⬜ | Pending implementation |
| FR-008 | ⬜ | Pending implementation |
| FR-009 | ⬜ | Pending implementation |
| FR-010 | ⬜ | Pending implementation |
| FR-011 | ⬜ | Pending implementation |
| FR-012 | ⬜ | Pending implementation |
| SC-001 | ⬜ | Pending implementation |
| SC-002 | ⬜ | Pending implementation |
| SC-003 | ⬜ | Pending implementation |
| SC-004 | ⬜ | Pending implementation |
| SC-005 | ⬜ | Pending implementation |
| SC-006 | ⬜ | Pending implementation |
| SC-007 | ⬜ | Pending implementation |

**Status Key:**
- ✅ MET: Requirement fully satisfied with test evidence
- ❌ NOT MET: Requirement not satisfied (spec is NOT complete)
- ⚠️ PARTIAL: Partially met with documented gap
- 🔄 DEFERRED: Explicitly moved to future work with user approval
- ⬜ PENDING: Not yet implemented

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: NOT STARTED

**Recommendation**: Proceed to `/speckit.plan` to design the detailed layout for each mode panel.

---

## Research Notes

### VSTGUI Layout Components Available

Based on research of the VSTGUI library (v4.x):

1. **CViewContainer** - Basic container for grouping views
   - `background-color` - can use distinct color for groups
   - `transparent` - can show underlying panel color
   - Suitable for creating visual group boundaries

2. **CRowColumnView** - Auto-layout container ([VSTGUI Changes](https://steinbergmedia.github.io/vst3_doc/vstgui/html/page_news_and_changes.html))
   - `row-style="true/false"` - arrange views as rows (vertical) or columns (horizontal)
   - `spacing` - pixel gap between child views
   - `margin` - padding inside container
   - `equal-size-layout` - alignment options (left-top, center, stretch, etc.)
   - Excellent for automatically arranging controls within a group

3. **CShadowViewContainer** - Adds drop shadow around children ([VSTGUI View System](https://github.com/steinbergmedia/vstgui/wiki/View-system-overview))
   - `shadow-intensity` - 0-1 opacity
   - `shadow-offset` - x,y offset
   - `shadow-blur-size` - blur radius
   - Could add visual depth to groups

4. **CTextLabel** - For group headers
   - Already using `section-font` (Arial 14 bold)
   - Can style with `font-color="accent"` for visibility

### Audio Plugin UI Best Practices

Based on research from [Pro Audio Files](https://theproaudiofiles.com/whats-in-a-gui/), [Number Analytics](https://www.numberanalytics.com/blog/best-practices-audio-plugin-ui), and [KVR Forum](https://www.kvraudio.com/forum/viewtopic.php?t=541318):

1. **Group related controls visually** - Add spacing to break rigid grids
2. **Flow by workflow** - Order controls by typical adjustment sequence
3. **Adjacent related controls** - Controls adjusted together should be near each other
4. **Reference designs**: FabFilter (intuitive, proportioned), Valhalla (all controls visible, no tabs), Native Instruments Replika XT (good organization with visual feedback)
5. **Avoid tabbed interfaces** - "Having all controls visible on screen at once was a turning point"
