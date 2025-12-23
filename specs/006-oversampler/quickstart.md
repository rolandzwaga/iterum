# Quickstart: Oversampler

**Feature**: 006-oversampler | **Layer**: 1 (DSP Primitive)

## Installation

Include the header:

```cpp
#include "dsp/primitives/oversampler.h"
```

## Basic Usage

### Stereo Saturation with 2x Oversampling

```cpp
#include "dsp/primitives/oversampler.h"
#include <cmath>

using namespace Iterum::DSP;

class MySaturator {
public:
    void prepare(double sampleRate, size_t maxBlockSize) {
        // Prepare oversampler with Standard quality (good tradeoff)
        oversampler_.prepare(sampleRate, maxBlockSize,
                            OversamplingQuality::Standard);

        // Report latency to host for PDC
        latency_ = oversampler_.getLatency();
    }

    void process(float* left, float* right, size_t numSamples) {
        // Process with oversampling - callback runs at 2x sample rate
        oversampler_.process(left, right, numSamples,
            [this](float* L, float* R, size_t n) {
                for (size_t i = 0; i < n; ++i) {
                    L[i] = std::tanh(L[i] * drive_);
                    R[i] = std::tanh(R[i] * drive_);
                }
            });
    }

    size_t getLatency() const { return latency_; }
    void setDrive(float drive) { drive_ = drive; }

private:
    Oversampler2x oversampler_;
    float drive_ = 1.0f;
    size_t latency_ = 0;
};
```

### Heavy Distortion with 4x Oversampling

```cpp
#include "dsp/primitives/oversampler.h"

using namespace Iterum::DSP;

// For aggressive distortion, use 4x to suppress more aliasing
Oversampler4x oversampler;

void prepare(double sampleRate, size_t maxBlockSize) {
    // High quality for mastering-grade transparency
    oversampler.prepare(sampleRate, maxBlockSize,
                       OversamplingQuality::High);
}

void process(float* left, float* right, size_t numSamples) {
    oversampler.process(left, right, numSamples,
        [](float* L, float* R, size_t n) {
            for (size_t i = 0; i < n; ++i) {
                // Hard clipping (generates lots of harmonics)
                L[i] = std::clamp(L[i] * 4.0f, -1.0f, 1.0f);
                R[i] = std::clamp(R[i] * 4.0f, -1.0f, 1.0f);
            }
        });
}
```

### Zero-Latency Mode for Live Monitoring

```cpp
#include "dsp/primitives/oversampler.h"

using namespace Iterum::DSP;

Oversampler2x oversampler;

void prepare(double sampleRate, size_t maxBlockSize) {
    // Economy quality = IIR filters = zero latency
    oversampler.prepare(sampleRate, maxBlockSize,
                       OversamplingQuality::Economy,
                       OversamplingMode::ZeroLatency);

    // Latency is 0
    assert(oversampler.getLatency() == 0);
}
```

## Quality Comparison

| Quality | Stopband | Latency (2x) | Latency (4x) | CPU | Use Case |
|---------|----------|--------------|--------------|-----|----------|
| Economy | ~48 dB | 0 | 0 | Lowest | Live monitoring, guitar amps |
| Standard | ~80 dB | 15 samples | 30 samples | Medium | Mixing, general use |
| High | ~100 dB | 31 samples | 62 samples | Highest | Mastering, critical listening |

## Manual Pipeline (Advanced)

For more control, use separate upsample/downsample calls:

```cpp
void processManual(float* buffer, size_t numSamples) {
    // Get internal buffer for zero-copy
    float* upsampled = oversampler.getOversampledBuffer(0);

    // Upsample
    oversampler.upsample(buffer, upsampled, numSamples, 0);

    // Apply multiple processing stages at oversampled rate
    const size_t oversampledSize = numSamples * 2;
    applyWaveshaper(upsampled, oversampledSize);
    applyFilter(upsampled, oversampledSize);
    applyCompressor(upsampled, oversampledSize);

    // Downsample back
    oversampler.downsample(upsampled, buffer, numSamples, 0);
}
```

## Reset on Transport Stop

```cpp
void onTransportStop() {
    // Clear filter state to prevent artifacts on restart
    oversampler.reset();
}
```

## Sample Rate Changes

```cpp
void onSampleRateChange(double newSampleRate) {
    // Re-prepare with new sample rate
    oversampler.prepare(newSampleRate, maxBlockSize_, quality_);

    // Update latency report to host
    reportLatency(oversampler.getLatency());
}
```

## Integration with VST3 Processor

```cpp
class MyProcessor : public Steinberg::Vst::AudioEffect {
public:
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        // Prepare oversampler in setupProcessing (NOT in process!)
        oversampler_.prepare(setup.sampleRate, setup.maxSamplesPerBlock,
                            OversamplingQuality::Standard);
        return AudioEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API process(ProcessData& data) override {
        // Safe to call - no allocations happen here
        float* left = data.outputs[0].channelBuffers32[0];
        float* right = data.outputs[0].channelBuffers32[1];

        oversampler_.process(left, right, data.numSamples,
            [this](float* L, float* R, size_t n) {
                applySaturation(L, R, n);
            });

        return kResultOk;
    }

    uint32 PLUGIN_API getLatencySamples() override {
        return static_cast<uint32>(oversampler_.getLatency());
    }

private:
    Oversampler2x oversampler_;
};
```

## Common Patterns

### DC Blocking After Asymmetric Saturation

```cpp
// If your saturation is asymmetric (different for +/-), add DC blocking
oversampler.process(left, right, numSamples,
    [this](float* L, float* R, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            // Asymmetric soft clipping
            L[i] = (L[i] > 0) ? std::tanh(L[i]) : std::tanh(L[i] * 0.5f) * 2.0f;
            R[i] = (R[i] > 0) ? std::tanh(R[i]) : std::tanh(R[i] * 0.5f) * 2.0f;
        }
    });

// Apply DC blocker AFTER oversampler (at base rate)
dcBlocker_.process(left, numSamples);
dcBlocker_.process(right, numSamples);
```

### Bypass When Disabled

```cpp
void process(float* left, float* right, size_t numSamples, bool enabled) {
    if (!enabled) {
        // No processing needed - audio passes through unchanged
        return;
    }

    oversampler_.process(left, right, numSamples, saturationCallback_);
}
```

## Performance Tips

1. **Choose appropriate quality**: Economy for live, Standard for mixing, High for mastering
2. **Process at native rate when possible**: Only use oversampling before nonlinearities
3. **Reuse the callback**: Avoid lambda captures that allocate
4. **Profile in Release builds**: Debug builds are not representative

## Troubleshooting

| Symptom | Cause | Solution |
|---------|-------|----------|
| Aliasing still audible | Quality too low | Use Standard or High |
| Phase issues when mixing | Using ZeroLatency mode | Switch to LinearPhase |
| High CPU usage | Using 4x with High quality | Reduce to 2x or Standard |
| Clicks on playback start | Filter state not cleared | Call reset() on transport stop |
| Buffer overrun crash | numSamples > maxBlockSize | Increase maxBlockSize in prepare() |
