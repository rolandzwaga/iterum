// ==============================================================================
// API Contract: Oversampler
// ==============================================================================
// This file defines the public API contract for the Oversampler primitive.
// Implementation must conform to this interface.
//
// Feature: 006-oversampler
// Layer: 1 (DSP Primitive)
// Dependencies: Layer 0 utilities, BiquadCascade (Layer 1)
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace Iterum {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// Oversampling factor (compile-time or runtime selectable)
enum class OversamplingFactor : uint8_t {
    x2 = 2,  ///< 2x oversampling (44.1k → 88.2k)
    x4 = 4   ///< 4x oversampling (44.1k → 176.4k)
};

/// Filter quality preset affecting stopband rejection and latency
enum class OversamplingQuality : uint8_t {
    Economy,   ///< IIR 8-pole, ~48dB stopband, 0 latency
    Standard,  ///< FIR 31-tap, ~80dB stopband, minimal latency
    High       ///< FIR 63-tap, ~100dB stopband, more latency
};

/// Latency/phase mode
enum class OversamplingMode : uint8_t {
    ZeroLatency,  ///< IIR filters (minimum-phase, no latency)
    LinearPhase   ///< FIR filters (symmetric, adds latency)
};

// =============================================================================
// Oversampler Class Template
// =============================================================================

/// @brief Upsampling/downsampling primitive for anti-aliased nonlinear processing.
///
/// Provides 2x or 4x oversampling with configurable anti-aliasing filter quality.
/// Use before saturation, waveshaping, or any nonlinear operation to prevent aliasing.
///
/// @tparam Factor Oversampling factor (2 or 4)
/// @tparam NumChannels Number of audio channels (1 = mono, 2 = stereo)
///
/// Constitution Compliance:
/// - Principle II: Real-Time Safety (noexcept process, no allocations)
/// - Principle IX: Layer 1 (depends only on Layer 0 and other Layer 1 primitives)
/// - Principle X: DSP Constraints (denormal flushing, stable filters)
///
/// @code
/// // Basic usage with lambda for nonlinear processing
/// Oversampler<OversamplingFactor::x2, 2> oversampler;
/// oversampler.prepare(44100.0, 512, OversamplingQuality::Standard);
///
/// // In process callback:
/// oversampler.process(leftBuffer, rightBuffer, numSamples,
///     [](float* left, float* right, size_t n) {
///         for (size_t i = 0; i < n; ++i) {
///             left[i] = std::tanh(left[i] * 2.0f);
///             right[i] = std::tanh(right[i] * 2.0f);
///         }
///     });
/// @endcode
template<OversamplingFactor Factor = OversamplingFactor::x2, size_t NumChannels = 2>
class Oversampler {
public:
    // =========================================================================
    // Type Aliases
    // =========================================================================

    /// Callback type for stereo processing at oversampled rate
    using StereoCallback = std::function<void(float*, float*, size_t)>;

    /// Callback type for mono processing at oversampled rate
    using MonoCallback = std::function<void(float*, size_t)>;

    // =========================================================================
    // Constants
    // =========================================================================

    /// Oversampling factor as integer
    static constexpr size_t factor() noexcept {
        return static_cast<size_t>(Factor);
    }

    /// Number of cascaded 2x stages (1 for 2x, 2 for 4x)
    static constexpr size_t numStages() noexcept {
        return (Factor == OversamplingFactor::x2) ? 1 : 2;
    }

    /// Number of channels
    static constexpr size_t numChannels() noexcept {
        return NumChannels;
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Default constructor (must call prepare() before use)
    Oversampler() noexcept = default;

    /// Destructor
    ~Oversampler() = default;

    // Non-copyable (contains filter state)
    Oversampler(const Oversampler&) = delete;
    Oversampler& operator=(const Oversampler&) = delete;

    // Movable
    Oversampler(Oversampler&&) noexcept = default;
    Oversampler& operator=(Oversampler&&) noexcept = default;

    // =========================================================================
    // Configuration (call before processing)
    // =========================================================================

    /// Prepare oversampler for processing.
    /// Allocates buffers and calculates filter coefficients.
    /// @param sampleRate Base sample rate in Hz (e.g., 44100)
    /// @param maxBlockSize Maximum samples per channel per process call
    /// @param quality Filter quality preset
    /// @param mode Latency mode (ignored for Economy quality)
    /// @note NOT real-time safe (allocates memory)
    void prepare(
        double sampleRate,
        size_t maxBlockSize,
        OversamplingQuality quality = OversamplingQuality::Standard,
        OversamplingMode mode = OversamplingMode::LinearPhase
    ) noexcept;

    /// Get latency introduced by oversampling (in base-rate samples)
    /// @return Latency samples (0 for ZeroLatency mode)
    [[nodiscard]] size_t getLatency() const noexcept;

    /// Get current quality setting
    [[nodiscard]] OversamplingQuality getQuality() const noexcept;

    /// Get current mode setting
    [[nodiscard]] OversamplingMode getMode() const noexcept;

    /// Check if oversampler has been prepared
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Processing (real-time safe)
    // =========================================================================

    /// Process stereo audio with oversampling.
    /// @param leftIn Left channel input buffer (numSamples)
    /// @param rightIn Right channel input buffer (numSamples)
    /// @param leftOut Left channel output buffer (numSamples)
    /// @param rightOut Right channel output buffer (numSamples)
    /// @param numSamples Number of samples per channel (must be <= maxBlockSize)
    /// @param callback User function applied at oversampled rate
    /// @note Real-time safe (no allocations, noexcept)
    /// @pre prepare() must have been called
    /// @pre numSamples <= maxBlockSize from prepare()
    void process(
        const float* leftIn,
        const float* rightIn,
        float* leftOut,
        float* rightOut,
        size_t numSamples,
        const StereoCallback& callback
    ) noexcept;

    /// Process stereo audio in-place with oversampling.
    /// @param left Left channel buffer (input/output)
    /// @param right Right channel buffer (input/output)
    /// @param numSamples Number of samples per channel
    /// @param callback User function applied at oversampled rate
    void process(
        float* left,
        float* right,
        size_t numSamples,
        const StereoCallback& callback
    ) noexcept;

    /// Process mono audio with oversampling.
    /// @param input Input buffer (numSamples)
    /// @param output Output buffer (numSamples)
    /// @param numSamples Number of samples
    /// @param callback User function applied at oversampled rate
    void process(
        const float* input,
        float* output,
        size_t numSamples,
        const MonoCallback& callback
    ) noexcept;

    /// Process mono audio in-place with oversampling.
    /// @param buffer Input/output buffer
    /// @param numSamples Number of samples
    /// @param callback User function applied at oversampled rate
    void process(
        float* buffer,
        size_t numSamples,
        const MonoCallback& callback
    ) noexcept;

    // =========================================================================
    // Low-Level Access (for advanced use)
    // =========================================================================

    /// Upsample only (for manual processing pipeline)
    /// @param input Input buffer [numSamples]
    /// @param output Output buffer [numSamples * Factor]
    /// @param numSamples Number of input samples
    /// @param channel Channel index (0 = left, 1 = right)
    void upsample(
        const float* input,
        float* output,
        size_t numSamples,
        size_t channel = 0
    ) noexcept;

    /// Downsample only (for manual processing pipeline)
    /// @param input Input buffer [numSamples * Factor]
    /// @param output Output buffer [numSamples]
    /// @param numSamples Number of OUTPUT samples
    /// @param channel Channel index (0 = left, 1 = right)
    void downsample(
        const float* input,
        float* output,
        size_t numSamples,
        size_t channel = 0
    ) noexcept;

    /// Get pointer to internal upsampled buffer (for zero-copy processing)
    /// @param channel Channel index
    /// @return Pointer to internal buffer (valid until next process call)
    [[nodiscard]] float* getOversampledBuffer(size_t channel = 0) noexcept;

    /// Get size of oversampled buffer
    /// @return Maximum oversampled samples per channel
    [[nodiscard]] size_t getOversampledBufferSize() const noexcept;

    // =========================================================================
    // State Management
    // =========================================================================

    /// Clear all filter states (call when transport stops or on reset)
    void reset() noexcept;

private:
    // Implementation details hidden - see actual implementation file
};

// =============================================================================
// Common Type Aliases
// =============================================================================

/// 2x stereo oversampler (most common configuration)
using Oversampler2x = Oversampler<OversamplingFactor::x2, 2>;

/// 4x stereo oversampler (for heavy distortion)
using Oversampler4x = Oversampler<OversamplingFactor::x4, 2>;

/// 2x mono oversampler
using Oversampler2xMono = Oversampler<OversamplingFactor::x2, 1>;

/// 4x mono oversampler
using Oversampler4xMono = Oversampler<OversamplingFactor::x4, 1>;

} // namespace DSP
} // namespace Iterum
