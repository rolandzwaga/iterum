// ==============================================================================
// Layer 0: Core Utility - BlockContext
// ==============================================================================
// Per-block processing context for DSP components.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (no allocation, noexcept)
// - Principle III: Modern C++ (constexpr, value semantics)
// - Principle IX: Layer 0 (no dependencies on higher layers)
//
// Reference: specs/017-layer0-utilities/spec.md
// ==============================================================================

#pragma once

#include "note_value.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// @brief Minimum tempo in BPM (prevents division issues).
inline constexpr double kMinTempoBPM = 20.0;

/// @brief Maximum tempo in BPM (reasonable musical limit).
inline constexpr double kMaxTempoBPM = 300.0;

// =============================================================================
// BlockContext Struct (FR-001 to FR-008)
// =============================================================================

/// @brief Per-block processing context for DSP components.
///
/// Carries host-provided information about the current processing block.
/// Used by tempo-synced components (delays, LFOs) and transport-aware features.
///
/// @note All member access is noexcept. No dynamic allocation.
/// @note Default values represent a typical standalone scenario (FR-007).
///
/// @example
/// @code
/// // Create context from host ProcessData
/// BlockContext ctx;
/// ctx.sampleRate = processData.processContext->sampleRate;
/// ctx.tempoBPM = processData.processContext->tempo;
/// ctx.isPlaying = processData.processContext->state & ProcessContext::kPlaying;
///
/// // Calculate tempo-synced delay time
/// size_t delaySamples = ctx.tempoToSamples(NoteValue::Quarter, NoteModifier::Dotted);
/// @endcode
struct BlockContext {
    // =========================================================================
    // Audio Context (FR-001, FR-002)
    // =========================================================================

    double sampleRate = 44100.0;      ///< Sample rate in Hz
    size_t blockSize = 512;           ///< Block size in samples

    // =========================================================================
    // Tempo Context (FR-003, FR-004)
    // =========================================================================

    double tempoBPM = 120.0;          ///< Tempo in beats per minute
    uint8_t timeSignatureNumerator = 4;    ///< Time signature numerator (e.g., 4 in 4/4)
    uint8_t timeSignatureDenominator = 4;  ///< Time signature denominator (e.g., 4 in 4/4)

    // =========================================================================
    // Transport Context (FR-005, FR-006)
    // =========================================================================

    bool isPlaying = false;           ///< Transport playing state
    int64_t transportPositionSamples = 0;  ///< Position in samples from song start

    // =========================================================================
    // Methods (FR-008)
    // =========================================================================

    /// @brief Convert note value to sample count at current tempo/sample rate.
    ///
    /// @param note The note value (quarter, eighth, etc.)
    /// @param modifier Optional timing modifier (dotted, triplet)
    /// @return Sample count for the note duration (0 if sample rate is 0)
    ///
    /// @note Tempo is clamped to [kMinTempoBPM, kMaxTempoBPM] for safety.
    ///
    /// @example
    /// @code
    /// BlockContext ctx;
    /// ctx.sampleRate = 44100.0;
    /// ctx.tempoBPM = 120.0;
    ///
    /// // Quarter note at 120 BPM = 0.5 seconds = 22050 samples
    /// size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
    /// assert(samples == 22050);
    ///
    /// // Dotted eighth = 0.75 * quarter = 16537.5 -> 16537 samples
    /// size_t dottedEighth = ctx.tempoToSamples(NoteValue::Eighth, NoteModifier::Dotted);
    /// @endcode
    [[nodiscard]] constexpr size_t tempoToSamples(
        NoteValue note,
        NoteModifier modifier = NoteModifier::None
    ) const noexcept {
        // Safety checks
        if (sampleRate <= 0.0) {
            return 0;
        }

        // Clamp tempo to valid range (FR edge case: tempo 0 or negative)
        const double clampedTempo = std::clamp(tempoBPM, kMinTempoBPM, kMaxTempoBPM);

        // Get beat duration for note value with modifier
        const double beatsPerNote = static_cast<double>(getBeatsForNote(note, modifier));

        // Calculate sample count:
        // seconds = (60 / BPM) * beatsPerNote
        // samples = seconds * sampleRate
        const double secondsPerBeat = 60.0 / clampedTempo;
        const double noteSeconds = secondsPerBeat * beatsPerNote;
        const double samples = noteSeconds * sampleRate;

        return static_cast<size_t>(samples);
    }

    /// @brief Get the duration of one beat in samples at current tempo.
    /// @return Samples per beat
    [[nodiscard]] constexpr size_t samplesPerBeat() const noexcept {
        return tempoToSamples(NoteValue::Quarter);
    }

    /// @brief Get the duration of one bar/measure in samples.
    /// @return Samples per bar (based on time signature)
    [[nodiscard]] constexpr size_t samplesPerBar() const noexcept {
        // A bar contains `numerator` beats of duration `4/denominator` quarter notes each
        // At 4/4: 4 beats * quarter note duration
        // At 3/4: 3 beats * quarter note duration
        // At 6/8: 6 beats * eighth note duration = 3 quarter note durations
        const size_t beatSamples = samplesPerBeat();
        const double quarterNotesPerBeat = 4.0 / static_cast<double>(timeSignatureDenominator);
        const double totalQuarterNotes = static_cast<double>(timeSignatureNumerator) * quarterNotesPerBeat;
        return static_cast<size_t>(static_cast<double>(beatSamples) * totalQuarterNotes);
    }
};

} // namespace DSP
} // namespace Iterum
