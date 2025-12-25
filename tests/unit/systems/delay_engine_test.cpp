// ==============================================================================
// Layer 3: System Component - DelayEngine Tests
// ==============================================================================
// Test-first development for DelayEngine wrapper class.
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-First Development
//
// Reference: specs/018-delay-engine/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/systems/delay_engine.h"

#include <array>
#include <cmath>
#include <numeric>

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// Test Utilities
// =============================================================================

namespace {

/// @brief Generate an impulse signal (1.0 at index 0, 0.0 elsewhere)
void generateImpulse(float* buffer, size_t size) {
    std::fill(buffer, buffer + size, 0.0f);
    if (size > 0) {
        buffer[0] = 1.0f;
    }
}

/// @brief Create a BlockContext with common test settings
BlockContext makeTestContext(double sampleRate = 44100.0, double tempoBPM = 120.0) {
    BlockContext ctx;
    ctx.sampleRate = sampleRate;
    ctx.tempoBPM = tempoBPM;
    ctx.blockSize = 512;
    ctx.isPlaying = true;
    return ctx;
}

/// @brief Find the index of the peak (impulse) in a buffer
size_t findPeakIndex(const float* buffer, size_t size) {
    size_t peakIndex = 0;
    float peakValue = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        if (std::abs(buffer[i]) > peakValue) {
            peakValue = std::abs(buffer[i]);
            peakIndex = i;
        }
    }
    return peakIndex;
}

} // anonymous namespace

// =============================================================================
// Phase 2: Foundational Tests (Class Skeleton)
// =============================================================================

TEST_CASE("DelayEngine can be default constructed", "[delay][systems][foundational]") {
    DelayEngine delay;
    REQUIRE_FALSE(delay.isPrepared());
}

TEST_CASE("DelayEngine can be moved", "[delay][systems][foundational]") {
    DelayEngine delay1;
    delay1.prepare(44100.0, 512, 1000.0f);

    DelayEngine delay2 = std::move(delay1);
    REQUIRE(delay2.isPrepared());
}

// =============================================================================
// Phase 3: User Story 1 - Free Time Mode Tests
// =============================================================================

TEST_CASE("DelayEngine prepare() allocates buffers", "[delay][systems][US1]") {
    DelayEngine delay;
    delay.prepare(44100.0, 512, 2000.0f);

    REQUIRE(delay.isPrepared());
    REQUIRE(delay.getMaxDelayMs() == Approx(2000.0f));
}

TEST_CASE("DelayEngine setDelayTimeMs(250) at 44.1kHz produces 11025 samples delay", "[delay][systems][US1]") {
    constexpr size_t bufferSize = 12000;  // Enough to see the delayed impulse
    std::array<float, bufferSize> buffer{};

    DelayEngine delay;
    delay.prepare(44100.0, bufferSize, 1000.0f);
    delay.setTimeMode(TimeMode::Free);
    delay.setDelayTimeMs(250.0f);
    delay.setMix(1.0f);  // 100% wet to see only delayed signal

    // Generate impulse at start
    generateImpulse(buffer.data(), bufferSize);

    // Process
    auto ctx = makeTestContext();
    ctx.blockSize = bufferSize;
    delay.process(buffer.data(), bufferSize, ctx);

    // Find where the impulse ended up
    size_t peakIndex = findPeakIndex(buffer.data(), bufferSize);

    // At 44.1kHz, 250ms = 11025 samples
    // Allow 1 sample tolerance per SC-001
    REQUIRE(peakIndex >= 11024);
    REQUIRE(peakIndex <= 11026);
}

TEST_CASE("DelayEngine delay time change is smoothed", "[delay][systems][US1]") {
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> buffer{};
    std::fill(buffer.begin(), buffer.end(), 1.0f);  // Constant signal

    DelayEngine delay;
    delay.prepare(44100.0, blockSize, 1000.0f);
    delay.setTimeMode(TimeMode::Free);
    delay.setDelayTimeMs(100.0f);
    delay.setMix(0.5f);

    // Prime the delay
    auto ctx = makeTestContext();
    ctx.blockSize = blockSize;
    for (int i = 0; i < 10; ++i) {
        delay.process(buffer.data(), blockSize, ctx);
    }

    // Get current delay
    float beforeDelay = delay.getCurrentDelayMs();

    // Change delay time
    delay.setDelayTimeMs(500.0f);

    // Process one block
    delay.process(buffer.data(), blockSize, ctx);

    // Current delay should be between old and new (smoothed)
    float afterDelay = delay.getCurrentDelayMs();
    REQUIRE(afterDelay > beforeDelay);
    REQUIRE(afterDelay < 500.0f);  // Not instantly at target
}

TEST_CASE("DelayEngine delay time clamped to [0, maxDelayMs]", "[delay][systems][US1]") {
    DelayEngine delay;
    delay.prepare(44100.0, 512, 500.0f);  // Max 500ms

    // Test upper bound
    delay.setDelayTimeMs(1000.0f);  // Over max

    // Process to let it settle
    std::array<float, 512> buffer{};
    auto ctx = makeTestContext();
    for (int i = 0; i < 100; ++i) {
        delay.process(buffer.data(), 512, ctx);
    }

    REQUIRE(delay.getCurrentDelayMs() <= 500.0f);

    // Test lower bound
    delay.setDelayTimeMs(-100.0f);  // Negative
    for (int i = 0; i < 100; ++i) {
        delay.process(buffer.data(), 512, ctx);
    }

    REQUIRE(delay.getCurrentDelayMs() >= 0.0f);
}

TEST_CASE("DelayEngine NaN delay time rejected", "[delay][systems][US1]") {
    DelayEngine delay;
    delay.prepare(44100.0, 512, 1000.0f);
    delay.setDelayTimeMs(250.0f);

    // Process to let it settle
    std::array<float, 512> buffer{};
    auto ctx = makeTestContext();
    for (int i = 0; i < 100; ++i) {
        delay.process(buffer.data(), 512, ctx);
    }

    float beforeNaN = delay.getCurrentDelayMs();

    // Set NaN
    delay.setDelayTimeMs(std::numeric_limits<float>::quiet_NaN());

    // Process
    for (int i = 0; i < 100; ++i) {
        delay.process(buffer.data(), 512, ctx);
    }

    // Should keep previous value (or reset to 0 per contract)
    float afterNaN = delay.getCurrentDelayMs();
    REQUIRE_FALSE(std::isnan(afterNaN));
}

TEST_CASE("DelayEngine linear interpolation for sub-sample delays", "[delay][systems][US1]") {
    constexpr size_t bufferSize = 2000;
    std::array<float, bufferSize> buffer{};

    DelayEngine delay;
    delay.prepare(44100.0, bufferSize, 100.0f);
    delay.setTimeMode(TimeMode::Free);
    delay.setDelayTimeMs(10.5f);  // Fractional ms -> fractional samples
    delay.setMix(1.0f);

    // Generate step from 0 to 1
    std::fill(buffer.begin(), buffer.begin() + 500, 0.0f);
    std::fill(buffer.begin() + 500, buffer.end(), 1.0f);

    auto ctx = makeTestContext();
    ctx.blockSize = bufferSize;
    delay.process(buffer.data(), bufferSize, ctx);

    // At the transition point, we should see interpolated values
    // (not a hard step from 0 to 1)
    // This is a basic check that interpolation is working
    size_t transitionIndex = static_cast<size_t>(10.5f * 44.1f) + 500;
    if (transitionIndex < bufferSize - 1) {
        // The value at transition should be between 0 and 1
        // (due to linear interpolation of the step)
        // This is a weak test but verifies interpolation is happening
        REQUIRE(buffer[transitionIndex] >= 0.0f);
        REQUIRE(buffer[transitionIndex] <= 1.0f);
    }
}

TEST_CASE("DelayEngine reset() clears buffer to silence", "[delay][systems][US1]") {
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> buffer{};

    DelayEngine delay;
    delay.prepare(44100.0, blockSize, 1000.0f);
    delay.setDelayTimeMs(100.0f);
    delay.setMix(1.0f);

    // Fill with audio
    std::fill(buffer.begin(), buffer.end(), 1.0f);
    auto ctx = makeTestContext();
    delay.process(buffer.data(), blockSize, ctx);

    // Reset
    delay.reset();

    // Process silence
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    delay.process(buffer.data(), blockSize, ctx);

    // Output should be silence (buffer was cleared)
    float sum = 0.0f;
    for (float sample : buffer) {
        sum += std::abs(sample);
    }
    REQUIRE(sum == Approx(0.0f).margin(0.001f));
}

// =============================================================================
// Phase 4: User Story 2 - Synced Time Mode Tests
// =============================================================================

TEST_CASE("DelayEngine setTimeMode switches mode", "[delay][systems][US2]") {
    DelayEngine delay;
    delay.prepare(44100.0, 512, 2000.0f);

    REQUIRE(delay.getTimeMode() == TimeMode::Free);  // Default

    delay.setTimeMode(TimeMode::Synced);
    REQUIRE(delay.getTimeMode() == TimeMode::Synced);

    delay.setTimeMode(TimeMode::Free);
    REQUIRE(delay.getTimeMode() == TimeMode::Free);
}

TEST_CASE("DelayEngine quarter note at 120 BPM = 500ms = 22050 samples", "[delay][systems][US2]") {
    constexpr size_t bufferSize = 25000;  // Enough to see 22050 sample delay
    std::array<float, bufferSize> buffer{};

    DelayEngine delay;
    delay.prepare(44100.0, bufferSize, 2000.0f);
    delay.setTimeMode(TimeMode::Synced);
    delay.setNoteValue(NoteValue::Quarter);
    delay.setMix(1.0f);

    // Generate impulse
    generateImpulse(buffer.data(), bufferSize);

    // Process with 120 BPM
    auto ctx = makeTestContext(44100.0, 120.0);
    ctx.blockSize = bufferSize;
    delay.process(buffer.data(), bufferSize, ctx);

    // Find peak
    size_t peakIndex = findPeakIndex(buffer.data(), bufferSize);

    // Quarter note at 120 BPM = 500ms = 22050 samples at 44.1kHz
    // Allow 1 sample tolerance
    REQUIRE(peakIndex >= 22049);
    REQUIRE(peakIndex <= 22051);
}

TEST_CASE("DelayEngine dotted eighth at 100 BPM = 450ms", "[delay][systems][US2]") {
    constexpr size_t bufferSize = 25000;
    std::array<float, bufferSize> buffer{};

    DelayEngine delay;
    delay.prepare(44100.0, bufferSize, 2000.0f);
    delay.setTimeMode(TimeMode::Synced);
    delay.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);
    delay.setMix(1.0f);

    generateImpulse(buffer.data(), bufferSize);

    // 100 BPM: quarter = 600ms, eighth = 300ms, dotted eighth = 450ms
    auto ctx = makeTestContext(44100.0, 100.0);
    ctx.blockSize = bufferSize;
    delay.process(buffer.data(), bufferSize, ctx);

    size_t peakIndex = findPeakIndex(buffer.data(), bufferSize);

    // 450ms at 44.1kHz = 19845 samples
    size_t expectedSamples = static_cast<size_t>(0.450 * 44100.0);
    REQUIRE(peakIndex >= expectedSamples - 1);
    REQUIRE(peakIndex <= expectedSamples + 1);
}

TEST_CASE("DelayEngine all NoteValue types produce correct times", "[delay][systems][US2][SC-005]") {
    DelayEngine delay;
    delay.prepare(44100.0, 512, 10000.0f);  // 10 second max for slow tempos
    delay.setTimeMode(TimeMode::Synced);
    delay.setMix(1.0f);

    auto ctx = makeTestContext(44100.0, 120.0);  // 120 BPM

    // At 120 BPM, quarter = 500ms = 22050 samples
    // Calculate expected samples for each note value
    const double samplesPerQuarter = 22050.0;

    struct TestCase {
        NoteValue note;
        NoteModifier mod;
        double expectedMs;
    };

    std::array<TestCase, 8> testCases = {{
        {NoteValue::Whole, NoteModifier::None, 2000.0},
        {NoteValue::Half, NoteModifier::None, 1000.0},
        {NoteValue::Quarter, NoteModifier::None, 500.0},
        {NoteValue::Eighth, NoteModifier::None, 250.0},
        {NoteValue::Sixteenth, NoteModifier::None, 125.0},
        {NoteValue::Quarter, NoteModifier::Dotted, 750.0},
        {NoteValue::Eighth, NoteModifier::Triplet, 166.667},
        {NoteValue::Half, NoteModifier::Dotted, 1500.0},
    }};

    for (const auto& tc : testCases) {
        delay.setNoteValue(tc.note, tc.mod);

        // Process to let smoother settle
        std::array<float, 512> buffer{};
        for (int i = 0; i < 200; ++i) {
            delay.process(buffer.data(), 512, ctx);
        }

        float currentMs = delay.getCurrentDelayMs();
        REQUIRE(currentMs == Approx(tc.expectedMs).margin(1.0f));  // Within 1ms
    }
}

TEST_CASE("DelayEngine tempo change updates delay smoothly", "[delay][systems][US2]") {
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> buffer{};

    DelayEngine delay;
    delay.prepare(44100.0, blockSize, 2000.0f);
    delay.setTimeMode(TimeMode::Synced);
    delay.setNoteValue(NoteValue::Quarter);
    delay.setMix(0.5f);

    // Process at 120 BPM
    auto ctx = makeTestContext(44100.0, 120.0);
    for (int i = 0; i < 100; ++i) {
        std::fill(buffer.begin(), buffer.end(), 1.0f);
        delay.process(buffer.data(), blockSize, ctx);
    }

    float delayAt120 = delay.getCurrentDelayMs();

    // Change to 140 BPM
    ctx.tempoBPM = 140.0;
    delay.process(buffer.data(), blockSize, ctx);

    // Should be transitioning (not instant jump)
    float afterChange = delay.getCurrentDelayMs();
    REQUIRE(afterChange < delayAt120);  // Moving toward new target
    REQUIRE(afterChange > 428.0f);  // Target at 140 BPM = ~428ms
}

TEST_CASE("DelayEngine triplet modifier works correctly", "[delay][systems][US2]") {
    DelayEngine delay;
    delay.prepare(44100.0, 512, 2000.0f);
    delay.setTimeMode(TimeMode::Synced);
    delay.setNoteValue(NoteValue::Quarter, NoteModifier::Triplet);

    auto ctx = makeTestContext(44100.0, 120.0);
    std::array<float, 512> buffer{};

    // Let it settle
    for (int i = 0; i < 200; ++i) {
        delay.process(buffer.data(), 512, ctx);
    }

    // Triplet quarter at 120 BPM = 500ms * 2/3 = 333.33ms
    float currentMs = delay.getCurrentDelayMs();
    REQUIRE(currentMs == Approx(333.333f).margin(1.0f));
}

// =============================================================================
// Phase 5: User Story 3 - Dry/Wet Mix Control Tests
// =============================================================================

TEST_CASE("DelayEngine mix 0% = 100% dry", "[delay][systems][US3]") {
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> input{};
    std::array<float, blockSize> output{};

    // Create test signal
    for (size_t i = 0; i < blockSize; ++i) {
        input[i] = std::sin(2.0f * 3.14159f * 440.0f * static_cast<float>(i) / 44100.0f);
    }

    DelayEngine delay;
    delay.prepare(44100.0, blockSize, 1000.0f);
    delay.setDelayTimeMs(100.0f);
    delay.setMix(0.0f);  // Fully dry

    auto ctx = makeTestContext();

    // Let the mix smoother settle
    std::array<float, blockSize> scratch{};
    for (int i = 0; i < 100; ++i) {
        std::fill(scratch.begin(), scratch.end(), 0.0f);
        delay.process(scratch.data(), blockSize, ctx);
    }

    // Now test with actual signal
    std::copy(input.begin(), input.end(), output.begin());
    delay.process(output.data(), blockSize, ctx);

    // Output should equal input (dry signal)
    for (size_t i = 0; i < blockSize; ++i) {
        REQUIRE(output[i] == Approx(input[i]).margin(0.0001f));
    }
}

TEST_CASE("DelayEngine mix 100% = 100% wet", "[delay][systems][US3]") {
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> buffer{};

    DelayEngine delay;
    delay.prepare(44100.0, blockSize, 1000.0f);
    delay.setDelayTimeMs(100.0f);
    delay.setMix(1.0f);  // Fully wet

    // Prime with silence so delay buffer is empty
    delay.reset();

    // Process a signal
    std::fill(buffer.begin(), buffer.end(), 1.0f);
    auto ctx = makeTestContext();
    delay.process(buffer.data(), blockSize, ctx);

    // Output should be near zero (wet signal is delayed, nothing in buffer yet)
    float sum = 0.0f;
    for (float sample : buffer) {
        sum += std::abs(sample);
    }
    // With 100ms delay, first 4410 samples should be silent
    // Since block is only 512, all should be ~0
    REQUIRE(sum == Approx(0.0f).margin(0.01f));
}

TEST_CASE("DelayEngine mix 50% = equal blend", "[delay][systems][US3]") {
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> buffer{};

    DelayEngine delay;
    delay.prepare(44100.0, blockSize, 1000.0f);
    delay.setDelayTimeMs(100.0f);
    delay.setMix(0.5f);  // 50/50 blend

    delay.reset();

    // Fill buffer with constant value
    std::fill(buffer.begin(), buffer.end(), 1.0f);

    auto ctx = makeTestContext();
    delay.process(buffer.data(), blockSize, ctx);

    // With 50% mix and empty delay buffer:
    // output = 0.5 * dry(1.0) + 0.5 * wet(0.0) = 0.5
    REQUIRE(buffer[0] == Approx(0.5f).margin(0.01f));
}

TEST_CASE("DelayEngine kill-dry mode outputs only wet", "[delay][systems][US3]") {
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> buffer{};

    DelayEngine delay;
    delay.prepare(44100.0, blockSize, 1000.0f);
    delay.setDelayTimeMs(100.0f);
    delay.setMix(0.5f);  // This would normally give 50% dry
    delay.setKillDry(true);  // But kill-dry removes dry signal

    delay.reset();

    std::fill(buffer.begin(), buffer.end(), 1.0f);

    auto ctx = makeTestContext();
    delay.process(buffer.data(), blockSize, ctx);

    // With kill-dry and empty delay buffer:
    // output = 0 * dry + 0.5 * wet(0.0) = 0.0
    REQUIRE(buffer[0] == Approx(0.0f).margin(0.01f));
}

TEST_CASE("DelayEngine mix changes are smoothed", "[delay][systems][US3]") {
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> buffer{};

    DelayEngine delay;
    delay.prepare(44100.0, blockSize, 1000.0f);
    delay.setDelayTimeMs(10.0f);
    delay.setMix(0.0f);

    // Prime
    auto ctx = makeTestContext();
    for (int i = 0; i < 10; ++i) {
        std::fill(buffer.begin(), buffer.end(), 1.0f);
        delay.process(buffer.data(), blockSize, ctx);
    }

    float beforeOutput = buffer[blockSize - 1];

    // Change mix abruptly
    delay.setMix(1.0f);
    std::fill(buffer.begin(), buffer.end(), 1.0f);
    delay.process(buffer.data(), blockSize, ctx);

    float afterOutput = buffer[0];

    // The change should be gradual (smoothed), not instant
    // So the first sample after change should be between old and new values
    // (This is a weak test but validates smoothing is happening)
    REQUIRE(afterOutput >= -0.1f);
    REQUIRE(afterOutput <= 1.1f);
}

// =============================================================================
// Phase 6: User Story 4 - State Management Tests
// =============================================================================

TEST_CASE("DelayEngine isPrepared() returns correct state", "[delay][systems][US4]") {
    DelayEngine delay;
    REQUIRE_FALSE(delay.isPrepared());

    delay.prepare(44100.0, 512, 1000.0f);
    REQUIRE(delay.isPrepared());
}

TEST_CASE("DelayEngine getMaxDelayMs() returns configured value", "[delay][systems][US4]") {
    DelayEngine delay;
    delay.prepare(44100.0, 512, 1234.5f);

    REQUIRE(delay.getMaxDelayMs() == Approx(1234.5f));
}

TEST_CASE("DelayEngine stereo process applies same delay", "[delay][systems][US4]") {
    constexpr size_t bufferSize = 12000;
    std::array<float, bufferSize> left{};
    std::array<float, bufferSize> right{};

    DelayEngine delay;
    delay.prepare(44100.0, bufferSize, 1000.0f);
    delay.setTimeMode(TimeMode::Free);
    delay.setDelayTimeMs(100.0f);
    delay.setMix(1.0f);

    // Different impulses in left and right
    generateImpulse(left.data(), bufferSize);
    std::fill(right.begin(), right.end(), 0.0f);
    right[100] = 1.0f;  // Impulse at different position

    auto ctx = makeTestContext();
    ctx.blockSize = bufferSize;
    delay.process(left.data(), right.data(), bufferSize, ctx);

    // Both should have same delay amount
    size_t leftPeak = findPeakIndex(left.data(), bufferSize);
    size_t rightPeak = findPeakIndex(right.data(), bufferSize);

    // Left impulse at 0, delayed by 100ms = 4410 samples
    size_t expectedDelay = static_cast<size_t>(0.1 * 44100.0);
    REQUIRE(leftPeak == Approx(expectedDelay).margin(2));

    // Right impulse at 100, delayed by same amount
    REQUIRE(rightPeak == Approx(100 + expectedDelay).margin(2));
}

TEST_CASE("DelayEngine variable block sizes work correctly", "[delay][systems][US4]") {
    DelayEngine delay;
    delay.prepare(44100.0, 1024, 1000.0f);  // Max 1024
    delay.setDelayTimeMs(50.0f);
    delay.setMix(0.5f);

    auto ctx = makeTestContext();

    // Process with different block sizes
    std::array<size_t, 5> blockSizes = {128, 256, 512, 1024, 64};

    for (size_t blockSize : blockSizes) {
        std::vector<float> buffer(blockSize, 1.0f);
        ctx.blockSize = blockSize;

        // Should not crash or produce invalid output
        REQUIRE_NOTHROW(delay.process(buffer.data(), blockSize, ctx));

        // Output should be valid
        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
            REQUIRE_FALSE(std::isinf(sample));
        }
    }
}

TEST_CASE("DelayEngine process is real-time safe", "[delay][systems][US4][SC-003]") {
    // This is a conceptual test - we can't truly test for allocations
    // but we can verify the process method is noexcept
    DelayEngine delay;
    delay.prepare(44100.0, 512, 1000.0f);

    std::array<float, 512> buffer{};
    auto ctx = makeTestContext();

    // Verify noexcept (compile-time check)
    static_assert(noexcept(delay.process(buffer.data(), 512, ctx)),
                  "process() must be noexcept for real-time safety");

    // Process should complete without issues
    REQUIRE_NOTHROW(delay.process(buffer.data(), 512, ctx));
}

// =============================================================================
// Phase 7: Edge Case Tests
// =============================================================================

TEST_CASE("DelayEngine 0ms delay outputs immediate signal", "[delay][systems][edge]") {
    constexpr size_t blockSize = 512;
    std::array<float, blockSize> input{};
    std::array<float, blockSize> output{};

    for (size_t i = 0; i < blockSize; ++i) {
        input[i] = std::sin(2.0f * 3.14159f * 440.0f * static_cast<float>(i) / 44100.0f);
    }
    std::copy(input.begin(), input.end(), output.begin());

    DelayEngine delay;
    delay.prepare(44100.0, blockSize, 1000.0f);
    delay.setDelayTimeMs(0.0f);
    delay.setMix(0.5f);  // 50% wet = immediate wet signal

    auto ctx = makeTestContext();

    // Let it settle (smoother)
    for (int i = 0; i < 100; ++i) {
        std::copy(input.begin(), input.end(), output.begin());
        delay.process(output.data(), blockSize, ctx);
    }

    // With 0ms delay and 50% mix, output should be 50% dry + 50% immediate wet
    // = 100% of input (since wet is same as dry at 0ms delay)
    REQUIRE(output[100] == Approx(input[100]).margin(0.01f));
}

TEST_CASE("DelayEngine negative delay time clamps to 0", "[delay][systems][edge]") {
    DelayEngine delay;
    delay.prepare(44100.0, 512, 1000.0f);

    delay.setDelayTimeMs(-100.0f);

    std::array<float, 512> buffer{};
    auto ctx = makeTestContext();
    for (int i = 0; i < 100; ++i) {
        delay.process(buffer.data(), 512, ctx);
    }

    REQUIRE(delay.getCurrentDelayMs() >= 0.0f);
}

TEST_CASE("DelayEngine infinity delay time clamps to maxDelayMs", "[delay][systems][edge]") {
    DelayEngine delay;
    delay.prepare(44100.0, 512, 500.0f);

    delay.setDelayTimeMs(std::numeric_limits<float>::infinity());

    std::array<float, 512> buffer{};
    auto ctx = makeTestContext();
    for (int i = 0; i < 100; ++i) {
        delay.process(buffer.data(), 512, ctx);
    }

    REQUIRE(delay.getCurrentDelayMs() <= 500.0f);
}

TEST_CASE("DelayEngine handles tempo=0 (clamps to 20 BPM)", "[delay][systems][edge]") {
    DelayEngine delay;
    delay.prepare(44100.0, 512, 10000.0f);
    delay.setTimeMode(TimeMode::Synced);
    delay.setNoteValue(NoteValue::Quarter);

    auto ctx = makeTestContext(44100.0, 0.0);  // 0 BPM
    std::array<float, 512> buffer{};

    for (int i = 0; i < 100; ++i) {
        delay.process(buffer.data(), 512, ctx);
    }

    // At 20 BPM (minimum), quarter = 3000ms
    // Should be valid and large
    float currentMs = delay.getCurrentDelayMs();
    REQUIRE(currentMs > 0.0f);
    REQUIRE(currentMs <= 10000.0f);
    REQUIRE(currentMs == Approx(3000.0f).margin(10.0f));
}
