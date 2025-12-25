# Feature Specification: BBD Delay

**Feature Branch**: `025-bbd-delay`
**Created**: 2025-12-26
**Status**: Draft
**Input**: User description: "025-bbd-delay - Layer 4 user feature implementing classic bucket-brigade device (BBD) delay emulation. Composes DelayEngine, FeedbackNetwork, CharacterProcessor (BBD mode), and ModulationMatrix. User controls: Time (delay length with bandwidth tracking), Feedback (loop gain), Modulation (triangle LFO depth for chorus effect), Modulation Rate (LFO speed), Age (clock noise, bandwidth reduction), Era (different BBD chip models - MN3005, MN3007, MN3205, SAD1024). Unique behaviors: bandwidth inversely proportional to delay time, compander artifacts (pumping/breathing), clock noise proportional to delay time, limited frequency response (dark character), anti-aliasing filter simulation. Should feel like authentic vintage analog delays (Boss DM-2, Electro-Harmonix Memory Man, Roland Dimension D)."

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic BBD Echo (Priority: P1)

A musician wants warm, dark analog delay echoes with the characteristic limited bandwidth and subtle artifacts of vintage bucket-brigade delays. They set up a simple delay with repeats that naturally darken with each repetition due to the inherent BBD filtering.

**Why this priority**: Core value proposition - without basic BBD delay functionality, the feature has no purpose. This establishes the fundamental delay engine with BBD character.

**Independent Test**: Process audio through BBD delay at default settings; output contains delayed repeats with bandwidth-limited, dark character distinct from clean digital delay.

**Acceptance Scenarios**:

1. **Given** default BBD delay settings, **When** an impulse is processed, **Then** delayed output shows progressive high-frequency rolloff with each repeat
2. **Given** BBD delay with feedback at 50%, **When** a transient signal is processed, **Then** echoes decay naturally with audible darkening
3. **Given** prepared BBD delay, **When** mix is set to 100% wet, **Then** only processed signal is heard

---

### User Story 2 - Modulation/Chorus Effect (Priority: P2)

A guitarist wants the classic "chorus-y" delay sound where a triangle LFO modulates the delay time, creating subtle pitch wobble on the repeats. This is the signature sound of units like the Memory Man and Boss DM-2.

**Why this priority**: Modulation is what distinguishes BBD delays from other delay types. The characteristic wobble is essential to the authentic sound.

**Independent Test**: Enable modulation at 50% depth and verify pitch variation in delayed output.

**Acceptance Scenarios**:

1. **Given** modulation depth set to 50%, **When** audio is processed, **Then** delayed signal shows pitch modulation
2. **Given** modulation rate set to 0.5 Hz, **When** modulation depth is above 0%, **Then** output pitch varies at approximately 0.5 Hz
3. **Given** modulation depth at 0%, **When** audio is processed, **Then** no pitch modulation occurs (clean delay)

---

### User Story 3 - Bandwidth Tracking (Priority: P3)

A producer wants authentic BBD behavior where longer delay times result in more limited bandwidth. Real BBD chips have this characteristic - the clock frequency determines both delay time AND the anti-aliasing filter cutoff, so longer delays sound progressively darker.

**Why this priority**: This is a key differentiator of authentic BBD emulation vs generic "analog-style" delay. Essential for realism.

**Independent Test**: Compare frequency response at 50ms vs 500ms delay times; longer delay should have lower bandwidth.

**Acceptance Scenarios**:

1. **Given** delay time at 50ms (short), **When** white noise is processed, **Then** bandwidth extends to approximately 10-15kHz
2. **Given** delay time at 500ms (long), **When** white noise is processed, **Then** bandwidth is limited to approximately 3-5kHz
3. **Given** delay time changed from 50ms to 500ms, **When** audio is processed, **Then** bandwidth decreases proportionally

---

### User Story 4 - BBD Chip Era Selection (Priority: P4)

A sound designer wants to switch between different BBD chip characteristics to match specific vintage units. Different chips (MN3005, MN3007, MN3205, SAD1024) have different stage counts, noise floors, and distortion characteristics.

**Why this priority**: Provides tonal variety and authentic recreation of specific vintage units. Important for users seeking specific sounds.

**Independent Test**: Switch between Era presets and verify audible differences in character.

**Acceptance Scenarios**:

1. **Given** Era set to MN3005 (high quality), **When** audio is processed, **Then** output has wider bandwidth and lower noise
2. **Given** Era set to MN3007 (dark), **When** audio is processed, **Then** output has more limited bandwidth
3. **Given** Era set to SAD1024 (noisy), **When** audio is processed, **Then** output has more pronounced clock noise

---

### User Story 5 - Age/Degradation Character (Priority: P5)

A lo-fi enthusiast wants to add analog degradation artifacts - clock noise, compander pumping, and additional bandwidth reduction - to create aged, characterful delay sounds.

**Why this priority**: Adds creative options for lo-fi and vintage-accurate sounds. Enhances authenticity for specific use cases.

**Independent Test**: Set Age to 100% and verify increased noise, pumping artifacts, and bandwidth reduction compared to Age at 0%.

**Acceptance Scenarios**:

1. **Given** Age at 0%, **When** audio is processed, **Then** minimal artifacts (clean BBD character)
2. **Given** Age at 100%, **When** audio is processed, **Then** audible clock noise, pumping, and reduced bandwidth
3. **Given** Age at 50%, **When** audio is processed, **Then** moderate degradation between clean and fully aged

---

### User Story 6 - Compander Artifacts (Priority: P6)

An experimental musician wants to emphasize the "pumping" and "breathing" artifacts caused by the compander (compressor/expander) circuits in real BBD delays. These circuits were used to improve signal-to-noise ratio but introduced their own coloration.

**Why this priority**: Authentic detail for users wanting maximum vintage realism. Less critical than core functionality.

**Independent Test**: Enable compander emulation and verify dynamic artifacts on transient material.

**Acceptance Scenarios**:

1. **Given** compander enabled with transient input, **When** audio is processed, **Then** output shows characteristic attack softening and release pumping
2. **Given** compander disabled, **When** same audio is processed, **Then** transients pass through more cleanly
3. **Given** high Age setting, **When** audio with varying dynamics is processed, **Then** pumping artifacts are more pronounced

---

### Edge Cases

- What happens when delay time is set to minimum (20ms)? Bandwidth should be at maximum, approaching clean digital quality
- What happens when delay time is set to maximum (1000ms)? Bandwidth should be severely limited (approximately 2.5kHz per FR-016)
- How does system handle feedback at 100%+? Should self-oscillate in controlled manner with BBD character
- What happens when modulation depth is at 100% with fast rate? Verify no audio artifacts or clicks
- How does modulation interact with bandwidth tracking? LFO-modulated delay time should cause corresponding bandwidth modulation

---

## Requirements *(mandatory)*

### Functional Requirements

#### Delay Time Control (Time)
- **FR-001**: System MUST provide delay time control ("Time") ranging from 20ms to 1000ms
- **FR-002**: Time changes MUST be smoothed to prevent clicks (parameter smoothing)
- **FR-003**: System MUST support tempo-synced time values via BlockContext
- **FR-004**: Default delay time MUST be 300ms

#### Feedback Control
- **FR-005**: System MUST provide feedback control ranging from 0% to 120%
- **FR-006**: Feedback MUST include soft limiting to prevent runaway oscillation at >100%
- **FR-007**: Feedback path MUST apply BBD character processing (not clean feedback)
- **FR-008**: Default feedback MUST be 40%

#### Modulation (LFO)
- **FR-009**: System MUST provide modulation depth control (0% to 100%)
- **FR-010**: System MUST provide modulation rate control (0.1 Hz to 10 Hz)
- **FR-011**: Modulation waveform MUST be triangle (authentic BBD character)
- **FR-012**: Modulation MUST affect delay time, creating pitch variation
- **FR-013**: Default modulation depth MUST be 0% (off), default rate MUST be 0.5 Hz

#### Bandwidth Tracking
- **FR-014**: Bandwidth MUST scale inversely with delay time
- **FR-015**: At minimum delay (20ms), bandwidth MUST be approximately 15kHz
- **FR-016**: At maximum delay (1000ms), bandwidth MUST be approximately 2.5kHz
- **FR-017**: Bandwidth scaling MUST follow realistic BBD clock/sample-rate relationship
- **FR-018**: Anti-aliasing filter simulation MUST use lowpass characteristic

#### Age/Degradation
- **FR-019**: System MUST provide Age control (0% to 100%)
- **FR-020**: Age MUST control clock noise level (0% = minimal, 100% = prominent)
- **FR-021**: Age MUST control additional bandwidth reduction beyond tracking
- **FR-022**: Age MUST control compander artifact intensity
- **FR-023**: Default Age MUST be 20% (slight vintage character)

#### Era (Chip Model)
- **FR-024**: System MUST provide Era selector with at least 4 BBD chip models
- **FR-025**: MN3005 Era MUST provide highest bandwidth (flagship chip - Memory Man)
- **FR-026**: MN3007 Era MUST provide medium-dark character (common in pedals)
- **FR-027**: MN3205 Era MUST provide darker, more limited bandwidth (budget chip)
- **FR-028**: SAD1024 Era MUST provide most noise and limited bandwidth (early chip)
- **FR-029**: Default Era MUST be MN3005

#### Compander Emulation
- **FR-030**: System MUST emulate compander artifacts (attack softening, release pumping)
- **FR-031**: Compander intensity MUST scale with Age parameter
- **FR-032**: Compander MUST affect dynamics processing in feedback path

#### Clock Noise
- **FR-033**: System MUST generate clock noise proportional to delay time
- **FR-034**: Clock noise MUST be higher at longer delay times (lower clock frequencies)
- **FR-035**: Clock noise level MUST be controllable via Age parameter

#### Mix and Output
- **FR-036**: System MUST provide dry/wet mix control (0% to 100%)
- **FR-037**: System MUST provide output level control in dB
- **FR-038**: Default mix MUST be 50%, default output level MUST be 0dB

#### Real-Time Safety
- **FR-039**: All processing methods MUST be noexcept
- **FR-040**: No memory allocation MUST occur in process() path
- **FR-041**: CPU usage MUST remain stable regardless of parameter settings

### Key Entities

- **BBDDelay**: Main Layer 4 feature class composing lower-layer components
- **BBDChipModel**: Enumeration of BBD chip types (MN3005, MN3007, MN3205, SAD1024)
- **BandwidthTracker**: Calculates frequency cutoff from delay time and Era settings

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: BBD delay produces audibly distinct character from clean digital delay (verified by A/B comparison test)
- **SC-002**: Bandwidth at 500ms delay is measurably lower than at 50ms delay (frequency response test)
- **SC-003**: Modulation creates pitch variation within expected range at 100% depth
- **SC-004**: All 4 Era presets produce audibly distinct tonal characteristics
- **SC-005**: Age at 100% produces measurably higher noise floor than Age at 0%
- **SC-006**: Feedback at 100%+ creates self-oscillation without infinite amplitude
- **SC-007**: All parameter changes are click-free (smooth transitions)
- **SC-008**: CPU usage remains below 5% at 44.1kHz stereo
- **SC-009**: Audio processing matches character of reference units (Boss DM-2, Memory Man, Dimension D)
- **SC-010**: All parameters support full automation without artifacts

---

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- CharacterProcessor already supports BBD mode with appropriate APIs
- DelayEngine provides tempo-synced delay with smooth time changes
- FeedbackNetwork provides feedback path with filter/saturation integration
- ModulationMatrix provides LFO routing to parameters
- User has prepared() the processor before calling process()
- Stereo processing uses dual-mono BBD character (independent L/R processing)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| TapeDelay | src/dsp/features/tape_delay.h | Reference architecture - similar Layer 4 pattern |
| CharacterProcessor | src/dsp/systems/character_processor.h | Has BBD mode - primary character source |
| FeedbackNetwork | src/dsp/systems/feedback_network.h | Feedback path with filtering |
| DelayEngine | src/dsp/systems/delay_engine.h | Core delay with tempo sync |
| ModulationMatrix | src/dsp/systems/modulation_matrix.h | LFO routing for modulation |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Parameter smoothing |
| Biquad | src/dsp/primitives/biquad.h | Anti-aliasing filter |
| LFO | src/dsp/primitives/lfo.h | Triangle modulation source |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "BBD" src/
grep -r "CharacterMode" src/
grep -r "setBBD" src/
grep -r "compander" src/
```

**Search Results Summary**: [To be filled during planning phase]

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- TapeDelay (024) - Already implemented, shares composition pattern
- DigitalDelay (future) - Will likely share FeedbackNetwork, mix/output controls
- PingPongDelay (future) - May share feedback and character infrastructure

**Potential shared components** (preliminary, refined in plan.md):
- BBDDelay may share parameter structure pattern with TapeDelay (Time/Feedback/Mix/Output)
- Bandwidth tracking logic might be extractable for future "analog-style" effects
- Compander emulation could be useful for other vintage emulations

---

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| ... | | |

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
