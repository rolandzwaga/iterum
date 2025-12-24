# Quickstart: DelayEngine

**Feature**: 018-delay-engine
**Date**: 2025-12-25

## Overview

DelayEngine is a Layer 3 wrapper for DelayLine that adds time modes (free/synced), smooth parameter transitions, and dry/wet mixing.

## Include

```cpp
#include "dsp/systems/delay_engine.h"
```

## Basic Usage (Free Mode)

```cpp
using namespace Iterum::DSP;

// Create and prepare
DelayEngine delay;
delay.prepare(44100.0, 512, 2000.0f);  // 2 seconds max delay

// Configure
delay.setTimeMode(TimeMode::Free);
delay.setDelayTimeMs(250.0f);  // 250ms delay
delay.setMix(0.5f);            // 50% wet

// Create block context
BlockContext ctx;
ctx.sampleRate = 44100.0;
ctx.blockSize = 512;

// Process audio (in-place)
delay.process(buffer, numSamples, ctx);
```

## Tempo-Synced Mode

```cpp
using namespace Iterum::DSP;

DelayEngine delay;
delay.prepare(44100.0, 512, 2000.0f);

// Switch to synced mode
delay.setTimeMode(TimeMode::Synced);
delay.setNoteValue(NoteValue::Quarter, NoteModifier::Dotted);  // Dotted quarter
delay.setMix(0.7f);

// Block context with tempo
BlockContext ctx;
ctx.sampleRate = 44100.0;
ctx.tempoBPM = 120.0;  // 120 BPM -> dotted quarter = 750ms

delay.process(buffer, numSamples, ctx);
```

## Stereo Processing

```cpp
DelayEngine delay;
delay.prepare(44100.0, 512, 2000.0f);

delay.setTimeMode(TimeMode::Free);
delay.setDelayTimeMs(300.0f);
delay.setMix(0.5f);

// Process stereo (both channels get same delay)
delay.process(leftBuffer, rightBuffer, numSamples, ctx);
```

## Kill-Dry Mode (Parallel Processing)

Use when DelayEngine is on an aux send/return:

```cpp
DelayEngine auxDelay;
auxDelay.prepare(44100.0, 512, 2000.0f);

auxDelay.setDelayTimeMs(500.0f);
auxDelay.setMix(1.0f);      // Doesn't matter when kill-dry is on
auxDelay.setKillDry(true);  // Only output wet signal

// In aux processing:
auxDelay.process(auxBuffer, numSamples, ctx);
// auxBuffer now contains only delayed signal
```

## Changing Parameters During Playback

All parameter changes are smoothed automatically:

```cpp
// These won't cause clicks
delay.setDelayTimeMs(500.0f);  // Smooth transition
delay.setMix(0.8f);            // Smooth transition

// To check current smoothed value:
float currentDelay = delay.getCurrentDelayMs();
```

## VST3 Integration Pattern

```cpp
// In Processor::setupProcessing()
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    delay_.prepare(
        setup.sampleRate,
        setup.maxSamplesPerBlock,
        2000.0f  // 2 second max
    );
    return AudioEffect::setupProcessing(setup);
}

// In Processor::process()
tresult PLUGIN_API Processor::process(ProcessData& data) {
    // Build BlockContext from ProcessData
    BlockContext ctx;
    ctx.sampleRate = processSetup.sampleRate;
    ctx.blockSize = data.numSamples;

    if (data.processContext) {
        ctx.tempoBPM = data.processContext->tempo;
        ctx.isPlaying = (data.processContext->state & ProcessContext::kPlaying) != 0;
    }

    // Apply parameter changes
    if (auto* changes = data.inputParameterChanges) {
        // ... handle parameter changes
    }

    // Process audio
    float* left = data.outputs[0].channelBuffers32[0];
    float* right = data.outputs[0].channelBuffers32[1];
    delay_.process(left, right, data.numSamples, ctx);

    return kResultOk;
}
```

## Reset on Playback Start

```cpp
// Call when transport starts to clear old audio from buffer
if (transportJustStarted) {
    delay_.reset();
}
```

## Time Mode Reference

| TimeMode | Description | Use When |
|----------|-------------|----------|
| `Free` | Delay in milliseconds | User wants precise timing |
| `Synced` | Delay from NoteValue + tempo | User wants musical timing |

## NoteValue Reference

| NoteValue | Beats (at 4/4) | Example at 120 BPM |
|-----------|----------------|-------------------|
| `Whole` | 4.0 | 2000ms |
| `Half` | 2.0 | 1000ms |
| `Quarter` | 1.0 | 500ms |
| `Eighth` | 0.5 | 250ms |
| `Sixteenth` | 0.25 | 125ms |
| `ThirtySecond` | 0.125 | 62.5ms |

| NoteModifier | Multiplier | Example (Quarter at 120) |
|--------------|------------|-------------------------|
| `None` | 1.0x | 500ms |
| `Dotted` | 1.5x | 750ms |
| `Triplet` | 0.667x | 333ms |

## Common Patterns

### Slapback Echo
```cpp
delay.setTimeMode(TimeMode::Free);
delay.setDelayTimeMs(80.0f);   // 80-120ms typical
delay.setMix(0.3f);
```

### Rhythmic Delay
```cpp
delay.setTimeMode(TimeMode::Synced);
delay.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);
delay.setMix(0.4f);
```

### Ambient/Long Delay
```cpp
delay.setTimeMode(TimeMode::Synced);
delay.setNoteValue(NoteValue::Half);
delay.setMix(0.6f);
// Note: For feedback/repeats, use FeedbackNetwork (021)
```
