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
#include <type_traits>

namespace Krate {
namespace DSP {

// ==============================================================================
// Constants
// ==============================================================================

/// Floor value for silence/zero gain in decibels.
/// Represents approximately 24-bit dynamic range (6.02 dB/bit * 24 = ~144 dB).
/// Used as the return value when gain is zero, negative, or NaN.
constexpr float kSilenceFloorDb = -144.0f;

/// Threshold below which values are flushed to zero (denormal prevention).
/// Values smaller than this in absolute magnitude become 0 to avoid
/// denormalized floating-point numbers which cause significant CPU slowdowns.
inline constexpr float kDenormalThreshold = 1e-15f;

namespace detail {

/// Optimization barrier: the float's bit pattern, laundered so the optimizer
/// cannot fold classification checks on it.
///
/// Under -ffast-math (-ffinite-math-only) the compiler is entitled to assume
/// no FP value is ever NaN or Inf, and newer compilers (observed: Apple Clang,
/// Xcode 26.6) propagate that assumption through function arguments (LLVM
/// `nofpclass`) far enough to fold even a std::bit_cast bit-pattern test to
/// "finite" — which silently deletes every NaN/Inf guard in the DSP. Routing
/// the bits through an empty asm with an integer register operand severs all
/// value-provenance facts: the compiler must test the bits the hardware
/// actually produced. The asm emits no instructions; the only cost is keeping
/// the value in a general-purpose register for the test it was about to do
/// anyway. MSVC has no GNU asm and (as of /fp:fast today) performs no such
/// folding, so it keeps the plain path.
[[nodiscard]] inline std::uint32_t opaqueFloatBits(float x) noexcept {
    auto bits = std::bit_cast<std::uint32_t>(x);
#if defined(__GNUC__) || defined(__clang__)
    __asm__("" : "+r"(bits));
#endif
    return bits;
}

// The is_constant_evaluated() branches below are the standard C++20 dual-path
// idiom; clang's -Wconstant-evaluated / MSVC's C5063 fire whenever the
// functions are themselves evaluated at compile time (static_assert /
// constexpr tables), which is expected and harmless — same treatment as
// -Wnan-infinity-disabled further down.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconstant-evaluated"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 5063)
#endif

/// Constexpr-safe NaN check using IEEE 754 bit pattern.
///
/// Uses std::bit_cast to examine the binary representation of the float.
/// NaN is defined as: exponent = all 1s (0xFF) AND mantissa != 0
///
/// Survives -ffast-math: at runtime the bits are read through the
/// opaqueFloatBits barrier above, so the finite-math assumption cannot fold
/// the test (it operates on integer bits with no FP provenance). In constant
/// evaluation the plain bit_cast path is used — constexpr callers
/// (constexprLn etc.) are unaffected by -ffast-math anyway.
constexpr bool isNaN(float x) noexcept {
    // NaN: exponent = 0xFF (all 1s), mantissa != 0
    const auto bits = std::is_constant_evaluated() ? std::bit_cast<std::uint32_t>(x)
                                                   : opaqueFloatBits(x);
    return ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
}

/// Optimization barrier, double flavour (see opaqueFloatBits).
[[nodiscard]] inline std::uint64_t opaqueDoubleBits(double x) noexcept {
    auto bits = std::bit_cast<std::uint64_t>(x);
#if defined(__GNUC__) || defined(__clang__)
    __asm__("" : "+r"(bits));
#endif
    return bits;
}

/// Fast-math-immune finiteness check (neither NaN nor Inf), one barrier read.
/// Use this instead of local memcpy/bit-mask clones — a plain bit-pattern
/// test is exactly what newer fast-math compilers fold away.
[[nodiscard]] constexpr bool isFinite(float x) noexcept {
    const auto bits = std::is_constant_evaluated() ? std::bit_cast<std::uint32_t>(x)
                                                   : opaqueFloatBits(x);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// Fast-math-immune finiteness check, double overload.
[[nodiscard]] constexpr bool isFinite(double x) noexcept {
    const auto bits = std::is_constant_evaluated() ? std::bit_cast<std::uint64_t>(x)
                                                   : opaqueDoubleBits(x);
    return (bits & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

/// Natural log of 10, used in dB conversions
constexpr float kLn10 = 2.302585093f;

/// 1 / ln(10), used for log10 calculation
constexpr float kInvLn10 = 0.434294482f;

/// Natural log of 2. Shared by every constexprExp(x * ln 2) constant.
constexpr float kLn2 = 0.693147181f;

// Suppress -Wnan-infinity-disabled for constexpr math functions that legitimately
// use infinity. These are evaluated at compile time, not affected by -ffast-math.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnan-infinity-disabled"
#endif

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

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

/// Flush denormal values to zero for real-time safety.
/// Denormalized floats can cause 100x CPU slowdowns on some processors.
/// @param x Value to check
/// @return 0 if |x| < kDenormalThreshold, otherwise x
[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept {
    return (x > -kDenormalThreshold && x < kDenormalThreshold) ? 0.0f : x;
}

/// Platform-independent infinity check using bit manipulation.
/// Survives -ffast-math via the opaqueFloatBits barrier (see isNaN above).
/// @param x Value to check
/// @return true if x is positive or negative infinity
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconstant-evaluated"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 5063)
#endif
[[nodiscard]] constexpr bool isInf(float x) noexcept {
    const auto bits = std::is_constant_evaluated() ? std::bit_cast<std::uint32_t>(x)
                                                   : opaqueFloatBits(x);
    return (bits & 0x7FFFFFFFu) == 0x7F800000u;
}
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

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
} // namespace Krate
