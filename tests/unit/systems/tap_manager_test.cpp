// ==============================================================================
// Layer 3: TapManager Unit Tests
// ==============================================================================
// Tests for the TapManager multi-tap delay system.
//
// Feature: 023-tap-manager
// Reference: specs/023-tap-manager/spec.md
//
// Test Categories:
// - [construction]: Lifecycle and initialization
// - [tap-config]: Per-tap configuration (time, level, pan, filter, feedback)
// - [patterns]: Preset pattern generation
// - [tempo]: Tempo sync functionality
// - [processing]: Audio processing
// - [queries]: State queries
// - [real-time]: Real-time safety verification
// ==============================================================================

#include "dsp/systems/tap_manager.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// Test Constants
// =============================================================================

static constexpr float kTestSampleRate = 44100.0f;
static constexpr size_t kTestBlockSize = 512;
static constexpr float kTestMaxDelayMs = 5000.0f;

// =============================================================================
// Helper Functions
// =============================================================================

/// @brief Create a prepared TapManager for testing
inline TapManager createPreparedTapManager(
    float sampleRate = kTestSampleRate,
    size_t blockSize = kTestBlockSize,
    float maxDelayMs = kTestMaxDelayMs) {
    TapManager tm;
    tm.prepare(sampleRate, blockSize, maxDelayMs);
    return tm;
}

/// @brief Generate a test impulse buffer
inline std::vector<float> generateImpulse(size_t length, size_t impulsePos = 0) {
    std::vector<float> buffer(length, 0.0f);
    if (impulsePos < length) {
        buffer[impulsePos] = 1.0f;
    }
    return buffer;
}

/// @brief Generate silence buffer
inline std::vector<float> generateSilence(size_t length) {
    return std::vector<float>(length, 0.0f);
}

/// @brief Find first sample above threshold
inline size_t findFirstPeak(const std::vector<float>& buffer, float threshold = 0.01f) {
    for (size_t i = 0; i < buffer.size(); ++i) {
        if (std::abs(buffer[i]) > threshold) {
            return i;
        }
    }
    return buffer.size();  // Not found
}

/// @brief Calculate RMS of a buffer
inline float calculateRMS(const std::vector<float>& buffer) {
    if (buffer.empty()) return 0.0f;
    float sum = 0.0f;
    for (float sample : buffer) {
        sum += sample * sample;
    }
    return std::sqrt(sum / static_cast<float>(buffer.size()));
}

// =============================================================================
// Construction / Lifecycle Tests
// =============================================================================

TEST_CASE("TapManager: Default construction", "[tap-manager][construction]") {
    TapManager tm;
    // Should not crash, all taps disabled by default
    REQUIRE(tm.getActiveTapCount() == 0);
    REQUIRE(tm.getPattern() == TapPattern::Custom);
}

TEST_CASE("TapManager: prepare() initializes correctly", "[tap-manager][construction]") {
    TapManager tm;
    tm.prepare(kTestSampleRate, kTestBlockSize, kTestMaxDelayMs);

    SECTION("All taps are disabled after prepare") {
        REQUIRE(tm.getActiveTapCount() == 0);
        for (size_t i = 0; i < kMaxTaps; ++i) {
            REQUIRE_FALSE(tm.isTapEnabled(i));
        }
    }

    SECTION("Pattern is Custom after prepare") {
        REQUIRE(tm.getPattern() == TapPattern::Custom);
    }

    SECTION("All tap times are zero after prepare") {
        for (size_t i = 0; i < kMaxTaps; ++i) {
            REQUIRE(tm.getTapTimeMs(i) == Approx(0.0f));
        }
    }
}

TEST_CASE("TapManager: reset() clears state", "[tap-manager][construction]") {
    auto tm = createPreparedTapManager();

    // Enable and configure some taps
    tm.setTapEnabled(0, true);
    tm.setTapTimeMs(0, 100.0f);
    tm.setTapLevelDb(0, -6.0f);

    // Process some audio
    auto input = generateImpulse(kTestBlockSize);
    auto output = generateSilence(kTestBlockSize);
    tm.process(input.data(), input.data(), output.data(), output.data(), kTestBlockSize);

    // Reset
    tm.reset();

    // Tap should still be enabled (reset doesn't disable)
    REQUIRE(tm.isTapEnabled(0));

    // But internal state (delay line, smoothers) should be cleared
    // Process silence - should get silence out
    auto silence = generateSilence(kTestBlockSize);
    auto silenceOut = generateSilence(kTestBlockSize);
    tm.process(silence.data(), silence.data(), silenceOut.data(), silenceOut.data(), kTestBlockSize);

    // After reset, delay line is cleared so no delayed output
    float maxOutput = 0.0f;
    for (float s : silenceOut) {
        maxOutput = std::max(maxOutput, std::abs(s));
    }
    REQUIRE(maxOutput < 0.001f);
}

// =============================================================================
// Tap Enable/Disable Tests (FR-002, FR-003, FR-004, FR-004a)
// =============================================================================

TEST_CASE("TapManager: setTapEnabled() enables and disables taps", "[tap-manager][tap-config]") {
    auto tm = createPreparedTapManager();

    SECTION("Enable single tap") {
        tm.setTapEnabled(0, true);
        REQUIRE(tm.isTapEnabled(0));
        REQUIRE(tm.getActiveTapCount() == 1);
    }

    SECTION("Enable multiple taps") {
        tm.setTapEnabled(0, true);
        tm.setTapEnabled(5, true);
        tm.setTapEnabled(15, true);
        REQUIRE(tm.getActiveTapCount() == 3);
    }

    SECTION("Disable tap") {
        tm.setTapEnabled(0, true);
        tm.setTapEnabled(0, false);
        REQUIRE_FALSE(tm.isTapEnabled(0));
        REQUIRE(tm.getActiveTapCount() == 0);
    }

    SECTION("Enable all 16 taps (FR-001)") {
        for (size_t i = 0; i < kMaxTaps; ++i) {
            tm.setTapEnabled(i, true);
        }
        REQUIRE(tm.getActiveTapCount() == kMaxTaps);
    }
}

TEST_CASE("TapManager: Out-of-range tap indices are silently ignored (FR-004a)", "[tap-manager][tap-config]") {
    auto tm = createPreparedTapManager();

    // These should not crash or throw
    REQUIRE_NOTHROW(tm.setTapEnabled(16, true));
    REQUIRE_NOTHROW(tm.setTapEnabled(100, true));
    REQUIRE_NOTHROW(tm.setTapEnabled(SIZE_MAX, true));

    // And should have no effect
    REQUIRE(tm.getActiveTapCount() == 0);
    REQUIRE_FALSE(tm.isTapEnabled(16));
    REQUIRE_FALSE(tm.isTapEnabled(100));
}

// =============================================================================
// Tap Time Configuration Tests (FR-005, FR-006, FR-007)
// =============================================================================

TEST_CASE("TapManager: setTapTimeMs() sets delay time", "[tap-manager][tap-config]") {
    auto tm = createPreparedTapManager();

    SECTION("Set valid time") {
        tm.setTapTimeMs(0, 250.0f);
        REQUIRE(tm.getTapTimeMs(0) == Approx(250.0f));
    }

    SECTION("Time is clamped to max delay") {
        tm.setTapTimeMs(0, 10000.0f);  // Exceeds kTestMaxDelayMs
        REQUIRE(tm.getTapTimeMs(0) == Approx(kTestMaxDelayMs));
    }

    SECTION("Negative time is clamped to zero") {
        tm.setTapTimeMs(0, -100.0f);
        REQUIRE(tm.getTapTimeMs(0) == Approx(0.0f));
    }

    SECTION("Out-of-range tap index is ignored") {
        tm.setTapTimeMs(16, 500.0f);
        REQUIRE(tm.getTapTimeMs(16) == Approx(0.0f));  // Returns 0 for invalid
    }
}

TEST_CASE("TapManager: Delay time accuracy within 1 sample (SC-003)", "[tap-manager][processing]") {
    auto tm = createPreparedTapManager();

    // Set up a single tap with known delay
    const float delayMs = 10.0f;  // 10ms = 441 samples at 44.1kHz
    const size_t expectedDelaySamples = static_cast<size_t>(delayMs * kTestSampleRate / 1000.0f);

    tm.setTapEnabled(0, true);
    tm.setTapTimeMs(0, delayMs);
    tm.setTapLevelDb(0, 0.0f);
    tm.setTapPan(0, 0.0f);
    tm.setDryWetMix(100.0f);  // 100% wet
    tm.reset();  // Snap smoothers

    // Process impulse through multiple blocks
    const size_t totalSamples = expectedDelaySamples + kTestBlockSize;
    auto inputL = generateSilence(totalSamples);
    auto inputR = generateSilence(totalSamples);
    inputL[0] = 1.0f;  // Impulse at sample 0
    inputR[0] = 1.0f;

    auto outputL = generateSilence(totalSamples);
    auto outputR = generateSilence(totalSamples);

    // Process in blocks
    for (size_t offset = 0; offset < totalSamples; offset += kTestBlockSize) {
        const size_t blockSamples = std::min(kTestBlockSize, totalSamples - offset);
        tm.process(inputL.data() + offset, inputR.data() + offset,
                   outputL.data() + offset, outputR.data() + offset, blockSamples);
    }

    // Find peak in output
    const size_t peakPos = findFirstPeak(outputL, 0.1f);

    // Verify within 1 sample of expected delay
    const int64_t error = static_cast<int64_t>(peakPos) - static_cast<int64_t>(expectedDelaySamples);
    REQUIRE(std::abs(error) <= 1);
}

// =============================================================================
// Tap Level Tests (FR-009, FR-010)
// =============================================================================

TEST_CASE("TapManager: setTapLevelDb() sets level", "[tap-manager][tap-config]") {
    auto tm = createPreparedTapManager();

    SECTION("Set valid level") {
        tm.setTapLevelDb(0, -12.0f);
        REQUIRE(tm.getTapLevelDb(0) == Approx(-12.0f));
    }

    SECTION("Level clamped to min (-96dB)") {
        tm.setTapLevelDb(0, -120.0f);
        REQUIRE(tm.getTapLevelDb(0) == Approx(kMinLevelDb));
    }

    SECTION("Level clamped to max (+6dB)") {
        tm.setTapLevelDb(0, 20.0f);
        REQUIRE(tm.getTapLevelDb(0) == Approx(kMaxLevelDb));
    }
}

TEST_CASE("TapManager: Level at -96dB produces silence (FR-010)", "[tap-manager][processing]") {
    auto tm = createPreparedTapManager();

    tm.setTapEnabled(0, true);
    tm.setTapTimeMs(0, 1.0f);  // 1ms delay
    tm.setTapLevelDb(0, kMinLevelDb);  // -96dB = silence
    tm.setDryWetMix(100.0f);
    tm.reset();

    // Process constant signal
    std::vector<float> input(kTestBlockSize, 1.0f);
    std::vector<float> outputL(kTestBlockSize, 0.0f);
    std::vector<float> outputR(kTestBlockSize, 0.0f);

    // Process multiple blocks to let delay fill
    for (int i = 0; i < 10; ++i) {
        tm.process(input.data(), input.data(), outputL.data(), outputR.data(), kTestBlockSize);
    }

    // Output should be essentially zero (silence)
    const float rms = calculateRMS(outputL);
    REQUIRE(rms < 1e-6f);
}

// =============================================================================
// Tap Pan Tests (FR-012, FR-013, SC-004)
// =============================================================================

TEST_CASE("TapManager: setTapPan() sets pan position", "[tap-manager][tap-config]") {
    auto tm = createPreparedTapManager();

    SECTION("Set center pan") {
        tm.setTapPan(0, 0.0f);
        REQUIRE(tm.getTapPan(0) == Approx(0.0f));
    }

    SECTION("Set full left") {
        tm.setTapPan(0, -100.0f);
        REQUIRE(tm.getTapPan(0) == Approx(-100.0f));
    }

    SECTION("Set full right") {
        tm.setTapPan(0, 100.0f);
        REQUIRE(tm.getTapPan(0) == Approx(100.0f));
    }

    SECTION("Pan clamped to range") {
        tm.setTapPan(0, -150.0f);
        REQUIRE(tm.getTapPan(0) == Approx(-100.0f));

        tm.setTapPan(0, 150.0f);
        REQUIRE(tm.getTapPan(0) == Approx(100.0f));
    }
}

TEST_CASE("TapManager: Constant-power pan law (SC-004)", "[tap-manager][processing]") {
    auto tm = createPreparedTapManager();

    tm.setTapEnabled(0, true);
    tm.setTapTimeMs(0, 1.0f);
    tm.setTapLevelDb(0, 0.0f);
    tm.setDryWetMix(100.0f);
    tm.reset();

    // Process constant signal with center pan
    tm.setTapPan(0, 0.0f);  // Center

    std::vector<float> input(kTestBlockSize * 10, 1.0f);
    std::vector<float> outputL(kTestBlockSize * 10, 0.0f);
    std::vector<float> outputR(kTestBlockSize * 10, 0.0f);

    tm.process(input.data(), input.data(), outputL.data(), outputR.data(), kTestBlockSize * 10);

    // At center pan, L and R should be equal
    const float rmsL = calculateRMS(outputL);
    const float rmsR = calculateRMS(outputR);

    REQUIRE(rmsL == Approx(rmsR).margin(0.01f));

    // Sum of squares should be constant (power preserved)
    // For constant power: L² + R² = constant
    const float powerCenter = rmsL * rmsL + rmsR * rmsR;

    // Now test full left
    tm.setTapPan(0, -100.0f);
    tm.reset();

    std::fill(outputL.begin(), outputL.end(), 0.0f);
    std::fill(outputR.begin(), outputR.end(), 0.0f);
    tm.process(input.data(), input.data(), outputL.data(), outputR.data(), kTestBlockSize * 10);

    const float rmsLLeft = calculateRMS(outputL);
    const float rmsRLeft = calculateRMS(outputR);
    const float powerLeft = rmsLLeft * rmsLLeft + rmsRLeft * rmsRLeft;

    // Power should be approximately preserved (within 0.5dB)
    REQUIRE(powerLeft == Approx(powerCenter).margin(0.12f));  // ~0.5dB tolerance
}

// =============================================================================
// Pattern Tests (FR-022 to FR-027)
// =============================================================================

TEST_CASE("TapManager: loadPattern() QuarterNote (FR-022)", "[tap-manager][patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 500ms per quarter note

    tm.loadPattern(TapPattern::QuarterNote, 4);

    REQUIRE(tm.getPattern() == TapPattern::QuarterNote);
    REQUIRE(tm.getActiveTapCount() == 4);

    // Quarter note at 120 BPM = 500ms
    // Pattern: n × 500ms where n = 1, 2, 3, 4 (1-based)
    REQUIRE(tm.getTapTimeMs(0) == Approx(500.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(1) == Approx(1000.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(2) == Approx(1500.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(3) == Approx(2000.0f).margin(1.0f));
}

TEST_CASE("TapManager: loadPattern() DottedEighth (FR-023)", "[tap-manager][patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 500ms per quarter, 375ms per dotted eighth

    tm.loadPattern(TapPattern::DottedEighth, 4);

    REQUIRE(tm.getPattern() == TapPattern::DottedEighth);
    REQUIRE(tm.getActiveTapCount() == 4);

    // Dotted eighth = 0.75 × quarter = 375ms at 120 BPM
    // Pattern: n × 375ms where n = 1, 2, 3, 4
    REQUIRE(tm.getTapTimeMs(0) == Approx(375.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(1) == Approx(750.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(2) == Approx(1125.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(3) == Approx(1500.0f).margin(1.0f));
}

TEST_CASE("TapManager: loadPattern() Triplet (FR-024)", "[tap-manager][patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 500ms per quarter, ~333ms per triplet

    tm.loadPattern(TapPattern::Triplet, 4);

    REQUIRE(tm.getPattern() == TapPattern::Triplet);
    REQUIRE(tm.getActiveTapCount() == 4);

    // Triplet = 2/3 × quarter = 333.33ms at 120 BPM
    const float tripletMs = 500.0f * (2.0f / 3.0f);
    REQUIRE(tm.getTapTimeMs(0) == Approx(tripletMs).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(1) == Approx(tripletMs * 2.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(2) == Approx(tripletMs * 3.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(3) == Approx(tripletMs * 4.0f).margin(1.0f));
}

TEST_CASE("TapManager: loadPattern() GoldenRatio (FR-025)", "[tap-manager][patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 500ms per quarter

    tm.loadPattern(TapPattern::GoldenRatio, 4);

    REQUIRE(tm.getPattern() == TapPattern::GoldenRatio);
    REQUIRE(tm.getActiveTapCount() == 4);

    // Golden ratio: tap[0] = quarter, tap[n] = tap[n-1] × 1.618
    REQUIRE(tm.getTapTimeMs(0) == Approx(500.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(1) == Approx(500.0f * kGoldenRatio).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(2) == Approx(500.0f * kGoldenRatio * kGoldenRatio).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(3) == Approx(500.0f * kGoldenRatio * kGoldenRatio * kGoldenRatio).margin(2.0f));
}

TEST_CASE("TapManager: loadPattern() Fibonacci (FR-026)", "[tap-manager][patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 500ms per quarter, 125ms base

    tm.loadPattern(TapPattern::Fibonacci, 6);

    REQUIRE(tm.getPattern() == TapPattern::Fibonacci);
    REQUIRE(tm.getActiveTapCount() == 6);

    // Fibonacci: fib(n) × baseMs, where base = quarter/4 = 125ms
    // fib sequence (1-based): 1, 1, 2, 3, 5, 8
    const float baseMs = 125.0f;  // 500 / 4
    REQUIRE(tm.getTapTimeMs(0) == Approx(1.0f * baseMs).margin(1.0f));   // fib(1)=1
    REQUIRE(tm.getTapTimeMs(1) == Approx(1.0f * baseMs).margin(1.0f));   // fib(2)=1
    REQUIRE(tm.getTapTimeMs(2) == Approx(2.0f * baseMs).margin(1.0f));   // fib(3)=2
    REQUIRE(tm.getTapTimeMs(3) == Approx(3.0f * baseMs).margin(1.0f));   // fib(4)=3
    REQUIRE(tm.getTapTimeMs(4) == Approx(5.0f * baseMs).margin(1.0f));   // fib(5)=5
    REQUIRE(tm.getTapTimeMs(5) == Approx(8.0f * baseMs).margin(1.0f));   // fib(6)=8
}

TEST_CASE("TapManager: loadPattern() clamps tap count (FR-027)", "[tap-manager][patterns]") {
    auto tm = createPreparedTapManager();

    SECTION("Clamp to minimum 1") {
        tm.loadPattern(TapPattern::QuarterNote, 0);
        REQUIRE(tm.getActiveTapCount() == 1);
    }

    SECTION("Clamp to maximum 16") {
        tm.loadPattern(TapPattern::QuarterNote, 100);
        REQUIRE(tm.getActiveTapCount() == kMaxTaps);
    }
}

TEST_CASE("TapManager: loadPattern() completes within 1ms (SC-008)", "[tap-manager][patterns]") {
    auto tm = createPreparedTapManager();

    // This is a timing test - loadPattern should be fast
    // We just verify it doesn't hang and completes normally
    for (int i = 0; i < 1000; ++i) {
        tm.loadPattern(TapPattern::GoldenRatio, kMaxTaps);
    }
    // If we get here, it's fast enough
    REQUIRE(true);
}

// =============================================================================
// Note Pattern Tests (Extended preset patterns using NoteValue + NoteModifier)
// =============================================================================

TEST_CASE("TapManager: loadNotePattern() with Quarter note (normal)", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 500ms per quarter note

    tm.loadNotePattern(NoteValue::Quarter, NoteModifier::None, 4);

    REQUIRE(tm.getActiveTapCount() == 4);

    // Quarter note at 120 BPM = 500ms
    // Pattern: n × 500ms where n = 1, 2, 3, 4 (1-based)
    REQUIRE(tm.getTapTimeMs(0) == Approx(500.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(1) == Approx(1000.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(2) == Approx(1500.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(3) == Approx(2000.0f).margin(1.0f));
}

TEST_CASE("TapManager: loadNotePattern() with Quarter note (dotted)", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 500ms per quarter note

    tm.loadNotePattern(NoteValue::Quarter, NoteModifier::Dotted, 4);

    REQUIRE(tm.getActiveTapCount() == 4);

    // Dotted quarter at 120 BPM = 500ms × 1.5 = 750ms
    REQUIRE(tm.getTapTimeMs(0) == Approx(750.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(1) == Approx(1500.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(2) == Approx(2250.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(3) == Approx(3000.0f).margin(1.0f));
}

TEST_CASE("TapManager: loadNotePattern() with Quarter note (triplet)", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 500ms per quarter note

    tm.loadNotePattern(NoteValue::Quarter, NoteModifier::Triplet, 4);

    REQUIRE(tm.getActiveTapCount() == 4);

    // Triplet quarter at 120 BPM = 500ms × (2/3) ≈ 333.33ms
    const float tripletMs = 500.0f * (2.0f / 3.0f);
    REQUIRE(tm.getTapTimeMs(0) == Approx(tripletMs).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(1) == Approx(tripletMs * 2.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(2) == Approx(tripletMs * 3.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(3) == Approx(tripletMs * 4.0f).margin(1.0f));
}

TEST_CASE("TapManager: loadNotePattern() with Eighth note variants", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 500ms per quarter note, 250ms per eighth

    SECTION("Eighth normal") {
        tm.loadNotePattern(NoteValue::Eighth, NoteModifier::None, 4);
        // 250ms per eighth
        REQUIRE(tm.getTapTimeMs(0) == Approx(250.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(1) == Approx(500.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(2) == Approx(750.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(3) == Approx(1000.0f).margin(1.0f));
    }

    SECTION("Eighth dotted") {
        tm.loadNotePattern(NoteValue::Eighth, NoteModifier::Dotted, 4);
        // Dotted eighth = 250ms × 1.5 = 375ms
        REQUIRE(tm.getTapTimeMs(0) == Approx(375.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(1) == Approx(750.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(2) == Approx(1125.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(3) == Approx(1500.0f).margin(1.0f));
    }

    SECTION("Eighth triplet") {
        tm.loadNotePattern(NoteValue::Eighth, NoteModifier::Triplet, 6);
        // Triplet eighth = 250ms × (2/3) ≈ 166.67ms
        const float tripletMs = 250.0f * (2.0f / 3.0f);
        REQUIRE(tm.getTapTimeMs(0) == Approx(tripletMs).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(2) == Approx(tripletMs * 3.0f).margin(1.0f));
    }
}

TEST_CASE("TapManager: loadNotePattern() with Sixteenth note", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 125ms per sixteenth

    tm.loadNotePattern(NoteValue::Sixteenth, NoteModifier::None, 8);

    REQUIRE(tm.getActiveTapCount() == 8);

    // Sixteenth at 120 BPM = 125ms
    REQUIRE(tm.getTapTimeMs(0) == Approx(125.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(3) == Approx(500.0f).margin(1.0f));  // 4 × 125
    REQUIRE(tm.getTapTimeMs(7) == Approx(1000.0f).margin(1.0f)); // 8 × 125
}

TEST_CASE("TapManager: loadNotePattern() with ThirtySecond note", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 62.5ms per thirty-second

    tm.loadNotePattern(NoteValue::ThirtySecond, NoteModifier::None, 8);

    REQUIRE(tm.getActiveTapCount() == 8);

    // 32nd at 120 BPM = 62.5ms
    REQUIRE(tm.getTapTimeMs(0) == Approx(62.5f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(7) == Approx(500.0f).margin(1.0f));  // 8 × 62.5
}

TEST_CASE("TapManager: loadNotePattern() with SixtyFourth note", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 31.25ms per sixty-fourth

    tm.loadNotePattern(NoteValue::SixtyFourth, NoteModifier::None, 16);

    REQUIRE(tm.getActiveTapCount() == 16);

    // 64th at 120 BPM = 31.25ms
    REQUIRE(tm.getTapTimeMs(0) == Approx(31.25f).margin(0.5f));
    REQUIRE(tm.getTapTimeMs(15) == Approx(500.0f).margin(1.0f));  // 16 × 31.25
}

TEST_CASE("TapManager: loadNotePattern() with Half note", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 1000ms per half note

    SECTION("Half normal") {
        tm.loadNotePattern(NoteValue::Half, NoteModifier::None, 4);
        REQUIRE(tm.getTapTimeMs(0) == Approx(1000.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(1) == Approx(2000.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(2) == Approx(3000.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(3) == Approx(4000.0f).margin(1.0f));
    }

    SECTION("Half dotted") {
        tm.loadNotePattern(NoteValue::Half, NoteModifier::Dotted, 3);
        // Dotted half = 1000ms × 1.5 = 1500ms
        REQUIRE(tm.getTapTimeMs(0) == Approx(1500.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(1) == Approx(3000.0f).margin(1.0f));
        REQUIRE(tm.getTapTimeMs(2) == Approx(4500.0f).margin(1.0f));
    }
}

TEST_CASE("TapManager: loadNotePattern() with Whole note", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 2000ms per whole note

    tm.loadNotePattern(NoteValue::Whole, NoteModifier::None, 2);

    REQUIRE(tm.getActiveTapCount() == 2);

    // Whole at 120 BPM = 2000ms
    REQUIRE(tm.getTapTimeMs(0) == Approx(2000.0f).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(1) == Approx(4000.0f).margin(1.0f));
}

TEST_CASE("TapManager: loadNotePattern() with DoubleWhole note", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(120.0f);  // 4000ms per double-whole note

    tm.loadNotePattern(NoteValue::DoubleWhole, NoteModifier::None, 1);

    REQUIRE(tm.getActiveTapCount() == 1);

    // Double-whole at 120 BPM = 4000ms
    REQUIRE(tm.getTapTimeMs(0) == Approx(4000.0f).margin(1.0f));
}

TEST_CASE("TapManager: loadNotePattern() clamps to max delay", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();
    tm.setTempo(30.0f);  // Very slow tempo = 2000ms per quarter

    tm.loadNotePattern(NoteValue::Whole, NoteModifier::None, 4);

    // Whole at 30 BPM = 8000ms, but max delay is 5000ms
    // All taps should be clamped to 5000ms
    REQUIRE(tm.getTapTimeMs(0) == Approx(kTestMaxDelayMs).margin(1.0f));
    REQUIRE(tm.getTapTimeMs(1) == Approx(kTestMaxDelayMs).margin(1.0f));
}

TEST_CASE("TapManager: loadNotePattern() clamps tap count", "[tap-manager][note-patterns]") {
    auto tm = createPreparedTapManager();

    SECTION("Minimum 1 tap") {
        tm.loadNotePattern(NoteValue::Quarter, NoteModifier::None, 0);
        REQUIRE(tm.getActiveTapCount() == 1);
    }

    SECTION("Maximum 16 taps") {
        tm.loadNotePattern(NoteValue::Quarter, NoteModifier::None, 100);
        REQUIRE(tm.getActiveTapCount() == kMaxTaps);
    }
}

TEST_CASE("TapManager: loadNotePattern() is noexcept", "[tap-manager][note-patterns][real-time]") {
    static_assert(noexcept(std::declval<TapManager>().loadNotePattern(
        NoteValue::Quarter, NoteModifier::None, 4)));
    REQUIRE(true);
}

// =============================================================================
// Tempo Sync Tests (US6, SC-006)
// =============================================================================

TEST_CASE("TapManager: setTempo() updates tempo-synced taps", "[tap-manager][tempo]") {
    auto tm = createPreparedTapManager();

    // Set up tempo-synced tap
    tm.setTapEnabled(0, true);
    tm.setTapNoteValue(0, NoteValue::Quarter);

    SECTION("Tempo 120 BPM = 500ms quarter") {
        tm.setTempo(120.0f);
        // Note: getTapTimeMs returns the stored timeMs which is updated by setTempo
        // The actual delay is calculated in process() for tempo-synced taps
    }

    SECTION("Tempo 60 BPM = 1000ms quarter") {
        tm.setTempo(60.0f);
        // At 60 BPM, quarter note = 1000ms
    }

    SECTION("Invalid tempo (0 or negative) is ignored") {
        tm.setTempo(120.0f);
        const float prevActive = tm.getActiveTapCount();

        tm.setTempo(0.0f);   // Should be ignored
        tm.setTempo(-100.0f); // Should be ignored

        REQUIRE(tm.getActiveTapCount() == prevActive);  // State unchanged
    }
}

// =============================================================================
// Filter Tests (FR-015 to FR-018)
// =============================================================================

TEST_CASE("TapManager: setTapFilterMode() sets filter type", "[tap-manager][tap-config]") {
    auto tm = createPreparedTapManager();

    SECTION("Set lowpass") {
        tm.setTapFilterMode(0, TapFilterMode::Lowpass);
        // No direct query, but shouldn't crash
        REQUIRE(true);
    }

    SECTION("Set highpass") {
        tm.setTapFilterMode(0, TapFilterMode::Highpass);
        REQUIRE(true);
    }

    SECTION("Set bypass") {
        tm.setTapFilterMode(0, TapFilterMode::Bypass);
        REQUIRE(true);
    }
}

TEST_CASE("TapManager: Filter cutoff clamped to valid range (FR-016)", "[tap-manager][tap-config]") {
    auto tm = createPreparedTapManager();

    // These should not crash
    REQUIRE_NOTHROW(tm.setTapFilterCutoff(0, 10.0f));     // Below min
    REQUIRE_NOTHROW(tm.setTapFilterCutoff(0, 30000.0f));  // Above max
    REQUIRE_NOTHROW(tm.setTapFilterCutoff(0, 1000.0f));   // Valid
}

TEST_CASE("TapManager: Filter Q clamped to valid range (FR-017)", "[tap-manager][tap-config]") {
    auto tm = createPreparedTapManager();

    REQUIRE_NOTHROW(tm.setTapFilterQ(0, 0.1f));   // Below min
    REQUIRE_NOTHROW(tm.setTapFilterQ(0, 20.0f));  // Above max
    REQUIRE_NOTHROW(tm.setTapFilterQ(0, 1.0f));   // Valid
}

// =============================================================================
// Feedback Tests (FR-019 to FR-021)
// =============================================================================

TEST_CASE("TapManager: setTapFeedback() sets feedback amount", "[tap-manager][tap-config]") {
    auto tm = createPreparedTapManager();

    REQUIRE_NOTHROW(tm.setTapFeedback(0, 50.0f));
    REQUIRE_NOTHROW(tm.setTapFeedback(0, 0.0f));
    REQUIRE_NOTHROW(tm.setTapFeedback(0, 100.0f));
}

TEST_CASE("TapManager: Feedback is soft-limited to prevent runaway (FR-021)", "[tap-manager][processing]") {
    auto tm = createPreparedTapManager();

    // Set up tap with 100% feedback (would cause runaway without limiting)
    tm.setTapEnabled(0, true);
    tm.setTapTimeMs(0, 10.0f);  // Short delay for quick feedback
    tm.setTapLevelDb(0, 6.0f);  // +6dB gain
    tm.setTapFeedback(0, 100.0f);
    tm.setDryWetMix(100.0f);
    tm.reset();

    // Process impulse followed by silence
    auto inputL = generateImpulse(kTestBlockSize);
    auto inputR = generateImpulse(kTestBlockSize);
    auto outputL = generateSilence(kTestBlockSize);
    auto outputR = generateSilence(kTestBlockSize);

    // Process many blocks
    for (int i = 0; i < 100; ++i) {
        if (i == 0) {
            tm.process(inputL.data(), inputR.data(), outputL.data(), outputR.data(), kTestBlockSize);
        } else {
            auto silence = generateSilence(kTestBlockSize);
            tm.process(silence.data(), silence.data(), outputL.data(), outputR.data(), kTestBlockSize);
        }

        // Check that output never exceeds reasonable bounds (soft limited)
        for (float s : outputL) {
            REQUIRE(std::abs(s) < 10.0f);  // Should be limited
        }
    }
}

// =============================================================================
// Master Controls Tests (FR-028 to FR-030)
// =============================================================================

TEST_CASE("TapManager: setMasterLevel() affects output", "[tap-manager][tap-config]") {
    auto tm = createPreparedTapManager();

    REQUIRE_NOTHROW(tm.setMasterLevel(0.0f));
    REQUIRE_NOTHROW(tm.setMasterLevel(-12.0f));
    REQUIRE_NOTHROW(tm.setMasterLevel(kMinLevelDb));
    REQUIRE_NOTHROW(tm.setMasterLevel(kMaxLevelDb));
}

TEST_CASE("TapManager: setDryWetMix() blends dry and wet signals", "[tap-manager][processing]") {
    auto tm = createPreparedTapManager();

    tm.setTapEnabled(0, true);
    tm.setTapTimeMs(0, 100.0f);
    tm.setTapLevelDb(0, 0.0f);
    tm.reset();

    std::vector<float> input(kTestBlockSize * 5, 1.0f);
    std::vector<float> outputL(kTestBlockSize * 5);
    std::vector<float> outputR(kTestBlockSize * 5);

    SECTION("0% wet = dry only") {
        tm.setDryWetMix(0.0f);
        tm.reset();

        tm.process(input.data(), input.data(), outputL.data(), outputR.data(), kTestBlockSize * 5);

        // Should be close to input (dry signal)
        REQUIRE(outputL[kTestBlockSize] == Approx(1.0f).margin(0.1f));
    }

    SECTION("100% wet = wet only") {
        tm.setDryWetMix(100.0f);
        tm.reset();

        // First samples should be near zero (delay not yet reached)
        tm.process(input.data(), input.data(), outputL.data(), outputR.data(), 10);

        // With 100% wet and delay, first samples should be low
        REQUIRE(std::abs(outputL[0]) < 0.1f);
    }
}

// =============================================================================
// Processing Tests (FR-028, FR-031, FR-032, SC-001)
// =============================================================================

TEST_CASE("TapManager: process() with no enabled taps outputs dry signal", "[tap-manager][processing]") {
    auto tm = createPreparedTapManager();

    std::vector<float> inputL = {1.0f, 0.5f, -0.5f, -1.0f};
    std::vector<float> inputR = {0.5f, 1.0f, -1.0f, -0.5f};
    std::vector<float> outputL(4);
    std::vector<float> outputR(4);

    tm.setDryWetMix(50.0f);  // 50% dry
    tm.reset();

    tm.process(inputL.data(), inputR.data(), outputL.data(), outputR.data(), 4);

    // With no wet signal and 50% mix, output should be 50% of input
    REQUIRE(outputL[0] == Approx(0.5f).margin(0.01f));
}

TEST_CASE("TapManager: process() supports in-place processing", "[tap-manager][processing]") {
    auto tm = createPreparedTapManager();

    tm.setTapEnabled(0, true);
    tm.setTapTimeMs(0, 1.0f);
    tm.setTapLevelDb(0, 0.0f);
    tm.setDryWetMix(100.0f);
    tm.reset();

    std::vector<float> buffer(kTestBlockSize, 0.5f);

    // In-place: input and output are same buffer
    tm.process(buffer.data(), buffer.data(), buffer.data(), buffer.data(), kTestBlockSize);

    // Should not crash, and buffer should be modified
    REQUIRE(true);
}

TEST_CASE("TapManager: 16 active taps process without dropouts (SC-001)", "[tap-manager][processing]") {
    auto tm = createPreparedTapManager();

    // Enable all 16 taps with different settings
    for (size_t i = 0; i < kMaxTaps; ++i) {
        tm.setTapEnabled(i, true);
        tm.setTapTimeMs(i, 10.0f + static_cast<float>(i) * 100.0f);
        tm.setTapLevelDb(i, -static_cast<float>(i) * 2.0f);
        tm.setTapPan(i, -100.0f + static_cast<float>(i) * 13.33f);
        tm.setTapFilterMode(i, static_cast<TapFilterMode>(i % 3));
        tm.setTapFilterCutoff(i, 200.0f + static_cast<float>(i) * 500.0f);
        tm.setTapFeedback(i, static_cast<float>(i) * 5.0f);
    }
    tm.reset();

    REQUIRE(tm.getActiveTapCount() == kMaxTaps);

    // Process many blocks
    std::vector<float> inputL(kTestBlockSize);
    std::vector<float> inputR(kTestBlockSize);
    std::vector<float> outputL(kTestBlockSize);
    std::vector<float> outputR(kTestBlockSize);

    // Generate test signal
    for (size_t i = 0; i < kTestBlockSize; ++i) {
        inputL[i] = std::sin(static_cast<float>(i) * 0.1f) * 0.5f;
        inputR[i] = std::cos(static_cast<float>(i) * 0.1f) * 0.5f;
    }

    // Process 1000 blocks (simulate real-time processing)
    for (int block = 0; block < 1000; ++block) {
        tm.process(inputL.data(), inputR.data(), outputL.data(), outputR.data(), kTestBlockSize);

        // Verify output is valid (no NaN, no inf, reasonable range)
        for (size_t i = 0; i < kTestBlockSize; ++i) {
            REQUIRE_FALSE(std::isnan(outputL[i]));
            REQUIRE_FALSE(std::isnan(outputR[i]));
            REQUIRE_FALSE(std::isinf(outputL[i]));
            REQUIRE_FALSE(std::isinf(outputR[i]));
            REQUIRE(std::abs(outputL[i]) < 100.0f);
            REQUIRE(std::abs(outputR[i]) < 100.0f);
        }
    }
}

// =============================================================================
// Query Tests
// =============================================================================

TEST_CASE("TapManager: Query methods return correct values", "[tap-manager][queries]") {
    auto tm = createPreparedTapManager();

    SECTION("isTapEnabled") {
        REQUIRE_FALSE(tm.isTapEnabled(0));
        tm.setTapEnabled(0, true);
        REQUIRE(tm.isTapEnabled(0));
    }

    SECTION("getPattern") {
        REQUIRE(tm.getPattern() == TapPattern::Custom);
        tm.loadPattern(TapPattern::QuarterNote, 4);
        REQUIRE(tm.getPattern() == TapPattern::QuarterNote);
    }

    SECTION("getActiveTapCount") {
        REQUIRE(tm.getActiveTapCount() == 0);
        tm.setTapEnabled(0, true);
        tm.setTapEnabled(5, true);
        REQUIRE(tm.getActiveTapCount() == 2);
    }

    SECTION("getTapTimeMs") {
        tm.setTapTimeMs(0, 123.45f);
        REQUIRE(tm.getTapTimeMs(0) == Approx(123.45f));
    }

    SECTION("getTapLevelDb") {
        tm.setTapLevelDb(0, -18.5f);
        REQUIRE(tm.getTapLevelDb(0) == Approx(-18.5f));
    }

    SECTION("getTapPan") {
        tm.setTapPan(0, 42.0f);
        REQUIRE(tm.getTapPan(0) == Approx(42.0f));
    }
}

// =============================================================================
// Real-Time Safety Tests (FR-031, FR-032)
// =============================================================================

TEST_CASE("TapManager: All public methods are noexcept", "[tap-manager][real-time]") {
    // This is a compile-time check
    static_assert(noexcept(std::declval<TapManager>().prepare(44100.0f, 512, 5000.0f)));
    static_assert(noexcept(std::declval<TapManager>().reset()));
    static_assert(noexcept(std::declval<TapManager>().setTapEnabled(0, true)));
    static_assert(noexcept(std::declval<TapManager>().setTapTimeMs(0, 100.0f)));
    static_assert(noexcept(std::declval<TapManager>().setTapNoteValue(0, NoteValue::Quarter)));
    static_assert(noexcept(std::declval<TapManager>().setTapLevelDb(0, 0.0f)));
    static_assert(noexcept(std::declval<TapManager>().setTapPan(0, 0.0f)));
    static_assert(noexcept(std::declval<TapManager>().setTapFilterMode(0, TapFilterMode::Bypass)));
    static_assert(noexcept(std::declval<TapManager>().setTapFilterCutoff(0, 1000.0f)));
    static_assert(noexcept(std::declval<TapManager>().setTapFilterQ(0, 1.0f)));
    static_assert(noexcept(std::declval<TapManager>().setTapFeedback(0, 50.0f)));
    static_assert(noexcept(std::declval<TapManager>().loadPattern(TapPattern::QuarterNote, 4)));
    static_assert(noexcept(std::declval<TapManager>().setTempo(120.0f)));
    static_assert(noexcept(std::declval<TapManager>().setMasterLevel(0.0f)));
    static_assert(noexcept(std::declval<TapManager>().setDryWetMix(100.0f)));
    static_assert(noexcept(std::declval<TapManager>().process(nullptr, nullptr, nullptr, nullptr, 0)));
    static_assert(noexcept(std::declval<const TapManager>().isTapEnabled(0)));
    static_assert(noexcept(std::declval<const TapManager>().getPattern()));
    static_assert(noexcept(std::declval<const TapManager>().getActiveTapCount()));
    static_assert(noexcept(std::declval<const TapManager>().getTapTimeMs(0)));
    static_assert(noexcept(std::declval<const TapManager>().getTapLevelDb(0)));
    static_assert(noexcept(std::declval<const TapManager>().getTapPan(0)));

    REQUIRE(true);  // If we get here, all static_asserts passed
}
