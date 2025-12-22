# Changelog

All notable changes to Iterum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.5] - 2025-12-23

### Added

- **Layer 1 DSP Primitive: Parameter Smoother** (`src/dsp/primitives/smoother.h`)
  - Three smoother types for different modulation characteristics:
    - `OnePoleSmoother` - Exponential approach (RC filter behavior)
    - `LinearRamp` - Constant rate change (tape-like pitch effects)
    - `SlewLimiter` - Maximum rate limiting with separate rise/fall rates
  - Sub-sample accurate transitions for artifact-free automation
  - Real-time safe: `noexcept`, zero allocations in `process()`
  - Configurable smoothing time (0.1ms - 1000ms)
  - Completion detection with configurable threshold (0.0001 default)
  - Denormal flushing (< 1e-15 → 0)
  - NaN/Infinity protection (NaN → 0, Inf → clamped)
  - Cross-platform NOINLINE macro for /fp:fast compatibility

- **Smoother characteristics**:
  - `OnePoleSmoother`: 99% convergence at specified time, exponential decay
  - `LinearRamp`: Fixed-duration transitions regardless of distance
  - `SlewLimiter`: Asymmetric rise/fall rates for envelope-like behavior

- **Comprehensive test suite** (5,320 assertions across 57 test cases)
  - All user stories covered (US1-US5)
  - Exponential convergence verification
  - Linear ramp timing accuracy
  - Slew rate limiting behavior
  - NaN/Infinity edge case handling
  - Completion detection with threshold
  - Reset and snap-to-target functionality

### Changed

- **Constitution v1.6.0**: Added Principle XIV (Pre-Implementation Research / ODR Prevention)
  - Mandatory codebase search before creating new classes
  - Diagnostic checklist for ODR symptoms (garbage values, test failures)
  - Lesson learned from 005-parameter-smoother incident

- **Planning templates**: Added mandatory codebase research gates
  - `spec-template.md`: "Existing Codebase Components" section
  - `plan-template.md`: Full "Codebase Research" section with search tables

- **CLAUDE.md**: Added "Pre-Implementation Research" section with ODR prevention checklist

### Fixed

- Removed duplicate `constexprExp` and `isNaN` functions from `smoother.h` (ODR violation)
- Updated `test_approvals_main.cpp` to use new OnePoleSmoother API

### Technical Details

- **Smoothing formulas**:
  - One-pole: `y = target + coeff * (y - target)` where `coeff = exp(-5000 / (timeMs * sampleRate))`
  - Linear: `y += increment` where `increment = delta / (timeMs * sampleRate / 1000)`
  - Slew: `y += clamp(target - y, -maxFall, +maxRise)`
- **Time constant**: Specified time is to 99% (5 tau), not 63% (1 tau)
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Dependencies**: `dsp/core/db_utils.h` for shared math utilities
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XIV (ODR Prevention)

### Usage

```cpp
#include "dsp/primitives/smoother.h"

using namespace Iterum::DSP;

// One-pole smoother for filter cutoff
OnePoleSmoother cutoffSmoother;
cutoffSmoother.configure(10.0f, 44100.0f);  // 10ms to 99%
cutoffSmoother.setTarget(2000.0f);

// In processBlock()
for (size_t i = 0; i < numSamples; ++i) {
    float smoothedCutoff = cutoffSmoother.process();
    filter.setCutoff(smoothedCutoff);
    output[i] = filter.process(input[i]);
}

// Linear ramp for delay time (tape effect)
LinearRamp delayRamp;
delayRamp.configure(100.0f, 44100.0f);  // Always 100ms transitions
delayRamp.setTarget(newDelayMs);

// Slew limiter for envelope follower
SlewLimiter envelope;
envelope.configure(10.0f, 100.0f, 44100.0f);  // Fast attack, slow release
envelope.setTarget(inputLevel);
float smoothedEnvelope = envelope.process();
```

---

## [0.0.4] - 2025-12-22

### Added

- **Layer 1 DSP Primitive: Biquad Filter** (`src/dsp/primitives/biquad.h`)
  - Second-order IIR filter using Transposed Direct Form II topology
  - 8 filter types from Robert Bristow-Johnson's Audio EQ Cookbook:
    - `Lowpass` - 12 dB/oct rolloff above cutoff
    - `Highpass` - 12 dB/oct rolloff below cutoff
    - `Bandpass` - Peak at center, rolloff both sides
    - `Notch` - Null at center frequency
    - `Allpass` - Flat magnitude, phase shift only
    - `LowShelf` - Boost/cut below shelf frequency
    - `HighShelf` - Boost/cut above shelf frequency
    - `Peak` - Parametric EQ bell curve
  - `BiquadCascade<N>` template for steeper slopes (24/36/48 dB/oct)
  - `SmoothedBiquad` for click-free coefficient modulation
  - Butterworth configuration (maximally flat passband)
  - Linkwitz-Riley configuration (flat sum at crossover)
  - Constexpr coefficient calculation for compile-time EQ
  - Denormal flushing (state < 1e-15 → 0)
  - NaN protection (returns 0, resets state)
  - Stability validation (Jury criterion)

- **Type aliases for common filter slopes**:
  - `Biquad12dB` - Single stage, 12 dB/oct
  - `Biquad24dB` - 2-stage cascade, 24 dB/oct
  - `Biquad36dB` - 3-stage cascade, 36 dB/oct
  - `Biquad48dB` - 4-stage cascade, 48 dB/oct

- **Utility functions**:
  - `butterworthQ(stageIndex, totalStages)` - Q values for Butterworth cascades
  - `linkwitzRileyQ(stageIndex, totalStages)` - Q values for LR crossovers
  - `BiquadCoefficients::isStable()` - Stability check
  - `BiquadCoefficients::isBypass()` - Bypass detection

- **Comprehensive test suite** (180 assertions across 49 test cases)
  - All 6 user stories covered (US1-US6)
  - Filter type coefficient verification
  - Frequency response tests at cutoff
  - Cascade slope verification (24/48 dB/oct)
  - Linkwitz-Riley flat sum at crossover
  - Smoothed coefficient convergence
  - Click-free modulation verification
  - Constexpr compile-time evaluation
  - Edge cases (frequency clamping, Q limits, denormals)

### Technical Details

- **TDF2 processing**: `y = b0*x + s0; s0 = b1*x - a1*y + s1; s1 = b2*x - a2*y`
- **Constexpr math**: Custom Taylor series for sin/cos (MSVC compatibility)
- **Smoothing**: One-pole interpolation per coefficient (1-100ms typical)
- **Stability**: Jury criterion with epsilon tolerance for boundary cases
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First)

### Usage

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
modFilter.setSmoothingTime(10.0f, 44100.0f);
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

## [0.0.3] - 2025-12-22

### Added

- **Layer 1 DSP Primitive: LFO (Low Frequency Oscillator)** (`src/dsp/primitives/lfo.h`)
  - Wavetable-based oscillator for generating modulation signals
  - 6 waveform types:
    - `Sine` - Smooth sinusoidal, starts at zero crossing
    - `Triangle` - Linear ramp 0→1→-1→0
    - `Sawtooth` - Linear ramp -1→+1, instant reset
    - `Square` - Binary +1/-1 alternation
    - `SampleHold` - Random value held for each cycle
    - `SmoothRandom` - Interpolated random, smooth transitions
  - Wavetable generation with 2048 samples per table
  - Double-precision phase accumulator (< 0.0001° drift over 24 hours)
  - Linear interpolation for smooth wavetable reading
  - Tempo sync with all note values (1/1 to 1/32)
  - Dotted and triplet modifiers
  - Phase offset (0-360°) for stereo LFO configurations
  - Retrigger functionality for note-on synchronization
  - Real-time safe: `noexcept`, no allocations in `process()`
  - Frequency range: 0.01-20 Hz

- **Enumerations for LFO configuration**:
  - `Iterum::DSP::Waveform` - 6 waveform types
  - `Iterum::DSP::NoteValue` - Note divisions (Whole to ThirtySecond)
  - `Iterum::DSP::NoteModifier` - None, Dotted, Triplet

- **Comprehensive test suite** (145,739 assertions across 87 test cases)
  - All 6 user stories covered (US1-US6)
  - Waveform shape verification
  - Tempo sync frequency calculations
  - Phase offset and retrigger behavior
  - Real-time safety verification (noexcept static_assert)
  - Edge case coverage (frequency clamping, phase wrapping)

### Technical Details

- **Wavetable size**: 2048 samples per waveform (power of 2 for efficient wrapping)
- **Phase precision**: Double-precision accumulator prevents drift over extended sessions
- **Tempo sync formula**: `frequency = BPM / (60 × noteBeats)`
- **Random generator**: LCG (Linear Congruential Generator) for deterministic, real-time safe randomness
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), XII (Test-First)

### Usage

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
                 Iterum::DSP::NoteModifier::Dotted);  // 1.33 Hz

// Stereo chorus with phase offset
Iterum::DSP::LFO lfoLeft, lfoRight;
lfoLeft.prepare(44100.0);
lfoRight.prepare(44100.0);
lfoLeft.setPhaseOffset(0.0f);
lfoRight.setPhaseOffset(90.0f);  // 90° offset for stereo width
```

---

## [0.0.2] - 2025-12-22

### Added

- **Layer 1 DSP Primitive: DelayLine** (`src/dsp/primitives/delay_line.h`)
  - Real-time safe circular buffer delay line with fractional sample interpolation
  - Three read methods for different use cases:
    - `read(size_t)` - Integer delay, no interpolation (fastest)
    - `readLinear(float)` - Linear interpolation for modulated delays (chorus, flanger, vibrato)
    - `readAllpass(float)` - Allpass interpolation for feedback loops (unity gain at all frequencies)
  - Power-of-2 buffer sizing for O(1) bitwise wraparound
  - `prepare(sampleRate, maxDelaySeconds)` - Allocates buffer before processing
  - `reset()` - Clears buffer to silence without reallocation
  - Query methods: `maxDelaySamples()`, `sampleRate()`

- **Utility function**: `Iterum::DSP::nextPowerOf2(size_t)` - Constexpr power-of-2 calculation

- **Comprehensive test suite** (436 assertions across 50 test cases)
  - Basic fixed delay (write/read operations)
  - Linear interpolation accuracy
  - Allpass interpolation unity gain verification
  - Modulation smoothness tests
  - Real-time safety verification (noexcept static_assert)
  - O(1) performance characteristics
  - Edge case coverage (clamping, wrap-around)

### Technical Details

- **Interpolation formulas**:
  - Linear: `y = y0 + frac * (y1 - y0)`
  - Allpass: `y = x0 + a * (state - x1)` where `a = (1 - frac) / (1 + frac)`
- **Buffer sizing**: Next power of 2 >= (maxDelaySamples + 1) for efficient bitwise AND wrap
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), XII (Test-First)

### Usage

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
float lfoDelay = 500.0f + 20.0f * lfoValue;
float chorus = delay.readLinear(lfoDelay);

// Feedback network (fractional comb filter)
float comb = delay.readAllpass(100.5f);  // Fixed fractional delay
```

---

## [0.0.1] - 2025-12-22

### Added

- **Layer 0 Core Utilities: dB/Linear Conversion** (`src/dsp/core/db_utils.h`)
  - `Iterum::DSP::dbToGain(float dB)` - Convert decibels to linear gain
  - `Iterum::DSP::gainToDb(float gain)` - Convert linear gain to decibels
  - `Iterum::DSP::kSilenceFloorDb` - Silence floor constant (-144 dB)
  - Full C++20 `constexpr` support for compile-time evaluation
  - Real-time safe: no allocation, no exceptions, no I/O
  - NaN handling: `dbToGain(NaN)` returns 0.0f, `gainToDb(NaN)` returns -144 dB

- **Custom constexpr math implementations** (MSVC compatibility)
  - Taylor series `constexprExp()` and `constexprLn()` functions
  - Required because MSVC lacks constexpr `std::pow`/`std::log10`

- **Comprehensive test suite** (146 assertions across 24 test cases)
  - Unit tests for all dB conversion functions
  - Constexpr compile-time evaluation tests
  - Edge case coverage (NaN, infinity, silence)

- **Project infrastructure**
  - Layered DSP architecture (Layer 0-4 hierarchy)
  - Test-first development workflow (Constitution Principle XII)
  - Catch2 testing framework integration

### Technical Details

- **Silence floor**: -144 dB (24-bit dynamic range: 6.02 dB/bit * 24 = ~144 dB)
- **Formulas**:
  - `dbToGain`: gain = 10^(dB/20)
  - `gainToDb`: dB = 20 * log10(gain), clamped to -144 dB floor
- **Namespace**: `Iterum::DSP` (Layer 0 core utilities)

### Usage

```cpp
#include "dsp/core/db_utils.h"

// Runtime conversion
float gain = Iterum::DSP::dbToGain(-6.0f);    // ~0.5
float dB   = Iterum::DSP::gainToDb(0.5f);     // ~-6 dB

// Compile-time lookup tables
constexpr std::array<float, 3> gains = {
    Iterum::DSP::dbToGain(-20.0f),  // 0.1
    Iterum::DSP::dbToGain(0.0f),    // 1.0
    Iterum::DSP::dbToGain(20.0f)    // 10.0
};
```
