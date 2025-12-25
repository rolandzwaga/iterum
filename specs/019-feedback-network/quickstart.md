# Quickstart: FeedbackNetwork

**Feature**: 019-feedback-network
**Date**: 2025-12-25

## Basic Usage

### Simple Feedback Delay

```cpp
#include "dsp/systems/feedback_network.h"

using namespace Iterum::DSP;

// Create and prepare
FeedbackNetwork network;
network.prepare(44100.0, 512, 2000.0f);  // 2 second max delay

// Configure
network.setDelayTimeMs(375.0f);    // 375ms delay
network.setFeedbackAmount(0.5f);   // 50% feedback (-6dB per repeat)

// Process (in audio callback)
void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) {
    network.process(left, right, numSamples, ctx);
}
```

### Tape-Style Warm Delay

```cpp
FeedbackNetwork network;
network.prepare(44100.0, 512, 2000.0f);

// Delay settings
network.setTimeMode(TimeMode::Synced);
network.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);
network.setFeedbackAmount(0.7f);  // 70% for long trails

// Tape-style filtering (high frequencies decay faster)
network.setFilterEnabled(true);
network.setFilterType(FilterType::Lowpass);
network.setFilterCutoff(3000.0f);  // Roll off above 3kHz
network.setFilterResonance(0.707f);

// Subtle tape saturation
network.setSaturationEnabled(true);
network.setSaturationType(SaturationType::Tape);
network.setSaturationDrive(6.0f);  // +6dB drive
```

### Self-Oscillating Delay

```cpp
FeedbackNetwork network;
network.prepare(44100.0, 512, 2000.0f);

// High feedback for self-oscillation
network.setDelayTimeMs(200.0f);
network.setFeedbackAmount(1.1f);  // 110% - builds up

// Saturation prevents runaway (essential!)
network.setSaturationEnabled(true);
network.setSaturationType(SaturationType::Tape);
network.setSaturationDrive(12.0f);  // Heavy limiting

// Filter shapes the oscillation
network.setFilterEnabled(true);
network.setFilterType(FilterType::Lowpass);
network.setFilterCutoff(2000.0f);  // Warmer oscillation
```

### Freeze Mode (Infinite Sustain)

```cpp
FeedbackNetwork network;
network.prepare(44100.0, 512, 2000.0f);
network.setDelayTimeMs(500.0f);
network.setFeedbackAmount(0.6f);

// Normal processing...

// User triggers freeze (e.g., button press)
network.setFreeze(true);
// Now: buffer loops forever, new input ignored

// User releases freeze
network.setFreeze(false);
// Now: returns to 60% feedback, input restored
```

### Stereo Ping-Pong Delay

```cpp
FeedbackNetwork network;
network.prepare(44100.0, 512, 2000.0f);

network.setDelayTimeMs(250.0f);
network.setFeedbackAmount(0.7f);

// Full cross-feedback for ping-pong
network.setCrossFeedbackAmount(1.0f);  // L→R, R→L

// Each repeat alternates:
// 1st repeat: opposite channel
// 2nd repeat: back to original
// 3rd repeat: opposite again
// etc.
```

### Mono-to-Stereo Spread

```cpp
FeedbackNetwork network;
network.prepare(44100.0, 512, 2000.0f);

network.setDelayTimeMs(300.0f);
network.setFeedbackAmount(0.65f);

// 50% cross-feedback spreads mono to stereo
network.setCrossFeedbackAmount(0.5f);

// Each repeat gets progressively more mono,
// but the alternation creates subtle width
```

## Integration Patterns

### In a VST Processor

```cpp
class Processor : public AudioEffect {
    FeedbackNetwork feedbackNetwork_;

public:
    tresult setupProcessing(ProcessSetup& setup) override {
        // Prepare with max delay from spec
        feedbackNetwork_.prepare(
            setup.sampleRate,
            setup.maxSamplesPerBlock,
            10000.0f  // 10 second max
        );
        return kResultOk;
    }

    tresult setActive(TBool state) override {
        if (!state) {
            feedbackNetwork_.reset();
        }
        return kResultOk;
    }

    tresult process(ProcessData& data) override {
        // Handle parameter changes
        if (auto* changes = data.inputParameterChanges) {
            processParameterChanges(changes);
        }

        // Build context from host
        BlockContext ctx = buildContext(data);

        // Process audio
        float* left = data.outputs[0].channelBuffers32[0];
        float* right = data.outputs[0].channelBuffers32[1];
        feedbackNetwork_.process(left, right, data.numSamples, ctx);

        return kResultOk;
    }

private:
    void processParameterChanges(IParameterChanges* changes) {
        // Map normalized parameters to FeedbackNetwork
        // Feedback: 0-1 normalized → 0-1.2 actual
        // Filter cutoff: 0-1 normalized → 20-20000 Hz (log scale)
        // etc.
    }
};
```

### Combined with Character Modes

```cpp
// FeedbackNetwork handles the core feedback loop.
// Character processing (wow/flutter, noise) would be in a
// separate CharacterProcessor that wraps FeedbackNetwork.

// Future: CharacterProcessor (021) will compose FeedbackNetwork (019)
// with additional tape/BBD character effects.
```

## Parameter Mapping Reference

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Delay Time | 0 - 10000 ms | 500 ms | Or tempo-synced |
| Feedback | 0% - 120% | 50% | >100% = self-oscillation |
| Filter Enable | bool | false | LP/HP/BP in feedback |
| Filter Cutoff | 20 - 20000 Hz | 8000 Hz | Log scale recommended |
| Filter Resonance | 0.1 - 10 | 0.707 | Higher = more resonant |
| Saturation Enable | bool | false | Tape/Tube/etc |
| Saturation Drive | 0 - 24 dB | 0 dB | Pre-saturation gain |
| Freeze | bool | false | Infinite sustain |
| Cross-Feedback | 0% - 100% | 0% | Stereo only |

## Common Recipes

| Effect | Feedback | Filter | Saturation | Cross |
|--------|----------|--------|------------|-------|
| Clean Digital | 50% | Off | Off | 0% |
| Warm Tape | 70% | LP 3kHz | Tape +6dB | 0% |
| Dark Dub | 80% | LP 1.5kHz | Tube +12dB | 0% |
| Self-Osc Drone | 110% | LP 2kHz | Tape +18dB | 0% |
| Classic Ping-Pong | 65% | Off | Off | 100% |
| Wide Spread | 60% | LP 5kHz | Off | 50% |
| Frozen Pad | 100% (freeze) | LP 4kHz | Tape +3dB | 0% |
