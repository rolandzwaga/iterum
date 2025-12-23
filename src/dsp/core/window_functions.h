// ==============================================================================
// Layer 0: Core Utility - Window Functions
// ==============================================================================
// Window function generators for STFT analysis and spectral processing.
// Includes Hann, Hamming, Blackman, Kaiser windows with COLA verification.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, allocation in generate() only)
// - Principle III: Modern C++ (constexpr where possible, C++20)
// - Principle IX: Layer 0 (no DSP primitive dependencies)
// - Principle XII: Test-First Development
//
// Reference: specs/007-fft-processor/spec.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// Forward Declarations
// =============================================================================

enum class WindowType : uint8_t;

// =============================================================================
// Constants
// =============================================================================

/// Pi constant for window calculations
inline constexpr float kPi = 3.14159265358979323846f;

/// Two pi constant
inline constexpr float kTwoPi = 2.0f * kPi;

/// Default Kaiser beta parameter (good sidelobe rejection ~80dB)
inline constexpr float kDefaultKaiserBeta = 9.0f;

/// Default COLA tolerance for verification
inline constexpr float kDefaultCOLATolerance = 1e-6f;

// =============================================================================
// Window Type Enumeration
// =============================================================================

/// @brief Supported window function types for STFT analysis
enum class WindowType : uint8_t {
    Hann,       ///< Hann (Hanning) window - COLA at 50%/75% overlap
    Hamming,    ///< Hamming window - COLA at 50%/75% overlap
    Blackman,   ///< Blackman window - COLA at 50%/75% overlap
    Kaiser      ///< Kaiser window - requires 90% overlap for COLA
};

// =============================================================================
// Window Namespace - Free Functions
// =============================================================================

namespace Window {

// -----------------------------------------------------------------------------
// Modified Bessel Function (for Kaiser window)
// -----------------------------------------------------------------------------

/// @brief Modified Bessel function of the first kind, order 0
/// @param x Input value
/// @return I0(x) approximation using series expansion
/// @note Used for Kaiser window computation
/// @note Real-time safe, noexcept
[[nodiscard]] inline float besselI0(float x) noexcept {
    // Series expansion: I0(x) = sum_{k=0}^{inf} [(x/2)^k / k!]^2
    // Converges quickly for moderate x values
    // Reference: Abramowitz & Stegun, Chapter 9

    constexpr int kMaxIterations = 20;
    constexpr float kEpsilon = 1e-10f;

    const float halfX = x * 0.5f;
    float sum = 1.0f;
    float term = 1.0f;

    for (int k = 1; k < kMaxIterations; ++k) {
        term *= (halfX / static_cast<float>(k)) * (halfX / static_cast<float>(k));
        sum += term;

        // Early termination for convergence
        if (term < kEpsilon * sum) {
            break;
        }
    }

    return sum;
}

// -----------------------------------------------------------------------------
// Window Generators (In-Place)
// -----------------------------------------------------------------------------

/// @brief Fill buffer with Hann window (periodic/DFT-even variant)
/// @param output Destination buffer
/// @param size Window size
/// @note Formula: 0.5 - 0.5*cos(2*pi*n/N) (periodic variant)
/// @note Real-time safe if buffer is pre-allocated
inline void generateHann(float* output, size_t size) noexcept {
    if (output == nullptr || size == 0) return;

    const float N = static_cast<float>(size);
    for (size_t n = 0; n < size; ++n) {
        // Periodic (DFT-even) variant: divides by N, not N-1
        // This is the correct variant for STFT that satisfies COLA
        const float phase = kTwoPi * static_cast<float>(n) / N;
        output[n] = 0.5f - 0.5f * std::cos(phase);
    }
}

/// @brief Fill buffer with Hamming window
/// @param output Destination buffer
/// @param size Window size
/// @note Formula: 0.54 - 0.46*cos(2*pi*n/N)
inline void generateHamming(float* output, size_t size) noexcept {
    if (output == nullptr || size == 0) return;

    const float N = static_cast<float>(size);
    for (size_t n = 0; n < size; ++n) {
        const float phase = kTwoPi * static_cast<float>(n) / N;
        output[n] = 0.54f - 0.46f * std::cos(phase);
    }
}

/// @brief Fill buffer with Blackman window
/// @param output Destination buffer
/// @param size Window size
/// @note Formula: 0.42 - 0.5*cos(2*pi*n/N) + 0.08*cos(4*pi*n/N)
inline void generateBlackman(float* output, size_t size) noexcept {
    if (output == nullptr || size == 0) return;

    const float N = static_cast<float>(size);
    for (size_t n = 0; n < size; ++n) {
        const float phase = kTwoPi * static_cast<float>(n) / N;
        output[n] = 0.42f - 0.5f * std::cos(phase) + 0.08f * std::cos(2.0f * phase);
    }
}

/// @brief Fill buffer with Kaiser window
/// @param output Destination buffer
/// @param size Window size
/// @param beta Shape parameter (higher = narrower main lobe, lower sidelobes)
/// @note Formula: I0(beta*sqrt(1-(n/M)^2)) / I0(beta)
inline void generateKaiser(float* output, size_t size, float beta) noexcept {
    if (output == nullptr || size == 0) return;

    const float denominator = besselI0(beta);
    const float M = static_cast<float>(size - 1) * 0.5f;

    for (size_t n = 0; n < size; ++n) {
        // Normalized position: -1 <= x <= 1
        const float x = (static_cast<float>(n) - M) / M;
        const float x2 = x * x;

        // sqrt(1 - x^2) can be zero at endpoints
        float sqrtTerm = (x2 >= 1.0f) ? 0.0f : std::sqrt(1.0f - x2);

        output[n] = besselI0(beta * sqrtTerm) / denominator;
    }
}

// -----------------------------------------------------------------------------
// COLA Verification
// -----------------------------------------------------------------------------

/// @brief Verify COLA (Constant Overlap-Add) property
/// @param window Window coefficients
/// @param size Window size
/// @param hopSize Frame advance in samples
/// @param tolerance Maximum deviation from unity (default: 1e-6)
/// @return true if overlapping windows sum to constant within tolerance
[[nodiscard]] inline bool verifyCOLA(
    const float* window,
    size_t size,
    size_t hopSize,
    float tolerance = kDefaultCOLATolerance
) noexcept {
    if (window == nullptr || size == 0 || hopSize == 0 || hopSize > size) {
        return false;
    }

    // COLA property: sum of overlapping windows should be constant
    // For synthesis windows, w[n] + w[n + hop] + w[n + 2*hop] + ... = constant
    //
    // We verify this by computing the sum at each position within one hop period
    // All positions should sum to the same value (ideally 1.0 for unity gain)

    // Number of overlapping frames
    const size_t numOverlaps = (size + hopSize - 1) / hopSize;

    // First, compute what the sum should be at position 0
    float referenceSum = 0.0f;
    for (size_t frame = 0; frame < numOverlaps; ++frame) {
        const size_t idx = frame * hopSize;
        if (idx < size) {
            referenceSum += window[idx];
        }
    }

    // Check that all positions within one hop period have the same sum
    for (size_t pos = 0; pos < hopSize; ++pos) {
        float sum = 0.0f;
        for (size_t frame = 0; frame < numOverlaps; ++frame) {
            // Index into window array, accounting for frame offset
            // Position in current frame = pos
            // But frame starts at frame * hopSize samples into the input
            // So we need: window[(pos + frame * hopSize) % arrangements]
            // Actually: each frame contributes window[pos] when its index aligns

            // For frame 0: position pos is at window[pos]
            // For frame 1: position pos is at window[pos - hopSize] if pos >= hopSize
            // We're checking if overlapping windows sum to constant

            // Recompute: at output position 'pos', which window samples contribute?
            // Frame 0 contributes window[pos] if pos < size
            // Frame 1 contributes window[pos + hopSize] if pos + hopSize < size
            // etc.

            const size_t idx = pos + frame * hopSize;
            if (idx < size) {
                sum += window[idx];
            }
        }

        // Check against reference
        if (std::abs(sum - referenceSum) > tolerance) {
            return false;
        }
    }

    // Also verify that the sum is close to unity (or at least non-zero)
    // For proper COLA, the sum should be 1.0
    // But we allow some tolerance for windowed-sinc synthesis
    return referenceSum > 0.1f;  // Must have meaningful gain
}

// -----------------------------------------------------------------------------
// Factory Function
// -----------------------------------------------------------------------------

/// @brief Generate window coefficients (allocates vector)
/// @param type Window type
/// @param size Window size
/// @param kaiserBeta Kaiser beta parameter (only used if type == Kaiser)
/// @return Vector of window coefficients
/// @note NOT real-time safe (allocates memory)
[[nodiscard]] inline std::vector<float> generate(
    WindowType type,
    size_t size,
    float kaiserBeta = kDefaultKaiserBeta
) {
    std::vector<float> window(size, 0.0f);

    switch (type) {
        case WindowType::Hann:
            generateHann(window.data(), size);
            break;
        case WindowType::Hamming:
            generateHamming(window.data(), size);
            break;
        case WindowType::Blackman:
            generateBlackman(window.data(), size);
            break;
        case WindowType::Kaiser:
            generateKaiser(window.data(), size, kaiserBeta);
            break;
    }

    return window;
}

} // namespace Window

} // namespace DSP
} // namespace Iterum
