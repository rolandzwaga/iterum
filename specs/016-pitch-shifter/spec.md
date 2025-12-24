# Feature Specification: Pitch Shift Processor

**Feature Branch**: `016-pitch-shifter`
**Created**: 2025-12-24
**Status**: Draft
**Input**: "Implement PitchShiftProcessor - a Layer 2 DSP processor that shifts audio pitch by semitones without changing playback speed. Compose from Layer 1 primitives (DelayLine, FFT/STFT for quality modes). Support three quality modes: Simple (delay modulation, zero latency, audible artifacts), Granular (low latency, good quality, general use), and Phase Vocoder (high latency, excellent quality, quality-critical). Pitch range: -24 to +24 semitones with fine cents control. Include formant preservation option for vocal processing. Real-time safe with smoothed parameter changes. Will be used in feedback paths for Shimmer delay mode."

## Overview

The Pitch Shift Processor is a Layer 2 DSP component that transposes audio pitch without affecting playback duration. It provides multiple quality/latency trade-off options to suit different use cases - from zero-latency monitoring to high-quality offline processing. This completes Layer 2 and enables the Shimmer delay mode in Layer 4.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Pitch Shifting (Priority: P1)

A sound designer needs to shift the pitch of audio material by musical intervals (semitones) while maintaining the original tempo and duration. They want predictable, artifact-free results for common transpositions within one octave.

**Why this priority**: Core functionality - without basic pitch shifting, the processor has no value. This is the foundation all other features build upon.

**Independent Test**: Can be fully tested by feeding a sine wave at known frequency, shifting by N semitones, and verifying output frequency matches the expected transposition ratio (2^(N/12)).

**Acceptance Scenarios**:

1. **Given** a 440Hz sine wave input, **When** pitch is shifted by +12 semitones (one octave up), **Then** output frequency is 880Hz (within pitch accuracy tolerance)
2. **Given** a 440Hz sine wave input, **When** pitch is shifted by -12 semitones (one octave down), **Then** output frequency is 220Hz (within pitch accuracy tolerance)
3. **Given** any audio input, **When** pitch is set to 0 semitones (unity), **Then** output matches input with no audible artifacts
4. **Given** audio input, **When** pitch shift changes during playback, **Then** transition is smooth without clicks or discontinuities

---

### User Story 2 - Quality Mode Selection (Priority: P1)

A producer needs different quality/latency trade-offs depending on their workflow. During live performance they need zero latency even with some artifacts. During mixing they prefer higher quality with acceptable latency.

**Why this priority**: Quality modes are essential for practical use - the processor must work in both real-time and quality-critical contexts.

**Independent Test**: Can be tested by measuring latency in each mode and verifying artifacts are within acceptable thresholds for each mode's stated characteristics.

**Acceptance Scenarios**:

1. **Given** Simple mode is selected, **When** processing audio, **Then** output latency is zero samples (pass-through timing)
2. **Given** Granular mode is selected, **When** processing audio, **Then** output latency is less than 50ms at 44.1kHz
3. **Given** Phase Vocoder mode is selected, **When** processing audio, **Then** pitch accuracy is highest among all modes
4. **Given** any mode, **When** mode is changed during processing, **Then** audio transitions smoothly without clicks

---

### User Story 3 - Fine Pitch Control with Cents (Priority: P2)

A musician needs precise pitch control for tuning corrections or creating subtle detuning effects. They need cent-level precision (1/100th of a semitone) for fine adjustments.

**Why this priority**: Enhances precision but core transposition works without cent-level control. Important for professional use cases.

**Independent Test**: Can be tested by verifying 50 cents produces exactly half-semitone shift (frequency ratio of 2^(0.5/12)).

**Acceptance Scenarios**:

1. **Given** pitch set to 0 semitones and +50 cents, **When** processing 440Hz sine, **Then** output is approximately 452.9Hz (quarter tone up)
2. **Given** pitch set to +1 semitone and -50 cents, **When** processing, **Then** result equals +0.5 semitones total
3. **Given** cents parameter, **When** adjusted in real-time, **Then** changes apply smoothly without glitches

---

### User Story 4 - Formant Preservation for Vocals (Priority: P2)

A vocal producer needs to shift the pitch of a vocal recording without the "chipmunk" or "monster" effect that occurs when formants shift with pitch. The result should sound like the same singer at a different pitch.

**Why this priority**: Critical for vocal processing but requires additional complexity. Not needed for instrument transposition.

**Independent Test**: Can be tested by comparing formant frequencies before and after pitch shift - formant peaks should remain at similar frequencies while fundamental shifts.

**Acceptance Scenarios**:

1. **Given** formant preservation is enabled, **When** pitch is shifted up, **Then** formant frequencies remain within 10% of original positions
2. **Given** formant preservation is disabled, **When** pitch is shifted up, **Then** formants shift proportionally with pitch (classic effect)
3. **Given** formant preservation toggle, **When** changed during playback, **Then** transition is smooth

---

### User Story 5 - Feedback Path Integration (Shimmer) (Priority: P2)

A delay plugin developer needs the pitch shifter to work cleanly in a feedback loop for creating "shimmer" effects where each delay repetition is shifted in pitch, creating cascading harmonics.

**Why this priority**: Key integration requirement for Layer 4 Shimmer mode. Must handle iterative processing gracefully.

**Independent Test**: Can be tested by routing output back to input with 50% feedback and verifying stable operation without runaway artifacts.

**Acceptance Scenarios**:

1. **Given** pitch shifter in feedback loop with 80% feedback, **When** processing impulse, **Then** output decays naturally without instability
2. **Given** multiple passes through shifter (simulating feedback), **When** pitch is +12 semitones, **Then** each iteration maintains pitch accuracy (no cumulative drift)
3. **Given** feedback path usage, **When** processing for extended duration, **Then** no DC offset or energy buildup occurs

---

### User Story 6 - Real-Time Parameter Automation (Priority: P3)

A performer needs to automate pitch shift parameters during a live performance or for creative effects like pitch sweeps and wobbles.

**Why this priority**: Enhances creative possibilities but basic functionality works without automation.

**Independent Test**: Can be tested by sweeping pitch from -12 to +12 over 1 second and verifying smooth, click-free transition.

**Acceptance Scenarios**:

1. **Given** pitch parameter changing continuously, **When** sweep from -24 to +24 semitones, **Then** output is smooth without stepping artifacts
2. **Given** rapid parameter changes (>100 changes/second), **When** automating pitch, **Then** no clicks, pops, or discontinuities occur
3. **Given** parameter smoothing, **When** target changes, **Then** value reaches target within 50ms

---

### Edge Cases

- What happens when pitch is set to extreme values (±24 semitones)?
  - Processor continues to function without crash or instability
  - Output may have audible artifacts at extreme shifts (acceptable)
- How does the system handle pitch shift on silence or very quiet signals?
  - No noise amplification or DC offset introduced
- What happens when sample rate changes while processing?
  - Requires re-calling prepare() with new sample rate
- How does the processor handle NaN or infinity input values?
  - Output silence (0.0f) for invalid input samples (FR-023)
- What happens when switching quality modes during active processing?
  - Brief crossfade transition, no clicks (FR-009)
- How does formant preservation behave at extreme pitch shifts (>1 octave)?
  - **Acceptance criteria**: Formant preservation gracefully degrades at shifts >12 semitones
  - The 10% tolerance (SC-007) applies to shifts within ±12 semitones
  - At extreme shifts (>1 octave), formant preservation makes best-effort attempt but may exceed 10% tolerance
  - No crash, no NaN output, processor remains stable

## Requirements *(mandatory)*

### Functional Requirements

#### Core Processing
- **FR-001**: Processor MUST shift pitch by semitones within range -24 to +24 (4 octaves total)
- **FR-002**: Processor MUST support fine-tuning via cents parameter (-100 to +100 cents)
- **FR-003**: Processor MUST combine semitone and cent values for total pitch shift
- **FR-004**: Processor MUST preserve audio duration (time-stretch ratio = 1.0)
- **FR-005**: Processor MUST maintain unity gain (no volume change from pitch shifting)

#### Quality Modes
- **FR-006**: Processor MUST provide Simple mode with zero processing latency
- **FR-007**: Processor MUST provide Granular mode with latency under 50ms
- **FR-008**: Processor MUST provide Phase Vocoder mode with highest pitch accuracy
- **FR-009**: Processor MUST allow mode switching without audio discontinuity
- **FR-010**: Simple mode MUST use delay-line modulation technique
- **FR-011**: Granular mode MUST use overlapping grain windows
- **FR-012**: Phase Vocoder mode MUST use STFT with phase coherence

#### Formant Preservation
- **FR-013**: Processor MUST provide formant preservation toggle (on/off)
- **FR-014**: When enabled, formant frequencies MUST remain within 10% of original
- **FR-015**: Formant preservation MUST be available in Granular and Phase Vocoder modes
- **FR-016**: Formant preservation MAY be unavailable in Simple mode (document limitation)

#### Parameter Control
- **FR-017**: All parameter changes MUST be smoothed to prevent clicks
- **FR-018**: Pitch parameter MUST support real-time automation
- **FR-019**: Processor MUST provide getters for all parameter values
- **FR-020**: Parameters MUST be clamped to valid ranges

#### Real-Time Safety
- **FR-021**: Processor MUST NOT allocate memory during process() calls
- **FR-022**: Processor MUST NOT use blocking operations in audio path
- **FR-023**: Processor MUST handle NaN/infinity inputs gracefully (output silence)
- **FR-024**: Processor MUST be re-entrant safe (no static mutable state)

#### Lifecycle
- **FR-025**: Processor MUST provide prepare(sampleRate, maxBlockSize) method
- **FR-026**: Processor MUST provide reset() method to clear internal state
- **FR-027**: Processor MUST support sample rates from 44100Hz to 192000Hz
- **FR-028**: Processor MUST work correctly after reset() without re-calling prepare()

#### Integration
- **FR-029**: Processor MUST support in-place processing (input buffer == output buffer)
- **FR-030**: Processor MUST be stable in feedback configurations
- **FR-031**: Processor MUST provide latency reporting per mode

### Key Entities

- **PitchShiftProcessor**: Main processor class managing quality modes and parameters
- **PitchMode**: Enumeration of quality modes (Simple, Granular, PhaseVocoder)
- **GrainWindow**: Overlapping audio grain for granular mode
- **PhaseVocoderState**: STFT bins with phase tracking for phase vocoder mode

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Pitch accuracy within ±10 cents for Simple mode, ±5 cents for Granular/PhaseVocoder modes
- **SC-002**: Simple mode latency is exactly 0 samples
- **SC-003**: Granular mode latency is less than 2048 samples at any sample rate (~46ms at 44.1kHz)
- **SC-004**: Phase Vocoder mode latency is less than 8192 samples (~186ms at 44.1kHz)
- **SC-005**: CPU usage below 2% per instance in Simple mode, below 5% in Granular, below 10% in Phase Vocoder (measured at 44.1kHz stereo)
- **SC-006**: Parameter changes produce no audible clicks when swept continuously
- **SC-007**: Formant preservation maintains formant peaks within 10% of original frequencies
- **SC-008**: Processor remains stable after 1000 iterations in feedback loop at 80% feedback

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Layer 1 primitives (DelayLine, FFT/STFT, LFO, Smoother) are available and working
- Input audio is properly normalized (peak values within -1.0 to +1.0)
- Host provides accurate sample rate and block size information
- Mono processing is sufficient (stereo handled by dual instances or stereo wrapper)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| DelayLine | src/dsp/primitives/delay_line.h | Core component for Simple mode delay modulation |
| FFT | src/dsp/primitives/fft.h | Required for Phase Vocoder mode |
| STFT | src/dsp/primitives/stft.h | Required for Phase Vocoder mode |
| SpectralBuffer | src/dsp/primitives/spectral_buffer.h | Phase manipulation storage |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Parameter smoothing |
| WindowFunctions | src/dsp/core/window_functions.h | Grain windowing (Hann, etc.) |
| LFO | src/dsp/primitives/lfo.h | Potential modulation source |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "PitchShift" src/
grep -r "Pitch" src/dsp/
grep -r "Granular" src/
grep -r "PhaseVocoder" src/
grep -r "Formant" src/
```

**Search Results Summary**: To be filled during planning phase

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 to FR-031 | | |
| SC-001 to SC-008 | | |

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
