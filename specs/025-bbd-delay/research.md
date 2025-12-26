# Research: BBD Delay

**Feature**: 025-bbd-delay
**Date**: 2025-12-26

## Overview

Bucket-brigade device (BBD) delay emulation for vintage analog delay sounds. This document captures the technical research needed to implement authentic BBD behavior.

## BBD Physics

### How BBD Chips Work

A BBD chip consists of a chain of capacitors (buckets) that pass analog samples from one stage to the next on each clock pulse. Key characteristics:

1. **Clock Frequency Determines Everything**:
   - Delay time = Number of stages / Clock frequency
   - Bandwidth = Clock frequency / 2 (Nyquist limit)
   - This creates the signature "longer delay = darker sound" behavior

2. **Anti-Aliasing Required**:
   - Input signal must be filtered before entering the BBD
   - Filter cutoff must track clock frequency to prevent aliasing
   - This is the source of the limited bandwidth

3. **Sample-and-Hold Artifacts**:
   - Each stage samples and holds the voltage
   - Creates subtle "staircasing" at low clock rates
   - Contributes to the "lo-fi" character

### Clock/Bandwidth Relationship

For a 4096-stage chip (like MN3005):
```
clockFreq = numStages / delayTime
bandwidth = clockFreq / 2

At 20ms delay:
  clockFreq = 4096 / 0.020 = 204.8 kHz
  bandwidth = 102.4 kHz (capped to ~15 kHz by practical filters)

At 1000ms delay:
  clockFreq = 4096 / 1.0 = 4.096 kHz
  bandwidth = 2.048 kHz
```

This is why BBD delays sound darker at longer delay times - it's not just filtering, it's physics.

## BBD Chip Comparison

| Chip | Stages | Manufacturer | Notable Uses | Character |
|------|--------|--------------|--------------|-----------|
| **MN3005** | 4096 | Panasonic | EHX Memory Man, DM-2 | Widest bandwidth, lowest noise |
| **MN3007** | 1024 | Panasonic | Many budget pedals | Medium-dark, some noise |
| **MN3205** | 4096 | Panasonic | Budget delays | Darker, noisier than MN3005 |
| **SAD1024** | 1024 | Reticon | Early units | Most noise, limited bandwidth |

### Chip Characteristics for Era Selector

| Era | Bandwidth Factor | Noise Factor | Saturation |
|-----|-----------------|--------------|------------|
| MN3005 | 1.0 (reference) | 1.0 (reference) | Soft |
| MN3007 | 0.85 | 1.3 | Medium |
| MN3205 | 0.75 | 1.5 | Medium |
| SAD1024 | 0.6 | 2.0 | Harder |

## Compander System

### Purpose

BBD chips have limited dynamic range (~50-60dB). Companders improve this by:
1. **Compressing** the input signal before the BBD (reduces peaks)
2. **Expanding** the output after the BBD (restores dynamics)

### Artifacts

The compander introduces characteristic artifacts:
- **Attack softening**: Compressor reduces transient impact
- **Pumping/Breathing**: Expander creates audible gain modulation on decays
- **Noise modulation**: Noise floor rises and falls with signal level

### Implementation Approach

Simple envelope-follower-based gain modulation:
```cpp
// Compression stage (before BBD)
float envelope = envFollower.process(abs(input));
float compGain = 1.0f / (1.0f + compressionRatio * envelope);
float compressed = input * compGain;

// Expansion stage (after BBD)
float expandGain = 1.0f + expansionRatio * envelope;
float expanded = output * expandGain;
```

The Age parameter controls the intensity of these artifacts.

## Clock Noise

### Source

Real BBD chips exhibit clock feedthrough - the clock signal leaking into the audio path. This manifests as:
- High-frequency whine proportional to clock frequency
- More audible at longer delay times (lower clock = closer to audio range)

### Implementation

CharacterProcessor already has clock noise simulation via NoiseGenerator. Configure:
- Noise level scales with Age parameter
- Higher noise at longer delays (lower clock frequency)

## Modulation (Chorus Effect)

### Triangle LFO

Classic BBD delays use triangle (not sine) modulation for chorus:
- Linear ramps create consistent pitch deviation
- Rate typically 0.1-2 Hz for subtle chorusing, up to 10 Hz for vibrato
- Depth varies delay time by small percentage (creates pitch shift)

### Pitch Deviation

Modulating delay time creates pitch shift:
```
pitchRatio = 1 + (delayChange / delayTime) * modulationRate
```

For a 300ms delay with 5ms modulation depth at 1Hz:
- Pitch deviation ≈ ±0.8 cents (subtle chorusing)

## Reference Units

### Boss DM-2

- MN3005 chip (or MN3205 in later versions)
- Delay range: 20-300ms
- Warm, dark character
- Subtle modulation

### Electro-Harmonix Memory Man

- Dual MN3005 chips (cascaded for longer delay)
- Delay range: up to 550ms
- Wider bandwidth than most BBD delays
- Pronounced chorusing capability

### Roland Dimension D

- BBD-based chorus (not delay)
- Four preset modes with fixed modulation
- Reference for modulation behavior

## Existing Codebase Integration

### CharacterProcessor BBD Mode

The CharacterProcessor already has BBD mode with:
- Bandwidth limiting via lowpass filter (BBDBandwidth)
- Soft saturation
- Clock noise generation

BBDDelay configures CharacterProcessor parameters dynamically based on:
- Current delay time (affects bandwidth)
- Age parameter (affects noise, saturation)
- Era selection (affects all characteristics)

### ModulationMatrix for LFO Routing

Set up modulation route:
- Source: LFO (Triangle waveform)
- Destination: Delay time
- Depth: User Modulation control
- Rate: User Modulation Rate control

## Implementation Strategy

1. **Follow TapeDelay Pattern**: Same composition of Layer 3 components
2. **Configure CharacterProcessor**: Set BBD mode, update parameters per-sample for bandwidth tracking
3. **Add Era Selection**: Enum with characteristic multipliers
4. **Add Compander**: Simple envelope-based gain modulation (new internal class)
5. **Wire Modulation**: Use ModulationMatrix for triangle LFO → delay time

## Test Strategy

1. **Bandwidth Tracking**: Verify frequency response changes with delay time
2. **Era Differences**: Verify audible character differences between chip models
3. **Modulation**: Verify pitch variation occurs with modulation enabled
4. **Age/Compander**: Verify artifacts scale with Age parameter
5. **Self-Oscillation**: Verify controlled oscillation at >100% feedback
6. **Real-Time Safety**: Verify no allocations in process path
