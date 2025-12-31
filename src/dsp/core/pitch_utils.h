// Layer 0: Core Utility - Pitch Conversion
// Part of Granular Delay feature (spec 034)
#pragma once

#include <cmath>
#include <cstdint>

namespace Iterum::DSP {

/// Constant for pitch conversion: 2^(1/12)
inline constexpr float kSemitoneRatio = 1.0594630943592953f;

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

}  // namespace Iterum::DSP
