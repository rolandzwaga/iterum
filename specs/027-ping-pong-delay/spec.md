# Feature Specification: Ping-Pong Delay Mode

**Feature Branch**: `027-ping-pong-delay`
**Created**: 2025-12-26
**Status**: Draft
**Input**: User description: "Ping-Pong Delay Mode - Layer 4 user feature implementing classic stereo ping-pong delay with alternating left/right bounces. Composes DelayEngine (x2 for L/R), StereoField for width control, and FeedbackNetwork for cross-channel feedback. Features include: independent L/R delay times with ratio control (1:1, 2:1, 3:2, etc.), cross-feedback amount between channels, stereo width from mono to ultra-wide, tempo sync support, and optional modulation. Controls: Time (base delay length), L/R Ratio (timing relationship), Feedback (with cross-feed), Width (stereo spread), Sync (tempo lock), Modulation depth/rate, Mix, and Output Level. Should provide classic ping-pong effects as well as creative asymmetric stereo delays."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Classic Ping-Pong Delay (Priority: P1)

A musician wants the classic ping-pong delay effect where their mono guitar signal bounces alternately between left and right speakers, creating spatial movement and depth. Each repeat should clearly alternate sides, with the signal starting on one side and bouncing to the opposite side with each subsequent repeat.

**Why this priority**: Core functionality - this is the defining characteristic of a ping-pong delay. Without clean alternating L/R behavior, the plugin cannot claim to be a ping-pong delay.

**Independent Test**: Can be fully tested by processing a mono impulse and verifying each repeat appears on alternating channels with correct timing.

**Acceptance Scenarios**:

1. **Given** a mono input signal and 1:1 ratio, **When** processed with 500ms delay time, **Then** the first repeat appears in left channel at 500ms, second repeat in right channel at 1000ms, and so on alternating.

2. **Given** 50% feedback, **When** an impulse is processed, **Then** each repeat alternates channels and decays by approximately 6dB per bounce.

3. **Given** 100% wet mix, **When** a mono signal is processed, **Then** the output contains only the alternating delayed signal with no dry signal present.

---

### User Story 2 - Asymmetric Stereo Timing (Priority: P2)

A producer wants creative asymmetric delays where the left and right channels have different timing relationships. Using ratios like 2:1 or 3:2, they can create polyrhythmic delays or emphasize certain beats differently in each channel, adding rhythmic complexity to their mix.

**Why this priority**: Primary creative feature after basic ping-pong. Ratio control distinguishes this from simple stereo delays.

**Independent Test**: Can be tested by measuring the timing relationship between left and right channel delays against the specified ratio.

**Acceptance Scenarios**:

1. **Given** 2:1 ratio with 500ms base time, **When** audio is processed, **Then** left channel delay is 500ms and right channel delay is 250ms.

2. **Given** 3:2 ratio with 600ms base time, **When** audio is processed, **Then** left channel is 600ms and right channel is 400ms.

3. **Given** 1:2 ratio (inverse), **When** audio is processed, **Then** left channel is shorter than right channel.

4. **Given** any ratio, **When** feedback is applied, **Then** cross-feedback creates the alternating ping-pong pattern at the respective timing intervals.

---

### User Story 3 - Tempo-Synced Ping-Pong (Priority: P2)

A musician working in a DAW wants their ping-pong delays locked to the song tempo. When the tempo changes, both channels should update their delay times while maintaining the selected L/R ratio, keeping the rhythmic pattern musically coherent.

**Why this priority**: Essential for professional music production. Tempo sync is expected in modern delay plugins.

**Independent Test**: Can be tested by changing host tempo and verifying both channel delay times update correctly while maintaining the ratio.

**Acceptance Scenarios**:

1. **Given** tempo sync enabled with quarter note at 120 BPM and 1:1 ratio, **When** host tempo is read, **Then** both L and R delay times are exactly 500ms.

2. **Given** tempo sync with dotted eighth at 120 BPM and 2:1 ratio, **When** delay is calculated, **Then** left is 375ms and right is 187.5ms.

3. **Given** tempo sync enabled, **When** host tempo changes from 120 to 140 BPM, **Then** both channel delays update smoothly without clicks or pops.

---

### User Story 4 - Stereo Width Control (Priority: P3)

A mixing engineer wants to control the stereo spread of the ping-pong effect. Sometimes they want a subtle mono-ish delay that sits in the center, other times they want an exaggerated ultra-wide effect that extends beyond the speakers. The width control should allow the full range from mono collapse to enhanced stereo.

**Why this priority**: Enhancement feature for mix flexibility. Width control adds professional polish.

**Independent Test**: Can be tested by measuring the correlation coefficient of the stereo output at different width settings.

**Acceptance Scenarios**:

1. **Given** width at 0%, **When** a stereo signal is processed, **Then** the output is mono (L and R channels identical).

2. **Given** width at 100%, **When** a stereo signal is processed, **Then** the output maintains natural stereo separation.

3. **Given** width at 150% (ultra-wide), **When** a mono signal is processed, **Then** the ping-pong effect extends beyond natural panning with enhanced stereo image.

4. **Given** any width setting, **When** feedback is high, **Then** the width remains consistent across all repeats.

---

### User Story 5 - Cross-Feedback Control (Priority: P3)

A sound designer wants independent control over how much signal crosses between channels in the feedback loop. Low cross-feedback creates parallel delays with occasional crossover, while high cross-feedback creates the classic interleaved ping-pong pattern. This control enables everything from dual mono delays to full ping-pong.

**Why this priority**: Creative control feature. Allows users to dial in exactly how "ping-pongy" the effect is.

**Independent Test**: Can be tested by measuring channel isolation at different cross-feedback settings.

**Acceptance Scenarios**:

1. **Given** cross-feedback at 0%, **When** a mono-left signal is processed, **Then** all repeats remain in the left channel only (dual mono behavior).

2. **Given** cross-feedback at 100%, **When** a mono-left signal is processed, **Then** repeats fully alternate between left and right channels.

3. **Given** cross-feedback at 50%, **When** a mono signal is processed, **Then** repeats partially cross channels, creating a hybrid pattern.

---

### User Story 6 - Modulated Ping-Pong (Priority: P4)

A guitarist wants to add subtle modulation to their ping-pong delays for a more organic, chorus-like quality. The modulation should add gentle pitch variation to the repeats, making them feel less static and more alive without overwhelming the core ping-pong effect.

**Why this priority**: Optional enhancement. Modulation adds dimension but is not essential to ping-pong functionality.

**Independent Test**: Can be tested by measuring pitch deviation on sustained notes with modulation enabled.

**Acceptance Scenarios**:

1. **Given** modulation depth at 0%, **When** a sustained note is processed, **Then** there is zero pitch variation in the repeats.

2. **Given** modulation depth at 50% and rate at 0.5Hz, **When** a sustained note is processed, **Then** smooth pitch wobble is audible on the repeats.

3. **Given** modulation applied, **When** ping-pong pattern is active, **Then** both channels are modulated independently.

---

### Edge Cases

- What happens when delay time is set to minimum (1ms)? Should produce clean comb filtering with alternating channels.
- What happens when delay time is at maximum (10 seconds)? Should work without buffer issues.
- How does system handle sudden tempo changes in sync mode? Should crossfade smoothly.
- What happens when feedback exceeds 100%? Limiter should engage without harsh clipping.
- What happens when switching ratios during playback? Should transition smoothly without pops.
- How does mono input behave vs stereo input? Mono should create classic ping-pong, stereo should process channels independently then cross-feed.
- What happens when width is set to extreme values during high feedback? Should remain stable.

## Requirements *(mandatory)*

### Functional Requirements

#### Core Ping-Pong Engine

- **FR-001**: System MUST provide delay times from 1ms to 10,000ms (10 seconds) per channel
- **FR-002**: System MUST support both free-running (milliseconds) and tempo-synced time modes
- **FR-003**: Tempo sync MUST support note values: 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1/1, with dotted and triplet modifiers
- **FR-004**: System MUST provide smooth delay time transitions without clicks or pops

#### L/R Ratio Control

- **FR-005**: System MUST provide L/R timing ratios: 1:1, 2:1, 3:2, 4:3, 1:2, 2:3, 3:4
- **FR-006**: Ratio MUST apply to the base delay time to calculate individual channel delays
- **FR-007**: Left channel delay = base time * left ratio component / max(left, right)
- **FR-008**: Right channel delay = base time * right ratio component / max(left, right)

#### Cross-Feedback System

- **FR-009**: System MUST provide cross-feedback control from 0% to 100%
- **FR-010**: At 0% cross-feedback, channels operate as independent parallel delays
- **FR-011**: At 100% cross-feedback, signal fully alternates between channels (classic ping-pong)
- **FR-012**: Feedback path MUST include limiting to prevent runaway oscillation
- **FR-013**: Feedback amount control from 0% to 120%

#### Stereo Width

- **FR-014**: System MUST provide stereo width control from 0% to 200%
- **FR-015**: Width at 0% MUST collapse output to mono
- **FR-016**: Width at 100% MUST maintain natural stereo separation
- **FR-017**: Width above 100% MUST enhance stereo image beyond natural panning
- **FR-018**: Width control MUST apply consistently across all feedback iterations

#### Modulation

- **FR-019**: System MUST provide optional LFO modulation of delay time
- **FR-020**: Modulation depth MUST be adjustable from 0% to 100%
- **FR-021**: Modulation rate MUST be adjustable from 0.1Hz to 10Hz
- **FR-022**: At 0% depth, there MUST be zero pitch variation
- **FR-023**: Left and right channels MUST be modulated independently (phase offset)

#### Mix and Output

- **FR-024**: System MUST provide dry/wet mix control from 0% to 100%
- **FR-025**: System MUST provide output level control in dB (-inf to +12dB)
- **FR-026**: All parameter changes MUST be smoothed to prevent zipper noise (20ms smoothing)
- **FR-027**: Mix at 0% MUST pass dry signal unaffected

#### Processing Modes

- **FR-028**: System MUST support stereo input processing
- **FR-029**: System MUST support mono input (distribute to both channels before processing)
- **FR-030**: Stereo input MUST maintain channel identity through the delay path
- **FR-031**: Cross-feedback MUST blend channels according to cross-feedback amount

#### Real-Time Safety

- **FR-032**: Processing MUST NOT allocate memory during audio callback
- **FR-033**: All processing functions MUST be noexcept
- **FR-034**: Processing MUST complete within real-time constraints

### Key Entities

- **PingPongDelay**: Main processor class composing Layer 3 components
- **LRRatio**: Enumeration of supported timing ratios (OneToOne, TwoToOne, ThreeToTwo, etc.)
- **TimeMode**: Reuse existing enum from DelayEngine (Free, Synced)
- **ChannelConfig**: Mono or Stereo input mode

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Classic ping-pong mode produces alternating L/R repeats with correct timing within 1 sample accuracy
- **SC-002**: All L/R ratios produce correct timing relationships within 1% tolerance
- **SC-003**: Tempo sync accuracy within 1 sample of calculated note value
- **SC-004**: Stereo width at 0% produces output with >0.99 correlation coefficient (mono)
- **SC-005**: Stereo width at 200% produces output with <0.5 correlation coefficient (wide)
- **SC-006**: Cross-feedback at 0% maintains channel isolation >60dB
- **SC-007**: Cross-feedback at 100% produces fully alternating pattern
- **SC-008**: Parameter changes produce no audible zipper noise
- **SC-009**: Feedback at 120% with limiter engaged produces stable, non-clipping output
- **SC-010**: Processing completes within real-time constraints at 44.1kHz stereo

### Perceptual Criteria (Manual Testing)

- **SC-011**: Classic ping-pong effect sounds like a well-known hardware ping-pong delay
- **SC-012**: Asymmetric ratios create interesting polyrhythmic patterns
- **SC-013**: Width control provides useful range from intimate to expansive

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Sample rate is provided by host and ranges from 44.1kHz to 192kHz
- Maximum block size is 8192 samples
- Host provides tempo and transport information via BlockContext
- Users understand the concept of L/R ratios for timing relationships
- Mono input is a common use case for ping-pong effects

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| DelayLine | src/dsp/primitives/delay_line.h | Core delay - 2 instances for independent L/R timing (per research.md) |
| stereoCrossBlend | src/dsp/core/stereo_utils.h | Cross-feedback blending between channels |
| FeedbackNetwork | src/dsp/systems/feedback_network.h | Reference for cross-feedback pattern |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Parameter smoothing |
| LFO | src/dsp/primitives/lfo.h | Modulation source |
| BlockContext | src/dsp/core/block_context.h | Tempo sync support |
| NoteValue | src/dsp/core/note_value.h | Note value calculations |
| dbToGain | src/dsp/core/db_utils.h | Level conversions |
| DynamicsProcessor | src/dsp/processors/dynamics_processor.h | Feedback limiting for >100% |
| DigitalDelay | src/dsp/features/digital_delay.h | Reference implementation pattern |

**Architecture Decisions (from research.md):**
- Use 2x DelayLine directly (not DelayEngine) for independent L/R delay times
- Use M/S (Mid-Side) technique for width control (not StereoField composition)
- Use stereoCrossBlend() for cross-feedback routing

**Initial codebase search for key terms:**

```bash
grep -r "PingPong" src/
grep -r "cross.*feedback" src/
grep -r "StereoField" src/
```

**Search Results Summary**: No existing PingPong implementation. StereoField exists for width control. Cross-feedback pattern exists in FeedbackNetwork.

### Forward Reusability Consideration

**Sibling features at same layer:**
- Multi-Tap Delay (will share delay engine patterns)
- Shimmer Delay (will share feedback network patterns)
- Ducking Delay (may share cross-channel routing)

**Potential shared components:**
- L/R ratio calculation could be extracted if Multi-Tap needs similar timing ratios
- Cross-feedback blending pattern may inform other stereo effects

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| SC-001 | | |
| SC-002 | | |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

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
