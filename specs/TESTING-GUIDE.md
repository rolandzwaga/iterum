# Testing Guide for VST Plugin Development

This document provides comprehensive guidelines for writing effective tests for this VST3 plugin project. It covers best practices, anti-patterns to avoid, DSP-specific testing strategies, and VST3 validation tools.

---

## Quick Start: Running Tests

### Build and Run All Tests

```bash
# Configure (first time or after CMakeLists.txt changes)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build everything (including tests and validator)
cmake --build build --config Debug

# Run unit tests (72 assertions across 12 test cases)
build\bin\Debug\dsp_tests.exe

# Run approval tests (golden master / regression tests)
build\bin\Debug\approval_tests.exe

# Run all tests via CTest
ctest --test-dir build -C Debug --output-on-failure
```

### VST3 Validation

```bash
# Full validation (all tests) - via CMake target
cmake --build build --config Debug --target validate_plugin

# Quick validation (basic conformity only)
cmake --build build --config Debug --target validate_plugin_quick

# Or run validator directly
build\bin\Debug\validator.exe "build\VST3\Debug\Iterum.vst3"

# Show all validator options
build\bin\Debug\validator.exe --help

# Run extensive tests (may take a long time)
build\bin\Debug\validator.exe -e "build\VST3\Debug\Iterum.vst3"
```

### Run Specific Tests

```bash
# Run only tests tagged [filter]
build\bin\Debug\dsp_tests.exe "[filter]"

# Run only regression tests
build\bin\Debug\approval_tests.exe "[regression]"

# Run only gain-related tests
build\bin\Debug\dsp_tests.exe "[gain]"

# Exclude slow tests
build\bin\Debug\dsp_tests.exe "~[slow]"

# List all available tests
build\bin\Debug\dsp_tests.exe --list-tests
```

### Test Helpers Location

- **Signal generators**: `tests/test_helpers/test_signals.h`
- **Buffer comparison**: `tests/test_helpers/buffer_comparison.h`
- **Allocation detector**: `tests/test_helpers/allocation_detector.h`

---

## Table of Contents

1. [Core Testing Philosophy](#core-testing-philosophy)
2. [Test Categories](#test-categories)
3. [Testing Best Practices](#testing-best-practices)
4. [Anti-Patterns to Avoid](#anti-patterns-to-avoid)
5. [DSP Testing Strategies](#dsp-testing-strategies)
   - [Test Signal Types](#test-signal-types)
   - [Frequency Domain Testing](#frequency-domain-testing)
   - [THD Measurement](#thd-total-harmonic-distortion-measurement)
   - [Frequency Estimation](#frequency-estimation-for-pitch-accuracy-tests)
   - [Guard Rail Tests](#guard-rail-tests)
   - [Latency and Delay Testing](#latency-and-delay-testing)
   - [Real-Time Safety Testing](#real-time-safety-testing)
6. [VST3-Specific Testing](#vst3-specific-testing)
7. [Test Organization](#test-organization)
8. [Catch2 Patterns](#catch2-patterns)
9. [Floating-Point Testing](#floating-point-testing)
10. [Test Doubles](#test-doubles)
11. [Approval Testing](#approval-testing)
12. [Continuous Integration](#continuous-integration)

---

## Core Testing Philosophy

### Test Behavior, Not Implementation

The most critical principle: **tests should verify what the code does, not how it does it**.

```cpp
// BAD: Tests implementation details
TEST_CASE("DelayLine uses circular buffer internally", "[bad]") {
    DelayLine delay(1000);
    // Testing that writeIndex_ wraps around - implementation detail!
    REQUIRE(delay.getWriteIndex() == 0);
    delay.write(1.0f);
    REQUIRE(delay.getWriteIndex() == 1);  // Fragile!
}

// GOOD: Tests behavior
TEST_CASE("DelayLine returns samples at specified delay", "[delay]") {
    DelayLine delay(1000);
    delay.write(0.5f);

    // Advance by 10 samples
    for (int i = 0; i < 10; ++i) {
        delay.write(0.0f);
    }

    // Sample written 10 samples ago should be at delay offset 10
    REQUIRE(delay.read(10) == Approx(0.5f));
}
```

**Why this matters:**
- Implementation details change during refactoring
- Behavior-focused tests survive code changes that don't alter functionality
- Tests serve as documentation of expected behavior

### Benefits of Behavioral Testing

| Aspect | Implementation Tests | Behavioral Tests |
|--------|---------------------|------------------|
| **Refactoring** | Break frequently | Survive changes |
| **Readability** | Obscure intent | Document behavior |
| **Maintenance** | High burden | Low burden |
| **Confidence** | False positives | True coverage |

---

## Test Categories

### Unit Tests

Test individual DSP functions and primitives in isolation.

```cpp
// Unit test: Single function, known inputs/outputs
TEST_CASE("dBToLinear converts correctly", "[dsp][unit]") {
    REQUIRE(dBToLinear(0.0f) == Approx(1.0f));
    REQUIRE(dBToLinear(-6.0206f) == Approx(0.5f).margin(0.001f));
}
```

**Scope:** Layer 0 (Core Utilities) and Layer 1 (DSP Primitives)

### Component Tests

Test composed DSP processors with their dependencies.

```cpp
// Component test: Processor using multiple primitives
TEST_CASE("Saturator applies oversampled distortion", "[dsp][component]") {
    Saturator sat;
    sat.prepare(44100.0, 512);
    sat.setDrive(2.0f);

    std::array<float, 512> buffer;
    generateSineWave(buffer.data(), 512, 1000.0f, 44100.0f);

    sat.process(buffer.data(), 512);

    // Verify saturation occurred (output bounded, harmonics added)
    REQUIRE(findPeak(buffer.data(), 512) <= 1.0f);
}
```

**Scope:** Layer 2 (DSP Processors) and Layer 3 (System Components)

### Integration Tests

Test complete signal paths through the plugin.

```cpp
// Integration test: Full audio path
TEST_CASE("Plugin processes stereo audio correctly", "[integration]") {
    TestProcessor processor;
    processor.initialize(44100.0, 512);
    processor.setParameter(kGainId, 0.5f);

    StereoBuffer input = generateTestSignal();
    StereoBuffer output = processor.process(input);

    REQUIRE(output.left.rms() == Approx(input.left.rms() * 0.5f).margin(0.01f));
}
```

**Scope:** Full plugin, VST3 host interaction

### Regression Tests

Capture known-good outputs for comparison.

```cpp
// Regression test: Compare against golden master
TEST_CASE("TapeMode output matches reference", "[regression]") {
    TapeMode tape;
    tape.prepare(44100.0, 512);

    auto output = tape.process(getReferenceInput());
    auto reference = loadGoldenMaster("tape_mode_reference.wav");

    REQUIRE(compareBuffers(output, reference, 1e-5f));
}
```

---

## Testing Best Practices

### 1. Use the Arrange-Act-Assert Pattern

```cpp
TEST_CASE("Filter attenuates above cutoff", "[dsp][filter]") {
    // ARRANGE: Set up test conditions
    BiquadFilter filter;
    filter.setLowpass(1000.0f, 44100.0f, 0.707f);
    std::array<float, 1024> buffer;
    generateSineWave(buffer.data(), 1024, 5000.0f, 44100.0f);  // Above cutoff
    float inputPeak = findPeak(buffer.data(), 1024);

    // ACT: Perform the operation
    filter.process(buffer.data(), 1024);

    // ASSERT: Verify expected outcome
    float outputPeak = findPeak(buffer.data(), 1024);
    REQUIRE(outputPeak < inputPeak * 0.5f);  // At least -6dB attenuation
}
```

### 2. One Assertion Per Concept

Each test should verify one logical concept, though multiple `REQUIRE` statements may be needed.

```cpp
// GOOD: Single concept (symmetry) with multiple checks
TEST_CASE("softClip is symmetric", "[dsp]") {
    REQUIRE(softClip(0.5f) == Approx(-softClip(-0.5f)));
    REQUIRE(softClip(1.0f) == Approx(-softClip(-1.0f)));
    REQUIRE(softClip(2.0f) == Approx(-softClip(-2.0f)));
}

// BAD: Multiple unrelated concepts
TEST_CASE("softClip works", "[dsp]") {
    REQUIRE(softClip(0.0f) == 0.0f);           // Concept 1: zero handling
    REQUIRE(softClip(0.5f) == -softClip(-0.5f)); // Concept 2: symmetry
    REQUIRE(softClip(10.0f) < 1.1f);            // Concept 3: saturation
}
```

### 3. Use Descriptive Test Names

Test names should read like specifications.

```cpp
// GOOD: Describes behavior and context
TEST_CASE("OnePoleSmoother reaches target within 5 time constants", "[dsp][smoother]")
TEST_CASE("DelayLine handles zero-length buffer without crash", "[dsp][delay][edge]")
TEST_CASE("Feedback network soft-clips when gain exceeds unity", "[dsp][feedback]")

// BAD: Vague or implementation-focused
TEST_CASE("testSmoother", "[dsp]")
TEST_CASE("coefficients are calculated", "[dsp]")
TEST_CASE("test1", "[dsp]")
```

### 4. Test Edge Cases and Boundaries

```cpp
TEST_CASE("DelayLine handles edge cases", "[dsp][delay][edge]") {
    DelayLine delay(100);

    SECTION("zero delay returns current sample") {
        delay.write(0.5f);
        REQUIRE(delay.read(0) == Approx(0.5f));
    }

    SECTION("maximum delay returns oldest sample") {
        for (int i = 0; i < 100; ++i) {
            delay.write(static_cast<float>(i));
        }
        REQUIRE(delay.read(99) == Approx(0.0f));
    }

    SECTION("fractional delay interpolates correctly") {
        delay.write(0.0f);
        delay.write(1.0f);
        REQUIRE(delay.read(0.5f) == Approx(0.5f));
    }
}
```

### 5. Test Failure Modes

```cpp
TEST_CASE("calculateRMS handles invalid input gracefully", "[dsp][analysis]") {
    SECTION("null pointer returns zero") {
        REQUIRE(calculateRMS(nullptr, 100) == 0.0f);
    }

    SECTION("zero length returns zero") {
        float buffer[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        REQUIRE(calculateRMS(buffer, 0) == 0.0f);
    }
}
```

---

## Anti-Patterns to Avoid

### 1. The Mockery

**Problem:** Test uses so many mocks that it tests mock behavior, not real code.

```cpp
// BAD: Testing mocks, not the actual implementation
TEST_CASE("Processor processes audio", "[bad]") {
    MockParameterHandler mockParams;
    MockAudioBuffer mockInput;
    MockAudioBuffer mockOutput;
    MockDSPEngine mockDSP;

    EXPECT_CALL(mockParams, getGain()).WillReturn(0.5f);
    EXPECT_CALL(mockDSP, process(_, _)).WillReturn(true);

    Processor proc(mockParams, mockDSP);
    proc.process(mockInput, mockOutput);

    // What did we actually test? Just that mocks were called.
}

// GOOD: Test real DSP with real data
TEST_CASE("Processor applies gain correctly", "[processor]") {
    Processor proc;
    proc.prepare(44100.0, 512);
    proc.setParameter(kGainId, 0.5f);  // Normalized: 0.5 = -6dB

    std::array<float, 512> buffer;
    std::fill(buffer.begin(), buffer.end(), 1.0f);

    proc.processBlock(buffer.data(), 512);

    REQUIRE(buffer[0] == Approx(0.5f));
}
```

**Alternative to mocking:** Use fakes (real implementations with shortcuts) or test DSP algorithms in isolation.

### 2. The Inspector

**Problem:** Test violates encapsulation to achieve coverage.

```cpp
// BAD: Accessing private members, testing implementation
TEST_CASE("LFO internal phase increments", "[bad]") {
    LFO lfo;
    lfo.setFrequency(1.0f, 44100.0f);

    // Accessing private member - fragile!
    REQUIRE(lfo.phase_ == 0.0f);
    lfo.process();
    REQUIRE(lfo.phase_ == Approx(1.0f / 44100.0f));
}

// GOOD: Test observable behavior
TEST_CASE("LFO completes one cycle at specified frequency", "[dsp][lfo]") {
    LFO lfo;
    lfo.setFrequency(1.0f, 100.0f);  // 1 Hz at 100 samples/sec = 100 samples/cycle

    float startValue = lfo.process();

    // Process one full cycle
    for (int i = 1; i < 100; ++i) {
        lfo.process();
    }

    // Should return to approximately the same value
    REQUIRE(lfo.process() == Approx(startValue).margin(0.01f));
}
```

### 3. The Happy Path Only

**Problem:** Tests only the successful case, missing error conditions.

```cpp
// BAD: Only tests success
TEST_CASE("loadPreset loads preset", "[preset]") {
    PresetManager pm;
    pm.loadPreset("valid_preset.json");
    REQUIRE(pm.isLoaded());
}

// GOOD: Tests error handling too
TEST_CASE("loadPreset handles missing files", "[preset]") {
    PresetManager pm;

    SECTION("returns false for missing file") {
        REQUIRE_FALSE(pm.loadPreset("nonexistent.json"));
    }

    SECTION("returns false for corrupted file") {
        REQUIRE_FALSE(pm.loadPreset("corrupted.json"));
    }

    SECTION("previous state preserved on failure") {
        pm.loadPreset("valid_preset.json");
        float originalValue = pm.getParameter("delay_time");

        pm.loadPreset("nonexistent.json");
        REQUIRE(pm.getParameter("delay_time") == originalValue);
    }
}
```

### 4. The Flaky Test

**Problem:** Test fails intermittently due to timing, randomness, or external dependencies.

```cpp
// BAD: Depends on timing
TEST_CASE("async operation completes", "[bad]") {
    AsyncLoader loader;
    loader.startLoading();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Race condition!

    REQUIRE(loader.isComplete());
}

// GOOD: Use deterministic synchronization or test synchronously
TEST_CASE("loader processes all items", "[loader]") {
    SyncLoader loader;  // Synchronous test double
    loader.loadAll(testItems);

    REQUIRE(loader.processedCount() == testItems.size());
}
```

### 5. The Coverage Chaser

**Problem:** Writing tests just to hit coverage numbers, not to verify behavior.

```cpp
// BAD: Meaningless test that inflates coverage
TEST_CASE("constructor constructs", "[bad]") {
    Filter f;
    REQUIRE(true);  // Pointless
}

// BAD: Testing getters/setters with no logic
TEST_CASE("setGain sets gain", "[bad]") {
    Processor p;
    p.setGain(0.5f);
    REQUIRE(p.getGain() == 0.5f);  // Tests nothing meaningful
}

// GOOD: Test meaningful behavior
TEST_CASE("Gain parameter is smoothed to prevent clicks", "[processor]") {
    Processor p;
    p.prepare(44100.0, 64);
    p.setGain(0.0f);
    p.processBlock(silence, 64);  // Let smoother settle

    p.setGain(1.0f);
    std::array<float, 64> output;
    std::fill(output.begin(), output.end(), 1.0f);
    p.processBlock(output.data(), 64);

    // First sample should NOT be at full gain (smoothing)
    REQUIRE(output[0] < 0.5f);
    // Last sample should be closer to target
    REQUIRE(output[63] > output[0]);
}
```

### 6. The Copy-Paste Test

**Problem:** Duplicated test code that becomes inconsistent.

```cpp
// BAD: Copy-paste with slight modifications
TEST_CASE("lowpass at 1kHz", "[filter]") {
    Filter f;
    f.setLowpass(1000.0f, 44100.0f);
    // ... 20 lines of test code ...
}

TEST_CASE("lowpass at 2kHz", "[filter]") {
    Filter f;
    f.setLowpass(2000.0f, 44100.0f);  // Only this changed
    // ... same 20 lines of test code ...
}

// GOOD: Parameterized test or shared setup
TEST_CASE("Lowpass attenuates above cutoff", "[filter]") {
    const std::array<float, 3> cutoffs = {500.0f, 1000.0f, 2000.0f};

    for (float cutoff : cutoffs) {
        DYNAMIC_SECTION("cutoff: " << cutoff << " Hz") {
            Filter f;
            f.setLowpass(cutoff, 44100.0f);

            auto response = measureFrequencyResponse(f, cutoff * 2);
            REQUIRE(response < -6.0f);  // At least -6dB at 2x cutoff
        }
    }
}
```

### 7. The Accumulator

**Problem:** Test accumulates many floating-point operations and expects exact results, when it should test single-operation precision.

```cpp
// BAD: Accumulating 48000 float additions, then expecting exact result
TEST_CASE("LFO completes one cycle", "[bad]") {
    constexpr float sampleRate = 48000.0f;
    constexpr float kTwoPi = 6.28318530718f;
    const float phaseIncrement = kTwoPi / sampleRate;

    float phase = 0.0f;
    for (int i = 0; i < static_cast<int>(sampleRate); ++i) {
        phase += phaseIncrement;  // 48000 additions!
    }

    // FAILS! Error accumulates to ~0.004 after 48000 additions
    REQUIRE(phase == Approx(kTwoPi).margin(1e-4f));  // BAD: widening tolerance hides the real issue
}

// GOOD: Test single-operation precision instead
TEST_CASE("LFO phase increment is precise", "[dsp][lfo]") {
    constexpr float lfoFreq = 1.0f;
    constexpr float sampleRate = 48000.0f;
    const float phaseIncrement = kTwoPi * lfoFreq / sampleRate;

    // Single operation - tests constant precision, not IEEE 754 accumulation
    REQUIRE(phaseIncrement * sampleRate == Approx(kTwoPi).margin(1e-5f));

    // Verify exact increment value
    REQUIRE(phaseIncrement == Approx(0.00013089969f).margin(1e-9f));

    // Verify sin/cos at calculated positions (single multiplication each)
    REQUIRE(std::sin(phaseIncrement * 12000) == Approx(1.0f).margin(1e-5f));  // sin(π/2)
    REQUIRE(std::cos(phaseIncrement * 24000) == Approx(-1.0f).margin(1e-5f)); // cos(π)
}
```

**Why it's wrong:**
- Accumulating N float additions tests IEEE 754 limits, not your constant's precision
- Error grows as O(√N) for random-direction errors, O(N) for systematic bias
- 48000 additions with ~1e-7 relative error per operation ≈ 0.004 total error

**When you actually need to test accumulation:**
Real DSP code should use phase wrapping to prevent unbounded error growth:

```cpp
// GOOD: Test that phase wrapping prevents accumulation error
TEST_CASE("LFO phase wrapping prevents error accumulation", "[dsp][lfo]") {
    LFO lfo;
    lfo.setFrequency(1.0f, 48000.0f);  // 1 Hz at 48kHz

    // Process many cycles (simulates long runtime)
    for (int cycle = 0; cycle < 1000; ++cycle) {
        for (int i = 0; i < 48000; ++i) {
            lfo.process();
        }
    }

    // After 1000 cycles, LFO should still produce valid output [-1, 1]
    float output = lfo.process();
    REQUIRE(output >= -1.0f);
    REQUIRE(output <= 1.0f);

    // Phase should still be bounded (not grown to millions)
    // This is testing the implementation's error management, not the constant
}
```

**Key insight:** Distinguish between testing *constant precision* (use single operations) and testing *implementation robustness* (verify real code handles accumulation via wrapping).

---

## DSP Testing Strategies

### Test Signal Types

Use standard test signals to verify DSP behavior:

| Signal | Use Case | How to Generate |
|--------|----------|-----------------|
| **Impulse** | Measure impulse response, latency | Single 1.0f, rest zeros |
| **Step** | DC response, settling time | Zeros then ones |
| **Sine Wave** | Frequency response, THD | `sin(2πft/sr)` |
| **White Noise** | Full-spectrum response | Random values [-1, 1] |
| **Sweep** | Time-varying frequency response | Chirp from 20Hz to 20kHz |
| **DC** | DC offset handling | Constant non-zero value |

```cpp
// Test signal generators
inline void generateImpulse(float* buffer, size_t size) {
    std::fill(buffer, buffer + size, 0.0f);
    buffer[0] = 1.0f;
}

inline void generateSine(float* buffer, size_t size,
                         float frequency, float sampleRate) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = std::sin(2.0f * M_PI * frequency * i / sampleRate);
    }
}

inline void generateWhiteNoise(float* buffer, size_t size) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (size_t i = 0; i < size; ++i) {
        buffer[i] = dist(gen);
    }
}
```

### Frequency Domain Testing

Use FFT analysis for filters and frequency-dependent effects:

```cpp
TEST_CASE("Lowpass filter frequency response", "[dsp][filter][fft]") {
    BiquadFilter filter;
    filter.setLowpass(1000.0f, 44100.0f, 0.707f);

    // Generate and process impulse
    std::array<float, 4096> impulse{};
    impulse[0] = 1.0f;
    filter.process(impulse.data(), impulse.size());

    // Compute FFT
    auto spectrum = computeFFT(impulse.data(), impulse.size());

    // Check passband (below cutoff - should be ~0dB)
    float passbandGain = magnitudeToDb(spectrum[frequencyToBin(500.0f, 44100.0f, 4096)]);
    REQUIRE(passbandGain == Approx(0.0f).margin(1.0f));

    // Check stopband (above cutoff - should be attenuated)
    float stopbandGain = magnitudeToDb(spectrum[frequencyToBin(4000.0f, 44100.0f, 4096)]);
    REQUIRE(stopbandGain < -20.0f);
}
```

### THD (Total Harmonic Distortion) Measurement

Use FFT-based harmonic analysis to measure distortion from saturation and nonlinear processing. This is essential for testing saturation processors, tape emulations, and other nonlinear effects.

**Why FFT-based THD?** Simple time-domain comparison (subtracting a reference sine) incorrectly measures phase shift and filter delay as distortion. FFT analysis properly isolates harmonic content.

```cpp
#include "dsp/primitives/fft.h"

// Measure THD using FFT-based harmonic analysis
// THD = sqrt(sum of harmonic powers) / fundamental power * 100%
float measureTHDWithFFT(const float* buffer, size_t size,
                         float fundamentalFreq, float sampleRate) {
    // FFT size must be power of 2
    size_t fftSize = 1;
    while (fftSize < size && fftSize < kMaxFFTSize) {
        fftSize <<= 1;
    }
    if (fftSize > size) fftSize >>= 1;
    if (fftSize < kMinFFTSize) fftSize = kMinFFTSize;

    // Apply Hann window to reduce spectral leakage
    std::vector<float> windowed(fftSize);
    constexpr float kTwoPi = 6.28318530718f;
    for (size_t i = 0; i < fftSize; ++i) {
        float window = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i) /
                                                static_cast<float>(fftSize - 1)));
        windowed[i] = buffer[i] * window;
    }

    // Perform FFT
    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> spectrum(fftSize / 2 + 1);
    fft.forward(windowed.data(), spectrum.data());

    // Find the bin corresponding to the fundamental frequency
    float binWidth = sampleRate / static_cast<float>(fftSize);
    size_t fundamentalBin = static_cast<size_t>(std::round(fundamentalFreq / binWidth));

    // Get fundamental magnitude (search nearby bins for peak)
    float fundamentalMag = 0.0f;
    size_t searchRange = 2;
    for (size_t i = fundamentalBin > searchRange ? fundamentalBin - searchRange : 0;
         i <= fundamentalBin + searchRange && i < spectrum.size(); ++i) {
        float mag = spectrum[i].magnitude();
        if (mag > fundamentalMag) {
            fundamentalMag = mag;
            fundamentalBin = i;
        }
    }

    if (fundamentalMag < 1e-10f) return 0.0f;  // No fundamental detected

    // Sum harmonic magnitudes (2nd through 10th harmonics)
    float harmonicPowerSum = 0.0f;
    for (int harmonic = 2; harmonic <= 10; ++harmonic) {
        size_t harmonicBin = fundamentalBin * harmonic;
        if (harmonicBin >= spectrum.size()) break;

        // Search nearby bins for the harmonic peak
        float harmonicMag = 0.0f;
        for (size_t i = harmonicBin > searchRange ? harmonicBin - searchRange : 0;
             i <= harmonicBin + searchRange && i < spectrum.size(); ++i) {
            float mag = spectrum[i].magnitude();
            if (mag > harmonicMag) harmonicMag = mag;
        }
        harmonicPowerSum += harmonicMag * harmonicMag;
    }

    // THD = sqrt(sum of harmonic powers) / fundamental power * 100%
    return std::sqrt(harmonicPowerSum) / fundamentalMag * 100.0f;
}
```

**Usage example:**

```cpp
TEST_CASE("Saturation THD is controllable", "[dsp][saturation][SC-005]") {
    SaturationProcessor sat;
    sat.prepare(44100.0, 512);

    std::array<float, 4096> buffer;

    auto measureTHDAtDrive = [&](float drive) {
        sat.setInputGain(drive);

        // Let processor settle
        for (int i = 0; i < 10; ++i) {
            generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.5f);
            sat.process(buffer.data(), buffer.size());
        }

        // Measure with fresh signal
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.5f);
        sat.process(buffer.data(), buffer.size());

        return measureTHDWithFFT(buffer.data(), buffer.size(), 1000.0f, 44100.0f);
    };

    float thdLow = measureTHDAtDrive(-10.0f);
    float thdHigh = measureTHDAtDrive(10.0f);

    REQUIRE(thdLow < 0.5f);      // Low drive = low THD
    REQUIRE(thdHigh >= 3.0f);    // High drive = significant THD
    REQUIRE(thdHigh > thdLow);   // THD increases with drive
}
```

**Key considerations:**

| Factor | Recommendation |
|--------|----------------|
| **Window function** | Use Hann window to reduce spectral leakage |
| **FFT size** | Use 4096+ samples for good frequency resolution |
| **Test frequency** | Use 1kHz (well away from FFT bin boundaries at common sample rates) |
| **Test amplitude** | Use 0.5 (gives headroom for saturation without clipping) |
| **Settling time** | Process 5-10 blocks before measurement to let smoothers settle |
| **Harmonic count** | Sum harmonics 2-10 (covers most audible distortion) |

**Avoid the simple difference method:**

```cpp
// BAD: Phase shift and filter delay measured as "distortion"
float measureTHD_WRONG(const float* buffer, size_t size, float freq, float sr) {
    std::vector<float> reference(size);
    generateSine(reference.data(), size, freq, sr);

    float diff = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        diff += (buffer[i] - reference[i]) * (buffer[i] - reference[i]);
    }
    return std::sqrt(diff / size);  // WRONG: includes phase error!
}
```

### Frequency Estimation for Pitch Accuracy Tests

When testing pitch shifters or other frequency-altering DSP, you need to measure the output frequency accurately. Two methods are available:

**1. FFT-based Detection** - Good for pure tones, but fooled by amplitude modulation:

```cpp
// FFT-based frequency estimation with parabolic interpolation
// WARNING: Fooled by AM artifacts from crossfading pitch shifters!
float estimateFrequencyFFT(const float* buffer, size_t size, float sampleRate,
                            float expectedFreqMin = 50.0f, float expectedFreqMax = 2000.0f) {
    size_t fftSize = /* power of 2 <= size */;

    // Apply Hann window
    std::vector<float> windowed(fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        float window = 0.5f * (1.0f - std::cos(kTwoPi * i / (fftSize - 1)));
        windowed[i] = buffer[i] * window;
    }

    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> spectrum(fftSize / 2 + 1);
    fft.forward(windowed.data(), spectrum.data());

    // Find peak bin in expected range
    float binWidth = sampleRate / static_cast<float>(fftSize);
    size_t minBin = static_cast<size_t>(expectedFreqMin / binWidth);
    size_t maxBin = static_cast<size_t>(expectedFreqMax / binWidth);

    size_t peakBin = minBin;
    float peakMag = 0.0f;
    for (size_t i = minBin; i <= maxBin && i < spectrum.size(); ++i) {
        float mag = spectrum[i].magnitude();
        if (mag > peakMag) { peakMag = mag; peakBin = i; }
    }

    // Parabolic interpolation for sub-bin accuracy
    if (peakBin > 0 && peakBin < spectrum.size() - 1) {
        float y0 = spectrum[peakBin - 1].magnitude();
        float y1 = spectrum[peakBin].magnitude();
        float y2 = spectrum[peakBin + 1].magnitude();
        float delta = 0.5f * (y2 - y0) / (2.0f * y1 - y0 - y2);
        return (peakBin + delta) * binWidth;
    }
    return peakBin * binWidth;
}
```

**2. Autocorrelation-based Detection** - Robust against AM artifacts:

```cpp
// Autocorrelation-based frequency estimation
// More robust for pitch shifters with crossfading/windowing artifacts
float estimateFrequencyAutocorr(const float* buffer, size_t size, float sampleRate,
                                 float minFreq = 50.0f, float maxFreq = 2000.0f) {
    size_t minLag = static_cast<size_t>(sampleRate / maxFreq);
    size_t maxLag = static_cast<size_t>(sampleRate / minFreq);
    maxLag = std::min(maxLag, size / 2);

    float bestCorr = -1.0f;
    size_t bestLag = minLag;

    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        float corr = 0.0f;
        for (size_t i = 0; i < size - lag; ++i) {
            corr += buffer[i] * buffer[i + lag];
        }
        if (corr > bestCorr) {
            bestCorr = corr;
            bestLag = lag;
        }
    }

    return sampleRate / static_cast<float>(bestLag);
}
```

**When to use which method:**

| Scenario | Recommended Method | Why |
|----------|-------------------|-----|
| Pure sine waves | FFT | Sub-bin accuracy via parabolic interpolation |
| Pitch shifters | Autocorrelation | Immune to AM sidebands from crossfading |
| Filters | FFT | Need frequency response at specific bins |
| Oscillators | Either | Both work well for clean sources |

**Lesson learned:** During spec 016 (Pitch Shifter) audit, FFT detection showed 894Hz for an 880Hz target (1.6% error). Autocorrelation correctly measured 882Hz (0.2% error). The difference was caused by crossfade amplitude modulation creating sidebands that shifted the FFT peak.

### Guard Rail Tests

Ensure DSP code doesn't produce invalid output:

```cpp
TEST_CASE("DSP output contains no NaN or Inf", "[dsp][safety]") {
    Saturator sat;
    sat.prepare(44100.0, 512);
    sat.setDrive(10.0f);  // Extreme setting

    std::array<float, 512> buffer;

    SECTION("normal input") {
        generateSine(buffer.data(), 512, 440.0f, 44100.0f);
        sat.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
            REQUIRE_FALSE(std::isinf(sample));
        }
    }

    SECTION("extreme input") {
        std::fill(buffer.begin(), buffer.end(), 1000.0f);  // Way over 0dBFS
        sat.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
            REQUIRE_FALSE(std::isinf(sample));
            REQUIRE(std::abs(sample) <= 2.0f);  // Bounded output
        }
    }

    SECTION("denormal input") {
        std::fill(buffer.begin(), buffer.end(), 1e-38f);  // Denormal
        sat.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
        }
    }
}
```

### Latency and Delay Testing

```cpp
TEST_CASE("DelayLine has correct latency", "[dsp][delay]") {
    DelayLine delay(44100);  // 1 second max
    delay.setDelay(100);     // 100 samples

    // Write impulse
    delay.write(1.0f);
    for (int i = 0; i < 99; ++i) {
        delay.write(0.0f);
        REQUIRE(delay.read(100) == 0.0f);  // Not yet arrived
    }

    delay.write(0.0f);  // 100th sample
    REQUIRE(delay.read(100) == 1.0f);  // Impulse arrived
}
```

### Real-Time Safety Testing

While we can't truly test real-time constraints in unit tests, we can verify allocation-free operation:

```cpp
TEST_CASE("Process method is allocation-free", "[dsp][realtime]") {
    Processor proc;
    proc.prepare(44100.0, 512);  // Allocations allowed here

    std::array<float, 512> buffer{};

    // Override global new to detect allocations
    AllocationDetector detector;
    detector.startTracking();

    proc.processBlock(buffer.data(), 512);

    REQUIRE(detector.getAllocationCount() == 0);
}
```

---

## VST3-Specific Testing

### Steinberg vstvalidator

The VST3 SDK includes a command-line validator tool that checks plugin conformity.

**Location:** `extern/vst3sdk/public.sdk/samples/vst-hosting/validator/`

**Usage:**
```bash
vstvalidator path/to/plugin.vst3
```

**What it tests:**
- Component initialization/termination
- State save/restore
- Parameter handling
- Bus configuration
- Audio processing
- Threading requirements

### VST3 Plugin Test Host

The SDK includes a graphical test host with built-in test suites.

**Location:** `extern/vst3sdk/public.sdk/samples/vst-hosting/`

**Features:**
- Load and test plugins interactively
- Run automated test suites
- View detailed test results
- Debug plugin behavior

### Writing Custom VST3 Tests

```cpp
// Test that state roundtrips correctly
TEST_CASE("Plugin state saves and restores", "[vst3][state]") {
    // Create processor
    auto processor = createProcessor();
    processor->initialize(nullptr);

    // Set some parameters
    processor->setParameter(kGainId, 0.75f);
    processor->setParameter(kDelayTimeId, 0.5f);

    // Save state
    MemoryStream stream;
    processor->getState(&stream);

    // Create new processor and restore
    auto processor2 = createProcessor();
    processor2->initialize(nullptr);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    processor2->setState(&stream);

    // Verify parameters match
    REQUIRE(processor2->getParameter(kGainId) == Approx(0.75f));
    REQUIRE(processor2->getParameter(kDelayTimeId) == Approx(0.5f));
}

// Test bypass behavior
TEST_CASE("Bypass passes audio unchanged", "[vst3][bypass]") {
    auto processor = createProcessor();
    processor->initialize(nullptr);
    processor->setParameter(kBypassId, 1.0f);  // Enable bypass

    std::array<float, 512> input, output;
    generateSine(input.data(), 512, 440.0f, 44100.0f);
    std::copy(input.begin(), input.end(), output.begin());

    processor->process(output.data(), 512);

    for (size_t i = 0; i < 512; ++i) {
        REQUIRE(output[i] == Approx(input[i]));
    }
}
```

### Testing Processor/Controller Separation

```cpp
TEST_CASE("Processor works without controller", "[vst3][separation]") {
    // Processor should function independently
    auto processor = createProcessor();
    processor->initialize(nullptr);
    processor->setupProcessing(ProcessSetup{44100.0, 512});
    processor->setActive(true);

    std::array<float, 512> buffer{};
    generateSine(buffer.data(), 512, 440.0f, 44100.0f);

    // Should process without crashing
    REQUIRE_NOTHROW(processor->process(buffer.data(), 512));
}

TEST_CASE("State flows correctly from Processor to Controller", "[vst3][state]") {
    auto processor = createProcessor();
    auto controller = createController();

    processor->initialize(nullptr);
    controller->initialize(nullptr);

    // Set parameter on processor
    processor->setParameter(kGainId, 0.25f);

    // Get processor state
    MemoryStream stream;
    processor->getState(&stream);

    // Controller should update from processor state
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    controller->setComponentState(&stream);

    REQUIRE(controller->getParamNormalized(kGainId) == Approx(0.25f));
}
```

---

## Test Organization

### Directory Structure

```
tests/
├── CMakeLists.txt
├── test_helpers/              # Shared test utilities
│   ├── test_signals.h         #   Signal generators
│   ├── buffer_comparison.h    #   Comparison utilities
│   └── allocation_detector.h  #   Real-time safety checking
├── unit/                      # Unit tests by layer
│   ├── core/                  #   Layer 0 tests
│   │   ├── test_fast_math.cpp
│   │   └── test_simd_ops.cpp
│   ├── primitives/            #   Layer 1 tests
│   │   ├── test_delay_line.cpp
│   │   ├── test_biquad.cpp
│   │   └── test_lfo.cpp
│   ├── processors/            #   Layer 2 tests
│   │   ├── test_filter.cpp
│   │   └── test_saturator.cpp
│   ├── systems/               #   Layer 3 tests
│   │   └── test_delay_engine.cpp
│   └── features/              #   Layer 4 tests
│       └── test_tape_mode.cpp
├── integration/               # Integration tests
│   ├── test_plugin_load.cpp
│   ├── test_state_persistence.cpp
│   └── test_audio_processing.cpp
└── regression/                # Regression tests with golden masters
    ├── golden_masters/
    │   ├── tape_mode_ref.wav
    │   └── shimmer_ref.wav
    └── test_regression.cpp
```

### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Test file | `test_<module>.cpp` | `test_delay_line.cpp` |
| Test case | Descriptive sentence | `"DelayLine interpolates fractional delays"` |
| Section | Specific scenario | `"linear interpolation"` |
| Tags | `[layer][category]` | `[dsp][primitives][delay]` |

### Test Tags

Use consistent tags for filtering:

```cpp
// Layer tags
[core]       // Layer 0
[primitives] // Layer 1
[processors] // Layer 2
[systems]    // Layer 3
[features]   // Layer 4

// Category tags
[dsp]        // DSP algorithm
[vst3]       // VST3 specific
[state]      // State save/restore
[edge]       // Edge case
[regression] // Regression test

// Speed tags
[fast]       // < 100ms
[slow]       // > 1s
```

Run specific tests:
```bash
# Run only primitive tests
ctest -R primitives

# Run fast DSP tests
./dsp_tests "[dsp][fast]"

# Exclude slow tests
./dsp_tests "~[slow]"
```

---

## Catch2 Patterns

### Sections for Shared Setup

```cpp
TEST_CASE("BiquadFilter modes", "[dsp][filter]") {
    BiquadFilter filter;
    std::array<float, 1024> buffer;

    // Shared setup
    generateWhiteNoise(buffer.data(), 1024);

    SECTION("lowpass") {
        filter.setLowpass(1000.0f, 44100.0f, 0.707f);
        filter.process(buffer.data(), 1024);

        auto spectrum = computeFFT(buffer.data(), 1024);
        REQUIRE(spectrum.highFrequencyEnergy() < spectrum.lowFrequencyEnergy());
    }

    SECTION("highpass") {
        filter.setHighpass(1000.0f, 44100.0f, 0.707f);
        filter.process(buffer.data(), 1024);

        auto spectrum = computeFFT(buffer.data(), 1024);
        REQUIRE(spectrum.highFrequencyEnergy() > spectrum.lowFrequencyEnergy());
    }
}
```

### Using Approx for Floating-Point

```cpp
// Default epsilon (scale-dependent)
REQUIRE(value == Approx(expected));

// Custom margin (absolute tolerance)
REQUIRE(value == Approx(expected).margin(0.001f));

// Custom epsilon (relative tolerance)
REQUIRE(value == Approx(expected).epsilon(0.01f));

// For near-zero values, use margin
REQUIRE(nearZeroValue == Approx(0.0f).margin(1e-6f));
```

### REQUIRE vs CHECK

```cpp
TEST_CASE("Processing chain", "[dsp]") {
    Processor proc;

    // REQUIRE: Stops test on failure (use for preconditions)
    REQUIRE(proc.initialize());

    // CHECK: Continues on failure (use for multiple independent assertions)
    std::array<float, 100> buffer{};
    proc.process(buffer.data(), 100);

    CHECK(buffer[0] == Approx(expected0));
    CHECK(buffer[50] == Approx(expected50));
    CHECK(buffer[99] == Approx(expected99));
}
```

### Matchers for Complex Assertions

```cpp
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Filter coefficients", "[dsp][filter]") {
    auto coeffs = calculateCoefficients(1000.0f, 44100.0f);

    REQUIRE_THAT(coeffs.a0, WithinAbs(1.0f, 0.001f));
    REQUIRE_THAT(coeffs.b1, WithinRel(expectedB1, 0.01f));
}
```

### Generators for Property-Based Testing

```cpp
TEST_CASE("dB/linear roundtrip", "[dsp][property]") {
    // Test with range of values
    auto linearValue = GENERATE(0.001f, 0.01f, 0.1f, 0.5f, 1.0f, 2.0f, 10.0f);

    float dB = linearToDb(linearValue);
    float recovered = dBToLinear(dB);

    REQUIRE(recovered == Approx(linearValue).epsilon(0.001f));
}
```

---

## Floating-Point Testing

### Comparison Strategies

| Situation | Strategy | Example |
|-----------|----------|---------|
| Known exact value | Direct comparison | `REQUIRE(x == 0.0f)` |
| Expected approximate | `Approx` with margin | `Approx(expected).margin(1e-5f)` |
| Relative precision | `Approx` with epsilon | `Approx(expected).epsilon(0.01f)` |
| Near zero | Margin only | `Approx(0.0f).margin(1e-7f)` |

### Handling Determinism

Floating-point results can vary across:
- Compiler optimization levels
- Different compilers
- Different CPU architectures
- Debug vs Release builds

```cpp
// Test with appropriate tolerance
TEST_CASE("Filter is stable", "[dsp][filter]") {
    // Use tolerance that accounts for platform differences
    constexpr float tolerance = 1e-5f;

    BiquadFilter filter;
    filter.setLowpass(1000.0f, 44100.0f, 0.707f);

    // Process same input
    std::array<float, 100> buffer;
    std::fill(buffer.begin(), buffer.end(), 1.0f);
    filter.process(buffer.data(), 100);

    // Compare against expected (with tolerance)
    REQUIRE(buffer[99] == Approx(expectedSteadyState).margin(tolerance));
}
```

### Testing for NaN/Inf

```cpp
inline bool isValidSample(float sample) {
    return std::isfinite(sample) && std::abs(sample) <= 100.0f;
}

TEST_CASE("Output is always valid", "[dsp][safety]") {
    Processor proc;
    proc.prepare(44100.0, 512);

    std::array<float, 512> buffer;

    // Test with various problematic inputs
    SECTION("denormals") {
        std::fill(buffer.begin(), buffer.end(), 1e-40f);
    }

    SECTION("very large") {
        std::fill(buffer.begin(), buffer.end(), 1e10f);
    }

    SECTION("NaN input") {
        buffer[0] = std::numeric_limits<float>::quiet_NaN();
    }

    proc.processBlock(buffer.data(), 512);

    for (size_t i = 0; i < 512; ++i) {
        REQUIRE(isValidSample(buffer[i]));
    }
}
```

---

## Test Doubles

### When to Use What

| Type | Use Case | DSP Example |
|------|----------|-------------|
| **Stub** | Return canned data | Fixed parameter values |
| **Fake** | Working simple implementation | In-memory preset storage |
| **Mock** | Verify interactions | Host callback verification |
| **Spy** | Record what happened | Track parameter changes |

### Prefer Fakes Over Mocks for DSP

```cpp
// GOOD: Fake audio buffer
class TestAudioBuffer {
public:
    TestAudioBuffer(size_t size) : data_(size) {}

    float* data() { return data_.data(); }
    size_t size() const { return data_.size(); }

    void fillSine(float freq, float sr) {
        for (size_t i = 0; i < data_.size(); ++i) {
            data_[i] = std::sin(2.0f * M_PI * freq * i / sr);
        }
    }

    float rms() const {
        float sum = 0.0f;
        for (float s : data_) sum += s * s;
        return std::sqrt(sum / data_.size());
    }

private:
    std::vector<float> data_;
};

// Use in test
TEST_CASE("Gain reduces level", "[dsp]") {
    TestAudioBuffer buffer(512);
    buffer.fillSine(440.0f, 44100.0f);
    float originalRms = buffer.rms();

    applyGain(buffer.data(), buffer.size(), 0.5f);

    REQUIRE(buffer.rms() == Approx(originalRms * 0.5f));
}
```

### Minimal Mocking

Only mock external dependencies that are impractical to use directly:

```cpp
// Mock only the host callback interface
class MockHostCallback : public IHostCallback {
public:
    void beginEdit(ParamID id) override { editedParams_.push_back(id); }
    void performEdit(ParamID id, float value) override {}
    void endEdit(ParamID id) override {}

    std::vector<ParamID> editedParams_;
};

TEST_CASE("Controller notifies host of edits", "[controller]") {
    MockHostCallback host;
    Controller ctrl(&host);

    ctrl.setParameterFromUI(kGainId, 0.5f);

    REQUIRE(host.editedParams_.size() == 1);
    REQUIRE(host.editedParams_[0] == kGainId);
}
```

---

## Approval Testing

Approval testing (golden master testing) compares current output against previously approved output.

### Use Cases for Audio

- Regression testing complex algorithms
- Verifying DSP refactoring doesn't change output
- Testing preset loading produces expected results

### Implementation with ApprovalTests.cpp

```cpp
#include <ApprovalTests.hpp>

TEST_CASE("TapeMode output matches approved", "[regression]") {
    TapeMode tape;
    tape.prepare(44100.0, 512);
    tape.setWow(0.5f);
    tape.setFlutter(0.3f);

    std::array<float, 4096> buffer;
    generateSine(buffer.data(), 4096, 440.0f, 44100.0f);
    tape.process(buffer.data(), 4096);

    // Convert to string for approval
    std::ostringstream oss;
    for (size_t i = 0; i < 4096; i += 64) {
        oss << std::fixed << std::setprecision(6) << buffer[i] << "\n";
    }

    Approvals::verify(oss.str());
}
```

### Manual Golden Master Approach

```cpp
TEST_CASE("Reverb matches reference", "[regression]") {
    Reverb reverb;
    reverb.prepare(44100.0, 512);
    reverb.setDecay(2.0f);
    reverb.setSize(0.8f);

    auto input = loadTestFile("impulse.wav");
    auto output = reverb.process(input);
    auto reference = loadTestFile("reverb_golden.wav");

    // Compare with tolerance
    REQUIRE(buffersMatch(output, reference, 1e-5f));
}

bool buffersMatch(const Buffer& a, const Buffer& b, float tolerance) {
    if (a.size() != b.size()) return false;

    for (size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) > tolerance) {
            return false;
        }
    }
    return true;
}
```

### When to Update Golden Masters

Update approved outputs when:
1. Intentionally changing algorithm behavior
2. Fixing a bug in the reference
3. Improving quality (document the change)

Never update because:
- Tests are "red" after unrelated changes
- You don't understand why output changed

---

## Continuous Integration

### CMake Integration

```cmake
# tests/CMakeLists.txt
enable_testing()
include(CTest)
include(Catch)

add_executable(dsp_tests
    unit/test_dsp_utils.cpp
    # ... more test files
)

target_link_libraries(dsp_tests PRIVATE Catch2::Catch2)
catch_discover_tests(dsp_tests)
```

### Running Tests

```bash
# Configure and build
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug

# Run all tests
ctest --preset windows-x64-debug

# Run with output on failure
ctest --test-dir build/tests -C Debug --output-on-failure

# Run specific test
./build/tests/Debug/dsp_tests.exe "[filter]"

# Run and generate JUnit XML
./build/tests/Debug/dsp_tests.exe -r junit -o test_results.xml
```

### Test Performance Targets

| Test Type | Target Time | Failure Action |
|-----------|-------------|----------------|
| Unit test | < 10ms | Optimize or skip in CI |
| Component test | < 100ms | Review if growing |
| Integration test | < 1s | Tag as [slow] |

### Pre-Commit Checklist

Before committing code:
1. All unit tests pass
2. No new compiler warnings
3. `vstvalidator` passes on built plugin
4. Code coverage hasn't decreased

---

## References

### General Testing Best Practices
- [Test Behavior, Not Implementation (Google)](https://testing.googleblog.com/2013/08/testing-on-toilet-test-behavior-not.html)
- [Mocks Aren't Stubs (Martin Fowler)](https://martinfowler.com/articles/mocksArentStubs.html)
- [Unit Testing Anti-Patterns](https://www.yegor256.com/2018/12/11/unit-testing-anti-patterns.html)
- [Software Testing Anti-patterns](https://blog.codepipes.com/testing/software-testing-antipatterns.html)

### DSP Testing
- [When to Test DSP Code (Melatonin)](https://melatonin.dev/blog/when-to-write-tests-for-dsp-code/)
- [Unit Testing Audio Processors with JUCE & Catch2](https://ejaaskel.dev/unit-testing-audio-processors-with-juce-catch2/)

### VST3 Development
- [VST3 Developer Portal](https://steinbergmedia.github.io/vst3_dev_portal/)
- [VST3 Validator Documentation](https://steinbergmedia.github.io/vst3_dev_portal/pages/What+is+the+VST+3+SDK/Validator.html)

### Frameworks
- [Catch2 Documentation](https://github.com/catchorg/Catch2)
- [ApprovalTests.cpp](https://approvaltestscpp.readthedocs.io/)

### Books
- *Unit Testing Principles, Practices, and Patterns* by Vladimir Khorikov
- *Designing Audio Effect Plugins in C++* by Will Pirkle
