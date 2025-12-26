# Quickstart: Multi-Tap Delay Mode

**Feature**: 028-multi-tap
**Date**: 2025-12-26

## Basic Usage

### Initialize and Process

```cpp
#include "dsp/features/multi_tap_delay.h"

using namespace Iterum::DSP;

// Create instance
MultiTapDelay multiTap;

// Prepare for processing
float sampleRate = 44100.0f;
size_t maxBlockSize = 512;
float maxDelayMs = 5000.0f;
multiTap.prepare(sampleRate, maxBlockSize, maxDelayMs);

// Load a timing pattern with 4 taps
multiTap.loadTimingPattern(TimingPattern::DottedEighth, 4);

// Apply a spatial pattern
multiTap.loadSpatialPattern(SpatialPattern::Cascade);

// Process audio
std::array<float, 512> leftIn{}, rightIn{}, leftOut{}, rightOut{};
multiTap.process(leftIn.data(), rightIn.data(),
                 leftOut.data(), rightOut.data(), 512);
```

### Tempo Sync

```cpp
// Set tempo for tempo-synced patterns
multiTap.setTempo(140.0f);  // 140 BPM

// Load a note-based pattern
multiTap.loadTimingPattern(TimingPattern::TripletEighth, 6);

// Or set base note value directly
multiTap.setBaseNoteValue(NoteValue::Quarter, NoteModifier::Dotted);
```

### Pattern Morphing

```cpp
// Morph to a new pattern over 500ms
multiTap.morphToTimingPattern(TimingPattern::GoldenRatio, 500.0f);

// Check morph progress
while (multiTap.isMorphing()) {
    float progress = multiTap.getMorphProgress();  // 0.0 to 1.0
    // Process audio during morph
    multiTap.process(in, in, out, out, blockSize);
}
```

### Per-Tap Configuration

```cpp
// Configure individual tap
TapConfiguration config;
config.enabled = true;
config.levelDb = -6.0f;
config.pan = -50.0f;  // Left of center
config.filterMode = TapFilterMode::Lowpass;
config.filterCutoff = 2000.0f;

multiTap.setTapConfiguration(2, config);  // Apply to tap 3 (0-indexed)

// Read back configuration
TapConfiguration readBack = multiTap.getTapConfiguration(2);
```

### Master Feedback

```cpp
// Set master feedback with filtering
multiTap.setMasterFeedback(60.0f);           // 60%
multiTap.setFeedbackLowpassCutoff(4000.0f);  // Darken repeats
multiTap.setFeedbackHighpassCutoff(100.0f);  // Remove mud

// Mix control
multiTap.setDryWetMix(50.0f);  // 50/50 mix
multiTap.setOutputLevel(0.0f); // Unity gain
```

### Modulation Integration

```cpp
#include "dsp/systems/modulation_matrix.h"

// Create shared modulation matrix
ModulationMatrix modMatrix;
modMatrix.prepare(sampleRate, maxBlockSize);

// Connect to multi-tap
multiTap.setModulationMatrix(&modMatrix);
multiTap.registerModulationDestinations();

// Route LFO to tap 3 time (destination ID = 2)
modMatrix.createRoute(lfoSourceId, 2 /* tap 2 time */, 0.1f /* 10% depth */);
```

## Test Scenarios

### US1: Basic Multi-Tap Rhythmic Delay

```cpp
TEST_CASE("Dotted eighth pattern creates correct tap times", "[multi-tap][us1]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);
    multiTap.setTempo(120.0f);  // 500ms quarter note

    multiTap.loadTimingPattern(TimingPattern::DottedEighth, 4);

    // Dotted eighth = 0.75 × quarter = 375ms
    // Expected: 375, 750, 1125, 1500ms
    REQUIRE(multiTap.getTapConfiguration(0).timeMs == Approx(375.0f));
    REQUIRE(multiTap.getTapConfiguration(1).timeMs == Approx(750.0f));
    REQUIRE(multiTap.getTapConfiguration(2).timeMs == Approx(1125.0f));
    REQUIRE(multiTap.getTapConfiguration(3).timeMs == Approx(1500.0f));
}

TEST_CASE("Golden ratio pattern creates correct exponential spacing", "[multi-tap][us1]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);
    multiTap.setTempo(120.0f);  // 500ms quarter note

    multiTap.loadTimingPattern(TimingPattern::GoldenRatio, 4);

    // φ = 1.618: 500, 809, 1309, 2118ms
    const float phi = 1.618f;
    REQUIRE(multiTap.getTapConfiguration(0).timeMs == Approx(500.0f));
    REQUIRE(multiTap.getTapConfiguration(1).timeMs == Approx(500.0f * phi).margin(1.0f));
    REQUIRE(multiTap.getTapConfiguration(2).timeMs == Approx(500.0f * phi * phi).margin(1.0f));
    REQUIRE(multiTap.getTapConfiguration(3).timeMs == Approx(500.0f * phi * phi * phi).margin(1.0f));
}
```

### US2: Per-Tap Level and Pan Control

```cpp
TEST_CASE("Per-tap pan uses constant-power pan law", "[multi-tap][us2]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);

    // Create impulse input
    std::array<float, 512> impulse{};
    impulse[0] = 1.0f;
    std::array<float, 512> leftOut{}, rightOut{};

    // Set tap 0 to hard left
    TapConfiguration config;
    config.enabled = true;
    config.timeMs = 100.0f;
    config.levelDb = 0.0f;
    config.pan = -100.0f;  // Full left
    multiTap.setTapConfiguration(0, config);

    multiTap.setDryWetMix(100.0f);
    multiTap.process(impulse.data(), impulse.data(),
                     leftOut.data(), rightOut.data(), 512);

    // Find tap output (at ~4410 samples for 100ms)
    size_t tapSample = static_cast<size_t>(100.0f * 44.1f);
    REQUIRE(std::abs(leftOut[tapSample]) > 0.5f);   // Left has signal
    REQUIRE(std::abs(rightOut[tapSample]) < 0.01f); // Right is silent
}
```

### US3: Master Feedback with Filtering

```cpp
TEST_CASE("Feedback creates multiple generations", "[multi-tap][us3]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);

    // Single tap at 100ms, 50% feedback
    multiTap.loadTimingPattern(TimingPattern::QuarterNote, 1);
    multiTap.setTapConfiguration(0, {.enabled = true, .timeMs = 100.0f, .levelDb = 0.0f});
    multiTap.setMasterFeedback(50.0f);
    multiTap.setDryWetMix(100.0f);

    // Process impulse
    std::vector<float> output(44100);  // 1 second
    std::array<float, 512> impulse{};
    impulse[0] = 1.0f;

    for (size_t i = 0; i < output.size(); i += 512) {
        multiTap.process(impulse.data(), impulse.data(),
                         output.data() + i, output.data() + i,
                         std::min(size_t(512), output.size() - i));
        std::fill(impulse.begin(), impulse.end(), 0.0f);  // Only first block has impulse
    }

    // Check multiple feedback generations
    float gen1 = std::abs(output[4410]);   // 100ms
    float gen2 = std::abs(output[8820]);   // 200ms
    float gen3 = std::abs(output[13230]);  // 300ms

    REQUIRE(gen1 > 0.5f);
    REQUIRE(gen2 == Approx(gen1 * 0.5f).margin(0.05f));  // 50% of previous
    REQUIRE(gen3 == Approx(gen2 * 0.5f).margin(0.05f));
}

TEST_CASE("Feedback above 100% remains stable", "[multi-tap][us3]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);

    multiTap.loadTimingPattern(TimingPattern::QuarterNote, 1);
    multiTap.setMasterFeedback(110.0f);  // Self-oscillation
    multiTap.setDryWetMix(100.0f);

    // Process 10 seconds
    std::array<float, 512> buffer{};
    buffer[0] = 0.5f;  // Initial impulse
    float maxLevel = 0.0f;

    for (int i = 0; i < 10 * 44100 / 512; ++i) {
        multiTap.process(buffer.data(), buffer.data(),
                         buffer.data(), buffer.data(), 512);

        for (float sample : buffer) {
            maxLevel = std::max(maxLevel, std::abs(sample));
        }
    }

    // SC-006: Output should not exceed +6 dBFS (~2.0 linear)
    REQUIRE(maxLevel < 2.0f);
}
```

### US4: Pattern Morphing

```cpp
TEST_CASE("Pattern morph interpolates smoothly", "[multi-tap][us4]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);
    multiTap.setTempo(120.0f);

    // Start with quarter note pattern
    multiTap.loadTimingPattern(TimingPattern::QuarterNote, 4);
    float initialTap1Time = multiTap.getTapConfiguration(0).timeMs;

    // Morph to triplet over 500ms
    multiTap.morphToTimingPattern(TimingPattern::TripletQuarter, 500.0f);

    REQUIRE(multiTap.isMorphing());

    // Process 250ms (half the morph time)
    std::array<float, 512> buffer{};
    for (int i = 0; i < 250 * 44100 / 1000 / 512; ++i) {
        multiTap.process(buffer.data(), buffer.data(),
                         buffer.data(), buffer.data(), 512);
    }

    // Should be approximately halfway between patterns
    float midpoint = multiTap.getMorphProgress();
    REQUIRE(midpoint > 0.4f);
    REQUIRE(midpoint < 0.6f);

    // Finish morph
    while (multiTap.isMorphing()) {
        multiTap.process(buffer.data(), buffer.data(),
                         buffer.data(), buffer.data(), 512);
    }

    // Should now have triplet timing
    float tripletTime = 500.0f * (2.0f / 3.0f);  // ~333ms
    REQUIRE(multiTap.getTapConfiguration(0).timeMs == Approx(tripletTime).margin(1.0f));
}
```

### US5: Per-Tap Modulation

```cpp
TEST_CASE("LFO modulates specific tap time", "[multi-tap][us5]") {
    MultiTapDelay multiTap;
    ModulationMatrix modMatrix;

    multiTap.prepare(44100.0f, 512, 5000.0f);
    modMatrix.prepare(44100.0f, 512);

    // Create mock LFO source
    MockLFO lfo;
    lfo.setValue(0.5f);  // 50% of range

    modMatrix.registerSource(0, &lfo);
    multiTap.setModulationMatrix(&modMatrix);
    multiTap.registerModulationDestinations();

    // Load pattern
    multiTap.loadTimingPattern(TimingPattern::QuarterNote, 4);

    // Route LFO to tap 2 time (dest ID = 2) with 10% depth
    modMatrix.createRoute(0, 2, 0.1f, ModulationMode::Bipolar);

    // Process
    std::array<float, 512> buffer{};
    multiTap.process(buffer.data(), buffer.data(),
                     buffer.data(), buffer.data(), 512);

    // Tap 2 time should be modulated, others should not
    float baseTap2Time = 500.0f * 3;  // Third tap of quarter pattern
    float modRange = baseTap2Time * 0.1f;  // 10% depth

    // With LFO at 0.5 (center), modulation should be 0
    // But any non-zero LFO value would shift the time
}
```

### US6: Tempo Sync

```cpp
TEST_CASE("Tempo change updates synced tap times", "[multi-tap][us6]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);

    // Set initial tempo
    multiTap.setTempo(120.0f);  // 500ms quarter
    multiTap.loadTimingPattern(TimingPattern::QuarterNote, 4);

    float time120bpm = multiTap.getTapConfiguration(0).timeMs;
    REQUIRE(time120bpm == Approx(500.0f));

    // Change tempo
    multiTap.setTempo(140.0f);  // ~428ms quarter

    // Process one block to update
    std::array<float, 512> buffer{};
    multiTap.process(buffer.data(), buffer.data(),
                     buffer.data(), buffer.data(), 512);

    float time140bpm = multiTap.getTapConfiguration(0).timeMs;
    REQUIRE(time140bpm == Approx(60000.0f / 140.0f).margin(1.0f));  // ~428ms
}
```

### Edge Cases

```cpp
TEST_CASE("Single tap works correctly", "[multi-tap][edge]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);

    multiTap.loadTimingPattern(TimingPattern::QuarterNote, 1);
    REQUIRE(multiTap.getActiveTapCount() == 1);

    // Should still produce output
    std::array<float, 512> impulse{};
    std::array<float, 512> output{};
    impulse[0] = 1.0f;

    multiTap.setDryWetMix(100.0f);
    multiTap.process(impulse.data(), impulse.data(),
                     output.data(), output.data(), 512);

    // Find non-zero output
    bool hasOutput = false;
    for (float sample : output) {
        if (std::abs(sample) > 0.01f) hasOutput = true;
    }
    REQUIRE(hasOutput);
}

TEST_CASE("All taps muted produces silence", "[multi-tap][edge]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);

    multiTap.loadTimingPattern(TimingPattern::QuarterNote, 4);

    // Mute all taps
    for (size_t i = 0; i < 4; ++i) {
        TapConfiguration config = multiTap.getTapConfiguration(i);
        config.enabled = false;
        multiTap.setTapConfiguration(i, config);
    }

    multiTap.setDryWetMix(100.0f);  // Full wet

    std::array<float, 512> input{};
    std::array<float, 512> output{};
    input[0] = 1.0f;

    multiTap.process(input.data(), input.data(),
                     output.data(), output.data(), 512);

    // All output should be zero (wet only, no active taps)
    for (float sample : output) {
        REQUIRE(std::abs(sample) < 0.0001f);
    }
}

TEST_CASE("Pattern scales to any tap count", "[multi-tap][edge]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);

    // Test minimum
    multiTap.loadTimingPattern(TimingPattern::GoldenRatio, 2);
    REQUIRE(multiTap.getActiveTapCount() == 2);

    // Test maximum
    multiTap.loadTimingPattern(TimingPattern::GoldenRatio, 16);
    REQUIRE(multiTap.getActiveTapCount() == 16);

    // Test beyond maximum (should clamp)
    multiTap.loadTimingPattern(TimingPattern::GoldenRatio, 20);
    REQUIRE(multiTap.getActiveTapCount() == 16);
}
```

## Performance Verification

```cpp
TEST_CASE("CPU usage under 1% at 44.1kHz stereo", "[multi-tap][performance]") {
    MultiTapDelay multiTap;
    multiTap.prepare(44100.0f, 512, 5000.0f);

    // Maximum load: 16 taps, all active, with modulation
    multiTap.loadTimingPattern(TimingPattern::QuarterNote, 16);
    multiTap.loadSpatialPattern(SpatialPattern::Cascade);
    multiTap.setMasterFeedback(80.0f);

    std::array<float, 512> left{}, right{};
    std::fill(left.begin(), left.end(), 0.1f);
    std::fill(right.begin(), right.end(), 0.1f);

    // Process 10 seconds
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10 * 44100 / 512; ++i) {
        multiTap.process(left.data(), right.data(),
                         left.data(), right.data(), 512);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // 10 seconds of audio should process in < 100ms for 1% CPU
    REQUIRE(duration.count() < 100);
}
```
