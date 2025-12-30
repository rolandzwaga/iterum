# Feature Specification: Mode Switch Click-Free Transitions

**Feature Branch**: `041-mode-switch-clicks`
**Created**: 2025-12-30
**Status**: Draft
**Input**: User description: "when I switch between modes and there's still audio in the buffer, there's quite audible clicks and crackles, this ought to be investigated and fixed"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Click-Free Mode Switching During Playback (Priority: P1)

As a music producer using Iterum during a mixing session, I want to switch between delay modes (e.g., from Tape to Granular) while audio is playing without hearing any clicks, pops, or crackles, so that I can audition different delay characters without disrupting my workflow or damaging my ears/speakers.

**Why this priority**: This is the core issue. Audible artifacts during mode switching break the creative flow and can be jarring at high volumes. This is a fundamental usability problem that affects every user who experiments with different delay modes.

**Independent Test**: Can be tested by playing continuous audio through the plugin, switching modes, and verifying no audible artifacts occur.

**Acceptance Scenarios**:

1. **Given** audio is playing through Iterum in Tape mode with delay buffer containing audio, **When** user switches to Granular mode, **Then** the transition produces no audible clicks, pops, or crackles
2. **Given** audio is playing through Iterum with feedback at 50%+ creating audible delay trails, **When** user rapidly switches between any two modes, **Then** the audio transitions smoothly without discontinuities
3. **Given** the delay buffer contains a loud transient (drum hit), **When** user switches modes at that exact moment, **Then** no click or pop is audible in the output

---

### User Story 2 - Preserve Audio Continuity During Mode Switch (Priority: P2)

As a sound designer performing live, I want the delay tail to fade out naturally when switching modes rather than cutting off abruptly, so that my performance sounds professional and intentional.

**Why this priority**: While P1 addresses the harmful clicks, this addresses the aesthetic quality of the transition. A graceful crossfade or fade-out is preferred over an abrupt silence, but is less critical than eliminating clicks.

**Independent Test**: Can be tested by creating a long delay tail, switching modes, and verifying the tail fades rather than cuts.

**Acceptance Scenarios**:

1. **Given** Iterum is producing a long delay tail in Digital mode, **When** user switches to Shimmer mode, **Then** the Digital delay tail fades out smoothly over a short period
2. **Given** freeze mode has captured sustained audio, **When** user switches to any other mode, **Then** the frozen audio fades out rather than cutting abruptly

---

### Edge Cases

- What happens when switching modes during the exact sample where a delay tap outputs audio?
- How does the system handle rapid mode switching (multiple switches within milliseconds)?
- What happens when switching modes while feedback is at 100% (infinite)?
- How does switching affect modes with vastly different buffer structures (e.g., MultiTap with 8 taps vs simple Digital)?
- What happens when switching from a mode with tempo sync enabled to one without?

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Mode switching MUST NOT produce audible clicks, pops, or crackles in the audio output
- **FR-002**: Mode switching MUST apply a crossfade or fade-out to prevent discontinuities from buffer content differences
- **FR-003**: The fade duration MUST be short enough to feel responsive (under 50ms perceived latency)
- **FR-004**: Mode switching MUST be safe to perform at any point during audio processing (sample-accurate safety)
- **FR-005**: The wet signal path MUST be smoothly transitioned; dry signal MUST remain unaffected
- **FR-006**: Rapid successive mode switches (within fade period) MUST NOT cause cumulative artifacts
- **FR-007**: Mode switching MUST properly handle the case where source and destination modes have different buffer sizes/structures
- **FR-008**: All 11 delay modes MUST support click-free transitions to/from any other mode

### Technical Context (For Investigation)

The click artifacts likely stem from one or more of these causes:
- **Buffer discontinuity**: Different modes may have different delay line contents; switching causes a sudden jump in output values
- **State discontinuity**: Filter states, feedback paths, or modulation phases may be in different states between modes
- **Sample-level discontinuity**: If mode switch happens mid-sample-block, output may have a step change

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Zero audible clicks detectable when switching between any two modes during continuous audio playback (verified by listening test)
- **SC-002**: Mode transition completes in under 50ms (user perceives near-instantaneous response)
- **SC-003**: Audio RMS level does not spike more than 3dB above the pre-switch level during transition
- **SC-004**: All 110 mode-to-mode combinations (11×10) pass click-free transition test
- **SC-005**: Rapid mode switching (10 switches per second) produces no cumulative artifacts or instability

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The issue is reproducible on all platforms (Windows, macOS, Linux)
- The issue occurs with default plugin settings (not edge-case parameter combinations)
- The host DAW is providing continuous audio (not paused/stopped)
- Sample rates from 44.1kHz to 192kHz should all be addressed

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| OnePoleSmoother | src/dsp/primitives/smoother.h | May be used for crossfade envelope |
| LinearRamp | src/dsp/primitives/smoother.h | Alternative for linear crossfade |
| Mode parameter handling | src/processor/processor.cpp | Where mode switch currently happens |
| DelayEngine | src/dsp/systems/delay_engine.h | Each mode's delay processing |
| FeedbackNetwork | src/dsp/systems/feedback_network.h | Feedback path that may need fading |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "setMode\|modeChanged\|switchMode" src/
grep -r "crossfade\|fade" src/dsp/
grep -r "kModeId" src/
```

**Search Results Summary**: To be completed during planning phase.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- Any future "preset morphing" feature would need similar crossfade logic
- Parameter smoothing for other discontinuous parameters

**Potential shared components** (preliminary, refined in plan.md):
- A general "CrossfadeManager" or similar could be reused for other abrupt transitions
- Fade envelope generation might be useful for other features

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is ❌ NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |

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

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
