# Data Model: Ping-Pong Delay Mode

**Feature Branch**: `027-ping-pong-delay`
**Date**: 2025-12-26

## Entities

### PingPongDelay

**Layer**: 4 (User Feature)
**Location**: `src/dsp/features/ping_pong_delay.h`

Main processor class for ping-pong delay effect.

#### Composed Components

| Component | Type | Layer | Purpose |
|-----------|------|-------|---------|
| delayLineL_ | DelayLine | 1 | Left channel delay buffer |
| delayLineR_ | DelayLine | 1 | Right channel delay buffer |
| lfoL_ | LFO | 1 | Left channel modulation |
| lfoR_ | LFO | 1 | Right channel modulation (90° phase offset) |
| limiter_ | DynamicsProcessor | 2 | Feedback limiting |
| timeSmoother_ | OnePoleSmoother | 1 | Base delay time smoothing |
| feedbackSmoother_ | OnePoleSmoother | 1 | Feedback amount smoothing |
| crossFeedbackSmoother_ | OnePoleSmoother | 1 | Cross-feedback smoothing |
| widthSmoother_ | OnePoleSmoother | 1 | Stereo width smoothing |
| mixSmoother_ | OnePoleSmoother | 1 | Dry/wet mix smoothing |
| outputLevelSmoother_ | OnePoleSmoother | 1 | Output level smoothing |
| modulationDepthSmoother_ | OnePoleSmoother | 1 | Modulation depth smoothing |

#### State Variables

| Variable | Type | Range | Default | Description |
|----------|------|-------|---------|-------------|
| sampleRate_ | double | 44100-192000 | 0.0 | Current sample rate |
| maxDelayMs_ | float | 1-10000 | 10000 | Maximum delay time |
| timeMode_ | TimeMode | enum | Free | Free or tempo-synced |
| noteValue_ | NoteValue | enum | Quarter | Tempo sync note value |
| noteModifier_ | NoteModifier | enum | None | Dotted/triplet modifier |
| lrRatio_ | LRRatio | enum | OneToOne | L/R timing ratio |
| delayTimeMs_ | float | 1-10000 | 500 | Base delay time in ms |
| feedback_ | float | 0-1.2 | 0.5 | Feedback amount |
| crossFeedback_ | float | 0-1.0 | 1.0 | Cross-feedback (ping-pong intensity) |
| width_ | float | 0-200 | 100 | Stereo width percentage |
| mix_ | float | 0-1.0 | 0.5 | Dry/wet mix |
| outputLevelDb_ | float | -inf to +12 | 0 | Output level in dB |
| modulationDepth_ | float | 0-1.0 | 0 | Modulation depth |
| modulationRate_ | float | 0.1-10 | 1.0 | Modulation rate in Hz |
| prepared_ | bool | - | false | Lifecycle state |

#### Public API

```cpp
// Lifecycle
void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
void reset() noexcept;

// Time configuration
void setTimeMode(TimeMode mode) noexcept;
void setDelayTimeMs(float ms) noexcept;
void setNoteValue(NoteValue note, NoteModifier mod) noexcept;
void setLRRatio(LRRatio ratio) noexcept;

// Feedback configuration
void setFeedback(float amount) noexcept;
void setCrossFeedback(float amount) noexcept;

// Stereo configuration
void setWidth(float widthPercent) noexcept;

// Modulation configuration
void setModulationDepth(float depth) noexcept;
void setModulationRate(float rateHz) noexcept;

// Output configuration
void setMix(float wetRatio) noexcept;
void setOutputLevel(float dB) noexcept;

// Processing
void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;

// Queries
[[nodiscard]] float getCurrentDelayMs() const noexcept;
[[nodiscard]] LRRatio getLRRatio() const noexcept;
[[nodiscard]] float getFeedback() const noexcept;
[[nodiscard]] float getCrossFeedback() const noexcept;
[[nodiscard]] float getWidth() const noexcept;
[[nodiscard]] bool isPrepared() const noexcept;
```

---

### LRRatio (Enumeration)

**Layer**: 4 (defined with PingPongDelay)
**Location**: `src/dsp/features/ping_pong_delay.h`

Preset L/R timing ratios for polyrhythmic ping-pong effects.

| Value | Ratio | L Multiplier | R Multiplier | Musical Use |
|-------|-------|--------------|--------------|-------------|
| OneToOne | 1:1 | 1.0 | 1.0 | Classic even ping-pong |
| TwoToOne | 2:1 | 1.0 | 0.5 | R is double speed |
| ThreeToTwo | 3:2 | 1.0 | 0.667 | Polyrhythmic triplet feel |
| FourToThree | 4:3 | 1.0 | 0.75 | Subtle polyrhythm |
| OneToTwo | 1:2 | 0.5 | 1.0 | L is double speed |
| TwoToThree | 2:3 | 0.667 | 1.0 | Inverse triplet feel |
| ThreeToFour | 3:4 | 0.75 | 1.0 | Inverse subtle polyrhythm |

#### Ratio Calculation Formula

```cpp
// Base time = user delay time (or tempo-synced time)
// Left delay = baseTimeMs * leftMultiplier
// Right delay = baseTimeMs * rightMultiplier

// Example: 2:1 ratio with 500ms base time
// Left = 500ms * 1.0 = 500ms
// Right = 500ms * 0.5 = 250ms
```

---

## State Transitions

### Lifecycle States

```
[Uninitialized] --prepare()--> [Prepared] --reset()--> [Prepared]
                                    |
                                    v
                               [Processing]
                                    |
                              --reset()-->
                                    |
                                    v
                               [Prepared]
```

### Processing Flow

```
Input L/R
    |
    v
[Store dry signal for mixing]
    |
    v
[Calculate L/R delay times from ratio]
    |
    v
[Apply modulation to delay times]
    |
    v
[Read from delay lines]
    |
    v
[Apply cross-feedback blend]
    |
    v
[Apply feedback limiting if > 100%]
    |
    v
[Write feedback + input to delay lines]
    |
    v
[Apply stereo width (M/S)]
    |
    v
[Mix dry/wet]
    |
    v
[Apply output level]
    |
    v
Output L/R
```

---

## Validation Rules

### Parameter Clamping (FR-026)

| Parameter | Min | Max | Invalid Handling |
|-----------|-----|-----|------------------|
| delayTimeMs | 1.0 | maxDelayMs_ | Clamp to range |
| feedback | 0.0 | 1.2 | Clamp to range |
| crossFeedback | 0.0 | 1.0 | Clamp to range |
| width | 0.0 | 200.0 | Clamp to range |
| mix | 0.0 | 1.0 | Clamp to range |
| outputLevelDb | -120.0 | 12.0 | Clamp to range |
| modulationDepth | 0.0 | 1.0 | Clamp to range |
| modulationRate | 0.1 | 10.0 | Clamp to range |
| NaN values | - | - | Replace with default |

### Smoothing Requirements (FR-026)

- All parameter changes use 20ms one-pole smoothing
- Delay time changes use crossfade to prevent clicks
- Mode/ratio changes reset delay lines to prevent artifacts
