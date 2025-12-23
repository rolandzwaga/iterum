// ==============================================================================
// Layer 1: DSP Primitive - Oversampler
// ==============================================================================
// Upsampling/downsampling primitive for anti-aliased nonlinear processing.
// Supports 2x and 4x oversampling with configurable filter quality and latency modes.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library / BiquadCascade)
// - Principle X: DSP Constraints (anti-aliasing for nonlinearities, denormal flushing)
// - Principle XII: Test-First Development
//
// Reference: specs/006-oversampler/spec.md
// ==============================================================================

#pragma once

#include "biquad.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// Forward Declarations
// =============================================================================

template<size_t Factor, size_t NumChannels> class Oversampler;

// =============================================================================
// Enumerations
// =============================================================================

/// Oversampling factor
enum class OversamplingFactor : uint8_t {
    TwoX = 2,   ///< 2x oversampling (44.1k -> 88.2k)
    FourX = 4   ///< 4x oversampling (44.1k -> 176.4k)
};

/// Filter quality preset affecting stopband rejection and latency
enum class OversamplingQuality : uint8_t {
    Economy,   ///< IIR 8-pole, ~48dB stopband, 0 latency
    Standard,  ///< FIR 31-tap, ~80dB stopband, 15 samples latency (2x)
    High       ///< FIR 63-tap, ~100dB stopband, 31 samples latency (2x)
};

/// Latency/phase mode
enum class OversamplingMode : uint8_t {
    ZeroLatency,  ///< IIR filters (minimum-phase, no latency)
    LinearPhase   ///< FIR filters (symmetric, adds latency)
};

// =============================================================================
// Halfband FIR Filter Coefficients
// =============================================================================
// Pre-computed halfband lowpass filter coefficients for 2x oversampling.
// Halfband filters have h[n]=0 for even n (except center), making them efficient.
// Only non-zero coefficients are stored.
// Designed with Kaiser window for specified stopband attenuation.

namespace detail {

// Standard quality: 31-tap halfband FIR (~80dB stopband)
// Latency: 15 samples at oversampled rate
// Non-zero coefficients at odd indices + center
constexpr size_t kStandardFirLength = 31;
constexpr size_t kStandardFirLatency = 15;  // (31-1)/2
constexpr std::array<float, 16> kStandardFirCoeffs = {{
    // Coefficients for indices 0,2,4,...,30 (center at 15)
    // Symmetric, so we only store half + center
    -0.00057786f,  // h[1] = h[29]
     0.00241797f,  // h[3] = h[27]
    -0.00655826f,  // h[5] = h[25]
     0.01444389f,  // h[7] = h[23]
    -0.02807887f,  // h[9] = h[21]
     0.05181259f,  // h[11] = h[19]
    -0.09887288f,  // h[13] = h[17]
     0.31565937f,  // h[15] = center
     0.50000000f,  // h[15] center (the main tap)
     0.31565937f,  // h[17] = h[13]
    -0.09887288f,  // h[19] = h[11]
     0.05181259f,  // h[21] = h[9]
    -0.02807887f,  // h[23] = h[7]
     0.01444389f,  // h[25] = h[5]
    -0.00655826f,  // h[27] = h[3]
     0.00241797f   // h[29] = h[1]
}};

// High quality: 63-tap halfband FIR (~100dB stopband)
// Latency: 31 samples at oversampled rate
constexpr size_t kHighFirLength = 63;
constexpr size_t kHighFirLatency = 31;  // (63-1)/2
constexpr std::array<float, 32> kHighFirCoeffs = {{
    // Symmetric halfband coefficients
     0.00002747f,
    -0.00009570f,
     0.00023925f,
    -0.00049938f,
     0.00092645f,
    -0.00157925f,
     0.00252505f,
    -0.00384040f,
     0.00561275f,
    -0.00794197f,
     0.01094422f,
    -0.01475671f,
     0.01956047f,
    -0.02561143f,
     0.03330618f,
    -0.04330415f,
     0.05680847f,
    -0.07605218f,
     0.10597531f,
    -0.16066185f,
     0.28205469f,
     0.50000000f,  // Center tap
     0.28205469f,
    -0.16066185f,
     0.10597531f,
    -0.07605218f,
     0.05680847f,
    -0.04330415f,
     0.03330618f,
    -0.02561143f,
     0.01956047f,
    -0.01475671f
}};

} // namespace detail

// =============================================================================
// HalfbandFilter Class
// =============================================================================

/// @brief Symmetric FIR halfband filter for linear-phase oversampling.
/// @tparam NumTaps Number of filter taps (must be odd)
template<size_t NumTaps>
class HalfbandFilter {
public:
    static_assert(NumTaps % 2 == 1, "Halfband filter must have odd number of taps");
    static constexpr size_t kLatency = (NumTaps - 1) / 2;

    HalfbandFilter() noexcept {
        reset();
    }

    /// Set filter coefficients
    void setCoefficients(const float* coeffs, size_t numCoeffs) noexcept {
        // For halfband filters, we store the full symmetric impulse response
        std::fill(coeffs_.begin(), coeffs_.end(), 0.0f);

        // Center tap
        coeffs_[kLatency] = 0.5f;

        // Odd-indexed taps (halfband property: even indices except center are 0)
        for (size_t i = 0; i < numCoeffs && i < NumTaps / 2; ++i) {
            const size_t oddIdx = 2 * i + 1;
            if (oddIdx < kLatency) {
                coeffs_[kLatency - oddIdx] = coeffs[i];
                coeffs_[kLatency + oddIdx] = coeffs[i];
            }
        }
    }

    /// Process a single sample
    [[nodiscard]] float process(float input) noexcept {
        // Shift delay line
        for (size_t i = NumTaps - 1; i > 0; --i) {
            delayLine_[i] = delayLine_[i - 1];
        }
        delayLine_[0] = input;

        // Convolve with symmetric coefficients
        float output = 0.0f;
        for (size_t i = 0; i < NumTaps; ++i) {
            output += delayLine_[i] * coeffs_[i];
        }

        // Flush denormals
        if (std::abs(output) < 1e-15f) {
            output = 0.0f;
        }

        return output;
    }

    /// Process a block of samples in-place
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    /// Reset filter state
    void reset() noexcept {
        std::fill(delayLine_.begin(), delayLine_.end(), 0.0f);
    }

    /// Get filter latency in samples
    [[nodiscard]] static constexpr size_t getLatency() noexcept {
        return kLatency;
    }

private:
    std::array<float, NumTaps> coeffs_{};
    std::array<float, NumTaps> delayLine_{};
};

// Type aliases for quality levels
using HalfbandFilterStandard = HalfbandFilter<detail::kStandardFirLength>;
using HalfbandFilterHigh = HalfbandFilter<detail::kHighFirLength>;

// =============================================================================
// Oversampler Class Template
// =============================================================================

/// @brief Upsampling/downsampling primitive for anti-aliased nonlinear processing.
///
/// @tparam Factor Oversampling factor (2 or 4)
/// @tparam NumChannels Number of audio channels (1 = mono, 2 = stereo)
template<size_t Factor = 2, size_t NumChannels = 2>
class Oversampler {
public:
    static_assert(Factor == 2 || Factor == 4, "Oversampler only supports 2x or 4x");
    static_assert(NumChannels >= 1 && NumChannels <= 2, "Oversampler supports 1-2 channels");

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
        return Factor;
    }

    /// Number of cascaded 2x stages (1 for 2x, 2 for 4x)
    static constexpr size_t numStages() noexcept {
        return (Factor == 2) ? 1 : 2;
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
    /// @param sampleRate Base sample rate in Hz (e.g., 44100)
    /// @param maxBlockSize Maximum samples per channel per process call
    /// @param quality Filter quality preset (default: Economy for IIR)
    /// @param mode Latency mode (default: ZeroLatency)
    /// @note NOT real-time safe (allocates memory)
    void prepare(
        double sampleRate,
        size_t maxBlockSize,
        OversamplingQuality quality = OversamplingQuality::Economy,
        OversamplingMode mode = OversamplingMode::ZeroLatency
    ) noexcept;

    /// Check if oversampler has been prepared
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// Get oversampling factor (2 or 4)
    [[nodiscard]] constexpr size_t getFactor() const noexcept { return Factor; }

    /// Get latency introduced by oversampling (in base-rate samples)
    [[nodiscard]] size_t getLatency() const noexcept { return latencySamples_; }

    /// Get current quality setting
    [[nodiscard]] OversamplingQuality getQuality() const noexcept { return quality_; }

    /// Get current mode setting
    [[nodiscard]] OversamplingMode getMode() const noexcept { return mode_; }

    /// Check if using FIR filters (vs IIR)
    [[nodiscard]] bool isUsingFir() const noexcept { return useFir_; }

    // =========================================================================
    // Processing (real-time safe)
    // =========================================================================

    /// Process stereo audio with oversampling.
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
    /// @param buffer Input/output buffer
    /// @param numSamples Number of samples
    /// @param callback User function applied at oversampled rate
    void process(
        float* buffer,
        size_t numSamples,
        const MonoCallback& callback
    ) noexcept;

    // =========================================================================
    // Low-Level Access
    // =========================================================================

    /// Upsample only (for manual processing pipeline)
    void upsample(const float* input, float* output, size_t numSamples, size_t channel = 0) noexcept;

    /// Downsample only (for manual processing pipeline)
    void downsample(const float* input, float* output, size_t numSamples, size_t channel = 0) noexcept;

    /// Get pointer to internal upsampled buffer
    [[nodiscard]] float* getOversampledBuffer(size_t channel = 0) noexcept;

    /// Get size of oversampled buffer
    [[nodiscard]] size_t getOversampledBufferSize() const noexcept;

    // =========================================================================
    // State Management
    // =========================================================================

    /// Clear all filter states
    void reset() noexcept;

private:
    // Configuration
    OversamplingQuality quality_ = OversamplingQuality::Economy;
    OversamplingMode mode_ = OversamplingMode::ZeroLatency;
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    size_t latencySamples_ = 0;
    bool prepared_ = false;
    bool useFir_ = false;

    // IIR filters for Economy/ZeroLatency mode (per channel, per stage)
    static constexpr size_t kNumStages = numStages();
    std::array<BiquadCascade<4>, NumChannels * kNumStages> iirUpsampleFilters_;
    std::array<BiquadCascade<4>, NumChannels * kNumStages> iirDownsampleFilters_;

    // FIR filters for Standard/High quality with LinearPhase mode
    std::array<HalfbandFilterStandard, NumChannels * kNumStages> firStandardUpsample_;
    std::array<HalfbandFilterStandard, NumChannels * kNumStages> firStandardDownsample_;
    std::array<HalfbandFilterHigh, NumChannels * kNumStages> firHighUpsample_;
    std::array<HalfbandFilterHigh, NumChannels * kNumStages> firHighDownsample_;

    // Pre-allocated buffers
    std::vector<float> oversampledBuffer_;  // Size: maxBlockSize * Factor * NumChannels
    std::vector<float> tempBuffer_;         // Temp buffer for FIR processing

    // Internal helpers
    void configureIirFilters() noexcept;
    void configureFirFilters() noexcept;
    size_t getFilterIndex(size_t channel, size_t stage) const noexcept {
        return channel * kNumStages + stage;
    }

    // IIR processing paths
    void upsampleIir(const float* input, float* output, size_t numSamples, size_t channel) noexcept;
    void downsampleIir(const float* input, float* output, size_t numSamples, size_t channel) noexcept;

    // FIR processing paths
    void upsampleFir(const float* input, float* output, size_t numSamples, size_t channel) noexcept;
    void downsampleFir(const float* input, float* output, size_t numSamples, size_t channel) noexcept;
};

// =============================================================================
// Common Type Aliases
// =============================================================================

/// 2x stereo oversampler (most common configuration)
using Oversampler2x = Oversampler<2, 2>;

/// 4x stereo oversampler (for heavy distortion)
using Oversampler4x = Oversampler<4, 2>;

/// 2x mono oversampler
using Oversampler2xMono = Oversampler<2, 1>;

/// 4x mono oversampler
using Oversampler4xMono = Oversampler<4, 1>;

// =============================================================================
// Implementation
// =============================================================================

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::prepare(
    double sampleRate,
    size_t maxBlockSize,
    OversamplingQuality quality,
    OversamplingMode mode
) noexcept {
    // Validate sample rate
    if (sampleRate <= 0.0) {
        return;
    }

    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    quality_ = quality;
    mode_ = mode;

    // Determine filter type based on quality and mode
    // - Economy always uses IIR (zero latency)
    // - ZeroLatency mode always uses IIR
    // - Standard/High with LinearPhase uses FIR
    useFir_ = (quality_ != OversamplingQuality::Economy) &&
              (mode_ == OversamplingMode::LinearPhase);

    // Calculate latency based on filter type
    if (!useFir_) {
        // IIR filters have zero latency
        latencySamples_ = 0;
    } else {
        // FIR latency depends on quality level
        // Latency per 2x stage (in oversampled samples), converted to base rate
        size_t latencyPerStage = 0;
        if (quality_ == OversamplingQuality::Standard) {
            latencyPerStage = detail::kStandardFirLatency;
        } else if (quality_ == OversamplingQuality::High) {
            latencyPerStage = detail::kHighFirLatency;
        }

        // Total latency: up + down for each stage, converted to base rate samples
        // For 2x: 1 stage, latency = 2 * latencyPerStage / 2 = latencyPerStage
        // For 4x: 2 stages, latency = 2 * (latencyPerStage/2 + latencyPerStage/4)
        if constexpr (Factor == 2) {
            latencySamples_ = latencyPerStage;  // 15 or 31
        } else {
            // 4x: latencies accumulate across stages
            latencySamples_ = latencyPerStage * 2;  // 30 or 62
        }
    }

    // Allocate oversampled buffer
    const size_t bufferSize = maxBlockSize_ * Factor * NumChannels;
    oversampledBuffer_.resize(bufferSize);
    std::fill(oversampledBuffer_.begin(), oversampledBuffer_.end(), 0.0f);

    // Allocate temp buffer for FIR processing
    tempBuffer_.resize(bufferSize);
    std::fill(tempBuffer_.begin(), tempBuffer_.end(), 0.0f);

    // Configure filters
    if (useFir_) {
        configureFirFilters();
    } else {
        configureIirFilters();
    }

    prepared_ = true;
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::configureIirFilters() noexcept {
    // Calculate cutoff frequency for anti-aliasing
    // Cutoff should be just below original Nyquist
    const float baseCutoff = static_cast<float>(sampleRate_) * 0.45f;  // 45% of base Nyquist

    // Configure each stage
    for (size_t stage = 0; stage < kNumStages; ++stage) {
        // Sample rate at this stage
        const double stageSampleRate = sampleRate_ * (1 << (stage + 1));
        const float stageCutoff = baseCutoff;  // Same cutoff in Hz

        for (size_t ch = 0; ch < NumChannels; ++ch) {
            const size_t idx = getFilterIndex(ch, stage);

            // Configure as Butterworth lowpass
            iirUpsampleFilters_[idx].setButterworth(
                FilterType::Lowpass,
                stageCutoff,
                static_cast<float>(stageSampleRate)
            );

            iirDownsampleFilters_[idx].setButterworth(
                FilterType::Lowpass,
                stageCutoff,
                static_cast<float>(stageSampleRate)
            );
        }
    }

    // Reset filter states
    for (auto& filter : iirUpsampleFilters_) {
        filter.reset();
    }
    for (auto& filter : iirDownsampleFilters_) {
        filter.reset();
    }
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::configureFirFilters() noexcept {
    // Configure FIR filters based on quality
    for (size_t ch = 0; ch < NumChannels; ++ch) {
        for (size_t stage = 0; stage < kNumStages; ++stage) {
            const size_t idx = getFilterIndex(ch, stage);

            if (quality_ == OversamplingQuality::Standard) {
                firStandardUpsample_[idx].setCoefficients(
                    detail::kStandardFirCoeffs.data(),
                    detail::kStandardFirCoeffs.size()
                );
                firStandardDownsample_[idx].setCoefficients(
                    detail::kStandardFirCoeffs.data(),
                    detail::kStandardFirCoeffs.size()
                );
            } else if (quality_ == OversamplingQuality::High) {
                firHighUpsample_[idx].setCoefficients(
                    detail::kHighFirCoeffs.data(),
                    detail::kHighFirCoeffs.size()
                );
                firHighDownsample_[idx].setCoefficients(
                    detail::kHighFirCoeffs.data(),
                    detail::kHighFirCoeffs.size()
                );
            }
        }
    }

    // Reset filter states
    for (auto& filter : firStandardUpsample_) { filter.reset(); }
    for (auto& filter : firStandardDownsample_) { filter.reset(); }
    for (auto& filter : firHighUpsample_) { filter.reset(); }
    for (auto& filter : firHighDownsample_) { filter.reset(); }
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::reset() noexcept {
    // Reset IIR filters
    for (auto& filter : iirUpsampleFilters_) {
        filter.reset();
    }
    for (auto& filter : iirDownsampleFilters_) {
        filter.reset();
    }

    // Reset FIR filters
    for (auto& filter : firStandardUpsample_) { filter.reset(); }
    for (auto& filter : firStandardDownsample_) { filter.reset(); }
    for (auto& filter : firHighUpsample_) { filter.reset(); }
    for (auto& filter : firHighDownsample_) { filter.reset(); }
}

// =============================================================================
// IIR Processing Paths
// =============================================================================

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::upsampleIir(
    const float* input,
    float* output,
    size_t numSamples,
    size_t channel
) noexcept {
    if constexpr (Factor == 2) {
        // Single 2x stage: zero-stuff then filter
        const size_t idx = getFilterIndex(channel, 0);

        for (size_t i = 0; i < numSamples; ++i) {
            // Zero-stuffing: insert sample, then zero
            output[i * 2] = input[i] * 2.0f;  // Gain compensation
            output[i * 2 + 1] = 0.0f;
        }

        // Apply lowpass filter
        iirUpsampleFilters_[idx].processBlock(output, numSamples * 2);
    } else {
        // 4x: Two cascaded 2x stages
        // Stage 1: 1x -> 2x
        const size_t idx0 = getFilterIndex(channel, 0);
        float* intermediate = output;  // Reuse output buffer

        for (size_t i = 0; i < numSamples; ++i) {
            intermediate[i * 2] = input[i] * 2.0f;
            intermediate[i * 2 + 1] = 0.0f;
        }
        iirUpsampleFilters_[idx0].processBlock(intermediate, numSamples * 2);

        // Stage 2: 2x -> 4x (in-place expansion, work backwards)
        const size_t idx1 = getFilterIndex(channel, 1);
        for (size_t i = numSamples * 2; i > 0; --i) {
            const size_t srcIdx = i - 1;
            const size_t dstIdx = (i - 1) * 2;
            output[dstIdx + 1] = 0.0f;
            output[dstIdx] = intermediate[srcIdx] * 2.0f;
        }
        iirUpsampleFilters_[idx1].processBlock(output, numSamples * 4);
    }
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::downsampleIir(
    const float* input,
    float* output,
    size_t numSamples,
    size_t channel
) noexcept {
    // We need a temporary buffer for filtering since input is const
    float* temp = tempBuffer_.data() + (channel * maxBlockSize_ * Factor);
    std::copy(input, input + numSamples * Factor, temp);

    if constexpr (Factor == 2) {
        // Single 2x stage: filter then decimate
        const size_t idx = getFilterIndex(channel, 0);

        // Apply lowpass filter
        iirDownsampleFilters_[idx].processBlock(temp, numSamples * 2);

        // Decimate: take every 2nd sample
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = temp[i * 2];
        }
    } else {
        // 4x: Two cascaded 2x stages (reverse order)
        // Stage 1: 4x -> 2x
        const size_t idx1 = getFilterIndex(channel, 1);
        iirDownsampleFilters_[idx1].processBlock(temp, numSamples * 4);

        // Decimate to 2x
        for (size_t i = 0; i < numSamples * 2; ++i) {
            temp[i] = temp[i * 2];
        }

        // Stage 2: 2x -> 1x
        const size_t idx0 = getFilterIndex(channel, 0);
        iirDownsampleFilters_[idx0].processBlock(temp, numSamples * 2);

        // Final decimation
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = temp[i * 2];
        }
    }
}

// =============================================================================
// FIR Processing Paths
// =============================================================================

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::upsampleFir(
    const float* input,
    float* output,
    size_t numSamples,
    size_t channel
) noexcept {
    if constexpr (Factor == 2) {
        // Single 2x stage: zero-stuff then filter
        const size_t idx = getFilterIndex(channel, 0);

        for (size_t i = 0; i < numSamples; ++i) {
            // Zero-stuffing: insert sample, then zero
            output[i * 2] = input[i] * 2.0f;  // Gain compensation
            output[i * 2 + 1] = 0.0f;
        }

        // Apply FIR filter based on quality
        if (quality_ == OversamplingQuality::Standard) {
            firStandardUpsample_[idx].processBlock(output, numSamples * 2);
        } else {
            firHighUpsample_[idx].processBlock(output, numSamples * 2);
        }
    } else {
        // 4x: Two cascaded 2x stages
        const size_t idx0 = getFilterIndex(channel, 0);
        const size_t idx1 = getFilterIndex(channel, 1);

        // Stage 1: 1x -> 2x
        for (size_t i = 0; i < numSamples; ++i) {
            output[i * 2] = input[i] * 2.0f;
            output[i * 2 + 1] = 0.0f;
        }

        if (quality_ == OversamplingQuality::Standard) {
            firStandardUpsample_[idx0].processBlock(output, numSamples * 2);
        } else {
            firHighUpsample_[idx0].processBlock(output, numSamples * 2);
        }

        // Stage 2: 2x -> 4x (work backwards for in-place expansion)
        for (size_t i = numSamples * 2; i > 0; --i) {
            const size_t srcIdx = i - 1;
            const size_t dstIdx = (i - 1) * 2;
            output[dstIdx + 1] = 0.0f;
            output[dstIdx] = output[srcIdx] * 2.0f;
        }

        if (quality_ == OversamplingQuality::Standard) {
            firStandardUpsample_[idx1].processBlock(output, numSamples * 4);
        } else {
            firHighUpsample_[idx1].processBlock(output, numSamples * 4);
        }
    }
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::downsampleFir(
    const float* input,
    float* output,
    size_t numSamples,
    size_t channel
) noexcept {
    // We need a temporary buffer for filtering since input is const
    float* temp = tempBuffer_.data() + (channel * maxBlockSize_ * Factor);
    std::copy(input, input + numSamples * Factor, temp);

    if constexpr (Factor == 2) {
        // Single 2x stage: filter then decimate
        const size_t idx = getFilterIndex(channel, 0);

        // Apply FIR filter based on quality
        if (quality_ == OversamplingQuality::Standard) {
            firStandardDownsample_[idx].processBlock(temp, numSamples * 2);
        } else {
            firHighDownsample_[idx].processBlock(temp, numSamples * 2);
        }

        // Decimate: take every 2nd sample
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = temp[i * 2];
        }
    } else {
        // 4x: Two cascaded 2x stages (reverse order)
        const size_t idx0 = getFilterIndex(channel, 0);
        const size_t idx1 = getFilterIndex(channel, 1);

        // Stage 1: 4x -> 2x
        if (quality_ == OversamplingQuality::Standard) {
            firStandardDownsample_[idx1].processBlock(temp, numSamples * 4);
        } else {
            firHighDownsample_[idx1].processBlock(temp, numSamples * 4);
        }

        // Decimate to 2x
        for (size_t i = 0; i < numSamples * 2; ++i) {
            temp[i] = temp[i * 2];
        }

        // Stage 2: 2x -> 1x
        if (quality_ == OversamplingQuality::Standard) {
            firStandardDownsample_[idx0].processBlock(temp, numSamples * 2);
        } else {
            firHighDownsample_[idx0].processBlock(temp, numSamples * 2);
        }

        // Final decimation
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = temp[i * 2];
        }
    }
}

// =============================================================================
// Public Processing Methods
// =============================================================================

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::upsample(
    const float* input,
    float* output,
    size_t numSamples,
    size_t channel
) noexcept {
    if (!prepared_ || channel >= NumChannels) {
        // Fill with zeros if not prepared
        std::fill(output, output + numSamples * Factor, 0.0f);
        return;
    }

    if (useFir_) {
        upsampleFir(input, output, numSamples, channel);
    } else {
        upsampleIir(input, output, numSamples, channel);
    }
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::downsample(
    const float* input,
    float* output,
    size_t numSamples,
    size_t channel
) noexcept {
    if (!prepared_ || channel >= NumChannels) {
        std::fill(output, output + numSamples, 0.0f);
        return;
    }

    if (useFir_) {
        downsampleFir(input, output, numSamples, channel);
    } else {
        downsampleIir(input, output, numSamples, channel);
    }
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::process(
    float* left,
    float* right,
    size_t numSamples,
    const StereoCallback& callback
) noexcept {
    static_assert(NumChannels == 2, "Stereo process requires 2 channels");

    if (!prepared_ || numSamples > maxBlockSize_) {
        return;
    }

    const size_t oversampledSize = numSamples * Factor;
    float* osLeft = oversampledBuffer_.data();
    float* osRight = oversampledBuffer_.data() + maxBlockSize_ * Factor;

    // Upsample
    upsample(left, osLeft, numSamples, 0);
    upsample(right, osRight, numSamples, 1);

    // Apply user callback at oversampled rate
    if (callback) {
        callback(osLeft, osRight, oversampledSize);
    }

    // Downsample
    downsample(osLeft, left, numSamples, 0);
    downsample(osRight, right, numSamples, 1);
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::process(
    float* buffer,
    size_t numSamples,
    const MonoCallback& callback
) noexcept {
    if (!prepared_ || numSamples > maxBlockSize_) {
        return;
    }

    const size_t oversampledSize = numSamples * Factor;
    float* osBuffer = oversampledBuffer_.data();

    // Upsample
    upsample(buffer, osBuffer, numSamples, 0);

    // Apply user callback at oversampled rate
    if (callback) {
        callback(osBuffer, oversampledSize);
    }

    // Downsample
    downsample(osBuffer, buffer, numSamples, 0);
}

template<size_t Factor, size_t NumChannels>
float* Oversampler<Factor, NumChannels>::getOversampledBuffer(size_t channel) noexcept {
    if (channel >= NumChannels || oversampledBuffer_.empty()) {
        return nullptr;
    }
    return oversampledBuffer_.data() + (channel * maxBlockSize_ * Factor);
}

template<size_t Factor, size_t NumChannels>
size_t Oversampler<Factor, NumChannels>::getOversampledBufferSize() const noexcept {
    return maxBlockSize_ * Factor;
}

} // namespace DSP
} // namespace Iterum
