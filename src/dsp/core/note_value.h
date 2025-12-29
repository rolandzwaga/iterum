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

#include <cstddef>  // size_t
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

// =============================================================================
// Dropdown Index Mapping
// =============================================================================
// Maps UI dropdown indices (0-9) to (NoteValue, NoteModifier) pairs.
// Used by Digital, PingPong, and other tempo-synced delay modes.
//
// Dropdown order (shortest to longest):
//   0: 1/32     1: 1/16T    2: 1/16    3: 1/8T    4: 1/8
//   5: 1/4T    6: 1/4      7: 1/2T    8: 1/2     9: 1/1
// =============================================================================

/// @brief Result of mapping a dropdown index to note value + modifier
struct NoteValueMapping {
    NoteValue note;
    NoteModifier modifier;
};

/// @brief Lookup table for dropdown index to note value mapping
/// Index 0-9 maps to the standard tempo sync dropdown order
inline constexpr NoteValueMapping kNoteValueDropdownMapping[] = {
    {NoteValue::ThirtySecond, NoteModifier::None},    // 0: 1/32
    {NoteValue::Sixteenth, NoteModifier::Triplet},    // 1: 1/16T
    {NoteValue::Sixteenth, NoteModifier::None},       // 2: 1/16
    {NoteValue::Eighth, NoteModifier::Triplet},       // 3: 1/8T
    {NoteValue::Eighth, NoteModifier::None},          // 4: 1/8
    {NoteValue::Quarter, NoteModifier::Triplet},      // 5: 1/4T
    {NoteValue::Quarter, NoteModifier::None},         // 6: 1/4
    {NoteValue::Half, NoteModifier::Triplet},         // 7: 1/2T
    {NoteValue::Half, NoteModifier::None},            // 8: 1/2
    {NoteValue::Whole, NoteModifier::None},           // 9: 1/1
};

/// @brief Convert dropdown index to (NoteValue, NoteModifier) pair
/// @param dropdownIndex Index from UI dropdown (0-9)
/// @return NoteValueMapping with note value and modifier
[[nodiscard]] inline constexpr NoteValueMapping getNoteValueFromDropdown(
    int dropdownIndex
) noexcept {
    if (dropdownIndex < 0 || dropdownIndex > 9) {
        // Default to 1/8 note if out of range
        return {NoteValue::Eighth, NoteModifier::None};
    }
    return kNoteValueDropdownMapping[dropdownIndex];
}

// =============================================================================
// Tempo Sync Utilities
// =============================================================================
// Reusable functions for converting note values to delay times.
// Used by Digital, PingPong, and other tempo-synced delay modes.
// =============================================================================

/// @brief Minimum tempo in BPM for tempo sync calculations
inline constexpr double kMinTempoSyncBPM = 20.0;

/// @brief Maximum tempo in BPM for tempo sync calculations
inline constexpr double kMaxTempoSyncBPM = 300.0;

/// @brief Convert note value + modifier to delay time in milliseconds at given tempo
///
/// This is a fundamental Layer 0 utility for tempo-synced delay effects.
/// The formula is: delayMs = (60000 / BPM) * beatsPerNote
///
/// @param note Note value (quarter, eighth, etc.)
/// @param modifier Timing modifier (dotted, triplet, or none)
/// @param tempoBPM Tempo in beats per minute (clamped to 20-300 BPM)
/// @return Delay time in milliseconds
///
/// @example
/// @code
/// // At 120 BPM:
/// // - Quarter note (1 beat) = 500ms
/// // - Eighth note (0.5 beats) = 250ms
/// // - Dotted eighth (0.75 beats) = 375ms
/// // - Eighth triplet (0.333 beats) = 166.67ms
///
/// float delayMs = noteToDelayMs(NoteValue::Quarter, NoteModifier::None, 120.0);
/// // delayMs == 500.0f
/// @endcode
[[nodiscard]] inline constexpr float noteToDelayMs(
    NoteValue note,
    NoteModifier modifier,
    double tempoBPM
) noexcept {
    // Clamp tempo to safe range (avoid division issues and unreasonable values)
    const double clampedTempo = (tempoBPM < kMinTempoSyncBPM) ? kMinTempoSyncBPM
                              : (tempoBPM > kMaxTempoSyncBPM ? kMaxTempoSyncBPM : tempoBPM);

    // Get beat duration for note value with modifier
    const float beatsPerNote = getBeatsForNote(note, modifier);

    // Calculate: milliseconds = (60000 / BPM) * beatsPerNote
    // Using 60000 directly (60 seconds * 1000ms) for clarity
    const double msPerBeat = 60000.0 / clampedTempo;
    return static_cast<float>(msPerBeat * static_cast<double>(beatsPerNote));
}

/// @brief Convert dropdown index directly to delay time in milliseconds
///
/// Convenience function that combines getNoteValueFromDropdown() and noteToDelayMs().
/// Use this when you have a dropdown index and need the final delay time directly.
///
/// @param dropdownIndex Index from UI dropdown (0-9)
/// @param tempoBPM Tempo in beats per minute
/// @return Delay time in milliseconds
///
/// @example
/// @code
/// // User selects "1/8T" (index 3) at 100 BPM
/// float delayMs = dropdownToDelayMs(3, 100.0);
/// // delayMs == 200.0f (eighth triplet = 0.333 beats = 200ms at 100 BPM)
/// @endcode
[[nodiscard]] inline constexpr float dropdownToDelayMs(
    int dropdownIndex,
    double tempoBPM
) noexcept {
    const auto mapping = getNoteValueFromDropdown(dropdownIndex);
    return noteToDelayMs(mapping.note, mapping.modifier, tempoBPM);
}

} // namespace DSP
} // namespace Iterum
