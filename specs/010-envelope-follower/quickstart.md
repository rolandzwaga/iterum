# Quickstart: Envelope Follower

**Feature**: 010-envelope-follower
**Date**: 2025-12-23

## Basic Usage

```cpp
#include "dsp/processors/envelope_follower.h"

using namespace Iterum::DSP;

// Create envelope follower
EnvelopeFollower env;

// Initialize in plugin's setupProcessing()
void setupProcessing(double sampleRate, size_t maxBlockSize) {
    env.prepare(sampleRate, maxBlockSize);
    env.setMode(DetectionMode::RMS);
    env.setAttackTime(10.0f);   // 10ms attack
    env.setReleaseTime(100.0f); // 100ms release
}

// Process in audio callback
void processBlock(float* input, float* envelope, size_t numSamples) {
    env.process(input, envelope, numSamples);
}
```

## Detection Modes

### Amplitude Mode (Default)

Full-wave rectification with smoothing. Best for general-purpose envelope tracking.

```cpp
env.setMode(DetectionMode::Amplitude);
env.setAttackTime(5.0f);    // Fast attack
env.setReleaseTime(50.0f);  // Medium release
```

### RMS Mode

Smoother response, perceptually-meaningful level. Best for compressor/limiter sidechains.

```cpp
env.setMode(DetectionMode::RMS);
env.setAttackTime(10.0f);   // Medium attack
env.setReleaseTime(100.0f); // Slow release

// RMS of a 0dB sine will be ~0.707
```

### Peak Mode

Instant attack for transient capture. Best for limiters and gates.

```cpp
env.setMode(DetectionMode::Peak);
env.setAttackTime(0.1f);    // Near-instant (< 1 sample at 44.1kHz)
env.setReleaseTime(200.0f); // Hold peaks, then release
```

## Sidechain Filtering

Use highpass filter to prevent low frequencies from dominating envelope.

```cpp
// Enable sidechain filter
env.setSidechainEnabled(true);
env.setSidechainCutoff(100.0f);  // 100Hz highpass

// Bass-heavy material won't cause pumping
env.process(input, envelope, numSamples);
```

## Per-Sample Processing

For modular synthesis or sample-accurate use:

```cpp
for (size_t i = 0; i < numSamples; ++i) {
    float envelopeValue = env.processSample(input[i]);

    // Use envelope to control another parameter
    filter.setCutoff(baseFreq + envelopeValue * modAmount);
    output[i] = filter.processSample(input[i]);
}
```

## Reading Envelope Without Advancing

```cpp
// Get current value without processing new input
float currentLevel = env.getCurrentValue();

// Useful for UI metering
updateMeterDisplay(currentLevel);
```

## Common Use Cases

### Compressor Sidechain

```cpp
EnvelopeFollower sidechain;
sidechain.prepare(sampleRate, blockSize);
sidechain.setMode(DetectionMode::RMS);
sidechain.setAttackTime(10.0f);
sidechain.setReleaseTime(100.0f);
sidechain.setSidechainEnabled(true);
sidechain.setSidechainCutoff(80.0f);

// In process:
sidechain.process(input, envelopeBuffer, numSamples);
// envelopeBuffer now contains level for gain reduction calculation
```

### Gate Trigger

```cpp
EnvelopeFollower gate;
gate.prepare(sampleRate, blockSize);
gate.setMode(DetectionMode::Peak);
gate.setAttackTime(0.1f);    // Instant
gate.setReleaseTime(50.0f);  // Quick release

// In process:
for (size_t i = 0; i < numSamples; ++i) {
    float level = gate.processSample(input[i]);
    bool gateOpen = (level > threshold);
    output[i] = gateOpen ? input[i] : 0.0f;
}
```

### Envelope-Controlled Filter (Auto-Wah)

```cpp
EnvelopeFollower envFollow;
envFollow.prepare(sampleRate, blockSize);
envFollow.setMode(DetectionMode::Amplitude);
envFollow.setAttackTime(5.0f);
envFollow.setReleaseTime(200.0f);

// In process:
for (size_t i = 0; i < numSamples; ++i) {
    float env = envFollow.processSample(input[i]);
    float cutoff = minFreq + env * (maxFreq - minFreq);
    filter.setCutoff(cutoff);
    output[i] = filter.processSample(input[i]);
}
```

### Ducking Effect

```cpp
EnvelopeFollower ducker;
ducker.prepare(sampleRate, blockSize);
ducker.setMode(DetectionMode::RMS);
ducker.setAttackTime(5.0f);
ducker.setReleaseTime(300.0f);

// In process:
for (size_t i = 0; i < numSamples; ++i) {
    float sideLevel = ducker.processSample(sidechainInput[i]);
    float duckAmount = std::min(1.0f, sideLevel * sensitivity);
    float gain = 1.0f - duckAmount;  // Reduce gain when sidechain is loud
    output[i] = mainInput[i] * gain;
}
```

## Reset on Transport Stop

```cpp
void handleTransportStop() {
    env.reset();  // Clear envelope state
}
```

## Latency Compensation

```cpp
// Report latency to host for delay compensation
size_t latency = env.getLatency();
// Typically 0, but may be small if sidechain filter enabled
```

## Parameter Ranges

| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| Attack | 0.1 - 500 | 10 | ms |
| Release | 1 - 5000 | 100 | ms |
| Sidechain Cutoff | 20 - 500 | 80 | Hz |

## Test Scenarios

### SC-001: Time Constant Accuracy

```cpp
// Attack reaches 63% of target within attack time
env.setAttackTime(10.0f);
// Feed step input (0→1), measure time to reach 0.63
```

### SC-002: RMS Accuracy

```cpp
// RMS of 0dB sine should be ~0.707
env.setMode(DetectionMode::RMS);
// Process 1kHz sine at 1.0 amplitude
// Output should stabilize near 0.707 (within 1%)
```

### SC-003: Peak Capture

```cpp
// Peak mode must capture single-sample impulses
env.setMode(DetectionMode::Peak);
env.setAttackTime(0.1f);
// Send impulse [0, 0, 1.0, 0, 0]
// Output[2] should be 1.0 (peak captured)
```
