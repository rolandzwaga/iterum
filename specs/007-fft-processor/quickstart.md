# Quickstart: FFT Processor

**Feature**: 007-fft-processor
**Layer**: 1 (DSP Primitives)

This guide shows common usage patterns for the FFT Processor components.

---

## Basic FFT Usage

### Forward FFT (Time → Frequency)

```cpp
#include "dsp/primitives/fft.h"
using namespace Iterum::DSP;

// Create and prepare FFT
FFT fft;
fft.prepare(1024);  // 1024-point FFT

// Input: 1024 time-domain samples
std::array<float, 1024> input;
// ... fill with audio samples ...

// Output: 513 complex bins (N/2+1)
std::array<Complex, 513> spectrum;

// Perform forward FFT
fft.forward(input.data(), spectrum.data());

// Analyze spectrum
for (size_t k = 0; k < 513; ++k) {
    float magnitude = spectrum[k].magnitude();
    float phase = spectrum[k].phase();
    // ... process spectrum ...
}
```

### Inverse FFT (Frequency → Time)

```cpp
// Modify spectrum...
spectrum[100].real *= 0.5f;  // Attenuate bin 100
spectrum[100].imag *= 0.5f;

// Reconstruct time-domain signal
std::array<float, 1024> output;
fft.inverse(spectrum.data(), output.data());

// output now contains reconstructed audio
```

### Round-Trip Verification

```cpp
// Perfect reconstruction test
fft.forward(input.data(), spectrum.data());
fft.inverse(spectrum.data(), output.data());

// Compare: output should equal input within float precision
float maxError = 0.0f;
for (size_t i = 0; i < 1024; ++i) {
    float error = std::abs(output[i] - input[i]);
    maxError = std::max(maxError, error);
}
// Expected: maxError < 1e-5 (< 0.001%)
```

---

## Window Functions

### Generating Windows

```cpp
#include "dsp/core/window_functions.h"
using namespace Iterum::DSP;

// Allocating version (for initialization)
auto hannWindow = Window::generate(WindowType::Hann, 1024);

// Non-allocating version (for real-time reuse)
std::array<float, 1024> window;
Window::generateHann(window.data(), 1024);

// Kaiser window with custom beta
Window::generateKaiser(window.data(), 1024, 9.0f);  // 80dB stopband
```

### Window Types Comparison

```cpp
// Best for general STFT (COLA at 50%/75% overlap)
Window::generateHann(window.data(), size);     // Lowest sidelobe, widest main lobe

// Better sidelobe rejection
Window::generateHamming(window.data(), size);  // ~53dB vs Hann's ~43dB

// Best sidelobe rejection (COLA windows)
Window::generateBlackman(window.data(), size); // ~74dB stopband

// Configurable, best for spectral modification
Window::generateKaiser(window.data(), size, 9.0f);  // ~80dB, needs 90% overlap
```

### Verifying COLA Property

```cpp
// Check if window is COLA-compliant at 50% overlap
bool isCOLA = Window::verifyCOLA(
    window.data(),
    1024,      // window size
    512,       // hop size (50% overlap)
    1e-6f      // tolerance
);

if (!isCOLA) {
    // Window/overlap combination will not give perfect reconstruction
}
```

---

## SpectralBuffer Manipulation

### Magnitude/Phase Access

```cpp
#include "dsp/primitives/spectral_buffer.h"
using namespace Iterum::DSP;

SpectralBuffer spectrum;
spectrum.prepare(1024);  // N/2+1 = 513 bins

// After FFT fills the buffer...

// Read magnitude and phase
for (size_t bin = 0; bin < spectrum.numBins(); ++bin) {
    float mag = spectrum.getMagnitude(bin);
    float phase = spectrum.getPhase(bin);

    // Modify...
    spectrum.setMagnitude(bin, mag * 0.5f);  // Attenuate by 6dB
    // Phase preserved automatically
}
```

### Spectral Filtering (Zero Bins)

```cpp
// Zero out frequency range (brick-wall filter)
float sampleRate = 44100.0f;
float lowCutoffHz = 500.0f;
float highCutoffHz = 5000.0f;

size_t fftSize = 1024;
size_t lowBin = static_cast<size_t>(lowCutoffHz * fftSize / sampleRate);
size_t highBin = static_cast<size_t>(highCutoffHz * fftSize / sampleRate);

// Zero bins outside passband
for (size_t bin = 0; bin < lowBin; ++bin) {
    spectrum.setCartesian(bin, 0.0f, 0.0f);
}
for (size_t bin = highBin; bin < spectrum.numBins(); ++bin) {
    spectrum.setCartesian(bin, 0.0f, 0.0f);
}
```

### Phase Manipulation

```cpp
// Invert phase of a specific bin
size_t bin = 50;
float phase = spectrum.getPhase(bin);
spectrum.setPhase(bin, phase + 3.14159f);  // 180° shift

// Time-shift entire spectrum (phase rotation)
float sampleDelay = 100.0f;  // samples
for (size_t bin = 0; bin < spectrum.numBins(); ++bin) {
    float freq = static_cast<float>(bin) / fftSize;
    float phaseShift = -2.0f * 3.14159f * freq * sampleDelay;
    float currentPhase = spectrum.getPhase(bin);
    spectrum.setPhase(bin, currentPhase + phaseShift);
}
```

---

## STFT Streaming Processing

### Basic STFT Analysis

```cpp
#include "dsp/primitives/stft.h"
using namespace Iterum::DSP;

// Setup STFT with 75% overlap
STFT stft;
stft.prepare(
    1024,              // FFT size
    256,               // Hop size (1024/4 = 75% overlap)
    WindowType::Hann
);

SpectralBuffer spectrum;
spectrum.prepare(1024);

// In audio callback...
void processBlock(const float* input, size_t numSamples) {
    stft.pushSamples(input, numSamples);

    while (stft.canAnalyze()) {
        stft.analyze(spectrum);

        // Process spectrum here...
        // (e.g., spectral filtering, freeze, etc.)
    }
}
```

### Complete STFT → ISTFT Round-Trip

```cpp
STFT stft;
OverlapAdd ola;
SpectralBuffer spectrum;

// Configure for perfect reconstruction
size_t fftSize = 1024;
size_t hopSize = 256;  // 75% overlap

stft.prepare(fftSize, hopSize, WindowType::Hann);
ola.prepare(fftSize, hopSize);
spectrum.prepare(fftSize);

// Processing loop
void processBlock(const float* input, float* output, size_t numSamples) {
    // Feed input to STFT
    stft.pushSamples(input, numSamples);

    // Process all available frames
    while (stft.canAnalyze()) {
        stft.analyze(spectrum);

        // Optional: modify spectrum here
        // For perfect reconstruction, leave spectrum unchanged

        ola.synthesize(spectrum);
    }

    // Extract reconstructed output
    size_t available = ola.samplesAvailable();
    if (available >= numSamples) {
        ola.pullSamples(output, numSamples);
    }
}
```

### Spectral Freeze Effect

```cpp
SpectralBuffer frozenSpectrum;
frozenSpectrum.prepare(1024);
bool isFrozen = false;

void processBlock(const float* input, float* output, size_t numSamples) {
    stft.pushSamples(input, numSamples);

    while (stft.canAnalyze()) {
        if (!isFrozen) {
            stft.analyze(spectrum);

            // Copy current spectrum for freeze
            if (shouldFreeze) {
                std::memcpy(frozenSpectrum.data(), spectrum.data(),
                           spectrum.numBins() * sizeof(Complex));
                isFrozen = true;
            }
        }

        // Use frozen or live spectrum
        ola.synthesize(isFrozen ? frozenSpectrum : spectrum);
    }

    ola.pullSamples(output, numSamples);
}
```

---

## Common Patterns

### Frequency Bin Calculations

```cpp
// Convert frequency to bin index
size_t frequencyToBin(float freqHz, float sampleRate, size_t fftSize) {
    return static_cast<size_t>(freqHz * fftSize / sampleRate);
}

// Convert bin index to frequency
float binToFrequency(size_t bin, float sampleRate, size_t fftSize) {
    return static_cast<float>(bin) * sampleRate / fftSize;
}

// Example: Find bin for 1kHz at 44.1kHz, 1024-point FFT
size_t bin = frequencyToBin(1000.0f, 44100.0f, 1024);  // ≈ 23
float freq = binToFrequency(23, 44100.0f, 1024);       // ≈ 990 Hz
```

### Latency Compensation

```cpp
// Report latency to host
size_t getPluginLatency() {
    // STFT analysis latency
    return stft.fftSize();  // Minimum: must collect one full frame
}
```

### Reset on Transport Stop

```cpp
void onTransportStop() {
    stft.reset();
    ola.reset();
    spectrum.reset();
}
```

---

## Performance Tips

### Pre-allocate Everything

```cpp
// In prepare() - NOT in processBlock()
void prepare(double sampleRate, size_t maxBlockSize) {
    fft.prepare(1024);
    stft.prepare(1024, 256, WindowType::Hann);
    ola.prepare(1024, 256);
    spectrum.prepare(1024);
}
```

### Avoid Per-Sample FFT

```cpp
// BAD: FFT every sample
for (size_t i = 0; i < numSamples; ++i) {
    fft.forward(&input[i], spectrum.data());  // Inefficient!
}

// GOOD: Use STFT with appropriate hop size
stft.pushSamples(input, numSamples);
while (stft.canAnalyze()) {
    stft.analyze(spectrum);
    // ... process frame ...
}
```

### Choose Appropriate FFT Size

| Use Case | Recommended FFT Size | Frequency Resolution @ 44.1kHz |
|----------|---------------------|-------------------------------|
| Low latency | 256 | ~172 Hz |
| General use | 1024 | ~43 Hz |
| High resolution | 2048 | ~21 Hz |
| Pitch detection | 4096+ | ~11 Hz |

---

## Error Handling

All FFT methods are `noexcept` and handle edge cases gracefully:

```cpp
// Safe: clamped to valid range
spectrum.setMagnitude(999999, 1.0f);  // Clamped to last valid bin

// Safe: returns 0 for out-of-range
float mag = spectrum.getMagnitude(999999);  // Returns 0.0f

// Assertion in debug: catches programmer errors
fft.prepare(1000);  // ASSERT fails - not power of 2
```

---

## Integration with Other Primitives

### FFT + Oversampler (Anti-aliased Spectral Modification)

```cpp
#include "dsp/primitives/oversampler.h"

// For spectral effects with nonlinear processing:
// 1. Oversample input
// 2. STFT at oversampled rate
// 3. Modify spectrum (may introduce harmonics)
// 4. Downsample output

// Note: For linear spectral operations (filtering, freeze),
// oversampling is unnecessary.
```

### FFT + LFO (Modulated Spectral Effects)

```cpp
#include "dsp/primitives/lfo.h"

LFO lfo;
lfo.prepare(sampleRate);
lfo.setFrequency(0.5f);  // 0.5 Hz modulation

// In process loop:
float mod = lfo.process();  // [-1, +1]

// Modulate spectral parameters
float filterCutoff = 1000.0f + 500.0f * mod;  // 500-1500 Hz sweep
size_t cutoffBin = frequencyToBin(filterCutoff, sampleRate, fftSize);

// Apply modulated filter to spectrum...
```
