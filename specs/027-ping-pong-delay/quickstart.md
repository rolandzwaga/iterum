# Quickstart: Ping-Pong Delay Mode

**Feature Branch**: `027-ping-pong-delay`
**Date**: 2025-12-26

## Test Scenarios

### Phase 1: Basic Ping-Pong (US1 - P1)

#### Test 1.1: Classic Alternating Delay
```cpp
TEST_CASE("Classic ping-pong alternates L/R", "[ping-pong][US1]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setLRRatio(LRRatio::OneToOne);
    delay.setDelayTimeMs(500.0f);  // 22050 samples
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);  // Full ping-pong
    delay.setMix(1.0f);  // Wet only

    // Process impulse
    std::array<float, 44100> left{};
    std::array<float, 44100> right{};
    left[0] = 1.0f;  // Impulse in left channel

    BlockContext ctx{44100.0, 44100, 120.0, false};
    delay.process(left.data(), right.data(), 44100, ctx);

    // Verify alternating pattern:
    // - First repeat at 500ms (22050 samples) should be in LEFT
    // - Second repeat at 1000ms should be in RIGHT
    // - Third repeat at 1500ms should be in LEFT
    const size_t firstRepeat = 22050;
    const size_t secondRepeat = 44100;

    // First repeat should have energy in left
    float leftEnergy1 = std::abs(left[firstRepeat]);
    float rightEnergy1 = std::abs(right[firstRepeat]);
    REQUIRE(leftEnergy1 > rightEnergy1);

    // Second repeat should have energy in right
    // (need longer buffer to test this)
}
```

#### Test 1.2: Feedback Decay
```cpp
TEST_CASE("Ping-pong feedback decays correctly", "[ping-pong][US1]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setFeedback(0.5f);  // -6dB per repeat
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    // Process impulse and verify each repeat is ~6dB quieter
    // (50% feedback = 0.5 amplitude = -6.02dB)
}
```

---

### Phase 2: Asymmetric Ratios (US2 - P2)

#### Test 2.1: 2:1 Ratio
```cpp
TEST_CASE("2:1 ratio produces correct timing", "[ping-pong][ratio][US2]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setLRRatio(LRRatio::TwoToOne);
    delay.setDelayTimeMs(500.0f);

    // Left delay = 500ms, Right delay = 250ms
    REQUIRE(delay.getLeftDelayMs() == Approx(500.0f));
    REQUIRE(delay.getRightDelayMs() == Approx(250.0f));
}
```

#### Test 2.2: All Ratios
```cpp
TEST_CASE("All ratios produce correct relationships", "[ping-pong][ratio][US2]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setDelayTimeMs(600.0f);

    SECTION("3:2 ratio") {
        delay.setLRRatio(LRRatio::ThreeToTwo);
        // L = 600ms, R = 400ms
        REQUIRE(delay.getRightDelayMs() == Approx(400.0f).margin(1.0f));
    }

    SECTION("4:3 ratio") {
        delay.setLRRatio(LRRatio::FourToThree);
        // L = 600ms, R = 450ms
        REQUIRE(delay.getRightDelayMs() == Approx(450.0f).margin(1.0f));
    }

    // Test inverse ratios
    SECTION("1:2 ratio") {
        delay.setLRRatio(LRRatio::OneToTwo);
        // L = 300ms, R = 600ms
        REQUIRE(delay.getLeftDelayMs() == Approx(300.0f).margin(1.0f));
    }
}
```

---

### Phase 3: Tempo Sync (US3 - P2)

#### Test 3.1: Quarter Note at 120 BPM
```cpp
TEST_CASE("Tempo sync produces correct timing", "[ping-pong][sync][US3]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setTimeMode(TimeMode::Synced);
    delay.setNoteValue(NoteValue::Quarter, NoteModifier::None);
    delay.setLRRatio(LRRatio::OneToOne);

    BlockContext ctx{44100.0, 1024, 120.0, true};  // 120 BPM

    // Quarter note at 120 BPM = 500ms
    // After processing, both channels should have 500ms delay
    delay.process(buffer.data(), bufferR.data(), 1024, ctx);

    REQUIRE(delay.getCurrentDelayMs() == Approx(500.0f).margin(1.0f));
}
```

---

### Phase 4: Stereo Width (US4 - P3)

#### Test 4.1: Width at 0% (Mono)
```cpp
TEST_CASE("Width 0% produces mono output", "[ping-pong][width][US4][SC-004]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setWidth(0.0f);
    delay.setMix(1.0f);

    // Process stereo signal
    // Verify L and R outputs are identical (correlation > 0.99)
}
```

#### Test 4.2: Width at 200% (Ultra-Wide)
```cpp
TEST_CASE("Width 200% produces wide stereo", "[ping-pong][width][US4][SC-005]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setWidth(200.0f);
    delay.setMix(1.0f);

    // Process signal
    // Verify correlation coefficient < 0.5
}
```

---

### Phase 5: Cross-Feedback (US5 - P3)

#### Test 5.1: Cross-Feedback at 0% (Dual Mono)
```cpp
TEST_CASE("Cross-feedback 0% isolates channels", "[ping-pong][crossfeed][US5][SC-006]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setCrossFeedback(0.0f);
    delay.setFeedback(0.5f);
    delay.setMix(1.0f);

    // Send impulse to LEFT only
    std::array<float, 44100> left{};
    std::array<float, 44100> right{};
    left[0] = 1.0f;

    // All repeats should stay in LEFT channel
    // Right channel should remain silent (>60dB isolation)
}
```

#### Test 5.2: Cross-Feedback at 100% (Full Ping-Pong)
```cpp
TEST_CASE("Cross-feedback 100% creates alternating pattern", "[ping-pong][crossfeed][US5][SC-007]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setCrossFeedback(1.0f);
    delay.setFeedback(0.5f);

    // Send impulse to LEFT only
    // Repeats should alternate: L -> R -> L -> R
}
```

---

### Phase 6: Modulation (US6 - P4)

#### Test 6.1: Zero Modulation
```cpp
TEST_CASE("Modulation depth 0% produces no pitch variation", "[ping-pong][mod][US6][FR-022]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setModulationDepth(0.0f);
    delay.setModulationRate(1.0f);

    // Process sustained note
    // Verify no pitch variation in output
}
```

---

### Phase 7: Edge Cases

#### Test 7.1: Feedback Limiting
```cpp
TEST_CASE("Feedback > 100% is limited", "[ping-pong][edge][SC-009]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setFeedback(1.2f);  // 120%

    // Process continuous signal
    // Verify output never exceeds 0dBFS (no clipping)
}
```

#### Test 7.2: Minimum Delay Time
```cpp
TEST_CASE("Minimum delay time works", "[ping-pong][edge]") {
    PingPongDelay delay;
    delay.prepare(44100.0, 1024, 10000.0f);
    delay.setDelayTimeMs(1.0f);  // 1ms = ~44 samples

    // Should produce comb filtering, no crashes
}
```

---

## Build Commands

```bash
# Build tests
cmake --build --preset windows-x64-debug --target dsp_tests

# Run ping-pong tests only
"./build/bin/Debug/dsp_tests.exe" "[ping-pong]"

# Run with specific user story
"./build/bin/Debug/dsp_tests.exe" "[US1]"
"./build/bin/Debug/dsp_tests.exe" "[US2]"
```

## File Structure

```
tests/unit/features/
└── ping_pong_delay_test.cpp   # All tests for this feature

src/dsp/features/
└── ping_pong_delay.h          # Main implementation
```
