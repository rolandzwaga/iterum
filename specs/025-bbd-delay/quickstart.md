# Quickstart: BBD Delay

**Feature**: 025-bbd-delay
**Layer**: 4 (User Feature)

## Overview

BBDDelay provides classic bucket-brigade device delay emulation with authentic vintage character. It composes Layer 3 components to recreate the sound of units like Boss DM-2, EHX Memory Man, and Roland Dimension D.

## Basic Usage

```cpp
#include "dsp/features/bbd_delay.h"

using namespace Iterum::DSP;

// Create and prepare
BBDDelay delay;
delay.prepare(44100.0, 512, 1000.0f);  // sampleRate, blockSize, maxDelayMs

// Configure basic settings
delay.setTime(300.0f);         // 300ms delay
delay.setFeedback(0.5f);       // 50% feedback
delay.setMix(0.5f);            // 50/50 dry/wet
delay.setModulation(0.3f);     // 30% modulation depth
delay.setModulationRate(0.5f); // 0.5 Hz rate
delay.setAge(0.2f);            // Slight vintage character
delay.setEra(BBDChipModel::MN3005);  // Memory Man chip

// In process callback
delay.process(left, right, numSamples);
```

## Parameters

### Time (FR-001 to FR-004)

Delay time in milliseconds [20ms, 1000ms].

```cpp
delay.setTime(500.0f);  // 500ms delay
float currentTime = delay.getTime();
```

**Unique Behavior**: Longer times result in darker sound (bandwidth tracking).

### Feedback (FR-005 to FR-008)

Feedback amount [0%, 120%]. Values above 100% create self-oscillation.

```cpp
delay.setFeedback(0.4f);  // 40% - natural decay
delay.setFeedback(1.1f);  // 110% - controlled self-oscillation
```

### Modulation (FR-009 to FR-013)

Triangle LFO modulation for chorus effect.

```cpp
delay.setModulation(0.5f);     // 50% depth
delay.setModulationRate(1.0f); // 1 Hz rate

// Disable modulation
delay.setModulation(0.0f);
```

### Age (FR-019 to FR-023)

Controls degradation artifacts [0%, 100%].

```cpp
delay.setAge(0.0f);   // Clean BBD character
delay.setAge(0.5f);   // Moderate vintage artifacts
delay.setAge(1.0f);   // Full vintage degradation
```

Affects:
- Clock noise level
- Additional bandwidth reduction
- Compander artifact intensity (pumping/breathing)

### Era (FR-024 to FR-029)

Selects BBD chip model for different character.

```cpp
delay.setEra(BBDChipModel::MN3005);   // High quality (Memory Man)
delay.setEra(BBDChipModel::MN3007);   // Medium-dark (common pedals)
delay.setEra(BBDChipModel::MN3205);   // Darker, noisier (budget)
delay.setEra(BBDChipModel::SAD1024);  // Most noise (early chip)
```

### Mix and Output

```cpp
delay.setMix(0.5f);           // 50% wet (dry/wet blend)
delay.setOutputLevel(0.0f);   // 0dB output (can boost/cut)
```

## Typical Presets

### Clean Analog Delay

```cpp
delay.setTime(300.0f);
delay.setFeedback(0.4f);
delay.setModulation(0.0f);  // No modulation
delay.setAge(0.1f);         // Minimal artifacts
delay.setEra(BBDChipModel::MN3005);
```

### Memory Man Clone

```cpp
delay.setTime(400.0f);
delay.setFeedback(0.5f);
delay.setModulation(0.3f);
delay.setModulationRate(0.5f);
delay.setAge(0.2f);
delay.setEra(BBDChipModel::MN3005);
```

### Dark Vintage Lo-Fi

```cpp
delay.setTime(500.0f);
delay.setFeedback(0.6f);
delay.setModulation(0.2f);
delay.setAge(0.8f);         // Heavy artifacts
delay.setEra(BBDChipModel::SAD1024);  // Noisiest chip
```

### Self-Oscillation Drone

```cpp
delay.setTime(200.0f);
delay.setFeedback(1.1f);    // Self-oscillating
delay.setModulation(0.4f);  // Pitch wobble
delay.setAge(0.3f);
```

## Key Behaviors

### Bandwidth Tracking

Longer delay times automatically reduce bandwidth:
- 20ms: ~15kHz bandwidth (bright)
- 1000ms: ~2.5kHz bandwidth (dark)

This is authentic BBD physics - no manual configuration needed.

### Compander Artifacts

When Age > 0, compander emulation adds:
- Softened attack transients
- "Breathing" effect on decays
- Signal-dependent noise modulation

### Era Affects Character

Each chip model has different:
- Base bandwidth (MN3005 = widest, SAD1024 = narrowest)
- Noise floor (MN3005 = quietest, SAD1024 = noisiest)
- Saturation character

## Thread Safety

- All setters are safe to call from any thread
- Parameters are smoothed to prevent clicks
- `process()` is noexcept and allocation-free

## Common Patterns

### Tempo-Synced Delay

```cpp
// Via BlockContext in process()
delay.setTimeMode(TimeMode::Synced);
delay.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);
```

### Freeze (Infinite Hold)

```cpp
delay.setFeedback(1.0f);  // 100% feedback
// Input continues to mix in - use mix control to fade input
```

### Kill Dry (Wet Only)

```cpp
delay.setMix(1.0f);  // 100% wet
```

## API Summary

| Method | Description |
|--------|-------------|
| `prepare(rate, block, maxDelay)` | Initialize for processing |
| `reset()` | Clear delay buffer |
| `process(L, R, n)` | Process stereo audio |
| `setTime(ms)` | Set delay time |
| `setFeedback(amt)` | Set feedback [0-1.2] |
| `setModulation(depth)` | Set mod depth [0-1] |
| `setModulationRate(hz)` | Set mod rate [0.1-10] |
| `setAge(amt)` | Set degradation [0-1] |
| `setEra(chip)` | Set chip model |
| `setMix(amt)` | Set dry/wet [0-1] |
| `setOutputLevel(dB)` | Set output gain |
