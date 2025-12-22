# Quickstart: Biquad Filter

**Feature**: 004-biquad-filter
**Date**: 2025-12-22
**Purpose**: Usage examples for the Biquad filter primitive

---

## Basic Usage

### 1. Simple Lowpass Filter

```cpp
#include "dsp/primitives/biquad.h"

using namespace Iterum::DSP;

// Create and configure a lowpass filter
Biquad lpf;
lpf.configure(FilterType::Lowpass, 1000.0f, 0.7071f, 0.0f, 44100.0f);
//            type                 freq    Q       gain(ignored) sampleRate

// Process a single sample
float input = 0.5f;
float output = lpf.process(input);

// Process a buffer
float buffer[512];
// ... fill buffer with audio ...
lpf.processBlock(buffer, 512);
```

### 2. Parametric EQ (Peak Filter)

```cpp
// Create a +6dB boost at 3kHz with Q=2
Biquad peakEQ;
peakEQ.configure(FilterType::Peak, 3000.0f, 2.0f, 6.0f, 44100.0f);
//               type              freq     Q     +6dB  sampleRate

// Cut mode: negative gain
peakEQ.configure(FilterType::Peak, 3000.0f, 2.0f, -6.0f, 44100.0f);
//                                                  -6dB
```

### 3. Shelf Filters

```cpp
// Low shelf: boost bass by 4dB below 200Hz
Biquad lowShelf;
lowShelf.configure(FilterType::LowShelf, 200.0f, 0.7071f, 4.0f, 44100.0f);

// High shelf: cut treble by 3dB above 8kHz
Biquad highShelf;
highShelf.configure(FilterType::HighShelf, 8000.0f, 0.7071f, -3.0f, 44100.0f);
```

---

## Smoothed Parameter Changes

### 4. Click-Free Filter Modulation

```cpp
#include "dsp/primitives/biquad.h"

using namespace Iterum::DSP;

SmoothedBiquad filter;

// Set smoothing time (10ms is a good default)
filter.setSmoothingTime(10.0f, 44100.0f);

// Set initial filter state
filter.setTarget(FilterType::Lowpass, 1000.0f, 0.7071f, 0.0f, 44100.0f);
filter.snapToTarget();  // Jump immediately (at start)

// In audio callback:
void processBlock(float* buffer, size_t numSamples) {
    // Update filter target (will smoothly transition)
    float newCutoff = getLFOValue() * 4000.0f + 100.0f;  // 100-4100 Hz
    filter.setTarget(FilterType::Lowpass, newCutoff, 0.7071f, 0.0f, 44100.0f);

    // Process with smooth coefficient updates
    filter.processBlock(buffer, numSamples);
}
```

---

## Cascaded Filters (Steeper Slopes)

### 5. 24 dB/octave Lowpass (4-pole)

```cpp
#include "dsp/primitives/biquad.h"

using namespace Iterum::DSP;

// Use Biquad24dB alias for 2-stage cascade
Biquad24dB steepLPF;
steepLPF.setButterworth(FilterType::Lowpass, 1000.0f, 44100.0f);

// Or create explicitly
BiquadCascade<2> steepLPF2;
steepLPF2.setButterworth(FilterType::Lowpass, 1000.0f, 44100.0f);

// Process
float output = steepLPF.process(input);
```

### 6. 48 dB/octave Highpass (8-pole)

```cpp
// Aggressive highpass for rumble removal
Biquad48dB rumbleFilter;
rumbleFilter.setButterworth(FilterType::Highpass, 30.0f, 44100.0f);

// Process entire buffer
rumbleFilter.processBlock(buffer, numSamples);
```

---

## Compile-Time Coefficient Calculation

### 7. Constexpr Filter Coefficients

```cpp
#include "dsp/primitives/biquad.h"

using namespace Iterum::DSP;

// Pre-compute coefficients at compile time
constexpr auto lpfCoeffs = BiquadCoefficients::calculateConstexpr(
    FilterType::Lowpass,
    1000.0f,    // 1kHz cutoff
    0.7071f,    // Butterworth Q
    0.0f,       // gain (ignored for lowpass)
    44100.0f    // sample rate
);

// Use in filter
Biquad filter(lpfCoeffs);

// Or create lookup table at compile time
constexpr std::array<BiquadCoefficients, 4> filterBank = {
    BiquadCoefficients::calculateConstexpr(FilterType::Lowpass, 500.0f, 0.7f, 0.0f, 44100.0f),
    BiquadCoefficients::calculateConstexpr(FilterType::Lowpass, 1000.0f, 0.7f, 0.0f, 44100.0f),
    BiquadCoefficients::calculateConstexpr(FilterType::Lowpass, 2000.0f, 0.7f, 0.0f, 44100.0f),
    BiquadCoefficients::calculateConstexpr(FilterType::Lowpass, 4000.0f, 0.7f, 0.0f, 44100.0f),
};
```

---

## Common Use Cases

### 8. DC Blocking Filter

```cpp
// Remove DC offset (subsonic content below 5Hz)
Biquad dcBlocker;
dcBlocker.configure(FilterType::Highpass, 5.0f, 0.7071f, 0.0f, 44100.0f);

// Apply before recording or at plugin output
dcBlocker.processBlock(buffer, numSamples);
```

### 9. Anti-Aliasing Before Downsampling

```cpp
// Lowpass at Nyquist/2 before 2x downsample
float nyquist = sampleRate / 2.0f;
Biquad24dB antiAlias;
antiAlias.setButterworth(FilterType::Lowpass, nyquist * 0.45f, sampleRate);

// Process at original rate
antiAlias.processBlock(oversampledBuffer, oversampledSize);

// Then decimate
```

### 10. Feedback Path Filtering (Delay Effect)

```cpp
class DelayWithFeedbackFilter {
    DelayLine delay_;
    Biquad feedbackFilter_;
    float feedback_ = 0.5f;

public:
    void configure(float cutoff, float sampleRate) {
        // Darken feedback with lowpass
        feedbackFilter_.configure(
            FilterType::Lowpass, cutoff, 0.7071f, 0.0f, sampleRate
        );
    }

    float process(float input) {
        float delayed = delay_.read();

        // Filter in feedback path
        float filtered = feedbackFilter_.process(delayed);

        // Write new sample with feedback
        delay_.write(input + filtered * feedback_);

        return input + delayed;  // Mix wet/dry
    }
};
```

---

## State Management

### 11. Reset Filter State

```cpp
// Reset to clear history (prevents clicks when stopping/starting)
filter.reset();

// Also reset when sample rate changes
void setSampleRate(float newSampleRate) {
    sampleRate_ = newSampleRate;
    filter.configure(type_, freq_, q_, gain_, sampleRate_);
    filter.reset();  // Clear state for clean start
}
```

### 12. Check Filter Stability

```cpp
auto coeffs = BiquadCoefficients::calculate(
    FilterType::Lowpass, freq, q, 0.0f, sampleRate
);

if (!coeffs.isStable()) {
    // Clamp parameters to safe range
    freq = std::min(freq, sampleRate * 0.45f);
    q = std::clamp(q, 0.1f, 30.0f);
    coeffs = BiquadCoefficients::calculate(
        FilterType::Lowpass, freq, q, 0.0f, sampleRate
    );
}

filter.setCoefficients(coeffs);
```

---

## Performance Tips

1. **Prefer block processing** over sample-by-sample when possible
2. **Avoid reconfiguring** every sample - use SmoothedBiquad for modulation
3. **Pre-compute coefficients** with constexpr when parameters are known
4. **Reset state** when starting after silence to avoid denormal buildup
5. **Use appropriate slope** - 12 dB/oct is often enough, 48 dB/oct is expensive
