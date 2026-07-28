// Layer 0: Core Utility - Grain Envelope Tables
// Part of Granular Delay feature (spec 034)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <krate/dsp/core/math_constants.h>

namespace Krate::DSP {

/// Grain envelope types for granular synthesis
enum class GrainEnvelopeType : uint8_t {
    Hann,        ///< Raised cosine (smooth, general purpose)
    Trapezoid,   ///< Attack-sustain-decay (preserves transients)
    Sine,        ///< Half-cosine (better for pitch shifting)
    Blackman,    ///< Low sidelobe (less coloration)
    Linear,      ///< Linear attack-sustain-decay (Pattern Freeze spec 069)
    Exponential  ///< RC-style curves with punchier attack (Pattern Freeze spec 069)
};

namespace GrainEnvelope {

// Note: kPi and kTwoPi are imported from Krate::DSP (math_constants.h)

/// Pre-compute envelope lookup table (call in prepare, not process)
/// @param output Pre-allocated buffer for envelope values
/// @param size Number of samples in the envelope
/// @param type Envelope shape to generate
/// @param attackRatio Attack time as ratio of total length (for Trapezoid)
/// @param releaseRatio Release time as ratio of total length (for Trapezoid)
inline void generate(float* output, size_t size, GrainEnvelopeType type,
                     float attackRatio = 0.1f, float releaseRatio = 0.1f) noexcept {
    if (output == nullptr || size == 0) {
        return;
    }

    // Use (size - 1) as denominator to ensure phase goes from 0 to 1 exactly
    // This ensures symmetry and that first/last samples are at endpoints
    const auto sizeM1 = static_cast<float>(size - 1);
    const auto sizeFloat = static_cast<float>(size);

    switch (type) {
        case GrainEnvelopeType::Hann:
            // Raised cosine: 0.5 * (1 - cos(2*pi*n/(N-1)))
            for (size_t i = 0; i < size; ++i) {
                const float phase = static_cast<float>(i) / sizeM1;
                output[i] = 0.5f * (1.0f - std::cos(kTwoPi * phase));
            }
            break;

        case GrainEnvelopeType::Trapezoid: {
            // Attack-sustain-decay envelope
            const size_t attackSamples =
                static_cast<size_t>(sizeFloat * std::clamp(attackRatio, 0.0f, 0.5f));
            const size_t releaseSamples =
                static_cast<size_t>(sizeFloat * std::clamp(releaseRatio, 0.0f, 0.5f));
            const size_t sustainEnd = size - releaseSamples;

            for (size_t i = 0; i < size; ++i) {
                if (i < attackSamples && attackSamples > 0) {
                    // Attack ramp
                    output[i] = static_cast<float>(i) / static_cast<float>(attackSamples);
                } else if (i >= sustainEnd && releaseSamples > 0) {
                    // Release ramp
                    output[i] = static_cast<float>(size - 1 - i) /
                                static_cast<float>(releaseSamples);
                } else {
                    // Sustain
                    output[i] = 1.0f;
                }
            }
            break;
        }

        case GrainEnvelopeType::Sine:
            // Half-sine (better for pitch shifting)
            for (size_t i = 0; i < size; ++i) {
                const float phase = static_cast<float>(i) / sizeM1;
                output[i] = std::sin(kPi * phase);
            }
            break;

        case GrainEnvelopeType::Blackman:
            // Blackman window: low sidelobe, less coloration
            // 0.42 - 0.5*cos(2*pi*n/(N-1)) + 0.08*cos(4*pi*n/(N-1))
            for (size_t i = 0; i < size; ++i) {
                const float phase = static_cast<float>(i) / sizeM1;
                float value = 0.42f - 0.5f * std::cos(kTwoPi * phase) +
                              0.08f * std::cos(2.0f * kTwoPi * phase);
                // Clamp small negative values to zero (floating point precision)
                output[i] = std::max(0.0f, value);
            }
            break;

        case GrainEnvelopeType::Linear: {
            // Linear attack-sustain-decay envelope (same as Trapezoid but explicit)
            // Pattern Freeze spec 069 - simple linear ramps
            const size_t attackSamples =
                static_cast<size_t>(sizeFloat * std::clamp(attackRatio, 0.0f, 0.5f));
            const size_t releaseSamples =
                static_cast<size_t>(sizeFloat * std::clamp(releaseRatio, 0.0f, 0.5f));
            const size_t sustainEnd = size - releaseSamples;

            for (size_t i = 0; i < size; ++i) {
                if (i < attackSamples && attackSamples > 0) {
                    // Linear attack ramp
                    output[i] = static_cast<float>(i) / static_cast<float>(attackSamples);
                } else if (i >= sustainEnd && releaseSamples > 0) {
                    // Linear release ramp
                    output[i] = static_cast<float>(size - 1 - i) /
                                static_cast<float>(releaseSamples);
                } else {
                    // Sustain
                    output[i] = 1.0f;
                }
            }
            break;
        }

        case GrainEnvelopeType::Exponential: {
            // RC-style exponential curves with punchier attack
            // Pattern Freeze spec 069 - faster initial rise, smoother sustain approach
            const size_t attackSamples =
                static_cast<size_t>(sizeFloat * std::clamp(attackRatio, 0.0f, 0.5f));
            const size_t releaseSamples =
                static_cast<size_t>(sizeFloat * std::clamp(releaseRatio, 0.0f, 0.5f));
            const size_t sustainEnd = size - releaseSamples;

            // Time constant for exponential curves (controls steepness)
            // Larger value = faster initial slope, smoother approach to target
            constexpr float kTimeConstant = 4.0f;

            for (size_t i = 0; i < size; ++i) {
                if (i < attackSamples && attackSamples > 0) {
                    // Exponential attack: 1 - exp(-t*k)
                    // Normalized to reach ~1.0 at end of attack
                    const float t = static_cast<float>(i) / static_cast<float>(attackSamples);
                    output[i] = 1.0f - std::exp(-t * kTimeConstant);
                    // Normalize to ensure we reach 1.0 at end of attack
                    const float endValue = 1.0f - std::exp(-kTimeConstant);
                    output[i] /= endValue;
                } else if (i >= sustainEnd && releaseSamples > 0) {
                    // Exponential release: exp(-t*k)
                    // Starts from 1.0, decays toward 0
                    const float t = static_cast<float>(i - sustainEnd) /
                                    static_cast<float>(releaseSamples);
                    output[i] = std::exp(-t * kTimeConstant);
                } else {
                    // Sustain
                    output[i] = 1.0f;
                }
            }
            break;
        }
    }
}

/// Get envelope value at normalized phase [0, 1] with linear interpolation
/// @param table Pre-computed envelope lookup table
/// @param tableSize Size of the lookup table
/// @param phase Normalized position in envelope [0.0, 1.0]
/// @return Interpolated envelope amplitude
[[nodiscard]] inline float lookup(const float* table, size_t tableSize,
                                  float phase) noexcept {
    if (table == nullptr || tableSize == 0) {
        return 0.0f;
    }

    // Clamp phase to valid range.
    //
    // TWO ORDERED COMPARISONS, NOT std::clamp, and SIGNED integer intermediates
    // below - both are codegen decisions on a function that sits on granular
    // inner loops (one call per grain per output sample).
    //   - std::clamp returns a const reference, so MSVC materialises the two
    //     bounds on the stack and selects between three ADDRESSES with cmov,
    //     then loads through the winner: a store/load round trip per call.
    //   - `size_t` conversions either way have no x86-64 instruction below
    //     AVX-512, so each expands to a compare, a branch and a fix-up. The
    //     index is bounded by tableSize, so ptrdiff_t is value-identical.
    // The comparison ORDER also makes a NaN phase land on 0 rather than on
    // `static_cast<size_t>(NaN)`, which was an out-of-range table index.
    if (!(phase >= 0.0f)) { phase = 0.0f; }  // negatives, -Inf, NaN
    if (phase > 1.0f)     { phase = 1.0f; }  // +Inf, over-range finite

    // Calculate fractional index
    const auto last = static_cast<ptrdiff_t>(tableSize - 1);
    const float indexFloat = phase * static_cast<float>(last);
    const auto index0 = static_cast<ptrdiff_t>(indexFloat);
    const ptrdiff_t index1 = (index0 < last) ? (index0 + 1) : last;
    const float frac = indexFloat - static_cast<float>(index0);

    // Linear interpolation
    return table[index0] + frac * (table[index1] - table[index0]);
}

}  // namespace GrainEnvelope

}  // namespace Krate::DSP
