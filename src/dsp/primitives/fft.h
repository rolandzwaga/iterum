// ==============================================================================
// Layer 1: DSP Primitive - Fast Fourier Transform
// ==============================================================================
// Radix-2 Decimation-in-Time FFT implementation for spectral processing.
// Provides forward (real-to-complex) and inverse (complex-to-real) transforms.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, allocations only in prepare())
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 1 (depends only on Layer 0)
// - Principle X: DSP Constraints (O(N log N) complexity)
// - Principle XII: Test-First Development
//
// Reference: specs/007-fft-processor/spec.md
// Algorithm: Cooley-Tukey Radix-2 DIT
// ==============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// Forward Declarations
// =============================================================================

struct Complex;
class FFT;

// =============================================================================
// Constants
// =============================================================================

/// Minimum supported FFT size
inline constexpr size_t kMinFFTSize = 256;

/// Maximum supported FFT size
inline constexpr size_t kMaxFFTSize = 8192;

// =============================================================================
// Complex Number (POD)
// =============================================================================

/// @brief Simple complex number for FFT operations
/// @note POD type for performance - no virtual functions
struct Complex {
    float real = 0.0f;  ///< Real component
    float imag = 0.0f;  ///< Imaginary component

    // -------------------------------------------------------------------------
    // Arithmetic Operators
    // -------------------------------------------------------------------------

    [[nodiscard]] constexpr Complex operator+(const Complex& other) const noexcept {
        return {real + other.real, imag + other.imag};
    }

    [[nodiscard]] constexpr Complex operator-(const Complex& other) const noexcept {
        return {real - other.real, imag - other.imag};
    }

    [[nodiscard]] constexpr Complex operator*(const Complex& other) const noexcept {
        return {
            real * other.real - imag * other.imag,
            real * other.imag + imag * other.real
        };
    }

    [[nodiscard]] constexpr Complex conjugate() const noexcept {
        return {real, -imag};
    }

    // -------------------------------------------------------------------------
    // Polar Representation
    // -------------------------------------------------------------------------

    /// @brief Get magnitude |z| = sqrt(real^2 + imag^2)
    [[nodiscard]] float magnitude() const noexcept {
        return std::sqrt(real * real + imag * imag);
    }

    /// @brief Get phase angle in radians
    [[nodiscard]] float phase() const noexcept {
        return std::atan2(imag, real);
    }
};

// =============================================================================
// FFT Class
// =============================================================================

/// @brief Core Fast Fourier Transform processor
/// @note Uses Radix-2 Decimation-in-Time (DIT) algorithm
class FFT {
public:
    FFT() noexcept = default;
    ~FFT() noexcept = default;

    // Non-copyable, movable
    FFT(const FFT&) = delete;
    FFT& operator=(const FFT&) = delete;
    FFT(FFT&&) noexcept = default;
    FFT& operator=(FFT&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Prepare FFT for given size (allocates LUTs and buffers)
    /// @param fftSize Power of 2 in range [256, 8192]
    /// @pre fftSize is power of 2
    /// @note NOT real-time safe (allocates memory)
    void prepare(size_t fftSize) noexcept {
        size_ = fftSize;

        // Validate power of 2
        if (fftSize == 0 || !std::has_single_bit(fftSize)) {
            size_ = 0;
            return;
        }

        // Number of bits needed to represent indices
        const size_t numBits = std::countr_zero(fftSize);

        // T039: Generate bit-reversal LUT
        bitReversalLUT_.resize(fftSize);
        for (size_t i = 0; i < fftSize; ++i) {
            size_t reversed = 0;
            size_t temp = i;
            for (size_t b = 0; b < numBits; ++b) {
                reversed = (reversed << 1) | (temp & 1);
                temp >>= 1;
            }
            bitReversalLUT_[i] = reversed;
        }

        // T040: Precompute twiddle factors W_N^k = exp(-2πik/N)
        // Only need N/2 factors due to symmetry
        const size_t halfSize = fftSize / 2;
        twiddleFactors_.resize(halfSize);
        const double twoPi = 2.0 * std::numbers::pi;
        for (size_t k = 0; k < halfSize; ++k) {
            const double angle = -twoPi * static_cast<double>(k) / static_cast<double>(fftSize);
            twiddleFactors_[k] = {
                static_cast<float>(std::cos(angle)),
                static_cast<float>(std::sin(angle))
            };
        }

        // Allocate work buffer
        workBuffer_.resize(fftSize);
    }

    /// @brief Reset internal work buffers (not LUTs)
    /// @note Real-time safe
    void reset() noexcept {
        std::fill(workBuffer_.begin(), workBuffer_.end(), Complex{0.0f, 0.0f});
    }

    // -------------------------------------------------------------------------
    // Processing (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// @brief Forward FFT: real time-domain → complex frequency-domain
    /// @param input N real samples
    /// @param output N/2+1 complex bins (DC to Nyquist)
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept
    void forward(const float* input, Complex* output) noexcept {
        if (!isPrepared() || input == nullptr || output == nullptr) return;

        // T041: Radix-2 DIT Forward FFT

        // Step 1: Copy input to work buffer with bit-reversal permutation
        for (size_t i = 0; i < size_; ++i) {
            const size_t j = bitReversalLUT_[i];
            workBuffer_[j] = {input[i], 0.0f};
        }

        // Step 2: Cooley-Tukey iterative FFT
        // Process stages from 1-point DFTs up to N-point DFT
        for (size_t stage = 1; stage < size_; stage <<= 1) {
            const size_t twiddleStep = size_ / (stage << 1);

            for (size_t k = 0; k < size_; k += (stage << 1)) {
                size_t twiddleIndex = 0;

                for (size_t j = 0; j < stage; ++j) {
                    // Butterfly operation
                    const Complex& twiddle = twiddleFactors_[twiddleIndex];
                    const size_t evenIdx = k + j;
                    const size_t oddIdx = evenIdx + stage;

                    const Complex even = workBuffer_[evenIdx];
                    const Complex odd = workBuffer_[oddIdx] * twiddle;

                    workBuffer_[evenIdx] = even + odd;
                    workBuffer_[oddIdx] = even - odd;

                    twiddleIndex += twiddleStep;
                }
            }
        }

        // T042: Pack output (N/2+1 bins: DC to Nyquist)
        // For real input, output has conjugate symmetry: X[k] = X[N-k]*
        // We only need bins 0 to N/2 (inclusive)
        const size_t numBins = size_ / 2 + 1;
        for (size_t i = 0; i < numBins; ++i) {
            output[i] = workBuffer_[i];
        }

        // DC and Nyquist bins should have zero imaginary part for real input
        // (enforced by symmetry, but we explicitly zero for numerical stability)
        output[0].imag = 0.0f;
        output[size_ / 2].imag = 0.0f;
    }

    /// @brief Inverse FFT: complex frequency-domain → real time-domain
    /// @param input N/2+1 complex bins (DC to Nyquist)
    /// @param output N real samples
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept
    void inverse(const Complex* input, float* output) noexcept {
        if (!isPrepared() || input == nullptr || output == nullptr) return;

        // T044: Unpack complex spectrum to full N-point complex array
        // Reconstruct negative frequencies from conjugate symmetry: X[N-k] = X[k]*
        const size_t halfSize = size_ / 2;

        // DC bin (no conjugate)
        workBuffer_[0] = input[0];

        // Positive frequencies and their conjugate mirrors
        for (size_t k = 1; k < halfSize; ++k) {
            workBuffer_[k] = input[k];
            workBuffer_[size_ - k] = input[k].conjugate();
        }

        // Nyquist bin (no conjugate)
        workBuffer_[halfSize] = input[halfSize];

        // T045: Radix-2 DIT Inverse FFT
        // IFFT is FFT with conjugate twiddle factors, followed by 1/N scaling
        // Or equivalently: conjugate input, FFT, conjugate output, scale

        // Conjugate the input
        for (size_t i = 0; i < size_; ++i) {
            workBuffer_[i].imag = -workBuffer_[i].imag;
        }

        // Apply bit-reversal permutation
        for (size_t i = 0; i < size_; ++i) {
            const size_t j = bitReversalLUT_[i];
            if (i < j) {
                std::swap(workBuffer_[i], workBuffer_[j]);
            }
        }

        // Cooley-Tukey iterative FFT
        for (size_t stage = 1; stage < size_; stage <<= 1) {
            const size_t twiddleStep = size_ / (stage << 1);

            for (size_t k = 0; k < size_; k += (stage << 1)) {
                size_t twiddleIndex = 0;

                for (size_t j = 0; j < stage; ++j) {
                    const Complex& twiddle = twiddleFactors_[twiddleIndex];
                    const size_t evenIdx = k + j;
                    const size_t oddIdx = evenIdx + stage;

                    const Complex even = workBuffer_[evenIdx];
                    const Complex odd = workBuffer_[oddIdx] * twiddle;

                    workBuffer_[evenIdx] = even + odd;
                    workBuffer_[oddIdx] = even - odd;

                    twiddleIndex += twiddleStep;
                }
            }
        }

        // Conjugate and scale output (1/N normalization)
        const float scale = 1.0f / static_cast<float>(size_);
        for (size_t i = 0; i < size_; ++i) {
            // Output should be real; take conjugate then real part (imag should be ~0)
            output[i] = workBuffer_[i].real * scale;
        }
    }

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    /// @brief Get configured FFT size
    [[nodiscard]] size_t size() const noexcept { return size_; }

    /// @brief Get number of output bins (N/2+1)
    [[nodiscard]] size_t numBins() const noexcept { return size_ / 2 + 1; }

    /// @brief Check if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept { return size_ > 0; }

private:
    size_t size_ = 0;
    std::vector<size_t> bitReversalLUT_;
    std::vector<Complex> twiddleFactors_;
    std::vector<Complex> workBuffer_;
};

} // namespace DSP
} // namespace Iterum
