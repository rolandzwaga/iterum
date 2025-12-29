// ==============================================================================
// Digital Delay Width Processing Tests
// ==============================================================================
// Tests for Mid/Side stereo width processing in Digital Delay (spec 036).
// Verifies M/S encoding, width application, and decoding.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/features/digital_delay.h"

#include <array>
#include <cmath>

using Catch::Approx;
using namespace Iterum::DSP;

// ==============================================================================
// Helper Functions
// ==============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr size_t kTestBufferSize = 8820;  // ~200ms at 44.1kHz

/// @brief Calculate Pearson correlation coefficient between two signals
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

// ==============================================================================
// Test: Width at 0% (Mono)
// ==============================================================================

TEST_CASE("Width at 0% produces mono output", "[features][digital-delay][width][SC-005]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);

    delay.setDelayTime(100.0f);  // 100ms delay
    delay.setFeedback(0.5f);
    delay.setMix(1.0f);  // 100% wet
    delay.setWidth(0.0f);  // 0% = mono
    delay.snapParameters();  // Skip smoothing for testing

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Impulse on left channel
    left[0] = 1.0f;

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // At width 0%, L and R should be identical (correlation > 0.99)
    float correlation = calculateCorrelation(left.data(), right.data(), kTestBufferSize);
    REQUIRE(correlation > 0.99f);
}

// ==============================================================================
// Test: Width at 100% (Original)
// ==============================================================================

TEST_CASE("Width at 100% preserves original stereo image", "[features][digital-delay][width][SC-006]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);

    delay.setDelayTime(100.0f);
    delay.setFeedback(0.5f);
    delay.setMix(1.0f);
    delay.setWidth(100.0f);  // 100% = natural stereo
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};

    // Stereo impulse with different amplitudes to create stereo content
    left[0] = 1.0f;
    right[0] = 0.5f;

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // At width 100%, stereo image should be preserved
    // Find the peak sample (delayed impulse) and verify L/R ratio
    size_t peakIdx = 0;
    float peakL = 0.0f;
    for (size_t i = 0; i < kTestBufferSize; ++i) {
        if (std::abs(left[i]) > std::abs(peakL)) {
            peakL = left[i];
            peakIdx = i;
        }
    }

    // At the peak, L/R ratio should match input (1.0/0.5 = 2.0)
    float outputRatio = left[peakIdx] / right[peakIdx];
    float expectedRatio = 1.0f / 0.5f;  // Input ratio
    REQUIRE(outputRatio == Approx(expectedRatio).margin(0.01f));

    // Also verify it's not mono (channels should be different)
    REQUIRE(std::abs(left[peakIdx] - right[peakIdx]) > 0.1f);
}

// ==============================================================================
// Test: Width at 200% (Maximum)
// ==============================================================================

TEST_CASE("Width at 200% doubles stereo separation", "[features][digital-delay][width][SC-007]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);

    delay.setDelayTime(100.0f);
    delay.setFeedback(0.5f);
    delay.setMix(1.0f);
    delay.setWidth(200.0f);  // 200% = ultra-wide
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};
    left[0] = 1.0f;  // Impulse

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    delay.process(left.data(), right.data(), kTestBufferSize, ctx);

    // At width 200%, correlation should be low (wide stereo)
    float correlation = calculateCorrelation(left.data(), right.data(), kTestBufferSize);
    REQUIRE(correlation < 0.5f);
}

// ==============================================================================
// Test: Width with Mono Input
// ==============================================================================

TEST_CASE("Width control works with mono input", "[features][digital-delay][width]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);

    delay.setDelayTime(100.0f);
    delay.setFeedback(0.5f);
    delay.setMix(1.0f);
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};
    left[0] = 1.0f;   // Mono impulse
    right[0] = 1.0f;  // Identical on both channels

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    // Test that mono input stays mono regardless of width
    for (float width : {0.0f, 100.0f, 200.0f}) {
        delay.setWidth(width);
        delay.snapParameters();

        // Reset buffers
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        left[0] = 1.0f;   // Mono impulse
        right[0] = 1.0f;  // Identical on both channels

        delay.process(left.data(), right.data(), kTestBufferSize, ctx);

        // With identical input, output should be identical (correlation = 1)
        float correlation = calculateCorrelation(left.data(), right.data(), kTestBufferSize);
        REQUIRE(correlation > 0.99f);
    }
}

// ==============================================================================
// Test: No NaN or Inf Output
// ==============================================================================

TEST_CASE("Width processing produces no NaN or Inf", "[features][digital-delay][width][safety]") {
    DigitalDelay delay;
    delay.prepare(kSampleRate, kBlockSize);

    delay.setDelayTime(100.0f);
    delay.setFeedback(0.5f);
    delay.setMix(1.0f);
    delay.snapParameters();

    std::array<float, kTestBufferSize> left{};
    std::array<float, kTestBufferSize> right{};
    left[0] = 1.0f;

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kTestBufferSize,
        .tempoBPM = 120.0,
        .isPlaying = false
    };

    for (float width : {0.0f, 200.0f}) {
        delay.setWidth(width);
        delay.snapParameters();

        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        left[0] = 1.0f;

        delay.process(left.data(), right.data(), kTestBufferSize, ctx);

        // Check for NaN/Inf
        for (size_t i = 0; i < kTestBufferSize; ++i) {
            REQUIRE(std::isfinite(left[i]));
            REQUIRE(std::isfinite(right[i]));
        }
    }
}
