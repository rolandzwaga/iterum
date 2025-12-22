# Research: LFO DSP Primitive

**Feature**: 003-lfo
**Date**: 2025-12-22
**Status**: Complete

## Overview

This document consolidates research findings for implementing the LFO (Low Frequency Oscillator) Layer 1 DSP primitive. All NEEDS CLARIFICATION items from the plan have been resolved.

---

## 1. Wavetable Generation

### Decision
Pre-compute wavetables during `prepare()` using standard mathematical formulas. Use 2048 samples per wavetable for balance between memory and quality.

### Rationale
- **2048 samples** provides adequate resolution for LFO frequencies (0.01-20 Hz)
- At 20 Hz and 44.1kHz sample rate, phase increment is ~0.001 per sample, meaning ~1000 table samples traversed per cycle - plenty of resolution
- Pre-computation eliminates runtime transcendental function calls
- Memory cost: 6 waveforms × 2048 samples × 4 bytes = 48KB (acceptable for Layer 1)

### Waveform Generation Formulas

```cpp
// Phase p is normalized [0, 1) representing one complete cycle

// Sine: sin(2π × p)
sine[i] = std::sin(2.0 * PI * (i / tableSize));

// Triangle: Linear ramp 0→1→-1→0
// p in [0, 0.25): 4p (0 to 1)
// p in [0.25, 0.75): 2 - 4p (1 to -1)
// p in [0.75, 1): 4p - 4 (-1 to 0)
triangle[i] = 1.0 - 4.0 * std::abs(std::fmod(p + 0.25, 1.0) - 0.5);

// Sawtooth: Linear ramp from -1 to +1
sawtooth[i] = 2.0 * p - 1.0;

// Square: +1 for first half, -1 for second half
square[i] = (p < 0.5) ? 1.0f : -1.0f;
```

### Alternatives Considered
- **Direct computation (no wavetable)**: Rejected for performance - `std::sin()` is expensive
- **256 samples**: Rejected - too few samples causes audible stepping at low frequencies
- **4096+ samples**: Rejected - diminishing returns for LFO rates, unnecessary memory

---

## 2. Sample & Hold / Smoothed Random

### Decision
Implement using a pseudo-random number generator (PRNG) seeded per-instance. Sample & Hold triggers new random value at cycle start. Smoothed Random interpolates between random targets.

### Rationale
- **PRNG choice**: Use simple LCG (Linear Congruential Generator) or `std::minstd_rand` for determinism and speed
- **Sample & Hold**: Generate new random value when phase wraps from 1.0 back to 0.0
- **Smoothed Random**: Linear interpolation between current and next random value over one cycle

### Implementation Strategy

```cpp
// Sample & Hold
if (phase wrapped this sample) {
    currentRandomValue_ = nextRandomValue();  // -1 to +1
}
return currentRandomValue_;

// Smoothed Random
if (phase wrapped this sample) {
    previousRandom_ = targetRandom_;
    targetRandom_ = nextRandomValue();
}
// Linear interpolation from previous to target based on phase
return previousRandom_ + phase_ * (targetRandom_ - previousRandom_);
```

### Alternatives Considered
- **External random source**: Rejected - breaks real-time safety (system RNG may allocate)
- **Perlin noise**: Rejected - overcomplicated for LFO use; smoothed random achieves similar effect
- **Pre-generated random table**: Considered but rejected - would repeat patterns; per-cycle generation is preferable

---

## 3. Phase Accumulator Precision

### Decision
Use `double` for phase accumulator and phase increment. Store phase in normalized range [0.0, 1.0).

### Rationale
- **Float precision issue**: At 0.01 Hz and 192kHz sample rate, phase increment is ~5.2×10⁻⁸
- After 24 hours: 192000 × 3600 × 24 = 16.6 billion samples
- **Float (23-bit mantissa)**: Error accumulates to ~0.001 (0.1% drift) - UNACCEPTABLE
- **Double (52-bit mantissa)**: Error accumulates to ~10⁻⁹ (0.0000001% drift) - ACCEPTABLE

### Phase Accumulator Design

```cpp
double phase_ = 0.0;        // Normalized [0, 1)
double phaseIncrement_ = 0.0;  // frequency / sampleRate

// Per sample:
phase_ += phaseIncrement_;
if (phase_ >= 1.0) {
    phase_ -= 1.0;  // Wrap to [0, 1)
    // Trigger S&H update, etc.
}
```

### Drift Calculation
- Target: < 0.0001° drift over 24 hours (per SC-004)
- 0.0001° = 0.0001/360 = 2.78×10⁻⁷ phase units
- At worst case (0.01 Hz, 192kHz): 16.6 billion additions
- Double precision error per addition: ~10⁻¹⁶
- Accumulated error: ~10⁻⁶ << 2.78×10⁻⁷ ✓

### Alternatives Considered
- **Fixed-point arithmetic**: Rejected - more complex, less portable, not needed for LFO
- **Periodic phase reset**: Rejected - would cause discontinuities; double precision is sufficient

---

## 4. Tempo Sync Formulas

### Decision
Convert note values to frequency using BPM and note duration multipliers.

### Rationale
Standard musical timing:
- Quarter note at 120 BPM = 0.5 seconds = 2 Hz
- Base formula: `frequency = BPM / (60 × noteBeats)`

### Note Value Durations (in quarter note beats)

| Note Value | Normal | Dotted (×1.5) | Triplet (×2/3) |
|------------|--------|---------------|----------------|
| 1/1 (Whole) | 4 | 6 | 2.667 |
| 1/2 (Half) | 2 | 3 | 1.333 |
| 1/4 (Quarter) | 1 | 1.5 | 0.667 |
| 1/8 (Eighth) | 0.5 | 0.75 | 0.333 |
| 1/16 (Sixteenth) | 0.25 | 0.375 | 0.167 |
| 1/32 (Thirty-second) | 0.125 | 0.1875 | 0.083 |

### Frequency Calculation

```cpp
// beatsPerNote = base note beats × modifier
// Dotted: beatsPerNote × 1.5
// Triplet: beatsPerNote × (2.0/3.0)

float beatsPerNote = getBeatsForNoteValue(noteValue, modifier);
float beatsPerSecond = bpm / 60.0f;
float frequency = beatsPerSecond / beatsPerNote;

// Example: 1/4 dotted at 120 BPM
// beatsPerNote = 1.0 × 1.5 = 1.5
// beatsPerSecond = 120 / 60 = 2
// frequency = 2 / 1.5 = 1.333 Hz (cycle every 750ms)
```

### Alternatives Considered
- **Lookup table for frequencies**: Rejected - simple formula is clearer and handles any BPM
- **Pre-computed note ratios only**: Considered but formula is equally efficient

---

## 5. Linear Interpolation for Wavetable

### Decision
Use linear interpolation between adjacent wavetable samples when reading.

### Rationale
- At LFO frequencies (≤20 Hz), linear interpolation introduces negligible error
- More complex interpolation (cubic, sinc) not needed for sub-audio rate signals
- Linear interpolation is simple, branch-free, and fast

### Implementation

```cpp
float readWavetable(const std::vector<float>& table, double phase) {
    // phase is [0, 1), scale to table index
    double scaledPhase = phase * table.size();
    size_t index0 = static_cast<size_t>(scaledPhase);
    size_t index1 = (index0 + 1) & (table.size() - 1);  // Wrap
    float frac = static_cast<float>(scaledPhase - index0);

    return table[index0] + frac * (table[index1] - table[index0]);
}
```

### Power-of-2 Table Size
Using 2048 (power of 2) enables:
- Bitwise AND for wrap: `index & (size - 1)` instead of modulo
- Efficient index calculation

### Alternatives Considered
- **No interpolation (nearest sample)**: Rejected - causes audible stepping
- **Cubic interpolation**: Rejected - unnecessary for LFO rates, adds complexity
- **Band-limited tables**: Rejected - only needed for audio-rate oscillators

---

## 6. Waveform Switching

### Decision
Allow waveform changes at any time. For click-free transitions, optionally crossfade over ~5ms.

### Rationale
- Instant switching can cause clicks if waveforms have different values at current phase
- However, for LFO modulation, clicks are often inaudible (modulating delay time, not audio directly)
- Provide instant switch as default; crossfade can be added in future if needed

### Implementation Notes
- Store current waveform enum, switch immediately on setWaveform()
- Phase continues from current position (no reset)
- S&H and SmoothRandom have separate state that persists across waveform changes

---

## 7. Design Summary

| Component | Decision | Key Parameters |
|-----------|----------|----------------|
| Wavetable size | 2048 samples | 6 waveforms × 48KB total |
| Phase precision | double | < 0.0001° drift/24hr |
| Interpolation | Linear | Bitwise wrap, O(1) |
| Random generator | PRNG (minstd_rand or LCG) | Seeded per instance |
| Tempo sync | Formula-based | BPM / (60 × noteBeats) |
| Waveform switch | Instant | No crossfade (v1) |

---

## References

- Julius O. Smith, "Mathematics of the Discrete Fourier Transform" - wavetable interpolation
- Ross Bencina, "Real-time audio programming 101" - phase accumulator design
- CLAUDE.md LFO section - interpolation selection guidance
- Existing DelayLine implementation - inline header pattern
