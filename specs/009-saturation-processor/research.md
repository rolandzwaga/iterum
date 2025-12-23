# Research: Saturation Processor Algorithms

**Feature**: 009-saturation-processor
**Date**: 2025-12-23
**Status**: Complete

## Overview

This document captures research and design decisions for the five saturation types in the Saturation Processor. Each algorithm is analyzed for harmonic characteristics, implementation approach, and testing methodology.

---

## 1. Tape Saturation (tanh)

### Decision
Use `std::tanh(x)` for tape saturation.

### Rationale
- **Industry Standard**: tanh is the canonical tape saturation curve
- **Symmetric**: Produces only odd harmonics (3rd, 5th, 7th...)
- **Smooth Compression**: Gradual transition from linear to saturated regions
- **No DC Offset**: Symmetric around zero

### Mathematical Formula
```
y = tanh(x * drive)
```

Where `drive` is the pre-saturation gain multiplier.

### Harmonic Analysis
For input `sin(wt)` with sufficient drive:
- 3rd harmonic at -20 to -30 dB relative to fundamental
- 5th harmonic at -35 to -45 dB
- 7th harmonic at -45 to -55 dB
- Higher harmonics decay progressively

### Alternatives Considered
| Alternative | Rejected Because |
|-------------|------------------|
| Fast tanh approximation (polynomial) | Accuracy is more important than speed; modern CPUs compute tanh efficiently |
| Look-up table with interpolation | Adds complexity; minimal CPU benefit for this use case |

### Implementation Notes
- Use `std::tanh` directly from `<cmath>`
- No approximation needed - standard library is sufficiently fast
- Reference: MultimodeFilter already uses `std::tanh` in drive processing

---

## 2. Tube Saturation (Asymmetric Polynomial)

### Decision
Use asymmetric polynomial curve that enhances even harmonics.

### Rationale
- **Even Harmonics**: Tube amplifiers are characterized by 2nd and 4th harmonic enhancement
- **Warm Sound**: Even harmonics perceived as "warm" and "musical"
- **Asymmetry**: Achieved by different transfer functions for positive vs negative input

### Mathematical Formula
```cpp
// Asymmetric polynomial for tube character
if (x >= 0) {
    y = x - (x * x * x) / 3.0f;  // Softer positive clipping
} else {
    y = x - (x * x * x) / 2.0f;  // Harder negative clipping
}
```

This creates asymmetry: positive half-wave clips softer than negative, generating even harmonics.

### Harmonic Analysis
For input `sin(wt)` with moderate drive:
- 2nd harmonic at -30 to -40 dB (spec requires > -50 dB with +12dB drive)
- 4th harmonic at -45 to -55 dB
- DC offset generated - requires DC blocking

### Alternatives Considered
| Alternative | Rejected Because |
|-------------|------------------|
| Exponential asymmetry | More complex, harder to control knee |
| Different polynomial coefficients | Current formula well-documented in audio literature |
| Wave folding | Different character, not traditional tube sound |

### Implementation Notes
- Apply DC blocker after this stage (highpass ~10Hz)
- May need output normalization to maintain unity gain at low drive
- Reference: "Virtual Analog Modeling" by Välimäki et al.

---

## 3. Transistor Saturation (Hard-Knee Soft Clip)

### Decision
Use hard-knee polynomial with smooth transition to clipping.

### Rationale
- **Distinct Character**: More aggressive than tape, less harsh than digital
- **Hard Knee**: Quick transition from linear to saturated region
- **Symmetric**: Maintains odd-harmonic character

### Mathematical Formula
```cpp
// Hard-knee soft clip
float threshold = 0.6667f;  // 2/3
if (std::abs(x) < threshold) {
    y = x;  // Linear region
} else if (x > 0) {
    float t = (x - threshold) / (1.0f - threshold);
    y = threshold + (1.0f - threshold) * (t - t * t * t / 3.0f);
} else {
    float t = (-x - threshold) / (1.0f - threshold);
    y = -(threshold + (1.0f - threshold) * (t - t * t * t / 3.0f));
}
```

### Harmonic Analysis
- Sharp knee creates more high-order harmonics than tape
- Still predominantly odd harmonics (symmetric)
- Characteristic "bite" in upper harmonics

### Alternatives Considered
| Alternative | Rejected Because |
|-------------|------------------|
| arctan | Too soft, similar to tape |
| Piece-wise linear | Discontinuous derivatives cause aliasing |
| Sigmoid variations | Less characteristic transistor sound |

### Implementation Notes
- Threshold parameter could be made adjustable in future
- Current threshold (2/3) is common in guitar amp modeling
- Reference: "Numerical Methods for Simulation of Guitar Distortion" by Yeh

---

## 4. Digital Saturation (Hard Clip)

### Decision
Use simple `std::clamp` for hard digital clipping.

### Rationale
- **Simplicity**: Easiest to implement and understand
- **CPU Efficient**: Single comparison operation
- **Distinct Sound**: Harsh, aggressive character
- **Maximum Harmonic Content**: Most harmonics of any type

### Mathematical Formula
```cpp
y = std::clamp(x * drive, -1.0f, 1.0f);
```

### Harmonic Analysis
- All odd harmonics present (rectangular wave characteristics)
- Harmonics decay as 1/n (Gibbs phenomenon)
- Very harsh at high frequencies - aliasing concern addressed by oversampling

### Alternatives Considered
| Alternative | Rejected Because |
|-------------|------------------|
| Soft digital clip | Defeats purpose of "digital" type |
| Bit crushing | Different effect, not clipping |

### Implementation Notes
- Reuse existing `hardClip()` from dsp_utils.h
- Apply drive before clipping, not after
- Most benefit from oversampling due to harsh harmonics

---

## 5. Diode Saturation (Soft Asymmetric)

### Decision
Use exponential-based asymmetric curve modeling diode characteristics.

### Rationale
- **Realistic Diode Behavior**: Diodes conduct asymmetrically
- **Subtle Warmth**: Less aggressive than tube, more "glue"
- **Mixed Harmonics**: Both even and odd harmonics

### Mathematical Formula
```cpp
// Diode-style soft asymmetric clipping
// Based on simplified Shockley diode equation
float positive = 1.0f - std::exp(-x);      // Forward bias
float negative = -1.0f + std::exp(x);       // Reverse bias (softer)
y = (x >= 0) ? positive : negative * 0.5f;  // Asymmetry factor
```

### Harmonic Analysis
- 2nd harmonic present but less prominent than tube
- Odd harmonics also present (mixed spectrum)
- Subtle DC offset - DC blocking required

### Alternatives Considered
| Alternative | Rejected Because |
|-------------|------------------|
| Full Shockley equation | Over-complicated for audio |
| Lookup table | Polynomial is sufficient |
| Piece-wise approximation | Discontinuities cause artifacts |

### Implementation Notes
- Exponential can be approximated if CPU becomes concern
- DC blocker essential for asymmetric output
- May need gain compensation due to asymmetric reduction

---

## Oversampling Strategy

### Decision
Use 2x oversampling for all saturation types.

### Rationale
- **Aliasing Prevention**: Nonlinear processing creates harmonics above Nyquist
- **Constitution Requirement**: Principle X mandates oversampling for nonlinearities
- **CPU Balance**: 2x is practical limit for real-time; higher adds transient smear

### Implementation
- Reuse existing `Oversampler<2,1>` from Layer 1
- Process: Upsample -> Saturate -> Downsample -> DC Block

### Note on 4x/8x
Higher oversampling factors provide diminishing returns and can introduce phase issues. The existing 2x oversampler with polyphase filtering is sufficient for quality saturation.

---

## DC Blocking Strategy

### Decision
Use single-pole highpass at ~10Hz after saturation.

### Rationale
- **Sub-Audible**: 10Hz is below audible range (20Hz threshold)
- **Minimal Phase Impact**: Single-pole has gentler phase shift than biquad
- **Sufficient Rejection**: Removes DC while preserving bass content

### Implementation Options

**Option A: One-Pole Filter (Chosen)**
```cpp
// Simple DC blocker
y[n] = x[n] - x[n-1] + R * y[n-1]
// where R = 0.995 for ~15Hz cutoff at 44.1kHz
```

**Option B: Biquad Highpass**
- Use existing Biquad with FilterType::Highpass at 10Hz
- Slightly more CPU but consistent with existing patterns

### Decision
Use Biquad with FilterType::Highpass for consistency with codebase patterns and Layer 1 reuse.

---

## Parameter Smoothing

### Decision
Use OnePoleSmoother with 5ms time constant for all parameters.

### Rationale
- **Click Prevention**: Sudden parameter changes cause audible clicks
- **Consistent Behavior**: Same smoothing used across processor
- **Layer 1 Reuse**: OnePoleSmoother already exists and tested

### Smoothed Parameters
1. Input gain (linear, after dB conversion)
2. Output gain (linear, after dB conversion)
3. Mix (already 0-1 range)

### Note on Type Changes
SaturationType enum changes are NOT smoothed - instant transition is acceptable since all types have similar output levels.

---

## Test Strategy

### Success Criteria Verification

| SC | Test Approach |
|----|---------------|
| SC-001 (Tape 3rd harmonic > -40dB) | FFT analysis of 1kHz sine with +12dB drive |
| SC-002 (Tube 2nd harmonic > -50dB) | FFT analysis of 1kHz sine with +12dB drive |
| SC-003 (Alias rejection > 48dB) | FFT of 10kHz sine, check for aliases below fundamental |
| SC-004 (DC offset < 0.001) | Mean of Tube output over 1 second |
| SC-005 (Click-free transitions) | Parameter ramp test - no samples > threshold |
| SC-006 (noexcept verification) | static_assert on all public methods |
| SC-007 (No allocations) | Code inspection |
| SC-008 (Mix accuracy) | 50% mix produces (dry+wet)/2 within 0.5dB |

### FFT Test Setup
- Use existing FFT primitive from Layer 1
- Window: Hann for harmonic analysis
- Size: 8192 samples minimum for frequency resolution
- Sample rate: 44100 Hz (standard test rate)

---

## Summary of Decisions

| Topic | Decision | Confidence |
|-------|----------|------------|
| Tape algorithm | std::tanh | High |
| Tube algorithm | Asymmetric polynomial | High |
| Transistor algorithm | Hard-knee polynomial | High |
| Digital algorithm | std::clamp (hardClip) | High |
| Diode algorithm | Exponential asymmetric | Medium-High |
| Oversampling | 2x via Oversampler<2,1> | High |
| DC blocking | Biquad Highpass @ 10Hz | High |
| Parameter smoothing | OnePoleSmoother @ 5ms | High |

All decisions align with Constitution principles and leverage existing Layer 1 primitives.
