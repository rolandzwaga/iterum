# Changelog

All notable changes to Iterum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.11] - 2025-12-23

### Added

- **Layer 2 DSP Processor: EnvelopeFollower** (`src/dsp/processors/envelope_follower.h`)
  - Amplitude envelope tracking processor for dynamics processing
  - Three detection modes with distinct characteristics:
    - `Amplitude` - Full-wave rectification + asymmetric smoothing (~0.637 for sine)
    - `RMS` - Squared signal + blended smoothing + sqrt (~0.707 for sine, true RMS)
    - `Peak` - Instant attack (at min), exponential release (~1.0 captures peaks)
  - Configurable attack time [0.1-500ms] with one-pole smoothing
  - Configurable release time [1-5000ms] with one-pole smoothing
  - Optional sidechain highpass filter [20-500Hz] to reduce bass pumping
  - Per-sample processing (`processSample()`) for real-time envelope tracking
  - Block processing (`process()`) with envelope output buffer
  - `getCurrentValue()` for reading envelope without advancing state
  - Zero latency (Biquad TDF2 sidechain filter has no latency)
  - Real-time safe: `noexcept`, no allocations in `process()`
  - NaN/Infinity input handling (returns 0 for NaN, clamps Inf)
  - Denormal flushing for consistent CPU performance

- **Comprehensive test suite** (36 test cases covering all user stories)
  - US1: Basic detection modes with waveform verification
  - US2: Configurable attack/release timing accuracy
  - US3: Sample and block processing API
  - US4: Smooth parameter changes without discontinuities
  - US5: Sidechain filter for bass-insensitive detection
  - US6: Edge cases (NaN, Infinity, denormals, zero input)
  - US7: Mode switching continuity (no pops/clicks)
  - RMS accuracy within 1% of theoretical (0.707 for sine)
  - Attack/release coefficient verification via exponential decay

### Technical Details

- **Detection formulas**:
  - Amplitude: `rectified = |x|; env = rect + coeff * (env - rect)` (asymmetric)
  - RMS: `squared = x²; sqEnv = sq + blendedCoeff * (sqEnv - sq); env = √sqEnv`
  - Peak: Instant capture when `|x| > env`, exponential release otherwise
- **RMS blended coefficient**: `rmsCoeff = attackCoeff * 0.25 + releaseCoeff * 0.75`
  - Provides symmetric averaging while maintaining transient response
  - Achieves true RMS (0.707) for sine wave within 1% tolerance
- **Coefficient formula**: `coeff = exp(-1.0 / (timeMs * 0.001 * sampleRate))`
- **Sidechain filter**: Biquad highpass (Butterworth Q=0.707) at configurable cutoff
- **Dependencies**: db_utils.h (isNaN, isInf, flushDenormal, constexprExp), biquad.h
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XV (Honest Completion)

### Usage

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

// In processBlock() - per-sample tracking for compressor
for (size_t i = 0; i < numSamples; ++i) {
    float envelope = env.processSample(input[i]);
    float gainReduction = calculateGainReduction(envelope);
    output[i] = input[i] * gainReduction;
}

// Or block processing (envelope output buffer)
std::vector<float> envelopeBuffer(numSamples);
env.process(input, envelopeBuffer.data(), numSamples);

// Gate trigger (Peak mode with fast attack)
env.setMode(DetectionMode::Peak);
env.setAttackTime(0.1f);  // Near-instant
env.setReleaseTime(50.0f);
```

---

## [0.0.10] - 2025-12-23

### Added

- **Layer 2 DSP Processor: SaturationProcessor** (`src/dsp/processors/saturation_processor.h`)
  - Analog-style saturation/waveshaping processor composing Layer 1 primitives
  - 5 saturation algorithm types with distinct harmonic characteristics:
    - `Tape` - tanh(x), symmetric odd harmonics, warm classic saturation
    - `Tube` - Asymmetric polynomial (x + 0.3x² - 0.15x³), even harmonics, rich character
    - `Transistor` - Hard-knee soft clip at 0.5 threshold, aggressive clipping
    - `Digital` - Hard clip (clamp ±1.0), harsh all-harmonic distortion
    - `Diode` - Asymmetric exp/linear curve, subtle warmth with even harmonics
  - 2x oversampling via `Oversampler<2,1>` for alias-free nonlinear processing
  - Automatic DC blocking (10Hz highpass biquad) after asymmetric saturation
  - Input/output gain staging [-24dB, +24dB] for drive and makeup control
  - Dry/wet mix control [0.0, 1.0] with efficiency bypass at 0%
  - Parameter smoothing (5ms) via OnePoleSmoother for click-free modulation
  - Block processing (`process()`) and sample processing (`processSample()`)
  - Latency reporting for host delay compensation
  - Real-time safe: `noexcept`, no allocations in `process()`

- **Comprehensive test suite** (5,162 assertions across 28 test cases)
  - All 7 user stories covered (US1-US7)
  - DFT-based harmonic analysis for saturation verification
  - THD (Total Harmonic Distortion) increases with drive
  - Odd/even harmonic balance per saturation type
  - Gain staging verification (input/output properly applied)
  - Mix blending accuracy (dry/wet interpolation)
  - DC blocking effectiveness verification
  - Real-time safety verification (noexcept static_assert)
  - Edge case coverage (NaN, infinity, denormals, max drive)

### Technical Details

- **Saturation formulas**:
  - Tape: `y = tanh(x)`
  - Tube: `y = tanh(x + 0.3x² - 0.15x³)` - polynomial creates even harmonics
  - Transistor: Linear below 0.5, then `y = 0.5 + 0.5 * tanh((|x| - 0.5) / 0.5)`
  - Digital: `y = clamp(x, -1, +1)`
  - Diode: Forward `y = 1 - exp(-1.5x)`, Reverse `y = x / (1 - 0.5x)`
- **Oversampling**: 2x factor using IIR anti-aliasing filters
- **DC blocker**: 10Hz highpass biquad (Q=0.707) removes asymmetric DC offset
- **Smoothing**: 5ms one-pole RC filter for input gain, output gain, and mix
- **Constants**:
  - `kMinGainDb = -24.0f`, `kMaxGainDb = +24.0f`
  - `kDefaultSmoothingMs = 5.0f`
  - `kDCBlockerCutoffHz = 10.0f`
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XV (Honest Completion)

### Usage

```cpp
#include "dsp/processors/saturation_processor.h"

using namespace Iterum::DSP;

SaturationProcessor sat;

// In prepare() - allocates buffers
sat.prepare(44100.0, 512);
sat.setType(SaturationType::Tape);
sat.setInputGain(12.0f);   // +12 dB drive
sat.setOutputGain(-6.0f);  // -6 dB makeup
sat.setMix(1.0f);          // 100% wet

// In processBlock() - real-time safe
sat.process(buffer, numSamples);

// Tube saturation for warmth
sat.setType(SaturationType::Tube);
sat.setInputGain(6.0f);  // Moderate drive for even harmonics

// Parallel saturation (NY compression style)
sat.setMix(0.5f);  // 50% wet blends with dry signal

// Get latency for delay compensation
size_t latency = sat.getLatency();

// Sample-accurate processing (less efficient)
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = sat.processSample(buffer[i]);
}
```

---

## [0.0.9] - 2025-12-23

### Added

- **Layer 2 DSP Processor: MultimodeFilter** (`src/dsp/processors/multimode_filter.h`)
  - Complete filter module composing Layer 1 primitives (Biquad, OnePoleSmoother, Oversampler)
  - 8 filter types from RBJ Audio EQ Cookbook:
    - `Lowpass` - Low frequencies pass, attenuates above cutoff
    - `Highpass` - High frequencies pass, attenuates below cutoff
    - `Bandpass` - Passes band around cutoff frequency
    - `Notch` - Rejects band around cutoff frequency
    - `Allpass` - Flat magnitude, phase shift only (for phaser effects)
    - `LowShelf` - Boost/cut below shelf frequency
    - `HighShelf` - Boost/cut above shelf frequency
    - `Peak` - Parametric EQ bell curve (boost/cut at center)
  - 4 selectable slopes for LP/HP/BP/Notch:
    - `Slope12dB` - 12 dB/oct (1 biquad stage)
    - `Slope24dB` - 24 dB/oct (2 biquad stages, Butterworth Q)
    - `Slope36dB` - 36 dB/oct (3 biquad stages)
    - `Slope48dB` - 48 dB/oct (4 biquad stages)
  - Parameter smoothing via OnePoleSmoother (5ms default, configurable)
  - Pre-filter drive/saturation with 2x oversampled tanh waveshaping
  - Block processing (`process()`) with per-block coefficient updates
  - Sample processing (`processSample()`) for sample-accurate modulation
  - Real-time safe: `noexcept`, no allocations in `process()`

- **Comprehensive test suite** (1,686 assertions across 21 test cases)
  - All 7 user stories covered (US1-US7)
  - Slope verification: 24dB LP/HP attenuation at octave boundaries
  - Bandpass bandwidth verification (Q relationship)
  - Click-free modulation testing
  - Self-oscillation at high resonance
  - Drive THD verification (harmonics added)
  - Real-time safety verification (noexcept static_assert)

### Technical Details

- **Butterworth Q formula**: `Q[i] = 1 / (2 * cos(π * (2i + 1) / (4 * N)))` for N stages
- **Slope mapping**: `FilterSlope` enum values 1-4 map directly to stage counts
- **Smoothing**: One-pole RC filter per parameter (cutoff, resonance, gain, drive)
- **Drive saturation**: `tanh(sample * driveGain)` at 2x oversampled rate
- **Latency**: 0 samples (drive disabled) or `oversampler.getLatency()` (drive enabled)
- **Parameter ranges**:
  - Cutoff: 20 Hz to Nyquist/2
  - Resonance (Q): 0.1 to 100
  - Gain: -24 to +24 dB (for Shelf/Peak types)
  - Drive: 0 to 24 dB
- **Namespace**: `Iterum::DSP` (Layer 2 DSP processors)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XV (Honest Completion)

### Usage

```cpp
#include "dsp/processors/multimode_filter.h"

using namespace Iterum::DSP;

MultimodeFilter filter;

// In prepare() - allocates buffers
filter.prepare(44100.0, 512);
filter.setType(FilterType::Lowpass);
filter.setSlope(FilterSlope::Slope24dB);  // 24 dB/oct
filter.setCutoff(1000.0f);
filter.setResonance(0.707f);  // Butterworth Q

// In processBlock() - real-time safe
filter.process(buffer, numSamples);

// LFO modulated filter
filter.setSmoothingTime(5.0f);  // 5ms smoothing
float lfoValue = lfo.process();
float cutoff = 1000.0f + lfoValue * 800.0f;  // 200-1800 Hz
filter.setCutoff(cutoff);
filter.process(buffer, numSamples);

// Pre-filter saturation
filter.setDrive(12.0f);  // 12dB drive (oversampled)
filter.process(buffer, numSamples);

// Sample-accurate modulation (more CPU)
for (size_t i = 0; i < numSamples; ++i) {
    filter.setCutoff(modulatedCutoff[i]);
    buffer[i] = filter.processSample(buffer[i]);
}

// High resonance (self-oscillation)
filter.setResonance(80.0f);  // Rings at cutoff frequency
```

---

## [0.0.8] - 2025-12-23

### Added

- **Layer 1 DSP Primitive: FFT Processor** (`src/dsp/primitives/fft.h`)
  - Radix-2 Cooley-Tukey FFT implementation for spectral processing
  - Forward real-to-complex FFT (`forward()`)
  - Inverse complex-to-real FFT (`inverse()`)
  - Power-of-2 sizes: 256, 512, 1024, 2048, 4096, 8192
  - O(N log N) time complexity
  - Real-time safe: `noexcept`, pre-allocated twiddle factors and bit-reversal table
  - N/2+1 complex bins output for N-point real FFT

- **Window Functions** (`src/dsp/primitives/window_functions.h`)
  - Hann window - cosine-based, good frequency resolution
  - Hamming window - reduced first sidelobe
  - Blackman window - excellent sidelobe rejection (-58 dB)
  - Kaiser window (β=9.0) - configurable main lobe/sidelobe tradeoff
  - All windows normalized for COLA (Constant Overlap-Add) reconstruction
  - Pre-computed lookup tables for real-time performance

- **STFT (Short-Time Fourier Transform)** (`src/dsp/primitives/stft.h`)
  - Frame-by-frame spectral analysis
  - Configurable hop size (50%/75% overlap)
  - Window application before FFT
  - Continuous streaming audio input
  - `analyze()` returns spectrum when frame is ready
  - `reset()` clears internal state without reallocation

- **Overlap-Add Synthesis** (`src/dsp/primitives/stft.h`)
  - Artifact-free audio reconstruction from spectral frames
  - COLA normalization for perfect reconstruction
  - Configurable hop size matching analysis
  - Output accumulator for smooth frame overlap
  - `synthesize()` accepts spectrum, returns audio when ready

- **SpectralBuffer** (`src/dsp/primitives/spectral_buffer.h`)
  - Complex spectrum storage and manipulation
  - Polar access: `getMagnitude()`, `getPhase()`, `setMagnitude()`, `setPhase()`
  - Cartesian access: `getReal()`, `getImag()`, `setCartesian()`
  - Raw data access for FFT input/output
  - Building block for spectral effects (filtering, freeze, morphing)

- **Comprehensive FFT test suite** (421,777 assertions across 287 test cases)
  - All 6 user stories covered (US1-US6)
  - Forward FFT frequency bin accuracy (< 1 bin error)
  - Round-trip reconstruction (< 0.0001% error)
  - STFT/ISTFT perfect reconstruction (< 0.01% error)
  - Window function shape verification
  - O(N log N) complexity verification
  - Real-time safety verification (noexcept static_assert)

### Technical Details

- **FFT formulas**:
  - Forward: `X[k] = Σ x[n] * e^(-j2πkn/N)`
  - Inverse: `x[n] = (1/N) * Σ X[k] * e^(j2πkn/N)`
  - Twiddle factor: `W_N^k = e^(-j2πk/N) = cos(2πk/N) - j*sin(2πk/N)`
- **COLA windows**: Hann with 50% overlap, Hann with 75% overlap both sum to constant
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), X (DSP Constraints), XII (Test-First), XV (Honest Completion)

### Usage

```cpp
#include "dsp/primitives/fft.h"
#include "dsp/primitives/window_functions.h"
#include "dsp/primitives/stft.h"
#include "dsp/primitives/spectral_buffer.h"

using namespace Iterum::DSP;

// Basic FFT round-trip
FFT fft;
fft.prepare(1024);

std::array<float, 1024> input = { /* audio samples */ };
SpectralBuffer spectrum;
spectrum.prepare(1024);

fft.forward(input.data(), spectrum.data());
// Modify spectrum...
fft.inverse(spectrum.data(), input.data());

// STFT for continuous spectral processing
STFT stft;
stft.prepare(1024, WindowType::Hann, 0.5f);  // 50% overlap

OverlapAdd ola;
ola.prepare(1024, WindowType::Hann, 0.5f);

// In processBlock()
for (size_t i = 0; i < numSamples; ++i) {
    if (stft.analyze(input[i], spectrum)) {
        // Process spectrum...
        spectrum.setMagnitude(100, spectrum.getMagnitude(100) * 0.5f);  // Attenuate bin 100

        if (ola.synthesize(spectrum, output[i])) {
            // Output sample ready
        }
    }
}
```

---

## [0.0.7] - 2025-12-23

### Added

- **Layer 1 DSP Primitive: Oversampler** (`src/dsp/primitives/oversampler.h`)
  - Upsampling/downsampling primitive for anti-aliased nonlinear processing
  - Supports 2x and 4x oversampling factors
  - Three quality presets:
    - `Economy` - IIR 8-pole Butterworth, ~48dB stopband, zero latency
    - `Standard` - FIR 31-tap halfband, ~80dB stopband, 15 samples latency
    - `High` - FIR 63-tap halfband, ~100dB stopband, 31 samples latency
  - Two processing modes:
    - `ZeroLatency` - IIR filters (minimum-phase, no latency)
    - `LinearPhase` - FIR filters (symmetric, linear phase)
  - Kaiser-windowed sinc FIR filter design for optimal stopband rejection
  - Pre-computed halfband coefficients (h[n]=0 for even n)
  - Polyphase FIR implementation for efficiency
  - Mono and stereo templates: `Oversampler<Factor, NumChannels>`
  - `processBlock()` for block-based processing with callback
  - `reset()` clears filter state without reallocation
  - Real-time safe: `noexcept`, no allocations in `process()`

- **Type aliases for common configurations**:
  - `Oversampler2xMono`, `Oversampler2xStereo`
  - `Oversampler4xMono`, `Oversampler4xStereo`

- **Comprehensive oversampler test suite** (1,068 assertions across 24 test cases)
  - All 5 user stories covered (US1-US5)
  - Stopband rejection verification (≥48dB economy, ≥80dB standard, ≥96dB high)
  - Passband flatness (<0.1dB ripple up to 20kHz)
  - Latency verification (0 for IIR, 15/31 samples for FIR)
  - Real-time safety verification (noexcept static_assert)
  - Multi-channel independence tests

### Changed

- **LFO: Click-free waveform transitions** (SC-008)
  - Smooth crossfade when changing waveforms during playback
  - Captures current output value at transition point
  - Handles mid-crossfade waveform changes correctly
  - `hasProcessed_` flag distinguishes setup vs runtime changes
  - Zero-crossing detection for waveforms that start at zero

- **Constitution v1.7.0**: Added Principle XV (Honest Completion)
  - Mandatory Implementation Verification sections in specs
  - Explicit compliance status tables for all requirements
  - Completion checklists with honest assessment guidelines
  - No softening of assessments or quiet scope reductions

- **Task templates**: Added cross-platform compatibility checks
  - IEEE 754 verification step after each user story
  - `-fno-fast-math` requirement for test files using `std::isnan`/`std::isfinite`/`std::isinf`
  - Floating-point precision guidelines (Approx().margin())

- **Tests CMakeLists.txt**: Extended `-fno-fast-math` to additional test files
  - `delay_line_test.cpp`, `lfo_test.cpp`, `biquad_test.cpp` added
  - Ensures IEEE 754 compliance on macOS/Linux (VST3 SDK enables `-ffast-math`)

### Fixed

- **Spec 001-db-conversion**: Added Implementation Verification section, SC-002 accuracy test
- **Spec 002-delay-line**: Added Implementation Verification section, SC-002/SC-003/SC-007 explicit tests
- **Spec 003-lfo**: Added Implementation Verification section, SC-008 click-free transitions
- **Spec 004-biquad-filter**: Added Implementation Verification section
- **Spec 005-parameter-smoother**: Added Implementation Verification section

### Technical Details

- **Oversampling formulas**:
  - FIR halfband: `y[n] = Σ h[k] * x[n-k]` for symmetric kernel
  - Kaiser window: `w[n] = I0(β × sqrt(1 - (n/M)²)) / I0(β)`
  - β calculation: `β = 0.1102 × (A - 8.7)` where A = stopband dB
- **LFO crossfade**: Linear interpolation over ~10ms (configurable)
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II, III, IX, X, XII, XV

### Usage

```cpp
#include "dsp/primitives/oversampler.h"

using namespace Iterum::DSP;

// 2x oversampling for saturation
Oversampler2xMono oversampler;
oversampler.prepare(44100.0);
oversampler.setQuality(OversamplingQuality::Standard);

// In processBlock() - process with callback
oversampler.processBlock(buffer, numSamples, [](float sample) {
    return std::tanh(sample * 2.0f);  // Saturation at oversampled rate
});

// 4x stereo oversampling
Oversampler4xStereo stereoOS;
stereoOS.prepare(48000.0);
stereoOS.setMode(OversamplingMode::LinearPhase);  // Best quality

// Get latency for delay compensation
size_t latency = stereoOS.getLatency();  // Report to host
```

---

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
