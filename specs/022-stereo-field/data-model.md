# Data Model: Stereo Field

**Feature**: 022-stereo-field
**Date**: 2025-12-25

## Entities

### StereoMode (Enumeration)

```cpp
enum class StereoMode : uint8_t {
    Mono,      // Sum L+R to mono, identical output on both channels
    Stereo,    // Independent L/R processing with optional ratio
    PingPong,  // Alternating L/R delays with cross-feedback
    DualMono,  // Same delay time, panned output
    MidSide    // M/S encoding with independent Mid/Side delays
};
```

### StereoField (Main Class)

**Purpose**: Layer 3 system component for stereo delay processing

**State**:
| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| mode_ | StereoMode | enum | Stereo | Active stereo mode |
| width_ | float | 0-200 | 100 | Stereo width in percent |
| pan_ | float | -100 to +100 | 0 | Output pan position |
| lrOffset_ | float | -50 to +50 | 0 | L/R timing offset in ms |
| lrRatio_ | float | 0.1-10.0 | 1.0 | L/R delay time ratio |
| sampleRate_ | double | >0 | 44100 | Current sample rate |

**Smoothed Parameters** (via OnePoleSmoother):
| Parameter | Smoothing Time | Reason |
|-----------|---------------|--------|
| width | 20ms | Prevent zipper noise (FR-017) |
| pan | 20ms | Prevent zipper noise (FR-017) |
| lrOffset | 20ms | Prevent clicks on offset change |
| lrRatio | 20ms | Prevent clicks on ratio change |
| modeCrossfade | 50ms | Smooth mode transitions (FR-003) |

**Composed Components**:
| Component | Type | Count | Purpose |
|-----------|------|-------|---------|
| delayL_ | DelayEngine | 1 | Left/Mid channel delay |
| delayR_ | DelayEngine | 1 | Right/Side channel delay |
| midSide_ | MidSideProcessor | 1 | M/S encoding and width |
| offsetDelayL_ | DelayLine | 1 | L/R offset (L channel) |
| offsetDelayR_ | DelayLine | 1 | L/R offset (R channel) |

## Validation Rules

| Parameter | Validation | FR Reference |
|-----------|------------|--------------|
| width | Clamp to [0, 200] | FR-012 |
| pan | Clamp to [-100, +100] | FR-013 |
| lrOffset | Clamp to [-50, +50] ms | FR-014 |
| lrRatio | Clamp to [0.1, 10.0] | FR-016 |
| input samples | Replace NaN with 0.0 | FR-019 |

## State Transitions

### Mode Transition

```
Any Mode ──setMode(newMode)──► Transition State ──50ms crossfade──► New Mode

During transition:
- previousMode_ runs in parallel with currentMode_
- Output = blend(previousOutput, currentOutput, crossfadeFactor)
- crossfadeFactor: 0.0 → 1.0 over 50ms
```

## Parameter Relationships

### Width Interaction with Modes

| Mode | Width Effect |
|------|--------------|
| Mono | Width ignored (output is always mono) |
| Stereo | Applied after L/R delays via MidSideProcessor |
| PingPong | Applied to ping-pong output |
| DualMono | Applied to panned output |
| MidSide | Controls Side scaling in M/S domain |

### L/R Ratio Interaction with Modes

| Mode | L/R Ratio Effect |
|------|------------------|
| Mono | Ignored (single delay) |
| Stereo | L delay = base × ratio, R delay = base |
| PingPong | L delay = base × ratio, R delay = base |
| DualMono | Ignored (same delay for both) |
| MidSide | Mid delay = base × ratio, Side delay = base |

### L/R Offset Interaction with Modes

| Mode | L/R Offset Effect |
|------|-------------------|
| Mono | Ignored (identical outputs) |
| Stereo | Applied after main delays |
| PingPong | Applied to alternating outputs |
| DualMono | Applied after panning |
| MidSide | Applied after M/S decode |
