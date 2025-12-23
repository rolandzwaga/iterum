// ==============================================================================
// Layer 1: DSP Primitive - Spectral Buffer
// ==============================================================================
// Complex spectrum storage with magnitude/phase manipulation for spectral
// processing effects (filtering, freeze, morphing).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept accessors, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 1 (depends only on Layer 0 / fft.h)
// - Principle XII: Test-First Development
//
// Reference: specs/007-fft-processor/spec.md
// ==============================================================================

#pragma once

#include "fft.h"  // For Complex struct

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// SpectralBuffer Class
// =============================================================================

/// @brief Complex spectrum storage with magnitude/phase manipulation
class SpectralBuffer {
public:
    SpectralBuffer() noexcept = default;
    ~SpectralBuffer() noexcept = default;

    // Movable
    SpectralBuffer(SpectralBuffer&&) noexcept = default;
    SpectralBuffer& operator=(SpectralBuffer&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Prepare buffer for given FFT size
    /// @param fftSize FFT size (buffer will hold fftSize/2+1 bins)
    /// @note NOT real-time safe (allocates memory)
    void prepare(size_t fftSize) noexcept {
        numBins_ = fftSize / 2 + 1;
        data_.resize(numBins_);
        reset();
    }

    /// @brief Reset all bins to zero
    /// @note Real-time safe
    void reset() noexcept {
        std::fill(data_.begin(), data_.end(), Complex{0.0f, 0.0f});
    }

    // -------------------------------------------------------------------------
    // Polar Access (Magnitude/Phase)
    // -------------------------------------------------------------------------

    /// @brief Get magnitude of bin k: |X[k]|
    [[nodiscard]] float getMagnitude(size_t bin) const noexcept {
        if (bin >= numBins_) return 0.0f;
        return data_[bin].magnitude();
    }

    /// @brief Get phase of bin k in radians: ∠X[k]
    [[nodiscard]] float getPhase(size_t bin) const noexcept {
        if (bin >= numBins_) return 0.0f;
        return data_[bin].phase();
    }

    /// @brief Set magnitude, preserving phase
    void setMagnitude(size_t bin, float magnitude) noexcept {
        if (bin >= numBins_) return;

        // Get current phase
        const float currentPhase = data_[bin].phase();

        // Convert polar to Cartesian: real = mag * cos(phase), imag = mag * sin(phase)
        data_[bin].real = magnitude * std::cos(currentPhase);
        data_[bin].imag = magnitude * std::sin(currentPhase);
    }

    /// @brief Set phase in radians, preserving magnitude
    void setPhase(size_t bin, float phase) noexcept {
        if (bin >= numBins_) return;

        // Get current magnitude
        const float currentMag = data_[bin].magnitude();

        // Convert polar to Cartesian: real = mag * cos(phase), imag = mag * sin(phase)
        data_[bin].real = currentMag * std::cos(phase);
        data_[bin].imag = currentMag * std::sin(phase);
    }

    // -------------------------------------------------------------------------
    // Cartesian Access (Real/Imaginary)
    // -------------------------------------------------------------------------

    /// @brief Get real component of bin k
    [[nodiscard]] float getReal(size_t bin) const noexcept {
        if (bin >= numBins_) return 0.0f;
        return data_[bin].real;
    }

    /// @brief Get imaginary component of bin k
    [[nodiscard]] float getImag(size_t bin) const noexcept {
        if (bin >= numBins_) return 0.0f;
        return data_[bin].imag;
    }

    /// @brief Set both real and imaginary components
    void setCartesian(size_t bin, float real, float imag) noexcept {
        if (bin < numBins_) {
            data_[bin].real = real;
            data_[bin].imag = imag;
        }
    }

    // -------------------------------------------------------------------------
    // Raw Access
    // -------------------------------------------------------------------------

    /// @brief Direct access to complex data array
    /// @note For FFT input/output only
    [[nodiscard]] Complex* data() noexcept { return data_.data(); }
    [[nodiscard]] const Complex* data() const noexcept { return data_.data(); }

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    /// @brief Number of bins (N/2+1)
    [[nodiscard]] size_t numBins() const noexcept { return numBins_; }

    /// @brief Check if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept { return numBins_ > 0; }

private:
    std::vector<Complex> data_;
    size_t numBins_ = 0;
};

} // namespace DSP
} // namespace Iterum
