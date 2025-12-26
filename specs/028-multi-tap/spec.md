# Feature Specification: Multi-Tap Delay Mode

**Feature Branch**: `028-multi-tap`
**Created**: 2025-12-26
**Status**: Draft
**Layer**: 4 (User Feature)
**Input**: User description: "Multi-Tap Delay Mode - Layer 4 user feature implementing rhythmic multi-tap delay patterns. Composes TapManager (Layer 3) for tap management, FeedbackNetwork for master feedback, and ModulationMatrix for per-tap modulation."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Multi-Tap Rhythmic Delay (Priority: P1)

A producer wants to add rhythmic complexity to a vocal track using multiple delay taps with different timing. They load the Multi-Tap mode and select from a comprehensive library of timing patterns - rhythmic (quarter notes, dotted eighths, triplets), mathematical (golden ratio, fibonacci), or spatial presets (cascading stereo, alternating pan).

**Why this priority**: This is the core value proposition - creating rhythmic delay patterns quickly. Without this, the feature has no purpose.

**Independent Test**: Can be fully tested by loading a pattern, processing audio, and verifying multiple taps produce delays at expected timing intervals with correct levels.

**Acceptance Scenarios**:

1. **Given** Multi-Tap mode is initialized, **When** user selects "DottedEighth" timing pattern with 4 taps, **Then** taps are positioned at dotted-eighth intervals
2. **Given** a timing pattern is loaded, **When** user applies "Cascade" spatial pattern, **Then** taps pan progressively from left to right
3. **Given** user selects "GoldenRatio" pattern, **When** 6 taps are active, **Then** each tap time is 1.618× the previous tap
4. **Given** tempo is set to 120 BPM, **When** user plays audio through the effect, **Then** tap delays align precisely to tempo grid

---

### User Story 2 - Per-Tap Level and Pan Control (Priority: P2)

A sound designer wants fine control over individual tap characteristics. They adjust the level and stereo position of each tap independently to create a custom spatial rhythm that sweeps across the stereo field.

**Why this priority**: Per-tap control differentiates multi-tap from simple echo - essential for creative use but not required for basic operation.

**Independent Test**: Can be tested by setting different level/pan values for each tap and verifying stereo output matches expectations.

**Acceptance Scenarios**:

1. **Given** 4 taps are active, **When** user sets tap 1 to -6dB left, tap 2 to -12dB right, **Then** output reflects these specific level and pan settings
2. **Given** per-tap controls are exposed, **When** user adjusts tap 3 pan from center to hard left, **Then** tap 3 output pans smoothly without clicks

---

### User Story 3 - Master Feedback with Filtering (Priority: P2)

A producer wants the multi-tap delay to build and evolve over time. They increase the master feedback amount and apply a lowpass filter to create a delay that darkens as it repeats, simulating analog tape behavior.

**Why this priority**: Feedback transforms static taps into evolving textures - core to delay sound design but independent of basic tap functionality.

**Independent Test**: Can be tested by setting feedback > 0, playing impulse, and verifying multiple delay generations with progressive filtering.

**Acceptance Scenarios**:

1. **Given** feedback is set to 50%, **When** impulse is processed, **Then** tap pattern repeats with each generation at 50% level of previous
2. **Given** lowpass filter is set to 2kHz in feedback path, **When** feedback loops, **Then** high frequencies are progressively attenuated each generation
3. **Given** feedback is set to 100%+, **When** processing audio, **Then** output remains stable (soft-limited) without runaway oscillation

---

### User Story 4 - Pattern Morphing Between Presets (Priority: P3)

A performer wants to transition smoothly between different rhythm patterns during a live performance. They trigger a morph from "QuarterNote" to "Triplet" pattern and the transition happens smoothly over 500ms without audio artifacts.

**Why this priority**: Smooth transitions enable live use but most users will set-and-forget patterns. Important for polish, not MVP.

**Independent Test**: Can be tested by triggering pattern morph, measuring intermediate tap positions, and verifying smooth interpolation without discontinuities.

**Acceptance Scenarios**:

1. **Given** QuarterNote pattern is active, **When** user triggers morph to Triplet over 500ms, **Then** tap times interpolate smoothly between patterns
2. **Given** morph is in progress, **When** audio plays, **Then** delay output shows no clicks or discontinuities during transition

---

### User Story 5 - Per-Tap Modulation (Priority: P3)

A sound designer wants each tap to have subtle movement. They assign an LFO to modulate tap 3's delay time slightly, creating a chorusing effect on just that one tap while others remain static.

**Why this priority**: Advanced sound design capability - adds depth but requires ModulationMatrix integration which is complex.

**Independent Test**: Can be tested by assigning LFO to specific tap's time parameter and verifying only that tap exhibits time modulation.

**Acceptance Scenarios**:

1. **Given** ModulationMatrix is connected, **When** LFO is routed to tap 2 time at 10% depth, **Then** only tap 2 time varies with LFO while others remain static
2. **Given** multiple modulation routes exist, **When** processing audio, **Then** each tap responds to its assigned modulation independently

---

### User Story 6 - Tempo Sync with Note Values (Priority: P2)

A producer working at 140 BPM wants all taps synchronized to musical time divisions. They enable tempo sync, and the base delay time control switches to note value mode (quarter, eighth, dotted, triplet).

**Why this priority**: Most delay effects require tempo sync for musical use - essential for DAW integration.

**Independent Test**: Can be tested by setting tempo, selecting note value, and verifying delay time in samples matches expected value for that BPM and note.

**Acceptance Scenarios**:

1. **Given** tempo is 120 BPM, **When** base time is set to QuarterNote, **Then** tap 1 delay is exactly 500ms (0.5s = 1 beat at 120 BPM)
2. **Given** tempo changes from 120 to 140 BPM, **When** tempo sync is enabled, **Then** all tap times adjust proportionally to maintain musical relationship

---

### Edge Cases

- What happens when tap count is set to 1? System should still function as a single-tap delay.
- What happens when all taps are muted? Output should be dry signal only (if mix < 100%) or silence (if mix = 100%).
- How does system handle tempo change during playback? Taps should smoothly transition to new times without clicks.
- What happens when feedback exceeds 100%? Soft limiter prevents runaway, output remains stable.
- How does pattern loading work when current tap count differs from pattern's default? Pattern scales to current tap count.

## Requirements *(mandatory)*

### Functional Requirements

#### Core Multi-Tap Processing
- **FR-001**: System MUST support 2 to 16 simultaneously active delay taps
- **FR-002**: System MUST provide rhythmic timing patterns:
  - WholeNote, HalfNote, QuarterNote, EighthNote, SixteenthNote, ThirtySecondNote
  - DottedHalf, DottedQuarter, DottedEighth, DottedSixteenth
  - TripletHalf, TripletQuarter, TripletEighth, TripletSixteenth
- **FR-002a**: System MUST provide mathematical timing patterns:
  - GoldenRatio (each tap = previous × 1.618)
  - Fibonacci (taps follow 1, 1, 2, 3, 5, 8... sequence)
  - Exponential (taps at 1×, 2×, 4×, 8×... base time)
  - PrimeNumbers (taps at 2×, 3×, 5×, 7×, 11×... base time)
  - LinearSpread (equal spacing from min to max time)
- **FR-002b**: System MUST provide spatial/level preset patterns:
  - Cascade (pan sweeps L→R across taps)
  - Alternating (pan alternates L, R, L, R...)
  - Centered (all taps center pan)
  - WideningStereo (pan spreads progressively wider)
  - DecayingLevel (each tap -3dB from previous)
  - FlatLevel (all taps equal level)
- **FR-003**: System MUST support custom user-defined tap timing patterns via `setCustomTimingPattern(std::span<float> timeRatios)` accepting an array of time multipliers relative to base time
- **FR-004**: Each tap MUST have independent time, level, pan, and filter controls
- **FR-005**: System MUST process stereo audio (2-channel input/output)

#### Timing and Sync
- **FR-006**: Base delay time range MUST be 1ms to 5000ms (5 seconds)
- **FR-007**: System MUST support tempo synchronization with host BPM
- **FR-008**: Tempo sync MUST support note values: whole, half, quarter, eighth, sixteenth with dotted and triplet variants
- **FR-009**: Tap times MUST be calculated relative to base time using pattern ratios
- **FR-010**: Time parameter changes MUST be smoothed to prevent clicks (20ms smoothing)

#### Per-Tap Controls
- **FR-011**: Per-tap level range MUST be -96 dB to +6 dB (matching TapManager's kMinLevelDb/kMaxLevelDb)
- **FR-012**: Per-tap pan MUST follow constant-power pan law (-3dB at center)
- **FR-013**: Per-tap filter MUST support bypass, lowpass, and highpass modes
- **FR-014**: Per-tap filter cutoff range MUST be 20 Hz to 20 kHz
- **FR-015**: All per-tap parameters MUST be smoothed to prevent zipper noise

#### Master Feedback
- **FR-016**: Master feedback amount range MUST be 0% to 110%
- **FR-017**: Feedback path MUST include soft limiter to prevent runaway oscillation
- **FR-018**: Feedback path MUST include optional lowpass and highpass filters
- **FR-019**: Feedback filter cutoff ranges MUST be 20 Hz to 20 kHz
- **FR-020**: Feedback MUST route combined tap output back to delay input

#### Modulation
- **FR-021**: System MUST accept ModulationMatrix input for per-tap parameter modulation
- **FR-022**: Modulatable parameters MUST include: time, level, pan, filter cutoff for each tap
- **FR-023**: Modulation MUST be applied additively with base parameter values

#### Pattern System
- **FR-024**: Preset patterns MUST scale to any tap count (2-16)
- **FR-025**: Pattern morphing MUST support smooth transitions between patterns
- **FR-026**: Morph time MUST be configurable (50ms to 2000ms)
- **FR-027**: Pattern MUST define time ratios, default levels, and default pans for each tap position

#### Output
- **FR-028**: Mix control MUST blend dry and wet signals (0% = dry, 100% = wet)
- **FR-029**: Output level control MUST provide +/- 12 dB gain adjustment
- **FR-030**: System MUST provide reset() to clear all delay buffers

### Key Entities

- **MultiTapDelay**: Layer 4 feature class composing TapManager, FeedbackNetwork, ModulationMatrix
- **TimingPattern**: Enumeration of timing patterns (rhythmic note values + mathematical sequences)
- **SpatialPattern**: Enumeration of pan/level distribution patterns (Cascade, Alternating, etc.)
- **TapConfiguration**: Runtime state for a single tap (time, level, pan, filter mode/cutoff, mute)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All timing patterns produce delays at mathematically correct intervals (verified by impulse response test for each of 20+ patterns)
- **SC-002**: Per-tap level accuracy within +/- 0.1 dB of target
- **SC-003**: Per-tap pan accuracy within +/- 1% of target position
- **SC-004**: All spatial patterns produce correct pan distribution (Cascade: linear L→R, Alternating: exact L/R toggle, etc.)
- **SC-005**: Parameter smoothing eliminates audible clicks during any parameter change
- **SC-006**: Feedback stability: 110% feedback produces no output exceeding +6 dBFS over 10 seconds
- **SC-007**: CPU usage remains under 1% per instance at 44.1 kHz stereo (Layer 4 budget)
- **SC-008**: Pattern morphing completes without audible discontinuities
- **SC-009**: Tempo sync accuracy within +/- 1 sample at any BPM (20-300)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- TapManager (spec 023) is fully implemented and tested
- FeedbackNetwork (spec 019) is fully implemented and tested
- ModulationMatrix (spec 020) is fully implemented and tested
- Host provides valid tempo information via VST3 processContext
- Audio buffer sizes are reasonable (32 to 4096 samples)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that will be composed:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| TapManager | src/dsp/systems/tap_manager.h | Primary dependency - provides 16-tap delay management |
| FeedbackNetwork | src/dsp/systems/feedback_network.h | Provides master feedback with filtering and limiting |
| ModulationMatrix | src/dsp/systems/modulation_matrix.h | Provides LFO/envelope routing to parameters |
| TapPattern enum | src/dsp/systems/tap_manager.h | Preset pattern definitions |
| NoteValue enum | src/dsp/core/note_value.h | Tempo sync note values |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Parameter smoothing |
| MultimodeFilter | src/dsp/processors/multimode_filter.h | Per-tap filtering |

**Search Results Summary**: All required Layer 3 components exist and are tested. TapManager provides core tap functionality with 5 timing patterns (QuarterNote, DottedEighth, Triplet, GoldenRatio, Fibonacci) plus flexible `loadNotePattern()` for any NoteValue. This spec extends the pattern system with additional mathematical patterns (Exponential, PrimeNumbers, LinearSpread) and adds spatial/level patterns (Cascade, Alternating, etc.) as a Layer 4 enhancement. FeedbackNetwork provides stable feedback with filtering. ModulationMatrix provides parameter modulation routing.

### Forward Reusability Consideration

**Sibling features at same layer (Layer 4)**:
- Ping-Pong Delay (spec 027) - already uses StereoField and FeedbackNetwork
- Shimmer Mode (spec 4.6) - will use FeedbackNetwork with PitchShifter
- Reverse Delay (spec 4.7) - may share pattern concepts

**Potential shared components**:
- Pattern morphing logic could be extracted to a shared utility if other features need it
- The Layer 4 composition pattern (combining Layer 3 systems) establishes precedent for future features

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
