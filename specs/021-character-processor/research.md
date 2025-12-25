# Research: Character Processor

**Feature**: 021-character-processor
**Date**: 2025-12-25
**Purpose**: Resolve technical questions and document design decisions

## Research Topics

### 1. Mode Transition Crossfading Strategy

**Question**: How to implement smooth, click-free transitions between character modes?

**Decision**: Dual-instance crossfade with OnePoleSmoother

**Rationale**:
- Keep both old and new mode processing paths active during transition
- Use OnePoleSmoother with 50ms time constant for crossfade coefficient
- Old mode output fades out while new mode fades in
- Total energy remains constant (equal-power crossfade: sqrt for each path)

**Alternatives Considered**:
1. **Parameter morphing**: Morph individual parameters from one mode to another
   - Rejected: Modes have completely different signal chains (Tape has wow/flutter, BBD has clock noise, Digital has bit crushing)
   - No meaningful interpolation between fundamentally different processing

2. **Overlap-add**: Process overlap regions with both modes, blend
   - Rejected: More complex, requires buffering, latency increase
   - 50ms crossfade is sufficient for click-free transitions

**Implementation Notes**:
- `processingModeA_` and `processingModeB_` internal mode trackers
- `crossfadeSmoother_` transitions from 0 (modeA) to 1 (modeB)
- During crossfade: `output = outputA * (1 - fade) + outputB * fade`

---

### 2. Tape Mode Wow/Flutter Implementation

**Question**: How to implement authentic wow/flutter modulation?

**Decision**: Dual-LFO modulation of pitch/amplitude

**Rationale**:
- **Wow**: Slow modulation, larger depth, simulates capstan eccentricity
- **Flutter**: Fast modulation, smaller depth, simulates motor/head vibration
- Both modulate delay time slightly to create pitch variation
- Amplitude modulation secondary effect of tape saturation responding to level changes

**Implementation Notes**:
- Use existing LFO class with Sine waveform for both
- User-configurable range: 0.1-10Hz for both wow and flutter (per FR-008)
- Default presets for authentic character:
  - Wow: 1.0Hz rate, triangle waveform (authentic range 0.5-2Hz)
  - Flutter: 5.0Hz rate, smoothed random (authentic range 5-10Hz)
- Modulate a short delay line (0-5ms range) for pitch wobble
- Depth parameter scales LFO output before applying to delay time

**Reference**: Classic tape delays use ~3-5ms max delay modulation for wow effect

---

### 3. BBD Clock Noise Characteristics

**Question**: What are the characteristics of BBD clock noise?

**Decision**: High-frequency filtered white noise gated by clock frequency

**Rationale**:
- BBD chips (MN3005, MN3007) use clock frequencies of 10kHz-100kHz
- Clock bleedthrough creates high-frequency whine/noise at clock frequency
- Reconstructed audio shows clock artifacts as aliasing at Nyquist-related frequencies

**Implementation Notes**:
- Use NoiseGenerator with RadioStatic or filtered white noise
- Level scales with delay time (longer delay = lower clock = more audible)
- Bandwidth limiting (8-12kHz typical) naturally masks some clock noise
- Add subtle modulation to noise level for authentic character

**Reference**: Classic BBD delays (Boss DM-2, EHX Memory Man) have characteristic high-end fizz

---

### 4. Digital Vintage Bit Reduction Algorithm

**Question**: How to implement bit depth reduction for lo-fi effect?

**Decision**: Create BitCrusher as Layer 1 primitive with quantization and optional TPDF dither

**Rationale**:
- Reduce bit depth by quantizing to fewer levels
- Formula: `output = round(input * levels) / levels` where `levels = 2^bits`
- Add triangular probability density function (TPDF) dither before quantization
- Dither converts harsh quantization noise into smoother white noise
- **Reusability**: Lo-fi effects, distortion plugins, retro emulation

**Implementation Notes (Layer 1 Primitive)**:
```cpp
// In BitCrusher::process()
float process(float input) noexcept {
    const float levels = std::pow(2.0f, bitDepth_) - 1.0f;

    // Apply TPDF dither if enabled
    if (dither_ > 0.0f) {
        float d1 = rng_.nextFloat();  // [-1, 1]
        float d2 = rng_.nextFloat();  // [-1, 1]
        input += (d1 + d2) * dither_ / levels;
    }

    return std::round(input * levels) / levels;
}
```
- Bits parameter range: 4-16 (4 bits = 16 levels, very crunchy)
- 8 bits = 256 levels (~48dB SNR)
- Dither amount controls TPDF noise injection level

---

### 5. Sample Rate Reduction Algorithm

**Question**: How to implement sample rate reduction for lo-fi effect?

**Decision**: Create SampleRateReducer as Layer 1 primitive with fractional reduction support

**Rationale**:
- Hold each sample for N samples (where N = reduction factor)
- Creates staircasing and aliasing artifacts characteristic of early digital
- Combined with bit reduction for authentic lo-fi character
- **Reusability**: Lo-fi effects, aliasing plugins, retro emulation

**Implementation Notes (Layer 1 Primitive)**:
```cpp
// In SampleRateReducer::process()
float process(float input) noexcept {
    holdCounter_ += 1.0f;
    if (holdCounter_ >= reductionFactor_) {
        holdValue_ = input;
        holdCounter_ -= reductionFactor_;  // Fractional support
    }
    return holdValue_;
}
```
- Reduction factor range: 1-8x (1x = no reduction, 8x = heavy aliasing)
- Fractional factors supported (e.g., 2.5x) for fine control
- reset() clears hold state for seamless transport stop/start

---

### 6. Per-Mode Component Configuration

**Question**: How should each mode configure its internal components?

**Decision**: Mode-specific parameter presets applied on mode switch

| Mode | Saturation | Noise | Filter | LFO |
|------|------------|-------|--------|-----|
| **Tape** | Tape curve, moderate drive | TapeHiss enabled | LowShelf rolloff 8-12kHz | Wow + Flutter enabled |
| **BBD** | Diode/Tube curve, soft | RadioStatic (clock noise) | LowPass 8-12kHz cutoff | None (no inherent modulation) |
| **DigitalVintage** | None | Optional dither noise | None (aliasing is feature) | None |
| **Clean** | Bypass | Disabled | Bypass | Disabled |

**Implementation Notes**:
- Each mode has a `configure(params)` method setting internal component state
- Mode switch calls configure() then initiates crossfade
- Parameters within a mode are smoothed individually for automation

---

## Component Reuse Summary

| Existing Component | Used In Mode(s) | Configuration |
|--------------------|-----------------|---------------|
| SaturationProcessor | Tape, BBD | Tape: Tape curve; BBD: Tube/Diode curve |
| NoiseGenerator | Tape, BBD | Tape: TapeHiss; BBD: RadioStatic |
| MultimodeFilter | Tape, BBD | Tape: HighShelf rolloff; BBD: Lowpass |
| LFO | Tape | Wow: Sine 0.1-10Hz (default 1Hz); Flutter: SmoothRandom 0.1-10Hz (default 5Hz) |
| OnePoleSmoother | All | Parameter smoothing, crossfade control |

## New Components Required

| Component | Layer | Location | Purpose | Reuse Potential |
|-----------|-------|----------|---------|-----------------|
| BitCrusher | 1 | dsp/primitives/bit_crusher.h | Bit depth reduction with dither | Lo-fi effects, distortion, retro emulation |
| SampleRateReducer | 1 | dsp/primitives/sample_rate_reducer.h | Sample-and-hold for SR reduction | Lo-fi effects, aliasing artifacts |
| CrossfadeController | internal | character_processor.h | Mode transition management | CharacterProcessor only |

---

## Performance Considerations

**CPU Budget**: < 1% per instance at 44.1kHz stereo (SC-003)

**Component Costs** (estimated from existing benchmarks):
- SaturationProcessor: ~0.3% (includes 2x oversampling)
- MultimodeFilter: ~0.1%
- NoiseGenerator: ~0.05%
- LFO (x2): ~0.02%
- BitCrusher/SampleRateReducer: ~0.01%
- Crossfade overhead: ~0.02%

**Total Estimated**: ~0.5% per instance (well within budget)

**Optimization Notes**:
- Clean mode should bypass all processing for minimal CPU
- Only active mode's components run (except during crossfade)
- Bit crushing and sample rate reduction are trivial operations

---

## Edge Case Handling

### Low Sample Rates (22.05kHz - 44.1kHz)

**Concern**: All modes must remain functional at low sample rates.

**Resolution**:
- Wow/flutter delay line: Max 5ms = 110 samples at 22.05kHz (well within reasonable buffer)
- BBD bandwidth limiting: Automatically scales with Nyquist frequency
- Digital Vintage SR reduction: Factor 8x at 22.05kHz = effective 2.76kHz (extreme but intentional)
- All component prepare() methods receive actual sample rate and adapt accordingly

### High Sample Rates (96kHz - 192kHz)

**Concern**: Digital Vintage mode sample rate reduction at high host rates.

**Resolution**:
- SampleRateReducer factor is **relative to host sample rate**, not absolute
- Factor 4x at 192kHz = effective 48kHz (still reasonable for lo-fi effect)
- Factor 8x at 192kHz = effective 24kHz (authentic early digital character)
- No upsampling/downsampling needed; sample-and-hold operates directly on host samples

### Sample Rate Extreme Values

| Host Rate | SR Reduction Factor | Effective Rate | Character |
|-----------|---------------------|----------------|-----------|
| 44.1kHz | 1x | 44.1kHz | Clean |
| 44.1kHz | 4x | 11kHz | Lo-fi |
| 44.1kHz | 8x | 5.5kHz | Extreme |
| 192kHz | 1x | 192kHz | Clean |
| 192kHz | 4x | 48kHz | Subtle |
| 192kHz | 8x | 24kHz | Moderate |
