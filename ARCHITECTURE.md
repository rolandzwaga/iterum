# Iterum Architecture

This document is the **living inventory** of all functional domains, components, and APIs in the Iterum project. It serves as the canonical reference when writing new specs to avoid duplication and ensure proper reuse.

> **Constitution Principle XIII**: Every spec implementation MUST update this document as a final task.

**Last Updated**: 2025-12-22 (001-db-conversion)

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
| **Namespace** | `VSTWork::DSP` |
| **Added** | 0.0.0 (initial) |

**Public API**:

```cpp
namespace VSTWork::DSP {
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

### Buffer Operations

| | |
|---|---|
| **Purpose** | Common buffer manipulation operations |
| **Location** | [src/dsp/dsp_utils.h](src/dsp/dsp_utils.h) |
| **Namespace** | `VSTWork::DSP` |
| **Added** | 0.0.0 (initial) |

**Public API**:

```cpp
namespace VSTWork::DSP {
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
| **Namespace** | `VSTWork::DSP` |
| **Added** | 0.0.0 (initial) |

**Public API**:

```cpp
namespace VSTWork::DSP {
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
VSTWork::DSP::OnePoleSmoother gainSmoother;
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
| **Namespace** | `VSTWork::DSP` |
| **Added** | 0.0.0 (initial) |

**Public API**:

```cpp
namespace VSTWork::DSP {
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
| **Namespace** | `VSTWork::DSP` |
| **Added** | 0.0.0 (initial) |

**Public API**:

```cpp
namespace VSTWork::DSP {
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
| Apply gain to buffer | `VSTWork::DSP::applyGain()` | dsp_utils.h |
| Mix two buffers | `VSTWork::DSP::mix()` | dsp_utils.h |
| Smooth parameter changes | `VSTWork::DSP::OnePoleSmoother` | dsp_utils.h |
| Hard limit output | `VSTWork::DSP::hardClip()` | dsp_utils.h |
| Add saturation | `VSTWork::DSP::softClip()` | dsp_utils.h |
| Measure RMS level | `VSTWork::DSP::calculateRMS()` | dsp_utils.h |
| Detect peak level | `VSTWork::DSP::findPeak()` | dsp_utils.h |
