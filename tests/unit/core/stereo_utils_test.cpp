// Layer 0: Core Utilities - Stereo Utils Tests
// Feature: 019-feedback-network (stereoCrossBlend utility for cross-feedback routing)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
#include <cmath>

#include "dsp/core/stereo_utils.h"

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// stereoCrossBlend Tests (FR-017, SC-010)
// =============================================================================

TEST_CASE("stereoCrossBlend at crossAmount=0.0 returns original L/R", "[stereo][US6]") {
    float inL = 1.0f;
    float inR = 0.5f;
    float outL, outR;

    stereoCrossBlend(inL, inR, 0.0f, outL, outR);

    REQUIRE(outL == Approx(1.0f));
    REQUIRE(outR == Approx(0.5f));
}

TEST_CASE("stereoCrossBlend at crossAmount=1.0 swaps L/R (ping-pong)", "[stereo][US6]") {
    float inL = 1.0f;
    float inR = 0.0f;
    float outL, outR;

    stereoCrossBlend(inL, inR, 1.0f, outL, outR);

    // Full swap: L becomes R, R becomes L
    REQUIRE(outL == Approx(0.0f));
    REQUIRE(outR == Approx(1.0f));
}

TEST_CASE("stereoCrossBlend at crossAmount=0.5 returns mono blend", "[stereo][US6]") {
    float inL = 1.0f;
    float inR = 0.0f;
    float outL, outR;

    stereoCrossBlend(inL, inR, 0.5f, outL, outR);

    // 50% blend: both channels become (L+R)/2
    REQUIRE(outL == Approx(0.5f));
    REQUIRE(outR == Approx(0.5f));
}

TEST_CASE("stereoCrossBlend preserves energy at various crossAmount values", "[stereo][US6]") {
    // Energy preservation: sum of squares should be preserved or reduced
    // For crossAmount = 0.5, we get mono sum which preserves energy

    float inL = 0.8f;
    float inR = 0.6f;
    float inputEnergy = inL * inL + inR * inR;

    SECTION("crossAmount = 0.0 preserves energy exactly") {
        float outL, outR;
        stereoCrossBlend(inL, inR, 0.0f, outL, outR);
        float outputEnergy = outL * outL + outR * outR;
        REQUIRE(outputEnergy == Approx(inputEnergy));
    }

    SECTION("crossAmount = 1.0 preserves energy exactly (swap)") {
        float outL, outR;
        stereoCrossBlend(inL, inR, 1.0f, outL, outR);
        float outputEnergy = outL * outL + outR * outR;
        REQUIRE(outputEnergy == Approx(inputEnergy));
    }

    SECTION("crossAmount = 0.5 produces mono with preserved sum") {
        float outL, outR;
        stereoCrossBlend(inL, inR, 0.5f, outL, outR);
        // Both outputs should be (inL + inR) / 2
        float expectedMono = (inL + inR) * 0.5f;
        REQUIRE(outL == Approx(expectedMono));
        REQUIRE(outR == Approx(expectedMono));
    }

    SECTION("crossAmount = 0.25 partial blend") {
        float outL, outR;
        stereoCrossBlend(inL, inR, 0.25f, outL, outR);
        // outL = inL * 0.75 + inR * 0.25
        // outR = inR * 0.75 + inL * 0.25
        REQUIRE(outL == Approx(inL * 0.75f + inR * 0.25f));
        REQUIRE(outR == Approx(inR * 0.75f + inL * 0.25f));
    }
}

TEST_CASE("stereoCrossBlend is constexpr (compile-time evaluation)", "[stereo][US6]") {
    // This test verifies the function can be evaluated at compile-time
    constexpr float inL = 1.0f;
    constexpr float inR = 0.0f;
    constexpr float cross = 0.5f;

    // Use a lambda to capture constexpr result
    constexpr auto result = []() {
        float outL = 0.0f, outR = 0.0f;
        stereoCrossBlend(inL, inR, cross, outL, outR);
        return outL + outR;  // Sum for simple verification
    }();

    // At 50% cross, both channels become 0.5, so sum = 1.0
    REQUIRE(result == Approx(1.0f));
}

TEST_CASE("stereoCrossBlend handles negative input values", "[stereo][US6][edge]") {
    float inL = -0.5f;
    float inR = 0.5f;
    float outL, outR;

    stereoCrossBlend(inL, inR, 0.5f, outL, outR);

    // Should blend to zero (average of -0.5 and 0.5)
    REQUIRE(outL == Approx(0.0f));
    REQUIRE(outR == Approx(0.0f));
}

TEST_CASE("stereoCrossBlend handles zero inputs", "[stereo][US6][edge]") {
    float outL, outR;

    stereoCrossBlend(0.0f, 0.0f, 0.5f, outL, outR);

    REQUIRE(outL == Approx(0.0f));
    REQUIRE(outR == Approx(0.0f));
}

TEST_CASE("stereoCrossBlend formula verification", "[stereo][US6]") {
    // Verify the documented formula:
    // outL = inL * (1 - crossAmount) + inR * crossAmount
    // outR = inR * (1 - crossAmount) + inL * crossAmount

    const float inL = 0.7f;
    const float inR = 0.3f;

    for (float cross = 0.0f; cross <= 1.0f; cross += 0.1f) {
        DYNAMIC_SECTION("crossAmount = " << cross) {
            float outL, outR;
            stereoCrossBlend(inL, inR, cross, outL, outR);

            float expectedL = inL * (1.0f - cross) + inR * cross;
            float expectedR = inR * (1.0f - cross) + inL * cross;

            REQUIRE(outL == Approx(expectedL).margin(1e-6f));
            REQUIRE(outR == Approx(expectedR).margin(1e-6f));
        }
    }
}
