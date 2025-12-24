// ==============================================================================
// Layer 0: Core Utility Tests - Interpolation
// ==============================================================================
// Tests for standalone interpolation utilities.
//
// Constitution Compliance:
// - Principle XII: Test-First Development
//
// Reference: specs/017-layer0-utilities/spec.md (Phase 5 - US3)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>

#include "dsp/core/interpolation.h"

using namespace Iterum::DSP::Interpolation;
using Catch::Approx;

// =============================================================================
// linearInterpolate Tests (T048-T051 - FR-017)
// =============================================================================

TEST_CASE("linearInterpolate basic values", "[interpolation][US3]") {
    SECTION("Mid-point interpolation") {
        // Spec US3 Scenario 1: samples [0.0, 1.0], position 0.5 -> 0.5
        REQUIRE(linearInterpolate(0.0f, 1.0f, 0.5f) == 0.5f);
    }

    SECTION("Quarter-point interpolation") {
        REQUIRE(linearInterpolate(0.0f, 4.0f, 0.25f) == 1.0f);
    }

    SECTION("Three-quarter-point interpolation") {
        REQUIRE(linearInterpolate(0.0f, 4.0f, 0.75f) == 3.0f);
    }

    SECTION("Negative to positive") {
        REQUIRE(linearInterpolate(-1.0f, 1.0f, 0.5f) == 0.0f);
    }

    SECTION("Descending values") {
        REQUIRE(linearInterpolate(10.0f, 0.0f, 0.5f) == 5.0f);
    }
}

TEST_CASE("linearInterpolate boundary values (FR-022)", "[interpolation][US3]") {
    SECTION("t=0 returns y0 exactly") {
        REQUIRE(linearInterpolate(5.0f, 10.0f, 0.0f) == 5.0f);
        REQUIRE(linearInterpolate(-3.0f, 7.0f, 0.0f) == -3.0f);
    }

    SECTION("t=1 returns y1 exactly") {
        REQUIRE(linearInterpolate(5.0f, 10.0f, 1.0f) == 10.0f);
        REQUIRE(linearInterpolate(-3.0f, 7.0f, 1.0f) == 7.0f);
    }
}

TEST_CASE("linearInterpolate extrapolation", "[interpolation][US3]") {
    SECTION("t < 0 extrapolates below") {
        // y = 0 + t*(10-0), t=-0.5 -> y = -5
        REQUIRE(linearInterpolate(0.0f, 10.0f, -0.5f) == -5.0f);
    }

    SECTION("t > 1 extrapolates above") {
        // y = 0 + t*(10-0), t=1.5 -> y = 15
        REQUIRE(linearInterpolate(0.0f, 10.0f, 1.5f) == 15.0f);
    }
}

TEST_CASE("linearInterpolate is constexpr", "[interpolation][constexpr][US4]") {
    SECTION("Can be used at compile time") {
        constexpr float mid = linearInterpolate(0.0f, 1.0f, 0.5f);
        REQUIRE(mid == 0.5f);
    }

    SECTION("Constexpr array initialization") {
        constexpr std::array<float, 5> values = {
            linearInterpolate(0.0f, 1.0f, 0.0f),
            linearInterpolate(0.0f, 1.0f, 0.25f),
            linearInterpolate(0.0f, 1.0f, 0.5f),
            linearInterpolate(0.0f, 1.0f, 0.75f),
            linearInterpolate(0.0f, 1.0f, 1.0f)
        };

        REQUIRE(values[0] == 0.0f);
        REQUIRE(values[1] == 0.25f);
        REQUIRE(values[2] == 0.5f);
        REQUIRE(values[3] == 0.75f);
        REQUIRE(values[4] == 1.0f);
    }

    SECTION("static_assert validation") {
        static_assert(linearInterpolate(0.0f, 1.0f, 0.5f) == 0.5f);
        static_assert(linearInterpolate(0.0f, 10.0f, 0.0f) == 0.0f);
        static_assert(linearInterpolate(0.0f, 10.0f, 1.0f) == 10.0f);
        REQUIRE(true);  // If we got here, static_asserts passed
    }
}

TEST_CASE("linearInterpolate is noexcept", "[interpolation][realtime][US3]") {
    REQUIRE(noexcept(linearInterpolate(0.0f, 1.0f, 0.5f)));
}

// =============================================================================
// cubicHermiteInterpolate Tests (T052-T055 - FR-018)
// =============================================================================

TEST_CASE("cubicHermiteInterpolate boundary values (FR-022)", "[interpolation][US3]") {
    // Using samples: -1, 0, 1, 2 (positions -1, 0, 1, 2)
    float ym1 = -1.0f, y0 = 0.0f, y1 = 1.0f, y2 = 2.0f;

    SECTION("t=0 returns y0 exactly") {
        REQUIRE(cubicHermiteInterpolate(ym1, y0, y1, y2, 0.0f) == y0);
    }

    SECTION("t=1 returns y1 exactly") {
        REQUIRE(cubicHermiteInterpolate(ym1, y0, y1, y2, 1.0f) == y1);
    }
}

TEST_CASE("cubicHermiteInterpolate linear data", "[interpolation][US3]") {
    // For linear data, cubic should match linear interpolation
    float ym1 = 0.0f, y0 = 1.0f, y1 = 2.0f, y2 = 3.0f;

    SECTION("Mid-point on linear data") {
        float result = cubicHermiteInterpolate(ym1, y0, y1, y2, 0.5f);
        REQUIRE(result == Approx(1.5f).margin(1e-5f));
    }

    SECTION("Quarter-point on linear data") {
        float result = cubicHermiteInterpolate(ym1, y0, y1, y2, 0.25f);
        REQUIRE(result == Approx(1.25f).margin(1e-5f));
    }
}

TEST_CASE("cubicHermiteInterpolate curved data", "[interpolation][US3]") {
    // Spec US3 Scenario 2: sine wave samples, cubic should be closer to true value
    // sin(0), sin(90), sin(180), sin(270) degrees
    constexpr float kPi = 3.14159265358979323846f;
    float samples[] = {
        std::sin(0.0f),         // 0.0
        std::sin(kPi / 2.0f),   // 1.0
        std::sin(kPi),          // 0.0
        std::sin(3.0f * kPi / 2.0f)  // -1.0
    };

    SECTION("Interpolating between sin(90) and sin(180) at midpoint") {
        // True value: sin(135 deg) = sin(3*pi/4) ≈ 0.707
        float trueValue = std::sin(3.0f * kPi / 4.0f);

        // Cubic Hermite interpolation
        float cubic = cubicHermiteInterpolate(
            samples[0], samples[1], samples[2], samples[3], 0.5f
        );

        // Linear interpolation for comparison
        float linear = linearInterpolate(samples[1], samples[2], 0.5f);

        // Cubic should be closer to true value than linear
        float cubicError = std::abs(cubic - trueValue);
        float linearError = std::abs(linear - trueValue);

        INFO("True: " << trueValue << ", Cubic: " << cubic << ", Linear: " << linear);
        REQUIRE(cubicError < linearError);
    }
}

TEST_CASE("cubicHermiteInterpolate is constexpr", "[interpolation][constexpr][US4]") {
    SECTION("Can be used at compile time") {
        constexpr float result = cubicHermiteInterpolate(0.0f, 1.0f, 2.0f, 3.0f, 0.5f);
        REQUIRE(result == Approx(1.5f).margin(1e-5f));
    }

    SECTION("Boundary values at compile time") {
        constexpr float atZero = cubicHermiteInterpolate(0.0f, 1.0f, 2.0f, 3.0f, 0.0f);
        constexpr float atOne = cubicHermiteInterpolate(0.0f, 1.0f, 2.0f, 3.0f, 1.0f);
        REQUIRE(atZero == 1.0f);
        REQUIRE(atOne == 2.0f);
    }
}

TEST_CASE("cubicHermiteInterpolate is noexcept", "[interpolation][realtime][US3]") {
    REQUIRE(noexcept(cubicHermiteInterpolate(0.0f, 1.0f, 2.0f, 3.0f, 0.5f)));
}

// =============================================================================
// lagrangeInterpolate Tests (T056-T059 - FR-019)
// =============================================================================

TEST_CASE("lagrangeInterpolate boundary values (FR-022)", "[interpolation][US3]") {
    float ym1 = -1.0f, y0 = 0.0f, y1 = 1.0f, y2 = 2.0f;

    SECTION("t=0 returns y0 exactly") {
        float result = lagrangeInterpolate(ym1, y0, y1, y2, 0.0f);
        REQUIRE(result == Approx(y0).margin(1e-6f));
    }

    SECTION("t=1 returns y1 exactly") {
        float result = lagrangeInterpolate(ym1, y0, y1, y2, 1.0f);
        REQUIRE(result == Approx(y1).margin(1e-6f));
    }
}

TEST_CASE("lagrangeInterpolate linear data", "[interpolation][US3]") {
    // Spec US3 Scenario 3: linear samples [1, 2, 3, 4], midpoint -> 2.5
    float samples[] = {1.0f, 2.0f, 3.0f, 4.0f};

    SECTION("Exact for linear data at midpoint") {
        float result = lagrangeInterpolate(
            samples[0], samples[1], samples[2], samples[3], 0.5f
        );
        REQUIRE(result == Approx(2.5f).margin(1e-5f));
    }

    SECTION("Exact for linear data at quarter-point") {
        float result = lagrangeInterpolate(
            samples[0], samples[1], samples[2], samples[3], 0.25f
        );
        REQUIRE(result == Approx(2.25f).margin(1e-5f));
    }

    SECTION("Exact for linear data at three-quarter-point") {
        float result = lagrangeInterpolate(
            samples[0], samples[1], samples[2], samples[3], 0.75f
        );
        REQUIRE(result == Approx(2.75f).margin(1e-5f));
    }
}

TEST_CASE("lagrangeInterpolate quadratic data", "[interpolation][US3]") {
    // Quadratic: y = x^2 at x = -1, 0, 1, 2 -> 1, 0, 1, 4
    float samples[] = {1.0f, 0.0f, 1.0f, 4.0f};

    SECTION("Quadratic at midpoint (x=0.5)") {
        // True value: 0.5^2 = 0.25
        float result = lagrangeInterpolate(
            samples[0], samples[1], samples[2], samples[3], 0.5f
        );
        REQUIRE(result == Approx(0.25f).margin(1e-5f));
    }

    SECTION("Quadratic at quarter-point (x=0.25)") {
        // True value: 0.25^2 = 0.0625
        float result = lagrangeInterpolate(
            samples[0], samples[1], samples[2], samples[3], 0.25f
        );
        REQUIRE(result == Approx(0.0625f).margin(1e-5f));
    }
}

TEST_CASE("lagrangeInterpolate cubic data", "[interpolation][US3]") {
    // Cubic: y = x^3 at x = -1, 0, 1, 2 -> -1, 0, 1, 8
    float samples[] = {-1.0f, 0.0f, 1.0f, 8.0f};

    SECTION("Cubic at midpoint (x=0.5)") {
        // True value: 0.5^3 = 0.125
        float result = lagrangeInterpolate(
            samples[0], samples[1], samples[2], samples[3], 0.5f
        );
        REQUIRE(result == Approx(0.125f).margin(1e-5f));
    }
}

TEST_CASE("lagrangeInterpolate is constexpr", "[interpolation][constexpr][US4]") {
    SECTION("Can be used at compile time") {
        constexpr float result = lagrangeInterpolate(1.0f, 2.0f, 3.0f, 4.0f, 0.5f);
        REQUIRE(result == Approx(2.5f).margin(1e-5f));
    }

    SECTION("Boundary values at compile time") {
        constexpr float atZero = lagrangeInterpolate(1.0f, 2.0f, 3.0f, 4.0f, 0.0f);
        constexpr float atOne = lagrangeInterpolate(1.0f, 2.0f, 3.0f, 4.0f, 1.0f);
        REQUIRE(atZero == Approx(2.0f).margin(1e-5f));
        REQUIRE(atOne == Approx(3.0f).margin(1e-5f));
    }
}

TEST_CASE("lagrangeInterpolate is noexcept", "[interpolation][realtime][US3]") {
    REQUIRE(noexcept(lagrangeInterpolate(0.0f, 1.0f, 2.0f, 3.0f, 0.5f)));
}

// =============================================================================
// Comparison Tests (T060-T062)
// =============================================================================

TEST_CASE("Interpolation methods comparison", "[interpolation][US3]") {
    SECTION("All methods agree on linear data") {
        float ym1 = 0.0f, y0 = 1.0f, y1 = 2.0f, y2 = 3.0f;
        float t = 0.5f;

        float linear = linearInterpolate(y0, y1, t);
        float hermite = cubicHermiteInterpolate(ym1, y0, y1, y2, t);
        float lagrange = lagrangeInterpolate(ym1, y0, y1, y2, t);

        REQUIRE(linear == Approx(1.5f).margin(1e-5f));
        REQUIRE(hermite == Approx(1.5f).margin(1e-5f));
        REQUIRE(lagrange == Approx(1.5f).margin(1e-5f));
    }

    SECTION("Cubic methods differ on curved data") {
        // Parabola: y = x^2 at -1, 0, 1, 2
        float ym1 = 1.0f, y0 = 0.0f, y1 = 1.0f, y2 = 4.0f;
        float t = 0.5f;

        float linear = linearInterpolate(y0, y1, t);
        float hermite = cubicHermiteInterpolate(ym1, y0, y1, y2, t);
        float lagrange = lagrangeInterpolate(ym1, y0, y1, y2, t);

        // Linear gives 0.5 (wrong)
        // True value is 0.25
        // Cubic methods should be closer to 0.25 than linear
        float trueValue = 0.25f;

        INFO("Linear: " << linear << ", Hermite: " << hermite << ", Lagrange: " << lagrange);
        REQUIRE(std::abs(lagrange - trueValue) < std::abs(linear - trueValue));
    }
}

TEST_CASE("Practical use case: fractional delay", "[interpolation][US3]") {
    // Simulating reading from a delay line with fractional sample position
    std::array<float, 8> buffer = {0.0f, 0.1f, 0.4f, 0.9f, 1.6f, 2.5f, 3.6f, 4.9f};

    SECTION("Read at fractional position 2.5") {
        // Reading between samples 2 and 3
        size_t index = 2;
        float frac = 0.5f;

        // Linear: simple but lower quality
        float linear = linearInterpolate(buffer[index], buffer[index + 1], frac);
        REQUIRE(linear == Approx(0.65f).margin(1e-5f));  // (0.4 + 0.9) / 2

        // Cubic Hermite: better quality
        float hermite = cubicHermiteInterpolate(
            buffer[index - 1], buffer[index], buffer[index + 1], buffer[index + 2], frac
        );
        // Result should be smooth through the sample points

        // Lagrange: highest accuracy
        float lagrange = lagrangeInterpolate(
            buffer[index - 1], buffer[index], buffer[index + 1], buffer[index + 2], frac
        );

        // All should be in reasonable range
        REQUIRE(hermite >= 0.4f);
        REQUIRE(hermite <= 0.9f);
        REQUIRE(lagrange >= 0.4f);
        REQUIRE(lagrange <= 0.9f);
    }
}
