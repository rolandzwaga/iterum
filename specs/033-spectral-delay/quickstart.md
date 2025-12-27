# Quickstart: Spectral Delay

**Feature**: 033-spectral-delay
**Date**: 2025-12-26

## Test-Driven Development Scenarios

This document defines the test scenarios that will drive implementation. Each scenario maps to spec requirements and can be independently verified.

---

## Phase 1: Setup & Foundational Tests

### Scenario S01: Default Construction
```cpp
TEST_CASE("SpectralDelay default construction") {
    SpectralDelay delay;
    REQUIRE_FALSE(delay.isPrepared());
    REQUIRE(delay.getFFTSize() == SpectralDelay::kDefaultFFTSize);
    REQUIRE(delay.getBaseDelayMs() == Approx(SpectralDelay::kDefaultDelayMs));
}
```

### Scenario S02: Prepare at Various Sample Rates
```cpp
TEST_CASE("SpectralDelay prepare at various sample rates") {
    SECTION("44100 Hz") { /* prepare, verify isPrepared() */ }
    SECTION("48000 Hz") { /* prepare, verify isPrepared() */ }
    SECTION("96000 Hz") { /* prepare, verify isPrepared() */ }
    SECTION("192000 Hz") { /* prepare, verify isPrepared() */ }
}
```

### Scenario S03: Reset Clears State
```cpp
TEST_CASE("SpectralDelay reset clears internal buffers") {
    // Process some audio
    // Reset
    // Verify no residual signal in output
}
```

### Scenario S04: Latency Reports FFT Size
```cpp
TEST_CASE("SpectralDelay latency equals FFT size") {
    SpectralDelay delay;
    delay.setFFTSize(1024);
    delay.prepare(44100.0, 512);
    REQUIRE(delay.getLatencySamples() == 1024);
}
```

---

## Phase 2: User Story 1 - Basic Spectral Delay

### Scenario S10: Uniform Delay (0% Spread)
```cpp
TEST_CASE("SpectralDelay with 0% spread produces coherent echo") {
    // FR-010: At 0% spread, all bands have identical delay time
    SpectralDelay delay;
    delay.prepare(44100.0, 512);
    delay.setBaseDelayMs(100.0f);
    delay.setSpreadMs(0.0f);
    delay.setDryWetMix(100.0f);
    delay.setFeedback(0.0f);

    // Process impulse
    // Verify delayed output resembles original (coherent echo)
}
```

### Scenario S11: Delayed Output Appears After Delay Time
```cpp
TEST_CASE("SpectralDelay output appears after configured delay") {
    // Feed impulse, verify output peak appears at baseDelayMs
}
```

### Scenario S12: Dry/Wet Mix at 0% (Dry Only)
```cpp
TEST_CASE("SpectralDelay 0% wet outputs only dry signal") {
    // FR-023: Dry/wet mix control
    delay.setDryWetMix(0.0f);
    // Verify output = input (no delayed signal)
}
```

### Scenario S13: Dry/Wet Mix at 100% (Wet Only)
```cpp
TEST_CASE("SpectralDelay 100% wet outputs only delayed signal") {
    // First samples should be near-zero (no dry)
}
```

---

## Phase 3: User Story 2 - Delay Spread Control

### Scenario S20: Spread Direction LowToHigh
```cpp
TEST_CASE("SpectralDelay LowToHigh spread delays high frequencies more") {
    // FR-009: Spread distributes delay according to direction
    delay.setSpreadDirection(SpreadDirection::LowToHigh);
    delay.setSpreadMs(500.0f);

    // Process impulse
    // Verify high-frequency content arrives later than low-frequency
}
```

### Scenario S21: Spread Direction HighToLow
```cpp
TEST_CASE("SpectralDelay HighToLow spread delays low frequencies more") {
    delay.setSpreadDirection(SpreadDirection::HighToLow);
    // Verify low-frequency content arrives later
}
```

### Scenario S22: Spread Direction CenterOut
```cpp
TEST_CASE("SpectralDelay CenterOut spread delays edge frequencies more") {
    delay.setSpreadDirection(SpreadDirection::CenterOut);
    // Verify mid-frequencies arrive first, edges arrive later
}
```

### Scenario S23: Base Delay + Spread Range
```cpp
TEST_CASE("SpectralDelay delay range spans baseDelay to baseDelay+spread") {
    // FR-006, FR-007: Base delay and spread amount controls
    delay.setBaseDelayMs(500.0f);
    delay.setSpreadMs(500.0f);
    // Verify shortest delay = 500ms, longest = 1000ms
}
```

---

## Phase 4: User Story 3 - Spectral Freeze

### Scenario S30: Freeze Holds Spectrum Indefinitely
```cpp
TEST_CASE("SpectralDelay freeze sustains output indefinitely") {
    // FR-014: Frozen output continues until disabled
    delay.setFreezeEnabled(true);
    // Process audio, then silence
    // Verify output continues with frozen spectrum
}
```

### Scenario S31: Freeze Ignores New Input
```cpp
TEST_CASE("SpectralDelay freeze ignores new input") {
    // FR-012: When frozen, system holds current spectrum
    delay.setFreezeEnabled(true);
    // Feed different audio
    // Verify output doesn't change
}
```

### Scenario S32: Freeze Transition is Click-Free
```cpp
TEST_CASE("SpectralDelay freeze transition has no clicks") {
    // FR-013: Crossfade 50-100ms
    // Enable freeze
    // Verify smooth amplitude transition (no discontinuities)
}
```

### Scenario S33: Unfreeze Resumes Normal Processing
```cpp
TEST_CASE("SpectralDelay unfreeze returns to normal processing") {
    delay.setFreezeEnabled(true);
    // ... frozen state ...
    delay.setFreezeEnabled(false);
    // Feed new audio
    // Verify new audio appears in output
}
```

---

## Phase 5: User Story 4 - Frequency-Dependent Feedback

### Scenario S40: Uniform Feedback Decays All Bands Equally
```cpp
TEST_CASE("SpectralDelay uniform feedback decays all bands equally") {
    // FR-015: Global feedback control
    delay.setFeedback(0.5f);
    delay.setFeedbackTilt(0.0f);
    // Process impulse, measure decay rate per band
    // Verify similar decay across frequency spectrum
}
```

### Scenario S41: Negative Tilt Sustains Low Frequencies
```cpp
TEST_CASE("SpectralDelay negative tilt sustains lows longer") {
    // FR-017: Negative tilt increases low-frequency feedback
    delay.setFeedbackTilt(-1.0f);
    // Verify low bands decay slower than high bands
}
```

### Scenario S42: Positive Tilt Sustains High Frequencies
```cpp
TEST_CASE("SpectralDelay positive tilt sustains highs longer") {
    // FR-018: Positive tilt increases high-frequency feedback
    delay.setFeedbackTilt(1.0f);
    // Verify high bands decay slower than low bands
}
```

### Scenario S43: Feedback Over 100% is Soft-Limited
```cpp
TEST_CASE("SpectralDelay feedback over 100% is limited") {
    // FR-019: Soft limiting prevents oscillation
    delay.setFeedback(1.2f);
    // Process signal
    // Verify no runaway oscillation (output bounded)
}
```

---

## Phase 6: User Story 5 - Diffusion

### Scenario S50: Zero Diffusion Preserves Spectrum
```cpp
TEST_CASE("SpectralDelay 0% diffusion preserves clean spectrum") {
    // FR-022: At 0% diffusion, spectral content unchanged
    delay.setDiffusion(0.0f);
    // Process pure tone
    // Verify output has same spectral content as input
}
```

### Scenario S51: High Diffusion Blurs Spectrum
```cpp
TEST_CASE("SpectralDelay 100% diffusion blurs spectrum") {
    // FR-021: Diffusion spreads spectral energy
    delay.setDiffusion(1.0f);
    // Process pure tone
    // Verify energy spreads to neighboring bins
}
```

---

## Phase 7: Output Controls

### Scenario S60: Output Gain Boost
```cpp
TEST_CASE("SpectralDelay +6dB gain boosts output") {
    // FR-024: Output gain control
    delay.setOutputGainDb(6.0f);
    // Verify output amplitude is ~2x input
}
```

### Scenario S61: Output Gain Mute
```cpp
TEST_CASE("SpectralDelay -96dB gain mutes output") {
    delay.setOutputGainDb(-96.0f);
    // Verify output is near-silent
}
```

---

## Phase 8: FFT Size Configuration

### Scenario S70: FFT Size 512
```cpp
TEST_CASE("SpectralDelay works with FFT size 512") {
    delay.setFFTSize(512);
    delay.prepare(44100.0, 512);
    REQUIRE(delay.getLatencySamples() == 512);
    // Process audio, verify output
}
```

### Scenario S71: FFT Size 2048
```cpp
TEST_CASE("SpectralDelay works with FFT size 2048") {
    delay.setFFTSize(2048);
    delay.prepare(44100.0, 512);
    REQUIRE(delay.getLatencySamples() == 2048);
}
```

### Scenario S72: FFT Size 4096
```cpp
TEST_CASE("SpectralDelay works with FFT size 4096") {
    delay.setFFTSize(4096);
    delay.prepare(44100.0, 512);
    REQUIRE(delay.getLatencySamples() == 4096);
}
```

---

## Performance Scenarios

### Scenario P01: CPU Budget at 44.1kHz Stereo
```cpp
TEST_CASE("SpectralDelay CPU < 3% at 44.1kHz stereo 2048 FFT") {
    // SC-005: CPU usage less than 3%
    // Benchmark 10 seconds of processing
    // Verify average CPU < 3%
}
```

### Scenario P02: Real-Time Safe Processing
```cpp
TEST_CASE("SpectralDelay process() makes no allocations") {
    // Principle II: Real-time safety
    // Use memory profiler or allocation tracker
    // Verify zero allocations during process()
}
```

---

## Edge Cases

### Scenario E01: Silent Input
```cpp
TEST_CASE("SpectralDelay handles silent input without artifacts") {
    // Feed zeros
    // Verify no noise floor artifacts, output remains silent
}
```

### Scenario E02: Full Scale Input
```cpp
TEST_CASE("SpectralDelay handles full scale input") {
    // Feed +/- 1.0 samples
    // Verify no clipping or distortion
}
```

### Scenario E03: Sample Rate Change
```cpp
TEST_CASE("SpectralDelay handles re-prepare at different sample rate") {
    delay.prepare(44100.0, 512);
    // Process some audio
    delay.prepare(96000.0, 512);  // Re-prepare at new rate
    REQUIRE(delay.isPrepared());
    // Process audio at new rate
}
```
