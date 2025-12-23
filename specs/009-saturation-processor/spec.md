# Feature Specification: Saturation Processor

**Feature Branch**: `009-saturation-processor`
**Created**: 2025-12-23
**Status**: Draft
**Layer**: 2 (DSP Processors)
**Input**: Layer 2 DSP Processor: Saturation Processor - A standalone saturation/waveshaping module that composes Oversampler, waveshaper functions, and DC blocker. Features multiple saturation types (Tape tanh symmetric, Tube polynomial with even harmonics, Transistor hard-knee soft clip, Digital hard clip, Diode soft saturation), input/output gain staging, mix control for parallel processing, and automatic DC blocking after asymmetric saturation. Must be real-time safe with oversampling to prevent aliasing. Will be used by Character Processor in Layer 3 for tape/BBD emulation modes.

---

## Overview

The Saturation Processor is a Layer 2 DSP component that provides analog-style saturation and waveshaping effects. It composes Layer 1 primitives (Oversampler, Biquad for DC blocking) with waveshaping algorithms to deliver warm, musical distortion without aliasing artifacts.

**Key Features**:
- 5 saturation types with distinct harmonic characteristics
- 2x oversampling to prevent aliasing from nonlinear processing
- Automatic DC blocking for asymmetric saturation curves
- Input/output gain staging for optimal drive and level control
- Dry/wet mix for parallel saturation (New York compression style)
- Real-time safe operation with no allocations in process path

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Apply Basic Saturation (Priority: P1)

A DSP developer wants to add warm saturation to an audio signal using the tape saturation algorithm, which produces smooth, musical distortion with odd harmonics.

**Why this priority**: Core functionality - the processor must be able to apply saturation before any other features matter.

**Independent Test**: Feed a sine wave through tape saturation with moderate drive, verify harmonic content is added and output is not clipped.

**Acceptance Scenarios**:

1. **Given** a prepared SaturationProcessor with Tape type, **When** processing a 1kHz sine wave with input gain of 12dB, **Then** output contains measurable odd harmonics (3rd, 5th) above -60dB relative to fundamental.
2. **Given** a prepared SaturationProcessor, **When** processing silence (zeros), **Then** output remains silence (no DC offset, no noise).
3. **Given** a prepared SaturationProcessor with input gain = 0dB (unity), **When** processing low-level audio (-40dBFS), **Then** output is nearly linear (< 1% THD).

---

### User Story 2 - Select Saturation Type (Priority: P1)

A DSP developer wants to choose between different saturation characters to match the desired sonic aesthetic (warm tape, rich tube, aggressive transistor, harsh digital, subtle diode).

**Why this priority**: Type selection is fundamental - each type has distinct harmonic characteristics that define the processor's versatility.

**Independent Test**: Process the same sine wave through each saturation type and verify distinct harmonic profiles via FFT analysis.

**Acceptance Scenarios**:

1. **Given** SaturationProcessor set to Tape type, **When** processing a sine wave, **Then** output shows predominantly odd harmonics (tanh characteristic).
2. **Given** SaturationProcessor set to Tube type, **When** processing a sine wave, **Then** output shows enhanced even harmonics (2nd, 4th) due to asymmetric polynomial.
3. **Given** SaturationProcessor set to Transistor type, **When** processing high-level input, **Then** output shows hard-knee clipping behavior with soft transition.
4. **Given** SaturationProcessor set to Digital type, **When** input exceeds threshold, **Then** output is hard-clipped with no soft knee.
5. **Given** SaturationProcessor set to Diode type, **When** processing a sine wave, **Then** output shows soft compression with subtle asymmetric harmonics.

---

### User Story 3 - Control Input/Output Gain (Priority: P2)

A DSP developer wants to drive the saturation harder (input gain) and compensate for level changes (output gain) to maintain consistent loudness.

**Why this priority**: Gain staging is essential for usable saturation - too little drive = no effect, too much = distorted mess, no makeup = volume drop.

**Independent Test**: Verify input gain increases saturation intensity, output gain scales final level, and both can be smoothly modulated.

**Acceptance Scenarios**:

1. **Given** input gain set to +12dB, **When** processing a -12dBFS sine wave, **Then** saturation algorithm receives 0dBFS signal (pre-drive amplification).
2. **Given** output gain set to -6dB, **When** processing audio, **Then** final output is reduced by 6dB relative to post-saturation level.
3. **Given** input gain changed from 0dB to 12dB during playback, **When** smoothing is active, **Then** transition completes without clicks or pops.

---

### User Story 4 - Blend Dry/Wet Mix (Priority: P2)

A DSP developer wants to blend the saturated signal with the original dry signal for parallel saturation effects, preserving transients while adding warmth.

**Why this priority**: Mix control enables parallel processing which is a common production technique and extends the processor's usefulness.

**Independent Test**: Set mix to 50%, verify output is equal parts dry and processed signal.

**Acceptance Scenarios**:

1. **Given** mix set to 0% (full dry), **When** processing audio, **Then** output equals input signal (complete bypass of saturation).
2. **Given** mix set to 100% (full wet), **When** processing audio, **Then** output equals saturated signal only.
3. **Given** mix set to 50%, **When** processing a sine wave, **Then** output is sum of (0.5 * dry) + (0.5 * wet) with correct phase alignment.
4. **Given** mix changed from 0% to 100% during playback, **When** smoothing is enabled, **Then** transition is click-free.

---

### User Story 5 - Prevent Aliasing with Oversampling (Priority: P1)

A DSP developer needs the saturation to be free of aliasing artifacts, which are audible as harsh, inharmonic frequencies when nonlinear processing creates harmonics above Nyquist.

**Why this priority**: Aliasing prevention is non-negotiable for professional audio - aliased harmonics sound harsh and unmusical.

**Independent Test**: Process a high-frequency sine wave (e.g., 10kHz at 44.1kHz sample rate), verify no aliased frequencies below the fundamental.

**Acceptance Scenarios**:

1. **Given** a SaturationProcessor prepared at 44.1kHz, **When** processing a 10kHz sine with heavy saturation, **Then** no aliased frequencies appear below 10kHz (verify via FFT).
2. **Given** oversampling is active, **When** calling getLatency(), **Then** correct latency in samples is reported for delay compensation.

---

### User Story 6 - Automatic DC Blocking (Priority: P2)

A DSP developer needs asymmetric saturation curves (Tube, Diode) to not introduce DC offset, which would cause headroom loss and speaker damage.

**Why this priority**: DC offset is a serious audio problem that must be automatically handled - users shouldn't need to add external DC blockers.

**Independent Test**: Process audio through Tube (asymmetric) saturation, verify output has no DC offset.

**Acceptance Scenarios**:

1. **Given** SaturationProcessor set to Tube type (asymmetric), **When** processing a sine wave for 1 second, **Then** mean of output samples is < 0.001 (effectively zero DC).
2. **Given** SaturationProcessor set to Tape type (symmetric), **When** processing audio, **Then** DC blocker still runs (consistent behavior) with minimal impact.
3. **Given** DC blocker is active, **When** audio below 20Hz is present, **Then** it is attenuated (highpass behavior around 5-20Hz).

---

### User Story 7 - Real-Time Safety (Priority: P1)

A DSP developer needs the processor to be safe for use in audio callbacks - no memory allocation, no blocking, all methods noexcept.

**Why this priority**: Real-time safety is a constitution-level requirement (Principle II) - violations cause audio glitches and crashes.

**Independent Test**: Code inspection verifies no allocations in process(), all public methods marked noexcept.

**Acceptance Scenarios**:

1. **Given** a prepared SaturationProcessor, **When** calling process(), **Then** no memory allocation occurs (verified by code inspection).
2. **Given** any public method, **When** checking signature, **Then** it is marked noexcept.
3. **Given** prepare() is called, **When** inspecting internal state, **Then** all buffers are pre-allocated.

---

### Edge Cases

- What happens when input contains NaN values? Output 0.0f and continue processing (no crash, documented behavior).
- What happens when input contains infinity? Clip to safe range, continue processing.
- What happens when sample rate changes? Requires re-calling prepare() with new rate.
- What happens with very small input values (denormals)? DC blocker handles gracefully without CPU spikes.
- What happens when drive is set to maximum (24dB)? Heavy saturation but no numerical overflow (output clamped if necessary).

---

## Requirements *(mandatory)*

### Functional Requirements

**Saturation Types**:
- **FR-001**: System MUST provide Tape saturation type using tanh(x) waveshaping curve (symmetric, odd harmonics).
- **FR-002**: System MUST provide Tube saturation type using asymmetric polynomial curve that generates even harmonics.
- **FR-003**: System MUST provide Transistor saturation type using hard-knee soft clipping curve.
- **FR-004**: System MUST provide Digital saturation type using hard clipping (min/max clamping).
- **FR-005**: System MUST provide Diode saturation type using soft asymmetric curve.

**Gain Staging**:
- **FR-006**: System MUST provide input gain control in range [-24dB, +24dB] applied BEFORE saturation.
- **FR-007**: System MUST provide output gain control in range [-24dB, +24dB] applied AFTER saturation and DC blocking.
- **FR-008**: System MUST smooth gain parameter changes to prevent clicks (default 5ms smoothing time).

**Mix Control**:
- **FR-009**: System MUST provide dry/wet mix control in range [0.0, 1.0] (0% to 100%).
- **FR-010**: Mix at 0.0 MUST output dry signal only (saturation bypassed for efficiency).
- **FR-011**: Mix MUST blend dry and wet signals as: output = dry * (1 - mix) + wet * mix.
- **FR-012**: System MUST smooth mix parameter changes to prevent clicks.

**Oversampling**:
- **FR-013**: System MUST oversample by 2x for all saturation processing to prevent aliasing.
- **FR-014**: System MUST use Oversampler primitive from Layer 1 with sufficient alias rejection (> 48dB stopband).
- **FR-015**: System MUST report processing latency via getLatency() method for host delay compensation.

**DC Blocking**:
- **FR-016**: System MUST apply DC blocking filter after saturation stage.
- **FR-017**: DC blocker MUST have cutoff frequency approximately 10Hz (sub-audible).
- **FR-018**: DC blocker MUST use biquad highpass topology for consistency with Layer 1 primitives.

**Processing Interface**:
- **FR-019**: System MUST provide prepare(sampleRate, maxBlockSize) for initialization.
- **FR-020**: System MUST provide process(buffer, numSamples) for block processing.
- **FR-021**: System MUST provide reset() to clear internal state without reallocation.
- **FR-022**: System MUST provide processSample(input) for per-sample processing.

**Real-Time Safety**:
- **FR-023**: All public methods MUST be marked noexcept.
- **FR-024**: process() and processSample() MUST NOT allocate memory.
- **FR-025**: All buffers MUST be pre-allocated in prepare().

**Composition**:
- **FR-026**: System MUST compose Oversampler<2,1> from Layer 1 primitives for oversampling.
- **FR-027**: System MUST use Biquad or one-pole filter from Layer 1 as DC blocker.
- **FR-028**: System MUST use OnePoleSmoother from Layer 1 for parameter smoothing.

---

### Non-Functional Requirements

- **NFR-001**: process() MUST be O(N) where N is number of samples.
- **NFR-002**: Memory footprint MUST be bounded by oversampler buffers + smoothers (no dynamic growth).
- **NFR-003**: CPU usage SHOULD be < 0.5% per instance at 44.1kHz mono (single core).

---

### Key Entities

- **SaturationType**: Enumeration of saturation algorithms (Tape, Tube, Transistor, Digital, Diode).
- **SaturationProcessor**: Main class providing saturation processing with all parameters.

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Tape saturation on 1kHz sine with +12dB input gain produces 3rd harmonic > -40dB relative to fundamental.
- **SC-002**: Tube saturation on 1kHz sine with +12dB input gain produces 2nd harmonic > -50dB relative to fundamental.
- **SC-003**: Processing 10kHz sine at 44.1kHz with heavy drive (+18dB) produces alias rejection > 48dB (no aliased frequencies above noise floor).
- **SC-004**: DC offset after Tube saturation (1 second of 1kHz sine) is < 0.001 (measured as absolute mean of output samples).
- **SC-005**: Parameter changes (input gain, output gain, mix) complete without audible clicks when smoothing is enabled.
- **SC-006**: All public methods pass noexcept verification via static_assert in tests.
- **SC-007**: process() contains no memory allocation calls (verified by code inspection).
- **SC-008**: Mix at 0.5 produces output level within 0.5dB of expected linear blend.

---

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Input audio is normalized to [-1.0, +1.0] range.
- Sample rate is set via prepare() before any processing.
- Users will call prepare() when sample rate changes.
- Mono processing only (stereo handled by instantiating two processors).
- Default smoothing time of 5ms is acceptable for most use cases.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| Oversampler<2,1> | src/dsp/primitives/oversampler.h | Direct reuse for 2x oversampling |
| Biquad | src/dsp/primitives/biquad.h | Use with Highpass type as DC blocker |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Reuse for input/output gain/mix smoothing |
| dbToGain/gainToDb | src/dsp/core/db_utils.h | Reuse for dB to linear conversion |
| MultimodeFilter | src/dsp/processors/multimode_filter.h | Reference for Layer 2 pattern (prepare/process/reset) |

**Initial codebase search for key terms:**

```bash
grep -r "Saturation\|Waveshaper\|tanh\|class.*Drive" src/
```

**Search Results Summary**: MultimodeFilter has applyDrive() with simple tanh saturation (lines 393-407). This new component will be a standalone, comprehensive saturation processor with multiple algorithms and full parameter control.

---

## Out of Scope

- Stereo processing (instantiate two mono processors)
- Modulation inputs (use external modulators)
- Preset management
- GUI/visualization
- Multi-band saturation (future Layer 3 component)
- ADAA (Antiderivative Anti-Aliasing) - may be added in future optimization pass

---

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | ✅ MET | saturateTape() uses std::tanh(x) - [US1] tests verify odd harmonics |
| FR-002 | ✅ MET | saturateTube() uses asymmetric x + 0.3x² - 0.15x³ - [US2] tests verify even harmonics |
| FR-003 | ✅ MET | saturateTransistor() uses threshold-based soft clip - [US2] tests verify hard-knee behavior |
| FR-004 | ✅ MET | saturateDigital() uses std::clamp(-1,1) - [US2] tests verify hard clipping |
| FR-005 | ✅ MET | saturateDiode() uses asymmetric exp/linear curve - [US2] tests verify asymmetric saturation |
| FR-006 | ✅ MET | setInputGain() accepts [-24,+24]dB, applied before saturation - [US3] tests verify |
| FR-007 | ✅ MET | setOutputGain() accepts [-24,+24]dB, applied after saturation - [US3] tests verify |
| FR-008 | ✅ MET | OnePoleSmoother with 5ms smoothing on gains - [US3][SC-005] tests verify no clicks |
| FR-009 | ✅ MET | setMix() accepts [0.0,1.0] range - [US4] tests verify |
| FR-010 | ✅ MET | Mix=0.0 outputs dry signal only - [US4] mix 0% test verifies |
| FR-011 | ✅ MET | Mix blending: dry*(1-mix) + wet*mix - [US4][SC-008] tests verify |
| FR-012 | ✅ MET | OnePoleSmoother with 5ms smoothing on mix - [US4][SC-005] tests verify |
| FR-013 | ✅ MET | Oversampler<2,1> provides 2x oversampling - [US5] tests verify |
| FR-014 | ✅ MET | Oversampler from Layer 1 with >48dB alias rejection - [US5][SC-003] tests verify |
| FR-015 | ✅ MET | getLatency() returns oversampler latency - [US5] test verifies |
| FR-016 | ✅ MET | Biquad highpass DC blocker after saturation - [US6] tests verify |
| FR-017 | ✅ MET | DC blocker at 10Hz cutoff (kDCBlockerCutoffHz) - [US6] tests verify attenuation |
| FR-018 | ✅ MET | Biquad configured as Highpass topology - code inspection |
| FR-019 | ✅ MET | prepare(sampleRate, maxBlockSize) implemented - foundational tests verify |
| FR-020 | ✅ MET | process(buffer, numSamples) implemented - all processing tests use it |
| FR-021 | ✅ MET | reset() clears state without reallocation - foundational tests verify |
| FR-022 | ✅ MET | processSample(input) implemented - [US1] per-sample tests verify |
| FR-023 | ✅ MET | All public methods marked noexcept - [US7][SC-006] static_asserts verify |
| FR-024 | ✅ MET | process()/processSample() no allocations - code inspection confirms |
| FR-025 | ✅ MET | oversampledBuffer_ pre-allocated in prepare() - code inspection |
| FR-026 | ✅ MET | Uses Oversampler<2,1> from Layer 1 - #include and member variable |
| FR-027 | ✅ MET | Uses Biquad from Layer 1 as DC blocker - #include and dcBlocker_ member |
| FR-028 | ✅ MET | Uses OnePoleSmoother from Layer 1 for smoothing - 3 smoother members |
| SC-001 | ✅ MET | Tape +12dB: 3rd harmonic > -40dB - [US1][SC-001] test measures -10dB achieved |
| SC-002 | ✅ MET | Tube +12dB: 2nd harmonic > -50dB - [US2][SC-002] test verifies |
| SC-003 | ✅ MET | 10kHz @ 44.1kHz: alias rejection > 48dB - [US5][SC-003] test verifies |
| SC-004 | ✅ MET | Tube DC offset < 0.001 - [US6][SC-004] test verifies |
| SC-005 | ✅ MET | Parameter changes click-free - [US3][US4][SC-005] tests verify derivative < 0.3 |
| SC-006 | ✅ MET | All public methods noexcept - [US7][SC-006] static_asserts compile |
| SC-007 | ✅ MET | No allocations in process() - code inspection, all buffers pre-allocated |
| SC-008 | ✅ MET | Mix 0.5 within 0.5dB of expected - [US4][SC-008] test verifies |

**Status Key:**
- ✅ MET: Requirement fully satisfied with test evidence
- ❌ NOT MET: Requirement not satisfied (spec is NOT complete)
- ⚠️ PARTIAL: Partially met with documented gap
- 🔄 DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [x] All FR-xxx requirements verified against implementation
- [x] All SC-xxx success criteria measured and documented
- [x] No test thresholds relaxed from spec requirements
- [x] No placeholder values or TODO comments in new code
- [x] No features quietly removed from scope
- [x] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

All 28 functional requirements (FR-001 to FR-028) and 8 success criteria (SC-001 to SC-008) have been met with test evidence. The implementation provides:

- 5 saturation types with distinct harmonic characteristics
- 2x oversampling with >48dB alias rejection
- 10Hz DC blocking via Biquad highpass
- Input/output gain staging [-24dB, +24dB]
- Dry/wet mix control [0.0, 1.0]
- 5ms parameter smoothing for click-free modulation
- Full real-time safety (noexcept, no allocations in process)

**Test Summary**: 28 test cases, 5162 assertions, all passing.

**Files Created/Modified**:
- `src/dsp/processors/saturation_processor.h` - Main implementation (424 lines)
- `tests/unit/processors/saturation_processor_test.cpp` - Comprehensive tests (1030+ lines)
- `tests/CMakeLists.txt` - Added test file to build
