// ==============================================================================
// Layer 0: Core Utility Tests - Crossfade Utilities
// ==============================================================================
// Tests for equal-power crossfade functions used by mode transitions and
// character processing.
//
// Test-First Development: These tests written BEFORE implementation (Principle XII)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/core/crossfade_utils.h"

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// T002: Boundary condition tests
// =============================================================================

TEST_CASE("equalPowerGains at boundaries", "[dsp][core][crossfade]") {
    float fadeOut, fadeIn;

    SECTION("position 0.0 - start of crossfade") {
        equalPowerGains(0.0f, fadeOut, fadeIn);
        REQUIRE(fadeOut == Approx(1.0f));
        REQUIRE(fadeIn == Approx(0.0f));
    }

    SECTION("position 1.0 - end of crossfade") {
        equalPowerGains(1.0f, fadeOut, fadeIn);
        REQUIRE(fadeOut == Approx(0.0f).margin(1e-6f));
        REQUIRE(fadeIn == Approx(1.0f));
    }

    SECTION("position 0.5 - midpoint") {
        equalPowerGains(0.5f, fadeOut, fadeIn);
        // At midpoint, both should be ~0.707 (sqrt(2)/2)
        REQUIRE(fadeOut == Approx(0.7071f).margin(0.001f));
        REQUIRE(fadeIn == Approx(0.7071f).margin(0.001f));
    }
}

TEST_CASE("equalPowerGains pair version matches reference version", "[dsp][core][crossfade]") {
    for (float pos = 0.0f; pos <= 1.0f; pos += 0.1f) {
        float fadeOut, fadeIn;
        equalPowerGains(pos, fadeOut, fadeIn);

        auto [pairFadeOut, pairFadeIn] = equalPowerGains(pos);

        REQUIRE(pairFadeOut == Approx(fadeOut).margin(1e-6f));
        REQUIRE(pairFadeIn == Approx(fadeIn).margin(1e-6f));
    }
}

// =============================================================================
// T003: Constant-power property tests
// =============================================================================

TEST_CASE("equalPowerGains maintains constant power", "[dsp][core][crossfade]") {
    // fadeOut² + fadeIn² should ≈ 1.0 at all positions
    // This is the defining property of equal-power crossfade
    for (float pos = 0.0f; pos <= 1.0f; pos += 0.05f) {
        float fadeOut, fadeIn;
        equalPowerGains(pos, fadeOut, fadeIn);
        float totalPower = fadeOut * fadeOut + fadeIn * fadeIn;
        REQUIRE(totalPower == Approx(1.0f).margin(0.001f));
    }
}

TEST_CASE("equalPowerGains produces smooth transition", "[dsp][core][crossfade]") {
    // Verify monotonic decrease of fadeOut and increase of fadeIn
    float prevFadeOut = 2.0f;  // Start higher than any valid value
    float prevFadeIn = -1.0f;  // Start lower than any valid value

    for (float pos = 0.0f; pos <= 1.0f; pos += 0.01f) {
        float fadeOut, fadeIn;
        equalPowerGains(pos, fadeOut, fadeIn);

        // fadeOut should decrease monotonically
        REQUIRE(fadeOut <= prevFadeOut);
        prevFadeOut = fadeOut;

        // fadeIn should increase monotonically
        REQUIRE(fadeIn >= prevFadeIn);
        prevFadeIn = fadeIn;

        // Both gains should be in valid range [0, 1]
        REQUIRE(fadeOut >= 0.0f);
        REQUIRE(fadeOut <= 1.0f);
        REQUIRE(fadeIn >= 0.0f);
        REQUIRE(fadeIn <= 1.0f);
    }
}

// =============================================================================
// T004: crossfadeIncrement calculation tests
// =============================================================================

TEST_CASE("crossfadeIncrement calculates correctly", "[dsp][core][crossfade]") {
    SECTION("50ms at 44100 Hz") {
        // 50ms at 44100 Hz = 2205 samples
        // increment = 1/2205 ≈ 0.000453
        float inc = crossfadeIncrement(50.0f, 44100.0);
        REQUIRE(inc == Approx(1.0f / 2205.0f).margin(1e-6f));
    }

    SECTION("50ms at 48000 Hz") {
        // 50ms at 48000 Hz = 2400 samples
        float inc = crossfadeIncrement(50.0f, 48000.0);
        REQUIRE(inc == Approx(1.0f / 2400.0f).margin(1e-6f));
    }

    SECTION("50ms at 96000 Hz") {
        // 50ms at 96000 Hz = 4800 samples
        float inc = crossfadeIncrement(50.0f, 96000.0);
        REQUIRE(inc == Approx(1.0f / 4800.0f).margin(1e-6f));
    }

    SECTION("20ms at 44100 Hz") {
        // 20ms at 44100 Hz = 882 samples
        float inc = crossfadeIncrement(20.0f, 44100.0);
        REQUIRE(inc == Approx(1.0f / 882.0f).margin(1e-6f));
    }
}

TEST_CASE("crossfadeIncrement edge cases", "[dsp][core][crossfade]") {
    SECTION("zero duration returns 1.0 (instant crossfade)") {
        float inc = crossfadeIncrement(0.0f, 44100.0);
        REQUIRE(inc == 1.0f);
    }

    SECTION("very short duration") {
        // 0.1ms at 44100 Hz = ~4.4 samples
        float inc = crossfadeIncrement(0.1f, 44100.0);
        REQUIRE(inc > 0.0f);
        REQUIRE(inc < 1.0f);
    }

    SECTION("very long duration") {
        // 1000ms at 44100 Hz = 44100 samples
        float inc = crossfadeIncrement(1000.0f, 44100.0);
        REQUIRE(inc == Approx(1.0f / 44100.0f).margin(1e-8f));
    }
}

TEST_CASE("crossfadeIncrement completes crossfade in expected samples", "[dsp][core][crossfade]") {
    // Verify that incrementing position by crossfadeIncrement reaches 1.0
    // in approximately the expected number of samples

    constexpr float durationMs = 50.0f;
    constexpr double sampleRate = 44100.0;
    constexpr int expectedSamples = 2205;  // 50ms * 44100 / 1000

    float increment = crossfadeIncrement(durationMs, sampleRate);
    float position = 0.0f;
    int sampleCount = 0;

    while (position < 1.0f && sampleCount < expectedSamples * 2) {
        position += increment;
        sampleCount++;
    }

    // Should complete within ±1 sample of expected
    REQUIRE(sampleCount == Approx(expectedSamples).margin(1));
}

// =============================================================================
// Integration test: crossfade simulation
// =============================================================================

TEST_CASE("Full crossfade simulation produces no discontinuity", "[dsp][core][crossfade]") {
    constexpr float durationMs = 50.0f;
    constexpr double sampleRate = 44100.0;

    float increment = crossfadeIncrement(durationMs, sampleRate);
    float position = 0.0f;

    // Simulate crossfading between two constant signals
    constexpr float oldSignal = 1.0f;
    constexpr float newSignal = -1.0f;

    float prevOutput = oldSignal;  // Initial output before crossfade

    while (position < 1.0f) {
        float fadeOut, fadeIn;
        equalPowerGains(position, fadeOut, fadeIn);

        float output = oldSignal * fadeOut + newSignal * fadeIn;

        // Check for discontinuity (large jump between samples)
        // Maximum allowable jump depends on increment rate
        float maxJump = 0.1f;  // Allow some change per sample
        REQUIRE(std::abs(output - prevOutput) < maxJump);

        prevOutput = output;
        position += increment;
    }

    // Final output should be close to newSignal
    float fadeOut, fadeIn;
    equalPowerGains(1.0f, fadeOut, fadeIn);
    float finalOutput = oldSignal * fadeOut + newSignal * fadeIn;
    REQUIRE(finalOutput == Approx(newSignal).margin(0.01f));
}
