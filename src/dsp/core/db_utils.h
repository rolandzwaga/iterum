// ==============================================================================
// Layer 0: Core Utilities
// db_utils.h - dB/Linear Conversion Functions
// ==============================================================================
// Constitution Principle II: Real-Time Audio Thread Safety
// - No allocation, no locks, no exceptions, no I/O
//
// Constitution Principle III: Modern C++ Standards
// - constexpr, const, value semantics
//
// Constitution Principle IX: Layered DSP Architecture
// - Layer 0: NO dependencies on higher layers
// ==============================================================================

#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Iterum {
namespace DSP {

// ==============================================================================
// Constants
// ==============================================================================

/// Floor value for silence/zero gain in decibels.
/// Represents approximately 24-bit dynamic range (6.02 dB/bit * 24 = ~144 dB).
/// Used as the return value when gain is zero, negative, or NaN.
constexpr float kSilenceFloorDb = -144.0f;

namespace detail {

/// Constexpr-safe NaN check using IEEE 754 bit pattern.
///
/// Uses std::bit_cast to examine the binary representation of the float.
/// NaN is defined as: exponent = all 1s (0xFF) AND mantissa != 0
///
/// IMPORTANT: The VST3 SDK enables -ffast-math globally, which causes
/// __builtin_isnan() and std::isnan() to be optimized out (the compiler
/// assumes NaN doesn't exist). Source files using this function MUST be
/// compiled with -fno-fast-math. See tests/CMakeLists.txt for the pattern.
///
/// This bit manipulation approach works correctly when -ffast-math is
/// disabled because it operates on integer bits, not floating-point
/// semantics.
constexpr bool isNaN(float x) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(x);
    // NaN: exponent = 0xFF (all 1s), mantissa != 0
    return ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
}

/// Natural log of 10, used in dB conversions
constexpr float kLn10 = 2.302585093f;

/// 1 / ln(10), used for log10 calculation
constexpr float kInvLn10 = 0.434294482f;

/// Constexpr natural logarithm using series expansion
/// Uses the identity: ln(x) = 2 * sum((z^(2n+1))/(2n+1)) where z = (x-1)/(x+1)
/// Valid for x > 0
constexpr float constexprLn(float x) noexcept {
    if (isNaN(x)) return std::numeric_limits<float>::quiet_NaN();
    if (x <= 0.0f) return -std::numeric_limits<float>::infinity();
    if (x == std::numeric_limits<float>::infinity()) return std::numeric_limits<float>::infinity();
    if (x == 1.0f) return 0.0f;

    // Reduce x to range [0.5, 2] for better convergence
    // ln(x * 2^n) = ln(x) + n * ln(2)
    constexpr float kLn2 = 0.693147181f;
    int exponent = 0;
    float mantissa = x;

    // Limit iterations to prevent infinite loops (max 150 for float range)
    for (int iter = 0; iter < 150 && mantissa > 2.0f; ++iter) {
        mantissa *= 0.5f;
        exponent++;
    }
    for (int iter = 0; iter < 150 && mantissa < 0.5f; ++iter) {
        mantissa *= 2.0f;
        exponent--;
    }

    // Series expansion: ln(x) = 2 * (z + z^3/3 + z^5/5 + z^7/7 + ...)
    // where z = (x-1)/(x+1)
    float z = (mantissa - 1.0f) / (mantissa + 1.0f);
    float z2 = z * z;
    float term = z;
    float sum = z;

    // 12 terms gives good accuracy for float
    for (int i = 1; i <= 12; ++i) {
        term *= z2;
        sum += term / (2.0f * static_cast<float>(i) + 1.0f);
    }

    return 2.0f * sum + static_cast<float>(exponent) * kLn2;
}

/// Constexpr log10 using natural log
constexpr float constexprLog10(float x) noexcept {
    return constexprLn(x) * kInvLn10;
}

/// Constexpr exponential function using Taylor series
/// exp(x) = 1 + x + x^2/2! + x^3/3! + ...
constexpr float constexprExp(float x) noexcept {
    // Handle special cases
    if (isNaN(x)) return std::numeric_limits<float>::quiet_NaN();
    if (x == 0.0f) return 1.0f;
    if (x > 88.0f) return std::numeric_limits<float>::infinity();
    if (x < -88.0f) return 0.0f;

    // Reduce x to range [-1, 1] for better convergence
    // exp(x) = exp(x/n)^n, use powers of 2 for efficiency
    constexpr float kLn2 = 0.693147181f;
    int k = static_cast<int>(x / kLn2);
    float r = x - static_cast<float>(k) * kLn2;

    // Taylor series for exp(r) where |r| <= ln(2)/2
    float term = 1.0f;
    float sum = 1.0f;

    for (int i = 1; i <= 16; ++i) {
        term *= r / static_cast<float>(i);
        sum += term;
        if (term < 1e-10f && term > -1e-10f) break;
    }

    // Multiply by 2^k (bounded to prevent infinite loops)
    if (k >= 0) {
        for (int i = 0; i < k && i < 150; ++i) sum *= 2.0f;
    } else {
        for (int i = 0; i > k && i > -150; --i) sum *= 0.5f;
    }

    return sum;
}

/// Constexpr pow(10, x) = exp(x * ln(10))
constexpr float constexprPow10(float x) noexcept {
    return constexprExp(x * kLn10);
}

} // namespace detail

// ==============================================================================
// Functions
// ==============================================================================

/// Convert decibels to linear gain.
///
/// @param dB  Decibel value (any finite float)
/// @return    Linear gain multiplier (>= 0)
///
/// @formula   gain = 10^(dB/20)
///
/// @note      Real-time safe: no allocation, no exceptions
/// @note      Constexpr: usable at compile time (C++20)
/// @note      NaN input returns 0.0f
///
/// @example   dbToGain(0.0f)    -> 1.0f     (unity gain)
/// @example   dbToGain(-6.02f)  -> ~0.5f    (half amplitude)
/// @example   dbToGain(-20.0f)  -> 0.1f     (-20 dB)
/// @example   dbToGain(+20.0f)  -> 10.0f    (+20 dB)
///
[[nodiscard]] constexpr float dbToGain(float dB) noexcept {
    // NaN check using helper function
    if (detail::isNaN(dB)) {
        return 0.0f;
    }
    return detail::constexprPow10(dB / 20.0f);
}

/// Convert linear gain to decibels.
///
/// @param gain  Linear gain value
/// @return      Decibel value (clamped to kSilenceFloorDb minimum)
///
/// @formula     dB = 20 * log10(gain), clamped to floor for invalid inputs
///
/// @note        Real-time safe: no allocation, no exceptions
/// @note        Constexpr: usable at compile time (C++20)
/// @note        Zero/negative/NaN input returns kSilenceFloorDb (-144 dB)
///
/// @example     gainToDb(1.0f)   -> 0.0f      (unity = 0 dB)
/// @example     gainToDb(0.5f)   -> ~-6.02f   (half amplitude)
/// @example     gainToDb(0.0f)   -> -144.0f   (silence floor)
/// @example     gainToDb(-1.0f)  -> -144.0f   (invalid -> floor)
///
[[nodiscard]] constexpr float gainToDb(float gain) noexcept {
    // NaN or non-positive check using helper function
    if (detail::isNaN(gain) || gain <= 0.0f) {
        return kSilenceFloorDb;
    }
    float result = 20.0f * detail::constexprLog10(gain);
    return (result < kSilenceFloorDb) ? kSilenceFloorDb : result;
}

} // namespace DSP
} // namespace Iterum
