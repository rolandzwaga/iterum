// ==============================================================================
// Layer 0: Core Utility - Math Constants
// ==============================================================================
// Centralized mathematical constants for DSP calculations.
// All DSP components should import these constants instead of defining locally.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (constexpr, no allocations)
// - Principle III: Modern C++ (constexpr, inline)
// - Principle IX: Layer 0 (no dependencies on other DSP layers)
// - Principle XII: Test-First Development
//
// Note: Constants are inline constexpr to ensure a single definition across
// all translation units (avoids ODR violations from multiple definitions).
// ==============================================================================

#pragma once

#include <cstddef>

namespace Iterum {
namespace DSP {

// =============================================================================
// Mathematical Constants
// =============================================================================

/// Pi constant for DSP calculations
/// Provides full float precision: 3.14159265358979323846
inline constexpr float kPi = 3.14159265358979323846f;

/// Two times Pi (full circle in radians)
/// Used for angular frequency calculations: omega = kTwoPi * f / fs
inline constexpr float kTwoPi = 2.0f * kPi;

/// Half Pi (quarter circle in radians)
/// Useful for phase calculations and sinusoidal LFO operations
inline constexpr float kHalfPi = kPi / 2.0f;

/// Pi squared
/// Used in some filter and window function calculations
inline constexpr float kPiSquared = kPi * kPi;

// =============================================================================
// Common Ratios
// =============================================================================

/// Golden ratio (phi)
/// Used in diffusion networks and allpass filter coefficients
inline constexpr float kGoldenRatio = 1.6180339887498948f;

/// Inverse golden ratio (1/phi = phi - 1)
/// Common allpass coefficient for maximum diffusion
inline constexpr float kInverseGoldenRatio = 0.6180339887498948f;

} // namespace DSP
} // namespace Iterum
