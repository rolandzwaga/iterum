// ==============================================================================
// Layer 1: DSP Primitive - BitCrusher
// ==============================================================================
// Bit depth reduction with optional TPDF dither for lo-fi effects.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 1 (no external dependencies except Layer 0)
//
// Reference: specs/021-character-processor/spec.md (FR-014, FR-016)
// Reference: specs/021-character-processor/research.md Section 4
// ==============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

/// @brief Layer 1 DSP Primitive - Bit depth reduction
///
/// Quantizes audio to a reduced bit depth with optional TPDF dither.
/// Creates quantization noise characteristic of early digital audio.
///
/// @par Algorithm
/// - Quantization: `output = round(input * levels) / levels`
/// - TPDF dither: Triangular PDF noise added before quantization
/// - Levels = 2^bitDepth - 1
///
/// @par Usage
/// @code
/// BitCrusher crusher;
/// crusher.prepare(44100.0);
/// crusher.setBitDepth(8.0f);    // 8-bit quantization
/// crusher.setDither(0.5f);      // 50% dither
///
/// float output = crusher.process(input);
/// @endcode
class BitCrusher {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinBitDepth = 4.0f;
    static constexpr float kMaxBitDepth = 16.0f;
    static constexpr float kDefaultBitDepth = 16.0f;
    static constexpr float kMinDither = 0.0f;
    static constexpr float kMaxDither = 1.0f;
    static constexpr float kDefaultDither = 0.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    BitCrusher() noexcept = default;

    /// @brief Prepare for processing
    /// @param sampleRate Audio sample rate in Hz (unused, for API consistency)
    void prepare(double sampleRate) noexcept {
        (void)sampleRate; // Not needed for bit crushing
        updateQuantizationLevels();
    }

    /// @brief Reset internal state
    void reset() noexcept {
        // Reset RNG state to initial seed
        rngState_ = kDefaultRngSeed;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample [-1, 1]
    /// @return Quantized output sample
    [[nodiscard]] float process(float input) noexcept {
        // Normalize to [0, 1] range for proper quantization
        float normalized = (input + 1.0f) * 0.5f;

        // Apply TPDF dither before quantization if enabled
        if (dither_ > 0.0f) {
            // Generate two uniform random values in [-1, 1]
            float r1 = nextRandom();
            float r2 = nextRandom();

            // TPDF = sum of two uniform distributions
            // Scaled by dither amount and quantization step size (1/levels)
            float ditherNoise = (r1 + r2) * dither_ / levels_;
            normalized += ditherNoise;
        }

        // Quantize: scale to integer range, round, scale back
        float scaled = normalized * levels_;
        float quantized = std::round(scaled);

        // Clamp to valid range [0, levels]
        quantized = std::clamp(quantized, 0.0f, levels_);

        // Denormalize back to [-1, 1]
        return (quantized / levels_) * 2.0f - 1.0f;
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

    /// @brief Set bit depth
    /// @param bits Bit depth [4, 16]
    void setBitDepth(float bits) noexcept {
        bitDepth_ = std::clamp(bits, kMinBitDepth, kMaxBitDepth);
        updateQuantizationLevels();
    }

    /// @brief Set dither amount
    /// @param amount Dither [0, 1] (0 = none, 1 = full TPDF)
    void setDither(float amount) noexcept {
        dither_ = std::clamp(amount, kMinDither, kMaxDither);
    }

    /// @brief Get current bit depth
    [[nodiscard]] float getBitDepth() const noexcept {
        return bitDepth_;
    }

    /// @brief Get current dither amount
    [[nodiscard]] float getDither() const noexcept {
        return dither_;
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Update quantization levels from bit depth
    void updateQuantizationLevels() noexcept {
        // levels = 2^bitDepth - 1
        // For 8 bits: 255 levels
        // For 16 bits: 65535 levels
        // Fractional bit depths use floating point power
        levels_ = std::pow(2.0f, bitDepth_) - 1.0f;

        // Prevent division by zero for extreme cases
        if (levels_ < 1.0f) {
            levels_ = 1.0f;
        }
    }

    /// @brief Generate next random value in [-1, 1] using xorshift32
    [[nodiscard]] float nextRandom() noexcept {
        // Xorshift32 PRNG - fast and sufficient for dither
        rngState_ ^= rngState_ << 13;
        rngState_ ^= rngState_ >> 17;
        rngState_ ^= rngState_ << 5;

        // Convert to float in range [-1, 1]
        // Use all 32 bits for full precision
        constexpr float kScale = 2.0f / 4294967295.0f; // 2 / (2^32 - 1)
        return static_cast<float>(rngState_) * kScale - 1.0f;
    }

    // =========================================================================
    // Private Data
    // =========================================================================

    static constexpr uint32_t kDefaultRngSeed = 0x12345678u;

    float bitDepth_ = kDefaultBitDepth;
    float dither_ = kDefaultDither;
    float levels_ = 65535.0f; // 2^16 - 1

    // RNG state for TPDF dither
    uint32_t rngState_ = kDefaultRngSeed;
};

} // namespace DSP
} // namespace Iterum
