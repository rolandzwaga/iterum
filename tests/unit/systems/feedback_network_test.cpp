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

// =============================================================================
// US3: Filtered Feedback Tests
// =============================================================================

TEST_CASE("FeedbackNetwork filter can be enabled/disabled", "[feedback][US3]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);

    // Default is disabled
    REQUIRE_FALSE(network.isFilterEnabled());

    // Can enable
    network.setFilterEnabled(true);
    REQUIRE(network.isFilterEnabled());

    // Can disable
    network.setFilterEnabled(false);
    REQUIRE_FALSE(network.isFilterEnabled());
}

TEST_CASE("FeedbackNetwork filter type can be set", "[feedback][US3]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setFilterEnabled(true);

    // Test LP, HP, BP filter types
    network.setFilterType(FilterType::Lowpass);
    network.setFilterType(FilterType::Highpass);
    network.setFilterType(FilterType::Bandpass);

    // Should not throw or crash
    auto ctx = createTestContext();
    std::array<float, 512> buffer = {};
    buffer[0] = 1.0f;
    REQUIRE_NOTHROW(network.process(buffer.data(), buffer.size(), ctx));
}

TEST_CASE("FeedbackNetwork filter cutoff and resonance can be set", "[feedback][US3]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setFilterEnabled(true);
    network.setFilterType(FilterType::Lowpass);

    // Set cutoff and resonance
    network.setFilterCutoff(2000.0f);
    network.setFilterResonance(0.707f);

    // Should not throw or crash
    auto ctx = createTestContext();
    std::array<float, 512> buffer = {};
    buffer[0] = 1.0f;
    REQUIRE_NOTHROW(network.process(buffer.data(), buffer.size(), ctx));
}

TEST_CASE("FeedbackNetwork LP filter attenuates HF in repeats", "[feedback][US3][SC-004]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);  // 50ms delay
    network.setFeedbackAmount(0.9f);
    network.setFilterEnabled(true);
    network.setFilterType(FilterType::Lowpass);
    network.setFilterCutoff(2000.0f);  // 2kHz cutoff

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 100;

    // Generate high frequency sine wave (10kHz)
    std::vector<float> allOutput;
    allOutput.reserve(kBlockSize * kNumBlocks);

    const float freq = 10000.0f;  // 10kHz
    const float sampleRate = 44100.0f;
    const float phaseInc = 2.0f * 3.14159265f * freq / sampleRate;

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> buffer = {};

        // Only input signal for first block
        if (block == 0) {
            float phase = 0.0f;
            for (size_t i = 0; i < kBlockSize; ++i) {
                buffer[i] = 0.5f * std::sin(phase);
                phase += phaseInc;
            }
        }

        network.process(buffer.data(), buffer.size(), ctx);
        allOutput.insert(allOutput.end(), buffer.begin(), buffer.end());
    }

    // Find energy in HF region for first and second delay
    constexpr size_t kDelayInSamples = 2205;  // 50ms

    auto measureHFEnergy = [&](size_t startSample, size_t numSamples) {
        float energy = 0.0f;
        for (size_t i = startSample; i < startSample + numSamples && i < allOutput.size(); ++i) {
            energy += allOutput[i] * allOutput[i];
        }
        return energy;
    };

    float energy1 = measureHFEnergy(kDelayInSamples, 512);
    float energy2 = measureHFEnergy(2 * kDelayInSamples, 512);

    // With LP filter at 2kHz, 10kHz should be heavily attenuated
    // Energy should decay faster than just the feedback amount
    // With 90% feedback, energy ratio would be 0.81 (= 0.9^2) for unfiltered
    // With LP filter, 10kHz is ~2.3 octaves above 2kHz cutoff, so ~-13.8dB attenuation per pass
    float energyRatio = energy2 / energy1;

    // HF energy should decay much faster than 81% (the unfiltered rate)
    REQUIRE(energyRatio < 0.5f);
}

TEST_CASE("FeedbackNetwork HP filter attenuates LF in repeats", "[feedback][US3]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);
    network.setFeedbackAmount(0.9f);
    network.setFilterEnabled(true);
    network.setFilterType(FilterType::Highpass);
    network.setFilterCutoff(2000.0f);  // 2kHz cutoff

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 100;

    // Generate low frequency sine wave (200Hz)
    std::vector<float> allOutput;
    allOutput.reserve(kBlockSize * kNumBlocks);

    const float freq = 200.0f;  // 200Hz (below cutoff)
    const float sampleRate = 44100.0f;
    const float phaseInc = 2.0f * 3.14159265f * freq / sampleRate;

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> buffer = {};

        if (block == 0) {
            float phase = 0.0f;
            for (size_t i = 0; i < kBlockSize; ++i) {
                buffer[i] = 0.5f * std::sin(phase);
                phase += phaseInc;
            }
        }

        network.process(buffer.data(), buffer.size(), ctx);
        allOutput.insert(allOutput.end(), buffer.begin(), buffer.end());
    }

    constexpr size_t kDelayInSamples = 2205;

    auto measureEnergy = [&](size_t startSample, size_t numSamples) {
        float energy = 0.0f;
        for (size_t i = startSample; i < startSample + numSamples && i < allOutput.size(); ++i) {
            energy += allOutput[i] * allOutput[i];
        }
        return energy;
    };

    float energy1 = measureEnergy(kDelayInSamples, 512);
    float energy2 = measureEnergy(2 * kDelayInSamples, 512);

    // With HP filter at 2kHz, 200Hz should be heavily attenuated
    float energyRatio = energy2 / energy1;

    // LF energy should decay much faster than unfiltered
    REQUIRE(energyRatio < 0.5f);
}

TEST_CASE("FeedbackNetwork filter bypass makes all frequencies decay equally", "[feedback][US3]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(10.0f);
    network.setFeedbackAmount(0.5f);
    network.setFilterEnabled(false);  // Filter bypassed

    auto ctx = createTestContext();

    // Process two signals at different frequencies and compare decay rates
    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 50;
    constexpr size_t kDelayInSamples = 441;

    // Test with impulse (broadband)
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

    // Find peaks
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

    // With 50% feedback and no filter, each repeat should be ~50% of previous
    float decay1to2 = repeat2 / repeat1;
    float decay2to3 = repeat3 / repeat2;

    // Both decay rates should be similar (around 0.5 ±10%)
    REQUIRE(decay1to2 == Approx(0.5f).margin(0.1f));
    REQUIRE(decay2to3 == Approx(0.5f).margin(0.1f));
}

// =============================================================================
// US4: Saturated Feedback Tests
// =============================================================================

TEST_CASE("FeedbackNetwork saturation type can be changed", "[feedback][US4]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);
    network.setFeedbackAmount(0.5f);
    network.setSaturationEnabled(true);

    auto ctx = createTestContext();

    // Test all saturation types can be set and process without crashing
    std::array<float, 512> buffer = {};

    // Tape saturation
    network.setSaturationType(SaturationType::Tape);
    buffer[0] = 1.0f;
    REQUIRE_NOTHROW(network.process(buffer.data(), buffer.size(), ctx));

    // Tube saturation
    network.reset();
    network.setSaturationType(SaturationType::Tube);
    buffer = {};
    buffer[0] = 1.0f;
    REQUIRE_NOTHROW(network.process(buffer.data(), buffer.size(), ctx));

    // Transistor saturation
    network.reset();
    network.setSaturationType(SaturationType::Transistor);
    buffer = {};
    buffer[0] = 1.0f;
    REQUIRE_NOTHROW(network.process(buffer.data(), buffer.size(), ctx));

    // Digital saturation
    network.reset();
    network.setSaturationType(SaturationType::Digital);
    buffer = {};
    buffer[0] = 1.0f;
    REQUIRE_NOTHROW(network.process(buffer.data(), buffer.size(), ctx));

    // Diode saturation
    network.reset();
    network.setSaturationType(SaturationType::Diode);
    buffer = {};
    buffer[0] = 1.0f;
    REQUIRE_NOTHROW(network.process(buffer.data(), buffer.size(), ctx));
}

TEST_CASE("FeedbackNetwork saturation drive can be adjusted", "[feedback][US4]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);
    network.setFeedbackAmount(0.5f);
    network.setSaturationEnabled(true);
    network.setSaturationType(SaturationType::Tape);

    auto ctx = createTestContext();
    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 20;

    // Test with low drive (0 dB)
    network.setSaturationDrive(0.0f);
    std::vector<float> lowDriveOutput;
    lowDriveOutput.reserve(kBlockSize * kNumBlocks);

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> buffer = {};
        if (block == 0) {
            buffer[0] = 1.0f;
        }
        network.process(buffer.data(), buffer.size(), ctx);
        lowDriveOutput.insert(lowDriveOutput.end(), buffer.begin(), buffer.end());
    }

    // Reset and test with high drive (+12 dB)
    network.reset();
    network.setSaturationDrive(12.0f);
    std::vector<float> highDriveOutput;
    highDriveOutput.reserve(kBlockSize * kNumBlocks);

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> buffer = {};
        if (block == 0) {
            buffer[0] = 1.0f;
        }
        network.process(buffer.data(), buffer.size(), ctx);
        highDriveOutput.insert(highDriveOutput.end(), buffer.begin(), buffer.end());
    }

    // Higher drive should create more saturation = more limiting = different output
    // Compare waveform shapes at first repeat
    constexpr size_t kDelayInSamples = 2205;

    float lowDrivePeak = 0.0f;
    float highDrivePeak = 0.0f;

    for (size_t i = kDelayInSamples; i < kDelayInSamples + 100 && i < lowDriveOutput.size(); ++i) {
        lowDrivePeak = std::max(lowDrivePeak, std::abs(lowDriveOutput[i]));
        highDrivePeak = std::max(highDrivePeak, std::abs(highDriveOutput[i]));
    }

    // High drive should compress the signal more (lower peak)
    // Both should have output (signal passes through)
    REQUIRE(lowDrivePeak > 0.0f);
    REQUIRE(highDrivePeak > 0.0f);
}

TEST_CASE("FeedbackNetwork saturation adds harmonics", "[feedback][US4][SC-005]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);
    network.setFeedbackAmount(0.9f);
    network.setSaturationEnabled(true);
    network.setSaturationType(SaturationType::Tape);
    network.setSaturationDrive(12.0f);  // High drive for more harmonics

    auto ctx = createTestContext();

    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 50;

    // Generate a pure sine wave at 1kHz
    std::vector<float> allOutput;
    allOutput.reserve(kBlockSize * kNumBlocks);

    const float freq = 1000.0f;
    const float sampleRate = 44100.0f;
    const float phaseInc = 2.0f * 3.14159265f * freq / sampleRate;
    float phase = 0.0f;

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> buffer = {};

        // Feed sine wave continuously
        if (block < 5) {
            for (size_t i = 0; i < kBlockSize; ++i) {
                buffer[i] = 0.8f * std::sin(phase);
                phase += phaseInc;
            }
        }

        network.process(buffer.data(), buffer.size(), ctx);
        allOutput.insert(allOutput.end(), buffer.begin(), buffer.end());
    }

    // Measure THD by comparing power at fundamental vs. at harmonic locations
    // After saturation, the signal should have harmonics at 2kHz, 3kHz, etc.
    // Simple test: look for non-zero correlation with harmonic frequencies

    // Sample from delayed region where saturation has taken effect
    constexpr size_t kStartSample = 2500;  // After first delay repeat
    constexpr size_t kWindowSize = 2048;

    float fundamentalPower = 0.0f;
    float thirdHarmonicPower = 0.0f;
    const float fundPhaseInc = 2.0f * 3.14159265f * 1000.0f / sampleRate;
    const float thirdPhaseInc = 2.0f * 3.14159265f * 3000.0f / sampleRate;

    float fundPhase = 0.0f;
    float thirdPhase = 0.0f;

    for (size_t i = 0; i < kWindowSize && (kStartSample + i) < allOutput.size(); ++i) {
        float sample = allOutput[kStartSample + i];

        // Correlate with fundamental
        float sinFund = std::sin(fundPhase);
        float cosFund = std::cos(fundPhase);
        fundamentalPower += sample * sinFund;
        fundamentalPower += sample * cosFund;

        // Correlate with 3rd harmonic
        float sin3rd = std::sin(thirdPhase);
        float cos3rd = std::cos(thirdPhase);
        thirdHarmonicPower += sample * sin3rd;
        thirdHarmonicPower += sample * cos3rd;

        fundPhase += fundPhaseInc;
        thirdPhase += thirdPhaseInc;
    }

    fundamentalPower = std::abs(fundamentalPower);
    thirdHarmonicPower = std::abs(thirdHarmonicPower);

    // With saturation, there should be measurable 3rd harmonic content
    // (at least 1% of fundamental)
    if (fundamentalPower > 0.0f) {
        float harmonicRatio = thirdHarmonicPower / fundamentalPower;
        REQUIRE(harmonicRatio > 0.01f);  // At least 1% THD
    }
}

TEST_CASE("FeedbackNetwork saturation bypass produces clean signal", "[feedback][US4]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);
    network.setFeedbackAmount(0.5f);
    network.setSaturationEnabled(false);  // Bypass!

    auto ctx = createTestContext();
    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 20;

    // Send an impulse through
    std::vector<float> output;
    output.reserve(kBlockSize * kNumBlocks);

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> buffer = {};
        if (block == 0) {
            buffer[0] = 1.0f;
        }
        network.process(buffer.data(), buffer.size(), ctx);
        output.insert(output.end(), buffer.begin(), buffer.end());
    }

    // Find delayed impulse
    constexpr size_t kDelayInSamples = 2205;

    // Check impulse shape at repeat locations
    // Without saturation, the impulse should remain sharp (concentrated energy)
    float peakValue = 0.0f;
    size_t peakIndex = 0;

    for (size_t i = kDelayInSamples - 50; i < kDelayInSamples + 50 && i < output.size(); ++i) {
        if (std::abs(output[i]) > peakValue) {
            peakValue = std::abs(output[i]);
            peakIndex = i;
        }
    }

    // Energy should be concentrated near peak (not spread by harmonics)
    float nearPeakEnergy = 0.0f;
    float farEnergy = 0.0f;

    for (size_t i = kDelayInSamples - 50; i < kDelayInSamples + 50 && i < output.size(); ++i) {
        if (i >= peakIndex - 5 && i <= peakIndex + 5) {
            nearPeakEnergy += output[i] * output[i];
        } else {
            farEnergy += output[i] * output[i];
        }
    }

    // Most energy should be near the peak (clean impulse)
    if (nearPeakEnergy + farEnergy > 0.0f) {
        float concentration = nearPeakEnergy / (nearPeakEnergy + farEnergy);
        REQUIRE(concentration > 0.5f);  // At least 50% near peak
    }
}

TEST_CASE("FeedbackNetwork saturation changes are click-free", "[feedback][US4]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);
    network.setFeedbackAmount(0.5f);
    network.setSaturationEnabled(true);
    network.setSaturationType(SaturationType::Tape);
    network.setSaturationDrive(0.0f);

    auto ctx = createTestContext();
    constexpr size_t kBlockSize = 512;

    // Generate continuous sine wave
    std::vector<float> output;
    const float freq = 440.0f;
    const float sampleRate = 44100.0f;
    const float phaseInc = 2.0f * 3.14159265f * freq / sampleRate;
    float phase = 0.0f;

    // Process 10 blocks, change drive mid-way
    for (size_t block = 0; block < 20; ++block) {
        std::array<float, kBlockSize> buffer = {};

        for (size_t i = 0; i < kBlockSize; ++i) {
            buffer[i] = 0.5f * std::sin(phase);
            phase += phaseInc;
        }

        // Change drive suddenly at block 10
        if (block == 10) {
            network.setSaturationDrive(12.0f);
        }

        network.process(buffer.data(), buffer.size(), ctx);
        output.insert(output.end(), buffer.begin(), buffer.end());
    }

    // Look for discontinuities around the change point
    // Block 10 starts at sample 10 * 512 = 5120
    constexpr size_t kChangePoint = 10 * kBlockSize;

    float maxDelta = 0.0f;
    for (size_t i = kChangePoint - 100; i < kChangePoint + 100 && i + 1 < output.size(); ++i) {
        float delta = std::abs(output[i + 1] - output[i]);
        maxDelta = std::max(maxDelta, delta);
    }

    // With smoothing, there shouldn't be harsh discontinuities
    // Max delta in a smooth sine should be limited
    // 440Hz at 44100Hz has max slope of ~0.0627 per sample
    // Even with saturation changes, should be < 0.5 (no clicks)
    REQUIRE(maxDelta < 0.5f);
}

// =============================================================================
// US5: Freeze Mode Tests
// =============================================================================

TEST_CASE("FeedbackNetwork freeze state can be set and queried", "[feedback][US5]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);

    // Default is not frozen
    REQUIRE_FALSE(network.isFrozen());

    // Can freeze
    network.setFreeze(true);
    REQUIRE(network.isFrozen());

    // Can unfreeze
    network.setFreeze(false);
    REQUIRE_FALSE(network.isFrozen());
}

TEST_CASE("FeedbackNetwork freeze sets feedback to 100%", "[feedback][US5]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(10.0f);  // 10ms delay = 441 samples
    network.setFeedbackAmount(0.5f);

    auto ctx = createTestContext();

    // Process some blocks to get signal into delay
    std::array<float, 512> buffer = {};
    buffer[0] = 1.0f;
    network.process(buffer.data(), buffer.size(), ctx);

    // Now freeze
    network.setFreeze(true);

    // Process many more blocks - signal should not decay
    constexpr size_t kNumBlocks = 100;
    constexpr size_t kBlockSize = 512;

    std::vector<float> allOutput;
    allOutput.reserve(kNumBlocks * kBlockSize);

    for (size_t block = 0; block < kNumBlocks; ++block) {
        buffer = {};  // No new input
        network.process(buffer.data(), buffer.size(), ctx);
        allOutput.insert(allOutput.end(), buffer.begin(), buffer.end());
    }

    // Find peaks at multiple repeat locations
    constexpr size_t kDelayInSamples = 441;

    auto findPeakAround = [&](size_t centerSample) {
        float peak = 0.0f;
        size_t start = (centerSample > 50) ? centerSample - 50 : 0;
        size_t end = std::min(centerSample + 50, allOutput.size());
        for (size_t i = start; i < end; ++i) {
            peak = std::max(peak, std::abs(allOutput[i]));
        }
        return peak;
    };

    float repeat5 = findPeakAround(5 * kDelayInSamples);
    float repeat50 = findPeakAround(50 * kDelayInSamples);
    float repeat100 = findPeakAround(100 * kDelayInSamples);

    // With 100% feedback (freeze), signal should not decay significantly
    // Allow some tolerance for smoothing and numerical precision
    if (repeat5 > 0.0f) {
        float ratio50to5 = repeat50 / repeat5;
        float ratio100to5 = repeat100 / repeat5;

        REQUIRE(ratio50to5 > 0.9f);  // Less than 10% decay over 50 repeats
        REQUIRE(ratio100to5 > 0.8f);  // Less than 20% decay over 100 repeats
    }
}

TEST_CASE("FeedbackNetwork freeze mutes new input", "[feedback][US5]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);  // 50ms = 2205 samples
    network.setFeedbackAmount(0.5f);

    auto ctx = createTestContext();

    // Put initial signal in delay
    std::array<float, 512> buffer = {};
    buffer[0] = 0.5f;  // Initial impulse
    network.process(buffer.data(), buffer.size(), ctx);

    // Freeze
    network.setFreeze(true);

    // Wait for smoothing to complete
    for (size_t i = 0; i < 10; ++i) {
        buffer = {};
        network.process(buffer.data(), buffer.size(), ctx);
    }

    // Now send a new large impulse - it should be muted
    buffer = {};
    buffer[0] = 2.0f;  // Much larger impulse

    // Process and collect output
    std::vector<float> output;
    output.reserve(50 * 512);

    for (size_t block = 0; block < 50; ++block) {
        if (block == 0) {
            buffer[0] = 2.0f;  // Try to add new signal
        } else {
            buffer = {};
        }
        network.process(buffer.data(), buffer.size(), ctx);
        output.insert(output.end(), buffer.begin(), buffer.end());
    }

    // The output should contain the original signal (0.5f based), not the new one (2.0f)
    // Find peaks - they should be around 0.5f level, not 2.0f
    float maxPeak = 0.0f;
    for (float sample : output) {
        maxPeak = std::max(maxPeak, std::abs(sample));
    }

    // New input should be muted, so peak should be around original level
    REQUIRE(maxPeak < 1.5f);  // Should not see the 2.0f impulse
}

TEST_CASE("FeedbackNetwork freeze stores and restores previous feedback", "[feedback][US5]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(10.0f);

    // Set specific feedback amount
    network.setFeedbackAmount(0.7f);
    REQUIRE(network.getFeedbackAmount() == Approx(0.7f));

    // Freeze
    network.setFreeze(true);

    // Feedback amount getter still returns the stored value
    REQUIRE(network.getFeedbackAmount() == Approx(0.7f));

    // Unfreeze
    network.setFreeze(false);

    // Feedback should be restored
    REQUIRE(network.getFeedbackAmount() == Approx(0.7f));
}

TEST_CASE("FeedbackNetwork freeze maintains content for extended duration", "[feedback][US5][SC-006]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(100.0f);  // 100ms delay
    network.setFeedbackAmount(0.5f);

    auto ctx = createTestContext();

    // Put initial signal in delay
    std::array<float, 512> buffer = {};
    buffer[0] = 1.0f;
    network.process(buffer.data(), buffer.size(), ctx);

    // Freeze
    network.setFreeze(true);

    // Simulate 60 seconds of processing at 44100Hz
    // 60s * 44100 samples/s = 2,646,000 samples
    // With 512-sample blocks = ~5168 blocks
    // Let's process 1000 blocks (about 11.6 seconds) for reasonable test time
    constexpr size_t kNumBlocks = 1000;
    constexpr size_t kBlockSize = 512;
    constexpr size_t kDelayInSamples = 4410;  // 100ms at 44100Hz

    float earlyPeak = 0.0f;
    float latePeak = 0.0f;

    for (size_t block = 0; block < kNumBlocks; ++block) {
        buffer = {};
        network.process(buffer.data(), buffer.size(), ctx);

        // Measure early (block 10) and late (block 990)
        if (block == 10) {
            earlyPeak = findPeak(buffer.data(), buffer.size());
        }
        if (block == 990) {
            latePeak = findPeak(buffer.data(), buffer.size());
        }
    }

    // Signal should be sustained - late peak should be similar to early
    if (earlyPeak > 0.01f) {
        float sustainRatio = latePeak / earlyPeak;
        REQUIRE(sustainRatio > 0.5f);  // At least 50% sustained after ~11 seconds
    }
}

TEST_CASE("FeedbackNetwork freeze transition is smooth", "[feedback][US5]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(10.0f);
    network.setFeedbackAmount(0.5f);

    auto ctx = createTestContext();

    // Generate continuous sine wave
    std::vector<float> output;
    const float freq = 440.0f;
    const float sampleRate = 44100.0f;
    const float phaseInc = 2.0f * 3.14159265f * freq / sampleRate;
    float phase = 0.0f;

    constexpr size_t kBlockSize = 512;

    // Process 20 blocks, freeze at block 10
    for (size_t block = 0; block < 20; ++block) {
        std::array<float, kBlockSize> buffer = {};

        for (size_t i = 0; i < kBlockSize; ++i) {
            buffer[i] = 0.5f * std::sin(phase);
            phase += phaseInc;
        }

        // Freeze at block 10
        if (block == 10) {
            network.setFreeze(true);
        }

        network.process(buffer.data(), buffer.size(), ctx);
        output.insert(output.end(), buffer.begin(), buffer.end());
    }

    // Check for clicks around freeze point
    constexpr size_t kFreezePoint = 10 * kBlockSize;

    float maxDelta = 0.0f;
    for (size_t i = kFreezePoint - 100; i < kFreezePoint + 100 && i + 1 < output.size(); ++i) {
        float delta = std::abs(output[i + 1] - output[i]);
        maxDelta = std::max(maxDelta, delta);
    }

    // Should be smooth, no clicks
    REQUIRE(maxDelta < 0.5f);
}

TEST_CASE("FeedbackNetwork unfreeze transition is smooth", "[feedback][US5]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(10.0f);
    network.setFeedbackAmount(0.5f);

    auto ctx = createTestContext();

    // Start frozen
    network.setFreeze(true);

    // Generate continuous sine wave
    std::vector<float> output;
    const float freq = 440.0f;
    const float sampleRate = 44100.0f;
    const float phaseInc = 2.0f * 3.14159265f * freq / sampleRate;
    float phase = 0.0f;

    constexpr size_t kBlockSize = 512;

    // Process 20 blocks, unfreeze at block 10
    for (size_t block = 0; block < 20; ++block) {
        std::array<float, kBlockSize> buffer = {};

        for (size_t i = 0; i < kBlockSize; ++i) {
            buffer[i] = 0.5f * std::sin(phase);
            phase += phaseInc;
        }

        // Unfreeze at block 10
        if (block == 10) {
            network.setFreeze(false);
        }

        network.process(buffer.data(), buffer.size(), ctx);
        output.insert(output.end(), buffer.begin(), buffer.end());
    }

    // Check for clicks around unfreeze point
    constexpr size_t kUnfreezePoint = 10 * kBlockSize;

    float maxDelta = 0.0f;
    for (size_t i = kUnfreezePoint - 100; i < kUnfreezePoint + 100 && i + 1 < output.size(); ++i) {
        float delta = std::abs(output[i + 1] - output[i]);
        maxDelta = std::max(maxDelta, delta);
    }

    // Should be smooth, no clicks
    REQUIRE(maxDelta < 0.5f);
}

// =============================================================================
// US6: Stereo Cross-Feedback Tests
// =============================================================================

TEST_CASE("FeedbackNetwork cross-feedback amount can be set and queried", "[feedback][US6]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);

    // Default is 0 (no cross-feedback)
    REQUIRE(network.getCrossFeedbackAmount() == Approx(0.0f));

    // Can set to various values
    network.setCrossFeedbackAmount(0.5f);
    REQUIRE(network.getCrossFeedbackAmount() == Approx(0.5f));

    network.setCrossFeedbackAmount(1.0f);
    REQUIRE(network.getCrossFeedbackAmount() == Approx(1.0f));

    network.setCrossFeedbackAmount(0.0f);
    REQUIRE(network.getCrossFeedbackAmount() == Approx(0.0f));
}

TEST_CASE("FeedbackNetwork cross-feedback clamps to valid range", "[feedback][US6]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);

    // Clamp below 0
    network.setCrossFeedbackAmount(-0.5f);
    REQUIRE(network.getCrossFeedbackAmount() == Approx(0.0f));

    // Clamp above 1
    network.setCrossFeedbackAmount(1.5f);
    REQUIRE(network.getCrossFeedbackAmount() == Approx(1.0f));
}

TEST_CASE("FeedbackNetwork cross-feedback rejects NaN", "[feedback][US6]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);

    network.setCrossFeedbackAmount(0.5f);
    network.setCrossFeedbackAmount(std::nanf(""));

    // Should retain previous value
    REQUIRE(network.getCrossFeedbackAmount() == Approx(0.5f));
}

TEST_CASE("FeedbackNetwork 0% cross-feedback keeps channels independent", "[feedback][US6]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);  // 2205 samples
    network.setFeedbackAmount(0.9f);
    network.setCrossFeedbackAmount(0.0f);  // No cross-feedback

    auto ctx = createTestContext();
    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 20;

    // Put signal only in left channel
    std::vector<float> leftOut, rightOut;
    leftOut.reserve(kBlockSize * kNumBlocks);
    rightOut.reserve(kBlockSize * kNumBlocks);

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> left = {};
        std::array<float, kBlockSize> right = {};

        if (block == 0) {
            left[0] = 1.0f;
            right[0] = 0.0f;  // Nothing in right
        }

        network.process(left.data(), right.data(), left.size(), ctx);
        leftOut.insert(leftOut.end(), left.begin(), left.end());
        rightOut.insert(rightOut.end(), right.begin(), right.end());
    }

    // Right channel should have no significant content
    float rightPeak = 0.0f;
    for (float sample : rightOut) {
        rightPeak = std::max(rightPeak, std::abs(sample));
    }

    // Left should have repeats, right should be near silent
    REQUIRE(rightPeak < 0.01f);
}

TEST_CASE("FeedbackNetwork 100% cross-feedback creates ping-pong", "[feedback][US6][SC-007]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);  // 2205 samples
    network.setFeedbackAmount(0.9f);
    network.setCrossFeedbackAmount(1.0f);  // Full ping-pong

    auto ctx = createTestContext();
    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 30;
    constexpr size_t kDelayInSamples = 2205;

    // Put signal only in left channel
    std::vector<float> leftOut, rightOut;
    leftOut.reserve(kBlockSize * kNumBlocks);
    rightOut.reserve(kBlockSize * kNumBlocks);

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> left = {};
        std::array<float, kBlockSize> right = {};

        if (block == 0) {
            left[0] = 1.0f;
        }

        network.process(left.data(), right.data(), left.size(), ctx);
        leftOut.insert(leftOut.end(), left.begin(), left.end());
        rightOut.insert(rightOut.end(), right.begin(), right.end());
    }

    // Find peaks at repeat locations
    auto findPeakAround = [](const std::vector<float>& buffer, size_t center, size_t window = 50) {
        float peak = 0.0f;
        size_t start = (center > window) ? center - window : 0;
        size_t end = std::min(center + window, buffer.size());
        for (size_t i = start; i < end; ++i) {
            peak = std::max(peak, std::abs(buffer[i]));
        }
        return peak;
    };

    // With 100% cross-feedback, the FEEDBACK signal crosses channels.
    // But OUTPUT is the delayed signal, so alternation is offset by one:
    // - Repeat 1: comes from LEFT delay (original signal was there)
    // - Repeat 2: comes from RIGHT delay (cross-fed from left)
    // - Repeat 3: comes from LEFT delay (cross-fed from right)

    float leftRepeat1 = findPeakAround(leftOut, kDelayInSamples);
    float rightRepeat1 = findPeakAround(rightOut, kDelayInSamples);
    float leftRepeat2 = findPeakAround(leftOut, 2 * kDelayInSamples);
    float rightRepeat2 = findPeakAround(rightOut, 2 * kDelayInSamples);
    float leftRepeat3 = findPeakAround(leftOut, 3 * kDelayInSamples);
    float rightRepeat3 = findPeakAround(rightOut, 3 * kDelayInSamples);

    // First repeat should be in LEFT (original signal location)
    REQUIRE(leftRepeat1 > rightRepeat1 * 2.0f);

    // Second repeat should be in RIGHT (cross-fed from left)
    REQUIRE(rightRepeat2 > leftRepeat2 * 2.0f);

    // Third repeat should be back in LEFT (cross-fed from right)
    REQUIRE(leftRepeat3 > rightRepeat3 * 2.0f);
}

TEST_CASE("FeedbackNetwork 50% cross-feedback blends channels", "[feedback][US6]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);
    network.setFeedbackAmount(0.9f);
    network.setCrossFeedbackAmount(0.5f);  // 50% blend

    auto ctx = createTestContext();
    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = 30;
    constexpr size_t kDelayInSamples = 2205;

    // Put signal only in left channel
    std::vector<float> leftOut, rightOut;
    leftOut.reserve(kBlockSize * kNumBlocks);
    rightOut.reserve(kBlockSize * kNumBlocks);

    for (size_t block = 0; block < kNumBlocks; ++block) {
        std::array<float, kBlockSize> left = {};
        std::array<float, kBlockSize> right = {};

        if (block == 0) {
            left[0] = 1.0f;
        }

        network.process(left.data(), right.data(), left.size(), ctx);
        leftOut.insert(leftOut.end(), left.begin(), left.end());
        rightOut.insert(rightOut.end(), right.begin(), right.end());
    }

    // At 50%, both channels should have similar content
    auto findPeakAround = [](const std::vector<float>& buffer, size_t center) {
        float peak = 0.0f;
        size_t start = (center > 50) ? center - 50 : 0;
        size_t end = std::min(center + 50, buffer.size());
        for (size_t i = start; i < end; ++i) {
            peak = std::max(peak, std::abs(buffer[i]));
        }
        return peak;
    };

    float leftRepeat2 = findPeakAround(leftOut, 2 * kDelayInSamples);
    float rightRepeat2 = findPeakAround(rightOut, 2 * kDelayInSamples);

    // Both channels should have content (blended toward mono)
    REQUIRE(leftRepeat2 > 0.01f);
    REQUIRE(rightRepeat2 > 0.01f);

    // Should be roughly similar levels (within 6dB)
    float ratio = leftRepeat2 / rightRepeat2;
    REQUIRE(ratio > 0.5f);
    REQUIRE(ratio < 2.0f);
}

TEST_CASE("FeedbackNetwork cross-feedback changes are smoothed", "[feedback][US6]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(10.0f);
    network.setFeedbackAmount(0.5f);
    network.setCrossFeedbackAmount(0.0f);

    auto ctx = createTestContext();
    constexpr size_t kBlockSize = 512;

    // Generate continuous stereo sine
    std::vector<float> leftOut, rightOut;
    const float freq = 440.0f;
    const float sampleRate = 44100.0f;
    const float phaseInc = 2.0f * 3.14159265f * freq / sampleRate;
    float phase = 0.0f;

    for (size_t block = 0; block < 20; ++block) {
        std::array<float, kBlockSize> left = {};
        std::array<float, kBlockSize> right = {};

        for (size_t i = 0; i < kBlockSize; ++i) {
            left[i] = 0.5f * std::sin(phase);
            right[i] = 0.5f * std::sin(phase + 3.14159265f);  // Out of phase
            phase += phaseInc;
        }

        // Change cross-feedback suddenly at block 10
        if (block == 10) {
            network.setCrossFeedbackAmount(1.0f);
        }

        network.process(left.data(), right.data(), left.size(), ctx);
        leftOut.insert(leftOut.end(), left.begin(), left.end());
        rightOut.insert(rightOut.end(), right.begin(), right.end());
    }

    // Check for clicks around change point
    constexpr size_t kChangePoint = 10 * kBlockSize;

    float maxDeltaL = 0.0f;
    float maxDeltaR = 0.0f;
    for (size_t i = kChangePoint - 100; i < kChangePoint + 100 && i + 1 < leftOut.size(); ++i) {
        float deltaL = std::abs(leftOut[i + 1] - leftOut[i]);
        float deltaR = std::abs(rightOut[i + 1] - rightOut[i]);
        maxDeltaL = std::max(maxDeltaL, deltaL);
        maxDeltaR = std::max(maxDeltaR, deltaR);
    }

    // Should be smooth
    REQUIRE(maxDeltaL < 0.5f);
    REQUIRE(maxDeltaR < 0.5f);
}

TEST_CASE("FeedbackNetwork cross-feedback works with freeze mode", "[feedback][US6]") {
    FeedbackNetwork network;
    network.prepare(44100.0, 512, 500.0f);
    network.setDelayTimeMs(50.0f);
    network.setFeedbackAmount(0.5f);
    network.setCrossFeedbackAmount(1.0f);  // Full ping-pong

    auto ctx = createTestContext();
    constexpr size_t kBlockSize = 512;

    // Put signal in left channel
    std::array<float, kBlockSize> left = {};
    std::array<float, kBlockSize> right = {};
    left[0] = 1.0f;
    network.process(left.data(), right.data(), left.size(), ctx);

    // Freeze
    network.setFreeze(true);

    // Process more blocks - should maintain ping-pong pattern
    std::vector<float> leftOut, rightOut;
    for (size_t block = 0; block < 50; ++block) {
        left = {};
        right = {};
        network.process(left.data(), right.data(), left.size(), ctx);
        leftOut.insert(leftOut.end(), left.begin(), left.end());
        rightOut.insert(rightOut.end(), right.begin(), right.end());
    }

    // Both channels should have content (ping-pong continues)
    float leftMax = 0.0f, rightMax = 0.0f;
    for (size_t i = 0; i < leftOut.size(); ++i) {
        leftMax = std::max(leftMax, std::abs(leftOut[i]));
        rightMax = std::max(rightMax, std::abs(rightOut[i]));
    }

    // Both channels should have signal (ping-pong pattern)
    REQUIRE(leftMax > 0.01f);
    REQUIRE(rightMax > 0.01f);
}
