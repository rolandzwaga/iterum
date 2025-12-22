# Iterum Architecture

This document is the **living inventory** of all functional domains, components, and APIs in the Iterum project. It serves as the canonical reference when writing new specs to avoid duplication and ensure proper reuse.

> **Constitution Principle XIII**: Every spec implementation MUST update this document as a final task.

**Last Updated**: 2025-12-22 (003-lfo)

---

## Layer Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    LAYER 4: USER FEATURES                   │
│  (Tape Mode, BBD Mode, Multi-Tap, Ping-Pong, Shimmer, etc.) │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 3: SYSTEM COMPONENTS                 │
│    (Delay Engine, Modulation Matrix, Feedback Network)      │
├─────────────────────────────────────────────────────────────┤
│                   LAYER 2: DSP PROCESSORS                   │
│  (Filters, Saturation, Pitch Shifter, Diffuser, Envelope)   │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 1: DSP PRIMITIVES                    │
│    (Delay Line, LFO, Biquad, Smoother, Oversampler)         │
├─────────────────────────────────────────────────────────────┤
│                    LAYER 0: CORE UTILITIES                  │
│      (Memory Pool, Lock-free Queue, Fast Math, SIMD)        │
└─────────────────────────────────────────────────────────────┘
```

**Dependency Rule**: Each layer can ONLY depend on layers below it. No circular dependencies.

---

## Layer 0: Core Utilities

Core utilities have **no dependencies** on higher layers. They provide fundamental building blocks used throughout the DSP stack.

### dB/Linear Conversion

| | |
|---|---|
| **Purpose** | Convert between decibels and linear gain values |
| **Location** | [src/dsp/core/db_utils.h](src/dsp/core/db_utils.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.1 (001-db-conversion) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Constants
    constexpr float kSilenceFloorDb = -144.0f;  // 24-bit dynamic range floor

    // Functions (all constexpr, noexcept, real-time safe)
    [[nodiscard]] constexpr float dbToGain(float dB) noexcept;
    [[nodiscard]] constexpr float gainToDb(float gain) noexcept;
}
```

**Behavior**:
- `dbToGain(0.0f)` → `1.0f` (unity gain)
- `dbToGain(-20.0f)` → `0.1f`
- `dbToGain(NaN)` → `0.0f` (safe fallback)
- `gainToDb(1.0f)` → `0.0f` (unity = 0 dB)
- `gainToDb(0.0f)` → `-144.0f` (silence floor)
- `gainToDb(negative or NaN)` → `-144.0f` (safe fallback)

**When to use**:
- Converting user-facing dB parameters to linear gain for processing
- Converting linear gain measurements to dB for display
- Compile-time lookup table initialization (constexpr)
- Any dB↔linear conversion in real-time audio code

**Example**:
```cpp
#include "dsp/core/db_utils.h"

// Runtime conversion
float gain = Iterum::DSP::dbToGain(volumeDb);
buffer[i] *= gain;

// Compile-time lookup table
constexpr std::array<float, 3> presets = {
    Iterum::DSP::dbToGain(-12.0f),  // Quiet
    Iterum::DSP::dbToGain(0.0f),    // Unity
    Iterum::DSP::dbToGain(6.0f)     // Boost
};
```

---

### Mathematical Constants

| | |
|---|---|
| **Purpose** | Common mathematical constants for DSP |
| **Location** | [src/dsp/dsp_utils.h](src/dsp/dsp_utils.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.0 (initial) |

**Public API**:

```cpp
namespace Iterum::DSP {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;
}
```

**When to use**:
- Oscillator frequency calculations
- Filter coefficient calculations
- Any trigonometric DSP operations

---

## Layer 1: DSP Primitives

DSP primitives depend only on Layer 0. They are the basic building blocks for higher-level processors.

### DelayLine

| | |
|---|---|
| **Purpose** | Real-time safe circular buffer delay line with fractional sample interpolation |
| **Location** | [src/dsp/primitives/delay_line.h](src/dsp/primitives/delay_line.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.2 (002-delay-line) |

**Public API**:

```cpp
namespace Iterum::DSP {
    class DelayLine {
    public:
        // Lifecycle (call before audio processing)
        void prepare(double sampleRate, float maxDelaySeconds) noexcept;
        void reset() noexcept;

        // Processing (real-time safe, O(1))
        void write(float sample) noexcept;
        [[nodiscard]] float read(size_t delaySamples) const noexcept;
        [[nodiscard]] float readLinear(float delaySamples) const noexcept;
        [[nodiscard]] float readAllpass(float delaySamples) noexcept;

        // Query
        [[nodiscard]] size_t maxDelaySamples() const noexcept;
        [[nodiscard]] double sampleRate() const noexcept;
    };

    // Utility
    [[nodiscard]] constexpr size_t nextPowerOf2(size_t n) noexcept;
}
```

**Behavior**:
- `prepare()` - Allocates buffer (power-of-2 sizing), must call before processing
- `reset()` - Clears buffer to silence without reallocation
- `write()` - Stores sample and advances write position
- `read(N)` - Returns sample written N samples ago (clamped to [0, maxDelay])
- `readLinear(N.f)` - Fractional delay with linear interpolation
- `readAllpass(N.f)` - Fractional delay with allpass interpolation (stateful)

**When to use**:

| Use Case | Method | Why |
|----------|--------|-----|
| Fixed integer delay | `read()` | Fastest, no interpolation |
| Modulated delay (chorus, flanger, vibrato) | `readLinear()` | Smooth fractional positions |
| Feedback loops with fractional delay | `readAllpass()` | Unity gain at all frequencies |

**Important**: Never use `readAllpass()` for modulated delays - the stateful filter causes artifacts when delay time changes. Use `readLinear()` for modulation.

**Example**:
```cpp
#include "dsp/primitives/delay_line.h"

Iterum::DSP::DelayLine delay;

// In prepare() - allocates memory
delay.prepare(44100.0, 1.0f);  // 1 second max delay

// In processBlock() - real-time safe
delay.write(inputSample);

// Fixed delay (simple echo)
float echo = delay.read(22050);  // 0.5 second delay

// Modulated delay (chorus with LFO)
float lfoDelay = 500.0f + 20.0f * lfoValue;  // 500±20 samples
float chorus = delay.readLinear(lfoDelay);

// Feedback network (fractional comb filter)
float comb = delay.readAllpass(100.5f);  // Fixed fractional delay
```

---

### LFO (Low Frequency Oscillator)

| | |
|---|---|
| **Purpose** | Wavetable-based oscillator for generating modulation signals |
| **Location** | [src/dsp/primitives/lfo.h](src/dsp/primitives/lfo.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.3 (003-lfo) |

**Public API**:

```cpp
namespace Iterum::DSP {
    enum class Waveform : uint8_t {
        Sine, Triangle, Sawtooth, Square, SampleHold, SmoothRandom
    };

    enum class NoteValue : uint8_t {
        Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond
    };

    enum class NoteModifier : uint8_t { None, Dotted, Triplet };

    class LFO {
    public:
        // Lifecycle (call before audio processing)
        void prepare(double sampleRate) noexcept;
        void reset() noexcept;

        // Processing (real-time safe, O(1) per sample)
        [[nodiscard]] float process() noexcept;
        void processBlock(float* output, size_t numSamples) noexcept;

        // Parameters
        void setWaveform(Waveform waveform) noexcept;
        void setFrequency(float hz) noexcept;           // [0.01, 20.0] Hz
        void setPhaseOffset(float degrees) noexcept;    // [0, 360)
        void setTempoSync(bool enabled) noexcept;
        void setTempo(float bpm) noexcept;              // [1, 999] BPM
        void setNoteValue(NoteValue value, NoteModifier mod = NoteModifier::None) noexcept;

        // Control
        void retrigger() noexcept;
        void setRetriggerEnabled(bool enabled) noexcept;

        // Query
        [[nodiscard]] Waveform waveform() const noexcept;
        [[nodiscard]] float frequency() const noexcept;
        [[nodiscard]] float phaseOffset() const noexcept;
        [[nodiscard]] bool tempoSyncEnabled() const noexcept;
        [[nodiscard]] bool retriggerEnabled() const noexcept;
        [[nodiscard]] double sampleRate() const noexcept;
    };
}
```

**Behavior**:
- `prepare()` - Generates wavetables (2048 samples each), must call before processing
- `reset()` - Resets phase to zero without regenerating wavetables
- `process()` - Returns single sample in [-1.0, +1.0] range
- `processBlock()` - Fills buffer with LFO output
- `retrigger()` - Resets phase to configured offset (if retrigger enabled)

**Waveform Shapes**:
- `Sine` - Smooth sinusoidal, starts at 0 (zero crossing)
- `Triangle` - Linear ramp 0→1→-1→0
- `Sawtooth` - Linear ramp -1→+1, instant reset
- `Square` - Binary +1/-1 alternation
- `SampleHold` - Random value held for each cycle
- `SmoothRandom` - Interpolated random, smooth transitions

**Tempo Sync Frequencies** (at 120 BPM):
| Note Value | Normal | Dotted | Triplet |
|------------|--------|--------|---------|
| Whole (1/1) | 0.5 Hz | 0.33 Hz | 0.75 Hz |
| Half (1/2) | 1 Hz | 0.67 Hz | 1.5 Hz |
| Quarter (1/4) | 2 Hz | 1.33 Hz | 3 Hz |
| Eighth (1/8) | 4 Hz | 2.67 Hz | 6 Hz |
| Sixteenth (1/16) | 8 Hz | 5.33 Hz | 12 Hz |
| ThirtySecond (1/32) | 16 Hz | 10.67 Hz | 24 Hz* |

*Frequencies above 20 Hz are clamped to maximum.

**When to use**:

| Use Case | Configuration |
|----------|---------------|
| Chorus modulation | Sine, 0.5-3 Hz, free-running |
| Tremolo | Sine/Triangle, tempo synced |
| Vibrato | Sine, 4-8 Hz |
| Stereo width | Two LFOs with 90° phase offset |
| Filter sweep | Triangle/Saw, tempo synced |
| Random modulation | SmoothRandom, slow rate |

**Example**:
```cpp
#include "dsp/primitives/lfo.h"

Iterum::DSP::LFO lfo;

// In prepare() - generates wavetables
lfo.prepare(44100.0);
lfo.setWaveform(Iterum::DSP::Waveform::Sine);
lfo.setFrequency(2.0f);  // 2 Hz

// In processBlock() - real-time safe
for (size_t i = 0; i < numSamples; ++i) {
    float mod = lfo.process();  // [-1, +1]
    // Use mod to modulate delay time, filter cutoff, etc.
}

// Tempo sync example
lfo.setTempoSync(true);
lfo.setTempo(120.0f);
lfo.setNoteValue(Iterum::DSP::NoteValue::Quarter,
                 Iterum::DSP::NoteModifier::Dotted);  // Dotted 1/4 at 120 BPM = 1.33 Hz
```

---

### Buffer Operations

| | |
|---|---|
| **Purpose** | Common buffer manipulation operations |
| **Location** | [src/dsp/dsp_utils.h](src/dsp/dsp_utils.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.0 (initial) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Apply gain to buffer in-place
    void applyGain(float* buffer, size_t numSamples, float gain) noexcept;

    // Copy buffer with gain applied
    void copyWithGain(const float* input, float* output,
                      size_t numSamples, float gain) noexcept;

    // Mix two buffers: output = a*gainA + b*gainB
    void mix(const float* a, float gainA,
             const float* b, float gainB,
             float* output, size_t numSamples) noexcept;

    // Clear buffer to zero
    void clear(float* buffer, size_t numSamples) noexcept;
}
```

**When to use**:
- Applying volume/gain to audio buffers
- Mixing multiple audio sources
- Clearing buffers between processing

---

### One-Pole Smoother

| | |
|---|---|
| **Purpose** | Parameter smoothing to prevent zipper noise |
| **Location** | [src/dsp/dsp_utils.h](src/dsp/dsp_utils.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.0 (initial) |

**Public API**:

```cpp
namespace Iterum::DSP {
    class OnePoleSmoother {
    public:
        void setTime(float timeSeconds, float sampleRate) noexcept;
        [[nodiscard]] float process(float input) noexcept;
        void reset(float value = 0.0f) noexcept;
        [[nodiscard]] float getValue() const noexcept;
    };
}
```

**When to use**:
- Smoothing parameter changes (volume, pan, filter cutoff)
- Preventing clicks/pops from sudden value changes
- Any time a parameter needs gradual transition

**Example**:
```cpp
Iterum::DSP::OnePoleSmoother gainSmoother;
gainSmoother.setTime(0.010f, sampleRate);  // 10ms smoothing

// In process loop
float smoothedGain = gainSmoother.process(targetGain);
```

---

### Clipping Functions

| | |
|---|---|
| **Purpose** | Hard and soft clipping/limiting |
| **Location** | [src/dsp/dsp_utils.h](src/dsp/dsp_utils.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.0 (initial) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Hard clip to [-1, 1]
    [[nodiscard]] constexpr float hardClip(float sample) noexcept;

    // Soft clip using tanh-like curve
    [[nodiscard]] float softClip(float sample) noexcept;
}
```

**When to use**:
- `hardClip`: Final output limiting, preventing digital overs
- `softClip`: Saturation effects, warm distortion, feedback limiting

---

### Analysis Functions

| | |
|---|---|
| **Purpose** | Audio buffer analysis (RMS, peak detection) |
| **Location** | [src/dsp/dsp_utils.h](src/dsp/dsp_utils.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.0 (initial) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Calculate RMS of buffer
    [[nodiscard]] float calculateRMS(const float* buffer, size_t numSamples) noexcept;

    // Find peak absolute value
    [[nodiscard]] float findPeak(const float* buffer, size_t numSamples) noexcept;
}
```

**When to use**:
- Level metering displays
- Automatic gain control
- Envelope following
- Peak detection for limiting

---

## Layer 2: DSP Processors

*No components yet. Future: Filters, Saturators, Pitch Shifters, Envelope Followers, Diffusers*

---

## Layer 3: System Components

*No components yet. Future: Delay Engine, Feedback Network, Modulation Matrix*

---

## Layer 4: User Features

*No components yet. Future: Tape Mode, BBD Mode, Shimmer Mode*

---

## Cross-Cutting Concerns

### Namespaces

| Namespace | Purpose | Layer |
|-----------|---------|-------|
| `Iterum::DSP` | New standardized DSP utilities | 0 |
| `VSTWork::DSP` | Legacy/existing DSP utilities | 0-1 |
| `Iterum::DSP::detail` | Implementation details (not public API) | 0 |

### Conventions

- All audio-thread functions are `noexcept`
- Pure functions use `[[nodiscard]]`
- Compile-time evaluable functions are `constexpr`
- Linear gain values are raw multipliers (1.0 = unity)
- dB values use -144 dB as silence floor (24-bit range)

### Real-Time Safety

All components in this document are **real-time safe** unless noted otherwise:
- No memory allocation
- No locks or blocking
- No exceptions
- No I/O operations

---

## Component Index

Quick lookup by functionality:

| Need to... | Use | Location |
|------------|-----|----------|
| Convert dB to linear gain | `Iterum::DSP::dbToGain()` | core/db_utils.h |
| Convert linear gain to dB | `Iterum::DSP::gainToDb()` | core/db_utils.h |
| Create delay line | `Iterum::DSP::DelayLine` | primitives/delay_line.h |
| Read fixed delay | `DelayLine::read()` | primitives/delay_line.h |
| Read modulated delay | `DelayLine::readLinear()` | primitives/delay_line.h |
| Fractional delay in feedback | `DelayLine::readAllpass()` | primitives/delay_line.h |
| Create LFO modulation | `Iterum::DSP::LFO` | primitives/lfo.h |
| Get LFO sample | `LFO::process()` | primitives/lfo.h |
| Set LFO waveform | `LFO::setWaveform()` | primitives/lfo.h |
| Enable tempo sync | `LFO::setTempoSync()` | primitives/lfo.h |
| Retrigger LFO | `LFO::retrigger()` | primitives/lfo.h |
| Apply gain to buffer | `Iterum::DSP::applyGain()` | dsp_utils.h |
| Mix two buffers | `Iterum::DSP::mix()` | dsp_utils.h |
| Smooth parameter changes | `Iterum::DSP::OnePoleSmoother` | dsp_utils.h |
| Hard limit output | `Iterum::DSP::hardClip()` | dsp_utils.h |
| Add saturation | `Iterum::DSP::softClip()` | dsp_utils.h |
| Measure RMS level | `Iterum::DSP::calculateRMS()` | dsp_utils.h |
| Detect peak level | `Iterum::DSP::findPeak()` | dsp_utils.h |
