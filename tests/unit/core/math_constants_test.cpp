// ==============================================================================
// Math Constants - Unit Tests
// ==============================================================================
// Layer 0: Core Utilities
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests for: src/dsp/core/math_constants.h
// Purpose: Verify centralized math constants for DSP calculations
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/core/math_constants.h"

#include <array>
#include <cmath>
#include <numbers>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// kPi Tests
// ==============================================================================

TEST_CASE("kPi has correct value", "[dsp][core][math_constants]") {

    SECTION("kPi matches std::numbers::pi_v<float>") {
        REQUIRE(kPi == Approx(std::numbers::pi_v<float>));
    }

    SECTION("kPi has sufficient precision for DSP calculations") {
        // Verify at least 7 significant digits (float precision)
        REQUIRE(kPi == Approx(3.14159265358979323846f).margin(1e-7f));
    }

    SECTION("sin(kPi) is approximately zero") {
        REQUIRE(std::sin(kPi) == Approx(0.0f).margin(1e-6f));
    }

    SECTION("cos(kPi) is approximately -1") {
        REQUIRE(std::cos(kPi) == Approx(-1.0f).margin(1e-6f));
    }
}

// ==============================================================================
// kTwoPi Tests
// ==============================================================================

TEST_CASE("kTwoPi has correct value", "[dsp][core][math_constants]") {

    SECTION("kTwoPi is exactly 2 * kPi") {
        REQUIRE(kTwoPi == 2.0f * kPi);
    }

    SECTION("kTwoPi matches 2 * std::numbers::pi_v<float>") {
        REQUIRE(kTwoPi == Approx(2.0f * std::numbers::pi_v<float>));
    }

    SECTION("sin(kTwoPi) is approximately zero (full cycle)") {
        REQUIRE(std::sin(kTwoPi) == Approx(0.0f).margin(1e-5f));
    }

    SECTION("cos(kTwoPi) is approximately 1 (full cycle)") {
        REQUIRE(std::cos(kTwoPi) == Approx(1.0f).margin(1e-6f));
    }
}

// ==============================================================================
// DSP Usage Tests
// ==============================================================================

TEST_CASE("Math constants work in typical DSP calculations", "[dsp][core][math_constants]") {

    SECTION("Phase calculation for 1kHz at 44.1kHz sample rate") {
        // Angular frequency: omega = 2 * pi * f / fs
        constexpr float frequency = 1000.0f;
        constexpr float sampleRate = 44100.0f;
        const float omega = kTwoPi * frequency / sampleRate;

        // Expected: 2 * pi * 1000 / 44100 = approximately 0.1425...
        REQUIRE(omega == Approx(0.142476f).margin(0.0001f));
    }

    SECTION("LFO phase increment calculation") {
        // Phase increment per sample for 1 Hz LFO at 48kHz
        constexpr float lfoFreq = 1.0f;
        constexpr float sampleRate = 48000.0f;
        const float phaseIncrement = kTwoPi * lfoFreq / sampleRate;

        // Verify the increment calculation is precise (single multiplication)
        REQUIRE(phaseIncrement * sampleRate == Approx(kTwoPi).margin(1e-5f));

        // Verify increment has expected value
        // 2π / 48000 ≈ 0.0001309...
        REQUIRE(phaseIncrement == Approx(0.00013089969f).margin(1e-9f));

        // Verify sin/cos at phase positions work correctly
        REQUIRE(std::sin(phaseIncrement * 12000) == Approx(1.0f).margin(1e-5f));  // sin(π/2)
        REQUIRE(std::cos(phaseIncrement * 24000) == Approx(-1.0f).margin(1e-5f)); // cos(π)
    }

    SECTION("Biquad coefficient calculation uses kPi correctly") {
        // Verify that normalized frequency calculation is correct
        // omega0 = 2 * pi * fc / fs
        constexpr float cutoff = 1000.0f;
        constexpr float sampleRate = 44100.0f;
        const float omega0 = kTwoPi * cutoff / sampleRate;
        const float sinOmega = std::sin(omega0);
        const float cosOmega = std::cos(omega0);

        // Verify trig identity: sin^2 + cos^2 = 1
        REQUIRE(sinOmega * sinOmega + cosOmega * cosOmega == Approx(1.0f).margin(1e-6f));
    }
}

// ==============================================================================
// Constexpr Tests
// ==============================================================================

TEST_CASE("Math constants are constexpr", "[dsp][core][math_constants]") {

    SECTION("kPi can be used in constexpr context") {
        constexpr float halfPi = kPi / 2.0f;
        REQUIRE(halfPi == Approx(1.5707963f).margin(1e-6f));
    }

    SECTION("kTwoPi can be used in constexpr context") {
        constexpr float quarterCycle = kTwoPi / 4.0f;
        REQUIRE(quarterCycle == Approx(kPi / 2.0f).margin(1e-7f));
    }

    SECTION("constexpr array initialization with math constants") {
        constexpr std::array<float, 4> phases = {
            0.0f,
            kPi / 2.0f,
            kPi,
            3.0f * kPi / 2.0f
        };

        REQUIRE(phases[0] == 0.0f);
        REQUIRE(phases[1] == Approx(kPi / 2.0f));
        REQUIRE(phases[2] == Approx(kPi));
        REQUIRE(phases[3] == Approx(3.0f * kPi / 2.0f));
    }
}
