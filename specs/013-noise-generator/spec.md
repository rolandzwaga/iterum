# Feature Specification: NoiseGenerator

**Feature Branch**: `013-noise-generator`
**Created**: 2025-12-23
**Status**: Draft
**Layer**: 2 (DSP Processor)
**Input**: User description: "Implement a NoiseGenerator Layer 2 DSP processor that produces various noise types for analog character and lo-fi effects. Should include: White noise (flat spectrum), Pink noise (-3dB/octave), Tape hiss (filtered, dynamic based on signal level), Vinyl crackle (impulsive random clicks/pops), and Asperity noise (tape head noise varying with signal level). Each noise type should be independently controllable with level/mix. Must be real-time safe with no allocations in process(), composable with other Layer 2 processors for the Character Processor in Layer 3."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - White Noise Generation (Priority: P1)

A DSP developer needs to add flat-spectrum white noise to audio signals for testing, dithering, or as a foundation for other noise types.

**Why this priority**: White noise is the foundational building block. All other noise types (pink, tape hiss) are derived from or built upon white noise generation. This must work first.

**Independent Test**: Can be fully tested by generating noise samples and verifying the spectral characteristics are flat (equal energy per frequency band) across the audible range.

**Acceptance Scenarios**:

1. **Given** a prepared NoiseGenerator, **When** white noise is generated for 1 second at 44.1kHz, **Then** the output contains 44,100 samples with values in the range [-1.0, 1.0]
2. **Given** white noise output, **When** spectral analysis is performed, **Then** energy is approximately equal across frequency bands (within 3dB tolerance)
3. **Given** a level control set to -12dB, **When** white noise is generated, **Then** the peak output is approximately 0.25 (-12dB from unity)

---

### User Story 2 - Pink Noise Generation (Priority: P2)

A DSP developer needs pink noise (-3dB/octave rolloff) for more natural-sounding noise that matches human hearing perception, commonly used in audio testing and subtle ambiance.

**Why this priority**: Pink noise is the second most common noise type after white, and is essential for realistic analog character. It requires white noise as input, so depends on US1.

**Independent Test**: Can be tested by generating pink noise and performing spectral analysis to verify the -3dB/octave slope characteristic.

**Acceptance Scenarios**:

1. **Given** a prepared NoiseGenerator with pink noise selected, **When** noise is generated for 1 second, **Then** output samples are in the range [-1.0, 1.0]
2. **Given** pink noise output, **When** spectral analysis compares energy at 1kHz vs 2kHz, **Then** energy at 2kHz is approximately 3dB lower than at 1kHz
3. **Given** pink noise output over the full audible range, **When** measuring the spectral slope, **Then** the slope is -3dB/octave within 1dB tolerance

---

### User Story 3 - Tape Hiss Generation (Priority: P3)

A DSP developer needs authentic tape hiss that responds dynamically to the audio signal level, becoming more prominent when signal is present (like real tape machines).

**Why this priority**: Tape hiss is essential for analog tape emulation in the Layer 3 Character Processor. It adds authenticity to tape delay modes.

**Independent Test**: Can be tested by providing varying input signal levels and measuring that hiss output modulates accordingly.

**Acceptance Scenarios**:

1. **Given** a prepared NoiseGenerator with tape hiss enabled, **When** the input signal level is 0dB (unity), **Then** tape hiss is generated at the configured level
2. **Given** tape hiss mode with input signal at -40dB, **When** noise is generated, **Then** the hiss level is reduced proportionally (lower signal = less hiss)
3. **Given** tape hiss with no input signal (silence), **When** noise is generated, **Then** a minimal floor noise is still present (idle noise floor)
4. **Given** tape hiss output, **When** spectral analysis is performed, **Then** the spectrum shows characteristic high-frequency emphasis (brighter than pink noise)

---

### User Story 4 - Vinyl Crackle Generation (Priority: P4)

A DSP developer needs vinyl crackle effects with random clicks, pops, and surface noise for lo-fi and vintage character.

**Why this priority**: Vinyl crackle provides distinct lo-fi character separate from tape. It's impulsive rather than continuous, requiring different generation algorithms.

**Independent Test**: Can be tested by generating crackle over time and verifying the presence of impulses with appropriate density and amplitude distribution.

**Acceptance Scenarios**:

1. **Given** a prepared NoiseGenerator with vinyl crackle enabled, **When** crackle is generated for 10 seconds, **Then** random impulses (clicks/pops) occur at the configured density
2. **Given** crackle density set to 1.0 Hz (1 click per second), **When** counting impulses over 10 seconds, **Then** approximately 8-12 distinct click events occur (Poisson distribution variance)
3. **Given** vinyl crackle output, **When** analyzing impulse amplitudes, **Then** amplitudes vary randomly (some loud pops, some quiet clicks)
4. **Given** vinyl crackle with surface noise enabled, **When** analyzing between impulses, **Then** low-level continuous surface noise is present

---

### User Story 5 - Asperity Noise Generation (Priority: P5)

A DSP developer needs asperity noise (tape head contact noise) that varies with signal level, simulating the micro-variations from tape-to-head contact.

**Why this priority**: Asperity noise is an advanced tape authenticity feature. It's subtle but important for high-fidelity tape emulation.

**Independent Test**: Can be tested by varying input signal levels and verifying asperity noise modulates accordingly with appropriate spectral characteristics.

**Acceptance Scenarios**:

1. **Given** a prepared NoiseGenerator with asperity noise enabled, **When** processing audio at varying levels, **Then** asperity noise intensity follows the signal envelope
2. **Given** asperity noise with high input signal, **When** analyzing the output, **Then** noise has broadband character with slight high-frequency emphasis
3. **Given** asperity noise with zero input signal, **When** generating noise, **Then** minimal or no asperity noise is produced (it requires signal)

---

### User Story 6 - Multi-Noise Mixing (Priority: P6)

A DSP developer needs to blend multiple noise types simultaneously with independent level controls for complex character effects.

**Why this priority**: The Character Processor will need to mix multiple noise types (e.g., tape hiss + asperity for full tape character). This enables compositional usage.

**Independent Test**: Can be tested by enabling multiple noise types with different levels and verifying the blended output contains contributions from each.

**Acceptance Scenarios**:

1. **Given** white noise at -20dB and pink noise at -20dB both enabled, **When** generating mixed output, **Then** the result contains spectral characteristics of both noise types
2. **Given** tape hiss at -30dB and vinyl crackle at -24dB, **When** mixing, **Then** continuous hiss is audible with occasional crackle impulses on top
3. **Given** all noise types enabled at varying levels, **When** processing, **Then** CPU usage remains within real-time budget

---

### Edge Cases

- What happens when all noise levels are set to 0 (off)? Output should be silence.
- What happens when noise level is set above 0dB? Output should be allowed (boost) up to a reasonable limit (+12dB).
- How does the system handle the minimum supported sample rate (44.1kHz)? Pink noise filtering coefficients should be correct.
- How does the system handle extremely high sample rates (e.g., 192kHz)? Noise generation should remain efficient.
- What happens if the random seed is the same across instances? Different instances should produce different sequences by default.
- What happens with NaN or Inf input to signal-dependent noise types? Should produce safe output (treat as silence).

## Requirements *(mandatory)*

### Functional Requirements

**Core Noise Generation:**

- **FR-001**: System MUST generate white noise with flat spectral density across the audible frequency range (20Hz-20kHz)
- **FR-002**: System MUST generate pink noise with -3dB/octave spectral rolloff, accurate within 1dB across the audible range
- **FR-003**: System MUST generate tape hiss with characteristic spectral shape (high-frequency emphasis) that modulates with input signal level
- **FR-004**: System MUST generate vinyl crackle with random impulses (clicks/pops) at configurable density
- **FR-005**: System MUST generate asperity noise that modulates with input signal envelope

**Level Control:**

- **FR-006**: Each noise type MUST have an independent level control in the range [-96dB, +12dB]
- **FR-007**: System MUST support mixing multiple noise types simultaneously
- **FR-008**: System MUST provide a master output level control

**Signal-Dependent Behavior:**

- **FR-009**: Tape hiss level MUST modulate based on input signal RMS level (louder signal = more hiss)
- **FR-010**: Asperity noise MUST follow the input signal envelope with configurable sensitivity
- **FR-011**: Signal-dependent noise types MUST provide a floor noise parameter for minimum output when input is silent

**Real-Time Safety:**

- **FR-012**: System MUST NOT allocate memory during process() calls
- **FR-013**: System MUST pre-allocate all required buffers during prepare()
- **FR-014**: System MUST support block-based processing up to 8192 samples per block

**Vinyl Crackle Specifics:**

- **FR-015**: Vinyl crackle density MUST be configurable (impulses per second range: 0.1 to 20 Hz)
- **FR-016**: Vinyl crackle MUST include varying impulse amplitudes (random distribution)
- **FR-017**: Vinyl crackle MUST include optional continuous surface noise between impulses

**Integration:**

- **FR-018**: System MUST provide prepare(sampleRate, maxBlockSize) for initialization
- **FR-019**: System MUST provide reset() to clear internal state and reseed random generators
- **FR-020**: System MUST be composable with other Layer 2 processors

### Key Entities

- **NoiseType**: Enumeration of available noise types (White, Pink, TapeHiss, VinylCrackle, Asperity)
- **NoiseChannel**: Individual noise generator with type, level, and type-specific parameters
- **NoiseGenerator**: Main processor containing multiple noise channels with mixing and output control

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: White noise spectral flatness within 3dB across 20Hz-20kHz when measured over 10 seconds
- **SC-002**: Pink noise slope of -3dB/octave with maximum deviation of 1dB across 20Hz-20kHz
- **SC-003**: All noise types generate output within [-1.0, 1.0] range at unity gain
- **SC-004**: No audible clicks or discontinuities when enabling/disabling noise types during playback
- **SC-005**: Processing 512-sample blocks at 44.1kHz uses less than 0.5% of a single CPU core (Layer 2 budget per Constitution XI)
- **SC-006**: Vinyl crackle produces visually distinct impulses when viewed on a waveform display
- **SC-007**: Signal-dependent noise types (tape hiss, asperity) show clear modulation when input signal varies
- **SC-008**: Multiple noise types mixed simultaneously produce perceptually correct blend

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Input signal for signal-dependent noise types is provided as a separate input buffer
- Sample rates from 44.1kHz to 192kHz are supported
- The random number generator provides adequate quality for audio applications (no audible patterns)
- Pink noise filtering uses Voss-McCartney or similar efficient algorithm suitable for real-time
- Level smoothing is applied to prevent zipper noise when changing levels

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| OnePoleSmoother | src/dsp/primitives/smoother.h | Use for level smoothing to prevent clicks |
| Biquad | src/dsp/primitives/biquad.h | May use for tape hiss spectral shaping |
| EnvelopeFollower | src/dsp/processors/envelope_follower.h | Use for signal-level detection in signal-dependent noise |
| dbToGain/gainToDb | src/dsp/core/db_utils.h | Use for level conversions |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "noise" src/
grep -r "random" src/
grep -r "pink" src/
grep -r "crackle" src/
```

**Search Results Summary**: To be completed during planning phase

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is not met without explicit user approval.*

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
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| FR-017 | | |
| FR-018 | | |
| FR-019 | | |
| FR-020 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |

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
