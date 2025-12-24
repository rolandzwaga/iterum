// ==============================================================================
// Layer 0: Core Utility - Interpolation
// ==============================================================================
// Standalone interpolation utilities for sample-domain operations.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (no allocation, noexcept)
// - Principle III: Modern C++ (constexpr, inline)
// - Principle IX: Layer 0 (no dependencies on higher layers)
//
// Reference: specs/017-layer0-utilities/spec.md
// ==============================================================================

#pragma once

namespace Iterum {
namespace DSP {
namespace Interpolation {

// =============================================================================
// Linear Interpolation (FR-017)
// =============================================================================

/// @brief Linear interpolation between two samples.
///
/// @param y0 Sample at position 0
/// @param y1 Sample at position 1
/// @param t Fractional position in [0, 1]
/// @return Interpolated value
///
/// @note Returns y0 when t=0, y1 when t=1 (FR-022)
/// @note For t outside [0,1], extrapolates linearly
///
/// @formula y = y0 + t * (y1 - y0) = (1-t)*y0 + t*y1
///
/// @example
/// @code
/// float mid = linearInterpolate(0.0f, 1.0f, 0.5f);  // = 0.5
/// constexpr float quarter = linearInterpolate(0.0f, 4.0f, 0.25f);  // = 1.0
/// @endcode
[[nodiscard]] constexpr float linearInterpolate(
    float y0,
    float y1,
    float t
) noexcept {
    return y0 + t * (y1 - y0);
}

// =============================================================================
// Cubic Hermite (Catmull-Rom) Interpolation (FR-018)
// =============================================================================

/// @brief Cubic Hermite (Catmull-Rom) interpolation using 4 samples.
///
/// Provides smooth interpolation with continuous first derivative.
/// Uses Catmull-Rom spline formulation (tension = 0.5).
///
/// @param ym1 Sample at position -1 (before y0)
/// @param y0 Sample at position 0
/// @param y1 Sample at position 1
/// @param y2 Sample at position 2 (after y1)
/// @param t Fractional position in [0, 1] between y0 and y1
/// @return Interpolated value
///
/// @note Returns y0 when t=0, y1 when t=1 (FR-022)
/// @note Provides better quality than linear for pitch shifting
///
/// @formula
/// c0 = y0
/// c1 = 0.5 * (y1 - ym1)
/// c2 = ym1 - 2.5*y0 + 2*y1 - 0.5*y2
/// c3 = 0.5*(y2 - ym1) + 1.5*(y0 - y1)
/// y = ((c3*t + c2)*t + c1)*t + c0
///
/// @example
/// @code
/// // Sine wave samples at 0, 90, 180, 270 degrees
/// float samples[] = {0.0f, 1.0f, 0.0f, -1.0f};
/// float interp = cubicHermiteInterpolate(
///     samples[0], samples[1], samples[2], samples[3], 0.5f
/// );
/// // Result is closer to true sin(135 deg) ~= 0.707 than linear would give
/// @endcode
[[nodiscard]] constexpr float cubicHermiteInterpolate(
    float ym1,
    float y0,
    float y1,
    float y2,
    float t
) noexcept {
    // Catmull-Rom spline coefficients (tension = 0.5)
    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);

    // Horner's method evaluation: ((c3*t + c2)*t + c1)*t + c0
    return ((c3 * t + c2) * t + c1) * t + c0;
}

// =============================================================================
// Lagrange Interpolation (FR-019)
// =============================================================================

/// @brief 4-point Lagrange interpolation.
///
/// Provides polynomial interpolation through 4 sample points.
/// Third-order polynomial, no smoothness guarantee at boundaries.
///
/// @param ym1 Sample at position -1 (before y0)
/// @param y0 Sample at position 0
/// @param y1 Sample at position 1
/// @param y2 Sample at position 2 (after y1)
/// @param t Fractional position in [0, 1] between y0 and y1
/// @return Interpolated value
///
/// @note Returns y0 when t=0, y1 when t=1 (FR-022)
/// @note More computationally expensive than Hermite
/// @note Suitable for oversampling and filter design
///
/// @formula
/// L0 = -t*(t-1)*(t-2)/6
/// L1 = (t+1)*(t-1)*(t-2)/2
/// L2 = -(t+1)*t*(t-2)/2
/// L3 = (t+1)*t*(t-1)/6
/// y = L0*ym1 + L1*y0 + L2*y1 + L3*y2
///
/// @example
/// @code
/// float samples[] = {1.0f, 2.0f, 3.0f, 4.0f};  // Linear samples
/// float interp = lagrangeInterpolate(
///     samples[0], samples[1], samples[2], samples[3], 0.5f
/// );
/// // Result = 2.5 (exact for linear data)
/// @endcode
[[nodiscard]] constexpr float lagrangeInterpolate(
    float ym1,
    float y0,
    float y1,
    float y2,
    float t
) noexcept {
    // Lagrange basis polynomials for 4 points at positions -1, 0, 1, 2
    // L_i(t) = product((t - xj) / (xi - xj)) for j != i

    // Pre-compute common terms
    const float tp1 = t + 1.0f;  // t - (-1) = t + 1
    const float tm1 = t - 1.0f;
    const float tm2 = t - 2.0f;

    // L0: basis for position -1
    // L0(t) = (t-0)(t-1)(t-2) / ((-1-0)(-1-1)(-1-2)) = t*(t-1)*(t-2) / (-1*-2*-3) = -t*(t-1)*(t-2)/6
    const float L0 = -t * tm1 * tm2 / 6.0f;

    // L1: basis for position 0
    // L1(t) = (t-(-1))(t-1)(t-2) / ((0-(-1))(0-1)(0-2)) = (t+1)*(t-1)*(t-2) / (1*-1*-2) = (t+1)*(t-1)*(t-2)/2
    const float L1 = tp1 * tm1 * tm2 / 2.0f;

    // L2: basis for position 1
    // L2(t) = (t-(-1))(t-0)(t-2) / ((1-(-1))(1-0)(1-2)) = (t+1)*t*(t-2) / (2*1*-1) = -(t+1)*t*(t-2)/2
    const float L2 = -tp1 * t * tm2 / 2.0f;

    // L3: basis for position 2
    // L3(t) = (t-(-1))(t-0)(t-1) / ((2-(-1))(2-0)(2-1)) = (t+1)*t*(t-1) / (3*2*1) = (t+1)*t*(t-1)/6
    const float L3 = tp1 * t * tm1 / 6.0f;

    return L0 * ym1 + L1 * y0 + L2 * y1 + L3 * y2;
}

} // namespace Interpolation
} // namespace DSP
} // namespace Iterum
