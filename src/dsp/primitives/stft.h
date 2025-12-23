// ==============================================================================
// Layer 1: DSP Primitive - Short-Time Fourier Transform
// ==============================================================================
// STFT for continuous audio stream analysis and OverlapAdd for synthesis.
// Provides streaming spectral processing with configurable windows and overlap.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept process, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 1 (depends only on Layer 0 / fft.h)
// - Principle X: DSP Constraints (COLA windows, proper overlap)
// - Principle XII: Test-First Development
//
// Reference: specs/007-fft-processor/spec.md
// ==============================================================================

#pragma once

#include "fft.h"
#include "spectral_buffer.h"
#include "../core/window_functions.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// STFT Class
// =============================================================================

/// @brief Short-Time Fourier Transform for continuous audio streams
class STFT {
public:
    STFT() noexcept = default;
    ~STFT() noexcept = default;

    // Non-copyable, movable
    STFT(const STFT&) = delete;
    STFT& operator=(const STFT&) = delete;
    STFT(STFT&&) noexcept = default;
    STFT& operator=(STFT&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Prepare STFT processor
    /// @param fftSize FFT size (power of 2, 256-8192)
    /// @param hopSize Frame advance in samples (typically fftSize/2 or fftSize/4)
    /// @param window Window type for analysis
    /// @param kaiserBeta Kaiser beta parameter (only used if window == Kaiser)
    /// @pre fftSize is power of 2
    /// @pre hopSize <= fftSize
    /// @note NOT real-time safe (allocates memory)
    void prepare(
        size_t fftSize,
        size_t hopSize,
        WindowType window = WindowType::Hann,
        float kaiserBeta = 9.0f
    ) noexcept {
        fftSize_ = fftSize;
        hopSize_ = hopSize;
        windowType_ = window;

        // Prepare internal FFT
        fft_.prepare(fftSize);

        // Generate window
        window_ = Window::generate(window, fftSize, kaiserBeta);

        // Allocate input buffer
        // Size: fftSize + max expected batch size
        // For batch testing, we need room for multiple frames worth of input
        // Using 8*fftSize allows pushing up to 7*fftSize samples before processing
        inputBuffer_.resize(fftSize * 8, 0.0f);

        // Allocate windowed frame buffer
        windowedFrame_.resize(fftSize, 0.0f);

        // Reset state
        writeIndex_ = 0;
        samplesAvailable_ = 0;
    }

    /// @brief Reset internal buffers (clear accumulated samples)
    /// @note Real-time safe
    void reset() noexcept {
        std::fill(inputBuffer_.begin(), inputBuffer_.end(), 0.0f);
        writeIndex_ = 0;
        samplesAvailable_ = 0;
    }

    // -------------------------------------------------------------------------
    // Input (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// @brief Push samples into input buffer
    /// @param input Input samples
    /// @param numSamples Number of samples
    /// @note Real-time safe, noexcept
    void pushSamples(const float* input, size_t numSamples) noexcept {
        if (input == nullptr || !isPrepared()) return;

        // Copy samples into circular buffer
        for (size_t i = 0; i < numSamples; ++i) {
            inputBuffer_[writeIndex_] = input[i];
            writeIndex_ = (writeIndex_ + 1) % inputBuffer_.size();
            ++samplesAvailable_;
        }
    }

    // -------------------------------------------------------------------------
    // Analysis (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// @brief Check if enough samples for analysis frame
    [[nodiscard]] bool canAnalyze() const noexcept {
        // Need at least fftSize samples for the first frame
        // After that, need hopSize new samples for each subsequent frame
        return samplesAvailable_ >= fftSize_;
    }

    /// @brief Perform windowed FFT analysis
    /// @param output SpectralBuffer to receive spectrum
    /// @pre canAnalyze() returns true
    /// @note Real-time safe, noexcept
    void analyze(SpectralBuffer& output) noexcept {
        if (!canAnalyze() || !output.isPrepared()) return;

        // Calculate read position (start of current frame)
        // We want to read the oldest fftSize samples
        const size_t bufSize = inputBuffer_.size();
        size_t readIdx = (writeIndex_ + bufSize - samplesAvailable_) % bufSize;

        // Extract and window the frame
        for (size_t i = 0; i < fftSize_; ++i) {
            windowedFrame_[i] = inputBuffer_[readIdx] * window_[i];
            readIdx = (readIdx + 1) % bufSize;
        }

        // Perform FFT
        fft_.forward(windowedFrame_.data(), output.data());

        // Consume hopSize samples (advance by hop)
        samplesAvailable_ -= hopSize_;
    }

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    [[nodiscard]] size_t fftSize() const noexcept { return fftSize_; }
    [[nodiscard]] size_t hopSize() const noexcept { return hopSize_; }
    [[nodiscard]] WindowType windowType() const noexcept { return windowType_; }

    /// @brief Processing latency in samples (equals fftSize)
    [[nodiscard]] size_t latency() const noexcept { return fftSize_; }

    [[nodiscard]] bool isPrepared() const noexcept { return fftSize_ > 0; }

private:
    FFT fft_;
    std::vector<float> window_;
    std::vector<float> inputBuffer_;
    std::vector<float> windowedFrame_;
    WindowType windowType_ = WindowType::Hann;
    size_t fftSize_ = 0;
    size_t hopSize_ = 0;
    size_t writeIndex_ = 0;
    size_t samplesAvailable_ = 0;
};

// =============================================================================
// OverlapAdd Class
// =============================================================================

/// @brief Overlap-Add synthesis for STFT reconstruction
class OverlapAdd {
public:
    OverlapAdd() noexcept = default;
    ~OverlapAdd() noexcept = default;

    // Non-copyable, movable
    OverlapAdd(const OverlapAdd&) = delete;
    OverlapAdd& operator=(const OverlapAdd&) = delete;
    OverlapAdd(OverlapAdd&&) noexcept = default;
    OverlapAdd& operator=(OverlapAdd&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Prepare synthesis processor (simple version, no window normalization)
    /// @param fftSize FFT size (must match STFT)
    /// @param hopSize Frame advance (must match STFT)
    /// @param window Window type (for COLA normalization)
    /// @param kaiserBeta Kaiser beta parameter (only used if window == Kaiser)
    /// @note NOT real-time safe (allocates memory)
    void prepare(
        size_t fftSize,
        size_t hopSize,
        WindowType window = WindowType::Hann,
        float kaiserBeta = 9.0f
    ) noexcept {
        fftSize_ = fftSize;
        hopSize_ = hopSize;

        // Prepare internal FFT
        fft_.prepare(fftSize);

        // Generate synthesis window and compute COLA normalization factor
        synthesisWindow_ = Window::generate(window, fftSize, kaiserBeta);

        // Compute COLA sum: at any position, sum of overlapping windows
        // This is the same as what verifyCOLA computes
        const size_t numOverlaps = (fftSize + hopSize - 1) / hopSize;
        float colaSum = 0.0f;
        for (size_t frame = 0; frame < numOverlaps; ++frame) {
            const size_t idx = frame * hopSize;
            if (idx < fftSize) {
                colaSum += synthesisWindow_[idx];
            }
        }
        // Normalization: divide by COLA sum to get unity gain reconstruction
        colaNormalization_ = (colaSum > 0.0f) ? (1.0f / colaSum) : 1.0f;

        // Output buffer needs to hold at least fftSize + hopSize for overlap-add
        // We use 2*fftSize for safe margin
        outputBuffer_.resize(fftSize * 2, 0.0f);

        // IFFT result buffer
        ifftBuffer_.resize(fftSize, 0.0f);

        // Reset state
        samplesReady_ = 0;
    }

    /// @brief Reset output accumulator
    /// @note Real-time safe
    void reset() noexcept {
        std::fill(outputBuffer_.begin(), outputBuffer_.end(), 0.0f);
        samplesReady_ = 0;
    }

    // -------------------------------------------------------------------------
    // Synthesis (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// @brief Add IFFT frame to output accumulator
    /// @param input SpectralBuffer containing spectrum to synthesize
    /// @note Real-time safe, noexcept
    void synthesize(const SpectralBuffer& input) noexcept {
        if (!isPrepared() || !input.isPrepared()) return;

        // Perform inverse FFT
        fft_.inverse(input.data(), ifftBuffer_.data());

        // Overlap-add: Add IFFT result to output buffer with COLA normalization
        // The normalization factor ensures unity gain reconstruction regardless of overlap ratio
        for (size_t i = 0; i < fftSize_; ++i) {
            outputBuffer_[i] += ifftBuffer_[i] * colaNormalization_;
        }

        // Mark hopSize more samples as ready
        samplesReady_ += hopSize_;
    }

    // -------------------------------------------------------------------------
    // Output (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// @brief Get number of samples available to pull
    [[nodiscard]] size_t samplesAvailable() const noexcept {
        return samplesReady_;
    }

    /// @brief Extract output samples from accumulator
    /// @param output Destination buffer
    /// @param numSamples Number of samples to extract
    /// @pre numSamples <= samplesAvailable()
    /// @note Real-time safe, noexcept
    void pullSamples(float* output, size_t numSamples) noexcept {
        if (output == nullptr || numSamples > samplesReady_) return;

        // Copy samples to output
        std::copy(outputBuffer_.begin(),
                  outputBuffer_.begin() + static_cast<std::ptrdiff_t>(numSamples),
                  output);

        // Shift remaining samples left
        std::copy(outputBuffer_.begin() + static_cast<std::ptrdiff_t>(numSamples),
                  outputBuffer_.end(),
                  outputBuffer_.begin());

        // Zero the freed portion at the end
        std::fill(outputBuffer_.end() - static_cast<std::ptrdiff_t>(numSamples),
                  outputBuffer_.end(),
                  0.0f);

        samplesReady_ -= numSamples;
    }

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    [[nodiscard]] size_t fftSize() const noexcept { return fftSize_; }
    [[nodiscard]] size_t hopSize() const noexcept { return hopSize_; }
    [[nodiscard]] bool isPrepared() const noexcept { return fftSize_ > 0; }

private:
    FFT fft_;
    std::vector<float> synthesisWindow_;
    std::vector<float> outputBuffer_;
    std::vector<float> ifftBuffer_;
    float colaNormalization_ = 1.0f;
    size_t fftSize_ = 0;
    size_t hopSize_ = 0;
    size_t samplesReady_ = 0;
};

} // namespace DSP
} // namespace Iterum
