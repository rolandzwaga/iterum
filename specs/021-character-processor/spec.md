# Feature Specification: Character Processor

**Feature Branch**: `021-character-processor`
**Created**: 2025-12-25
**Status**: Draft
**Input**: User description: "Character Processor - Layer 3 system component that applies analog character/coloration to audio. Core features: Tape mode (saturation, wow/flutter, hiss, EQ rolloff), BBD mode (soft saturation, clock noise, bandwidth limiting), Digital Vintage mode (bit reduction, sample rate reduction), Clean mode (bypass/minimal). Composes from existing Layer 2 processors: SaturationProcessor, NoiseGenerator, MultimodeFilter, and Layer 1 LFO for modulation. Per-mode parameter controls. Real-time safe with smooth mode transitions."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Apply Tape Character (Priority: P1)

A producer wants to add vintage tape warmth to their delay repeats. They select Tape mode and the audio immediately gains the characteristic saturation, subtle wow/flutter modulation, and gentle high-frequency rolloff that evokes classic tape delays.

**Why this priority**: Tape mode is the most commonly requested analog character. It serves as the reference implementation for the character processor architecture.

**Independent Test**: Can be fully tested by processing a sine wave through Tape mode and verifying: (1) harmonic distortion is added, (2) amplitude modulation from wow/flutter is present, (3) high frequencies are attenuated.

**Acceptance Scenarios**:

1. **Given** a CharacterProcessor in Tape mode with default settings, **When** processing a 1kHz sine wave, **Then** the output contains added harmonics (THD > 0.1%) characteristic of tape saturation
2. **Given** a CharacterProcessor in Tape mode with wow/flutter enabled, **When** processing steady-state audio, **Then** the output exhibits pitch/amplitude variation at the configured wow/flutter rate (0.5-5Hz)
3. **Given** a CharacterProcessor in Tape mode, **When** processing white noise, **Then** high frequencies above the tape EQ rolloff point are attenuated by at least 6dB at 10kHz

---

### User Story 2 - Apply BBD Character (Priority: P1)

A user creating a vintage chorus or flanger effect wants authentic bucket-brigade device coloration. They select BBD mode and the audio takes on the characteristic bandwidth-limited, slightly noisy quality of analog BBD chips.

**Why this priority**: BBD mode is equally important for vintage chorus/flanger sounds and represents a distinct character from tape.

**Independent Test**: Can be tested by processing audio through BBD mode and verifying bandwidth limiting and clock noise are applied.

**Acceptance Scenarios**:

1. **Given** a CharacterProcessor in BBD mode, **When** processing white noise, **Then** the output bandwidth is limited (significant rolloff above configurable cutoff, default ~8kHz)
2. **Given** a CharacterProcessor in BBD mode with clock noise enabled, **When** processing silence, **Then** subtle clock noise artifacts are audible in the output
3. **Given** a CharacterProcessor in BBD mode with saturation, **When** processing a loud signal, **Then** soft clipping occurs characteristic of BBD input stages

---

### User Story 3 - Apply Digital Vintage Character (Priority: P2)

A producer wants the lo-fi sound of early digital delays. They select Digital Vintage mode and hear the quantization noise and aliasing artifacts characteristic of 8-12 bit converters and lower sample rates.

**Why this priority**: Digital vintage is a distinct aesthetic that requires bit reduction and sample rate reduction, which are different processing than analog modes.

**Independent Test**: Can be tested by processing audio through Digital Vintage mode and measuring bit depth reduction and aliasing artifacts.

**Acceptance Scenarios**:

1. **Given** a CharacterProcessor in Digital Vintage mode with bit depth set to 8, **When** processing a sine wave, **Then** quantization noise is audible and measurable (SNR reduced to ~48dB)
2. **Given** a CharacterProcessor in Digital Vintage mode with sample rate reduction factor of 4x, **When** processing a 10kHz sine wave at 44.1kHz, **Then** aliasing artifacts appear in the output spectrum
3. **Given** a CharacterProcessor in Digital Vintage mode, **When** reducing bit depth from 16 to 8 bits, **Then** quantization noise increases proportionally (SNR decreases from ~96dB to ~48dB)

---

### User Story 4 - Clean/Bypass Mode (Priority: P2)

A user wants to compare their delay sound with and without character processing. They select Clean mode and the audio passes through with minimal or no coloration.

**Why this priority**: Essential for A/B comparison and when pristine digital delay is desired.

**Independent Test**: Can be tested by processing a signal through Clean mode and verifying output matches input within tolerance.

**Acceptance Scenarios**:

1. **Given** a CharacterProcessor in Clean mode, **When** processing any audio, **Then** the output is identical to input within 0.001dB
2. **Given** a CharacterProcessor switching from Tape to Clean mode, **When** the transition occurs, **Then** no clicks or pops are produced (smooth fade)

---

### User Story 5 - Smooth Mode Transitions (Priority: P3)

A performer changes character modes during a live performance. The transitions between modes must be smooth and click-free to avoid jarring audio artifacts.

**Why this priority**: Real-time mode switching is important for live use but builds on the core mode implementations.

**Independent Test**: Can be tested by switching modes while processing audio and verifying no discontinuities occur.

**Acceptance Scenarios**:

1. **Given** a CharacterProcessor in any mode, **When** mode is changed to any other mode, **Then** the transition completes within 50ms with no audible clicks
2. **Given** rapid mode switching (10 times per second), **When** audio is processing, **Then** no clicks, pops, or zipper noise occurs

---

### User Story 6 - Per-Mode Parameter Control (Priority: P3)

A sound designer wants to fine-tune the character of each mode. Each mode provides relevant parameters (e.g., Tape: saturation amount, wow/flutter depth, hiss level, rolloff frequency; BBD: bandwidth, clock noise, input saturation).

**Why this priority**: Parameter control enables creative customization but the default settings should work well out of the box.

**Independent Test**: Can be tested by adjusting each parameter and verifying the corresponding audio characteristic changes.

**Acceptance Scenarios**:

1. **Given** Tape mode, **When** saturation amount is increased from 0% to 100%, **Then** THD increases proportionally from ~0.1% to ~5%
2. **Given** Tape mode, **When** wow/flutter depth is increased, **Then** pitch modulation depth increases proportionally
3. **Given** BBD mode, **When** bandwidth parameter is reduced, **Then** high-frequency cutoff decreases accordingly
4. **Given** Digital Vintage mode, **When** bit depth parameter is reduced, **Then** quantization noise increases

---

### Edge Cases

- What happens when switching modes during a loud transient? Crossfade should prevent clicks.
- How does the system handle very low sample rates (e.g., 22.05kHz)? All modes must remain functional.
- What happens when saturation amount is set to 0? Should behave like clean passthrough for that stage.
- How does Digital Vintage mode handle sample rate reduction at high host sample rates (192kHz)? Should downsample, process, upsample correctly.
- What happens when NaN values enter the processor? Should be handled gracefully (treat as silence).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: CharacterProcessor MUST provide four distinct character modes: Tape, BBD, DigitalVintage, and Clean
- **FR-002**: CharacterProcessor MUST provide `setMode(mode)` to select the active character mode
- **FR-003**: Mode transitions MUST be smooth and click-free using crossfading (50ms default)
- **FR-004**: CharacterProcessor MUST provide `prepare(sampleRate, maxBlockSize)` initialization
- **FR-005**: CharacterProcessor MUST provide `process(buffer, numSamples)` for mono and stereo processing
- **FR-006**: CharacterProcessor MUST provide `reset()` to clear internal state without reallocation
- **FR-007**: Tape mode MUST apply saturation with configurable amount (0-100%)
- **FR-008**: Tape mode MUST apply wow/flutter modulation with configurable rate (0.1-10Hz) and depth (0-100%)
- **FR-009**: Tape mode MUST add tape hiss noise with configurable level (-144dB to -40dB)
- **FR-010**: Tape mode MUST apply high-frequency rolloff with configurable frequency (2kHz-20kHz)
- **FR-011**: BBD mode MUST apply bandwidth limiting with configurable cutoff (2kHz-15kHz)
- **FR-012**: BBD mode MUST add clock noise with configurable level (-144dB to -50dB)
- **FR-013**: BBD mode MUST apply soft saturation at input stage with configurable drive (0-100%)
- **FR-014**: DigitalVintage mode MUST apply bit depth reduction (4-16 bits)
- **FR-015**: DigitalVintage mode MUST apply sample rate reduction (factor 1x-8x of host rate)
- **FR-016**: DigitalVintage mode MUST add quantization dither with configurable amount
- **FR-017**: Clean mode MUST pass audio through with no processing (unity gain, no additional latency beyond mode crossfade)
- **FR-018**: All parameter changes MUST be smoothed to prevent zipper noise (20ms default)
- **FR-019**: Process path MUST NOT allocate memory (real-time safe)
- **FR-020**: NaN input values MUST be treated as 0.0

### Key Entities

- **CharacterMode**: Enumeration of available modes (Tape, BBD, DigitalVintage, Clean)
- **CharacterProcessor**: The main class managing mode selection and processing
- **TapeCharacter**: Internal component for tape-style processing
- **BBDCharacter**: Internal component for BBD-style processing
- **DigitalVintageCharacter**: Internal component for lo-fi digital processing

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Each character mode produces perceptually distinct audio characteristics (verified by spectral analysis)
- **SC-002**: Mode transitions complete within 50ms with no audible artifacts
- **SC-003**: Processing a 512-sample block at 44.1kHz completes in <1% CPU per instance
- **SC-004**: All parameter changes are glitch-free when automated at 100Hz rate
- **SC-005**: Tape mode THD is controllable from 0.1% to 5% via saturation parameter
- **SC-006**: BBD bandwidth limiting achieves at least -12dB attenuation at 2x the cutoff frequency
- **SC-007**: Digital Vintage 8-bit mode achieves SNR of approximately 48dB (±3dB)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- CharacterProcessor is used as part of a delay feedback loop or as an insert effect
- Host provides valid sample rate via prepare() before any audio processing
- Mode changes happen between process() calls or are handled with crossfading
- Default parameter values provide musically useful results without adjustment
- All internal components (SaturationProcessor, NoiseGenerator, etc.) are already implemented and tested

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| SaturationProcessor | src/dsp/processors/saturation_processor.h | Provides tape/tube saturation curves - reuse directly |
| NoiseGenerator | src/dsp/processors/noise_generator.h | Provides hiss, crackle, noise types - reuse for tape hiss and BBD clock noise |
| MultimodeFilter | src/dsp/processors/multimode_filter.h | Provides LP/HP/BP filtering - reuse for EQ rolloff and bandwidth limiting |
| LFO | src/dsp/primitives/lfo.h | Provides modulation waveforms - reuse for wow/flutter |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Parameter smoothing - reuse for all parameters |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "CharacterProcessor" src/
grep -r "TapeMode" src/
grep -r "BBDMode" src/
grep -r "BitCrush" src/
```

**Search Results Summary**: No existing CharacterProcessor, TapeMode, BBDMode, or BitCrush classes found. These are new types to be created. The component will compose from existing Layer 1-2 processors.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

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
