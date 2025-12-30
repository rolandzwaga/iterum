# Research: GUI Layout Redesign

**Feature**: 040-gui-layout-redesign
**Date**: 2025-12-30

## Research Questions

### Q1: What VSTGUI components are available for grouping controls?

**Decision**: Use `CViewContainer` with `CTextLabel` for groups

**Rationale**:
- CViewContainer is the standard VSTGUI container, well-tested and cross-platform
- CTextLabel provides consistent typography for group headers
- These components are already used elsewhere in editor.uidesc
- No custom C++ code required

**Alternatives Considered**:
1. **CRowColumnView** - Auto-layout container with spacing/margin support
   - Pro: Automatic control arrangement
   - Con: Less control over exact positioning
   - Con: May not work well with existing control sizes
   - Decision: Not used initially, but could be evaluated for future versions

2. **CShadowViewContainer** - Adds drop shadow for visual depth
   - Pro: Modern visual appearance
   - Con: Additional visual complexity
   - Con: May not match current flat design
   - Decision: Not used, keep consistent with existing design

3. **Custom CView subclass** - Full control over appearance
   - Pro: Maximum flexibility
   - Con: Requires C++ code
   - Con: Violates "XML-only" approach
   - Decision: Rejected

### Q2: How should groups be visually distinguished?

**Decision**: Use existing `section` background color (#3a3a3a) with `section-font` headers

**Rationale**:
- `section` color already exists and provides appropriate contrast against `panel` (#353535)
- `section-font` (Arial 14 bold) is already defined and styled appropriately
- `accent` color (#4a90d9) for headers matches existing mode title styling
- Minimal changes to color palette, consistent with existing design language

**Alternatives Considered**:
1. **New "group" color (#404040)**
   - Pro: Stronger visual separation
   - Con: Another color to maintain
   - Decision: Could be added if existing `section` doesn't provide enough contrast

2. **Border/outline styling**
   - Pro: Clear group boundaries
   - Con: VSTGUI border support is limited
   - Con: May look dated
   - Decision: Rely on background color contrast instead

### Q3: What is the optimal group ordering per mode?

**Decision**: Order by usage frequency - Time & Mix always first, Output always last

**Rationale**:
- Research from KVR Forum and Pro Audio Files suggests ordering by "typical adjustment sequence"
- Primary controls (time, feedback, mix) are adjusted most frequently → top position
- Output controls are set-and-forget → bottom position
- Mode-specific controls in the middle, grouped by function

**Reference**: [Pro Audio Files - Plugin GUI](https://theproaudiofiles.com/whats-in-a-gui/), [KVR Forum](https://www.kvraudio.com/forum/viewtopic.php?t=541318)

### Q4: How many groups should each mode have?

**Decision**: 3-6 groups per mode, depending on control count

**Rationale**:
- Too few groups (1-2) defeats the purpose of organization
- Too many groups (7+) creates visual clutter
- FabFilter and Valhalla plugins (cited as good examples) use 3-5 major sections
- Each mode has different control counts, so group counts vary accordingly

**Mode Group Counts**:
| Mode | Groups | Control Count |
|------|--------|---------------|
| Granular | 5 | 18 |
| Spectral | 4 | 13 |
| Shimmer | 4 | 14 |
| Tape | 5 | 17 |
| BBD | 4 | 10 |
| Digital | 5 | 15 |
| PingPong | 4 | 13 |
| Reverse | 3 | 10 |
| MultiTap | 5 | 15 |
| Freeze | 5 | 17 |
| Ducking | 4 | 15 |

### Q5: Should CRowColumnView be used for automatic layout?

**Decision**: No - use manual positioning with CViewContainer

**Rationale**:
- Existing controls have specific sizes and positions that work well
- CRowColumnView would require recalculating all control positions
- Manual positioning preserves exact current layout within groups
- CRowColumnView can be evaluated as future enhancement

## Implementation Approach

### Phase 1: Add Group Containers
1. Create nested CViewContainer for each group within mode panels
2. Add CTextLabel for group header
3. Move existing controls into appropriate group containers
4. Adjust control positions to be relative to group container

### Phase 2: Visual Refinement
1. Verify spacing between groups (10px)
2. Ensure consistent group sizing
3. Test in all modes

### Phase 3: Validation
1. Build and test plugin
2. Run pluginval validation
3. Visual verification on all platforms

## Resources

- [VSTGUI UIDescription Editor](https://steinbergmedia.github.io/vst3_doc/vstgui/html/page_uidescription_editor.html)
- [VSTGUI View Container](https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_c_view_container.html)
- [Pro Audio Files - Plugin GUI Design](https://theproaudiofiles.com/whats-in-a-gui/)
- [Number Analytics - Best Practices](https://www.numberanalytics.com/blog/best-practices-audio-plugin-ui)
