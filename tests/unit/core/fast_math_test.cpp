// ==============================================================================
// Unit Tests: FastMath (Layer 0)
// ==============================================================================
// Tests for fast approximations of transcendental functions.
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-First Development
//
// Reference: specs/017-layer0-utilities/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <bit>
#include <cmath>
#include <array>
#include <cstdint>
#include <limits>

#include "dsp/core/fast_math.h"

using namespace Iterum::DSP::FastMath;
using Catch::Approx;

namespace {

/// @brief Calculate relative error between two values.
float relativeError(float actual, float expected) {
    if (std::abs(expected) < 1e-6f) {
        return std::abs(actual - expected);
    }
    return std::abs((actual - expected) / expected);
}

/// @brief Check if value is NaN using bit manipulation.
/// Uses same approach as db_utils.h - works even with -ffast-math on MSVC.
bool isNaN(float x) {
    const auto bits = std::bit_cast<std::uint32_t>(x);
    return ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
}

} // namespace

// =============================================================================
// fastTanh Tests
// =============================================================================

TEST_CASE("fastTanh basic values", "[fast_math][tanh]") {
    // tanh(0) = 0
    REQUIRE(fastTanh(0.0f) == Approx(0.0f));

    // Symmetry: tanh(-x) = -tanh(x)
    REQUIRE(fastTanh(-1.0f) == Approx(-fastTanh(1.0f)));
    REQUIRE(fastTanh(-2.0f) == Approx(-fastTanh(2.0f)));
}

TEST_CASE("fastTanh accuracy within 0.5% for |x| < 3", "[fast_math][tanh]") {
    // Test many points in [-3, 3]
    for (float x = -3.0f; x <= 3.0f; x += 0.1f) {
        float expected = std::tanh(x);
        float actual = fastTanh(x);
        float relErr = relativeError(actual, expected);

        INFO("x = " << x << ", expected = " << expected << ", actual = " << actual << ", relErr = " << relErr);
        REQUIRE(relErr < 0.005f);  // 0.5%
    }
}

TEST_CASE("fastTanh accuracy within 1% for |x| >= 3", "[fast_math][tanh]") {
    // Test saturation region
    for (float x = 3.0f; x <= 5.0f; x += 0.2f) {
        float expected = std::tanh(x);
        float actual = fastTanh(x);
        float relErr = relativeError(actual, expected);

        REQUIRE(relErr < 0.01f);  // 1%
    }

    // Negative side
    for (float x = -5.0f; x <= -3.0f; x += 0.2f) {
        float expected = std::tanh(x);
        float actual = fastTanh(x);
        float relErr = relativeError(actual, expected);

        REQUIRE(relErr < 0.01f);  // 1%
    }
}

TEST_CASE("fastTanh saturation behavior", "[fast_math][tanh]") {
    // For large |x|, tanh approaches +/-1
    REQUIRE(fastTanh(10.0f) == Approx(1.0f).margin(0.001f));
    REQUIRE(fastTanh(-10.0f) == Approx(-1.0f).margin(0.001f));
    REQUIRE(fastTanh(100.0f) == Approx(1.0f));
    REQUIRE(fastTanh(-100.0f) == Approx(-1.0f));
}

TEST_CASE("fastTanh NaN handling", "[fast_math][tanh]") {
    float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(isNaN(fastTanh(nan)));
}

TEST_CASE("fastTanh infinity handling", "[fast_math][tanh]") {
    float inf = std::numeric_limits<float>::infinity();
    float negInf = -std::numeric_limits<float>::infinity();

    REQUIRE(fastTanh(inf) == 1.0f);
    REQUIRE(fastTanh(negInf) == -1.0f);
}

TEST_CASE("fastTanh is noexcept", "[fast_math][tanh]") {
    STATIC_REQUIRE(noexcept(fastTanh(1.0f)));
}

TEST_CASE("fastTanh is constexpr", "[fast_math][tanh]") {
    // Verify constexpr evaluation
    constexpr float zero = fastTanh(0.0f);
    STATIC_REQUIRE(zero == 0.0f);

    // Verify constexpr array initialization
    constexpr std::array<float, 5> tanhTable = {
        fastTanh(-2.0f),
        fastTanh(-1.0f),
        fastTanh(0.0f),
        fastTanh(1.0f),
        fastTanh(2.0f)
    };

    REQUIRE(tanhTable[2] == 0.0f);  // tanh(0) = 0
    REQUIRE(tanhTable[0] == -tanhTable[4]);  // symmetry
    REQUIRE(tanhTable[1] == -tanhTable[3]);  // symmetry
}

TEST_CASE("fastTanh output range is [-1, 1]", "[fast_math][tanh]") {
    // Test that output is always in valid range
    for (float x = -10.0f; x <= 10.0f; x += 0.1f) {
        float result = fastTanh(x);
        REQUIRE(result >= -1.0f);
        REQUIRE(result <= 1.0f);
    }
}

TEST_CASE("fastTanh monotonically increasing", "[fast_math][tanh]") {
    // tanh is strictly monotonically increasing
    float prev = fastTanh(-10.0f);
    for (float x = -9.9f; x <= 10.0f; x += 0.1f) {
        float curr = fastTanh(x);
        REQUIRE(curr >= prev);
        prev = curr;
    }
}
