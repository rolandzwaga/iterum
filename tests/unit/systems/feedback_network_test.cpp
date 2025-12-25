// Layer 3: System Component - FeedbackNetwork Tests
// Feature: 019-feedback-network
//
// Tests for FeedbackNetwork which manages feedback loops for delay effects.
// Composes DelayEngine, MultimodeFilter, and SaturationProcessor.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
#include <cmath>
#include <algorithm>
#include <vector>

#include "dsp/systems/feedback_network.h"
#include "dsp/core/block_context.h"

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// Create a default BlockContext for testing
BlockContext createTestContext(double sampleRate = 44100.0) {
    BlockContext ctx;
    ctx.sampleRate = sampleRate;
    ctx.tempoBPM = 120.0;
    ctx.timeSignatureNumerator = 4;
    ctx.timeSignatureDenominator = 4;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;
    return ctx;
}

/// Generate an impulse at the start of a buffer
void generateImpulse(float* buffer, size_t size, float amplitude = 1.0f) {
    std::fill(buffer, buffer + size, 0.0f);
    if (size > 0) {
        buffer[0] = amplitude;
    }
}

/// Find the peak absolute value in a buffer
float findPeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// Convert linear amplitude to dB
float linearToDb(float linear) {
    if (linear <= 0.0f) return -144.0f;
    return 20.0f * std::log10(linear);
}

} // anonymous namespace

// =============================================================================
// US1: Basic Feedback Loop Tests
// =============================================================================

TEST_CASE("FeedbackNetwork default constructor initializes correctly", "[feedback][US1]") {
    FeedbackNetwork network;

    // Should not be prepared initially
    REQUIRE_FALSE(network.isPrepared());

    // Default feedback should be 0.5 (50%)
    REQUIRE(network.getFeedbackAmount() == Approx(0.5f));
}

TEST_CASE("FeedbackNetwork prepare() allocates resources", "[feedback][US1]") {
    FeedbackNetwork network;

    // Prepare with reasonable parameters
    network.prepare(44100.0, 512, 2000.0f);

    REQUIRE(network.isPrepared());
}

TEST_CASE("FeedbackNetwork reset() clears internal state", "[feedback][US1]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 2000.0f);
    network.setDelayTimeMs(100.0f);
    network.setFeedbackAmount(0.8f);

    auto ctx = createTestContext();

    // Process some audio to fill delay buffer
    std::array<float, 512> buffer;
    std::fill(buffer.begin(), buffer.end(), 1.0f);
    network.process(buffer.data(), buffer.size(), ctx);

    // Reset should clear state
    network.reset();

    // Process silence - should get silence out (no leftover delayed audio)
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    network.process(buffer.data(), buffer.size(), ctx);

    float peak = findPeak(buffer.data(), buffer.size());
    REQUIRE(peak < 0.001f);
}

TEST_CASE("FeedbackNetwork setFeedbackAmount(0.0) produces single repeat", "[feedback][US1]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 2000.0f);
    network.setDelayTimeMs(100.0f);  // 100ms = 4410 samples at 44.1kHz
    network.setFeedbackAmount(0.0f);

    auto ctx = createTestContext();

    // Process impulse through enough blocks to see multiple delays
    constexpr size_t kBlockSize = 512;
    constexpr size_t kDelayInSamples = 4410;  // 100ms
    constexpr size_t kNumBlocks = 20;  // Enough to see 2+ delays

    std::vector<float> allOutput;
    allOutput.reserve(kBlockSize * kNumBlocks);

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> buffer = {};
        if (block == 0) {
            buffer[0] = 1.0f;  // Impulse at start
        }
        network.process(buffer.data(), buffer.size(), ctx);
        allOutput.insert(allOutput.end(), buffer.begin(), buffer.end());
    }

    // First delay should appear around kDelayInSamples
    // With 0% feedback, there should be only one delayed repeat

    // Check there's a peak around the first delay
    float firstDelayPeak = 0.0f;
    for (size_t i = kDelayInSamples - 50; i < kDelayInSamples + 50 && i < allOutput.size(); ++i) {
        firstDelayPeak = std::max(firstDelayPeak, std::abs(allOutput[i]));
    }
    REQUIRE(firstDelayPeak > 0.5f);  // First repeat should be present

    // Check there's NO peak around the second delay (2x delay time)
    float secondDelayPeak = 0.0f;
    size_t secondDelayStart = 2 * kDelayInSamples - 50;
    size_t secondDelayEnd = std::min(2 * kDelayInSamples + 50, allOutput.size());
    for (size_t i = secondDelayStart; i < secondDelayEnd; ++i) {
        secondDelayPeak = std::max(secondDelayPeak, std::abs(allOutput[i]));
    }
    REQUIRE(secondDelayPeak < 0.01f);  // No second repeat with 0% feedback
}

TEST_CASE("FeedbackNetwork setFeedbackAmount(0.5) produces ~6dB decay per repeat", "[feedback][US1][SC-001]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 2000.0f);
    network.setDelayTimeMs(100.0f);  // 100ms = 4410 samples at 44.1kHz
    network.setFeedbackAmount(0.5f);

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    constexpr size_t kDelayInSamples = 4410;
    constexpr size_t kNumBlocks = 30;  // Enough for 3 repeats

    std::vector<float> allOutput;
    allOutput.reserve(kBlockSize * kNumBlocks);

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> buffer = {};
        if (block == 0) {
            buffer[0] = 1.0f;
        }
        network.process(buffer.data(), buffer.size(), ctx);
        allOutput.insert(allOutput.end(), buffer.begin(), buffer.end());
    }

    // Find peaks at each delay interval
    auto findPeakAround = [&](size_t centerSample) {
        float peak = 0.0f;
        size_t start = (centerSample > 100) ? centerSample - 100 : 0;
        size_t end = std::min(centerSample + 100, allOutput.size());
        for (size_t i = start; i < end; ++i) {
            peak = std::max(peak, std::abs(allOutput[i]));
        }
        return peak;
    };

    float repeat1 = findPeakAround(kDelayInSamples);
    float repeat2 = findPeakAround(2 * kDelayInSamples);
    float repeat3 = findPeakAround(3 * kDelayInSamples);

    // Each repeat should be ~50% of previous (-6.02dB)
    // SC-001 tolerance: ±0.5dB
    const float expectedDecayDb = -6.02f;
    const float toleranceDb = 0.5f;

    float decay1to2Db = linearToDb(repeat2 / repeat1);
    float decay2to3Db = linearToDb(repeat3 / repeat2);

    REQUIRE(decay1to2Db == Approx(expectedDecayDb).margin(toleranceDb));
    REQUIRE(decay2to3Db == Approx(expectedDecayDb).margin(toleranceDb));
}

TEST_CASE("FeedbackNetwork setFeedbackAmount(1.0) maintains signal indefinitely", "[feedback][US1][SC-002]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 2000.0f);
    network.setDelayTimeMs(100.0f);
    network.setFeedbackAmount(1.0f);

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    constexpr size_t kDelayInSamples = 4410;
    // Need at least 10 repeats: 10 * 4410 = 44100 samples = ceil(44100/512) = 87 blocks
    constexpr size_t kNumBlocks = 90;

    std::vector<float> allOutput;
    allOutput.reserve(kBlockSize * kNumBlocks);

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> buffer = {};
        if (block == 0) {
            buffer[0] = 1.0f;
        }
        network.process(buffer.data(), buffer.size(), ctx);
        allOutput.insert(allOutput.end(), buffer.begin(), buffer.end());
    }

    // Find peaks at each delay interval
    auto findPeakAround = [&](size_t centerSample) {
        float peak = 0.0f;
        size_t start = (centerSample > 100) ? centerSample - 100 : 0;
        size_t end = std::min(centerSample + 100, allOutput.size());
        for (size_t i = start; i < end; ++i) {
            peak = std::max(peak, std::abs(allOutput[i]));
        }
        return peak;
    };

    // Check first 10 repeats maintain level (SC-002: ±0.1dB tolerance)
    float repeat1 = findPeakAround(kDelayInSamples);
    const float toleranceDb = 0.1f;

    // Verify repeat1 is actually present (not 0)
    REQUIRE(repeat1 > 0.5f);

    for (int n = 2; n <= 10; ++n) {
        float repeatN = findPeakAround(n * kDelayInSamples);
        float decayDb = linearToDb(repeatN / repeat1);
        REQUIRE(decayDb == Approx(0.0f).margin(toleranceDb));
    }
}

TEST_CASE("FeedbackNetwork feedback values are clamped to [0.0, 1.2]", "[feedback][US1][FR-012]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 2000.0f);

    SECTION("negative values clamped to 0") {
        network.setFeedbackAmount(-0.5f);
        REQUIRE(network.getFeedbackAmount() == Approx(0.0f));
    }

    SECTION("values above 1.2 clamped to 1.2") {
        network.setFeedbackAmount(1.5f);
        REQUIRE(network.getFeedbackAmount() == Approx(1.2f));
    }

    SECTION("valid values in range accepted") {
        network.setFeedbackAmount(0.7f);
        REQUIRE(network.getFeedbackAmount() == Approx(0.7f));

        network.setFeedbackAmount(1.1f);
        REQUIRE(network.getFeedbackAmount() == Approx(1.1f));
    }
}

TEST_CASE("FeedbackNetwork NaN feedback values are rejected", "[feedback][US1][FR-013]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 2000.0f);

    network.setFeedbackAmount(0.7f);
    REQUIRE(network.getFeedbackAmount() == Approx(0.7f));

    // Try to set NaN - should keep previous value
    network.setFeedbackAmount(std::numeric_limits<float>::quiet_NaN());
    REQUIRE(network.getFeedbackAmount() == Approx(0.7f));
}

TEST_CASE("FeedbackNetwork process() mono works correctly", "[feedback][US1][FR-008]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(5.0f);  // 5ms = 221 samples (fits in one block)
    network.setFeedbackAmount(0.5f);

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    constexpr size_t kDelayInSamples = 221;  // 5ms at 44.1kHz

    // Process impulse through enough blocks to see delayed output
    std::vector<float> allOutput;
    allOutput.reserve(kBlockSize * 2);

    for (size_t block = 0; block < 2; ++block) {
        std::array<float, kBlockSize> buffer = {};
        if (block == 0) {
            buffer[0] = 1.0f;  // Impulse at start
        }
        network.process(buffer.data(), buffer.size(), ctx);
        allOutput.insert(allOutput.end(), buffer.begin(), buffer.end());
    }

    // Output should contain delayed signal after delay time
    float peakAfterDelay = 0.0f;
    for (size_t i = kDelayInSamples; i < allOutput.size(); ++i) {
        peakAfterDelay = std::max(peakAfterDelay, std::abs(allOutput[i]));
    }
    REQUIRE(peakAfterDelay >= 0.5f);
}

TEST_CASE("FeedbackNetwork process() stereo works correctly", "[feedback][US1][FR-009]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(5.0f);  // 5ms = 221 samples (fits in one block)
    network.setFeedbackAmount(0.5f);

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    constexpr size_t kDelayInSamples = 221;

    // Collect stereo output over multiple blocks
    std::vector<float> allLeft, allRight;
    allLeft.reserve(kBlockSize * 2);
    allRight.reserve(kBlockSize * 2);

    for (size_t block = 0; block < 2; ++block) {
        std::array<float, kBlockSize> left = {}, right = {};
        if (block == 0) {
            left[0] = 1.0f;   // Impulse in left
            right[0] = 0.5f;  // Different impulse in right
        }
        network.process(left.data(), right.data(), left.size(), ctx);
        allLeft.insert(allLeft.end(), left.begin(), left.end());
        allRight.insert(allRight.end(), right.begin(), right.end());
    }

    // Both channels should have delayed output
    float peakLeftAfterDelay = 0.0f, peakRightAfterDelay = 0.0f;
    for (size_t i = kDelayInSamples; i < allLeft.size(); ++i) {
        peakLeftAfterDelay = std::max(peakLeftAfterDelay, std::abs(allLeft[i]));
        peakRightAfterDelay = std::max(peakRightAfterDelay, std::abs(allRight[i]));
    }
    REQUIRE(peakLeftAfterDelay >= 0.5f);
    REQUIRE(peakRightAfterDelay >= 0.25f);  // Half amplitude impulse
}

TEST_CASE("FeedbackNetwork parameter smoothing prevents clicks", "[feedback][US1][FR-011][SC-009]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);
    network.setFeedbackAmount(0.0f);

    auto ctx = createTestContext();

    // Process to let smoother settle
    std::array<float, 512> buffer = {};
    for (int i = 0; i < 10; ++i) {
        network.process(buffer.data(), buffer.size(), ctx);
    }

    // Now make an abrupt parameter change
    network.setFeedbackAmount(1.0f);

    // Feed a constant signal
    std::fill(buffer.begin(), buffer.end(), 0.5f);
    network.process(buffer.data(), buffer.size(), ctx);

    // Check for discontinuities (clicks)
    // A click would appear as a large sample-to-sample change
    float maxDelta = 0.0f;
    for (size_t i = 1; i < buffer.size(); ++i) {
        float delta = std::abs(buffer[i] - buffer[i-1]);
        maxDelta = std::max(maxDelta, delta);
    }

    // With smoothing, max delta should be small
    // Without smoothing, instant feedback change would cause large delta
    REQUIRE(maxDelta < 0.1f);
}

// =============================================================================
// Additional Edge Case Tests
// =============================================================================

TEST_CASE("FeedbackNetwork handles zero delay time", "[feedback][US1][edge]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(0.0f);
    network.setFeedbackAmount(0.5f);

    auto ctx = createTestContext();

    std::array<float, 512> buffer;
    generateImpulse(buffer.data(), buffer.size());

    // Should not crash with zero delay
    REQUIRE_NOTHROW(network.process(buffer.data(), buffer.size(), ctx));
}

TEST_CASE("FeedbackNetwork handles maximum delay time", "[feedback][US1][edge]") {
    FeedbackNetwork network;
    constexpr float maxDelay = 2000.0f;
    network.prepare(44100.0, 512, maxDelay);
    network.setDelayTimeMs(maxDelay);
    network.setFeedbackAmount(0.5f);

    auto ctx = createTestContext();

    std::array<float, 512> buffer;
    generateImpulse(buffer.data(), buffer.size());

    // Should not crash with max delay
    REQUIRE_NOTHROW(network.process(buffer.data(), buffer.size(), ctx));
}

TEST_CASE("FeedbackNetwork handles empty buffer", "[feedback][US1][edge]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);

    auto ctx = createTestContext();

    float dummy = 0.0f;

    // Should handle zero samples without crash
    REQUIRE_NOTHROW(network.process(&dummy, 0, ctx));
}

TEST_CASE("FeedbackNetwork not prepared returns early", "[feedback][US1][edge]") {
    FeedbackNetwork network;
    // NOT calling prepare()

    auto ctx = createTestContext();
    std::array<float, 512> buffer;
    generateImpulse(buffer.data(), buffer.size());

    // Should return without processing (and not crash)
    REQUIRE_NOTHROW(network.process(buffer.data(), buffer.size(), ctx));
}

// =============================================================================
// US2: Self-Oscillation Mode Tests
// =============================================================================

TEST_CASE("FeedbackNetwork accepts feedback values up to 120%", "[feedback][US2]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);

    // Setting 120% feedback should work
    network.setFeedbackAmount(1.2f);
    REQUIRE(network.getFeedbackAmount() == Approx(1.2f));

    // Values above 120% should be clamped
    network.setFeedbackAmount(1.5f);
    REQUIRE(network.getFeedbackAmount() == Approx(1.2f));
}

TEST_CASE("FeedbackNetwork saturation can be enabled/disabled", "[feedback][US2]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);

    // Default is disabled
    REQUIRE_FALSE(network.isSaturationEnabled());

    // Can enable
    network.setSaturationEnabled(true);
    REQUIRE(network.isSaturationEnabled());

    // Can disable
    network.setSaturationEnabled(false);
    REQUIRE_FALSE(network.isSaturationEnabled());
}

TEST_CASE("FeedbackNetwork 120% feedback with saturation keeps output bounded", "[feedback][US2][SC-003]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(10.0f);  // 10ms = 441 samples (short for faster test)
    network.setFeedbackAmount(1.2f);  // 120% feedback
    network.setSaturationEnabled(true);  // Enable saturation to limit signal

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    // Process for ~1 second to let oscillation build up
    constexpr size_t kNumBlocks = 100;

    // Start with an impulse
    float maxOutputSeen = 0.0f;

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> buffer = {};
        if (block == 0) {
            buffer[0] = 1.0f;  // Impulse
        }
        network.process(buffer.data(), buffer.size(), ctx);

        // Track maximum output
        for (size_t i = 0; i < kBlockSize; ++i) {
            maxOutputSeen = std::max(maxOutputSeen, std::abs(buffer[i]));
        }
    }

    // SC-003: Output should be bounded below 2.0 (saturation limits growth)
    REQUIRE(maxOutputSeen < 2.0f);
    // Should still have significant output (oscillation is happening)
    REQUIRE(maxOutputSeen > 0.5f);
}

TEST_CASE("FeedbackNetwork self-oscillation builds up over repeats", "[feedback][US2]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(10.0f);  // 10ms delay
    network.setFeedbackAmount(1.2f);  // 120% feedback for stronger growth
    network.setSaturationEnabled(true);

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    constexpr size_t kDelayInSamples = 441;

    // Use smaller initial impulse so signal can grow before hitting saturation
    std::vector<float> allOutput;
    allOutput.reserve(kBlockSize * 30);

    for (size_t block = 0; block < 30; ++block) {
        std::array<float, kBlockSize> buffer = {};
        if (block == 0) {
            buffer[0] = 0.1f;  // Small impulse to allow growth
        }
        network.process(buffer.data(), buffer.size(), ctx);
        allOutput.insert(allOutput.end(), buffer.begin(), buffer.end());
    }

    // Find peaks at successive delay intervals
    auto findPeakAround = [&](size_t centerSample) {
        float peak = 0.0f;
        size_t start = (centerSample > 50) ? centerSample - 50 : 0;
        size_t end = std::min(centerSample + 50, allOutput.size());
        for (size_t i = start; i < end; ++i) {
            peak = std::max(peak, std::abs(allOutput[i]));
        }
        return peak;
    };

    float repeat1 = findPeakAround(kDelayInSamples);
    float repeat2 = findPeakAround(2 * kDelayInSamples);
    float repeat3 = findPeakAround(3 * kDelayInSamples);

    // With 120% feedback and small initial signal:
    // Signal should grow in early repeats before saturation limits it
    // tanh(0.1) ≈ 0.0997, so ~100% of signal passes through
    // With 120% feedback: 0.1 * 1.2 = 0.12, then 0.12 * 1.2 = 0.144, etc.
    REQUIRE(repeat1 > 0.05f);  // First repeat present
    REQUIRE(repeat2 > repeat1 * 1.05f);  // Second repeat grows (at least 5% larger)
    REQUIRE(repeat3 > repeat2 * 1.0f);   // Third repeat at least same or larger
}

TEST_CASE("FeedbackNetwork saturation provides soft limiting", "[feedback][US2]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(5.0f);  // Very short delay for fast oscillation
    network.setFeedbackAmount(1.2f);  // 120% feedback
    network.setSaturationEnabled(true);

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    // Process for many blocks to reach steady-state oscillation
    constexpr size_t kNumBlocks = 200;

    std::array<float, kBlockSize> buffer = {};
    float lastMaxDelta = 0.0f;

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        if (block == 0) {
            buffer[0] = 1.0f;
        }
        network.process(buffer.data(), buffer.size(), ctx);

        // In steady state, check for soft limiting (no harsh clipping)
        if (block > 150) {
            // Find maximum sample-to-sample delta
            for (size_t i = 1; i < kBlockSize; ++i) {
                float delta = std::abs(buffer[i] - buffer[i-1]);
                lastMaxDelta = std::max(lastMaxDelta, delta);
            }
        }
    }

    // Soft saturation means gradual transitions, not hard clips
    // Hard clipping would create very large deltas when signal hits limit
    // With tape saturation, deltas should be smoother
    REQUIRE(lastMaxDelta < 0.5f);  // No extreme jumps
}

TEST_CASE("FeedbackNetwork output remains bounded after long oscillation", "[feedback][US2][SC-003]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(10.0f);
    network.setFeedbackAmount(1.2f);
    network.setSaturationEnabled(true);

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    // Process for ~5 seconds at 44.1kHz
    constexpr size_t kNumBlocks = 450;  // 450 * 512 / 44100 ≈ 5 seconds

    std::array<float, kBlockSize> buffer = {};

    // Feed impulse at start
    buffer[0] = 1.0f;
    network.process(buffer.data(), buffer.size(), ctx);

    // Continue processing for several seconds
    float maxOutput = 0.0f;
    for (size_t block = 1; block < kNumBlocks; ++block) {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        network.process(buffer.data(), buffer.size(), ctx);

        // Track maximum in last second of processing
        if (block > 350) {
            for (size_t i = 0; i < kBlockSize; ++i) {
                maxOutput = std::max(maxOutput, std::abs(buffer[i]));
            }
        }
    }

    // Output should still be bounded (saturation preventing runaway)
    REQUIRE(maxOutput < 2.0f);
}
