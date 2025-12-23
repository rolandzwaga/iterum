# Iterum Architecture

This document is the **living inventory** of all functional domains, components, and APIs in the Iterum project. It serves as the canonical reference when writing new specs to avoid duplication and ensure proper reuse.

> **Constitution Principle XIII**: Every spec implementation MUST update this document as a final task.

**Last Updated**: 2025-12-23 (006-oversampler)

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
