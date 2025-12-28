// ==============================================================================
// Layer 1: DSP Primitive Tests - CrossfadingDelayLine
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests for click-free delay time changes using two-tap crossfading.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include "dsp/primitives/crossfading_delay_line.h"

#include <cmath>
#include <array>
#include <vector>
#include <numeric>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// Basic Functionality Tests
// =============================================================================

TEST_CASE("CrossfadingDelayLine prepare allocates buffer", "[delay][crossfade][prepare]") {
    CrossfadingDelayLine delay;

    SECTION("prepares with standard sample rate") {
        delay.prepare(44100.0, 1.0f);  // 1 second max delay
        REQUIRE(delay.maxDelaySamples() >= 44100);
    }

    SECTION("prepares with high sample rate") {
        delay.prepare(96000.0, 0.5f);  // 0.5 seconds at 96kHz
        REQUIRE(delay.maxDelaySamples() >= 48000);
    }
}

TEST_CASE("CrossfadingDelayLine reset clears state", "[delay][crossfade][reset]") {
    CrossfadingDelayLine delay;
    delay.prepare(44100.0, 0.1f);

    // Write some samples
    for (int i = 0; i < 100; ++i) {
        delay.write(1.0f);
    }

    // Trigger a crossfade
    delay.setDelayMs(50.0f);

    delay.reset();

    // After reset, crossfading should be false
    REQUIRE_FALSE(delay.isCrossfading());
}

TEST_CASE("CrossfadingDelayLine basic write/read", "[delay][crossfade][basic]") {
    CrossfadingDelayLine delay;
    delay.prepare(44100.0, 0.1f);

    SECTION("write and read at fixed delay") {
        // Set a fixed delay (use larger delay to trigger crossfade)
        delay.setDelaySamples(500.0f);

        // Wait for crossfade to complete (~20ms = 882 samples at 44.1kHz)
        for (int i = 0; i < 1000; ++i) {
            delay.write(0.0f);
            (void)delay.read();
        }
        REQUIRE_FALSE(delay.isCrossfading());  // Crossfade should be done

        // Now write an impulse
        delay.write(1.0f);

        // Process until impulse appears at delay position
        float output = 0.0f;
        for (int i = 0; i < 499; ++i) {
            delay.write(0.0f);
            output = delay.read();
        }
        // After 500 samples, the impulse should appear
        delay.write(0.0f);
        output = delay.read();
        REQUIRE(output == Approx(1.0f));
    }
}

// =============================================================================
// Crossfade Trigger Tests
// =============================================================================

TEST_CASE("CrossfadingDelayLine triggers crossfade on large delay change", "[delay][crossfade][trigger]") {
    CrossfadingDelayLine delay;
    delay.prepare(44100.0, 1.0f);

    // Start at 100ms delay
    delay.setDelayMs(100.0f);

    // Prime buffer with samples so the delay time is established
    for (int i = 0; i < 5000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Small change should NOT trigger crossfade (44 samples < 100 threshold)
    delay.setDelayMs(101.0f);
    REQUIRE_FALSE(delay.isCrossfading());

    // Large change (>100 samples at 44.1kHz) SHOULD trigger crossfade
    delay.setDelayMs(200.0f);  // 99ms jump = 4370 samples > 100 threshold
    REQUIRE(delay.isCrossfading());
}

TEST_CASE("CrossfadingDelayLine small changes don't trigger crossfade", "[delay][crossfade][smooth]") {
    CrossfadingDelayLine delay;
    delay.prepare(44100.0, 1.0f);

    delay.setDelaySamples(1000.0f);

    // Prime buffer first
    for (int i = 0; i < 2000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Changes less than kCrossfadeThresholdSamples (100) should not trigger
    for (float d = 1000.0f; d <= 1050.0f; d += 10.0f) {
        delay.setDelaySamples(d);
        REQUIRE_FALSE(delay.isCrossfading());
    }
}

// =============================================================================
// Click-Free Operation Tests (THE KEY TESTS)
// =============================================================================

TEST_CASE("CrossfadingDelayLine eliminates clicks during large delay changes",
          "[delay][crossfade][clickfree]") {
    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    // Fill buffer with a continuous signal (sine wave)
    const float freq = 440.0f;
    const float omega = 2.0f * 3.14159265f * freq / static_cast<float>(sampleRate);

    // Initialize at 100ms delay
    delay.setDelayMs(100.0f);

    // Prime the buffer
    for (int i = 0; i < 10000; ++i) {
        float input = std::sin(omega * static_cast<float>(i));
        delay.write(input);
        (void)delay.read();
    }

    // Now make a large delay change (100ms -> 300ms = 8820 samples change)
    std::vector<float> outputs;
    outputs.reserve(2000);

    float prevOutput = delay.read();

    for (int i = 0; i < 2000; ++i) {
        if (i == 0) {
            delay.setDelayMs(300.0f);  // Large jump!
        }

        float input = std::sin(omega * static_cast<float>(10000 + i));
        delay.write(input);
        float output = delay.read();
        outputs.push_back(output);
    }

    // Check for discontinuities
    float maxDiscontinuity = 0.0f;
    prevOutput = outputs[0];

    for (size_t i = 1; i < outputs.size(); ++i) {
        float discontinuity = std::abs(outputs[i] - prevOutput);
        maxDiscontinuity = std::max(maxDiscontinuity, discontinuity);
        prevOutput = outputs[i];
    }

    INFO("Maximum discontinuity during 200ms delay jump: " << maxDiscontinuity);

    // With crossfading, discontinuity should be minimal
    // A sine wave at 440Hz has max sample-to-sample change of about 0.06
    // Allow 0.2 for crossfading transient (should be much smoother than without)
    REQUIRE(maxDiscontinuity < 0.2f);
}

TEST_CASE("CrossfadingDelayLine vs plain delay line during large jumps",
          "[delay][crossfade][comparison]") {
    // This test demonstrates the problem crossfading solves
    // by comparing behavior during a sudden delay change

    CrossfadingDelayLine crossfadingDelay;
    DelayLine plainDelay;

    const double sampleRate = 44100.0;
    crossfadingDelay.prepare(sampleRate, 1.0f);
    plainDelay.prepare(sampleRate, 1.0f);

    // Fill both with same signal
    const float freq = 440.0f;
    const float omega = 2.0f * 3.14159265f * freq / static_cast<float>(sampleRate);

    float delayMs = 100.0f;
    crossfadingDelay.setDelayMs(delayMs);
    float delaySamples = delayMs * 0.001f * static_cast<float>(sampleRate);

    for (int i = 0; i < 10000; ++i) {
        float input = std::sin(omega * static_cast<float>(i));
        crossfadingDelay.write(input);
        plainDelay.write(input);
        (void)crossfadingDelay.read();
        (void)plainDelay.readLinear(delaySamples);
    }

    // Now jump delay from 100ms to 300ms
    float newDelayMs = 300.0f;
    crossfadingDelay.setDelayMs(newDelayMs);
    float newDelaySamples = newDelayMs * 0.001f * static_cast<float>(sampleRate);

    // CrossfadingDelayLine should be crossfading after the large change
    REQUIRE(crossfadingDelay.isCrossfading() == true);

    // Capture first few samples after the change and verify smooth transition
    std::vector<float> crossfadingOutputs;
    std::vector<float> plainOutputs;

    for (int i = 0; i < 100; ++i) {
        float input = std::sin(omega * static_cast<float>(10000 + i));
        crossfadingDelay.write(input);
        plainDelay.write(input);
        crossfadingOutputs.push_back(crossfadingDelay.read());
        plainOutputs.push_back(plainDelay.readLinear(newDelaySamples));
    }

    // After 100 samples, crossfade should still be in progress (takes ~882 samples)
    REQUIRE(crossfadingDelay.isCrossfading() == true);

    // Verify crossfading output is smooth (no large discontinuities)
    float maxDiscontinuity = 0.0f;
    for (size_t i = 1; i < crossfadingOutputs.size(); ++i) {
        float disc = std::abs(crossfadingOutputs[i] - crossfadingOutputs[i - 1]);
        maxDiscontinuity = std::max(maxDiscontinuity, disc);
    }
    REQUIRE(maxDiscontinuity < 0.2f);  // Smooth output
}

TEST_CASE("CrossfadingDelayLine crossfade completes correctly", "[delay][crossfade][completion]") {
    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    delay.setDelayMs(100.0f);

    // Prime buffer
    for (int i = 0; i < 5000; ++i) {
        delay.write(static_cast<float>(i) * 0.001f);
        (void)delay.read();
    }

    // Trigger crossfade
    delay.setDelayMs(300.0f);
    REQUIRE(delay.isCrossfading());

    // Process until crossfade completes (default is 20ms = 882 samples)
    int samplesUntilComplete = 0;
    for (int i = 0; i < 2000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
        samplesUntilComplete++;
        if (!delay.isCrossfading()) {
            break;
        }
    }

    REQUIRE_FALSE(delay.isCrossfading());

    // Should complete within approximately 20ms
    float completionTimeMs = static_cast<float>(samplesUntilComplete) / static_cast<float>(sampleRate) * 1000.0f;
    INFO("Crossfade completed in " << completionTimeMs << " ms");
    REQUIRE(completionTimeMs < 25.0f);  // Allow some margin
    REQUIRE(completionTimeMs > 15.0f);  // But not too fast
}

// =============================================================================
// Crossfade Time Configuration Tests
// =============================================================================

TEST_CASE("CrossfadingDelayLine configurable crossfade time", "[delay][crossfade][config]") {
    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    SECTION("faster crossfade (10ms)") {
        delay.setCrossfadeTime(10.0f);
        delay.setDelayMs(100.0f);

        // Prime
        for (int i = 0; i < 5000; ++i) {
            delay.write(0.5f);
            (void)delay.read();
        }

        delay.setDelayMs(300.0f);

        int samples = 0;
        while (delay.isCrossfading() && samples < 2000) {
            delay.write(0.5f);
            (void)delay.read();
            samples++;
        }

        float timeMs = static_cast<float>(samples) / static_cast<float>(sampleRate) * 1000.0f;
        REQUIRE(timeMs < 15.0f);
        REQUIRE(timeMs > 5.0f);
    }

    SECTION("slower crossfade (50ms)") {
        delay.setCrossfadeTime(50.0f);
        delay.setDelayMs(100.0f);

        // Prime
        for (int i = 0; i < 5000; ++i) {
            delay.write(0.5f);
            (void)delay.read();
        }

        delay.setDelayMs(300.0f);

        int samples = 0;
        while (delay.isCrossfading() && samples < 5000) {
            delay.write(0.5f);
            (void)delay.read();
            samples++;
        }

        float timeMs = static_cast<float>(samples) / static_cast<float>(sampleRate) * 1000.0f;
        REQUIRE(timeMs > 40.0f);
        REQUIRE(timeMs < 60.0f);
    }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("CrossfadingDelayLine handles rapid successive changes", "[delay][crossfade][edge]") {
    CrossfadingDelayLine delay;
    delay.prepare(44100.0, 1.0f);

    delay.setDelayMs(100.0f);

    // Prime buffer
    for (int i = 0; i < 5000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Trigger crossfade
    delay.setDelayMs(300.0f);
    REQUIRE(delay.isCrossfading());

    // Try to change again while crossfading - should be ignored
    delay.setDelayMs(500.0f);

    // Process a bit
    for (int i = 0; i < 100; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Still crossfading to original target
    REQUIRE(delay.isCrossfading());

    // Let crossfade complete
    while (delay.isCrossfading()) {
        delay.write(0.5f);
        (void)delay.read();
    }

    // Now we can trigger another crossfade
    // Note: We use 700.0f, not 500.0f, because the inactive tap was tracking
    // the 500.0f target during the previous crossfade, so we need a different
    // target to trigger a new crossfade.
    delay.setDelayMs(700.0f);
    REQUIRE(delay.isCrossfading());
}

TEST_CASE("CrossfadingDelayLine getCurrentDelaySamples during crossfade", "[delay][crossfade][query]") {
    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    float startDelayMs = 100.0f;
    float endDelayMs = 300.0f;
    float startDelaySamples = startDelayMs * 0.001f * static_cast<float>(sampleRate);
    float endDelaySamples = endDelayMs * 0.001f * static_cast<float>(sampleRate);

    delay.setDelayMs(startDelayMs);

    // Prime buffer
    for (int i = 0; i < 5000; ++i) {
        delay.write(0.5f);
        (void)delay.read();
    }

    float currentBefore = delay.getCurrentDelaySamples();
    REQUIRE(currentBefore == Approx(startDelaySamples));

    // Trigger crossfade
    delay.setDelayMs(endDelayMs);

    // During crossfade, current delay should be somewhere between start and end
    for (int i = 0; i < 441; ++i) {  // Half the crossfade time
        delay.write(0.5f);
        (void)delay.read();
    }

    if (delay.isCrossfading()) {
        float currentDuring = delay.getCurrentDelaySamples();
        REQUIRE(currentDuring > startDelaySamples);
        REQUIRE(currentDuring < endDelaySamples);
    }
}

// =============================================================================
// Real-Time Safety Tests
// =============================================================================

TEST_CASE("CrossfadingDelayLine noexcept specifications", "[delay][crossfade][realtime]") {
    SECTION("constructors and destructors are noexcept") {
        static_assert(std::is_nothrow_default_constructible_v<CrossfadingDelayLine>,
                      "Default constructor must be noexcept");
        static_assert(std::is_nothrow_destructible_v<CrossfadingDelayLine>,
                      "Destructor must be noexcept");
        static_assert(std::is_nothrow_move_constructible_v<CrossfadingDelayLine>,
                      "Move constructor must be noexcept");
        static_assert(std::is_nothrow_move_assignable_v<CrossfadingDelayLine>,
                      "Move assignment must be noexcept");
        REQUIRE(true);
    }

    SECTION("processing methods are noexcept") {
        CrossfadingDelayLine delay;
        delay.prepare(44100.0, 0.1f);

        static_assert(noexcept(delay.write(0.0f)), "write() must be noexcept");
        static_assert(noexcept(delay.read()), "read() must be noexcept");
        static_assert(noexcept(delay.process(0.0f)), "process() must be noexcept");
        static_assert(noexcept(delay.setDelaySamples(0.0f)), "setDelaySamples() must be noexcept");
        static_assert(noexcept(delay.setDelayMs(0.0f)), "setDelayMs() must be noexcept");
        static_assert(noexcept(delay.reset()), "reset() must be noexcept");
        REQUIRE(true);
    }

    SECTION("query methods are noexcept") {
        CrossfadingDelayLine delay;
        static_assert(noexcept(delay.isCrossfading()), "isCrossfading() must be noexcept");
        static_assert(noexcept(delay.getCurrentDelaySamples()), "getCurrentDelaySamples() must be noexcept");
        static_assert(noexcept(delay.maxDelaySamples()), "maxDelaySamples() must be noexcept");
        REQUIRE(true);
    }
}

// =============================================================================
// REGRESSION TEST: Zipper Noise During Delay Time Changes
// =============================================================================

TEST_CASE("REGRESSION: No zipper noise during 200ms delay time change",
          "[delay][crossfade][regression][zipper]") {
    // This is the regression test for the issue discovered in DigitalDelay tests.
    //
    // PROBLEM: When delay time changed from 300ms to 100ms (200ms jump), the
    // SC-009 test detected a discontinuity of 3.20724f at sample 33136.
    // This was during parameter smoothing which moves the read position,
    // causing pitch artifacts and audible clicks.
    //
    // SOLUTION: CrossfadingDelayLine uses two-tap crossfading to eliminate
    // this discontinuity by blending between old and new positions instead
    // of moving a single read pointer.

    CrossfadingDelayLine delay;
    const double sampleRate = 44100.0;
    delay.prepare(sampleRate, 1.0f);

    // Simulate the exact scenario from SC-009: 300ms -> 100ms change
    delay.setDelayMs(300.0f);

    // Feed a sine wave (same as original test)
    const float freq = 440.0f;
    const float omega = 2.0f * 3.14159265f * freq / static_cast<float>(sampleRate);

    // Prime the buffer
    for (size_t i = 0; i < 44100; ++i) {
        float input = std::sin(omega * static_cast<float>(i));
        (void)delay.process(input);
    }

    // Now make the 200ms jump (same as original failing test)
    delay.setDelayMs(100.0f);

    // Process and check for discontinuities
    float maxDiscontinuity = 0.0f;
    float prevOutput = delay.read();
    delay.write(0.0f);  // Advance buffer

    for (size_t i = 0; i < 44100; ++i) {
        float input = std::sin(omega * static_cast<float>(44100 + i));
        float output = delay.process(input);

        float discontinuity = std::abs(output - prevOutput);
        if (discontinuity > maxDiscontinuity) {
            maxDiscontinuity = discontinuity;
            INFO("New max discontinuity " << discontinuity << " at sample " << i);
        }
        prevOutput = output;
    }

    INFO("Maximum discontinuity during 200ms delay jump: " << maxDiscontinuity);

    // The original failing test saw 3.20724f discontinuity.
    // With crossfading, this should be well under 1.0f (a sine wave's
    // maximum sample-to-sample change is ~0.063 at 440Hz/44.1kHz)
    // Allow 0.5f for the crossfading blend transient
    REQUIRE(maxDiscontinuity < 0.5f);
}
