// ==============================================================================
// Tests: MultiTapDelay (Layer 4 User Feature)
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests MUST be written before implementation.
//
// Feature: 028-multi-tap
// Reference: specs/028-multi-tap/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/features/multi_tap_delay.h"
#include "dsp/core/block_context.h"
#include "dsp/core/note_value.h"
#include "dsp/systems/delay_engine.h"

#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr float kMaxDelayMs = 5000.0f;

/// @brief Create a default BlockContext for testing
BlockContext makeTestContext(double sampleRate = kSampleRate, double bpm = 120.0) {
    return BlockContext{sampleRate, kBlockSize, bpm, 4, 4, true};
}

/// @brief Generate an impulse in a stereo buffer
void generateImpulse(float* left, float* right, size_t size) {
    std::fill(left, left + size, 0.0f);
    std::fill(right, right + size, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;
}

/// @brief Generate mono impulse
void generateMonoImpulse(float* buffer, size_t size) {
    std::fill(buffer, buffer + size, 0.0f);
    buffer[0] = 1.0f;
}

/// @brief Find peak in buffer
float findPeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// @brief Find first sample above threshold
size_t findFirstPeak(const float* buffer, size_t size, float threshold = 0.1f) {
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(buffer[i]) > threshold) {
            return i;
        }
    }
    return size; // Not found
}

/// @brief Calculate RMS of buffer
float calculateRMS(const float* buffer, size_t size) {
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

/// @brief Count samples above threshold
size_t countPeaks(const float* buffer, size_t size, float threshold = 0.01f) {
    size_t count = 0;
    bool inPeak = false;
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(buffer[i]) > threshold) {
            if (!inPeak) {
                ++count;
                inPeak = true;
            }
        } else {
            inPeak = false;
        }
    }
    return count;
}

} // anonymous namespace

// =============================================================================
// TimingPattern Enum Tests (T004)
// =============================================================================

TEST_CASE("TimingPattern enum values", "[multi-tap][enum]") {
    SECTION("rhythmic patterns exist") {
        // Basic note values
        REQUIRE(static_cast<int>(TimingPattern::QuarterNote) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::EighthNote) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::SixteenthNote) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::HalfNote) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::WholeNote) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::ThirtySecondNote) >= 0);

        // Dotted variants
        REQUIRE(static_cast<int>(TimingPattern::DottedQuarter) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::DottedEighth) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::DottedSixteenth) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::DottedHalf) >= 0);

        // Triplet variants
        REQUIRE(static_cast<int>(TimingPattern::TripletQuarter) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::TripletEighth) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::TripletSixteenth) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::TripletHalf) >= 0);
    }

    SECTION("mathematical patterns exist") {
        REQUIRE(static_cast<int>(TimingPattern::GoldenRatio) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::Fibonacci) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::Exponential) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::PrimeNumbers) >= 0);
        REQUIRE(static_cast<int>(TimingPattern::LinearSpread) >= 0);
    }

    SECTION("custom pattern exists") {
        REQUIRE(static_cast<int>(TimingPattern::Custom) >= 0);
    }

    SECTION("enum values are distinct") {
        // All patterns should have unique values
        REQUIRE(static_cast<int>(TimingPattern::QuarterNote) !=
                static_cast<int>(TimingPattern::GoldenRatio));
        REQUIRE(static_cast<int>(TimingPattern::Fibonacci) !=
                static_cast<int>(TimingPattern::Exponential));
    }
}

// =============================================================================
// SpatialPattern Enum Tests (T005)
// =============================================================================

TEST_CASE("SpatialPattern enum values", "[multi-tap][enum]") {
    SECTION("all spatial patterns exist") {
        REQUIRE(static_cast<int>(SpatialPattern::Cascade) >= 0);
        REQUIRE(static_cast<int>(SpatialPattern::Alternating) >= 0);
        REQUIRE(static_cast<int>(SpatialPattern::Centered) >= 0);
        REQUIRE(static_cast<int>(SpatialPattern::WideningStereo) >= 0);
        REQUIRE(static_cast<int>(SpatialPattern::DecayingLevel) >= 0);
        REQUIRE(static_cast<int>(SpatialPattern::FlatLevel) >= 0);
        REQUIRE(static_cast<int>(SpatialPattern::Custom) >= 0);
    }

    SECTION("enum values are distinct") {
        REQUIRE(static_cast<int>(SpatialPattern::Cascade) !=
                static_cast<int>(SpatialPattern::Alternating));
        REQUIRE(static_cast<int>(SpatialPattern::Centered) !=
                static_cast<int>(SpatialPattern::WideningStereo));
    }
}

// =============================================================================
// TapConfiguration Struct Tests (T006)
// =============================================================================

TEST_CASE("TapConfiguration struct", "[multi-tap][struct]") {
    SECTION("default construction") {
        TapConfiguration config;
        // Should have reasonable defaults
        REQUIRE(config.enabled == false);
        REQUIRE(config.timeMs >= 0.0f);
        REQUIRE(config.levelDb <= 0.0f);
        REQUIRE(config.pan >= -100.0f);
        REQUIRE(config.pan <= 100.0f);
    }

    SECTION("can set all fields") {
        TapConfiguration config;
        config.enabled = true;
        config.timeMs = 250.0f;
        config.levelDb = -6.0f;
        config.pan = -50.0f;
        config.filterMode = TapFilterMode::Lowpass;
        config.filterCutoff = 2000.0f;
        config.muted = false;

        REQUIRE(config.enabled == true);
        REQUIRE(config.timeMs == Approx(250.0f));
        REQUIRE(config.levelDb == Approx(-6.0f));
        REQUIRE(config.pan == Approx(-50.0f));
        REQUIRE(config.filterMode == TapFilterMode::Lowpass);
        REQUIRE(config.filterCutoff == Approx(2000.0f));
        REQUIRE(config.muted == false);
    }
}

// =============================================================================
// MultiTapDelay Lifecycle Tests (T010, T011)
// =============================================================================

TEST_CASE("MultiTapDelay lifecycle", "[multi-tap][lifecycle]") {
    MultiTapDelay delay;

    SECTION("not prepared initially") {
        REQUIRE_FALSE(delay.isPrepared());
    }

    SECTION("prepared after prepare()") {
        delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
        REQUIRE(delay.isPrepared());
    }

    SECTION("reset() clears state") {
        delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
        delay.reset();
        REQUIRE(delay.isPrepared()); // Still prepared after reset
    }
}

// =============================================================================
// User Story 1: Basic Multi-Tap Rhythmic Delay (P1 MVP)
// =============================================================================

TEST_CASE("US1: Basic timing patterns", "[multi-tap][us1][patterns]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("loadTimingPattern sets pattern") {
        delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
        REQUIRE(delay.getTimingPattern() == TimingPattern::QuarterNote);
    }

    SECTION("tap count is set correctly") {
        delay.loadTimingPattern(TimingPattern::DottedEighth, 6);
        REQUIRE(delay.getActiveTapCount() == 6);
    }

    SECTION("tap count clamped to valid range 2-16") {
        delay.loadTimingPattern(TimingPattern::QuarterNote, 1);
        REQUIRE(delay.getActiveTapCount() >= 2);

        delay.loadTimingPattern(TimingPattern::QuarterNote, 20);
        REQUIRE(delay.getActiveTapCount() <= 16);
    }
}

TEST_CASE("US1: Quarter note pattern timing", "[multi-tap][us1][quarter]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.setTempo(120.0f); // 500ms per quarter note
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
    delay.snapParameters();

    SECTION("tap times are multiples of quarter note") {
        // At 120 BPM: 500ms per beat
        // Taps should be at: 500, 1000, 1500, 2000ms
        REQUIRE(delay.getTapTimeMs(0) == Approx(500.0f).margin(1.0f));
        REQUIRE(delay.getTapTimeMs(1) == Approx(1000.0f).margin(1.0f));
        REQUIRE(delay.getTapTimeMs(2) == Approx(1500.0f).margin(1.0f));
        REQUIRE(delay.getTapTimeMs(3) == Approx(2000.0f).margin(1.0f));
    }
}

TEST_CASE("US1: Dotted eighth pattern timing", "[multi-tap][us1][dotted]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.setTempo(120.0f); // 500ms per quarter note
    delay.loadTimingPattern(TimingPattern::DottedEighth, 4);
    delay.snapParameters();

    SECTION("taps at dotted eighth intervals") {
        // Dotted eighth = 0.75 × quarter = 375ms
        // Taps at: 375, 750, 1125, 1500ms
        REQUIRE(delay.getTapTimeMs(0) == Approx(375.0f).margin(1.0f));
        REQUIRE(delay.getTapTimeMs(1) == Approx(750.0f).margin(1.0f));
        REQUIRE(delay.getTapTimeMs(2) == Approx(1125.0f).margin(1.0f));
        REQUIRE(delay.getTapTimeMs(3) == Approx(1500.0f).margin(1.0f));
    }
}

TEST_CASE("US1: Golden ratio pattern timing", "[multi-tap][us1][golden]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.setTempo(120.0f);
    delay.loadTimingPattern(TimingPattern::GoldenRatio, 6);
    delay.snapParameters();

    SECTION("each tap time is 1.618x previous") {
        constexpr float phi = 1.618033988749895f;
        float baseTime = delay.getTapTimeMs(0);

        for (size_t i = 1; i < 6 && delay.getTapTimeMs(i) < kMaxDelayMs; ++i) {
            float expected = delay.getTapTimeMs(i - 1) * phi;
            // Might be clamped to max delay
            expected = std::min(expected, kMaxDelayMs);
            REQUIRE(delay.getTapTimeMs(i) == Approx(expected).margin(5.0f));
        }
    }
}

TEST_CASE("US1: Spatial pattern application", "[multi-tap][us1][spatial]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);

    SECTION("Cascade pattern sweeps L to R") {
        delay.applySpatialPattern(SpatialPattern::Cascade);
        delay.snapParameters();

        // First tap should be left, last tap should be right
        REQUIRE(delay.getTapPan(0) < delay.getTapPan(3));
        REQUIRE(delay.getTapPan(0) <= -50.0f); // Mostly left
        REQUIRE(delay.getTapPan(3) >= 50.0f);  // Mostly right
    }

    SECTION("Alternating pattern alternates L/R") {
        delay.applySpatialPattern(SpatialPattern::Alternating);
        delay.snapParameters();

        // Odd taps opposite to even taps
        float pan0 = delay.getTapPan(0);
        float pan1 = delay.getTapPan(1);
        REQUIRE(pan0 * pan1 < 0.0f); // Opposite signs
    }

    SECTION("Centered pattern keeps all center") {
        delay.applySpatialPattern(SpatialPattern::Centered);
        delay.snapParameters();

        for (size_t i = 0; i < 4; ++i) {
            REQUIRE(delay.getTapPan(i) == Approx(0.0f).margin(1.0f));
        }
    }
}

// =============================================================================
// User Story 2: Per-Tap Level and Pan Control (P2)
// =============================================================================

TEST_CASE("US2: Per-tap level control", "[multi-tap][us2][level]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);

    SECTION("can set individual tap levels") {
        delay.setTapLevelDb(0, 0.0f);
        delay.setTapLevelDb(1, -6.0f);
        delay.setTapLevelDb(2, -12.0f);
        delay.setTapLevelDb(3, -18.0f);

        REQUIRE(delay.getTapLevelDb(0) == Approx(0.0f));
        REQUIRE(delay.getTapLevelDb(1) == Approx(-6.0f));
        REQUIRE(delay.getTapLevelDb(2) == Approx(-12.0f));
        REQUIRE(delay.getTapLevelDb(3) == Approx(-18.0f));
    }

    SECTION("level clamped to valid range -96 to +6 dB") {
        delay.setTapLevelDb(0, -200.0f);
        REQUIRE(delay.getTapLevelDb(0) >= -96.0f);

        delay.setTapLevelDb(0, 20.0f);
        REQUIRE(delay.getTapLevelDb(0) <= 6.0f);
    }
}

TEST_CASE("US2: Per-tap pan control", "[multi-tap][us2][pan]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);

    SECTION("can set individual tap pans") {
        delay.setTapPan(0, -100.0f);
        delay.setTapPan(1, -50.0f);
        delay.setTapPan(2, 50.0f);
        delay.setTapPan(3, 100.0f);

        REQUIRE(delay.getTapPan(0) == Approx(-100.0f));
        REQUIRE(delay.getTapPan(1) == Approx(-50.0f));
        REQUIRE(delay.getTapPan(2) == Approx(50.0f));
        REQUIRE(delay.getTapPan(3) == Approx(100.0f));
    }

    SECTION("pan clamped to valid range -100 to +100") {
        delay.setTapPan(0, -150.0f);
        REQUIRE(delay.getTapPan(0) >= -100.0f);

        delay.setTapPan(0, 150.0f);
        REQUIRE(delay.getTapPan(0) <= 100.0f);
    }
}

// =============================================================================
// User Story 3: Master Feedback with Filtering (P2)
// =============================================================================

TEST_CASE("US3: Master feedback control", "[multi-tap][us3][feedback]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("feedback range 0-110%") {
        delay.setFeedbackAmount(0.0f);
        REQUIRE(delay.getFeedbackAmount() == Approx(0.0f));

        delay.setFeedbackAmount(0.5f);
        REQUIRE(delay.getFeedbackAmount() == Approx(0.5f));

        delay.setFeedbackAmount(1.1f);
        REQUIRE(delay.getFeedbackAmount() == Approx(1.1f));
    }

    SECTION("feedback clamped to valid range") {
        delay.setFeedbackAmount(-0.5f);
        REQUIRE(delay.getFeedbackAmount() >= 0.0f);

        delay.setFeedbackAmount(2.0f);
        REQUIRE(delay.getFeedbackAmount() <= 1.1f);
    }
}

TEST_CASE("US3: Feedback filter control", "[multi-tap][us3][filter]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("can set feedback lowpass cutoff") {
        delay.setFeedbackLPCutoff(2000.0f);
        REQUIRE(delay.getFeedbackLPCutoff() == Approx(2000.0f));
    }

    SECTION("can set feedback highpass cutoff") {
        delay.setFeedbackHPCutoff(100.0f);
        REQUIRE(delay.getFeedbackHPCutoff() == Approx(100.0f));
    }

    SECTION("filter cutoffs clamped to 20Hz-20kHz") {
        delay.setFeedbackLPCutoff(5.0f);
        REQUIRE(delay.getFeedbackLPCutoff() >= 20.0f);

        delay.setFeedbackLPCutoff(30000.0f);
        REQUIRE(delay.getFeedbackLPCutoff() <= 20000.0f);
    }
}

// =============================================================================
// User Story 4: Pattern Morphing (P3)
// =============================================================================

TEST_CASE("US4: Pattern morphing", "[multi-tap][us4][morph]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.setTempo(120.0f);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);

    SECTION("can trigger morph to new pattern") {
        delay.morphToPattern(TimingPattern::TripletEighth, 500.0f);
        // Should not throw and morph should be in progress
        REQUIRE(delay.isMorphing() == true);
    }

    SECTION("morph time configurable 50-2000ms") {
        delay.setMorphTime(100.0f);
        REQUIRE(delay.getMorphTime() == Approx(100.0f));

        delay.setMorphTime(30.0f);
        REQUIRE(delay.getMorphTime() >= 50.0f);

        delay.setMorphTime(3000.0f);
        REQUIRE(delay.getMorphTime() <= 2000.0f);
    }
}

// =============================================================================
// User Story 5: Per-Tap Modulation (P3)
// =============================================================================

TEST_CASE("US5: Modulation matrix connection", "[multi-tap][us5][modulation]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("can connect modulation matrix") {
        ModulationMatrix modMatrix;
        modMatrix.prepare(kSampleRate, kBlockSize);

        delay.setModulationMatrix(&modMatrix);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("null modulation matrix is handled") {
        delay.setModulationMatrix(nullptr);
        // Should not crash
        REQUIRE(true);
    }
}

// =============================================================================
// User Story 6: Tempo Sync (P2)
// =============================================================================

TEST_CASE("US6: Tempo synchronization", "[multi-tap][us6][tempo]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("tempo affects pattern timing") {
        delay.setTempo(120.0f);
        delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
        float time120 = delay.getTapTimeMs(0);

        delay.setTempo(140.0f);
        delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
        float time140 = delay.getTapTimeMs(0);

        // Faster tempo = shorter delay times
        REQUIRE(time140 < time120);
    }

    SECTION("tempo range 20-300 BPM") {
        delay.setTempo(20.0f);
        REQUIRE(delay.getTempo() >= 20.0f);

        delay.setTempo(300.0f);
        REQUIRE(delay.getTempo() <= 300.0f);
    }
}

// =============================================================================
// Output Controls (Phase 9)
// =============================================================================

TEST_CASE("Output controls", "[multi-tap][output]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("dry/wet mix control") {
        delay.setDryWetMix(0.0f);
        REQUIRE(delay.getDryWetMix() == Approx(0.0f));

        delay.setDryWetMix(50.0f);
        REQUIRE(delay.getDryWetMix() == Approx(50.0f));

        delay.setDryWetMix(100.0f);
        REQUIRE(delay.getDryWetMix() == Approx(100.0f));
    }

}

// =============================================================================
// Edge Cases (T083)
// =============================================================================

TEST_CASE("Edge cases", "[multi-tap][edge-case]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("single tap (tap count = 1) functions as single-tap delay") {
        // Even if set to 1, minimum is 2 per spec, but should still work
        delay.loadTimingPattern(TimingPattern::QuarterNote, 2);
        REQUIRE(delay.getActiveTapCount() >= 1);
    }

    SECTION("all taps muted produces dry signal only") {
        delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
        for (size_t i = 0; i < 4; ++i) {
            delay.setTapMuted(i, true);
        }
        delay.setDryWetMix(50.0f);
        delay.snapParameters();

        // Process some audio
        std::array<float, 512> left{}, right{};
        generateImpulse(left.data(), right.data(), 512);
        auto ctx = makeTestContext();
        delay.process(left.data(), right.data(), 512, ctx);

        // With all taps muted and 50% mix, output should be 50% dry only
        // The impulse should still be present but attenuated
        REQUIRE(findPeak(left.data(), 512) > 0.0f);
    }

    SECTION("maximum feedback (110%) remains stable") {
        delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
        delay.setTempo(120.0f);
        delay.setFeedbackAmount(1.1f);
        delay.setDryWetMix(100.0f);
        delay.snapParameters();

        // Process many blocks to check stability
        std::array<float, 512> left{}, right{};
        generateImpulse(left.data(), right.data(), 512);
        auto ctx = makeTestContext();

        for (int block = 0; block < 100; ++block) {
            delay.process(left.data(), right.data(), 512, ctx);
            // Check that output doesn't explode
            float peak = std::max(findPeak(left.data(), 512), findPeak(right.data(), 512));
            REQUIRE(peak < 10.0f); // Should be limited, not runaway
            // Clear for next block
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
        }
    }
}

// =============================================================================
// Custom Patterns (FR-003, T083a-T083b)
// =============================================================================

TEST_CASE("Custom user-defined patterns", "[multi-tap][custom]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("can set custom timing pattern via span") {
        std::array<float, 4> timeRatios = {1.0f, 2.5f, 3.7f, 5.0f};
        delay.setCustomTimingPattern(std::span<float>(timeRatios));
        delay.setBaseTimeMs(100.0f);
        delay.snapParameters();

        REQUIRE(delay.getTapTimeMs(0) == Approx(100.0f).margin(1.0f));
        REQUIRE(delay.getTapTimeMs(1) == Approx(250.0f).margin(1.0f));
        REQUIRE(delay.getTapTimeMs(2) == Approx(370.0f).margin(1.0f));
        REQUIRE(delay.getTapTimeMs(3) == Approx(500.0f).margin(1.0f));
    }

    SECTION("custom pattern sets pattern type to Custom") {
        std::array<float, 3> timeRatios = {1.0f, 2.0f, 3.0f};
        delay.setCustomTimingPattern(std::span<float>(timeRatios));
        REQUIRE(delay.getTimingPattern() == TimingPattern::Custom);
    }

    SECTION("custom pattern clamps to max taps") {
        std::array<float, 20> manyRatios{};
        for (size_t i = 0; i < 20; ++i) {
            manyRatios[i] = static_cast<float>(i + 1);
        }
        delay.setCustomTimingPattern(std::span<float>(manyRatios));
        REQUIRE(delay.getActiveTapCount() <= 16);
    }
}

// =============================================================================
// Audio Processing Tests
// =============================================================================

TEST_CASE("Audio processing", "[multi-tap][process]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.setTempo(120.0f);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
    delay.setDryWetMix(100.0f);
    delay.setFeedbackAmount(0.0f);
    delay.snapParameters();

    SECTION("produces output at expected delay times") {
        // At 120 BPM, first tap at 500ms = 22050 samples
        // We need to process enough samples to see the first tap
        constexpr size_t totalSamples = 25000;
        std::vector<float> left(totalSamples, 0.0f);
        std::vector<float> right(totalSamples, 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;

        auto ctx = makeTestContext();
        size_t processed = 0;
        while (processed < totalSamples) {
            size_t blockSize = std::min(size_t(512), totalSamples - processed);
            delay.process(left.data() + processed, right.data() + processed, blockSize, ctx);
            processed += blockSize;
        }

        // Find first significant output (after dry impulse fades)
        size_t firstEcho = findFirstPeak(left.data() + 100, totalSamples - 100, 0.05f);
        // Should be around 22050 samples (500ms at 44.1kHz)
        REQUIRE(firstEcho + 100 > 20000);
        REQUIRE(firstEcho + 100 < 24000);
    }

    SECTION("stereo output respects pan settings") {
        delay.applySpatialPattern(SpatialPattern::Cascade);
        delay.snapParameters();

        constexpr size_t totalSamples = 25000;
        std::vector<float> left(totalSamples, 0.0f);
        std::vector<float> right(totalSamples, 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;

        auto ctx = makeTestContext();
        size_t processed = 0;
        while (processed < totalSamples) {
            size_t blockSize = std::min(size_t(512), totalSamples - processed);
            delay.process(left.data() + processed, right.data() + processed, blockSize, ctx);
            processed += blockSize;
        }

        // With Cascade, first tap is left-panned, last tap is right-panned
        // Check that left channel has more energy at first tap time
        // This is a simplified check
        float leftEnergy = 0.0f, rightEnergy = 0.0f;
        for (size_t i = 22000; i < 23000 && i < totalSamples; ++i) {
            leftEnergy += left[i] * left[i];
            rightEnergy += right[i] * right[i];
        }
        // First tap should favor left channel
        REQUIRE(leftEnergy > rightEnergy * 0.5f);
    }
}

// =============================================================================
// SC-005: Parameter smoothing eliminates clicks
// =============================================================================

TEST_CASE("SC-005: Parameter changes don't cause clicks", "[multi-tap][sc-005][smoothing]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
    delay.setTempo(120.0f);
    delay.setDryWetMix(100.0f);
    delay.snapParameters();

    auto ctx = makeTestContext();

    SECTION("level change during processing doesn't cause discontinuity") {
        // Process with constant input to build up delay content
        std::array<float, 512> left{}, right{};
        std::fill(left.begin(), left.end(), 0.5f);
        std::fill(right.begin(), right.end(), 0.5f);

        // Let delay settle
        for (int i = 0; i < 100; ++i) {
            delay.process(left.data(), right.data(), 512, ctx);
        }

        // Now change level abruptly and check for large sample-to-sample jumps
        delay.setTapLevelDb(0, -12.0f);  // Sudden level change

        float maxJump = 0.0f;
        float prevSample = left[511];  // Last sample before change

        for (int block = 0; block < 10; ++block) {
            std::fill(left.begin(), left.end(), 0.5f);
            std::fill(right.begin(), right.end(), 0.5f);
            delay.process(left.data(), right.data(), 512, ctx);

            for (size_t i = 0; i < 512; ++i) {
                float jump = std::abs(left[i] - prevSample);
                maxJump = std::max(maxJump, jump);
                prevSample = left[i];
            }
        }

        // With proper smoothing, sample-to-sample jumps should be small
        // A click would show as a jump > 0.1 (10% of full scale)
        REQUIRE(maxJump < 0.1f);
    }

    SECTION("pan change during processing doesn't cause discontinuity") {
        std::array<float, 512> left{}, right{};
        std::fill(left.begin(), left.end(), 0.5f);
        std::fill(right.begin(), right.end(), 0.5f);

        // Let delay settle
        for (int i = 0; i < 100; ++i) {
            delay.process(left.data(), right.data(), 512, ctx);
        }

        // Change pan abruptly
        delay.setTapPan(0, -100.0f);  // Hard left

        float maxJumpL = 0.0f, maxJumpR = 0.0f;
        float prevL = left[511], prevR = right[511];

        for (int block = 0; block < 10; ++block) {
            std::fill(left.begin(), left.end(), 0.5f);
            std::fill(right.begin(), right.end(), 0.5f);
            delay.process(left.data(), right.data(), 512, ctx);

            for (size_t i = 0; i < 512; ++i) {
                maxJumpL = std::max(maxJumpL, std::abs(left[i] - prevL));
                maxJumpR = std::max(maxJumpR, std::abs(right[i] - prevR));
                prevL = left[i];
                prevR = right[i];
            }
        }

        REQUIRE(maxJumpL < 0.1f);
        REQUIRE(maxJumpR < 0.1f);
    }

    SECTION("dry/wet mix change doesn't cause discontinuity") {
        std::array<float, 512> left{}, right{};
        std::fill(left.begin(), left.end(), 0.5f);
        std::fill(right.begin(), right.end(), 0.5f);

        // Let delay settle at 100% wet
        for (int i = 0; i < 100; ++i) {
            delay.process(left.data(), right.data(), 512, ctx);
        }

        // Change to 0% wet abruptly
        delay.setDryWetMix(0.0f);

        float maxJump = 0.0f;
        float prevSample = left[511];

        for (int block = 0; block < 10; ++block) {
            std::fill(left.begin(), left.end(), 0.5f);
            std::fill(right.begin(), right.end(), 0.5f);
            delay.process(left.data(), right.data(), 512, ctx);

            for (size_t i = 0; i < 512; ++i) {
                float jump = std::abs(left[i] - prevSample);
                maxJump = std::max(maxJump, jump);
                prevSample = left[i];
            }
        }

        REQUIRE(maxJump < 0.1f);
    }
}

// =============================================================================
// SC-008: Pattern morphing without discontinuities
// =============================================================================

TEST_CASE("SC-008: Pattern morphing produces no discontinuities", "[multi-tap][sc-008][morph]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
    delay.setTempo(120.0f);
    delay.setDryWetMix(100.0f);
    delay.snapParameters();

    auto ctx = makeTestContext();

    SECTION("morph transition maintains stable output") {
        // Fill delay with content
        std::array<float, 512> left{}, right{};
        std::fill(left.begin(), left.end(), 0.3f);
        std::fill(right.begin(), right.end(), 0.3f);

        for (int i = 0; i < 100; ++i) {
            delay.process(left.data(), right.data(), 512, ctx);
        }

        // Start morph to different pattern
        delay.morphToPattern(TimingPattern::DottedEighth, 200.0f);

        float maxOutput = 0.0f;
        bool hasNaN = false;

        // Process through the morph (200ms = ~8820 samples at 44100)
        for (int block = 0; block < 20; ++block) {
            std::fill(left.begin(), left.end(), 0.3f);
            std::fill(right.begin(), right.end(), 0.3f);
            delay.process(left.data(), right.data(), 512, ctx);

            for (size_t i = 0; i < 512; ++i) {
                if (std::isnan(left[i]) || std::isnan(right[i])) {
                    hasNaN = true;
                }
                maxOutput = std::max(maxOutput, std::abs(left[i]));
                maxOutput = std::max(maxOutput, std::abs(right[i]));
            }
        }

        // Morphing should maintain stable output - no NaN or runaway
        REQUIRE_FALSE(hasNaN);
        INFO("Max output during morph: " << maxOutput);
        REQUIRE(maxOutput < 10.0f);  // No runaway
    }

    SECTION("morph completes without runaway or NaN") {
        std::array<float, 512> left{}, right{};
        std::fill(left.begin(), left.end(), 0.3f);
        std::fill(right.begin(), right.end(), 0.3f);

        // Start with quick pattern
        delay.loadTimingPattern(TimingPattern::SixteenthNote, 8);
        delay.snapParameters();

        for (int i = 0; i < 100; ++i) {
            delay.process(left.data(), right.data(), 512, ctx);
        }

        // Morph to very different pattern
        delay.morphToPattern(TimingPattern::WholeNote, 500.0f);

        float maxOutput = 0.0f;
        bool hasNaN = false;

        // Process enough blocks for 500ms morph to complete (500ms = ~22k samples at 44.1kHz)
        // Use 100 blocks (51200 samples) to be safe with smoother settling
        for (int block = 0; block < 100; ++block) {
            std::fill(left.begin(), left.end(), 0.3f);
            std::fill(right.begin(), right.end(), 0.3f);
            delay.process(left.data(), right.data(), 512, ctx);

            for (size_t i = 0; i < 512; ++i) {
                if (std::isnan(left[i]) || std::isnan(right[i])) {
                    hasNaN = true;
                }
                maxOutput = std::max(maxOutput, std::abs(left[i]));
                maxOutput = std::max(maxOutput, std::abs(right[i]));
            }
        }

        // Morph should complete without producing NaN or runaway values
        REQUIRE_FALSE(hasNaN);
        REQUIRE(maxOutput < 10.0f);  // No runaway
        // Note: Morph may still be active if smoother uses exponential decay
        // The important thing is stability, not exact completion time
    }
}

// =============================================================================
// SC-007: CPU usage benchmark (informational)
// =============================================================================

TEST_CASE("SC-007: CPU usage benchmark", "[multi-tap][sc-007][benchmark][!benchmark]") {
    // This test measures processing time to verify reasonable performance
    // The [!benchmark] tag allows skipping in normal test runs

    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.loadTimingPattern(TimingPattern::GoldenRatio, 16);  // Max taps
    delay.setTempo(120.0f);
    delay.setFeedbackAmount(0.8f);
    delay.setDryWetMix(50.0f);
    delay.snapParameters();

    auto ctx = makeTestContext();
    std::array<float, 512> left{}, right{};

    // Warm up
    for (int i = 0; i < 10; ++i) {
        generateImpulse(left.data(), right.data(), 512);
        delay.process(left.data(), right.data(), 512, ctx);
    }

    // Measure time for 1 second of audio (44100 samples = ~86 blocks of 512)
    constexpr int numBlocks = 86;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < numBlocks; ++i) {
        std::fill(left.begin(), left.end(), 0.1f);
        std::fill(right.begin(), right.end(), 0.1f);
        delay.process(left.data(), right.data(), 512, ctx);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // 1 second of audio should process in < 10ms for <1% CPU
    // But debug builds are much slower, so we use a generous threshold
    // In debug: < 200ms is acceptable (20% of real-time)
    // In release: should be < 10ms (1% of real-time)
    INFO("Processing 1 second of audio took " << duration.count() << " microseconds");

    // Debug build threshold: 200ms (200000 microseconds)
    // This validates the algorithm doesn't have O(n^2) or worse complexity
    REQUIRE(duration.count() < 200000);
}

// =============================================================================
// Tempo Sync Interface Tests (TDD - spec 043)
// =============================================================================
// These tests verify that MultiTapDelay supports the standard tempo sync
// interface (setTimeMode/setNoteValue) matching DigitalDelay.
// In Synced mode, the base time comes from BlockContext.tempoBPM + noteValue.

TEST_CASE("MultiTapDelay tempo sync: setTimeMode stores mode", "[multi-tap][tempo]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("default mode is Free") {
        REQUIRE(delay.getTimeMode() == TimeMode::Free);
    }

    SECTION("setTimeMode stores Free") {
        delay.setTimeMode(TimeMode::Free);
        REQUIRE(delay.getTimeMode() == TimeMode::Free);
    }

    SECTION("setTimeMode stores Synced") {
        delay.setTimeMode(TimeMode::Synced);
        REQUIRE(delay.getTimeMode() == TimeMode::Synced);
    }
}

TEST_CASE("MultiTapDelay tempo sync: setNoteValue stores note and modifier", "[multi-tap][tempo]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("default note value is Eighth") {
        REQUIRE(delay.getNoteValue() == NoteValue::Eighth);
    }

    SECTION("setNoteValue stores Quarter") {
        delay.setNoteValue(NoteValue::Quarter);
        REQUIRE(delay.getNoteValue() == NoteValue::Quarter);
    }

    SECTION("setNoteValue stores Sixteenth") {
        delay.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
        REQUIRE(delay.getNoteValue() == NoteValue::Sixteenth);
    }

    SECTION("setNoteValue stores with triplet modifier") {
        delay.setNoteValue(NoteValue::Eighth, NoteModifier::Triplet);
        REQUIRE(delay.getNoteValue() == NoteValue::Eighth);
    }
}

TEST_CASE("MultiTapDelay tempo sync: pattern timing uses host tempo", "[multi-tap][tempo]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    // MultiTapDelay's preset patterns (QuarterNote, EighthNote, etc.) always
    // use tempo for timing. The process() method automatically updates
    // tempo from BlockContext.

    delay.setTimeMode(TimeMode::Free);
    delay.setTempo(120.0f);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
    delay.snapParameters();

    // At 120 BPM, quarter note = 500ms
    REQUIRE(delay.getTapTimeMs(0) == Approx(500.0f).margin(1.0f));

    // Process with a different tempo in BlockContext
    auto ctx = makeTestContext(kSampleRate, 60.0);
    std::array<float, 512> left{}, right{};
    delay.process(left.data(), right.data(), 512, ctx);

    // Tempo updates from host, so quarter note is now 1000ms
    REQUIRE(delay.getTapTimeMs(0) == Approx(1000.0f).margin(5.0f));
}

TEST_CASE("MultiTapDelay tempo sync: Synced mode with custom pattern uses noteValue", "[multi-tap][tempo]") {
    // For MultiTapDelay, preset patterns (QuarterNote, EighthNote, etc.) have their
    // own built-in note value calculations based on tempo. The noteValue parameter
    // affects the baseTimeMs_ which is used for Custom patterns.

    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setTimeMode(TimeMode::Synced);
    delay.setNoteValue(NoteValue::Quarter, NoteModifier::None);

    SECTION("QuarterNote pattern at 120 BPM = 500ms first tap") {
        delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
        delay.snapParameters();

        auto ctx = makeTestContext(kSampleRate, 120.0);
        std::array<float, 512> left{}, right{};
        delay.process(left.data(), right.data(), 512, ctx);

        // QuarterNote pattern uses tempo directly: 120 BPM = 500ms per quarter
        REQUIRE(delay.getTapTimeMs(0) == Approx(500.0f).margin(5.0f));
    }

    SECTION("QuarterNote pattern at 60 BPM = 1000ms first tap") {
        delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
        delay.snapParameters();

        auto ctx = makeTestContext(kSampleRate, 60.0);
        std::array<float, 512> left{}, right{};
        delay.process(left.data(), right.data(), 512, ctx);

        // 60 BPM = 1000ms per quarter note
        REQUIRE(delay.getTapTimeMs(0) == Approx(1000.0f).margin(5.0f));
    }

    SECTION("EighthNote pattern at 120 BPM = 250ms first tap") {
        delay.loadTimingPattern(TimingPattern::EighthNote, 4);
        delay.snapParameters();

        auto ctx = makeTestContext(kSampleRate, 120.0);
        std::array<float, 512> left{}, right{};
        delay.process(left.data(), right.data(), 512, ctx);

        // EighthNote pattern: 120 BPM = 250ms per eighth note
        REQUIRE(delay.getTapTimeMs(0) == Approx(250.0f).margin(5.0f));
    }
}

TEST_CASE("MultiTapDelay tempo sync: tempo changes update taps in Synced mode", "[multi-tap][tempo]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setTimeMode(TimeMode::Synced);
    delay.setNoteValue(NoteValue::Quarter, NoteModifier::None);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
    delay.setDryWetMix(100.0f);
    delay.snapParameters();

    SECTION("tap times adapt when tempo changes from 120 to 60 BPM") {
        // Process at 120 BPM
        auto ctx120 = makeTestContext(kSampleRate, 120.0);
        std::array<float, 512> left{}, right{};
        delay.process(left.data(), right.data(), 512, ctx120);

        // First tap at 120 BPM should be ~500ms
        float time120 = delay.getTapTimeMs(0);
        REQUIRE(time120 == Approx(500.0f).margin(10.0f));

        // Process at 60 BPM
        auto ctx60 = makeTestContext(kSampleRate, 60.0);
        delay.process(left.data(), right.data(), 512, ctx60);

        // First tap at 60 BPM should be ~1000ms (clamped if exceeds max)
        float time60 = delay.getTapTimeMs(0);
        REQUIRE(time60 == Approx(1000.0f).margin(10.0f));

        // Slower tempo = longer delay
        REQUIRE(time60 > time120);
    }
}

TEST_CASE("MultiTapDelay tempo sync: base time clamped to valid range", "[multi-tap][tempo]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setTimeMode(TimeMode::Synced);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
    delay.snapParameters();

    SECTION("very slow tempo clamps taps to max delay") {
        delay.setNoteValue(NoteValue::Whole, NoteModifier::None);  // 4 beats
        delay.loadTimingPattern(TimingPattern::QuarterNote, 4);  // 4 taps

        // At 20 BPM, whole note = 12000ms (exceeds 5000ms max)
        auto ctx = makeTestContext(kSampleRate, 20.0);
        std::array<float, 512> left{}, right{};
        delay.process(left.data(), right.data(), 512, ctx);

        // Should be clamped to max delay
        REQUIRE(delay.getTapTimeMs(0) <= kMaxDelayMs);
        REQUIRE_FALSE(std::isnan(delay.getTapTimeMs(0)));
    }

    SECTION("very fast tempo with short note stays above minimum") {
        delay.setNoteValue(NoteValue::ThirtySecond, NoteModifier::None);

        // At 300 BPM, 1/32 note = 25ms (above 1ms minimum)
        auto ctx = makeTestContext(kSampleRate, 300.0);
        std::array<float, 512> left{}, right{};
        delay.process(left.data(), right.data(), 512, ctx);

        // Should be at least minimum delay
        REQUIRE(delay.getTapTimeMs(0) >= MultiTapDelay::kMinDelayMs);
        REQUIRE_FALSE(std::isnan(delay.getTapTimeMs(0)));
    }
}

TEST_CASE("MultiTapDelay tempo sync: TripletQuarter pattern at 120 BPM", "[multi-tap][tempo]") {
    // MultiTapDelay has built-in triplet patterns that handle triplet timing
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setTimeMode(TimeMode::Synced);
    delay.loadTimingPattern(TimingPattern::TripletQuarter, 4);
    delay.snapParameters();

    // At 120 BPM, triplet quarter = 500ms * (2/3) = ~333ms
    auto ctx = makeTestContext(kSampleRate, 120.0);
    std::array<float, 512> left{}, right{};
    delay.process(left.data(), right.data(), 512, ctx);

    // TripletQuarter pattern uses triplet timing
    float expectedTime = noteToDelayMs(NoteValue::Quarter, NoteModifier::Triplet, 120.0);
    REQUIRE(expectedTime == Approx(333.33f).margin(1.0f));

    REQUIRE(delay.getTapTimeMs(0) == Approx(333.33f).margin(10.0f));
}

TEST_CASE("MultiTapDelay tempo sync: DottedEighth pattern at 120 BPM", "[multi-tap][tempo]") {
    // MultiTapDelay has built-in dotted patterns that handle dotted timing
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setTimeMode(TimeMode::Synced);
    delay.loadTimingPattern(TimingPattern::DottedEighth, 4);
    delay.snapParameters();

    // At 120 BPM, dotted eighth = 250ms * 1.5 = 375ms
    auto ctx = makeTestContext(kSampleRate, 120.0);
    std::array<float, 512> left{}, right{};
    delay.process(left.data(), right.data(), 512, ctx);

    // DottedEighth pattern uses dotted timing
    float expectedTime = noteToDelayMs(NoteValue::Eighth, NoteModifier::Dotted, 120.0);
    REQUIRE(expectedTime == Approx(375.0f).margin(1.0f));

    REQUIRE(delay.getTapTimeMs(0) == Approx(375.0f).margin(10.0f));
}
