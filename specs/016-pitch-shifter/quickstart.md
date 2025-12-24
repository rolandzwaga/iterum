# Quickstart: Pitch Shift Processor

**Feature**: 016-pitch-shifter
**Date**: 2025-12-24
**Related**: [spec.md](spec.md) | [contracts/pitch_shift_processor.h](contracts/pitch_shift_processor.h)

## Installation

Include the header in your DSP code:

```cpp
#include "dsp/processors/pitch_shift_processor.h"
```

## Basic Usage

### Simple Pitch Shifting

```cpp
#include "dsp/processors/pitch_shift_processor.h"

using namespace Iterum::DSP;

// Create and prepare the processor
PitchShiftProcessor shifter;
shifter.prepare(44100.0, 512);  // 44.1kHz, max 512 samples per block

// Shift up by a perfect fifth (7 semitones)
shifter.setSemitones(7.0f);

// Process audio (supports in-place)
float buffer[512];
shifter.process(buffer, buffer, 512);
```

### Selecting Quality Mode

```cpp
PitchShiftProcessor shifter;
shifter.prepare(44100.0, 512);

// Choose based on your use case:

// For zero-latency monitoring (audible artifacts acceptable)
shifter.setMode(PitchMode::Simple);

// For general use (good quality, low latency)
shifter.setMode(PitchMode::Granular);

// For highest quality (accepts higher latency)
shifter.setMode(PitchMode::PhaseVocoder);
```

### Fine Pitch Control with Cents

```cpp
PitchShiftProcessor shifter;
shifter.prepare(44100.0, 512);

// Shift up by 1.5 semitones (1 semitone + 50 cents)
shifter.setSemitones(1.0f);
shifter.setCents(50.0f);

// Or shift down by a quarter tone (50 cents)
shifter.setSemitones(0.0f);
shifter.setCents(-50.0f);

// Get the resulting pitch ratio
float ratio = shifter.getPitchRatio();
// ratio ≈ 0.9715 for -50 cents
```

### Vocal Processing with Formant Preservation

```cpp
PitchShiftProcessor shifter;
shifter.prepare(44100.0, 512);

// Use high-quality mode for vocals
shifter.setMode(PitchMode::PhaseVocoder);

// Enable formant preservation to avoid "chipmunk" effect
shifter.setFormantPreserve(true);

// Shift the pitch up by an octave
shifter.setSemitones(12.0f);

// Process vocal audio
shifter.process(input, output, numSamples);
// Voice sounds like the same person, just at a higher pitch
```

## Integration Patterns

### In VST3 Processor

```cpp
class MyProcessor : public Steinberg::Vst::AudioEffect {
    Iterum::DSP::PitchShiftProcessor pitchShifter_;

public:
    Steinberg::tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        pitchShifter_.prepare(setup.sampleRate, setup.maxSamplesPerBlock);
        return AudioEffect::setupProcessing(setup);
    }

    Steinberg::tresult PLUGIN_API process(ProcessData& data) override {
        if (data.numSamples == 0) return kResultOk;

        float* input = data.inputs[0].channelBuffers32[0];
        float* output = data.outputs[0].channelBuffers32[0];

        pitchShifter_.process(input, output, data.numSamples);

        return kResultOk;
    }
};
```

### Shimmer Delay Effect

```cpp
class ShimmerDelay {
    Iterum::DSP::PitchShiftProcessor shifter_;
    Iterum::DSP::DelayLine delay_;
    float feedback_ = 0.7f;

public:
    void prepare(double sampleRate, size_t maxBlockSize) {
        shifter_.prepare(sampleRate, maxBlockSize);
        shifter_.setMode(PitchMode::Granular);  // Good quality, acceptable latency
        shifter_.setSemitones(12.0f);           // Octave up for classic shimmer

        delay_.prepare(sampleRate, maxBlockSize, 2.0);  // 2 second max delay
    }

    void process(const float* input, float* output, size_t numSamples) {
        for (size_t i = 0; i < numSamples; ++i) {
            // Read from delay
            float delayedSample = delay_.read(delayTimeSeconds_);

            // Pitch shift the delayed sample
            float shiftedSample;
            shifter_.process(&delayedSample, &shiftedSample, 1);

            // Mix and write to delay (feedback path)
            float feedbackSample = input[i] + shiftedSample * feedback_;
            delay_.write(feedbackSample);

            // Output is dry + wet
            output[i] = input[i] * dryMix_ + shiftedSample * wetMix_;
        }
    }
};
```

### Stereo Processing

```cpp
// PitchShiftProcessor is mono - use two instances for stereo
class StereoPitchShifter {
    Iterum::DSP::PitchShiftProcessor shifterL_;
    Iterum::DSP::PitchShiftProcessor shifterR_;

public:
    void prepare(double sampleRate, size_t maxBlockSize) {
        shifterL_.prepare(sampleRate, maxBlockSize);
        shifterR_.prepare(sampleRate, maxBlockSize);
    }

    void setSemitones(float semitones) {
        shifterL_.setSemitones(semitones);
        shifterR_.setSemitones(semitones);
    }

    void setMode(PitchMode mode) {
        shifterL_.setMode(mode);
        shifterR_.setMode(mode);
    }

    void process(const float* inputL, const float* inputR,
                 float* outputL, float* outputR,
                 size_t numSamples) {
        shifterL_.process(inputL, outputL, numSamples);
        shifterR_.process(inputR, outputR, numSamples);
    }
};
```

### Latency Compensation

```cpp
PitchShiftProcessor shifter;
shifter.prepare(44100.0, 512);

// Report latency to host for proper timing compensation
size_t latency = shifter.getLatencySamples();

// Latency varies by mode:
// - Simple: 0 samples
// - Granular: ~2048 samples (~46ms)
// - PhaseVocoder: ~5120 samples (~116ms)

// When mode changes, update host latency reporting
void onModeChange(PitchMode newMode) {
    shifter.setMode(newMode);
    reportLatencyToHost(shifter.getLatencySamples());
}
```

### Real-Time Parameter Automation

```cpp
// Parameters are smoothed internally - safe to change rapidly
void processWithAutomation(const float* input, float* output,
                           size_t numSamples,
                           float targetSemitones) {
    // Set target - smoothing happens automatically
    shifter.setSemitones(targetSemitones);

    // Process - no clicks even with rapid changes
    shifter.process(input, output, numSamples);
}

// Example: pitch wobble effect
void processWithWobble(const float* input, float* output,
                       size_t numSamples, float phase) {
    float wobble = std::sin(phase) * 1.0f;  // +/- 1 semitone
    shifter.setSemitones(wobble);
    shifter.process(input, output, numSamples);
}
```

## Mode Selection Guide

| Use Case | Recommended Mode | Why |
|----------|------------------|-----|
| Live monitoring | Simple | Zero latency for real-time feedback |
| General mixing | Granular | Good quality, acceptable latency |
| Vocal tuning | PhaseVocoder + Formant | Best quality, formant preservation |
| Shimmer effects | Granular | Balance of quality and feedback stability |
| Sound design | PhaseVocoder | Maximum quality for rendered audio |
| Performance | Simple/Granular | Low CPU overhead |

## Common Pitfalls

### 1. Forgetting to call prepare()

```cpp
PitchShiftProcessor shifter;
// WRONG: process() before prepare()
shifter.process(input, output, 512);  // Undefined behavior!

// CORRECT: Always prepare first
shifter.prepare(44100.0, 512);
shifter.process(input, output, 512);
```

### 2. Formant preservation in Simple mode

```cpp
shifter.setMode(PitchMode::Simple);
shifter.setFormantPreserve(true);  // Has no effect in Simple mode!

// Formant preservation only works in Granular and PhaseVocoder
shifter.setMode(PitchMode::PhaseVocoder);
shifter.setFormantPreserve(true);  // Now it works
```

### 3. Ignoring latency differences

```cpp
// If you switch modes, update latency reporting!
shifter.setMode(PitchMode::Simple);
// Latency: 0 samples

shifter.setMode(PitchMode::PhaseVocoder);
// Latency: ~5120 samples - host needs to know!
reportLatencyToHost(shifter.getLatencySamples());
```

### 4. Processing more than maxBlockSize

```cpp
shifter.prepare(44100.0, 512);  // Max 512 samples

// WRONG: Processing more than prepared for
shifter.process(input, output, 1024);  // Buffer overflow!

// CORRECT: Process in chunks
for (size_t i = 0; i < 1024; i += 512) {
    shifter.process(input + i, output + i, 512);
}
```

## Performance Tips

1. **Choose mode wisely**: Simple mode uses significantly less CPU than PhaseVocoder
2. **Batch processing**: Process larger blocks when possible (fewer function calls)
3. **Avoid mode switching**: Changing modes mid-stream has overhead; set once and leave
4. **In-place is free**: Using same buffer for input/output has no penalty
5. **Reset sparingly**: Only call reset() when truly needed (song start, etc.)
