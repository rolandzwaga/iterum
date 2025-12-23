# Quickstart: Saturation Processor

**Feature**: 009-saturation-processor
**Date**: 2025-12-23

## Basic Usage

### Include Header

```cpp
#include "dsp/processors/saturation_processor.h"
using namespace Iterum::DSP;
```

### Initialize and Prepare

```cpp
SaturationProcessor saturator;

// Prepare with sample rate and max block size
// Call this in your audio setup, not in the process callback!
saturator.prepare(44100.0, 512);
```

### Basic Processing

```cpp
// In your audio callback
void processBlock(float* buffer, size_t numSamples) {
    saturator.process(buffer, numSamples);
}
```

## Examples

### Example 1: Warm Tape Saturation

Classic tape-style warmth with moderate drive.

```cpp
SaturationProcessor saturator;
saturator.prepare(44100.0, 512);

// Configure for warm tape sound
saturator.setType(SaturationType::Tape);
saturator.setInputGain(6.0f);   // +6 dB drive
saturator.setOutputGain(-3.0f); // -3 dB makeup (roughly compensate)
saturator.setMix(1.0f);         // 100% wet

// Process
saturator.process(buffer, numSamples);
```

### Example 2: Rich Tube Saturation

Tube-style saturation with enhanced even harmonics.

```cpp
SaturationProcessor saturator;
saturator.prepare(44100.0, 512);

// Configure for tube character
saturator.setType(SaturationType::Tube);
saturator.setInputGain(12.0f);  // +12 dB for noticeable coloring
saturator.setOutputGain(-6.0f); // Compensate for increased level
saturator.setMix(1.0f);

// Process
saturator.process(buffer, numSamples);
```

### Example 3: Parallel Saturation (New York Style)

Blend saturated signal with dry for punchy transients with added warmth.

```cpp
SaturationProcessor saturator;
saturator.prepare(44100.0, 512);

// Configure for parallel processing
saturator.setType(SaturationType::Tape);
saturator.setInputGain(18.0f);  // Heavy drive on wet signal
saturator.setOutputGain(0.0f);
saturator.setMix(0.3f);         // 30% wet, 70% dry - preserves transients

// Process - output is blended automatically
saturator.process(buffer, numSamples);
```

### Example 4: Aggressive Transistor Distortion

More aggressive character for guitars or synths.

```cpp
SaturationProcessor saturator;
saturator.prepare(44100.0, 512);

// Configure for aggressive sound
saturator.setType(SaturationType::Transistor);
saturator.setInputGain(18.0f);  // Heavy drive
saturator.setOutputGain(-9.0f); // Significant makeup reduction
saturator.setMix(1.0f);

// Process
saturator.process(buffer, numSamples);
```

### Example 5: Digital Hard Clip

Lo-fi digital distortion character.

```cpp
SaturationProcessor saturator;
saturator.prepare(44100.0, 512);

// Configure for harsh digital clip
saturator.setType(SaturationType::Digital);
saturator.setInputGain(12.0f);  // Drive into hard clipping
saturator.setOutputGain(-6.0f);
saturator.setMix(0.5f);         // Blend to tame harshness

// Process
saturator.process(buffer, numSamples);
```

### Example 6: Subtle Diode Warmth

Gentle "glue" effect on buses.

```cpp
SaturationProcessor saturator;
saturator.prepare(44100.0, 512);

// Configure for subtle coloring
saturator.setType(SaturationType::Diode);
saturator.setInputGain(3.0f);   // Gentle drive
saturator.setOutputGain(-1.0f);
saturator.setMix(1.0f);

// Process
saturator.process(buffer, numSamples);
```

### Example 7: Per-Sample Processing

For modular synthesis or sample-accurate automation.

```cpp
SaturationProcessor saturator;
saturator.prepare(44100.0, 1);  // Single sample block size

saturator.setType(SaturationType::Tape);
saturator.setInputGain(12.0f);

// Process sample by sample
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = saturator.processSample(buffer[i]);
}
```

### Example 8: Dynamic Type Switching

Change saturation type in real-time (e.g., via automation).

```cpp
SaturationProcessor saturator;
saturator.prepare(44100.0, 512);

// Types can be changed at any time without clicks
// (only gain/mix changes are smoothed, type is instant)
void onTypeChanged(int typeIndex) {
    saturator.setType(static_cast<SaturationType>(typeIndex));
}

void onDriveChanged(float driveDb) {
    saturator.setInputGain(driveDb);  // Smoothed automatically
}
```

### Example 9: Stereo Processing

Process stereo using two instances.

```cpp
SaturationProcessor saturatorL;
SaturationProcessor saturatorR;

void prepare(double sampleRate, size_t blockSize) {
    saturatorL.prepare(sampleRate, blockSize);
    saturatorR.prepare(sampleRate, blockSize);

    // Configure both channels the same
    auto configureChannel = [](SaturationProcessor& sat) {
        sat.setType(SaturationType::Tape);
        sat.setInputGain(12.0f);
        sat.setOutputGain(-6.0f);
        sat.setMix(1.0f);
    };
    configureChannel(saturatorL);
    configureChannel(saturatorR);
}

void process(float* left, float* right, size_t numSamples) {
    saturatorL.process(left, numSamples);
    saturatorR.process(right, numSamples);
}
```

### Example 10: Getting Latency for Delay Compensation

```cpp
SaturationProcessor saturator;
saturator.prepare(44100.0, 512);

// Report latency to host for PDC (Plugin Delay Compensation)
size_t latencySamples = saturator.getLatency();

// Typical value: depends on oversampler filter design
// Usually a few samples for 2x oversampling
```

## Testing Verification

### Verifying Harmonic Content (SC-001, SC-002)

```cpp
#include <catch2/catch_test_macros.hpp>
#include "dsp/processors/saturation_processor.h"
#include "dsp/primitives/fft.h"

TEST_CASE("Tape saturation produces odd harmonics", "[saturation][SC-001]") {
    SaturationProcessor sat;
    sat.prepare(44100.0, 8192);
    sat.setType(SaturationType::Tape);
    sat.setInputGain(12.0f);  // +12 dB drive

    // Generate 1kHz sine
    std::array<float, 8192> buffer;
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = std::sin(2.0f * M_PI * 1000.0f * i / 44100.0f);
    }

    sat.process(buffer.data(), buffer.size());

    // FFT analysis
    FFT fft(8192);
    auto spectrum = fft.computeMagnitude(buffer.data());

    float fundamental = spectrum[185];  // ~1kHz bin
    float thirdHarmonic = spectrum[555]; // ~3kHz bin

    float ratio = 20.0f * std::log10(thirdHarmonic / fundamental);
    REQUIRE(ratio > -40.0f);  // SC-001: 3rd harmonic > -40dB
}
```

### Verifying DC Blocking (SC-004)

```cpp
TEST_CASE("Tube saturation has no DC offset", "[saturation][SC-004]") {
    SaturationProcessor sat;
    sat.prepare(44100.0, 512);
    sat.setType(SaturationType::Tube);  // Asymmetric
    sat.setInputGain(12.0f);

    // Process 1 second of 1kHz sine
    std::vector<float> buffer(44100);
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = std::sin(2.0f * M_PI * 1000.0f * i / 44100.0f);
    }

    // Process in blocks
    for (size_t offset = 0; offset < buffer.size(); offset += 512) {
        size_t blockSize = std::min<size_t>(512, buffer.size() - offset);
        sat.process(buffer.data() + offset, blockSize);
    }

    // Calculate mean (DC offset)
    float sum = 0.0f;
    for (float s : buffer) sum += s;
    float mean = std::abs(sum / buffer.size());

    REQUIRE(mean < 0.001f);  // SC-004: DC offset < 0.001
}
```

## Common Patterns

### Reset on Transport Stop

```cpp
void onTransportStop() {
    saturator.reset();  // Clear filter states
}
```

### Sample Rate Change

```cpp
void onSampleRateChanged(double newSampleRate) {
    // Must re-prepare when sample rate changes
    saturator.prepare(newSampleRate, maxBlockSize_);
    // Settings are preserved
}
```

### Bypass Implementation

```cpp
// Option 1: Use mix = 0
saturator.setMix(0.0f);  // Full dry, saturation bypassed (efficient)

// Option 2: Skip processing entirely when bypassed
if (!bypassed_) {
    saturator.process(buffer, numSamples);
}
```

## Performance Notes

- CPU usage: < 0.5% per instance at 44.1kHz (mono)
- Latency: Typically 2-4 samples (oversampler dependent)
- Memory: ~1-2 KB per instance + oversampling buffer
- All methods are noexcept and allocation-free after prepare()
