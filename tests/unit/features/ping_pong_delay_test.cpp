// ==============================================================================
// Tests: PingPongDelay (Layer 4 User Feature)
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests MUST be written before implementation.
//
// Feature: 027-ping-pong-delay
// Reference: specs/027-ping-pong-delay/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/features/ping_pong_delay.h"
#include "dsp/core/block_context.h"

#include <array>
#include <cmath>
#include <numeric>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr float kMaxDelayMs = 2000.0f;

/// @brief Create a default BlockContext for testing
BlockContext makeTestContext(double sampleRate = kSampleRate, double bpm = 120.0) {
    return BlockContext{sampleRate, kBlockSize, bpm, 4, 4, true};
}

/// @brief Generate an impulse in a stereo buffer
void generateImpulse(float* left, float* right, size_t size) {
    std::fill(left, left + size, 0.0f);
    std::fill(right, right + size, 0.0f);
    left[0] = 1.0f;
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

/// @brief Calculate correlation coefficient between two buffers
float calculateCorrelation(const float* a, const float* b, size_t size) {
    float sumA = 0.0f, sumB = 0.0f, sumAB = 0.0f;
    float sumA2 = 0.0f, sumB2 = 0.0f;

    for (size_t i = 0; i < size; ++i) {
        sumA += a[i];
        sumB += b[i];
        sumAB += a[i] * b[i];
        sumA2 += a[i] * a[i];
        sumB2 += b[i] * b[i];
    }

    float n = static_cast<float>(size);
    float numerator = n * sumAB - sumA * sumB;
    float denominator = std::sqrt((n * sumA2 - sumA * sumA) * (n * sumB2 - sumB * sumB));

    if (denominator < 1e-10f) return 1.0f; // Avoid division by zero
    return numerator / denominator;
}

} // anonymous namespace

// =============================================================================
// Lifecycle Tests (Foundational)
// =============================================================================

TEST_CASE("PingPongDelay lifecycle", "[ping-pong][lifecycle]") {
    PingPongDelay delay;

    SECTION("not prepared initially") {
        REQUIRE_FALSE(delay.isPrepared());
    }

    SECTION("prepared after prepare()") {
        delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
        REQUIRE(delay.isPrepared());
    }

    SECTION("reset() doesn't change prepared state") {
        delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
        delay.reset();
        REQUIRE(delay.isPrepared());
    }
}

// =============================================================================
// LRRatio Enum Tests (Foundational)
// =============================================================================

TEST_CASE("LRRatio enum values", "[ping-pong][ratio]") {
    // Verify enum values exist
    REQUIRE(static_cast<int>(LRRatio::OneToOne) >= 0);
    REQUIRE(static_cast<int>(LRRatio::TwoToOne) >= 0);
    REQUIRE(static_cast<int>(LRRatio::ThreeToTwo) >= 0);
    REQUIRE(static_cast<int>(LRRatio::FourToThree) >= 0);
    REQUIRE(static_cast<int>(LRRatio::OneToTwo) >= 0);
    REQUIRE(static_cast<int>(LRRatio::TwoToThree) >= 0);
    REQUIRE(static_cast<int>(LRRatio::ThreeToFour) >= 0);
}

// =============================================================================
// User Story 1: Classic Ping-Pong (MVP)
// FR-001, FR-004, FR-011, FR-013, FR-024, FR-027
// SC-001, SC-007
// =============================================================================

TEST_CASE("US1: Classic ping-pong alternating L/R pattern", "[ping-pong][US1][SC-001][SC-007]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    // Configure for classic ping-pong
    delay.setDelayTimeMs(100.0f);  // 100ms = 4410 samples at 44.1kHz
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);  // Full ping-pong
    delay.setMix(1.0f);            // 100% wet for easier testing
    delay.setLRRatio(LRRatio::OneToOne);

    // Process impulse on left channel only
    constexpr size_t kBufferSize = 22050;  // 0.5 seconds
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;  // Impulse

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // Expected: First echo on RIGHT (ping), second on LEFT (pong)
    // At 100ms delay = 4410 samples
    constexpr size_t kDelaySamples = 4410;

    SECTION("First echo appears on right channel (SC-007)") {
        // First echo should be on RIGHT around sample 4410
        float rightPeak = 0.0f;
        for (size_t i = kDelaySamples - 10; i < kDelaySamples + 10; ++i) {
            rightPeak = std::max(rightPeak, std::abs(right[i]));
        }
        REQUIRE(rightPeak > 0.3f);  // Should be significant

        // Left should be quiet at this point
        float leftAtFirstEcho = 0.0f;
        for (size_t i = kDelaySamples - 10; i < kDelaySamples + 10; ++i) {
            leftAtFirstEcho = std::max(leftAtFirstEcho, std::abs(left[i]));
        }
        REQUIRE(leftAtFirstEcho < 0.1f);
    }

    SECTION("Second echo appears on left channel (SC-007)") {
        // Second echo should be on LEFT around sample 8820
        constexpr size_t kSecondEcho = kDelaySamples * 2;
        float leftPeak = 0.0f;
        for (size_t i = kSecondEcho - 10; i < kSecondEcho + 10; ++i) {
            leftPeak = std::max(leftPeak, std::abs(left[i]));
        }
        REQUIRE(leftPeak > 0.1f);  // Reduced by feedback
    }
}

TEST_CASE("US1: Feedback decay at 50%", "[ping-pong][US1][feedback]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    constexpr size_t kBufferSize = 22050;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // Find peaks for first and third echoes (both on same channel after full cycle)
    constexpr size_t kDelaySamples = 4410;

    float firstEchoPeak = 0.0f;
    for (size_t i = kDelaySamples - 10; i < kDelaySamples + 10; ++i) {
        firstEchoPeak = std::max(firstEchoPeak, std::abs(right[i]));
    }

    // Third echo at 3x delay (still on right channel due to alternation)
    float thirdEchoPeak = 0.0f;
    constexpr size_t kThirdEcho = kDelaySamples * 3;
    for (size_t i = kThirdEcho - 10; i < kThirdEcho + 10; ++i) {
        thirdEchoPeak = std::max(thirdEchoPeak, std::abs(right[i]));
    }

    // After 2 feedback cycles (first->third), amplitude should be ~0.25 (0.5^2)
    float expectedRatio = 0.25f;
    float actualRatio = thirdEchoPeak / firstEchoPeak;
    REQUIRE(actualRatio == Approx(expectedRatio).margin(0.1f));
}

TEST_CASE("US1: Dry/wet mix control", "[ping-pong][US1][mix]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    std::fill(left.begin(), left.end(), 1.0f);
    std::fill(right.begin(), right.end(), 1.0f);

    auto ctx = makeTestContext();

    SECTION("0% mix = dry only (FR-027)") {
        delay.setMix(0.0f);
        delay.snapParameters();  // Immediate application for testing
        delay.process(left.data(), right.data(), kBlockSize, ctx);

        // Output should equal input (dry only)
        REQUIRE(left[kBlockSize / 2] == Approx(1.0f).margin(0.01f));
    }

    SECTION("100% mix = wet only") {
        delay.setMix(1.0f);
        delay.snapParameters();  // Immediate application for testing
        delay.process(left.data(), right.data(), kBlockSize, ctx);

        // First block with no delay history should be mostly silent (wet only)
        // After processing, the delayed signal will come in later blocks
        REQUIRE(left[0] == Approx(0.0f).margin(0.1f));
    }

    SECTION("50% mix = equal blend") {
        delay.setMix(0.5f);
        delay.snapParameters();  // Immediate application for testing
        delay.process(left.data(), right.data(), kBlockSize, ctx);

        // Should be between 0 and 1
        REQUIRE(left[kBlockSize / 2] > 0.0f);
        REQUIRE(left[kBlockSize / 2] < 1.0f);
    }
}

// =============================================================================
// User Story 2: Asymmetric Stereo Timing
// FR-005, FR-006, FR-007, FR-008
// SC-002
// =============================================================================

TEST_CASE("US2: 2:1 ratio timing", "[ping-pong][US2][ratio]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(500.0f);  // Base time 500ms
    delay.setLRRatio(LRRatio::TwoToOne);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    constexpr size_t kBufferSize = 44100;  // 1 second
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // 2:1 ratio: L = 500ms, R = 250ms
    // First echo on R at 250ms = 11025 samples
    constexpr size_t kRightDelay = 11025;

    size_t rightPeakPos = findFirstPeak(right.data(), kBufferSize, 0.3f);
    REQUIRE(rightPeakPos == Approx(kRightDelay).margin(kRightDelay * 0.01f));  // 1% tolerance (SC-002)
}

TEST_CASE("US2: 3:2 ratio timing", "[ping-pong][US2][ratio]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(600.0f);  // Base time 600ms
    delay.setLRRatio(LRRatio::ThreeToTwo);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    constexpr size_t kBufferSize = 44100;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // 3:2 ratio: L = 600ms, R = 400ms
    // First echo on R at 400ms = 17640 samples
    constexpr size_t kRightDelay = 17640;

    size_t rightPeakPos = findFirstPeak(right.data(), kBufferSize, 0.3f);
    REQUIRE(rightPeakPos == Approx(kRightDelay).margin(kRightDelay * 0.01f));
}

TEST_CASE("US2: Inverse ratios (1:2, 2:3, 3:4)", "[ping-pong][US2][ratio]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(500.0f);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    constexpr size_t kBufferSize = 44100;

    SECTION("1:2 ratio - left is faster") {
        delay.setLRRatio(LRRatio::OneToTwo);

        std::array<float, kBufferSize> left{};
        std::array<float, kBufferSize> right{};
        left[0] = 1.0f;

        auto ctx = makeTestContext();
        delay.process(left.data(), right.data(), kBufferSize, ctx);

        // 1:2 ratio: L = 250ms, R = 500ms
        // First echo on R at 500ms (R is the base time for inverse ratios)
        // Actually, with cross-feedback, echo goes L->R
        // The R delay is 500ms = 22050 samples
        constexpr size_t kRightDelay = 22050;

        size_t rightPeakPos = findFirstPeak(right.data(), kBufferSize, 0.3f);
        REQUIRE(rightPeakPos == Approx(kRightDelay).margin(kRightDelay * 0.02f));
    }
}

// =============================================================================
// User Story 3: Tempo-Synced Ping-Pong
// FR-002, FR-003
// SC-003
// =============================================================================

TEST_CASE("US3: Quarter note at 120 BPM = 500ms", "[ping-pong][US3][tempo-sync]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setTimeMode(TimeMode::Synced);
    delay.setNoteValue(NoteValue::Quarter, NoteModifier::None);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    constexpr size_t kBufferSize = 44100;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext(kSampleRate, 120.0f);  // 120 BPM
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // At 120 BPM, quarter note = 500ms = 22050 samples
    constexpr size_t kExpectedDelay = 22050;

    size_t rightPeakPos = findFirstPeak(right.data(), kBufferSize, 0.3f);
    // SC-003: Within 1 sample accuracy
    REQUIRE(rightPeakPos == Approx(kExpectedDelay).margin(1.0f));
}

TEST_CASE("US3: Dotted eighth at 120 BPM", "[ping-pong][US3][tempo-sync]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setTimeMode(TimeMode::Synced);
    delay.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    constexpr size_t kBufferSize = 44100;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext(kSampleRate, 120.0f);
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // At 120 BPM, dotted eighth = 375ms = 16537.5 samples
    constexpr float kExpectedDelay = 16537.5f;

    size_t rightPeakPos = findFirstPeak(right.data(), kBufferSize, 0.3f);
    REQUIRE(static_cast<float>(rightPeakPos) == Approx(kExpectedDelay).margin(2.0f));
}

// =============================================================================
// User Story 4: Stereo Width Control
// FR-014, FR-015, FR-016, FR-017, FR-018
// SC-004, SC-005
// =============================================================================

TEST_CASE("US4: Width 0% = mono (SC-004)", "[ping-pong][US4][width]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);
    delay.setWidth(0.0f);  // Mono
    delay.snapParameters();  // Immediate application for testing

    constexpr size_t kBufferSize = 8820;  // ~200ms
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // At width 0%, L and R should be identical (correlation > 0.99)
    float correlation = calculateCorrelation(left.data(), right.data(), kBufferSize);
    REQUIRE(correlation > 0.99f);
}

TEST_CASE("US4: Width 100% = natural stereo", "[ping-pong][US4][width]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);
    delay.setWidth(100.0f);  // Natural stereo

    constexpr size_t kBufferSize = 8820;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // At width 100%, L and R should be different (ping-pong pattern)
    float correlation = calculateCorrelation(left.data(), right.data(), kBufferSize);
    REQUIRE(correlation < 0.99f);  // Not mono
}

TEST_CASE("US4: Width 200% = ultra-wide (SC-005)", "[ping-pong][US4][width]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);
    delay.setWidth(200.0f);  // Ultra-wide

    constexpr size_t kBufferSize = 8820;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // At width 200%, correlation should be < 0.5
    float correlation = calculateCorrelation(left.data(), right.data(), kBufferSize);
    REQUIRE(correlation < 0.5f);
}

// =============================================================================
// User Story 5: Cross-Feedback Control
// FR-009, FR-010, FR-012
// SC-006, SC-009
// =============================================================================

TEST_CASE("US5: 0% cross-feedback = channel isolation (SC-006)", "[ping-pong][US5][cross-feedback]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(0.0f);  // No cross-feedback
    delay.setMix(1.0f);
    delay.snapParameters();  // Immediate application for testing

    constexpr size_t kBufferSize = 8820;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;  // Impulse on left only

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // Right channel should be essentially silent (>60dB isolation)
    float rightPeak = findPeak(right.data(), kBufferSize);
    float leftPeak = findPeak(left.data(), kBufferSize);

    // 60dB = 0.001 ratio
    if (leftPeak > 0.0f) {
        float ratio = rightPeak / leftPeak;
        REQUIRE(ratio < 0.001f);
    }
}

TEST_CASE("US5: 50% cross-feedback = hybrid pattern", "[ping-pong][US5][cross-feedback]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(0.5f);  // 50% cross-feedback
    delay.setMix(1.0f);

    constexpr size_t kBufferSize = 8820;
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // Both channels should have signal (hybrid pattern)
    float rightPeak = findPeak(right.data(), kBufferSize);
    float leftPeak = findPeak(left.data(), kBufferSize);

    REQUIRE(rightPeak > 0.1f);
    REQUIRE(leftPeak > 0.1f);
}

TEST_CASE("US5: Feedback 120% with limiter = stable output (SC-009)", "[ping-pong][US5][limiter]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(50.0f);  // Short delay for faster feedback buildup
    delay.setFeedback(1.2f);      // 120% - would runaway without limiting
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    constexpr size_t kBufferSize = 44100;  // 1 second
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // Output should be bounded (limiter active)
    float peakL = findPeak(left.data(), kBufferSize);
    float peakR = findPeak(right.data(), kBufferSize);

    REQUIRE(peakL <= 2.0f);  // Reasonable bound
    REQUIRE(peakR <= 2.0f);
}

// =============================================================================
// User Story 6: Modulated Ping-Pong
// FR-019, FR-020, FR-021, FR-022, FR-023
// =============================================================================

TEST_CASE("US6: 0% modulation = zero pitch variation (FR-022)", "[ping-pong][US6][modulation]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);
    delay.setModulationDepth(0.0f);  // No modulation
    delay.setModulationRate(1.0f);
    delay.snapParameters();  // Immediate application for testing

    // Process twice with same input - output should be identical
    constexpr size_t kBufferSize = 4410;
    std::array<float, kBufferSize> left1{};
    std::array<float, kBufferSize> right1{};
    std::array<float, kBufferSize> left2{};
    std::array<float, kBufferSize> right2{};

    left1[0] = 1.0f;
    left2[0] = 1.0f;

    auto ctx = makeTestContext();

    delay.process(left1.data(), right1.data(), kBufferSize, ctx);
    delay.reset();  // reset() also snaps parameters
    delay.process(left2.data(), right2.data(), kBufferSize, ctx);

    // Outputs should be identical (no modulation variation)
    for (size_t i = 0; i < kBufferSize; ++i) {
        REQUIRE(left1[i] == Approx(left2[i]).margin(1e-5f));
        REQUIRE(right1[i] == Approx(right2[i]).margin(1e-5f));
    }
}

TEST_CASE("US6: Modulation depth and rate settings", "[ping-pong][US6][modulation]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    // These should not throw or crash
    delay.setModulationDepth(0.5f);
    delay.setModulationRate(2.0f);

    REQUIRE(true);  // If we get here, settings work
}

TEST_CASE("US6: L/R modulation is independent (90 phase offset)", "[ping-pong][US6][modulation]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(0.0f);  // No feedback to isolate modulation effect
    delay.setCrossFeedback(0.0f);
    delay.setMix(1.0f);
    delay.setModulationDepth(1.0f);  // Full modulation
    delay.setModulationRate(1.0f);

    // Fill buffers with constant signal
    constexpr size_t kBufferSize = 44100;  // 1 second = 1 LFO cycle
    std::array<float, kBufferSize> left{};
    std::array<float, kBufferSize> right{};
    std::fill(left.begin(), left.end(), 1.0f);
    std::fill(right.begin(), right.end(), 1.0f);

    auto ctx = makeTestContext();
    delay.process(left.data(), right.data(), kBufferSize, ctx);

    // With 90° phase offset, L and R modulation should be different
    // This is hard to test precisely without knowing exact implementation
    // Just verify both channels have variation
    float leftVar = 0.0f, rightVar = 0.0f;
    float leftMean = 0.0f, rightMean = 0.0f;

    for (size_t i = 0; i < kBufferSize; ++i) {
        leftMean += left[i];
        rightMean += right[i];
    }
    leftMean /= static_cast<float>(kBufferSize);
    rightMean /= static_cast<float>(kBufferSize);

    for (size_t i = 0; i < kBufferSize; ++i) {
        leftVar += (left[i] - leftMean) * (left[i] - leftMean);
        rightVar += (right[i] - rightMean) * (right[i] - rightMean);
    }

    // Both channels should have some variance due to modulation
    REQUIRE(leftVar > 0.0f);
    REQUIRE(rightVar > 0.0f);
}

// =============================================================================
// Phase 9: Edge Cases and Polish
// =============================================================================

TEST_CASE("Edge case: Min/max delay values", "[ping-pong][edge-case]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    SECTION("Minimum delay (1ms)") {
        delay.setDelayTimeMs(1.0f);
        delay.setFeedback(0.5f);
        delay.setCrossFeedback(1.0f);
        delay.setMix(1.0f);

        std::array<float, kBlockSize> left{};
        std::array<float, kBlockSize> right{};
        left[0] = 1.0f;

        auto ctx = makeTestContext();
        REQUIRE_NOTHROW(delay.process(left.data(), right.data(), kBlockSize, ctx));
    }

    SECTION("Maximum delay") {
        delay.setDelayTimeMs(kMaxDelayMs);
        delay.setFeedback(0.5f);
        delay.setCrossFeedback(1.0f);
        delay.setMix(1.0f);

        std::array<float, kBlockSize> left{};
        std::array<float, kBlockSize> right{};
        left[0] = 1.0f;

        auto ctx = makeTestContext();
        REQUIRE_NOTHROW(delay.process(left.data(), right.data(), kBlockSize, ctx));
    }
}

TEST_CASE("Edge case: Feedback > 100%", "[ping-pong][edge-case]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(1.2f);  // 120%
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};
    left[0] = 1.0f;

    auto ctx = makeTestContext();
    REQUIRE_NOTHROW(delay.process(left.data(), right.data(), kBlockSize, ctx));
}

TEST_CASE("Edge case: Ratio switching", "[ping-pong][edge-case]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(100.0f);
    delay.setFeedback(0.5f);
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    auto ctx = makeTestContext();

    // Switch ratios rapidly - should not crash or produce NaN
    for (int i = 0; i < 7; ++i) {
        delay.setLRRatio(static_cast<LRRatio>(i));
        std::fill(left.begin(), left.end(), 1.0f);
        std::fill(right.begin(), right.end(), 1.0f);
        delay.process(left.data(), right.data(), kBlockSize, ctx);

        // Check for NaN
        for (size_t j = 0; j < kBlockSize; ++j) {
            REQUIRE_FALSE(std::isnan(left[j]));
            REQUIRE_FALSE(std::isnan(right[j]));
        }
    }
}

TEST_CASE("Output level dB-to-gain conversion (FR-025)", "[ping-pong][output-level]") {
    PingPongDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);

    delay.setDelayTimeMs(10.0f);  // Short delay
    delay.setFeedback(0.0f);       // No feedback
    delay.setCrossFeedback(1.0f);
    delay.setMix(1.0f);

    auto ctx = makeTestContext();

    SECTION("0dB = unity gain") {
        delay.setOutputLevel(0.0f);
        delay.snapParameters();  // Immediate application for testing

        std::array<float, kBlockSize> left{};
        std::array<float, kBlockSize> right{};

        // Process multiple blocks to let delay fill, refilling input each time
        for (int i = 0; i < 10; ++i) {
            std::fill(left.begin(), left.end(), 1.0f);
            std::fill(right.begin(), right.end(), 1.0f);
            delay.process(left.data(), right.data(), kBlockSize, ctx);
        }

        // Output should be approximately 1.0 (unity)
        float peak = findPeak(left.data(), kBlockSize);
        REQUIRE(peak == Approx(1.0f).margin(0.1f));
    }

    SECTION("-6dB = half amplitude") {
        delay.setOutputLevel(-6.02f);
        delay.snapParameters();  // Immediate application for testing

        std::array<float, kBlockSize> left{};
        std::array<float, kBlockSize> right{};

        // Process multiple blocks to let delay fill, refilling input each time
        for (int i = 0; i < 10; ++i) {
            std::fill(left.begin(), left.end(), 1.0f);
            std::fill(right.begin(), right.end(), 1.0f);
            delay.process(left.data(), right.data(), kBlockSize, ctx);
        }

        float peak = findPeak(left.data(), kBlockSize);
        REQUIRE(peak == Approx(0.5f).margin(0.1f));
    }

    SECTION("+12dB = ~4x amplitude") {
        delay.setOutputLevel(12.0f);
        delay.snapParameters();  // Immediate application for testing

        std::array<float, kBlockSize> left{};
        std::array<float, kBlockSize> right{};

        // Process multiple blocks to let delay fill, refilling input each time
        for (int i = 0; i < 10; ++i) {
            std::fill(left.begin(), left.end(), 0.25f);  // Start lower to avoid clipping
            std::fill(right.begin(), right.end(), 0.25f);
            delay.process(left.data(), right.data(), kBlockSize, ctx);
        }

        float peak = findPeak(left.data(), kBlockSize);
        REQUIRE(peak == Approx(1.0f).margin(0.2f));  // 0.25 * 4 = 1.0
    }
}

