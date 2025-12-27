#pragma once

// ==============================================================================
// DSP Utilities
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// - DSP algorithms MUST be pure functions testable without VST infrastructure
// - All functions in this module should be independently unit testable
//
// Constitution Principle IV: SIMD & DSP Optimization
// - Align data to SIMD register width
// - Process in contiguous, sequential memory access patterns
// - Minimize branching in inner loops
// ==============================================================================

#include <cstddef>
#include <cmath>
#include <algorithm>
#include <array>

// Layer 0 Core Utilities
#include "core/db_utils.h"
#include "core/math_constants.h"

namespace Iterum {
namespace DSP {

// ==============================================================================
// Gain Utilities - Use Iterum::DSP (core/db_utils.h)
// ==============================================================================
// dB/linear conversion functions are in src/dsp/core/db_utils.h:
//   - Iterum::DSP::dbToGain(float dB) -> linear gain
//   - Iterum::DSP::gainToDb(float gain) -> dB value
//   - Iterum::DSP::kSilenceFloorDb -> -144 dB floor (24-bit dynamic range)

// ==============================================================================
// Buffer Operations
// ==============================================================================

/// Apply gain to a buffer of samples
/// @param buffer Pointer to sample buffer (modified in place)
/// @param numSamples Number of samples to process
/// @param gain Linear gain multiplier
///
/// Constitution Principle IV: Contiguous memory access, no branching
inline void applyGain(float* buffer, size_t numSamples, float gain) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] *= gain;
    }
}

/// Copy buffer with gain applied
/// @param input Source buffer
/// @param output Destination buffer
/// @param numSamples Number of samples to process
/// @param gain Linear gain multiplier
inline void copyWithGain(const float* input, float* output,
                         size_t numSamples, float gain) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = input[i] * gain;
    }
}

/// Mix two buffers: output = a * gainA + b * gainB
/// @param a First source buffer
/// @param gainA Gain for first buffer
/// @param b Second source buffer
/// @param gainB Gain for second buffer
/// @param output Destination buffer
/// @param numSamples Number of samples
inline void mix(const float* a, float gainA,
                const float* b, float gainB,
                float* output, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = a[i] * gainA + b[i] * gainB;
    }
}

/// Clear a buffer to zero
/// @param buffer Buffer to clear
/// @param numSamples Number of samples
inline void clear(float* buffer, size_t numSamples) noexcept {
    std::fill_n(buffer, numSamples, 0.0f);
}

// ==============================================================================
// Smoothing
// ==============================================================================
// NOTE: OnePoleSmoother, LinearRamp, and SlewLimiter have been moved to
// dsp/primitives/smoother.h per spec 005-parameter-smoother.
// Use #include "dsp/primitives/smoother.h" for the full implementation.

// ==============================================================================
// Clipping / Limiting
// ==============================================================================

/// Hard clip a sample to [-1, 1] range
[[nodiscard]] inline constexpr float hardClip(float sample) noexcept {
    return std::clamp(sample, -1.0f, 1.0f);
}

/// Soft clip using tanh-like curve
[[nodiscard]] inline float softClip(float sample) noexcept {
    // Fast approximation of tanh
    if (sample > 3.0f) return 1.0f;
    if (sample < -3.0f) return -1.0f;
    const float x2 = sample * sample;
    return sample * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ==============================================================================
// Analysis
// ==============================================================================

/// Calculate RMS of a buffer
/// @param buffer Sample buffer
/// @param numSamples Number of samples
/// @return RMS value
[[nodiscard]] inline float calculateRMS(const float* buffer,
                                        size_t numSamples) noexcept {
    if (numSamples == 0) return 0.0f;

    float sum = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(numSamples));
}

/// Find peak absolute value in buffer
/// @param buffer Sample buffer
/// @param numSamples Number of samples
/// @return Peak absolute value
[[nodiscard]] inline float findPeak(const float* buffer,
                                    size_t numSamples) noexcept {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

} // namespace DSP
} // namespace Iterum
