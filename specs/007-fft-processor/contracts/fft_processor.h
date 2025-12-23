// ==============================================================================
// API Contract: FFT Processor (007-fft-processor)
// ==============================================================================
// This file defines the PUBLIC API contract for the FFT Processor feature.
// Implementation MUST match these signatures exactly.
//
// Layer: 1 (DSP Primitives)
// Dependencies: Layer 0 (dsp/core/window_functions.h, dsp/dsp_utils.h)
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// Forward Declarations
// =============================================================================

struct Complex;
class FFT;
class SpectralBuffer;
class STFT;
class OverlapAdd;

// =============================================================================
// Enumerations
// =============================================================================

/// Window function types for STFT analysis
enum class WindowType : uint8_t {
    Hann,       ///< Hann (Hanning) window - COLA at 50%/75%
    Hamming,    ///< Hamming window - COLA at 50%/75%
    Blackman,   ///< Blackman window - COLA at 50%/75%
    Kaiser      ///< Kaiser window - requires 90% overlap for COLA
};

// =============================================================================
// Complex Number (POD)
// =============================================================================

/// Simple complex number for FFT operations
struct Complex {
    float real = 0.0f;  ///< Real component
    float imag = 0.0f;  ///< Imaginary component

    // Arithmetic operators
    [[nodiscard]] Complex operator+(const Complex& other) const noexcept;
    [[nodiscard]] Complex operator-(const Complex& other) const noexcept;
    [[nodiscard]] Complex operator*(const Complex& other) const noexcept;
    [[nodiscard]] Complex conjugate() const noexcept;

    // Magnitude and phase
    [[nodiscard]] float magnitude() const noexcept;
    [[nodiscard]] float phase() const noexcept;
};

// =============================================================================
// Window Functions (Layer 0 - src/dsp/core/window_functions.h)
// =============================================================================

namespace Window {

/// Generate window coefficients (allocates)
[[nodiscard]] std::vector<float> generate(
    WindowType type,
    size_t size,
    float kaiserBeta = 9.0f
);

/// Fill buffer with Hann window (periodic/DFT-even variant)
void generateHann(float* output, size_t size) noexcept;

/// Fill buffer with Hamming window
void generateHamming(float* output, size_t size) noexcept;

/// Fill buffer with Blackman window
void generateBlackman(float* output, size_t size) noexcept;

/// Fill buffer with Kaiser window
/// @param beta Shape parameter (9.0 = ~80dB stopband)
void generateKaiser(float* output, size_t size, float beta) noexcept;

/// Verify COLA (Constant Overlap-Add) property
/// @param window Window coefficients
/// @param size Window size
/// @param hopSize Frame advance in samples
/// @param tolerance Maximum deviation from unity (default: 1e-6)
/// @return true if overlapping windows sum to constant within tolerance
[[nodiscard]] bool verifyCOLA(
    const float* window,
    size_t size,
    size_t hopSize,
    float tolerance = 1e-6f
) noexcept;

/// Modified Bessel function of the first kind, order 0
/// Used for Kaiser window computation
[[nodiscard]] float besselI0(float x) noexcept;

} // namespace Window

// =============================================================================
// FFT Class (Layer 1 - src/dsp/primitives/fft.h)
// =============================================================================

/// Core Fast Fourier Transform processor
/// Uses Radix-2 Decimation-in-Time (DIT) algorithm
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

    /// Prepare FFT for given size (allocates LUTs and buffers)
    /// @param fftSize Power of 2 in range [256, 8192]
    /// @pre fftSize is power of 2
    /// @note NOT real-time safe (allocates memory)
    void prepare(size_t fftSize) noexcept;

    /// Reset internal work buffers (not LUTs)
    /// @note Real-time safe
    void reset() noexcept;

    // -------------------------------------------------------------------------
    // Processing (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// Forward FFT: real time-domain → complex frequency-domain
    /// @param input N real samples
    /// @param output N/2+1 complex bins (DC to Nyquist)
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept
    void forward(const float* input, Complex* output) noexcept;

    /// Inverse FFT: complex frequency-domain → real time-domain
    /// @param input N/2+1 complex bins (DC to Nyquist)
    /// @param output N real samples
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept
    void inverse(const Complex* input, float* output) noexcept;

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    /// Get configured FFT size
    [[nodiscard]] size_t size() const noexcept;

    /// Get number of output bins (N/2+1)
    [[nodiscard]] size_t numBins() const noexcept;

    /// Check if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    size_t size_ = 0;
    std::vector<size_t> bitReversalLUT_;
    std::vector<Complex> twiddleFactors_;
    std::vector<Complex> workBuffer_;
};

// =============================================================================
// SpectralBuffer Class (Layer 1 - src/dsp/primitives/spectral_buffer.h)
// =============================================================================

/// Complex spectrum storage with magnitude/phase manipulation
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

    /// Prepare buffer for given FFT size
    /// @param fftSize FFT size (buffer will hold fftSize/2+1 bins)
    /// @note NOT real-time safe (allocates memory)
    void prepare(size_t fftSize) noexcept;

    /// Reset all bins to zero
    /// @note Real-time safe
    void reset() noexcept;

    // -------------------------------------------------------------------------
    // Polar Access (Magnitude/Phase)
    // -------------------------------------------------------------------------

    /// Get magnitude of bin k: |X[k]|
    [[nodiscard]] float getMagnitude(size_t bin) const noexcept;

    /// Get phase of bin k in radians: ∠X[k]
    [[nodiscard]] float getPhase(size_t bin) const noexcept;

    /// Set magnitude, preserving phase
    void setMagnitude(size_t bin, float magnitude) noexcept;

    /// Set phase in radians, preserving magnitude
    void setPhase(size_t bin, float phase) noexcept;

    // -------------------------------------------------------------------------
    // Cartesian Access (Real/Imaginary)
    // -------------------------------------------------------------------------

    /// Get real component of bin k
    [[nodiscard]] float getReal(size_t bin) const noexcept;

    /// Get imaginary component of bin k
    [[nodiscard]] float getImag(size_t bin) const noexcept;

    /// Set both real and imaginary components
    void setCartesian(size_t bin, float real, float imag) noexcept;

    // -------------------------------------------------------------------------
    // Raw Access
    // -------------------------------------------------------------------------

    /// Direct access to complex data array
    /// @note For FFT input/output only
    [[nodiscard]] Complex* data() noexcept;
    [[nodiscard]] const Complex* data() const noexcept;

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    /// Number of bins (N/2+1)
    [[nodiscard]] size_t numBins() const noexcept;

    /// Check if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    std::vector<Complex> data_;
};

// =============================================================================
// STFT Class (Layer 1 - src/dsp/primitives/stft.h)
// =============================================================================

/// Short-Time Fourier Transform for continuous audio streams
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

    /// Prepare STFT processor
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
    ) noexcept;

    /// Reset internal buffers (clear accumulated samples)
    /// @note Real-time safe
    void reset() noexcept;

    // -------------------------------------------------------------------------
    // Input (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// Push samples into input buffer
    /// @param input Input samples
    /// @param numSamples Number of samples
    /// @note Real-time safe, noexcept
    void pushSamples(const float* input, size_t numSamples) noexcept;

    // -------------------------------------------------------------------------
    // Analysis (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// Check if enough samples for analysis frame
    [[nodiscard]] bool canAnalyze() const noexcept;

    /// Perform windowed FFT analysis
    /// @param output SpectralBuffer to receive spectrum
    /// @pre canAnalyze() returns true
    /// @note Real-time safe, noexcept
    void analyze(SpectralBuffer& output) noexcept;

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    [[nodiscard]] size_t fftSize() const noexcept;
    [[nodiscard]] size_t hopSize() const noexcept;
    [[nodiscard]] WindowType windowType() const noexcept;

    /// Processing latency in samples (equals fftSize)
    [[nodiscard]] size_t latency() const noexcept;

    [[nodiscard]] bool isPrepared() const noexcept;

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
// OverlapAdd Class (Layer 1 - src/dsp/primitives/stft.h)
// =============================================================================

/// Overlap-Add synthesis for STFT reconstruction
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

    /// Prepare synthesis processor
    /// @param fftSize FFT size (must match STFT)
    /// @param hopSize Frame advance (must match STFT)
    /// @note NOT real-time safe (allocates memory)
    void prepare(size_t fftSize, size_t hopSize) noexcept;

    /// Reset output accumulator
    /// @note Real-time safe
    void reset() noexcept;

    // -------------------------------------------------------------------------
    // Synthesis (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// Add IFFT frame to output accumulator
    /// @param input SpectralBuffer containing spectrum to synthesize
    /// @note Real-time safe, noexcept
    void synthesize(const SpectralBuffer& input) noexcept;

    // -------------------------------------------------------------------------
    // Output (Real-Time Safe)
    // -------------------------------------------------------------------------

    /// Get number of samples available to pull
    [[nodiscard]] size_t samplesAvailable() const noexcept;

    /// Extract output samples from accumulator
    /// @param output Destination buffer
    /// @param numSamples Number of samples to extract
    /// @pre numSamples <= samplesAvailable()
    /// @note Real-time safe, noexcept
    void pullSamples(float* output, size_t numSamples) noexcept;

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    [[nodiscard]] size_t fftSize() const noexcept;
    [[nodiscard]] size_t hopSize() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    FFT fft_;
    std::vector<float> outputBuffer_;
    std::vector<float> ifftBuffer_;
    size_t fftSize_ = 0;
    size_t hopSize_ = 0;
    size_t samplesReady_ = 0;
};

} // namespace DSP
} // namespace Iterum
