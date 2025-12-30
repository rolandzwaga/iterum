// ==============================================================================
// Layer 0: Core Utility - Crossfade Utilities
// ==============================================================================
// Shared crossfade math for smooth audio transitions.
//
// Used by:
// - 041-mode-switch-clicks: Mode transition crossfade in Processor
// - CharacterProcessor: Character mode transitions
// - CrossfadingDelayLine: Delay time changes (optional upgrade)
//
// Constitution Compliance:
// - Principle IX: Layer 0 (no dependencies except standard library)
// - Principle XIV: ODR Prevention (single definition for all consumers)
// ==============================================================================

#pragma once

#include <cmath>
#include <utility>

namespace Iterum {
namespace DSP {

/// Pi/2 constant for crossfade calculations
constexpr float kHalfPi = 1.5707963267948966f;

/// @brief Calculate equal-power crossfade gains (constant power: fadeOut² + fadeIn² ≈ 1)
///
/// Equal-power crossfade maintains constant perceived loudness during transitions
/// by using sine/cosine curves instead of linear interpolation.
///
/// At position 0.0: fadeOut=1.0, fadeIn=0.0 (full outgoing signal)
/// At position 0.5: fadeOut≈0.707, fadeIn≈0.707 (equal blend)
/// At position 1.0: fadeOut=0.0, fadeIn=1.0 (full incoming signal)
///
/// @param position Crossfade position [0.0 = start, 1.0 = complete]
/// @param fadeOut Output gain for outgoing signal (1.0 → 0.0)
/// @param fadeIn Output gain for incoming signal (0.0 → 1.0)
///
/// @note Real-time safe: noexcept, no allocations
/// @note Does NOT clamp position - caller is responsible for keeping it in [0, 1]
///
/// @par Example
/// @code
/// float fadeOut, fadeIn;
/// equalPowerGains(0.5f, fadeOut, fadeIn);
/// // fadeOut ≈ 0.707, fadeIn ≈ 0.707
/// float blended = oldSignal * fadeOut + newSignal * fadeIn;
/// @endcode
inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept {
    fadeOut = std::cos(position * kHalfPi);
    fadeIn = std::sin(position * kHalfPi);
}

/// @brief Single-call version returning both gains as a pair
///
/// Convenience overload for structured bindings:
/// @code
/// auto [fadeOut, fadeIn] = equalPowerGains(position);
/// @endcode
///
/// @param position Crossfade position [0.0 = start, 1.0 = complete]
/// @return {fadeOut, fadeIn} gains as std::pair
[[nodiscard]] inline std::pair<float, float> equalPowerGains(float position) noexcept {
    return {std::cos(position * kHalfPi), std::sin(position * kHalfPi)};
}

/// @brief Calculate crossfade increment for given duration and sample rate
///
/// Returns the per-sample increment value to advance crossfade position from 0 to 1
/// over the specified duration.
///
/// @param durationMs Crossfade duration in milliseconds
/// @param sampleRate Sample rate in Hz
/// @return Per-sample increment value
///
/// @note Returns 1.0 if duration is 0 or negative (instant crossfade)
///
/// @par Example
/// @code
/// // 50ms crossfade at 44.1kHz
/// float increment = crossfadeIncrement(50.0f, 44100.0);
/// // increment ≈ 0.000453 (1/2205)
///
/// // Usage in process loop:
/// position += increment;
/// if (position >= 1.0f) position = 1.0f;
/// @endcode
[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept {
    const float samples = durationMs * 0.001f * static_cast<float>(sampleRate);
    return (samples > 0.0f) ? (1.0f / samples) : 1.0f;
}

} // namespace DSP
} // namespace Iterum
