# Iterum Architecture

This document is the **living inventory** of all functional domains, components, and APIs in the Iterum project. It serves as the canonical reference when writing new specs to avoid duplication and ensure proper reuse.

> **Constitution Principle XIII**: Every spec implementation MUST update this document as a final task.

**Last Updated**: 2025-12-25 (022-stereo-field)

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
│ (Filters, Saturation, Dynamics, Envelope, Pitch, Diffuser)  │
├─────────────────────────────────────────────────────────────┤
│                  LAYER 1: DSP PRIMITIVES                    │
│  (Delay Line, LFO, Biquad, Smoother, Oversampler, FFT, STFT)│
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

### Window Functions

| | |
|---|---|
| **Purpose** | Window function generators for STFT analysis and spectral processing |
| **Location** | [src/dsp/core/window_functions.h](src/dsp/core/window_functions.h) |
| **Namespace** | `Iterum::DSP::Window` |
| **Added** | 0.0.7 (007-fft-processor) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Window type enumeration
    enum class WindowType : uint8_t {
        Hann,       // COLA at 50%/75% overlap
        Hamming,    // COLA at 50%/75% overlap
        Blackman,   // COLA at 50%/75% overlap
        Kaiser      // Requires ~90% overlap for COLA
    };

    // Constants
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;
    constexpr float kDefaultKaiserBeta = 9.0f;  // ~80dB sidelobe rejection
    constexpr float kDefaultCOLATolerance = 1e-6f;

    namespace Window {
        // Modified Bessel function (for Kaiser window)
        [[nodiscard]] float besselI0(float x) noexcept;

        // In-place window generators (real-time safe if buffer pre-allocated)
        void generateHann(float* output, size_t size) noexcept;
        void generateHamming(float* output, size_t size) noexcept;
        void generateBlackman(float* output, size_t size) noexcept;
        void generateKaiser(float* output, size_t size, float beta) noexcept;

        // COLA verification
        [[nodiscard]] bool verifyCOLA(const float* window, size_t size,
                                       size_t hopSize, float tolerance = 1e-6f) noexcept;

        // Factory function (allocates)
        [[nodiscard]] std::vector<float> generate(WindowType type, size_t size,
                                                   float kaiserBeta = 9.0f);
    }
}
```

**Behavior**:
- `generateHann()` - Periodic (DFT-even) variant: 0.5 - 0.5*cos(2πn/N)
- `generateHamming()` - Periodic variant: 0.54 - 0.46*cos(2πn/N)
- `generateBlackman()` - 3-term: 0.42 - 0.5*cos(2πn/N) + 0.08*cos(4πn/N)
- `generateKaiser()` - Adjustable beta parameter controls sidelobe rejection
- `verifyCOLA()` - Verifies overlapping windows sum to constant (required for perfect STFT reconstruction)
- `besselI0()` - Modified Bessel function of first kind, order 0 (for Kaiser computation)

**Window Properties**:

| Window | Sidelobes | Mainlobe Width | Best For |
|--------|-----------|----------------|----------|
| Hann | -31 dB | Medium | General STFT analysis |
| Hamming | -42 dB | Medium | Better sidelobe rejection |
| Blackman | -58 dB | Wide | Low-leakage analysis |
| Kaiser (β=9) | -80 dB | Adjustable | Precision spectral analysis |

**COLA Overlap Requirements**:

| Window | 50% Overlap | 75% Overlap | 90% Overlap |
|--------|-------------|-------------|-------------|
| Hann | ✓ Sum=1 | ✓ Sum=2 | ✓ Sum=4 |
| Hamming | ✓ Sum≈1.08 | ✓ | ✓ |
| Blackman | ✗ | ✓ | ✓ |
| Kaiser | ✗ | ✗ | ✓ |

**When to use**:

| Use Case | Window | Overlap |
|----------|--------|---------|
| General spectral analysis | Hann | 50% |
| Spectral modifications (pitch shift, time stretch) | Hann | 75% |
| Low-leakage frequency measurement | Blackman | 75% |
| Precision analysis with adjustable rejection | Kaiser | 90% |

**Example**:
```cpp
#include "dsp/core/window_functions.h"
using namespace Iterum::DSP;

// Generate Hann window
std::vector<float> window(1024);
Window::generateHann(window.data(), window.size());

// Verify COLA property
bool canReconstruct = Window::verifyCOLA(window.data(), 1024, 512);  // 50% overlap

// Factory usage
auto kaiser = Window::generate(WindowType::Kaiser, 1024, 12.0f);  // High rejection
```

---

### Xorshift32 PRNG

| | |
|---|---|
| **Purpose** | Fast, deterministic random number generator for audio noise generation |
| **Location** | [src/dsp/core/random.h](src/dsp/core/random.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.14 (013-noise-generator) |

**Public API**:

```cpp
namespace Iterum::DSP {
    class Xorshift32 {
    public:
        // Construction
        explicit constexpr Xorshift32(uint32_t seedValue = kDefaultSeed) noexcept;

        // Generation (O(1), no branching, SIMD-friendly)
        [[nodiscard]] constexpr uint32_t next() noexcept;        // Raw 32-bit value
        [[nodiscard]] constexpr float nextFloat() noexcept;      // [-1.0, 1.0]
        [[nodiscard]] constexpr float nextUnipolar() noexcept;   // [0.0, 1.0]

        // Reseeding
        constexpr void seed(uint32_t seedValue) noexcept;

        // Constants
        static constexpr uint32_t kDefaultSeed = 2463534242u;
    };
}
```

**Behavior**:
- `next()` - Returns raw 32-bit pseudorandom value (period: 2^32 - 1)
- `nextFloat()` - Returns bipolar float in [-1.0, 1.0] for audio noise
- `nextUnipolar()` - Returns unipolar float in [0.0, 1.0] for probability checks
- `seed(0)` - Automatically replaced with kDefaultSeed (zero state produces all zeros)
- Different instances with same seed produce identical sequences (deterministic)

**Algorithm**: Xorshift32 by George Marsaglia (2003)
```cpp
state ^= state << 13;
state ^= state >> 17;
state ^= state << 5;
```

**When to use**:

| Use Case | Method |
|----------|--------|
| White noise generation | `nextFloat()` |
| Click/crackle probability | `nextUnipolar()` < probability |
| Deterministic test sequences | Construct with fixed seed |
| Multiple uncorrelated streams | Use different seeds per instance |

**Example**:
```cpp
#include "dsp/core/random.h"
using namespace Iterum::DSP;

Xorshift32 rng{12345};  // Fixed seed for reproducibility

// In processBlock() - real-time safe
for (size_t i = 0; i < numSamples; ++i) {
    float whiteNoise = rng.nextFloat();  // [-1, 1]
    buffer[i] += whiteNoise * noiseLevel;
}

// Poisson-distributed events (clicks at 10 Hz)
float clickProb = 10.0f / sampleRate;
if (rng.nextUnipolar() < clickProb) {
    triggerClick();
}
```

---

### NoteValue Enums

| | |
|---|---|
| **Purpose** | Musical note duration constants for tempo-synced features |
| **Location** | [src/dsp/core/note_value.h](src/dsp/core/note_value.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.17 (017-layer0-utilities) |

**Public API**:

```cpp
namespace Iterum::DSP {
    enum class NoteValue : uint8_t {
        Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond
    };

    enum class NoteModifier : uint8_t {
        None, Dotted, Triplet
    };

    // Beats per note value (Quarter = 1.0 beat)
    constexpr std::array<float, 6> kBeatsPerNote = {
        4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f
    };

    // Get total beats including modifier
    [[nodiscard]] constexpr float getBeatsForNote(NoteValue value,
                                                   NoteModifier modifier = NoteModifier::None) noexcept;
}
```

**Behavior**:
- `getBeatsForNote(Quarter, None)` → `1.0f`
- `getBeatsForNote(Quarter, Dotted)` → `1.5f` (×1.5)
- `getBeatsForNote(Quarter, Triplet)` → `0.667f` (×2/3)

**When to use**:
- Tempo-synced delay time calculations
- LFO rate synchronization
- Any musical timing where note values are needed

---

### BlockContext

| | |
|---|---|
| **Purpose** | Per-block processing context (sample rate, tempo, transport) for tempo-synced DSP |
| **Location** | [src/dsp/core/block_context.h](src/dsp/core/block_context.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.17 (017-layer0-utilities) |

**Public API**:

```cpp
namespace Iterum::DSP {
    struct BlockContext {
        // Audio context
        double sampleRate = 44100.0;
        size_t blockSize = 512;

        // Tempo/transport context
        double tempoBPM = 120.0;
        int timeSignatureNumerator = 4;
        int timeSignatureDenominator = 4;
        bool isPlaying = false;
        int64_t transportPositionSamples = 0;

        // Tempo-to-samples conversion
        [[nodiscard]] constexpr double tempoToSamples(NoteValue noteValue,
                                                       NoteModifier modifier = NoteModifier::None) const noexcept;
    };
}
```

**Behavior**:
- `tempoToSamples(Quarter)` at 120 BPM, 44100 Hz → `22050.0` samples (0.5 seconds)
- `tempoToSamples(Eighth, Dotted)` → samples for dotted eighth note
- Default-constructible with sensible audio defaults

**When to use**:
- Layer 3 Delay Engine for tempo-synced delay times
- LFO tempo sync mode
- Any feature that needs tempo, transport, or sample rate context

**Example**:
```cpp
#include "dsp/core/block_context.h"
using namespace Iterum::DSP;

BlockContext ctx;
ctx.tempoBPM = 120.0;
ctx.sampleRate = 48000.0;

double delaySamples = ctx.tempoToSamples(NoteValue::Quarter);  // 24000 samples
```

---

### FastMath

| | |
|---|---|
| **Purpose** | Optimized approximations of transcendental functions for CPU-critical paths |
| **Location** | [src/dsp/core/fast_math.h](src/dsp/core/fast_math.h) |
| **Namespace** | `Iterum::DSP::FastMath` |
| **Added** | 0.0.17 (017-layer0-utilities) |

**Public API**:

```cpp
namespace Iterum::DSP::FastMath {
    // Fast hyperbolic tangent using Padé (5,4) approximation
    // ~3x faster than std::tanh (verified benchmark)
    [[nodiscard]] constexpr float fastTanh(float x) noexcept;
}
```

**Behavior**:
- `fastTanh(0.0f)` → `0.0f`
- `fastTanh(0.5f)` → `~0.462f` (within 0.05% of std::tanh)
- `fastTanh(3.5f+)` → `1.0f` (saturation)
- `fastTanh(NaN)` → `NaN`
- `fastTanh(±Infinity)` → `±1.0f`

**Performance** (Windows/MSVC Release, 1M samples × 10 iterations):

| Function | Time | Speedup vs std:: | Status |
|----------|------|------------------|--------|
| fastTanh | ~28ms | **~3x faster** | ✅ Recommended |

**Critical Finding - std:: vs Fast Approximations**:

| Function | Recommendation | Reason |
|----------|----------------|--------|
| **tanh** | Use `fastTanh()` | ~3x faster, ideal for saturation/waveshaping |
| **sin/cos** | Use `std::sin/cos` | MSVC uses SIMD/lookup tables, faster than polynomials |
| **exp** | Use `std::exp` | MSVC uses SIMD/lookup tables, faster than polynomials |

> **Why only fastTanh?** Benchmarking revealed MSVC's std::sin/cos/exp implementations are highly optimized (SIMD/lookup tables) and outperform pure C++ polynomial approximations. Only `tanh` benefits from a custom Padé approximation.

**When to use**:

| Use Case | Function | Why |
|----------|----------|-----|
| Saturation in feedback loops | `fastTanh()` | Called thousands of times per block, 3x speedup |
| Waveshaping/soft clipping | `fastTanh()` | Hot path optimization |
| LFO sine generation | `std::sin()` | MSVC is faster than polynomial approx |
| Filter coefficient calculation | `std::cos()` | MSVC is faster than polynomial approx |
| Envelope smoothing | `std::exp()` | MSVC is faster than polynomial approx |
| Compile-time tanh tables | `fastTanh()` | constexpr capable |
| Compile-time exp values | `Iterum::DSP::detail::constexprExp()` | In db_utils.h |

**Example**:
```cpp
#include "dsp/core/fast_math.h"
using namespace Iterum::DSP::FastMath;

// Saturation in feedback path (hot code)
for (size_t i = 0; i < numSamples; ++i) {
    feedback[i] = fastTanh(feedback[i] * drive);  // 3x faster than std::tanh
}

// Compile-time lookup table
constexpr std::array<float, 5> tanhTable = {
    fastTanh(-2.0f), fastTanh(-1.0f), fastTanh(0.0f),
    fastTanh(1.0f), fastTanh(2.0f)
};
```

---

### Interpolation Utilities

| | |
|---|---|
| **Purpose** | Standalone interpolation functions for fractional sample reading |
| **Location** | [src/dsp/core/interpolation.h](src/dsp/core/interpolation.h) |
| **Namespace** | `Iterum::DSP::Interpolation` |
| **Added** | 0.0.17 (017-layer0-utilities) |

**Public API**:

```cpp
namespace Iterum::DSP::Interpolation {
    // Linear interpolation (2-point)
    [[nodiscard]] constexpr float linearInterpolate(float y0, float y1, float t) noexcept;

    // Cubic Hermite interpolation (4-point, C1 continuous)
    [[nodiscard]] constexpr float cubicHermiteInterpolate(
        float y_minus1, float y0, float y1, float y2, float t) noexcept;

    // Lagrange interpolation (4-point, 3rd-order polynomial)
    [[nodiscard]] constexpr float lagrangeInterpolate(
        float y_minus1, float y0, float y1, float y2, float t) noexcept;
}
```

**Behavior**:
- `linearInterpolate(0, 1, 0.5)` → `0.5f`
- All functions return exact sample value when `t = 0` (returns y0)
- All functions are constexpr and noexcept

**Interpolation Quality Comparison**:

| Method | Points | Order | Continuity | Aliasing | Use Case |
|--------|--------|-------|------------|----------|----------|
| Linear | 2 | 1st | C0 | Higher | Modulated delay (chorus) |
| Cubic Hermite | 4 | 3rd | C1 | Low | Pitch shifting, resampling |
| Lagrange | 4 | 3rd | C0 | Lowest | High-quality transposition |

**When to use**:

| Use Case | Method | Why |
|----------|--------|-----|
| Modulated delay (chorus, flanger) | `linearInterpolate` | Fast, smooth for slow modulation |
| Pitch shifting | `cubicHermiteInterpolate` | C1 continuity reduces artifacts |
| High-quality resampling | `lagrangeInterpolate` | Lowest aliasing |
| Compile-time tables | Any (all constexpr) | Pre-compute interpolated values |

**Example**:
```cpp
#include "dsp/core/interpolation.h"
using namespace Iterum::DSP::Interpolation;

// Read fractional delay position
float delaySamples = 100.5f;
size_t idx = static_cast<size_t>(delaySamples);
float frac = delaySamples - idx;

// 4-point cubic Hermite
float sample = cubicHermiteInterpolate(
    buffer[idx - 1], buffer[idx], buffer[idx + 1], buffer[idx + 2], frac);
```

---

### Stereo Cross-Blend Utility

| | |
|---|---|
| **Purpose** | Stereo channel cross-blending for ping-pong delays and stereo field manipulation |
| **Location** | [src/dsp/core/stereo_utils.h](src/dsp/core/stereo_utils.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.19 (019-feedback-network) |

**Public API**:

```cpp
namespace Iterum::DSP {
    /// @brief Apply stereo cross-blend routing
    /// @param inL Left input sample
    /// @param inR Right input sample
    /// @param crossAmount Cross-blend amount [0.0, 1.0]
    /// @param outL Output: blended left sample
    /// @param outR Output: blended right sample
    constexpr void stereoCrossBlend(
        float inL, float inR,
        float crossAmount,
        float& outL, float& outR
    ) noexcept;
}
```

**Behavior**:
- `crossAmount = 0.0`: No cross (normal stereo, L→L, R→R)
- `crossAmount = 0.5`: Mono blend (both channels become (L+R)/2)
- `crossAmount = 1.0`: Full swap / ping-pong (L→R, R→L)

**Formula**:
```
outL = inL × (1 - crossAmount) + inR × crossAmount
outR = inR × (1 - crossAmount) + inL × crossAmount
```

**When to use**:

| Use Case | crossAmount | Effect |
|----------|-------------|--------|
| Normal stereo delay | 0.0 | Independent L/R feedback |
| Subtle stereo widening | 0.1-0.3 | Gentle cross-channel bleed |
| Mono-compatible delay | 0.5 | Feedback is mono-summed |
| Ping-pong delay | 1.0 | Full L↔R swap per repeat |

**Future Consumers**:
- 019-feedback-network: Cross-feedback routing (current)
- 022-stereo-field: Ping-pong mode (planned)
- 023-tap-manager: Per-tap stereo routing (planned)

**Example**:
```cpp
#include "dsp/core/stereo_utils.h"
using namespace Iterum::DSP;

// In feedback loop (per sample)
float feedbackL = delayedL;
float feedbackR = delayedR;

// Apply cross-feedback (50% creates ping-pong effect)
float crossedL, crossedR;
stereoCrossBlend(feedbackL, feedbackR, crossAmount, crossedL, crossedR);

// Continue processing with crossedL, crossedR
```

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

### Biquad Filter

| | |
|---|---|
| **Purpose** | Second-order IIR filter (TDF2) with 8 filter types, cascading, and smoothed coefficient updates |
| **Location** | [src/dsp/primitives/biquad.h](src/dsp/primitives/biquad.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.4 (004-biquad-filter) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Filter type enumeration
    enum class FilterType : uint8_t {
        Lowpass, Highpass, Bandpass, Notch,
        Allpass, LowShelf, HighShelf, Peak
    };

    // Filter coefficients (normalized: a0=1)
    struct BiquadCoefficients {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;  // Feedforward
        float a1 = 0.0f, a2 = 0.0f;              // Feedback

        [[nodiscard]] static constexpr BiquadCoefficients calculate(
            FilterType type, float freq, float Q, float gainDb, float sampleRate
        ) noexcept;

        [[nodiscard]] static constexpr BiquadCoefficients calculateConstexpr(
            FilterType type, float freq, float Q, float gainDb, float sampleRate
        ) noexcept;

        [[nodiscard]] bool isStable() const noexcept;
        [[nodiscard]] bool isBypass() const noexcept;
    };

    // Single biquad stage (12 dB/oct)
    class Biquad {
    public:
        void configure(FilterType type, float freq, float Q, float gainDb,
                       float sampleRate) noexcept;
        void setCoefficients(const BiquadCoefficients& coeffs) noexcept;
        [[nodiscard]] float process(float input) noexcept;
        void processBlock(float* buffer, size_t numSamples) noexcept;
        void reset() noexcept;
    };

    // Multi-stage cascade (N * 12 dB/oct)
    template<size_t NumStages>
    class BiquadCascade {
    public:
        void setButterworth(FilterType type, float freq, float sampleRate) noexcept;
        void setLinkwitzRiley(FilterType type, float freq, float sampleRate) noexcept;
        [[nodiscard]] float process(float input) noexcept;
        void processBlock(float* buffer, size_t numSamples) noexcept;
        void reset() noexcept;
        [[nodiscard]] static constexpr size_t numStages() noexcept;
        [[nodiscard]] static constexpr float slopeDbPerOctave() noexcept;
    };

    // Type aliases for common slopes
    using Biquad12dB = Biquad;               // 12 dB/oct (2-pole)
    using Biquad24dB = BiquadCascade<2>;     // 24 dB/oct (4-pole)
    using Biquad36dB = BiquadCascade<3>;     // 36 dB/oct (6-pole)
    using Biquad48dB = BiquadCascade<4>;     // 48 dB/oct (8-pole)

    // Click-free filter modulation
    class SmoothedBiquad {
    public:
        void setSmoothingTime(float milliseconds, float sampleRate) noexcept;
        void setTarget(FilterType type, float freq, float Q, float gainDb,
                       float sampleRate) noexcept;
        void snapToTarget() noexcept;
        [[nodiscard]] float process(float input) noexcept;
        void processBlock(float* buffer, size_t numSamples) noexcept;
        [[nodiscard]] bool isSmoothing() const noexcept;
        void reset() noexcept;
    };

    // Constants
    constexpr float kMinFilterFrequency = 1.0f;
    constexpr float kMinQ = 0.1f;
    constexpr float kMaxQ = 30.0f;
    constexpr float kButterworthQ = 0.7071067811865476f;

    // Q helper functions
    [[nodiscard]] constexpr float butterworthQ() noexcept;
    [[nodiscard]] constexpr float linkwitzRileyQ() noexcept;
}
```

**Filter Types**:

| Type | Description | Uses gainDb |
|------|-------------|-------------|
| `Lowpass` | 12 dB/oct rolloff above cutoff | No |
| `Highpass` | 12 dB/oct rolloff below cutoff | No |
| `Bandpass` | Peak at center, rolloff both sides | No |
| `Notch` | Null at center frequency | No |
| `Allpass` | Flat magnitude, phase shift only | No |
| `LowShelf` | Boost/cut below shelf frequency | Yes |
| `HighShelf` | Boost/cut above shelf frequency | Yes |
| `Peak` | Parametric EQ bell curve | Yes |

**Behavior**:
- `configure()` - Calculates and applies new coefficients (may click if called during audio)
- `process()` - TDF2 processing: y = b0*x + s0; s0 = b1*x - a1*y + s1; s1 = b2*x - a2*y
- `setButterworth()` - Maximally flat passband, configures all cascade stages with appropriate Q
- `setLinkwitzRiley()` - Sums to unity power at crossover (LP²+HP²=1), uses Butterworth Q per stage
- `SmoothedBiquad::setTarget()` - Sets target coefficients (smoothly transitions over time)
- `SmoothedBiquad::snapToTarget()` - Immediately applies target (use at init or after silence)
- Denormal flushing: State values below 1e-15 are flushed to zero (prevents CPU spikes)
- NaN protection: NaN input returns 0 and resets state

**When to use**:

| Use Case | Class | Configuration |
|----------|-------|---------------|
| Simple LP/HP filtering | `Biquad` | `configure()` once |
| Parametric EQ | `Biquad` | FilterType::Peak with gainDb |
| Steep crossover | `Biquad24dB` | `setLinkwitzRiley()` |
| Rumble removal | `Biquad48dB` | `setButterworth(Highpass, 30Hz)` |
| LFO filter sweep | `SmoothedBiquad` | Update `setTarget()` per block |
| Static compile-time EQ | `Biquad(coeffs)` | `BiquadCoefficients::calculateConstexpr()` |
| Feedback path filtering | `Biquad` | Configure once, process in loop |

**Example**:
```cpp
#include "dsp/primitives/biquad.h"

using namespace Iterum::DSP;

// Basic lowpass
Biquad lpf;
lpf.configure(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);
float out = lpf.process(input);

// Steep 24 dB/oct highpass
Biquad24dB hp;
hp.setButterworth(FilterType::Highpass, 80.0f, 44100.0f);
hp.processBlock(buffer, numSamples);

// Click-free filter modulation
SmoothedBiquad modFilter;
modFilter.setSmoothingTime(10.0f, 44100.0f);  // 10ms smoothing
modFilter.setTarget(FilterType::Lowpass, 1000.0f, butterworthQ(), 0.0f, 44100.0f);
modFilter.snapToTarget();

// In audio callback - smoothly modulate cutoff
float cutoff = baseCutoff + lfo.process() * modAmount;
modFilter.setTarget(FilterType::Lowpass, cutoff, butterworthQ(), 0.0f, 44100.0f);
modFilter.processBlock(buffer, numSamples);

// Compile-time coefficients
constexpr auto staticEQ = BiquadCoefficients::calculateConstexpr(
    FilterType::Peak, 3000.0f, 2.0f, 6.0f, 44100.0f);
Biquad eq(staticEQ);
```

---

### Oversampler

| | |
|---|---|
| **Purpose** | Upsampling/downsampling primitive for anti-aliased nonlinear processing |
| **Location** | [src/dsp/primitives/oversampler.h](src/dsp/primitives/oversampler.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.6 (006-oversampler) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Factor enumeration
    enum class OversamplingFactor : uint8_t {
        TwoX = 2,   // 44.1k -> 88.2k
        FourX = 4   // 44.1k -> 176.4k
    };

    // Quality levels (affects stopband rejection)
    enum class OversamplingQuality : uint8_t {
        Economy,   // ~48dB, IIR, 0 latency
        Standard,  // ~80dB, FIR, minimal latency
        High       // ~100dB, FIR, more latency
    };

    // Latency modes
    enum class OversamplingMode : uint8_t {
        ZeroLatency,  // IIR filters (minimum-phase)
        LinearPhase   // FIR filters (symmetric)
    };

    // Template class (Factor=2 or 4, NumChannels=1 or 2)
    template<size_t Factor = 2, size_t NumChannels = 2>
    class Oversampler {
    public:
        // Callback types
        using StereoCallback = std::function<void(float*, float*, size_t)>;
        using MonoCallback = std::function<void(float*, size_t)>;

        // Lifecycle (call before audio processing)
        void prepare(double sampleRate, size_t maxBlockSize,
                    OversamplingQuality quality = OversamplingQuality::Economy,
                    OversamplingMode mode = OversamplingMode::ZeroLatency) noexcept;
        void reset() noexcept;

        // Processing (real-time safe - callback runs at oversampled rate)
        void process(float* left, float* right, size_t numSamples,
                    const StereoCallback& callback) noexcept;
        void process(float* buffer, size_t numSamples,
                    const MonoCallback& callback) noexcept;

        // Low-level access (for manual pipeline)
        void upsample(const float* input, float* output, size_t numSamples, size_t channel = 0) noexcept;
        void downsample(const float* input, float* output, size_t numSamples, size_t channel = 0) noexcept;
        [[nodiscard]] float* getOversampledBuffer(size_t channel = 0) noexcept;

        // Query
        [[nodiscard]] size_t getFactor() const noexcept;
        [[nodiscard]] size_t getLatency() const noexcept;
        [[nodiscard]] bool isPrepared() const noexcept;
    };

    // Type aliases for common configurations
    using Oversampler2x = Oversampler<2, 2>;      // Stereo 2x
    using Oversampler4x = Oversampler<4, 2>;      // Stereo 4x
    using Oversampler2xMono = Oversampler<2, 1>;  // Mono 2x
    using Oversampler4xMono = Oversampler<4, 1>;  // Mono 4x
}
```

**Behavior**:
- `prepare()` - Allocates buffers, configures anti-aliasing filters (NOT real-time safe)
- `reset()` - Clears filter states without reallocation (call on transport stop)
- `process()` - Upsamples, calls callback at higher rate, downsamples back (real-time safe)
- `upsample()` / `downsample()` - Low-level access for custom pipelines
- Anti-aliasing filters prevent imaging artifacts during upsampling and aliasing during downsampling
- 4x oversampling is implemented as cascaded 2x stages for efficiency

**Quality Comparison**:

| Quality | Stopband | 2x Latency | 4x Latency | CPU | Use Case |
|---------|----------|------------|------------|-----|----------|
| Economy | ~48 dB | 0 | 0 | Lowest | Live monitoring, guitar amps |
| Standard | ~80 dB | ~15 samp | ~30 samp | Medium | Mixing, general use |
| High | ~100 dB | ~31 samp | ~62 samp | Highest | Mastering, critical listening |

**When to use**:

| Use Case | Configuration |
|----------|---------------|
| Subtle saturation (tape, tube) | `Oversampler2x` + Economy |
| Standard waveshaping | `Oversampler2x` + Standard |
| Heavy distortion (hard clipping) | `Oversampler4x` + Standard |
| Guitar amp simulation (live) | `Oversampler2x` + ZeroLatency |
| Mastering saturation | `Oversampler2x` + High + LinearPhase |

**Important**: Only use oversampling before nonlinear processing. Linear operations (filtering, delay) don't benefit from oversampling.

**Example**:
```cpp
#include "dsp/primitives/oversampler.h"
using namespace Iterum::DSP;

Oversampler2x oversampler;

// In prepare() - allocates memory
oversampler.prepare(44100.0, 512, OversamplingQuality::Standard);

// Report latency to host
size_t latency = oversampler.getLatency();

// In process() - real-time safe
oversampler.process(left, right, numSamples,
    [](float* L, float* R, size_t n) {
        // This callback runs at 2x sample rate (88.2kHz)
        for (size_t i = 0; i < n; ++i) {
            L[i] = std::tanh(L[i] * drive);
            R[i] = std::tanh(R[i] * drive);
        }
    });

// On transport stop
oversampler.reset();
```

---

### FFT (Fast Fourier Transform)

| | |
|---|---|
| **Purpose** | Radix-2 DIT FFT for real-to-complex and complex-to-real transforms |
| **Location** | [src/dsp/primitives/fft.h](src/dsp/primitives/fft.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.7 (007-fft-processor) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Complex number for FFT operations
    struct Complex {
        float real = 0.0f;
        float imag = 0.0f;

        // Arithmetic
        [[nodiscard]] constexpr Complex operator+(const Complex&) const noexcept;
        [[nodiscard]] constexpr Complex operator-(const Complex&) const noexcept;
        [[nodiscard]] constexpr Complex operator*(const Complex&) const noexcept;
        [[nodiscard]] constexpr Complex conjugate() const noexcept;

        // Polar
        [[nodiscard]] float magnitude() const noexcept;  // |z| = sqrt(re² + im²)
        [[nodiscard]] float phase() const noexcept;      // ∠z = atan2(im, re)
    };

    // Constants
    constexpr size_t kMinFFTSize = 256;
    constexpr size_t kMaxFFTSize = 8192;

    class FFT {
    public:
        // Lifecycle (call before audio processing)
        void prepare(size_t fftSize) noexcept;  // Power of 2, [256-8192]
        void reset() noexcept;

        // Processing (real-time safe, O(N log N))
        void forward(const float* input, Complex* output) noexcept;  // N real → N/2+1 complex
        void inverse(const Complex* input, float* output) noexcept;  // N/2+1 complex → N real

        // Query
        [[nodiscard]] size_t size() const noexcept;       // FFT size N
        [[nodiscard]] size_t numBins() const noexcept;    // Output bins: N/2+1
        [[nodiscard]] bool isPrepared() const noexcept;
    };
}
```

**Behavior**:
- `prepare()` - Generates bit-reversal LUT and precomputes twiddle factors (allocates memory)
- `forward()` - Real-to-complex FFT: N real samples → N/2+1 complex bins (DC to Nyquist)
- `inverse()` - Complex-to-real IFFT: N/2+1 complex bins → N real samples (normalized by 1/N)
- DC bin (index 0) and Nyquist bin (index N/2) always have zero imaginary component
- Negative frequency bins reconstructed from conjugate symmetry: X[N-k] = X[k]*

**Complexity**: O(N log N) for both forward and inverse transforms.

**Memory Footprint**: Working buffer uses 2N floats (within 3N float limit per NFR-003).

**When to use**:

| Use Case | Method |
|----------|--------|
| Analyze frequency content | `forward()` → examine bin magnitudes |
| Spectral filtering | `forward()` → modify bins → `inverse()` |
| Convolution (frequency domain) | `forward()` both signals → multiply → `inverse()` |
| Pitch detection | `forward()` → find peak bin |

**Example**:
```cpp
#include "dsp/primitives/fft.h"
using namespace Iterum::DSP;

FFT fft;

// In prepare() - allocates LUTs
fft.prepare(1024);

// In processBlock() - real-time safe
std::vector<Complex> spectrum(fft.numBins());  // 513 bins for N=1024
fft.forward(input, spectrum.data());

// Find dominant frequency bin
size_t peakBin = 0;
float maxMag = 0.0f;
for (size_t i = 1; i < fft.numBins() - 1; ++i) {
    float mag = spectrum[i].magnitude();
    if (mag > maxMag) { maxMag = mag; peakBin = i; }
}
float peakFreq = peakBin * sampleRate / fft.size();

// Round-trip reconstruction
std::vector<float> output(fft.size());
fft.inverse(spectrum.data(), output.data());
// output ≈ input (< 0.0001% error)
```

---

### SpectralBuffer

| | |
|---|---|
| **Purpose** | Complex spectrum storage with magnitude/phase manipulation for spectral effects |
| **Location** | [src/dsp/primitives/spectral_buffer.h](src/dsp/primitives/spectral_buffer.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.7 (007-fft-processor) |

**Public API**:

```cpp
namespace Iterum::DSP {
    class SpectralBuffer {
    public:
        // Lifecycle
        void prepare(size_t fftSize) noexcept;  // Allocates N/2+1 bins
        void reset() noexcept;                  // Clears all bins to zero

        // Polar access (magnitude/phase)
        [[nodiscard]] float getMagnitude(size_t bin) const noexcept;  // |X[k]|
        [[nodiscard]] float getPhase(size_t bin) const noexcept;      // ∠X[k] (radians)
        void setMagnitude(size_t bin, float magnitude) noexcept;      // Preserves phase
        void setPhase(size_t bin, float phase) noexcept;              // Preserves magnitude

        // Cartesian access
        [[nodiscard]] float getReal(size_t bin) const noexcept;
        [[nodiscard]] float getImag(size_t bin) const noexcept;
        void setCartesian(size_t bin, float real, float imag) noexcept;

        // Raw access (for FFT I/O)
        [[nodiscard]] Complex* data() noexcept;
        [[nodiscard]] const Complex* data() const noexcept;

        // Query
        [[nodiscard]] size_t numBins() const noexcept;  // N/2+1
        [[nodiscard]] bool isPrepared() const noexcept;
    };
}
```

**Behavior**:
- `setMagnitude()` - Scales the bin to new magnitude while preserving phase angle
- `setPhase()` - Rotates the bin to new phase while preserving magnitude
- Bounds checking: Out-of-range bin indices return 0 or are ignored
- Direct `data()` access for efficient FFT input/output operations

**When to use**:

| Use Case | Methods |
|----------|---------|
| Spectral gain (EQ) | `getMagnitude()` / `setMagnitude()` |
| Phase vocoder (pitch shift) | `getPhase()` / `setPhase()` |
| Spectral freeze | Copy buffer, reuse for subsequent frames |
| Spectral gating | Set bins below threshold to 0 |

**Example**:
```cpp
#include "dsp/primitives/spectral_buffer.h"
using namespace Iterum::DSP;

FFT fft;
SpectralBuffer spectrum;

fft.prepare(1024);
spectrum.prepare(1024);

// Analyze
fft.forward(input, spectrum.data());

// Apply spectral effect: boost 1kHz region
float binFreq = sampleRate / 1024.0f;  // ~43 Hz per bin at 44.1kHz
size_t targetBin = static_cast<size_t>(1000.0f / binFreq);
for (size_t i = targetBin - 2; i <= targetBin + 2; ++i) {
    float mag = spectrum.getMagnitude(i);
    spectrum.setMagnitude(i, mag * 2.0f);  // +6 dB boost
}

// Resynthesize
fft.inverse(spectrum.data(), output);
```

---

### STFT (Short-Time Fourier Transform)

| | |
|---|---|
| **Purpose** | Streaming spectral analysis with configurable windows and overlap |
| **Location** | [src/dsp/primitives/stft.h](src/dsp/primitives/stft.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.7 (007-fft-processor) |

**Public API**:

```cpp
namespace Iterum::DSP {
    class STFT {
    public:
        // Lifecycle (call before audio processing)
        void prepare(size_t fftSize, size_t hopSize,
                    WindowType window = WindowType::Hann,
                    float kaiserBeta = 9.0f) noexcept;
        void reset() noexcept;

        // Input (real-time safe)
        void pushSamples(const float* input, size_t numSamples) noexcept;

        // Analysis (real-time safe)
        [[nodiscard]] bool canAnalyze() const noexcept;
        void analyze(SpectralBuffer& output) noexcept;

        // Query
        [[nodiscard]] size_t fftSize() const noexcept;
        [[nodiscard]] size_t hopSize() const noexcept;
        [[nodiscard]] WindowType windowType() const noexcept;
        [[nodiscard]] size_t latency() const noexcept;  // = fftSize
        [[nodiscard]] bool isPrepared() const noexcept;
    };
}
```

**Behavior**:
- `prepare()` - Allocates circular input buffer, generates analysis window
- `pushSamples()` - Accumulates input samples in circular buffer
- `canAnalyze()` - Returns true when fftSize samples available
- `analyze()` - Extracts frame, applies window, performs FFT, consumes hopSize samples
- Continuous streaming: push small chunks, call analyze() when ready

**Latency**: One FFT frame (fftSize samples). For 1024 samples at 44.1kHz = ~23ms.

**When to use**:

| Use Case | Configuration |
|----------|---------------|
| Real-time spectral display | Hann, 50% overlap |
| Pitch shifting | Hann, 75% overlap |
| Time stretching | Hann, 75% overlap |
| Spectral freeze effect | Any window, analyze once |

**Example**:
```cpp
#include "dsp/primitives/stft.h"
using namespace Iterum::DSP;

STFT stft;
SpectralBuffer spectrum;

// In prepare()
stft.prepare(1024, 512, WindowType::Hann);  // 50% overlap
spectrum.prepare(1024);

// In processBlock() - streaming
stft.pushSamples(inputBuffer, numSamples);

while (stft.canAnalyze()) {
    stft.analyze(spectrum);

    // Process spectrum (e.g., spectral gate, EQ, freeze)
    for (size_t i = 0; i < spectrum.numBins(); ++i) {
        if (spectrum.getMagnitude(i) < threshold) {
            spectrum.setCartesian(i, 0.0f, 0.0f);  // Gate
        }
    }

    // Continue to synthesis...
}
```

---

### OverlapAdd

| | |
|---|---|
| **Purpose** | Overlap-add synthesis for artifact-free STFT reconstruction |
| **Location** | [src/dsp/primitives/stft.h](src/dsp/primitives/stft.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.7 (007-fft-processor) |

**Public API**:

```cpp
namespace Iterum::DSP {
    class OverlapAdd {
    public:
        // Lifecycle (call before audio processing)
        void prepare(size_t fftSize, size_t hopSize,
                    WindowType window = WindowType::Hann,
                    float kaiserBeta = 9.0f) noexcept;
        void reset() noexcept;

        // Synthesis (real-time safe)
        void synthesize(const SpectralBuffer& input) noexcept;

        // Output (real-time safe)
        [[nodiscard]] size_t samplesAvailable() const noexcept;
        void pullSamples(float* output, size_t numSamples) noexcept;

        // Query
        [[nodiscard]] size_t fftSize() const noexcept;
        [[nodiscard]] size_t hopSize() const noexcept;
        [[nodiscard]] bool isPrepared() const noexcept;
    };
}
```

**Behavior**:
- `prepare()` - Allocates output accumulator, computes COLA normalization factor
- `synthesize()` - IFFT of spectrum, adds to accumulator with proper overlap
- `pullSamples()` - Extracts output samples, shifts accumulator buffer
- Automatic COLA normalization: Divides by window sum for unity-gain reconstruction

**Important**: Window type in OverlapAdd MUST match the STFT analysis window for perfect reconstruction.

**Round-Trip Accuracy**: STFT→OverlapAdd < 0.01% error when windows match and COLA is satisfied.

**When to use**: Always paired with STFT for spectral processing that requires resynthesis.

**Example**:
```cpp
#include "dsp/primitives/stft.h"
using namespace Iterum::DSP;

STFT stft;
OverlapAdd ola;
SpectralBuffer spectrum;

// In prepare() - MUST use same window!
stft.prepare(1024, 512, WindowType::Hann);
ola.prepare(1024, 512, WindowType::Hann);
spectrum.prepare(1024);

// Report latency to host
latency = stft.latency();  // = fftSize

// In processBlock() - full STFT pipeline
stft.pushSamples(input, numSamples);

while (stft.canAnalyze()) {
    stft.analyze(spectrum);

    // Spectral processing (e.g., apply EQ, freeze, morph)
    // ... modify spectrum ...

    ola.synthesize(spectrum);
}

// Extract output
if (ola.samplesAvailable() >= numSamples) {
    ola.pullSamples(output, numSamples);
}
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

DSP Processors compose Layer 1 primitives into higher-level processing modules.

### MultimodeFilter

| | |
|---|---|
| **Purpose** | Complete filter module with 8 filter types, selectable slopes, coefficient smoothing, and optional drive |
| **Location** | [src/dsp/processors/multimode_filter.h](src/dsp/processors/multimode_filter.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.8 (008-multimode-filter) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Filter slope selection (LP/HP/BP/Notch only)
    enum class FilterSlope : uint8_t {
        Slope12dB = 1,  // 12 dB/oct (1 biquad stage)
        Slope24dB = 2,  // 24 dB/oct (2 biquad stages)
        Slope36dB = 3,  // 36 dB/oct (3 biquad stages)
        Slope48dB = 4   // 48 dB/oct (4 biquad stages)
    };

    // Utility functions
    [[nodiscard]] constexpr size_t slopeToStages(FilterSlope slope) noexcept;
    [[nodiscard]] constexpr float slopeTodBPerOctave(FilterSlope slope) noexcept;

    class MultimodeFilter {
    public:
        // Constants
        static constexpr float kMinCutoff = 20.0f;
        static constexpr float kMinQ = 0.1f;
        static constexpr float kMaxQ = 100.0f;
        static constexpr float kMinGain = -24.0f;
        static constexpr float kMaxGain = 24.0f;
        static constexpr float kMinDrive = 0.0f;
        static constexpr float kMaxDrive = 24.0f;
        static constexpr float kDefaultSmoothingMs = 5.0f;

        // Lifecycle (call before audio processing)
        void prepare(double sampleRate, size_t maxBlockSize) noexcept;
        void reset() noexcept;

        // Processing (real-time safe)
        void process(float* buffer, size_t numSamples) noexcept;
        [[nodiscard]] float processSample(float input) noexcept;

        // Parameter setters (real-time safe)
        void setType(FilterType type) noexcept;
        void setSlope(FilterSlope slope) noexcept;  // LP/HP/BP/Notch only
        void setCutoff(float hz) noexcept;          // [20, Nyquist/2]
        void setResonance(float q) noexcept;        // [0.1, 100]
        void setGain(float dB) noexcept;            // [-24, +24] Shelf/Peak only
        void setDrive(float dB) noexcept;           // [0, 24] pre-filter saturation
        void setSmoothingTime(float ms) noexcept;

        // Parameter getters
        [[nodiscard]] FilterType getType() const noexcept;
        [[nodiscard]] FilterSlope getSlope() const noexcept;
        [[nodiscard]] float getCutoff() const noexcept;
        [[nodiscard]] float getResonance() const noexcept;
        [[nodiscard]] float getGain() const noexcept;
        [[nodiscard]] float getDrive() const noexcept;

        // Query
        [[nodiscard]] size_t getLatency() const noexcept;  // From oversampler when drive > 0
        [[nodiscard]] bool isPrepared() const noexcept;
        [[nodiscard]] double sampleRate() const noexcept;
    };
}
```

**Behavior**:
- `prepare()` - Allocates buffers, configures smoothers (NOT real-time safe)
- `process()` - Block processing with per-block coefficient update
- `processSample()` - Per-sample processing with per-sample coefficient update (expensive)
- `setSlope()` - Controls number of cascaded biquad stages (1-4)
- Slope is ignored for Allpass, LowShelf, HighShelf, Peak (always single stage)
- Drive applies oversampled tanh saturation BEFORE filtering
- All parameters are smoothed to prevent clicks

**Filter Types** (from FilterType enum in biquad.h):

| Type | Description | Uses Slope | Uses Gain |
|------|-------------|------------|-----------|
| `Lowpass` | Attenuates above cutoff | Yes | No |
| `Highpass` | Attenuates below cutoff | Yes | No |
| `Bandpass` | Peak at center, rolloff both sides | Yes | No |
| `Notch` | Null at center frequency | Yes | No |
| `Allpass` | Flat magnitude, phase shift | No (always 1) | No |
| `LowShelf` | Boost/cut below shelf frequency | No (always 1) | Yes |
| `HighShelf` | Boost/cut above shelf frequency | No (always 1) | Yes |
| `Peak` | Parametric EQ bell curve | No (always 1) | Yes |

**Dependencies** (Layer 1 primitives):
- `Biquad` - Individual filter stages
- `OnePoleSmoother` - Parameter smoothing for cutoff, resonance, gain, drive
- `Oversampler<2,1>` - 2x oversampling for drive saturation

**When to use**:

| Use Case | Configuration |
|----------|---------------|
| Delay feedback filtering | Lowpass/Highpass, Slope12dB |
| Synthesizer filter | Lowpass, Slope24dB, high Q for resonance |
| Parametric EQ band | Peak type, adjust gain and Q |
| Rumble removal | Highpass, Slope48dB, 80Hz cutoff |
| Bass boost | LowShelf, +6dB gain |
| Pre-saturation filtering | Any type + drive > 0 |

**Example**:
```cpp
#include "dsp/processors/multimode_filter.h"
using namespace Iterum::DSP;

MultimodeFilter filter;

// In prepare() - allocates memory
filter.prepare(44100.0, 512);
filter.setType(FilterType::Lowpass);
filter.setSlope(FilterSlope::Slope24dB);  // 24 dB/oct
filter.setCutoff(1000.0f);
filter.setResonance(2.0f);
filter.setSmoothingTime(5.0f);  // 5ms parameter smoothing

// In processBlock() - real-time safe
filter.process(buffer, numSamples);

// Modulated filter with LFO
float modCutoff = 1000.0f + lfo.process() * 800.0f;
filter.setCutoff(modCutoff);
filter.process(buffer, numSamples);

// Sample-by-sample for precise modulation
for (size_t i = 0; i < numSamples; ++i) {
    filter.setCutoff(envFollower.process() * 5000.0f);
    buffer[i] = filter.processSample(buffer[i]);
}

// Add drive for character
filter.setDrive(12.0f);  // 12dB pre-filter saturation
// Note: getLatency() will now return oversampler latency
```

---

### SaturationProcessor

| | |
|---|---|
| **Purpose** | Analog-style saturation/waveshaping with 5 algorithms, oversampling, and DC blocking |
| **Location** | [src/dsp/processors/saturation_processor.h](src/dsp/processors/saturation_processor.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.10 (009-saturation-processor) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Saturation algorithm selection
    enum class SaturationType : uint8_t {
        Tape,       // tanh(x) - symmetric, odd harmonics
        Tube,       // Asymmetric polynomial - even harmonics
        Transistor, // Hard-knee soft clip - aggressive
        Digital,    // Hard clip (clamp) - harsh
        Diode       // Soft asymmetric - subtle warmth
    };

    class SaturationProcessor {
    public:
        // Constants
        static constexpr float kMinGainDb = -24.0f;
        static constexpr float kMaxGainDb = +24.0f;
        static constexpr float kDefaultSmoothingMs = 5.0f;
        static constexpr float kDCBlockerCutoffHz = 10.0f;

        // Lifecycle (call before audio processing)
        void prepare(double sampleRate, size_t maxBlockSize) noexcept;
        void reset() noexcept;

        // Processing (real-time safe)
        void process(float* buffer, size_t numSamples) noexcept;
        [[nodiscard]] float processSample(float input) noexcept;

        // Parameter setters (real-time safe, smoothed)
        void setType(SaturationType type) noexcept;
        void setInputGain(float dB) noexcept;   // [-24, +24] dB
        void setOutputGain(float dB) noexcept;  // [-24, +24] dB
        void setMix(float mix) noexcept;        // [0.0, 1.0]

        // Parameter getters
        [[nodiscard]] SaturationType getType() const noexcept;
        [[nodiscard]] float getInputGain() const noexcept;
        [[nodiscard]] float getOutputGain() const noexcept;
        [[nodiscard]] float getMix() const noexcept;

        // Query
        [[nodiscard]] size_t getLatency() const noexcept;  // From oversampler
    };
}
```

**Saturation Types**:

| Type | Description | Character | Harmonics |
|------|-------------|-----------|-----------|
| `Tape` | tanh(x) symmetric curve | Warm, smooth | Odd (3rd, 5th) |
| `Tube` | Asymmetric polynomial | Rich, musical | Even (2nd, 4th) |
| `Transistor` | Hard-knee soft clip | Aggressive, punchy | All |
| `Digital` | Hard clip (clamp) | Harsh, edgy | All (aliased without OS) |
| `Diode` | Soft asymmetric | Subtle warmth | Even (2nd) |

**Behavior**:
- `prepare()` - Allocates oversampled buffer, configures DC blocker (NOT real-time safe)
- `reset()` - Clears filter/smoother states without reallocation
- `process()` - 2x oversampled processing with DC blocking
- All parameters are smoothed (5ms) to prevent clicks
- When mix == 0.0, saturation is bypassed for efficiency
- DC blocker (10Hz highpass) removes offset introduced by asymmetric saturation

**Dependencies** (Layer 0/1 primitives):
- `Oversampler<2,1>` - 2x mono oversampling for alias-free nonlinear processing
- `Biquad` - DC blocking filter (10Hz highpass)
- `OnePoleSmoother` - Parameter smoothing for input/output gain and mix
- `dbToGain()` - dB to linear conversion (from db_utils.h)

**When to use**:

| Use Case | Type | Configuration |
|----------|------|---------------|
| Tape warmth | Tape | +6dB input, -3dB output |
| Tube preamp | Tube | +12dB input, variable output |
| Guitar amp crunch | Transistor | +18dB input, 0dB output |
| Harsh digital distortion | Digital | High input gain |
| Subtle analog color | Diode | +3dB input, mix 30-50% |
| Parallel saturation | Any | mix < 1.0 for parallel blend |

**Example**:
```cpp
#include "dsp/processors/saturation_processor.h"
using namespace Iterum::DSP;

SaturationProcessor sat;

// In prepare() - allocates buffers
sat.prepare(44100.0, 512);
sat.setType(SaturationType::Tape);
sat.setInputGain(12.0f);   // +12dB drive
sat.setOutputGain(-6.0f);  // -6dB makeup
sat.setMix(1.0f);          // Full wet

// Report latency to host (from 2x oversampler)
size_t latency = sat.getLatency();

// In processBlock() - real-time safe
sat.process(buffer, numSamples);

// Parallel saturation (50% blend)
sat.setMix(0.5f);
sat.process(buffer, numSamples);
```

---

### EnvelopeFollower

| | |
|---|---|
| **Purpose** | Amplitude envelope tracking with configurable attack/release times and three detection modes |
| **Location** | [src/dsp/processors/envelope_follower.h](src/dsp/processors/envelope_follower.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.11 (010-envelope-follower) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Detection algorithm selection
    enum class DetectionMode : uint8_t {
        Amplitude,  // Full-wave rectification + smoothing
        RMS,        // Squared signal + smoothing + sqrt
        Peak        // Instant attack, configurable release
    };

    class EnvelopeFollower {
    public:
        // Constants
        static constexpr float kMinAttackMs = 0.1f;
        static constexpr float kMaxAttackMs = 500.0f;
        static constexpr float kMinReleaseMs = 1.0f;
        static constexpr float kMaxReleaseMs = 5000.0f;
        static constexpr float kMinSidechainHz = 20.0f;
        static constexpr float kMaxSidechainHz = 500.0f;

        // Lifecycle (call before audio processing)
        void prepare(double sampleRate, size_t maxBlockSize) noexcept;
        void reset() noexcept;

        // Processing (real-time safe)
        void process(const float* input, float* output, size_t numSamples) noexcept;
        void process(float* buffer, size_t numSamples) noexcept;  // In-place
        [[nodiscard]] float processSample(float input) noexcept;
        [[nodiscard]] float getCurrentValue() const noexcept;

        // Parameter setters (real-time safe)
        void setMode(DetectionMode mode) noexcept;
        void setAttackTime(float ms) noexcept;   // [0.1, 500]
        void setReleaseTime(float ms) noexcept;  // [1, 5000]
        void setSidechainEnabled(bool enabled) noexcept;
        void setSidechainCutoff(float hz) noexcept;  // [20, 500]

        // Parameter getters
        [[nodiscard]] DetectionMode getMode() const noexcept;
        [[nodiscard]] float getAttackTime() const noexcept;
        [[nodiscard]] float getReleaseTime() const noexcept;
        [[nodiscard]] bool isSidechainEnabled() const noexcept;
        [[nodiscard]] float getSidechainCutoff() const noexcept;

        // Query
        [[nodiscard]] size_t getLatency() const noexcept;  // 0 (no latency)
    };
}
```

**Detection Modes**:

| Mode | Description | Output for 0dB Sine |
|------|-------------|---------------------|
| `Amplitude` | Full-wave rectification + asymmetric smoothing | ~0.637 (average of |sin|) |
| `RMS` | Squared signal + blended smoothing + sqrt | ~0.707 (sine RMS) |
| `Peak` | Instant attack (at min), exponential release | ~1.0 (captures peaks) |

**Behavior**:
- `prepare()` - Recalculates attack/release coefficients for new sample rate
- `reset()` - Clears envelope state to zero
- `processSample()` - Returns envelope value for single input sample
- `process()` - Block processing with envelope output
- `getCurrentValue()` - Returns current envelope without advancing state
- Asymmetric one-pole smoothing: attack coefficient when rising, release when falling
- RMS mode uses blended coefficient for accurate RMS (within 1% of theoretical)
- Optional sidechain highpass filter (Biquad) to reduce bass pumping

**Dependencies** (Layer 0/1 primitives):
- `detail::isNaN()` - NaN input handling (from db_utils.h)
- `detail::isInf()` - Infinity input handling (from db_utils.h)
- `detail::flushDenormal()` - Denormal flushing for real-time safety (from db_utils.h)
- `detail::constexprExp()` - Coefficient calculation (from db_utils.h)
- `Biquad` - Sidechain highpass filter (from biquad.h)

**When to use**:

| Use Case | Mode | Configuration |
|----------|------|---------------|
| Compressor/limiter sidechain | RMS | 10ms attack, 100ms release |
| Gate trigger | Peak | 0.1ms attack, 50ms release |
| Ducking (sidechain compression) | RMS + sidechain | 80Hz sidechain filter |
| Envelope-based modulation | Amplitude | Fast attack (1ms), medium release |
| Level metering display | RMS | 5ms attack, 300ms release |

**Example**:
```cpp
#include "dsp/processors/envelope_follower.h"
using namespace Iterum::DSP;

EnvelopeFollower env;

// In prepare() - recalculates coefficients
env.prepare(44100.0, 512);
env.setMode(DetectionMode::RMS);
env.setAttackTime(10.0f);   // 10ms attack
env.setReleaseTime(100.0f); // 100ms release

// Enable sidechain filter to reduce bass pumping
env.setSidechainEnabled(true);
env.setSidechainCutoff(80.0f);  // 80Hz highpass

// In processBlock() - per-sample tracking
for (size_t i = 0; i < numSamples; ++i) {
    float envelope = env.processSample(input[i]);
    // Use envelope for compression, ducking, modulation...
    float gainReduction = calculateGainReduction(envelope);
    output[i] = input[i] * gainReduction;
}

// Or block processing
std::vector<float> envelopeBuffer(numSamples);
env.process(input, envelopeBuffer.data(), numSamples);
```

---

### DynamicsProcessor (Compressor/Limiter)

| | |
|---|---|
| **Purpose** | Real-time compressor/limiter with threshold, ratio, knee, attack/release, makeup gain, lookahead, and sidechain filtering |
| **Location** | [src/dsp/processors/dynamics_processor.h](src/dsp/processors/dynamics_processor.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.12 (011-dynamics-processor) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Detection mode (for EnvelopeFollower)
    enum class DynamicsDetectionMode : uint8_t {
        RMS,   // Average-responding, good for program material
        Peak   // Transient-catching, good for limiting
    };

    class DynamicsProcessor {
    public:
        // Constants
        static constexpr float kDefaultThreshold = -20.0f;   // dB
        static constexpr float kDefaultRatio = 4.0f;         // 4:1
        static constexpr float kDefaultKnee = 0.0f;          // Hard knee
        static constexpr float kDefaultAttackMs = 10.0f;
        static constexpr float kDefaultReleaseMs = 100.0f;
        static constexpr float kDefaultMakeupGain = 0.0f;    // dB
        static constexpr float kDefaultLookaheadMs = 0.0f;   // Disabled
        static constexpr float kDefaultSidechainHz = 80.0f;

        // Parameter ranges
        static constexpr float kMinThreshold = -60.0f;
        static constexpr float kMaxThreshold = 0.0f;
        static constexpr float kMinRatio = 1.0f;
        static constexpr float kMaxRatio = 100.0f;  // Limiter mode
        static constexpr float kMinKnee = 0.0f;
        static constexpr float kMaxKnee = 24.0f;
        static constexpr float kMinAttackMs = 0.1f;
        static constexpr float kMaxAttackMs = 500.0f;
        static constexpr float kMinReleaseMs = 1.0f;
        static constexpr float kMaxReleaseMs = 5000.0f;
        static constexpr float kMinMakeupGain = -24.0f;
        static constexpr float kMaxMakeupGain = 24.0f;
        static constexpr float kMinLookaheadMs = 0.0f;
        static constexpr float kMaxLookaheadMs = 10.0f;
        static constexpr float kMinSidechainHz = 20.0f;
        static constexpr float kMaxSidechainHz = 500.0f;

        // Lifecycle (call before audio processing)
        void prepare(double sampleRate, size_t maxBlockSize) noexcept;
        void reset() noexcept;

        // Processing (real-time safe)
        void process(float* buffer, size_t numSamples) noexcept;
        [[nodiscard]] float processSample(float input) noexcept;

        // Threshold/Ratio (basic compression)
        void setThreshold(float dB) noexcept;
        void setRatio(float ratio) noexcept;
        [[nodiscard]] float getThreshold() const noexcept;
        [[nodiscard]] float getRatio() const noexcept;

        // Knee
        void setKneeWidth(float dB) noexcept;
        [[nodiscard]] float getKneeWidth() const noexcept;

        // Attack/Release
        void setAttackTime(float ms) noexcept;
        void setReleaseTime(float ms) noexcept;
        [[nodiscard]] float getAttackTime() const noexcept;
        [[nodiscard]] float getReleaseTime() const noexcept;

        // Makeup Gain
        void setMakeupGain(float dB) noexcept;
        void setAutoMakeup(bool enabled) noexcept;
        [[nodiscard]] float getMakeupGain() const noexcept;
        [[nodiscard]] bool isAutoMakeupEnabled() const noexcept;

        // Detection Mode
        void setDetectionMode(DynamicsDetectionMode mode) noexcept;
        [[nodiscard]] DynamicsDetectionMode getDetectionMode() const noexcept;

        // Sidechain Filtering
        void setSidechainEnabled(bool enabled) noexcept;
        void setSidechainCutoff(float hz) noexcept;
        [[nodiscard]] bool isSidechainEnabled() const noexcept;
        [[nodiscard]] float getSidechainCutoff() const noexcept;

        // Lookahead
        void setLookahead(float ms) noexcept;
        [[nodiscard]] float getLookahead() const noexcept;
        [[nodiscard]] size_t getLatency() const noexcept;

        // Metering
        [[nodiscard]] float getCurrentGainReduction() const noexcept;  // Negative dB
    };
}
```

**Behavior**:
- `prepare()` - Allocates buffers, configures envelope follower (NOT real-time safe)
- `reset()` - Clears envelope state, gain reduction, lookahead buffer
- `processSample()` - Real-time processing with gain reduction applied
- `getCurrentGainReduction()` - Returns current GR in negative dB (e.g., -7.5 dB)

**Gain Reduction Formula**:
```
For input above threshold (hard knee):
  GR = (inputLevel_dB - threshold) * (1 - 1/ratio)

For soft knee:
  Quadratic interpolation in knee region [threshold - knee/2, threshold + knee/2]
```

**Auto-Makeup Formula**:
```
autoMakeup = -threshold * (1 - 1/ratio)
```

**Dependencies** (Layer 1-2 primitives):
- `EnvelopeFollower` - Level detection (peer Layer 2 component)
- `OnePoleSmoother` - Gain reduction smoothing
- `DelayLine` - Lookahead delay
- `Biquad` - Sidechain highpass filter
- `dbToGain()` / `gainToDb()` - dB/linear conversion

**When to use**:

| Use Case | Configuration |
|----------|---------------|
| Vocal compression | -24dB threshold, 3:1, 6dB knee, 15ms attack, 150ms release |
| Drum bus | -12dB threshold, 4:1, 30ms attack, 80ms release, sidechain 80Hz |
| Master limiter | -0.3dB threshold, 100:1, 0.1ms attack, 100ms release, 5ms lookahead |
| Bass compression | -18dB threshold, 4:1, sidechain HPF 100Hz (reduces pumping) |
| Transparent compression | Wide knee (12dB), low ratio (2:1), auto-makeup |

**Example**:
```cpp
#include "dsp/processors/dynamics_processor.h"
using namespace Iterum::DSP;

DynamicsProcessor comp;

// In prepare() - allocates buffers
comp.prepare(44100.0, 512);
comp.setThreshold(-20.0f);
comp.setRatio(4.0f);
comp.setKneeWidth(6.0f);  // 6dB soft knee
comp.setAttackTime(10.0f);
comp.setReleaseTime(100.0f);
comp.setAutoMakeup(true);

// Enable sidechain HPF to reduce bass pumping
comp.setSidechainEnabled(true);
comp.setSidechainCutoff(80.0f);

// In processBlock() - real-time safe
comp.process(buffer, numSamples);

// For metering UI
float grDb = comp.getCurrentGainReduction();  // e.g., -7.5

// Limiter mode
comp.setThreshold(-0.3f);
comp.setRatio(100.0f);
comp.setLookahead(5.0f);  // 5ms lookahead
// Report latency to host
size_t latency = comp.getLatency();
```

---

### DuckingProcessor

| | |
|---|---|
| **Purpose** | Sidechain-triggered gain reduction for ducking one audio source when another becomes active |
| **Location** | [src/dsp/processors/ducking_processor.h](src/dsp/processors/ducking_processor.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.13 (012-ducking-processor) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // State machine for hold time behavior
    enum class DuckingState : uint8_t {
        Idle,     // Sidechain below threshold, no gain reduction
        Ducking,  // Sidechain above threshold, gain reduction active
        Holding   // Sidechain dropped, holding before release
    };

    class DuckingProcessor {
    public:
        // Constants
        static constexpr float kMinThreshold = -60.0f;
        static constexpr float kMaxThreshold = 0.0f;
        static constexpr float kDefaultThreshold = -30.0f;
        static constexpr float kMinDepth = -48.0f;
        static constexpr float kMaxDepth = 0.0f;
        static constexpr float kDefaultDepth = -12.0f;
        static constexpr float kMinAttackMs = 0.1f;
        static constexpr float kMaxAttackMs = 500.0f;
        static constexpr float kMinReleaseMs = 1.0f;
        static constexpr float kMaxReleaseMs = 5000.0f;
        static constexpr float kMinHoldMs = 0.0f;
        static constexpr float kMaxHoldMs = 1000.0f;
        static constexpr float kMinRange = -48.0f;
        static constexpr float kMaxRange = 0.0f;
        static constexpr float kMinSidechainHz = 20.0f;
        static constexpr float kMaxSidechainHz = 500.0f;

        // Lifecycle (call before audio processing)
        void prepare(double sampleRate, size_t maxBlockSize) noexcept;
        void reset() noexcept;

        // Dual-input processing (real-time safe)
        [[nodiscard]] float processSample(float main, float sidechain) noexcept;
        void process(const float* main, const float* sidechain,
                     float* output, size_t numSamples) noexcept;
        void process(float* mainInOut, const float* sidechain,
                     size_t numSamples) noexcept;

        // Parameter setters (real-time safe)
        void setThreshold(float dB) noexcept;
        void setDepth(float dB) noexcept;
        void setAttackTime(float ms) noexcept;
        void setReleaseTime(float ms) noexcept;
        void setHoldTime(float ms) noexcept;
        void setRange(float dB) noexcept;
        void setSidechainFilterEnabled(bool enabled) noexcept;
        void setSidechainFilterCutoff(float hz) noexcept;

        // Parameter getters
        [[nodiscard]] float getThreshold() const noexcept;
        [[nodiscard]] float getDepth() const noexcept;
        [[nodiscard]] float getAttackTime() const noexcept;
        [[nodiscard]] float getReleaseTime() const noexcept;
        [[nodiscard]] float getHoldTime() const noexcept;
        [[nodiscard]] float getRange() const noexcept;
        [[nodiscard]] bool isSidechainFilterEnabled() const noexcept;
        [[nodiscard]] float getSidechainFilterCutoff() const noexcept;

        // Metering
        [[nodiscard]] float getCurrentGainReduction() const noexcept;
        [[nodiscard]] size_t getLatency() const noexcept;  // Always 0
    };
}
```

**Behavior**:
- `prepare()` - Configures envelope follower, gain smoother, sidechain filter
- `reset()` - Clears all state (envelope, smoother, filter, hold timer)
- `processSample()` - Dual-input processing: main audio + sidechain trigger
- `getCurrentGainReduction()` - Returns current GR in negative dB (e.g., -8.5 dB)

**State Machine**:
```
          sidechain exceeds threshold
Idle ──────────────────────────────────────► Ducking
  ▲                                             │
  │      hold expired                           │ sidechain drops below threshold
  │                                             ▼
  └─────────────────────────────────────── Holding
           (after holdSamplesTotal_)
```

**Gain Reduction Formula**:
```
overshoot = envelopeDb - thresholdDb
factor = clamp(overshoot / 10.0, 0.0, 1.0)  // Full depth at 10dB overshoot
targetGR = depth * factor
actualGR = max(targetGR, range)  // Range limits maximum attenuation
```

**Dependencies** (Layer 1-2 primitives):
- `EnvelopeFollower` - Sidechain level detection (peer Layer 2 component)
- `OnePoleSmoother` - Gain reduction smoothing for click-free transitions
- `Biquad` - Sidechain highpass filter (removes bass from triggering)
- `dbToGain()` / `gainToDb()` - dB/linear conversion

**When to use**:

| Use Case | Configuration |
|----------|---------------|
| Voiceover ducking (podcast) | -40dB threshold, -15dB depth, 5ms attack, 500ms release, 200ms hold |
| DJ sidechain pumping | -20dB threshold, -24dB depth, 1ms attack, 100ms release, 0ms hold |
| Subtle background reduction | -35dB threshold, -6dB depth, 50ms attack, 1000ms release, 500ms hold |
| Range-limited ducking | -30dB threshold, -48dB depth, -12dB range (limits max attenuation) |
| Bass-avoiding ducking | Enable sidechain HPF at 200Hz to ignore kick drums |

**Example**:
```cpp
#include "dsp/processors/ducking_processor.h"
using namespace Iterum::DSP;

DuckingProcessor ducker;

// In prepare() - allocates buffers
ducker.prepare(44100.0, 512);
ducker.setThreshold(-30.0f);
ducker.setDepth(-12.0f);
ducker.setAttackTime(10.0f);
ducker.setReleaseTime(200.0f);
ducker.setHoldTime(100.0f);

// Enable sidechain HPF to focus on voice frequencies
ducker.setSidechainFilterEnabled(true);
ducker.setSidechainFilterCutoff(150.0f);

// In processBlock() - real-time safe
// mainBuffer = music, sidechainBuffer = voice
ducker.process(mainBuffer, sidechainBuffer, outputBuffer, numSamples);

// For metering UI
float grDb = ducker.getCurrentGainReduction();  // e.g., -8.5
```

---

### NoiseGenerator

| | |
|---|---|
| **Purpose** | Multi-type noise generator for analog character and lo-fi effects |
| **Location** | [src/dsp/processors/noise_generator.h](src/dsp/processors/noise_generator.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.14 (013-noise-generator) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Noise generation algorithm types
    enum class NoiseType : uint8_t {
        White,        // Flat spectrum white noise
        Pink,         // -3dB/octave pink noise (Paul Kellet filter)
        TapeHiss,     // Signal-dependent tape hiss with high-frequency emphasis
        VinylCrackle, // Impulsive clicks/pops with optional surface noise
        Asperity      // Tape head contact noise varying with signal level
    };

    class NoiseGenerator {
    public:
        // Constants
        static constexpr float kMinLevelDb = -96.0f;
        static constexpr float kMaxLevelDb = 12.0f;
        static constexpr float kDefaultLevelDb = -20.0f;
        static constexpr float kMinCrackleDensity = 0.1f;
        static constexpr float kMaxCrackleDensity = 20.0f;
        static constexpr float kDefaultCrackleDensity = 3.0f;
        static constexpr float kMinSensitivity = 0.0f;
        static constexpr float kMaxSensitivity = 2.0f;

        // Lifecycle (call before audio processing)
        void prepare(float sampleRate, size_t maxBlockSize) noexcept;
        void reset() noexcept;

        // Processing (real-time safe)
        void process(float* output, size_t numSamples) noexcept;
        void process(const float* input, float* output, size_t numSamples) noexcept;
        void processMix(const float* input, float* output, size_t numSamples) noexcept;

        // Level Control
        void setNoiseLevel(NoiseType type, float dB) noexcept;
        void setNoiseEnabled(NoiseType type, bool enabled) noexcept;
        void setMasterLevel(float dB) noexcept;
        [[nodiscard]] float getNoiseLevel(NoiseType type) const noexcept;
        [[nodiscard]] bool isNoiseEnabled(NoiseType type) const noexcept;
        [[nodiscard]] float getMasterLevel() const noexcept;
        [[nodiscard]] bool isAnyEnabled() const noexcept;

        // Type-Specific Parameters
        void setTapeHissParams(float floorDb, float sensitivity) noexcept;
        void setAsperityParams(float floorDb, float sensitivity) noexcept;
        void setCrackleParams(float density, float surfaceNoiseDb) noexcept;
    };
}
```

**Noise Types**:

| Type | Description | Signal-Dependent | Character |
|------|-------------|------------------|-----------|
| `White` | Flat spectrum (equal energy per frequency) | No | Hiss, static |
| `Pink` | -3dB/octave rolloff (equal energy per octave) | No | Natural, smooth |
| `TapeHiss` | Pink + high-shelf boost + envelope modulation | Yes | Authentic tape |
| `VinylCrackle` | Poisson clicks + surface noise | No | Vinyl record |
| `Asperity` | White noise modulated by signal envelope | Yes | Tape contact |

**Behavior**:
- `prepare()` - Allocates buffers, configures smoothers and envelope followers (NOT real-time safe)
- `reset()` - Clears filter states, reseeds RNG for different sequence
- `process(output, n)` - Generates noise without sidechain (signal-dependent types use floor level)
- `process(input, output, n)` - Uses input as sidechain for TapeHiss/Asperity modulation
- `processMix(input, output, n)` - Adds noise to input signal (output = input + noise)
- All level changes are smoothed (5ms) to prevent clicks
- Each noise type has independent enable and level control

**Dependencies** (Layer 0/1/2 primitives):
- `Xorshift32` - Random number generation (Layer 0)
- `Biquad` - High-shelf filter for tape hiss character (Layer 1)
- `OnePoleSmoother` - Level smoothing for all channels (Layer 1)
- `EnvelopeFollower` - Signal-dependent modulation (Layer 2)
- `dbToGain()` - dB to linear conversion (Layer 0)

**When to use**:

| Use Case | Configuration |
|----------|---------------|
| Tape delay character | TapeHiss enabled, -30dB, floor -60dB |
| Vinyl lo-fi effect | VinylCrackle + Pink, 5 clicks/sec, -35dB surface |
| Subtle analog warmth | White at -40dB + Pink at -45dB |
| BBD delay character | Asperity at -50dB, moderate sensitivity |
| Lo-fi mix effect | All types enabled at low levels |

**Example**:
```cpp
#include "dsp/processors/noise_generator.h"
using namespace Iterum::DSP;

NoiseGenerator noise;

// In prepare() - allocates buffers
noise.prepare(44100.0f, 512);

// Configure tape hiss
noise.setNoiseEnabled(NoiseType::TapeHiss, true);
noise.setNoiseLevel(NoiseType::TapeHiss, -30.0f);
noise.setTapeHissParams(-60.0f, 1.0f);  // Floor at -60dB, normal sensitivity

// Configure vinyl crackle
noise.setNoiseEnabled(NoiseType::VinylCrackle, true);
noise.setNoiseLevel(NoiseType::VinylCrackle, -25.0f);
noise.setCrackleParams(5.0f, -40.0f);  // 5 clicks/sec, -40dB surface

// In processBlock() - with sidechain for signal-dependent noise
noise.processMix(audioBuffer, audioBuffer, numSamples);  // In-place

// Or separate noise output
std::vector<float> noiseBuffer(numSamples);
noise.process(audioBuffer, noiseBuffer.data(), numSamples);
// Mix manually...
```

---

### DiffusionNetwork

| | |
|---|---|
| **Purpose** | 8-stage Schroeder allpass diffusion network for reverb-like temporal smearing |
| **Location** | [src/dsp/processors/diffusion_network.h](src/dsp/processors/diffusion_network.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.15 (015-diffusion-network) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Constants
    constexpr size_t kNumDiffusionStages = 8;
    constexpr float kAllpassCoeff = 0.618033988749895f;  // Golden ratio inverse
    constexpr float kBaseDelayMs = 3.2f;
    constexpr float kMaxModDepthMs = 2.0f;

    class DiffusionNetwork {
    public:
        // Constants
        static constexpr float kMinSize = 0.0f;
        static constexpr float kMaxSize = 100.0f;
        static constexpr float kMinDensity = 0.0f;
        static constexpr float kMaxDensity = 100.0f;
        static constexpr float kMinWidth = 0.0f;
        static constexpr float kMaxWidth = 100.0f;
        static constexpr float kMinModDepth = 0.0f;
        static constexpr float kMaxModDepth = 100.0f;
        static constexpr float kMinModRate = 0.1f;
        static constexpr float kMaxModRate = 5.0f;

        // Lifecycle (call before audio processing)
        void prepare(float sampleRate, size_t maxBlockSize) noexcept;
        void reset() noexcept;

        // Processing (real-time safe, stereo)
        void process(const float* leftIn, const float* rightIn,
                     float* leftOut, float* rightOut,
                     size_t numSamples) noexcept;

        // Parameter setters (real-time safe, smoothed)
        void setSize(float sizePercent) noexcept;       // [0%, 100%] scales delay times
        void setDensity(float densityPercent) noexcept; // [0%, 100%] → 0-8 stages
        void setWidth(float widthPercent) noexcept;     // [0%, 100%] mono→stereo
        void setModDepth(float depthPercent) noexcept;  // [0%, 100%] LFO depth
        void setModRate(float rateHz) noexcept;         // [0.1Hz, 5Hz] LFO speed

        // Parameter getters
        [[nodiscard]] float getSize() const noexcept;
        [[nodiscard]] float getDensity() const noexcept;
        [[nodiscard]] float getWidth() const noexcept;
        [[nodiscard]] float getModDepth() const noexcept;
        [[nodiscard]] float getModRate() const noexcept;
    };
}
```

**Parameters**:

| Parameter | Range | Description |
|-----------|-------|-------------|
| `size` | [0%, 100%] | Scales all delay times (0%=bypass, 100%=max diffusion) |
| `density` | [0%, 100%] | Number of active stages (25%=2, 50%=4, 75%=6, 100%=8) |
| `width` | [0%, 100%] | Stereo spread (0%=mono, 100%=full decorrelation) |
| `modDepth` | [0%, 100%] | LFO modulation depth for chorus-like movement |
| `modRate` | [0.1-5Hz] | LFO modulation speed |

**Behavior**:
- `prepare()` - Allocates delay buffers, configures smoothers (NOT real-time safe)
- `reset()` - Clears delay lines and snaps smoothers to targets
- `process()` - Stereo block processing, supports in-place operation
- Uses irrational delay ratios based on √n: {1.0, 1.127, 1.414, 1.732, 2.236, 2.828, 3.317, 4.123}
- Right channel has 1.127× delay offset for stereo decorrelation
- Per-stage LFO phase offsets (45°) create decorrelated modulation
- All parameters are smoothed (10ms) to prevent zipper noise
- Preserves frequency spectrum (energy-conserving allpass filters)
- Uses allpass interpolation for energy-preserving fractional delays

**Technical Details**:
- Single-delay-line Schroeder allpass formulation: `v[n] = x[n] + g*v[n-D]`, `y[n] = -g*v[n] + v[n-D]`
- Allpass coefficient g = 0.618 (golden ratio inverse) for optimal diffusion
- First-order allpass interpolation preserves energy at all frequencies
- Density control uses crossfade for smooth stage enable/disable

**Dependencies** (Layer 1 primitives):
- `DelayLine` - Variable delay with allpass interpolation
- `OnePoleSmoother` - Parameter smoothing for all controls

**When to use**:

| Use Case | Configuration |
|----------|---------------|
| Reverb pre-diffuser | size=50-80%, density=100%, modDepth=0% |
| Smeared/washy delay | size=100%, density=100%, modDepth=20% |
| Stereo enhancement | size=30%, density=50%, width=100% |
| Chorus-like texture | size=50%, modDepth=50%, modRate=1.5Hz |
| Subtle smoothing | size=20%, density=50%, modDepth=0% |

**Example**:
```cpp
#include "dsp/processors/diffusion_network.h"
using namespace Iterum::DSP;

DiffusionNetwork diffuser;

// In prepare() - allocates delay buffers
diffuser.prepare(44100.0f, 512);
diffuser.setSize(60.0f);       // 60% diffusion size
diffuser.setDensity(100.0f);   // All 8 stages
diffuser.setWidth(100.0f);     // Full stereo
diffuser.setModDepth(25.0f);   // Subtle movement
diffuser.setModRate(1.0f);     // 1 Hz LFO

// In processBlock() - real-time safe
diffuser.process(leftIn, rightIn, leftOut, rightOut, numSamples);

// As reverb pre-diffuser (no modulation for cleaner tail)
diffuser.setModDepth(0.0f);
diffuser.setSize(80.0f);
diffuser.process(leftIn, rightIn, leftOut, rightOut, numSamples);
// Feed output to reverb tank...

// In-place processing
diffuser.process(buffer, buffer, buffer, buffer, numSamples);
```

---

### PitchShiftProcessor

| | |
|---|---|
| **Purpose** | Time-preserving pitch shifting with 3 quality modes: Simple (zero-latency), Granular (balanced), PhaseVocoder (high quality) |
| **Location** | [src/dsp/processors/pitch_shift_processor.h](src/dsp/processors/pitch_shift_processor.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.16 (016-pitch-shifter) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Quality mode selection
    enum class PitchMode : uint8_t {
        Simple,      // Zero latency, dual delay-line crossfade
        Granular,    // Low latency (<2048 samples), 4-voice OLA
        PhaseVocoder // High quality (<8192 samples), STFT-based
    };

    // Utility functions
    [[nodiscard]] constexpr float pitchRatioFromSemitones(float semitones) noexcept;
    [[nodiscard]] constexpr float semitonesFromPitchRatio(float ratio) noexcept;

    class PitchShiftProcessor {
    public:
        // Constants
        static constexpr float kMinSemitones = -24.0f;  // -2 octaves
        static constexpr float kMaxSemitones = +24.0f;  // +2 octaves
        static constexpr float kMinCents = -100.0f;     // -1 semitone
        static constexpr float kMaxCents = +100.0f;     // +1 semitone

        // Lifecycle (call before audio processing)
        void prepare(double sampleRate, size_t maxBlockSize) noexcept;
        void reset() noexcept;

        // Processing (real-time safe)
        void process(const float* input, float* output, size_t numSamples) noexcept;

        // Mode selection
        void setMode(PitchMode mode) noexcept;
        [[nodiscard]] PitchMode getMode() const noexcept;

        // Pitch parameters (real-time safe, smoothed)
        void setSemitones(float semitones) noexcept;   // [-24, +24]
        void setCents(float cents) noexcept;            // [-100, +100]
        [[nodiscard]] float getSemitones() const noexcept;
        [[nodiscard]] float getCents() const noexcept;
        [[nodiscard]] float getPitchRatio() const noexcept;

        // Formant preservation (PhaseVocoder mode only)
        void setFormantPreserve(bool enable) noexcept;
        [[nodiscard]] bool getFormantPreserve() const noexcept;

        // Query
        [[nodiscard]] size_t getLatencySamples() const noexcept;
        [[nodiscard]] bool isPrepared() const noexcept;
    };
}
```

**Quality Modes**:

| Mode | Latency | CPU | Quality | Best For |
|------|---------|-----|---------|----------|
| `Simple` | 0 samples | ~0.5% | Good | Monitoring, feedback loops |
| `Granular` | <2048 samples (~46ms) | ~2% | Better | General pitch shifting |
| `PhaseVocoder` | <8192 samples (~186ms) | ~8% | Best | Vocals, polyphonic material |

**Behavior**:
- `prepare()` - Allocates buffers for all modes (NOT real-time safe)
- `reset()` - Clears delay lines, resets phase accumulators
- `process()` - Supports separate or in-place I/O (input can equal output)
- Duration is preserved: 100 input samples → 100 output samples
- Pitch range: ±24 semitones (4 octaves total)
- Fine control: ±100 cents for detuning effects
- All parameters smoothed to prevent clicks during automation

**Formant Preservation** (PhaseVocoder only):
- Uses cepstral envelope extraction to separate formants from harmonics
- Prevents "chipmunk" effect when pitch-shifting vocals
- Quefrency cutoff: 1.5ms (suitable for vocals up to ~666Hz F0)
- Automatically disabled in Simple/Granular modes (no spectral access)

**Algorithm Details**:

| Mode | Algorithm |
|------|-----------|
| Simple | Dual delay-line with half-sine crossfade (50ms window), time-varying delay creates Doppler shift |
| Granular | 4-voice overlap-add with Hann windows (46ms grains), 33% crossfade region |
| PhaseVocoder | STFT with phase locking (Laroche & Dolson), frequency bin scaling, 4096-sample FFT, 4x overlap |

**Dependencies** (Layer 1 primitives):
- `DelayLine` - Circular buffer for Simple/Granular modes
- `STFT` - Short-time Fourier transform for PhaseVocoder
- `FFT` - Forward/inverse FFT for PhaseVocoder
- `SpectralBuffer` - Complex spectrum storage for PhaseVocoder
- `OverlapAdd` - Output reconstruction for Granular/PhaseVocoder
- `OnePoleSmoother` - Parameter smoothing for semitones/cents
- `WindowFunctions` - Hann windows for Granular/PhaseVocoder

**When to use**:

| Use Case | Mode | Configuration |
|----------|------|---------------|
| Shimmer delay feedback | Simple | +12 semitones, formant off |
| Vocal pitch correction | PhaseVocoder | ±1-2 semitones, formant on |
| Harmonizer (+5th) | Granular | +7 semitones |
| Detuning effect | Simple | 0 semitones, ±25 cents |
| Octave up for synth | PhaseVocoder | +12 semitones, formant off |
| Real-time monitoring | Simple | Any (zero latency) |

**Example**:
```cpp
#include "dsp/processors/pitch_shift_processor.h"
using namespace Iterum::DSP;

PitchShiftProcessor shifter;

// In prepare() - allocates buffers
shifter.prepare(44100.0, 512);
shifter.setMode(PitchMode::PhaseVocoder);  // Highest quality
shifter.setSemitones(7.0f);                 // Perfect fifth up
shifter.setFormantPreserve(true);           // Preserve vocal formants

// In processBlock() - real-time safe
shifter.process(input, output, numSamples);

// Shimmer delay (zero-latency mode for feedback loop)
shifter.setMode(PitchMode::Simple);
shifter.setSemitones(12.0f);  // Octave up
shifter.setFormantPreserve(false);  // Ignored in Simple mode
shifter.process(feedbackBuffer, feedbackBuffer, numSamples);  // In-place

// Fine pitch control with cents
shifter.setSemitones(0.0f);
shifter.setCents(50.0f);  // Quarter-tone up
float ratio = shifter.getPitchRatio();  // ≈1.029

// Harmonizer with two instances
PitchShiftProcessor harmony1, harmony2;
harmony1.prepare(sampleRate, blockSize);
harmony2.prepare(sampleRate, blockSize);
harmony1.setMode(PitchMode::Granular);
harmony2.setMode(PitchMode::Granular);
harmony1.setSemitones(4.0f);   // Major third
harmony2.setSemitones(-3.0f);  // Minor third down
harmony1.process(input, output1, numSamples);
harmony2.process(input, output2, numSamples);
// Mix output1 + output2 + dry...
```

---

## Layer 3: System Components

Layer 3 components compose Layer 1-2 primitives and processors into complete audio building blocks ready for plugin integration.

### DelayEngine

| | |
|---|---|
| **Purpose** | High-level delay wrapper with time modes, smoothing, and dry/wet mixing |
| **Location** | [src/dsp/systems/delay_engine.h](src/dsp/systems/delay_engine.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.18 (018-delay-engine) |
| **Dependencies** | DelayLine (L1), OnePoleSmoother (L1), BlockContext (L0), NoteValue (L0) |

**Public API**:

```cpp
namespace Iterum::DSP {
    // Time mode enumeration
    enum class TimeMode : uint8_t {
        Free,    // Delay time in milliseconds
        Synced   // Delay time from NoteValue + host tempo
    };

    class DelayEngine {
    public:
        // Lifecycle
        void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
        void reset() noexcept;

        // Configuration
        void setTimeMode(TimeMode mode) noexcept;
        void setDelayTimeMs(float ms) noexcept;           // Free mode
        void setNoteValue(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;
        void setMix(float wetRatio) noexcept;             // 0.0-1.0
        void setKillDry(bool killDry) noexcept;

        // Processing (real-time safe, noexcept)
        void process(float* buffer, size_t numSamples, const BlockContext& ctx) noexcept;
        void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;

        // Query
        [[nodiscard]] float getCurrentDelayMs() const noexcept;
        [[nodiscard]] TimeMode getTimeMode() const noexcept;
        [[nodiscard]] float getMaxDelayMs() const noexcept;
        [[nodiscard]] bool isPrepared() const noexcept;
    };
}
```

**Behavior**:
- **Free mode**: Delay time set directly in milliseconds
- **Synced mode**: Delay time calculated from NoteValue + BlockContext tempo
- **Smoothing**: All parameter changes smoothed with 20ms one-pole filter
- **Mix**: 0.0 = fully dry, 1.0 = fully wet; kill-dry mode removes dry signal
- **NaN handling**: Invalid values rejected, previous value kept
- **Clamping**: Delay time clamped to [0, maxDelayMs]

**When to use**:
- Building delay-based effects (simple delay, slapback, tempo-synced delay)
- As building block for more complex effects (ping-pong, multi-tap, feedback networks)
- Any effect requiring tempo-synchronized delay times

**Example - Free Mode**:
```cpp
#include "dsp/systems/delay_engine.h"

Iterum::DSP::DelayEngine delay;
delay.prepare(44100.0, 512, 2000.0f);  // 2 second max delay
delay.setTimeMode(Iterum::DSP::TimeMode::Free);
delay.setDelayTimeMs(250.0f);
delay.setMix(0.5f);

// In audio callback
Iterum::DSP::BlockContext ctx;
ctx.sampleRate = 44100.0;
delay.process(buffer, numSamples, ctx);
```

**Example - Tempo Synced**:
```cpp
delay.setTimeMode(Iterum::DSP::TimeMode::Synced);
delay.setNoteValue(Iterum::DSP::NoteValue::Quarter, Iterum::DSP::NoteModifier::Dotted);
delay.setMix(0.7f);

// BlockContext with tempo info
ctx.tempoBPM = 120.0;
delay.process(buffer, numSamples, ctx);  // Delay = 750ms (dotted quarter at 120 BPM)
```

---

### FeedbackNetwork

| | |
|---|---|
| **Purpose** | Manages feedback loops for delay effects with filtering, saturation, and cross-feedback routing |
| **Location** | [src/dsp/systems/feedback_network.h](src/dsp/systems/feedback_network.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.19 (019-feedback-network) |
| **Dependencies** | DelayLine (L1), MultimodeFilter (L2), SaturationProcessor (L2), OnePoleSmoother (L1), BlockContext (L0) |

**Public API**:

```cpp
namespace Iterum::DSP {
    class FeedbackNetwork {
    public:
        // Lifecycle
        void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
        void reset() noexcept;

        // Feedback Configuration
        void setFeedbackAmount(float amount) noexcept;       // 0.0-1.2 (120% for self-oscillation)
        void setCrossFeedbackAmount(float amount) noexcept;  // 0.0-1.0 for ping-pong
        void setFrozen(bool frozen) noexcept;                // Infinite sustain mode

        // Delay Time
        void setDelayTimeMs(float ms) noexcept;

        // Filter in Feedback Path
        void setFilterEnabled(bool enabled) noexcept;
        void setFilterType(FilterType type) noexcept;        // Lowpass, Highpass, Bandpass
        void setFilterCutoff(float hz) noexcept;
        void setFilterResonance(float q) noexcept;

        // Saturation in Feedback Path
        void setSaturationEnabled(bool enabled) noexcept;
        void setSaturationType(SaturationType type) noexcept;
        void setSaturationDrive(float dB) noexcept;

        // Processing (real-time safe, noexcept)
        void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;

        // Query
        [[nodiscard]] float getFeedbackAmount() const noexcept;
        [[nodiscard]] bool isFrozen() const noexcept;
    };
}
```

**Behavior**:
- **Feedback range**: 0-120% - values >100% enable self-oscillation with saturation limiting
- **Cross-feedback**: Routes left/right outputs to opposite channels for ping-pong effects
- **Freeze mode**: Mutes input while maintaining 100% feedback for infinite sustain
- **Filter placement**: In feedback path for progressive tone shaping on each repeat
- **Saturation**: Soft limits feedback to prevent runaway oscillation at high settings

**When to use**:
- Building delay effects with feedback control
- Creating ping-pong delays with cross-feedback
- Adding warmth with filtered/saturated repeats
- Implementing freeze/hold functionality

**Example**:
```cpp
#include "dsp/systems/feedback_network.h"

Iterum::DSP::FeedbackNetwork feedback;
feedback.prepare(44100.0, 512, 2000.0f);

feedback.setDelayTimeMs(375.0f);
feedback.setFeedbackAmount(0.6f);
feedback.setFilterEnabled(true);
feedback.setFilterType(Iterum::DSP::FilterType::Lowpass);
feedback.setFilterCutoff(4000.0f);

// In audio callback
feedback.process(left, right, numSamples, ctx);
```

---

### ModulationMatrix

| | |
|---|---|
| **Purpose** | Routes modulation sources (LFO, EnvelopeFollower) to parameter destinations with depth control |
| **Location** | [src/dsp/systems/modulation_matrix.h](src/dsp/systems/modulation_matrix.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.20 (020-modulation-matrix) |
| **Dependencies** | OnePoleSmoother (L1), db_utils (L0) |

**Public API**:

```cpp
namespace Iterum::DSP {
    enum class ModulationMode : uint8_t {
        Bipolar,   // Source [-1,+1] maps directly to [-1,+1] × depth
        Unipolar   // Source [-1,+1] maps to [0,1] × depth
    };

    class ModulationSource {
    public:
        virtual ~ModulationSource() = default;
        [[nodiscard]] virtual float getCurrentValue() const noexcept = 0;
        [[nodiscard]] virtual std::pair<float, float> getSourceRange() const noexcept = 0;
    };

    class ModulationMatrix {
    public:
        // Lifecycle
        void prepare(double sampleRate, size_t maxBlockSize, size_t maxRoutes = 32) noexcept;
        void reset() noexcept;

        // Source/Destination Registration
        bool registerSource(uint8_t id, ModulationSource* source) noexcept;
        bool registerDestination(uint8_t id, float minValue, float maxValue,
                                 const char* label = nullptr) noexcept;

        // Route Management
        int createRoute(uint8_t sourceId, uint8_t destinationId, float depth = 1.0f,
                       ModulationMode mode = ModulationMode::Bipolar) noexcept;
        void setRouteDepth(int routeIndex, float depth) noexcept;
        void setRouteEnabled(int routeIndex, bool enabled) noexcept;

        // Processing (real-time safe, noexcept)
        void process(size_t numSamples) noexcept;

        // Value Retrieval
        [[nodiscard]] float getModulatedValue(uint8_t destinationId, float baseValue) const noexcept;
        [[nodiscard]] float getCurrentModulation(uint8_t destinationId) const noexcept;

        // Query
        [[nodiscard]] size_t getSourceCount() const noexcept;
        [[nodiscard]] size_t getDestinationCount() const noexcept;
        [[nodiscard]] size_t getRouteCount() const noexcept;
    };
}
```

**Constants**:
- `kMaxModulationSources = 16`
- `kMaxModulationDestinations = 16`
- `kMaxModulationRoutes = 32`
- `kModulationSmoothingTimeMs = 20.0f`

**Behavior**:
- **Bipolar mode**: Source [-1,+1] maps directly, modulates ±50% of destination range at depth=1.0
- **Unipolar mode**: Source [-1,+1] maps to [0,1], modulates 0-50% of destination range at depth=1.0
- **Multiple routes**: Contributions to same destination are summed
- **Depth smoothing**: 20ms one-pole smoothing prevents zipper noise on depth changes
- **Clamping**: Final modulated values clamped to destination min/max range
- **NaN handling**: Invalid source values treated as 0.0

**When to use**:
- Routing LFO to delay time for chorus/vibrato effects
- Routing EnvelopeFollower to filter cutoff for auto-wah
- Creating complex modulation with multiple sources per destination
- Building modular-style effects with flexible routing

**Example**:
```cpp
#include "dsp/systems/modulation_matrix.h"
#include "dsp/primitives/lfo.h"

// LFO implements ModulationSource interface
class LFOModSource : public Iterum::DSP::ModulationSource {
    Iterum::DSP::LFO& lfo_;
public:
    explicit LFOModSource(Iterum::DSP::LFO& lfo) : lfo_(lfo) {}
    float getCurrentValue() const noexcept override { return lfo_.getCurrentValue(); }
    std::pair<float, float> getSourceRange() const noexcept override { return {-1.0f, 1.0f}; }
};

Iterum::DSP::ModulationMatrix matrix;
matrix.prepare(44100.0, 512, 32);

// Setup LFO and wrap as ModulationSource
Iterum::DSP::LFO lfo;
lfo.prepare(44100.0);
lfo.setRate(2.0f);  // 2 Hz
LFOModSource lfoSource(lfo);

// Register source and destination
matrix.registerSource(0, &lfoSource);
matrix.registerDestination(0, 0.0f, 100.0f, "Delay Time");

// Create route: LFO → Delay Time, 50% depth, bipolar
int route = matrix.createRoute(0, 0, 0.5f, Iterum::DSP::ModulationMode::Bipolar);

// In audio callback
lfo.process();  // Advance LFO
matrix.process(numSamples);

// Get modulated delay time
float baseDelayMs = 50.0f;
float modulatedDelayMs = matrix.getModulatedValue(0, baseDelayMs);  // 50 ± 25ms
```

---

### StereoField

| | |
|---|---|
| **Purpose** | Manages stereo processing modes with width control, panning, L/R offset, and ratio |
| **Location** | [src/dsp/systems/stereo_field.h](src/dsp/systems/stereo_field.h) |
| **Namespace** | `Iterum::DSP` |
| **Added** | 0.0.22 (022-stereo-field) |
| **Dependencies** | DelayLine (L1), OnePoleSmoother (L1), MidSideProcessor (L2), db_utils (L0) |

**Public API**:

```cpp
namespace Iterum::DSP {
    enum class StereoMode : uint8_t {
        Mono,       // Sum L+R, output to both channels
        Stereo,     // Independent L/R processing with ratio
        PingPong,   // Alternating L/R with cross-feedback
        DualMono,   // Same delay time, panned outputs
        MidSide     // M/S encode, delay, decode
    };

    class StereoField {
    public:
        // Lifecycle
        void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
        void reset() noexcept;

        // Mode Control
        void setMode(StereoMode mode) noexcept;
        [[nodiscard]] StereoMode getMode() const noexcept;

        // Delay Time (base delay for all modes)
        void setDelayTimeMs(float ms) noexcept;
        [[nodiscard]] float getDelayTimeMs() const noexcept;

        // Stereo Width (0-200%)
        void setWidth(float widthPercent) noexcept;
        [[nodiscard]] float getWidth() const noexcept;

        // Panning (-100 to +100)
        void setPan(float pan) noexcept;
        [[nodiscard]] float getPan() const noexcept;

        // L/R Timing Offset (±50ms)
        void setLROffset(float offsetMs) noexcept;
        [[nodiscard]] float getLROffset() const noexcept;

        // L/R Delay Ratio (0.1 to 10.0)
        void setLRRatio(float ratio) noexcept;
        [[nodiscard]] float getLRRatio() const noexcept;

        // Processing (real-time safe, noexcept)
        void process(const float* leftIn, const float* rightIn,
                    float* leftOut, float* rightOut,
                    size_t numSamples) noexcept;

        // Query
        [[nodiscard]] bool isPrepared() const noexcept;
    };
}
```

**Behavior**:
- **Mono**: Sum L+R inputs, process through single delay, output identical signals
- **Stereo**: Independent L/R delays with ratio control (L = base × ratio, R = base)
- **PingPong**: Alternating L/R output with configurable cross-feedback
- **DualMono**: Same delay time for both channels, independent pan control
- **MidSide**: Encode to M/S, delay Mid and Side independently, decode back to L/R
- **Width**: 0% = mono, 100% = original stereo, 200% = exaggerated stereo (2× Side)
- **Pan**: Constant-power panning law (sin/cos) for even power distribution
- **L/R Offset**: Haas-style widening with timing differences between channels
- **L/R Ratio**: Polyrhythmic delays (e.g., 3:4 relationship between channels)
- **Mode transitions**: 50ms crossfade for click-free switching
- **Parameter smoothing**: 20ms one-pole smoothing on all parameters

**When to use**:
- Building stereo delay effects with multiple routing options
- Creating wide stereo images with width and offset control
- Implementing ping-pong delays
- Adding polyrhythmic L/R timing relationships
- Processing Mid/Side content separately

**Example**:
```cpp
#include "dsp/systems/stereo_field.h"

Iterum::DSP::StereoField stereo;
stereo.prepare(44100.0, 512, 2000.0f);

// Wide ping-pong delay
stereo.setMode(Iterum::DSP::StereoMode::PingPong);
stereo.setDelayTimeMs(375.0f);
stereo.setWidth(150.0f);  // Enhanced stereo

// In audio callback
stereo.process(leftIn, rightIn, leftOut, rightOut, numSamples);
```

**Example - Polyrhythmic Delay**:
```cpp
stereo.setMode(Iterum::DSP::StereoMode::Stereo);
stereo.setDelayTimeMs(400.0f);  // Base = 400ms for R
stereo.setLRRatio(0.75f);       // L = 300ms (3:4 ratio)

// Creates polyrhythmic delay pattern
stereo.process(leftIn, rightIn, leftOut, rightOut, numSamples);
```

---

## Layer 4: User Features

User-facing delay effect modes that compose Layer 3 systems into complete audio experiences.

### TapeDelay

Classic tape echo emulation (Roland RE-201, Echoplex, Watkins Copicat style).

**File**: `src/dsp/features/tape_delay.h`

**Purpose**: Layer 4 user feature providing vintage tape delay character with motor inertia, wow/flutter, and multi-head echo patterns.

**Composes**:
- TapManager (Layer 3): Multi-head echo patterns at fixed ratios (1x, 1.5x, 2x)
- FeedbackNetwork (Layer 3): Feedback with progressive darkening
- CharacterProcessor (Layer 3): Tape character (wow/flutter, hiss, saturation, rolloff)

**User Controls**:
- Motor Speed: Delay time 20-2000ms with motor inertia (200-500ms transitions)
- Wear: Wow/flutter depth + hiss level
- Saturation: Tape drive/warmth
- Age: EQ rolloff + noise degradation
- Echo Heads: 3 playback heads at fixed timing ratios
- Feedback: 0-120% (>100% enables controlled self-oscillation)
- Mix: Dry/wet balance
- Output Level: -96dB to +12dB

**Supporting Components**:

#### TapeHead

Configuration for a single playback head.

```cpp
struct TapeHead {
    float ratio = 1.0f;        // Timing ratio relative to Motor Speed
    float levelDb = 0.0f;      // Output level [-96, +6] dB
    float pan = 0.0f;          // Stereo position [-100, +100]
    bool enabled = true;       // Head contribution to output
};
```

#### MotorController

Manages delay time with motor inertia simulation.

```cpp
class MotorController {
    void prepare(float sampleRate, size_t maxBlockSize) noexcept;
    void setTargetDelayMs(float ms) noexcept;
    void setInertiaTimeMs(float ms) noexcept;  // 100-1000ms
    float getCurrentDelayMs() const noexcept;
    float process() noexcept;                   // Returns smoothed delay
    bool isTransitioning() const noexcept;
};
```

**Public API**:

```cpp
class TapeDelay {
    // Constants
    static constexpr size_t kNumHeads = 3;
    static constexpr float kMinDelayMs = 20.0f;
    static constexpr float kMaxDelayMs = 2000.0f;
    static constexpr float kHeadRatio1 = 1.0f;
    static constexpr float kHeadRatio2 = 1.5f;
    static constexpr float kHeadRatio3 = 2.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void reset() noexcept;
    bool isPrepared() const noexcept;

    // Motor Speed
    void setMotorSpeed(float ms) noexcept;
    void setMotorInertia(float ms) noexcept;
    float getCurrentDelayMs() const noexcept;
    float getTargetDelayMs() const noexcept;
    bool isTransitioning() const noexcept;

    // Character controls
    void setWear(float amount) noexcept;        // 0-1
    void setSaturation(float amount) noexcept;  // 0-1
    void setAge(float amount) noexcept;         // 0-1

    // Echo Heads
    void setHeadEnabled(size_t headIndex, bool enabled) noexcept;
    void setHeadLevel(size_t headIndex, float levelDb) noexcept;
    void setHeadPan(size_t headIndex, float pan) noexcept;
    TapeHead getHead(size_t headIndex) const noexcept;
    bool isHeadEnabled(size_t headIndex) const noexcept;
    size_t getActiveHeadCount() const noexcept;

    // Output
    void setFeedback(float amount) noexcept;    // 0-1.2
    void setMix(float amount) noexcept;         // 0-1
    void setOutputLevel(float dB) noexcept;     // -96 to +12

    // Processing
    void process(float* left, float* right, size_t numSamples) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;
};
```

**Usage Example**:

```cpp
#include "dsp/features/tape_delay.h"

TapeDelay delay;
delay.prepare(44100.0, 512, 2000.0f);

// Configure for warm, vintage sound
delay.setMotorSpeed(375.0f);   // Classic Space Echo tempo
delay.setWear(0.3f);           // Moderate wow/flutter
delay.setSaturation(0.4f);     // Warm tape drive
delay.setAge(0.2f);            // Slight degradation
delay.setFeedback(0.5f);       // 50% feedback
delay.setMix(0.4f);            // 40% wet

// In process callback
delay.process(leftChannel, rightChannel, blockSize);
```

**When to use**: Creating tape-style delay effects with authentic vintage character. Ideal for adding warmth, movement, and lo-fi texture to delay sounds.

### BBDDelay

Classic bucket-brigade device (BBD) delay emulation (Boss DM-2, EHX Memory Man, Roland Dimension D style).

**File**: `src/dsp/features/bbd_delay.h`

**Purpose**: Layer 4 user feature providing vintage analog delay character with authentic BBD behaviors including bandwidth tracking, compander artifacts, and era-specific chip characteristics.

**Composes**:
- DelayEngine (Layer 3): Core delay with tempo sync capability
- FeedbackNetwork (Layer 3): Feedback with filtering and saturation
- CharacterProcessor (Layer 3): BBD character (bandwidth limiting, clock noise)
- LFO (Layer 1): Triangle modulation for chorus effect

**Unique BBD Behaviors**:
- **Bandwidth Tracking**: Bandwidth inversely proportional to delay time (BBD clock physics) - 15kHz at 20ms down to 2.5kHz at 1000ms
- **Compander Artifacts**: Compression on input, expansion on output creates pumping/breathing
- **Clock Noise**: Proportional to delay time and Age parameter
- **Era Selection**: Different BBD chip characteristics (MN3005, MN3007, MN3205, SAD1024)

**User Controls**:
- Time: Delay time 20-1000ms with automatic bandwidth tracking
- Feedback: 0-120% (>100% enables self-oscillation)
- Modulation: Triangle LFO depth 0-100% for chorus effect
- Modulation Rate: LFO frequency 0.1-10 Hz
- Age: Degradation artifacts 0-100% (noise, bandwidth reduction, compander)
- Era: BBD chip model selection for character
- Mix: Dry/wet balance 0-100%
- Output Level: -96dB to +12dB

**Supporting Enumerations**:

#### BBDChipModel

Chip model selection for era-based character.

```cpp
enum class BBDChipModel : uint8_t {
    MN3005 = 0,   // Panasonic 4096-stage (Memory Man) - widest BW, lowest noise
    MN3007 = 1,   // Panasonic 1024-stage - medium-dark character
    MN3205 = 2,   // Panasonic 4096-stage budget - darker, noisier
    SAD1024 = 3   // Reticon 1024-stage early chip - most noise, limited BW
};
```

**Public API**:

```cpp
class BBDDelay {
    // Constants
    static constexpr float kMinDelayMs = 20.0f;
    static constexpr float kMaxDelayMs = 1000.0f;
    static constexpr float kDefaultDelayMs = 300.0f;
    static constexpr float kDefaultFeedback = 0.4f;
    static constexpr float kMinBandwidthHz = 2500.0f;   // BW at max delay
    static constexpr float kMaxBandwidthHz = 15000.0f;  // BW at min delay

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void reset() noexcept;
    bool isPrepared() const noexcept;

    // Time Control
    void setTime(float ms) noexcept;
    float getTime() const noexcept;

    // Feedback Control
    void setFeedback(float amount) noexcept;  // 0-1.2
    float getFeedback() const noexcept;

    // Modulation Control
    void setModulation(float depth) noexcept;      // 0-1
    void setModulationRate(float rateHz) noexcept; // 0.1-10
    float getModulation() const noexcept;
    float getModulationRate() const noexcept;

    // Age/Degradation Control
    void setAge(float amount) noexcept;  // 0-1
    float getAge() const noexcept;

    // Era Selection
    void setEra(BBDChipModel model) noexcept;
    BBDChipModel getEra() const noexcept;

    // Output Control
    void setMix(float amount) noexcept;        // 0-1
    void setOutputLevel(float dB) noexcept;    // -96 to +12
    float getMix() const noexcept;
    float getOutputLevel() const noexcept;

    // Processing
    void process(float* left, float* right, size_t numSamples) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;
};
```

**Usage Example**:

```cpp
#include "dsp/features/bbd_delay.h"

BBDDelay delay;
delay.prepare(44100.0, 512, 1000.0f);

// Configure for classic Memory Man sound
delay.setTime(350.0f);           // 350ms delay
delay.setFeedback(0.5f);         // 50% feedback
delay.setModulation(0.3f);       // 30% chorus modulation
delay.setModulationRate(0.4f);   // Slow modulation
delay.setAge(0.2f);              // Slight vintage character
delay.setEra(BBDChipModel::MN3005);  // Memory Man chip
delay.setMix(0.4f);              // 40% wet

// In process callback
delay.process(leftChannel, rightChannel, blockSize);
```

**When to use**: Creating analog-style delay effects with authentic BBD character. Ideal for warm, dark delays with subtle modulation and vintage coloration. Use different Era settings to emulate specific vintage units.

### DigitalDelay

Clean digital delay with era presets (Lexicon PCM42, Roland SDE-3000, vintage sampler style).

**File**: `src/dsp/features/digital_delay.h`

**Purpose**: Layer 4 user feature providing pristine digital delay with optional vintage digital character. Three era presets: Pristine (transparent), 80s Digital (vintage warmth), and Lo-Fi (aggressive degradation).

**Composes**:
- DelayEngine (Layer 3): Core delay with tempo sync
- FeedbackNetwork (Layer 3): Feedback path with filtering
- CharacterProcessor (Layer 3): DigitalVintage mode for 80s/Lo-Fi character
- DynamicsProcessor (Layer 2): Program-dependent feedback limiter
- LFO (Layer 1): Modulation with 6 waveform shapes

**User Controls**:
- Time: Delay time 1-10000ms with tempo sync option
- Feedback: 0-120% (>100% enables controlled self-oscillation via limiter)
- Era: Character preset (Pristine, 80s Digital, Lo-Fi)
- Age: Degradation intensity for vintage eras 0-100%
- Modulation: Depth 0-100%, Rate 0.1-10Hz, 6 waveforms
- Limiter Character: Soft/Medium/Hard knee
- Mix: Dry/wet balance
- Output Level: -96dB to +12dB

**Enumerations**:

```cpp
enum class DigitalEra : uint8_t {
    Pristine = 0,        // Crystal-clear, transparent delay
    EightiesDigital = 1, // Vintage digital character (PCM42, SDE-3000)
    LoFi = 2             // Aggressive bit-crushed degradation
};

enum class LimiterCharacter : uint8_t {
    Soft = 0,   // 6dB knee - gentle, musical limiting
    Medium = 1, // 3dB knee - balanced response
    Hard = 2    // 0dB knee - aggressive, brick-wall limiting
};
```

**Public API**:

```cpp
class DigitalDelay {
    // Constants
    static constexpr float kMinDelayMs = 1.0f;
    static constexpr float kMaxDelayMs = 10000.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void reset() noexcept;
    bool isPrepared() const noexcept;

    // Time Control
    void setTime(float ms) noexcept;
    float getTime() const noexcept;
    void setTimeMode(TimeMode mode) noexcept;
    void setNoteValue(NoteValue value) noexcept;

    // Feedback Control
    void setFeedback(float amount) noexcept;      // 0-1.2
    void setLimiterCharacter(LimiterCharacter character) noexcept;

    // Era/Character Control
    void setEra(DigitalEra era) noexcept;
    void setAge(float amount) noexcept;           // 0-1

    // Modulation Control
    void setModulationDepth(float depth) noexcept;    // 0-1
    void setModulationRate(float rateHz) noexcept;    // 0.1-10
    void setModulationWaveform(Waveform waveform) noexcept;

    // Output Control
    void setMix(float amount) noexcept;           // 0-1
    void setOutputLevel(float dB) noexcept;       // -96 to +12

    // Processing
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;
    void process(float* buffer, size_t numSamples, const BlockContext& ctx) noexcept;
};
```

**Usage Example**:

```cpp
#include "dsp/features/digital_delay.h"

DigitalDelay delay;
delay.prepare(44100.0, 512, 10000.0f);

// Configure for pristine digital delay
delay.setTime(500.0f);                    // 500ms delay
delay.setFeedback(0.5f);                  // 50% feedback
delay.setEra(DigitalEra::Pristine);       // Crystal clear
delay.setMix(0.4f);                       // 40% wet

// Or configure for vintage 80s digital
delay.setEra(DigitalEra::EightiesDigital);
delay.setAge(0.3f);                       // Moderate vintage character
delay.setModulationDepth(0.2f);           // Subtle modulation
delay.setModulationRate(1.0f);            // 1Hz rate
delay.setModulationWaveform(Waveform::Sine);

// In process callback
BlockContext ctx;
ctx.sampleRate = 44100.0;
ctx.tempoBPM = 120.0;
delay.process(leftChannel, rightChannel, blockSize, ctx);
```

**When to use**: Creating digital delay effects ranging from pristine transparency to vintage digital character. Ideal for clean, precise delays with optional 80s warmth or lo-fi degradation. Use LFO modulation for chorus-like effects.

### PingPongDelay

Classic stereo ping-pong delay with alternating left/right bounces.

**File**: `src/dsp/features/ping_pong_delay.h`

**Purpose**: Layer 4 user feature providing stereo ping-pong delay effects with L/R timing ratios for polyrhythmic patterns, adjustable cross-feedback routing, stereo width control, and optional LFO modulation.

**Composes**:
- DelayLine (Layer 1): 2 instances for independent L/R delay buffers
- LFO (Layer 1): 2 instances for stereo modulation with 90° phase offset
- OnePoleSmoother (Layer 1): 7 instances for click-free parameter changes
- DynamicsProcessor (Layer 2): Limiting for feedback > 100%
- stereoCrossBlend (Layer 0): Cross-feedback routing between channels

**User Controls**:
- Time: Delay time 1-10000ms with optional tempo sync
- L/R Ratio: 7 preset ratios for polyrhythmic patterns (1:1, 2:1, 3:2, 4:3, 1:2, 2:3, 3:4)
- Cross-Feedback: 0% (dual mono) to 100% (full ping-pong)
- Stereo Width: 0% (mono) to 200% (ultra-wide) using M/S processing
- Feedback: 0-120% (>100% enables controlled self-oscillation)
- Modulation: LFO depth 0-100%, rate 0.1-10Hz
- Mix: Dry/wet balance 0-100%
- Output Level: -120dB to +12dB

**Enumerations**:

#### LRRatio

Preset L/R timing ratios for polyrhythmic ping-pong effects.

\`\`\`cpp
enum class LRRatio : uint8_t {
    OneToOne = 0,     // 1:1 - Classic even ping-pong (L=1.0, R=1.0)
    TwoToOne = 1,     // 2:1 - R is double speed (L=1.0, R=0.5)
    ThreeToTwo = 2,   // 3:2 - Polyrhythmic triplet feel (L=1.0, R=0.667)
    FourToThree = 3,  // 4:3 - Subtle polyrhythm (L=1.0, R=0.75)
    OneToTwo = 4,     // 1:2 - L is double speed (L=0.5, R=1.0)
    TwoToThree = 5,   // 2:3 - Inverse triplet feel (L=0.667, R=1.0)
    ThreeToFour = 6   // 3:4 - Inverse subtle polyrhythm (L=0.75, R=1.0)
};
\`\`\`

**Public API**:

\`\`\`cpp
class PingPongDelay {
    // Constants
    static constexpr float kMinDelayMs = 1.0f;
    static constexpr float kMaxDelayMs = 10000.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void reset() noexcept;
    void snapParameters() noexcept;  // Snap smoothers to targets (for tests/presets)
    bool isPrepared() const noexcept;

    // Time Control
    void setDelayTimeMs(float ms) noexcept;
    float getDelayTimeMs() const noexcept;
    void setTimeMode(TimeMode mode) noexcept;
    TimeMode getTimeMode() const noexcept;
    void setNoteValue(NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept;
    NoteValue getNoteValue() const noexcept;

    // L/R Ratio Control
    void setLRRatio(LRRatio ratio) noexcept;
    LRRatio getLRRatio() const noexcept;

    // Feedback Control
    void setFeedback(float amount) noexcept;       // 0-1.2
    float getFeedback() const noexcept;
    void setCrossFeedback(float amount) noexcept;  // 0-1 (0=dual mono, 1=full ping-pong)
    float getCrossFeedback() const noexcept;

    // Stereo Width Control
    void setWidth(float widthPercent) noexcept;    // 0-200
    float getWidth() const noexcept;

    // Modulation Control
    void setModulationDepth(float depth) noexcept; // 0-1
    float getModulationDepth() const noexcept;
    void setModulationRate(float rateHz) noexcept; // 0.1-10
    float getModulationRate() const noexcept;

    // Output Control
    void setMix(float amount) noexcept;            // 0-1
    float getMix() const noexcept;
    void setOutputLevel(float dB) noexcept;        // -120 to +12
    float getOutputLevel() const noexcept;

    // Processing
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;
};
\`\`\`

**Usage Example**:

\`\`\`cpp
#include "dsp/features/ping_pong_delay.h"

PingPongDelay delay;
delay.prepare(44100.0, 512, 2000.0f);

// Configure for classic ping-pong
delay.setDelayTimeMs(375.0f);           // 375ms delay
delay.setFeedback(0.5f);                // 50% feedback
delay.setCrossFeedback(1.0f);           // Full ping-pong routing
delay.setLRRatio(LRRatio::OneToOne);    // Even L/R timing
delay.setWidth(100.0f);                 // Natural stereo width
delay.setMix(0.4f);                     // 40% wet

// Or configure for polyrhythmic pattern
delay.setLRRatio(LRRatio::ThreeToTwo);  // Triplet feel
delay.setCrossFeedback(0.75f);          // Partial crossing

// In process callback
BlockContext ctx;
ctx.sampleRate = 44100.0;
ctx.tempoBPM = 120.0;
delay.process(leftChannel, rightChannel, blockSize, ctx);
\`\`\`

**When to use**: Creating stereo ping-pong delay effects with alternating L/R bounces. Ideal for adding spatial movement and rhythmic interest. Use L/R ratios for polyrhythmic patterns, adjust cross-feedback for partial or full ping-pong routing.

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
| Filter audio (LP/HP/BP/etc) | `Iterum::DSP::Biquad` | primitives/biquad.h |
| Calculate filter coefficients | `BiquadCoefficients::calculate()` | primitives/biquad.h |
| Steep filter slope (24+ dB/oct) | `Iterum::DSP::BiquadCascade<N>` | primitives/biquad.h |
| Linkwitz-Riley crossover | `BiquadCascade::setLinkwitzRiley()` | primitives/biquad.h |
| Click-free filter modulation | `Iterum::DSP::SmoothedBiquad` | primitives/biquad.h |
| Compile-time filter coeffs | `BiquadCoefficients::calculateConstexpr()` | primitives/biquad.h |
| Check filter stability | `BiquadCoefficients::isStable()` | primitives/biquad.h |
| Apply gain to buffer | `Iterum::DSP::applyGain()` | dsp_utils.h |
| Mix two buffers | `Iterum::DSP::mix()` | dsp_utils.h |
| Smooth parameter changes | `Iterum::DSP::OnePoleSmoother` | dsp_utils.h |
| Hard limit output | `Iterum::DSP::hardClip()` | dsp_utils.h |
| Add saturation | `Iterum::DSP::softClip()` | dsp_utils.h |
| Measure RMS level | `Iterum::DSP::calculateRMS()` | dsp_utils.h |
| Detect peak level | `Iterum::DSP::findPeak()` | dsp_utils.h |
| Oversample for nonlinear DSP | `Iterum::DSP::Oversampler2x` | primitives/oversampler.h |
| 4x oversample for heavy distortion | `Iterum::DSP::Oversampler4x` | primitives/oversampler.h |
| Zero-latency oversampling | `Oversampler.prepare(..., ZeroLatency)` | primitives/oversampler.h |
| Get oversampler latency | `Oversampler::getLatency()` | primitives/oversampler.h |
| Generate window function | `Window::generateHann()` | core/window_functions.h |
| Verify COLA property | `Window::verifyCOLA()` | core/window_functions.h |
| Forward FFT (time→frequency) | `FFT::forward()` | primitives/fft.h |
| Inverse FFT (frequency→time) | `FFT::inverse()` | primitives/fft.h |
| Access spectrum magnitude | `SpectralBuffer::getMagnitude()` | primitives/spectral_buffer.h |
| Modify spectrum magnitude | `SpectralBuffer::setMagnitude()` | primitives/spectral_buffer.h |
| Streaming spectral analysis | `STFT::pushSamples()` / `analyze()` | primitives/stft.h |
| Overlap-add synthesis | `OverlapAdd::synthesize()` | primitives/stft.h |
| Full STFT→modify→ISTFT pipeline | STFT + SpectralBuffer + OverlapAdd | primitives/stft.h |
| Complete filter with modulation | `Iterum::DSP::MultimodeFilter` | processors/multimode_filter.h |
| Selectable filter slope | `MultimodeFilter::setSlope()` | processors/multimode_filter.h |
| Click-free parameter changes | `MultimodeFilter::setSmoothingTime()` | processors/multimode_filter.h |
| Pre-filter saturation | `MultimodeFilter::setDrive()` | processors/multimode_filter.h |
| Track amplitude envelope | `Iterum::DSP::EnvelopeFollower` | processors/envelope_follower.h |
| Set envelope mode | `EnvelopeFollower::setMode()` | processors/envelope_follower.h |
| Set attack/release times | `EnvelopeFollower::setAttackTime()` | processors/envelope_follower.h |
| Enable sidechain filter | `EnvelopeFollower::setSidechainEnabled()` | processors/envelope_follower.h |
| Get current envelope value | `EnvelopeFollower::getCurrentValue()` | processors/envelope_follower.h |
| Compress/limit dynamics | `Iterum::DSP::DynamicsProcessor` | processors/dynamics_processor.h |
| Set compressor threshold | `DynamicsProcessor::setThreshold()` | processors/dynamics_processor.h |
| Set compression ratio | `DynamicsProcessor::setRatio()` | processors/dynamics_processor.h |
| Set soft knee | `DynamicsProcessor::setKneeWidth()` | processors/dynamics_processor.h |
| Enable auto-makeup gain | `DynamicsProcessor::setAutoMakeup()` | processors/dynamics_processor.h |
| Enable lookahead | `DynamicsProcessor::setLookahead()` | processors/dynamics_processor.h |
| Get gain reduction (metering) | `DynamicsProcessor::getCurrentGainReduction()` | processors/dynamics_processor.h |
| Duck audio with sidechain | `Iterum::DSP::DuckingProcessor` | processors/ducking_processor.h |
| Set ducking threshold | `DuckingProcessor::setThreshold()` | processors/ducking_processor.h |
| Set ducking depth | `DuckingProcessor::setDepth()` | processors/ducking_processor.h |
| Set hold time | `DuckingProcessor::setHoldTime()` | processors/ducking_processor.h |
| Set range limit | `DuckingProcessor::setRange()` | processors/ducking_processor.h |
| Enable sidechain HPF | `DuckingProcessor::setSidechainFilterEnabled()` | processors/ducking_processor.h |
| Get ducking GR (metering) | `DuckingProcessor::getCurrentGainReduction()` | processors/ducking_processor.h |
| Generate random numbers | `Iterum::DSP::Xorshift32` | core/random.h |
| Get bipolar random [-1,1] | `Xorshift32::nextFloat()` | core/random.h |
| Get unipolar random [0,1] | `Xorshift32::nextUnipolar()` | core/random.h |
| Generate multi-type noise | `Iterum::DSP::NoiseGenerator` | processors/noise_generator.h |
| Enable noise type | `NoiseGenerator::setNoiseEnabled()` | processors/noise_generator.h |
| Set noise level | `NoiseGenerator::setNoiseLevel()` | processors/noise_generator.h |
| Configure tape hiss | `NoiseGenerator::setTapeHissParams()` | processors/noise_generator.h |
| Configure vinyl crackle | `NoiseGenerator::setCrackleParams()` | processors/noise_generator.h |
| Configure asperity | `NoiseGenerator::setAsperityParams()` | processors/noise_generator.h |
| Mix noise with audio | `NoiseGenerator::processMix()` | processors/noise_generator.h |
| Create tempo-synced delay | `Iterum::DSP::DelayEngine` | systems/delay_engine.h |
| Set delay time mode | `DelayEngine::setTimeMode()` | systems/delay_engine.h |
| Set delay time ms | `DelayEngine::setDelayTimeMs()` | systems/delay_engine.h |
| Set delay note value | `DelayEngine::setNoteValue()` | systems/delay_engine.h |
| Create feedback network | `Iterum::DSP::FeedbackNetwork` | systems/feedback_network.h |
| Set feedback amount | `FeedbackNetwork::setFeedbackAmount()` | systems/feedback_network.h |
| Set cross-feedback (ping-pong) | `FeedbackNetwork::setCrossFeedbackAmount()` | systems/feedback_network.h |
| Enable feedback freeze | `FeedbackNetwork::setFrozen()` | systems/feedback_network.h |
| Set feedback filter | `FeedbackNetwork::setFilterCutoff()` | systems/feedback_network.h |
| Route modulation sources | `Iterum::DSP::ModulationMatrix` | systems/modulation_matrix.h |
| Register modulation source | `ModulationMatrix::registerSource()` | systems/modulation_matrix.h |
| Register modulation destination | `ModulationMatrix::registerDestination()` | systems/modulation_matrix.h |
| Create modulation route | `ModulationMatrix::createRoute()` | systems/modulation_matrix.h |
| Set route depth | `ModulationMatrix::setRouteDepth()` | systems/modulation_matrix.h |
| Get modulated value | `ModulationMatrix::getModulatedValue()` | systems/modulation_matrix.h |
| Query current modulation | `ModulationMatrix::getCurrentModulation()` | systems/modulation_matrix.h |
| Create stereo processor | `Iterum::DSP::StereoField` | systems/stereo_field.h |
| Set stereo mode | `StereoField::setMode()` | systems/stereo_field.h |
| Set stereo width | `StereoField::setWidth()` | systems/stereo_field.h |
| Set stereo pan | `StereoField::setPan()` | systems/stereo_field.h |
| Set L/R timing offset | `StereoField::setLROffset()` | systems/stereo_field.h |
| Set L/R delay ratio | `StereoField::setLRRatio()` | systems/stereo_field.h |
| Create tape delay effect | `Iterum::DSP::TapeDelay` | features/tape_delay.h |
| Create BBD delay effect | `Iterum::DSP::BBDDelay` | features/bbd_delay.h |
| Create digital delay effect | `Iterum::DSP::DigitalDelay` | features/digital_delay.h |
| Set digital delay era | `DigitalDelay::setEra()` | features/digital_delay.h |
| Set limiter character | `DigitalDelay::setLimiterCharacter()` | features/digital_delay.h |
| Set delay modulation | `DigitalDelay::setModulationDepth()` | features/digital_delay.h |
| Create ping-pong delay | `Iterum::DSP::PingPongDelay` | features/ping_pong_delay.h |
| Set L/R ratio (ping-pong) | `PingPongDelay::setLRRatio()` | features/ping_pong_delay.h |
| Set cross-feedback | `PingPongDelay::setCrossFeedback()` | features/ping_pong_delay.h |
| Set ping-pong width | `PingPongDelay::setWidth()` | features/ping_pong_delay.h |
