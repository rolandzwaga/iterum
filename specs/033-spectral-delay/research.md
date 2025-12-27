# Research: Spectral Delay

**Feature**: 033-spectral-delay
**Date**: 2025-12-26

## Overview

This document records design decisions made during the planning phase for the Spectral Delay feature.

---

## Decision 1: Per-Bin Delay Architecture

**Decision**: Use individual DelayLine instances per frequency bin (up to 2049 for 4096 FFT)

**Rationale**:
- Each bin needs independent delay time for spectral spread effect
- Existing DelayLine primitive is optimized and tested
- Memory overhead acceptable: 2049 bins * 2s max delay * 44.1kHz = ~180MB worst case
- Typical use (1024 FFT, 500ms) = ~11MB per channel

**Alternatives Considered**:
1. **Single circular buffer with per-bin read pointers** - More complex indexing, harder to maintain
2. **Matrix delay approach** - Overkill for this application, higher latency
3. **Shared delay pool** - Would require complex allocation logic

---

## Decision 2: Spectral Processing Rate

**Decision**: Process at spectral frame rate (every hopSize samples), not audio sample rate

**Rationale**:
- STFT analysis produces frames at hopSize intervals (typically fftSize/2 or fftSize/4)
- Per-bin delays operate on spectral frames, not audio samples
- Reduces CPU: 512 spectral frames/sec at 50% overlap vs 44100 samples/sec
- Delay time resolution = hopSize / sampleRate (e.g., ~11.6ms at 512 hop, 44.1kHz)

**Alternatives Considered**:
1. **Audio-rate per-sample processing** - Prohibitively expensive (2049 * 44100 operations/sec)
2. **Hybrid approach** - Unnecessary complexity for target effect

---

## Decision 3: Freeze Implementation

**Decision**: Copy-and-hold spectral buffer contents; bypass STFT analysis during freeze

**Rationale**:
- Simple and efficient - just stop updating the spectrum
- Output continues via OverlapAdd with frozen spectrum
- Crossfade during transition (50-100ms) prevents clicks
- Already proven pattern in FreezeMode (031)

**Alternatives Considered**:
1. **Freeze individual bins** - More complex, minimal benefit
2. **Freeze with slow fade** - Too subtle for user control

---

## Decision 4: Spread Direction Algorithm

**Decision**: Linear interpolation across bins based on spread direction enum

**Rationale**:
- Three modes cover common use cases:
  - LowToHigh: Higher frequencies delay more (rising arpeggio effect)
  - HighToLow: Lower frequencies delay more (falling echo)
  - CenterOut: Mid frequencies delay least, extremes delay most (smear effect)
- Linear mapping is predictable and CPU-efficient

**Algorithm**:
```cpp
// LowToHigh: bin 0 = baseDelay, bin N = baseDelay + spreadAmount
// HighToLow: bin 0 = baseDelay + spreadAmount, bin N = baseDelay
// CenterOut: mid bins = baseDelay, edge bins = baseDelay + spreadAmount

float normalizedBin = static_cast<float>(bin) / (numBins - 1);  // 0..1
float delayOffset = 0.0f;

switch (direction) {
    case LowToHigh:
        delayOffset = normalizedBin * spreadMs;
        break;
    case HighToLow:
        delayOffset = (1.0f - normalizedBin) * spreadMs;
        break;
    case CenterOut:
        delayOffset = std::abs(normalizedBin - 0.5f) * 2.0f * spreadMs;
        break;
}
return baseDelayMs + delayOffset;
```

**Alternatives Considered**:
1. **Logarithmic spread** - More complex, less intuitive for users
2. **Custom curve** - Would need additional UI parameter

---

## Decision 5: Feedback Implementation

**Decision**: Apply feedback in spectral domain after per-bin delay, before synthesis

**Rationale**:
- Spectral feedback creates unique building textures
- Feedback tilt can shape decay per frequency band
- Soft limiting prevents oscillation in any bin

**Algorithm**:
```cpp
// For each bin:
float delayedMag = delayLines_[bin].read(binDelayFrames);
float feedbackGain = calculateTiltedFeedback(bin, globalFeedback, tilt);
delayLines_[bin].write(inputMag + delayedMag * feedbackGain);
outputMag = delayedMag;
```

**Feedback Tilt Formula**:
```cpp
// tilt: -1.0 = full low bias, 0.0 = uniform, +1.0 = full high bias
float normalizedBin = static_cast<float>(bin) / (numBins - 1);
float tiltFactor = 1.0f + tilt * (normalizedBin - 0.5f) * 2.0f;
return std::clamp(globalFeedback * tiltFactor, 0.0f, 1.5f);
```

---

## Decision 6: FFT Size Selection

**Decision**: User-selectable from 512, 1024, 2048, 4096

**Rationale**:
- Tradeoff between frequency resolution and latency:
  | FFT Size | Bins | Latency (44.1kHz) | Freq Resolution |
  |----------|------|-------------------|-----------------|
  | 512 | 257 | 11.6ms | 86 Hz |
  | 1024 | 513 | 23.2ms | 43 Hz |
  | 2048 | 1025 | 46.4ms | 21.5 Hz |
  | 4096 | 2049 | 92.9ms | 10.7 Hz |

- Default: 1024 (good balance for delay effects)
- User can trade latency for resolution based on material

**Alternatives Considered**:
1. **Fixed FFT size** - Less flexibility
2. **Auto-select based on content** - Too complex, unpredictable

---

## Decision 7: Diffusion Implementation

**Decision**: Spectral blurring via magnitude spreading to neighboring bins

**Rationale**:
- Simple convolution kernel blurs spectrum over time
- Creates soft, evolving textures
- Low CPU cost (single pass per frame)

**Algorithm**:
```cpp
// 3-tap blur kernel with diffusion amount control
float blurKernel[3] = {diffusion * 0.25f, 1.0f - diffusion * 0.5f, diffusion * 0.25f};

for (size_t bin = 1; bin < numBins - 1; ++bin) {
    blurredMag[bin] =
        spectrum.getMagnitude(bin - 1) * blurKernel[0] +
        spectrum.getMagnitude(bin) * blurKernel[1] +
        spectrum.getMagnitude(bin + 1) * blurKernel[2];
}
```

**Alternatives Considered**:
1. **Phase randomization** - Different character, more CPU
2. **Multi-tap convolution** - Heavier blur, more expensive

---

## Decision 8: Stereo Processing

**Decision**: Independent left/right STFT chains with shared parameters

**Rationale**:
- True stereo preserves spatial information
- Each channel gets own STFT, bin delays, OverlapAdd
- Parameters (spread, feedback, freeze) applied identically to both
- Memory: 2x single channel, but maintains stereo imaging

**Alternatives Considered**:
1. **Mid/Side processing** - Different character, not typical for delays
2. **Mono->Stereo** - Loses input spatial info

---

## Implementation Notes

### Memory Allocation Strategy

1. **prepare()** allocates:
   - 2 STFT analyzers (L/R)
   - 2 OverlapAdd synthesizers (L/R)
   - 4 SpectralBuffers (input L/R, output L/R)
   - 1 frozen SpectralBuffer (for freeze)
   - 2 * numBins DelayLine instances (L/R per bin)
   - Parameter smoothers

2. **process()** performs:
   - No allocations (real-time safe)
   - Frame-rate spectral operations
   - Per-bin delay read/write

### Latency Calculation

Total latency = FFT analysis latency + per-bin delay (minimum)
- FFT latency = fftSize samples (for analysis window fill)
- Reported via getLatencySamples()

### CPU Optimization Opportunities

1. SIMD for per-bin operations (future)
2. Skip processing for frozen frames (just repeat output)
3. Sparse delay updates when spread is 0 (all bins same delay)
