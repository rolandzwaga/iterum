// ==============================================================================
// Layer 1: DSP Primitive - SampleRateReducer
// ==============================================================================
// Sample rate reduction using sample-and-hold for lo-fi effects.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 1 (no external dependencies except Layer 0)
//
// Reference: specs/021-character-processor/spec.md (FR-015)
// Reference: specs/021-character-processor/research.md Section 5
// ==============================================================================

#pragma once

#include <algorithm>
#include <cstddef>

namespace Iterum {
namespace DSP {

/// @brief Layer 1 DSP Primitive - Sample rate reduction
///
/// Reduces effective sample rate using sample-and-hold technique.
/// Creates aliasing artifacts characteristic of early digital audio.
///
/// @par Algorithm
/// - Hold counter accumulates each sample
/// - When counter exceeds reduction factor, capture new input
/// - Output held value until next capture
///
/// @par Fractional Support
/// Uses floating-point counter for fractional reduction factors.
/// E.g., factor 2.5 means a new sample is captured every 2.5 input samples
/// on average.
///
/// @par Usage
/// @code
/// SampleRateReducer reducer;
/// reducer.prepare(44100.0);
/// reducer.setReductionFactor(4.0f);  // Reduce to 1/4 sample rate
///
/// float output = reducer.process(input);
/// @endcode
class SampleRateReducer {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinReductionFactor = 1.0f;
    static constexpr float kMaxReductionFactor = 8.0f;
    static constexpr float kDefaultReductionFactor = 1.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    SampleRateReducer() noexcept = default;

    /// @brief Prepare for processing
    /// @param sampleRate Audio sample rate in Hz (unused, for API consistency)
    void prepare(double sampleRate) noexcept {
        (void)sampleRate; // Not needed for sample-and-hold
        reset();
    }

    /// @brief Reset internal state
    void reset() noexcept {
        holdValue_ = 0.0f;
        holdCounter_ = reductionFactor_; // Ensure first sample is captured
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample
    /// @return Reduced sample rate output (held value)
    [[nodiscard]] float process(float input) noexcept {
        // Increment hold counter
        holdCounter_ += 1.0f;

        // When counter exceeds factor, capture new sample
        if (holdCounter_ >= reductionFactor_) {
            holdValue_ = input;
            // Subtract factor (not reset to 0) for fractional accuracy
            holdCounter_ -= reductionFactor_;
        }

        return holdValue_;
    }

    /// @brief Process a buffer in-place
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void process(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set reduction factor
    /// @param factor Reduction [1, 8] (1 = no reduction, 8 = heavy aliasing)
    void setReductionFactor(float factor) noexcept {
        reductionFactor_ = std::clamp(factor, kMinReductionFactor, kMaxReductionFactor);
    }

    /// @brief Get current reduction factor
    [[nodiscard]] float getReductionFactor() const noexcept {
        return reductionFactor_;
    }

private:
    // =========================================================================
    // Private Data
    // =========================================================================

    float reductionFactor_ = kDefaultReductionFactor;
    float holdValue_ = 0.0f;
    float holdCounter_ = 1.0f; // Start at factor to capture first sample
};

} // namespace DSP
} // namespace Iterum
