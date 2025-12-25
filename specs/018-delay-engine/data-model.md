# Data Model: DelayEngine

**Feature**: 018-delay-engine
**Date**: 2025-12-25
**Layer**: 3 (System Component)

## Enumerations

### TimeMode

Determines how delay time is specified.

```cpp
enum class TimeMode : uint8_t {
    Free,    // Delay time specified in milliseconds
    Synced   // Delay time calculated from NoteValue and host tempo
};
```

**Usage**:
- `Free`: User sets delay time directly (e.g., 250ms)
- `Synced`: User sets note value (e.g., Quarter), time calculated from BlockContext tempo

## Classes

### DelayEngine

Main wrapper class providing high-level delay functionality.

**Namespace**: `Iterum::DSP`

**Layer**: 3 (depends on Layer 0-1 only)

#### Public Interface

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `prepare` | `double sampleRate, size_t maxBlockSize, float maxDelayMs` | `void` | Initialize with sample rate and allocate buffers |
| `reset` | none | `void` | Clear all internal state to zero |
| `setTimeMode` | `TimeMode mode` | `void` | Switch between Free and Synced modes |
| `setDelayTimeMs` | `float ms` | `void` | Set delay time in milliseconds (Free mode) |
| `setNoteValue` | `NoteValue note, NoteModifier mod` | `void` | Set note value (Synced mode) |
| `setMix` | `float wetRatio` | `void` | Set dry/wet mix (0.0 = dry, 1.0 = wet) |
| `setKillDry` | `bool killDry` | `void` | Enable/disable kill-dry mode |
| `process` (mono) | `float* buffer, size_t numSamples, const BlockContext& ctx` | `void` | Process mono audio buffer |
| `process` (stereo) | `float* left, float* right, size_t numSamples, const BlockContext& ctx` | `void` | Process stereo audio buffers |
| `getCurrentDelayMs` | none | `float` | Get current smoothed delay time in ms |
| `getTimeMode` | none | `TimeMode` | Get current time mode |

#### Private State

| Member | Type | Description |
|--------|------|-------------|
| `delayLine_` | `DelayLine` | Core delay buffer (Layer 1) |
| `delayLineRight_` | `DelayLine` | Second delay line for stereo right channel |
| `delaySmoother_` | `OnePoleSmoother` | Smooths delay time changes |
| `mixSmoother_` | `OnePoleSmoother` | Smooths mix parameter changes |
| `timeMode_` | `TimeMode` | Current time mode (Free/Synced) |
| `delayTimeMs_` | `float` | Target delay time in ms (Free mode) |
| `noteValue_` | `NoteValue` | Note value for synced mode |
| `noteModifier_` | `NoteModifier` | Note modifier (None/Dotted/Triplet) |
| `mix_` | `float` | Target wet ratio (0.0-1.0) |
| `killDry_` | `bool` | Kill-dry mode flag |
| `sampleRate_` | `double` | Current sample rate |
| `maxDelayMs_` | `float` | Maximum delay time in ms |
| `maxBlockSize_` | `size_t` | Maximum block size |

#### Invariants

1. `delayLine_` is always prepared before first process() call
2. `delaySmoother_` and `mixSmoother_` are configured with same sample rate
3. `mix_` is always in range [0.0, 1.0]
4. `delayTimeMs_` is always in range [0.0, maxDelayMs_]
5. Delay time in samples never exceeds `delayLine_.maxDelaySamples()`

#### State Transitions

```text
                 ┌──────────────────┐
                 │   Uninitialized  │
                 └────────┬─────────┘
                          │ prepare()
                          ▼
                 ┌──────────────────┐
          ┌──────│     Ready        │◄─────┐
          │      └────────┬─────────┘      │
          │               │ process()      │ reset()
          │               ▼                │
          │      ┌──────────────────┐      │
          │      │   Processing     │──────┘
          │      └──────────────────┘
          │               │
          └───────────────┘
              prepare() again
```

## Relationships

```text
┌─────────────────────────────────────────────────────────────┐
│                    DelayEngine (Layer 3)                    │
├─────────────────────────────────────────────────────────────┤
│  Composes:                                                  │
│  ├── DelayLine (Layer 1) - 1 or 2 instances                 │
│  ├── OnePoleSmoother (Layer 1) - 2 instances                │
│  │                                                          │
│  References:                                                │
│  ├── BlockContext (Layer 0) - passed to process()           │
│  ├── NoteValue (Layer 0) - stored for synced mode           │
│  └── NoteModifier (Layer 0) - stored for synced mode        │
└─────────────────────────────────────────────────────────────┘
```

## Validation Rules

| Field | Validation | Action |
|-------|------------|--------|
| `delayTimeMs` | Must be >= 0 | Clamp to 0 |
| `delayTimeMs` | Must be <= maxDelayMs | Clamp to maxDelayMs |
| `delayTimeMs` | Must not be NaN | Reject, keep previous |
| `delayTimeMs` | Must not be Infinity | Clamp to maxDelayMs |
| `mix` | Must be in [0, 1] | Clamp to range |
| `sampleRate` | Must be > 0 | Ignore prepare() if invalid |
| `maxDelayMs` | Must be > 0 | Minimum 1ms |

## Memory Layout

Approximate size per instance:

| Component | Size (bytes) |
|-----------|--------------|
| DelayLine (mono, 1s @ 48kHz) | ~200KB |
| DelayLine (stereo, 1s @ 48kHz) | ~200KB |
| OnePoleSmoother x2 | ~40 |
| Configuration state | ~32 |
| **Total (mono)** | **~200KB** |
| **Total (stereo)** | **~400KB** |

Note: Most memory is in DelayLine buffer. For 10s max delay at 192kHz, buffer alone is ~7.7MB.
