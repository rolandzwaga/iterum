# Feature Specification: Spectral Delay Tempo Sync

**Feature Branch**: `041-spectral-tempo-sync`
**Created**: 2025-12-31
**Status**: Draft
**Input**: User description: "the spectral delay only supports free delay times, no synced ones with note values like the digital delay and others. IMHO, it makes sense to add those there too, and to have the base delay label and control hidden when synced is selected, also like the digital delay UI is working."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Tempo-Synced Base Delay (Priority: P1)

As a music producer working on tempo-based projects, I want the spectral delay's base delay parameter to lock to musical note divisions, so that the spectral echoes consistently align with my song's rhythm regardless of tempo changes.

**Why this priority**: Core functionality - tempo sync is the primary feature being added. Without this working, the feature provides no value. This follows the established pattern already used by Digital Delay, Ping-Pong Delay, and Granular Delay modes.

**Independent Test**: Can be fully tested by setting Time Mode to "Synced", selecting a note value (e.g., 1/4 note), playing audio at different tempos (60, 120, 180 BPM), and verifying the base delay automatically adjusts to match the tempo (500ms at 120 BPM for 1/4 note).

**Acceptance Scenarios**:

1. **Given** Time Mode is set to "Synced" and Note Value is "1/4", **When** host tempo is 120 BPM, **Then** base delay is 500ms (one quarter note duration)
2. **Given** Time Mode is set to "Synced" and Note Value is "1/8", **When** host tempo is 120 BPM, **Then** base delay is 250ms (one eighth note duration)
3. **Given** Time Mode is set to "Synced" and Note Value is "1/4T" (triplet), **When** host tempo is 120 BPM, **Then** base delay is ~333.33ms (quarter note triplet duration)
4. **Given** Time Mode is set to "Synced", **When** host tempo changes from 120 to 60 BPM, **Then** base delay doubles smoothly without clicks or artifacts

---

### User Story 2 - Free Time Mode Preservation (Priority: P1)

As a sound designer, I want to retain the ability to set base delay in milliseconds for experimental and non-musical applications, so I can create frequency-dependent textures without being constrained by musical timing.

**Why this priority**: Essential for backward compatibility and non-musical use cases. Users who don't need tempo sync should be able to use the existing millisecond-based control unchanged.

**Independent Test**: Can be fully tested by setting Time Mode to "Free", adjusting the base delay slider, and verifying the delay is set exactly in milliseconds regardless of host tempo.

**Acceptance Scenarios**:

1. **Given** Time Mode is set to "Free", **When** base delay is set to 350ms, **Then** spectral delay uses exactly 350ms regardless of host tempo
2. **Given** Time Mode is set to "Free" and host tempo changes, **When** base delay is 500ms, **Then** base delay remains at 500ms (tempo-independent)
3. **Given** Time Mode changes from "Synced" to "Free", **When** base delay was 500ms in synced mode, **Then** base delay stays at 500ms in free mode (smooth transition)

---

### User Story 3 - Conditional UI Visibility (Priority: P1)

As a user, I want the Base Delay control hidden when Time Mode is "Synced" and the Note Value dropdown visible, so the interface clearly shows only the relevant control for the current mode, matching the Digital Delay's behavior.

**Why this priority**: Explicit user requirement - the user specifically requested this UI behavior to match Digital Delay. Showing both controls when only one is active creates confusion.

**Independent Test**: Can be tested by toggling Time Mode between "Free" and "Synced" and observing that the Base Delay label/slider appear/disappear while the Note Value dropdown has the opposite behavior.

**Acceptance Scenarios**:

1. **Given** Time Mode is "Free", **When** viewing the Spectral Delay panel, **Then** Base Delay label and slider are visible, Note Value dropdown is hidden
2. **Given** Time Mode is "Synced", **When** viewing the Spectral Delay panel, **Then** Base Delay label and slider are hidden, Note Value dropdown is visible
3. **Given** Time Mode changes from "Free" to "Synced", **When** UI updates, **Then** visibility switches immediately without flicker

---

### User Story 4 - Note Value Selection (Priority: P2)

As a musician, I want to select from standard musical note divisions including triplets, so I can match the spectral delay effect to the rhythmic feel of my music.

**Why this priority**: Enhances musical flexibility but depends on the core tempo sync mechanism (US1) being functional first.

**Independent Test**: Can be tested by cycling through all note value options and verifying each produces the mathematically correct duration at a known tempo.

**Acceptance Scenarios**:

1. **Given** Time Mode is "Synced", **When** Note Value dropdown is opened, **Then** options include: 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1 (10 values matching existing delay modes)
2. **Given** Note Value is "1/8T" (triplet) at 120 BPM, **When** audio is processed, **Then** base delay is ~166.67ms (eighth note triplet duration)
3. **Given** Note Value is "1/2T" (triplet) at 120 BPM, **When** audio is processed, **Then** base delay is ~666.67ms (half note triplet duration)

---

### Edge Cases

- What happens when host tempo is 0 or undefined? System uses fallback tempo of 120 BPM (consistent with other delay modes)
- What happens when calculated delay exceeds maximum buffer size (2000ms)? Delay is clamped to maximum delay time
- What happens when switching Time Mode while audio is playing? Transition is smooth with parameter smoothing (no clicks)
- How does Spread interact with tempo sync? Spread is added to the tempo-synced base delay, resulting in per-bin delays from baseDelay to baseDelay+spread

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST provide a Time Mode selector with "Free" and "Synced" options for Spectral Delay
- **FR-002**: System MUST provide a Note Value selector with 10 options matching existing delay modes: 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1
- **FR-003**: When Time Mode is "Synced", system MUST calculate base delay from host tempo and selected note value
- **FR-004**: When Time Mode is "Free", system MUST use the millisecond-based base delay parameter directly (existing behavior)
- **FR-005**: System MUST smoothly transition between Free and Synced modes without audio artifacts
- **FR-006**: System MUST clamp calculated delay to the valid range (0-2000ms)
- **FR-007**: System MUST use a fallback tempo of 120 BPM when host tempo is unavailable or zero
- **FR-008**: UI MUST hide Base Delay label and slider when Time Mode is "Synced"
- **FR-009**: UI MUST hide Note Value dropdown when Time Mode is "Free"
- **FR-010**: Spread parameter MUST work with both Free and Synced modes (added to base delay)
- **FR-011**: All new parameters MUST be persisted in preset/state save/load

### Key Entities

- **TimeMode**: Enumeration with Free and Synced values, controlling how base delay is determined (reuse existing from delay_engine.h)
- **NoteValue**: Musical note division (matches existing enum in note_value.h)
- **dropdownToDelayMs()**: Existing utility function that converts note value index + tempo to milliseconds

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Tempo-synced base delay is mathematically accurate within 0.1ms of expected value for all note values at any tempo from 20-300 BPM
- **SC-002**: Mode switching completes without audible clicks or discontinuities
- **SC-003**: All existing spectral delay functionality continues to work identically when Time Mode is "Free"
- **SC-004**: UI correctly shows Base Delay control in Free mode and Note Value control in Synced mode
- **SC-005**: Spread works correctly with both Free and Synced modes
- **SC-006**: New parameters (Time Mode, Note Value) are correctly saved and restored from presets

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Host DAW provides valid tempo information via BlockContext
- Existing parameter smoothing infrastructure handles mode transitions
- VisibilityController pattern (used by Digital Delay) works for Spectral Delay panel controls
- VSTGUI control-tags can be added for Spectral Delay label visibility

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| TimeMode enum | dsp/systems/delay_engine.h | Should reuse existing enum for consistency |
| dropdownToDelayMs() | dsp/core/note_value.h | Existing function converts note index + tempo to ms |
| VisibilityController | controller/controller.cpp | Should reuse for UI visibility control |
| Digital delay tempo sync | parameters/digital_params.h | Reference implementation pattern |
| Granular delay tempo sync | parameters/granular_params.h | Recent reference implementation |
| SpectralDelay | dsp/features/spectral_delay.h | Component to extend |
| SpectralParams | parameters/spectral_params.h | Parameter pack to extend |
| BlockContext | dsp/core/block_context.h | Provides tempo information |

**Initial codebase search for key terms:**

```bash
grep -r "TimeMode" src/
grep -r "dropdownToDelayMs" src/
grep -r "VisibilityController" src/
```

**Search Results Summary**: TimeMode enum, dropdownToDelayMs() function, and VisibilityController class already exist and are used by Digital Delay, PingPong Delay, and Granular Delay. These should be reused for consistency.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- Any future delay modes that need tempo sync
- Pattern is already shared by 4 delay modes

**Potential shared components** (preliminary, refined in plan.md):
- The TimeMode/NoteValue parameter pattern is already shared across multiple delay modes
- No new shared components anticipated - this feature follows established patterns exactly

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | 🔄 | Pending implementation |
| FR-002 | 🔄 | Pending implementation |
| FR-003 | 🔄 | Pending implementation |
| FR-004 | 🔄 | Pending implementation |
| FR-005 | 🔄 | Pending implementation |
| FR-006 | 🔄 | Pending implementation |
| FR-007 | 🔄 | Pending implementation |
| FR-008 | 🔄 | Pending implementation |
| FR-009 | 🔄 | Pending implementation |
| FR-010 | 🔄 | Pending implementation |
| FR-011 | 🔄 | Pending implementation |
| SC-001 | 🔄 | Pending implementation |
| SC-002 | 🔄 | Pending implementation |
| SC-003 | 🔄 | Pending implementation |
| SC-004 | 🔄 | Pending implementation |
| SC-005 | 🔄 | Pending implementation |
| SC-006 | 🔄 | Pending implementation |

**Status Key:**
- ✅ MET: Requirement fully satisfied with test evidence
- ❌ NOT MET: Requirement not satisfied (spec is NOT complete)
- ⚠️ PARTIAL: Partially met with documented gap
- 🔄 DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: DRAFT

**If NOT COMPLETE, document gaps:**
- Specification phase - implementation not yet started

**Recommendation**: Proceed to `/speckit.plan` for implementation planning
