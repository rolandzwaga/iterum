// ==============================================================================
// Layer 0: Core Utility - Note Value Enums
// ==============================================================================
// Musical note value types for tempo synchronization.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (trivial types, no allocation)
// - Principle III: Modern C++ (enum class, constexpr)
// - Principle IX: Layer 0 (no dependencies on higher layers)
//
// Reference: specs/017-layer0-utilities/spec.md
// ==============================================================================

#pragma once

#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Musical note divisions for tempo sync.
///
/// Used by BlockContext::tempoToSamples() and LFO tempo sync features.
/// Values represent standard Western notation durations.
enum class NoteValue : uint8_t {
    DoubleWhole = 0, ///< 2/1 note (8 beats at 4/4) - breve
    Whole,           ///< 1/1 note (4 beats at 4/4)
    Half,            ///< 1/2 note (2 beats)
    Quarter,         ///< 1/4 note (1 beat) - default
    Eighth,          ///< 1/8 note (0.5 beats)
    Sixteenth,       ///< 1/16 note (0.25 beats)
    ThirtySecond,    ///< 1/32 note (0.125 beats)
    SixtyFourth      ///< 1/64 note (0.0625 beats)
};

/// @brief Timing modifiers for note values.
///
/// Applied as multipliers to the base note duration.
enum class NoteModifier : uint8_t {
    None = 0,        ///< Normal duration (1.0x)
    Dotted,          ///< 1.5x duration
    Triplet          ///< 2/3x duration (~0.667x)
};

// =============================================================================
// Constants
// =============================================================================

/// @brief Beats per note value (at 4/4 time signature).
/// Array indexed by static_cast<size_t>(NoteValue).
inline constexpr float kBeatsPerNote[] = {
    8.0f,    // DoubleWhole (breve)
    4.0f,    // Whole
    2.0f,    // Half
    1.0f,    // Quarter
    0.5f,    // Eighth
    0.25f,   // Sixteenth
    0.125f,  // ThirtySecond
    0.0625f  // SixtyFourth
};

/// @brief Modifier multipliers.
/// Array indexed by static_cast<size_t>(NoteModifier).
inline constexpr float kModifierMultiplier[] = {
    1.0f,              // None
    1.5f,              // Dotted
    0.6666666666667f   // Triplet (2/3)
};

// =============================================================================
// Helper Functions
// =============================================================================

/// @brief Get the beat duration for a note value with modifier.
/// @param note Base note value
/// @param modifier Timing modifier (dotted, triplet, or none)
/// @return Duration in beats (e.g., quarter note = 1.0 beat)
[[nodiscard]] inline constexpr float getBeatsForNote(
    NoteValue note,
    NoteModifier modifier = NoteModifier::None
) noexcept {
    return kBeatsPerNote[static_cast<size_t>(note)] *
           kModifierMultiplier[static_cast<size_t>(modifier)];
}

} // namespace DSP
} // namespace Iterum
