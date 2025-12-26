# Feature Specification: Shimmer Delay Mode

**Feature Branch**: `029-shimmer-delay`
**Created**: 2025-12-26
**Status**: Draft
**Layer**: 4 (User Feature)
**Input**: User description: "Shimmer Delay Mode - Layer 4 user feature implementing pitch-shifted feedback delay for ambient/ethereal textures. Composes DelayEngine (Layer 3), PitchShiftProcessor (Layer 2), DiffusionNetwork (Layer 2), FeedbackNetwork (Layer 3), and ModulationMatrix (Layer 3). Classic shimmer effect with octave-up pitch shifting in the feedback path, creating evolving cascades of harmonics. User controls: pitch shift amount (±24 semitones), shimmer mix (pitched vs unpitched in feedback), diffusion amount, feedback intensity, delay time with tempo sync, and modulation."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Classic Shimmer Reverb Sound (Priority: P1)

An ambient producer wants to create ethereal, pad-like textures from a simple guitar arpeggio. They load the Shimmer mode, set the pitch shift to +12 semitones (one octave up), and adjust the shimmer mix so the pitched signal dominates the feedback path. The delay repeats build and cascade upward in pitch, creating the classic "shimmer reverb" sound popularized by the Strymon BigSky and Eventide Space.

**Why this priority**: This is the core value proposition - creating pitch-shifted cascading delays is what defines shimmer. Without this, the feature has no purpose.

**Independent Test**: Can be fully tested by setting pitch shift to +12, feedback to 50%+, processing audio, and verifying that delay repeats progressively shift pitch upward while maintaining stability.

**Acceptance Scenarios**:

1. **Given** Shimmer mode is initialized, **When** user sets pitch shift to +12 semitones and feedback to 50%, **Then** each delay repeat is pitched one octave higher than the previous
2. **Given** pitch shift is set to +12 semitones, **When** impulse is processed with shimmer mix at 100%, **Then** successive delays show 2x frequency content relative to previous repeat
3. **Given** pitch shift is set to -12 semitones, **When** audio is processed, **Then** delays cascade downward in pitch (octave down per repeat)
4. **Given** any pitch shift setting, **When** feedback exceeds 100%, **Then** output remains stable (soft-limited) without runaway oscillation

---

### User Story 2 - Diffused Shimmer Texture (Priority: P2)

A sound designer wants a more lush, reverb-like shimmer texture rather than discrete echo taps. They increase the diffusion amount, which smears the delay taps into a continuous wash of sound while maintaining the pitch-shifting behavior. The result is similar to a shimmer reverb (like Valhalla Shimmer).

**Why this priority**: Diffusion transforms clinical pitched delays into rich ambient textures - essential for professional-quality shimmer but builds on the core P1 functionality.

**Independent Test**: Can be tested by setting diffusion to 100%, processing audio, and verifying the output is temporally smeared while still exhibiting pitch shift characteristics.

**Acceptance Scenarios**:

1. **Given** diffusion is set to 100%, **When** impulse is processed, **Then** output shows smeared, reverb-like decay rather than discrete echo taps
2. **Given** diffusion is set to 0%, **When** impulse is processed, **Then** output shows clear, distinct echo taps with pitch shifting
3. **Given** diffusion is enabled, **When** shimmer is processing, **Then** frequency spectrum still shows pitch shifting behavior

---

### User Story 3 - Blend Control Between Pitched and Unpitched Feedback (Priority: P2)

A producer wants subtle shimmer that adds harmonic interest without overwhelming the original delay sound. They use the shimmer mix control to blend between pitched and unpitched content in the feedback path - at 30% shimmer mix, the majority of the delay repeats are normal pitch while a smaller portion cascades upward.

**Why this priority**: The blend control allows shimmer to work as a subtle enhancement rather than only as an extreme effect - critical for versatility but not required for basic shimmer operation.

**Independent Test**: Can be tested by setting shimmer mix to 50%, processing audio, and verifying the feedback path contains both pitched and unpitched signal components.

**Acceptance Scenarios**:

1. **Given** shimmer mix is set to 0%, **When** audio is processed, **Then** no pitch shifting occurs (standard delay behavior)
2. **Given** shimmer mix is set to 100%, **When** audio is processed, **Then** entire feedback path is pitch-shifted
3. **Given** shimmer mix is set to 50%, **When** audio is processed, **Then** output contains equal parts pitched and unpitched delay repeats
4. **Given** shimmer mix changes during playback, **When** audio plays, **Then** transition is smooth without clicks or artifacts

---

### User Story 4 - Tempo-Synced Shimmer Delay (Priority: P2)

A producer working at 120 BPM wants the shimmer delay times synchronized to musical note values. They enable tempo sync and select a dotted-eighth note pattern, creating rhythmic shimmer cascades that lock to the track's groove.

**Why this priority**: Tempo sync is essential for DAW integration and professional music production but builds on the core shimmer functionality.

**Independent Test**: Can be tested by setting tempo, selecting note value, and verifying delay time in samples matches expected value for that BPM.

**Acceptance Scenarios**:

1. **Given** tempo is 120 BPM, **When** delay time is set to quarter note in synced mode, **Then** delay is exactly 500ms
2. **Given** tempo changes from 120 to 140 BPM, **When** tempo sync is enabled, **Then** delay time adjusts proportionally
3. **Given** free mode is selected, **When** user sets delay to 350ms, **Then** delay is exactly 350ms regardless of host tempo

---

### User Story 5 - Modulated Shimmer for Movement (Priority: P3)

A sound designer wants the shimmer delay to have subtle pitch and time modulation for a more organic, "alive" quality reminiscent of tape-based delays. They connect an LFO to modulate the pitch shift slightly and the delay time, creating detuned shimmer with chorusing characteristics.

**Why this priority**: Modulation adds organic movement and character but requires ModulationMatrix integration and is an advanced sound design feature.

**Independent Test**: Can be tested by routing LFO to pitch and time parameters and verifying audible modulation in the shimmer output.

**Acceptance Scenarios**:

1. **Given** LFO is routed to pitch at 5% depth, **When** audio is processed, **Then** pitch shift varies subtly with LFO rate
2. **Given** multiple modulation routes exist, **When** processing, **Then** each parameter responds independently to its assigned modulation
3. **Given** modulation depth is set to 0%, **When** processing, **Then** no modulation artifacts are present

---

### User Story 6 - Pitch Shift Quality Selection (Priority: P3)

An advanced user wants to choose between pitch shifting algorithms based on their latency/quality trade-offs. They select "PhaseVocoder" mode for the highest quality shimmer at the cost of higher latency, suitable for offline rendering or non-real-time use.

**Why this priority**: Advanced users benefit from quality options, but most users will be fine with the default quality mode.

**Independent Test**: Can be tested by switching between pitch modes and verifying different latency values and output quality characteristics.

**Acceptance Scenarios**:

1. **Given** Granular mode is selected, **When** processing, **Then** latency is approximately 46ms with good quality
2. **Given** PhaseVocoder mode is selected, **When** processing, **Then** latency is approximately 116ms with excellent quality
3. **Given** Simple mode is selected, **When** processing, **Then** latency is 0ms with audible artifacts at extreme shifts

---

### Edge Cases

- What happens when pitch shift is set to 0 semitones? System should behave as a normal delay (no pitch change in feedback).
- What happens when feedback exceeds 100% with pitch shifting? Soft limiter in feedback path prevents runaway oscillation.
- How does system handle tempo change during playback? Delay times smoothly transition to new values.
- What happens when diffusion is 100% but shimmer mix is 0%? Result should be diffused delay without pitch shifting.
- What happens when pitch shift quality mode changes during playback? Crossfade between modes to prevent clicks.

## Requirements *(mandatory)*

### Functional Requirements

#### Core Shimmer Processing
- **FR-001**: System MUST route pitch-shifted signal into the feedback path to create harmonic cascades
- **FR-002**: Pitch shift range MUST be ±24 semitones with fine tuning in cents (±100)
- **FR-003**: System MUST provide shimmer mix control (0-100%) blending pitched and unpitched feedback
- **FR-004**: System MUST support stereo audio processing (2-channel input/output)
- **FR-005**: Feedback amount range MUST be 0% to 120% (allowing self-oscillation)

#### Pitch Shifting
- **FR-006**: System MUST use PitchShiftProcessor (Layer 2) for pitch transformation
- **FR-007**: System MUST support all three pitch shift quality modes (Simple, Granular, PhaseVocoder)
- **FR-008**: Default pitch shift mode MUST be Granular (good quality/latency balance)
- **FR-009**: Pitch shift changes MUST be smoothed to prevent clicks (10ms smoothing)

#### Diffusion
- **FR-010**: System MUST integrate DiffusionNetwork (Layer 2) for smeared textures
- **FR-011**: Diffusion amount range MUST be 0% to 100%
- **FR-012**: Diffusion MUST be applied after pitch shifting in the feedback path
- **FR-013**: Diffusion size parameter MUST be adjustable for different smear characteristics

#### Delay
- **FR-014**: Base delay time range MUST be 10ms to 5000ms (5 seconds)
- **FR-015**: System MUST support tempo synchronization via host BPM (TimeMode::Synced)
- **FR-016**: Tempo sync MUST support all NoteValue and NoteModifier combinations
- **FR-017**: Delay time changes MUST be smoothed to prevent clicks (20ms smoothing)

#### Feedback Path
- **FR-018**: System MUST use FeedbackNetwork (Layer 3) for feedback management
- **FR-019**: Feedback path MUST include soft limiting to prevent runaway oscillation
- **FR-020**: Feedback path MUST include optional lowpass filter (via FeedbackNetwork)
- **FR-021**: Feedback filter cutoff ranges MUST be 20 Hz to 20 kHz

#### Modulation
- **FR-022**: System MUST accept ModulationMatrix input for parameter modulation
- **FR-023**: Modulatable parameters MUST include: pitch, shimmer mix, delay time, feedback, diffusion
- **FR-024**: Modulation MUST be applied additively with base parameter values

#### Output
- **FR-025**: Mix control MUST blend dry and wet signals (0% = dry, 100% = wet)
- **FR-026**: Output level control MUST provide ±12 dB gain adjustment
- **FR-027**: System MUST provide reset() to clear all delay buffers and state
- **FR-028**: System MUST report total latency including pitch shifter latency

### Key Entities

- **ShimmerDelay**: Layer 4 feature class composing DelayEngine, PitchShiftProcessor, DiffusionNetwork, FeedbackNetwork, ModulationMatrix. Configuration parameters (pitch, shimmer mix, diffusion, feedback, delay time, etc.) are embedded as member variables with setter methods.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Pitch shift accuracy within ±5 cents of target across ±24 semitone range
- **SC-002**: Shimmer mix at 100% routes 100% of feedback through pitch shifter (verified by frequency analysis)
- **SC-003**: Shimmer mix at 0% produces no pitch shifting in output (standard delay)
- **SC-004**: Parameter smoothing eliminates audible clicks during any parameter change
- **SC-005**: Feedback stability: 120% feedback with +12 semitone shift produces no output exceeding +6 dBFS over 10 seconds
- **SC-006**: CPU usage remains under 1% per instance at 44.1 kHz stereo (Layer 4 budget)
- **SC-007**: Tempo sync accuracy within ±1 sample at any BPM (20-300)
- **SC-008**: Latency reporting accurate within 1 sample for all pitch shift modes
- **SC-009**: All diffusion settings (0-100%) produce audible difference in temporal smearing

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- PitchShiftProcessor (spec 016) is fully implemented and tested
- DiffusionNetwork (spec 015) is fully implemented and tested
- FeedbackNetwork (spec 019) is fully implemented and tested
- DelayEngine (spec 018) is fully implemented and tested
- ModulationMatrix (spec 020) is fully implemented and tested
- Host provides valid tempo information via VST3 processContext
- Audio buffer sizes are reasonable (32 to 4096 samples)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that will be composed:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| PitchShiftProcessor | src/dsp/processors/pitch_shift_processor.h | Core pitch shifting - use as-is |
| DiffusionNetwork | src/dsp/processors/diffusion_network.h | Temporal smearing - use as-is |
| FeedbackNetwork | src/dsp/systems/feedback_network.h | Feedback with filtering/limiting - use as-is |
| DelayEngine | src/dsp/systems/delay_engine.h | Delay with tempo sync - use as-is |
| ModulationMatrix | src/dsp/systems/modulation_matrix.h | LFO/envelope routing - use as-is |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Parameter smoothing |
| BlockContext | src/dsp/core/block_context.h | Tempo and transport info |
| PitchMode | src/dsp/processors/pitch_shift_processor.h | Pitch quality modes |

**Search Results Summary**: All required Layer 2-3 components exist and are tested. PitchShiftProcessor provides three quality modes (Simple, Granular, PhaseVocoder). DiffusionNetwork provides 8-stage allpass diffusion. FeedbackNetwork provides stable feedback with filtering and limiting. DelayEngine provides tempo-synced delay. ModulationMatrix provides parameter modulation routing.

### Forward Reusability Consideration

**Sibling features at same layer (Layer 4)**:
- Reverse Delay (spec 4.7) - may share feedback structure with pitch processing
- Granular Delay (spec 4.8) - may share modulation patterns
- Freeze mode - similar feedback path with 100% feedback

**Potential shared components**:
- The Layer 4 composition pattern (combining Layer 2-3 systems) is established by previous specs
- No new reusable components anticipated - this spec primarily composes existing components

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
