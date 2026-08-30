// Layer 0: Core Utility - Pitch Conversion
// Part of Granular Delay feature (spec 034)
#pragma once

#include <cmath>
#include <cstdint>

namespace Krate::DSP {

/// Pitch quantization modes (Phase 2.2)
enum class PitchQuantMode : uint8_t {
    Off = 0,       ///< No quantization, use raw pitch value
    Semitones = 1, ///< Quantize to nearest semitone
    Octaves = 2,   ///< Quantize to nearest octave (0, ±12, ±24)
    Fifths = 3,    ///< Quantize to perfect fifth intervals (0, 7, 12, 19, etc.)
    Scale = 4      ///< Quantize to major scale degrees
};

/// Convert semitones to playback rate ratio
/// +12 semitones = 2.0 (octave up), -12 = 0.5 (octave down), 0 = 1.0
/// @param semitones Pitch offset in semitones (-24 to +24 typical range)
/// @return Playback rate ratio (e.g., 2.0 for octave up)
[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept {
    // ratio = 2^(semitones/12)
    return std::pow(2.0f, semitones / 12.0f);
}

/// @brief Convert a cent offset to a pitch ratio, accurate over the whole float range.
/// Deliberately NOT named centsToRatio: that identifier is a local variable at
/// processors/multi_pitch_detector.h:96. Deliberately NOT HarmonicCloud's
/// detail::centsToDriftRatio (systems/harmonic_cloud.h:105): that is Layer 3 and is
/// documented accurate only on [-50, +50] cents (:104).
[[nodiscard]] inline float centsToPitchRatio(float cents) noexcept {
    constexpr float kInvCentsPerOctave = 1.0f / 1200.0f;
    return std::exp2(cents * kInvCentsPerOctave);
}

/// @brief Cent offset to pitch ratio over a SMALL, BOUNDED cent domain.
///
/// The degree-4 Taylor series of `e^u` with `u = cents * ln2 / 1200`, in Horner
/// form: four fused multiply-adds, no transcendental. Promoted here from
/// `HarmonicCloud::detail::centsToDriftRatio` (systems/harmonic_cloud.h), which is
/// now a one-line forward to this function, so both live off ONE definition.
///
/// @par Accuracy, and the domain it holds on
/// On `[-50, +50]` cents `|u| <= 0.0289`, where the truncated term `u^5/120` is
/// `1.7e-10` relative - under float epsilon - so what is left is Horner rounding.
/// The bound is MEASURED, not asserted: `HarmonicCloud_CentsToRatioMatchesExp2`
/// sweeps the whole domain at 20,001 points against a double-precision
/// `std::exp2(cents/1200)` and records a worst case of **6.15e-08 relative at
/// 29.25 cents** - 1.06e-4 cent of pitch error. The same case runs a realistic
/// defect (the `ln(2)` base conversion dropped) through the identical measurement
/// at 1.29e-02, so the tolerance is not vacuous.
///
/// `cents == 0` returns EXACTLY `1.0f` (`u = 0`, so Horner yields 1), which is
/// what lets a caller's zero-entropy / zero-detune configuration stay bitwise
/// unperturbed without a separate branch.
///
/// OUTSIDE `[-50, +50]` THE ERROR GROWS AS `u^5`: at 1200 cents it is 1.3e-3
/// relative. Callers whose cent domain is unbounded must use `centsToPitchRatio`.
///
/// @param cents Detune in cents; accurate on `[-50, +50]`, degrading outside it
[[nodiscard]] inline float centsToPitchRatioFast(float cents) noexcept {
    constexpr float kCentsToNatLog = 0.693147180559945309f / 1200.0f;  // ln(2)/1200
    const float u = cents * kCentsToNatLog;
    // e^u = 1 + u(1 + u/2(1 + u/3(1 + u/4)))  -- Horner, 4 fused multiply-adds.
    return 1.0f + u * (1.0f + u * (0.5f + u * (1.0f / 6.0f + u * (1.0f / 24.0f))));
}

/// Convert playback rate ratio to semitones
/// @param ratio Playback rate (e.g., 2.0 for octave up, 0.5 for octave down)
/// @return Pitch offset in semitones
[[nodiscard]] inline float ratioToSemitones(float ratio) noexcept {
    // semitones = 12 * log2(ratio)
    if (ratio <= 0.0f) {
        return 0.0f;  // Invalid ratio, return neutral
    }
    return 12.0f * std::log2(ratio);
}

/// Quantize pitch in semitones according to the given mode (Phase 2.2)
/// @param semitones Input pitch in semitones
/// @param mode Quantization mode
/// @return Quantized pitch in semitones
[[nodiscard]] inline float quantizePitch(float semitones, PitchQuantMode mode) noexcept {
    switch (mode) {
        case PitchQuantMode::Off:
            return semitones;

        case PitchQuantMode::Semitones:
            // Round to nearest semitone
            return std::round(semitones);

        case PitchQuantMode::Octaves:
            // Round to nearest octave (0, ±12, ±24)
            return std::round(semitones / 12.0f) * 12.0f;

        case PitchQuantMode::Fifths: {
            // Quantize to perfect fifth intervals (0, 7, 12, 19, 24, etc.)
            // A perfect fifth is 7 semitones
            // Valid intervals: ..., -12, -7, 0, 7, 12, 19, 24, ...
            // These are: n*12 and n*12+7 for integer n
            float octave = std::floor(semitones / 12.0f);
            float withinOctave = semitones - octave * 12.0f;

            // Within an octave, choose between 0 and 7
            float quantized;
            if (withinOctave < 3.5f) {
                quantized = 0.0f;  // Closer to root
            } else if (withinOctave < 9.5f) {
                quantized = 7.0f;  // Closer to fifth
            } else {
                quantized = 12.0f;  // Closer to next octave
            }

            return octave * 12.0f + quantized;
        }

        case PitchQuantMode::Scale: {
            // Quantize to major scale degrees within each octave
            // Major scale: 0, 2, 4, 5, 7, 9, 11 semitones
            constexpr float majorScale[] = {0.0f, 2.0f, 4.0f, 5.0f, 7.0f, 9.0f, 11.0f};
            constexpr int scaleSize = 7;

            float octave = std::floor(semitones / 12.0f);
            float withinOctave = semitones - octave * 12.0f;

            // Handle negative within-octave (shouldn't happen but just in case)
            if (withinOctave < 0.0f) {
                withinOctave += 12.0f;
                octave -= 1.0f;
            }

            // Find nearest scale degree
            float minDist = std::abs(withinOctave - majorScale[0]);
            float nearest = majorScale[0];

            for (int i = 1; i < scaleSize; ++i) {
                float dist = std::abs(withinOctave - majorScale[i]);
                if (dist < minDist) {
                    minDist = dist;
                    nearest = majorScale[i];
                }
            }

            // Also check distance to next octave's root (use <= to prefer root)
            float distToNextRoot = std::abs(withinOctave - 12.0f);
            if (distToNextRoot <= minDist) {
                nearest = 12.0f;
            }

            return octave * 12.0f + nearest;
        }

        default:
            return semitones;
    }
}

// =============================================================================
// Frequency-to-MIDI-Note Conversion (spec 037-basic-synth-voice)
// =============================================================================

/// Convert frequency in Hz to continuous MIDI note number.
/// Uses 12-TET: midiNote = 12 * log2(hz / 440) + 69
/// @param hz Frequency in Hz (must be > 0)
/// @return Continuous MIDI note (69.0 = A4, 60.0 = C4). Returns 0.0 if hz <= 0.
/// @note Real-time safe: noexcept, no allocations
[[nodiscard]] inline float frequencyToMidiNote(float hz) noexcept {
    if (hz <= 0.0f) return 0.0f;
    return 12.0f * std::log2(hz / 440.0f) + 69.0f;
}

// =============================================================================
// Frequency-to-Note Conversion (spec 093-note-selective-filter, FR-011, FR-036)
// =============================================================================

/// Convert frequency in Hz to note class (0-11)
///
/// Uses standard frequency-to-MIDI conversion:
/// MIDI note = 12 * log2(frequency / 440) + 69
/// Note class = MIDI note mod 12
///
/// Where note class mapping is: 0=C, 1=C#, 2=D, 3=D#, 4=E, 5=F,
/// 6=F#, 7=G, 8=G#, 9=A, 10=A#, 11=B
///
/// @param hz Frequency in Hz (must be > 0)
/// @return Note class (0-11), or -1 if invalid frequency
/// @note Real-time safe: noexcept, no allocations
[[nodiscard]] inline int frequencyToNoteClass(float hz) noexcept {
    if (hz <= 0.0f) return -1;

    // MIDI note = 12 * log2(hz/440) + 69
    float midiNote = 12.0f * std::log2(hz / 440.0f) + 69.0f;

    // Round to nearest MIDI note, then get note class (0-11)
    int noteClass = static_cast<int>(std::round(midiNote)) % 12;

    // Handle negative modulo for very low frequencies (< C-1)
    if (noteClass < 0) noteClass += 12;

    return noteClass;
}

/// Calculate deviation from nearest note center in cents
///
/// Returns the signed cents deviation from the nearest chromatic note.
/// Positive values mean the frequency is sharp (above note center),
/// negative values mean flat (below note center).
///
/// The returned value is in the range approximately [-50, +50] cents,
/// since values outside this range would round to a different note.
///
/// @param hz Frequency in Hz (must be > 0)
/// @return Cents deviation from nearest note center (-50 to +50), or 0 if invalid
/// @note Real-time safe: noexcept, no allocations
[[nodiscard]] inline float frequencyToCentsDeviation(float hz) noexcept {
    if (hz <= 0.0f) return 0.0f;

    // MIDI note (continuous) = 12 * log2(hz/440) + 69
    float midiNote = 12.0f * std::log2(hz / 440.0f) + 69.0f;

    // Nearest integer MIDI note
    float roundedNote = std::round(midiNote);

    // Deviation in semitones, then convert to cents (100 cents = 1 semitone)
    return (midiNote - roundedNote) * 100.0f;
}

}  // namespace Krate::DSP
