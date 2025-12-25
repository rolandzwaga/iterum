// Layer 0: Core Utilities - Stereo Utilities
// Feature: 019-feedback-network
//
// Provides reusable stereo signal processing utilities.
// Used by:
// - 019-feedback-network: Cross-feedback routing
// - 022-stereo-field: Ping-pong mode (future)
// - 023-tap-manager: Per-tap stereo routing (future)

#pragma once

namespace Iterum {
namespace DSP {

/// @brief Apply stereo cross-blend routing (FR-017)
///
/// Blends left and right channels based on crossAmount:
/// - 0.0: No cross (normal stereo, L->L, R->R)
/// - 0.5: Mono blend (both channels become (L+R)/2)
/// - 1.0: Full swap / ping-pong (L->R, R->L)
///
/// Formula:
///   outL = inL * (1 - crossAmount) + inR * crossAmount
///   outR = inR * (1 - crossAmount) + inL * crossAmount
///
/// @param inL Left input sample
/// @param inR Right input sample
/// @param crossAmount Cross-blend amount [0.0, 1.0]
/// @param outL Output: blended left sample
/// @param outR Output: blended right sample
///
/// @note constexpr and noexcept for real-time safety and compile-time usage
/// @note Does NOT clamp crossAmount - caller is responsible for validation
///
/// @par Example
/// @code
/// float outL, outR;
/// stereoCrossBlend(1.0f, 0.0f, 1.0f, outL, outR);
/// // outL = 0.0f, outR = 1.0f (full swap)
/// @endcode
constexpr void stereoCrossBlend(
    float inL, float inR,
    float crossAmount,
    float& outL, float& outR
) noexcept {
    const float keep = 1.0f - crossAmount;
    outL = inL * keep + inR * crossAmount;
    outR = inR * keep + inL * crossAmount;
}

} // namespace DSP
} // namespace Iterum
