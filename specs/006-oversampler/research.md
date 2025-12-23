# Research: Oversampler

**Feature**: 006-oversampler | **Date**: 2025-12-23 | **Spec**: [spec.md](spec.md)

## Design Decisions

### D1: Anti-Aliasing Filter Architecture

**Question**: What filter topology should be used for anti-aliasing?

**Decision**: Dual-mode architecture:
- **IIR Mode** (ZeroLatency): Use `BiquadCascade<4>` (8-pole, 48 dB/oct Butterworth lowpass) for upsampling and downsampling. Zero latency, lower quality.
- **FIR Mode** (LinearPhase): Use halfband FIR filters for polyphase implementation. Higher quality, introduces latency.

**Rationale**:
- IIR filters provide zero latency but have phase distortion and moderate stopband rejection
- FIR filters provide linear phase and superior stopband rejection but introduce latency
- Real-time audio often needs zero-latency option for monitoring/live use
- Mastering applications need linear-phase option for transparency
- 8-pole Butterworth (~48 dB/oct) provides adequate rejection for 2x oversampling

**References**:
- Robert Bristow-Johnson's Audio EQ Cookbook (biquad formulas)
- Julius O. Smith's "Introduction to Digital Filters" (polyphase decimation)
- Existing `BiquadCascade` in `dsp/primitives/biquad.h`

### D2: Oversampling Factor Implementation

**Question**: How should 4x oversampling be implemented?

**Decision**: Cascade two 2x stages rather than single 4x stage.

**Rationale**:
- Cascading 2x stages is more efficient than single 4x stage
- Each 2x stage only needs to reject from Fs/2 to Fs (narrower transition band)
- Single 4x would need to reject from Fs/4 to Fs (much steeper filter required)
- Example: 44.1kHz base rate
  - Stage 1: 44.1kHz → 88.2kHz (filter rejects 22-44kHz)
  - Stage 2: 88.2kHz → 176.4kHz (filter rejects 44-88kHz)
- CPU cost of two simpler filters < one complex filter

### D3: Halfband FIR Filter Design

**Question**: What FIR filter design should be used for linear-phase mode?

**Decision**: Halfband filters with Kaiser window design.

**Rationale**:
- Halfband filters have coefficients that are zero at every other tap (except center)
- This reduces computation by ~50% compared to general FIR
- Kaiser window provides good tradeoff between transition bandwidth and stopband rejection
- Filter lengths by quality level:
  - Economy: 15-tap halfband (~48 dB stopband)
  - Standard: 31-tap halfband (~80 dB stopband)
  - High: 63-tap halfband (~100+ dB stopband)
- Latency = (filter_length - 1) / 2 samples at oversampled rate

### D4: Buffer Management

**Question**: How should internal buffers be managed for real-time safety?

**Decision**: Pre-allocate maximum-size buffers in `prepare()`.

**Rationale**:
- Maximum block size is 8192 samples (configurable)
- At 4x oversampling, internal buffer needs 8192 * 4 = 32768 samples
- Stereo requires 2x this = 65536 samples total
- Pre-allocate in `prepare()` based on maxBlockSize parameter
- No reallocation during `process()` - strictly forbidden by Constitution Principle II
- Use `std::vector::reserve()` in `prepare()`, never grow during processing

### D5: Stereo Processing Strategy

**Question**: Should stereo be processed as interleaved or split channels?

**Decision**: Split (non-interleaved) channel processing with per-channel filter state.

**Rationale**:
- VST3 provides non-interleaved buffers by default
- Maintaining separate filter state per channel prevents crosstalk
- IIR filters (BiquadCascade) already operate on single channels
- FIR filters can be applied independently per channel
- Template parameter for channel count (1 or 2) avoids runtime branching

### D6: Quality Level Specifications

**Question**: What are the precise specifications for each quality level?

**Decision**:

| Quality | Passband | Stopband | Filter Type | Latency (2x) | Latency (4x) |
|---------|----------|----------|-------------|--------------|--------------|
| Economy | -0.5dB @ 18kHz | -48dB | IIR 8-pole | 0 | 0 |
| Standard | -0.1dB @ 20kHz | -80dB | FIR 31-tap | 15 samples | 30 samples |
| High | -0.01dB @ 20kHz | -100dB | FIR 63-tap | 31 samples | 62 samples |

**Rationale**:
- Economy uses IIR for zero latency, acceptable for live/monitoring
- Standard is the default, suitable for most mixing applications
- High is for mastering where transparency is critical
- Latency values are at base sample rate

### D7: DC Offset Handling

**Question**: How should DC offset be handled through the oversampling process?

**Decision**: Filters will be designed with 0 dB gain at DC (0 Hz).

**Rationale**:
- Lowpass anti-aliasing filters naturally pass DC unchanged
- No explicit DC blocking in the oversampler itself
- DC blocking should be done in the saturation/waveshaping stage if needed
- This maintains the oversampler as a pure, neutral primitive

### D8: Denormal Flushing

**Question**: Where should denormals be flushed?

**Decision**: Flush denormals at IIR filter output (already in BiquadCascade) and FIR accumulator output.

**Rationale**:
- Denormals from IIR filter decay are already handled by `detail::flushDenormal()` in biquad.h
- FIR filters don't produce denormals naturally (finite response)
- However, small input signals * small coefficients could produce denormals
- Add `flushDenormal()` after FIR accumulation as defensive measure
- Use existing `kDenormalThreshold = 1e-15f` from biquad.h

## Codebase Integration

### Existing Components to Reuse

| Component | Location | Usage |
|-----------|----------|-------|
| `BiquadCascade<4>` | dsp/primitives/biquad.h | IIR anti-aliasing in ZeroLatency mode |
| `FilterType::Lowpass` | dsp/primitives/biquad.h | Filter type for anti-aliasing |
| `kDenormalThreshold` | dsp/primitives/biquad.h | Denormal flushing constant |
| `detail::flushDenormal()` | dsp/primitives/biquad.h | Denormal flushing utility |
| `kButterworthQ` | dsp/primitives/biquad.h | Q value for flat passband |
| `butterworthQ(stage, total)` | dsp/primitives/biquad.h | Per-stage Q calculation |

### New Components to Create

| Component | Purpose |
|-----------|---------|
| `Oversampler<Factor, Quality>` | Main processing class, templated on factor and quality |
| `HalfbandFilter` | FIR halfband filter for linear-phase mode |
| `OversamplingFactor` | Enum: `x2`, `x4` |
| `OversamplingQuality` | Enum: `Economy`, `Standard`, `High` |
| `OversamplingMode` | Enum: `ZeroLatency`, `LinearPhase` |

## Technical Risks

### R1: Filter Coefficient Precision

**Risk**: Taylor series approximations for sin/cos may not be accurate enough for filter design at high sample rates.

**Mitigation**: Use runtime `std::sin`/`std::cos` for coefficient calculation (done in `prepare()`, not real-time critical). Only use constexpr versions if compile-time calculation is needed.

### R2: FIR Filter Coefficient Storage

**Risk**: High-quality FIR filters (63+ taps) increase memory footprint.

**Mitigation**:
- 63 taps * 4 bytes = 252 bytes per filter
- With 2 filters (up/down) * 2 stages (for 4x) * 2 channels = 8 filter instances
- Total: 8 * 252 = 2016 bytes = ~2KB (negligible)

### R3: CPU Overhead at High Sample Rates

**Risk**: At 192kHz with 4x oversampling, internal processing runs at 768kHz.

**Mitigation**:
- At 192kHz base rate, aliasing is already above 96kHz (inaudible)
- Consider automatically reducing oversampling factor at high sample rates
- Document recommended maximum base rate per factor in API

## Performance Estimates

| Configuration | Est. CPU (512 stereo @ 44.1kHz) |
|---------------|--------------------------------|
| 2x Economy (IIR) | 0.1% |
| 2x Standard (FIR 31-tap) | 0.3% |
| 2x High (FIR 63-tap) | 0.5% |
| 4x Economy (IIR) | 0.2% |
| 4x Standard (FIR 31-tap) | 0.6% |
| 4x High (FIR 63-tap) | 1.0% |

**Note**: These are estimates. Actual performance will be measured during implementation and optimized as needed.

## Open Questions (Resolved)

All clarifications have been resolved through design decisions above. No NEEDS CLARIFICATION markers remain.
