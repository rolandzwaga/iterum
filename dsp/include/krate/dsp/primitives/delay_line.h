// ==============================================================================
// Layer 1: DSP Primitive - DelayLine
// ==============================================================================
// Real-time safe circular buffer delay line with fractional interpolation.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in read/write)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <cstddef>
#include <vector>
#include <cmath>
#include <algorithm>

namespace Krate {
namespace DSP {

/// @brief Compute next power of 2 greater than or equal to n.
/// @param n Input value
/// @return Next power of 2, or n if already power of 2
inline constexpr size_t nextPowerOf2(size_t n) noexcept {
    if (n == 0) return 1;
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
}

/// @brief Real-time safe circular buffer delay line with fractional interpolation.
///
/// Provides integer, linear, and allpass interpolation modes for different use cases:
/// - read(): Integer delay, fastest, for fixed sample-aligned delays
/// - readLinear(): Fractional delay with linear interpolation, for modulated delays
/// - readAllpass(): Fractional delay with allpass interpolation, for feedback loops
///
/// @note All read/write methods are noexcept and allocation-free for real-time safety.
/// @note Memory is allocated only in prepare(), which must be called before setActive().
///
/// @example Basic usage:
/// @code
/// DelayLine delay;
/// delay.prepare(44100.0, 1.0f);  // 1 second max delay
///
/// // In audio callback:
/// delay.write(inputSample);
/// float output = delay.read(22050);  // 0.5 second delay
/// @endcode
class DelayLine {
public:
    /// @brief Default constructor. Creates an uninitialized delay line.
    /// @note prepare() must be called before use.
    DelayLine() noexcept = default;

    /// @brief Destructor.
    ~DelayLine() = default;

    // Non-copyable, movable
    DelayLine(const DelayLine&) = delete;
    DelayLine& operator=(const DelayLine&) = delete;
    DelayLine(DelayLine&&) noexcept = default;
    DelayLine& operator=(DelayLine&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (call before audio processing)
    // =========================================================================

    /// @brief Prepare the delay line for processing.
    ///
    /// Allocates the internal buffer based on sample rate and maximum delay time.
    /// Buffer is automatically sized to the next power of 2 for efficient wraparound.
    ///
    /// @param sampleRate The sample rate in Hz (e.g., 44100.0, 48000.0, 96000.0)
    /// @param maxDelaySeconds Maximum delay time in seconds (up to 10 seconds at 192kHz)
    ///
    /// @note This method allocates memory and must be called before setActive(true).
    /// @note Calling prepare() again reconfigures the delay line and clears the buffer.
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;

    /// @brief Clear the buffer to silence without reallocating.
    ///
    /// Use this when starting playback to prevent artifacts from previous audio.
    /// Faster than prepare() when buffer size doesn't need to change.
    void reset() noexcept;

    // =========================================================================
    // Processing Methods (real-time safe)
    // =========================================================================

    /// @brief Write a sample to the delay line.
    ///
    /// @param sample The input sample to write.
    ///
    /// @note Call this once per sample, before any read operations.
    /// @note O(1) time complexity, no allocations.
    void write(float sample) noexcept;

    /// @brief Read a sample at an integer delay (no interpolation).
    ///
    /// @param delaySamples Number of samples to delay (0 = current sample).
    /// @return The delayed sample value.
    ///
    /// @note Delay is clamped to [0, maxDelaySamples].
    /// @note Fastest read method; use when delay time doesn't change.
    /// @note O(1) time complexity.
    [[nodiscard]] float read(size_t delaySamples) const noexcept;

    /// @brief Read a sample at a fractional delay with linear interpolation.
    ///
    /// @param delaySamples Number of samples to delay (fractional allowed).
    /// @return The interpolated sample value.
    ///
    /// @note Delay is clamped to [0, maxDelaySamples].
    /// @note Use for LFO-modulated delays (chorus, flanger, vibrato).
    /// @note O(1) time complexity.
    [[nodiscard]] float readLinear(float delaySamples) const noexcept;

    /// @brief Read a sample at a fractional delay with cubic Hermite interpolation.
    ///
    /// @param delaySamples Number of samples to delay (fractional allowed).
    /// @return The interpolated sample value.
    ///
    /// @note Delay is clamped to [1, maxDelaySamples - 1] (needs neighbors).
    /// @note Use for chorus and pitch-modulated delays requiring smooth interpolation.
    /// @note O(1) time complexity.
    [[nodiscard]] float readCubic(float delaySamples) const noexcept;

    /// @brief Read a sample at a fractional delay with allpass interpolation.
    ///
    /// @param delaySamples Number of samples to delay (fractional allowed).
    /// @return The interpolated sample value.
    ///
    /// @note Delay is clamped to [0, maxDelaySamples].
    /// @note Use ONLY for fixed delays in feedback loops.
    /// @note Do NOT use for modulated delays (causes artifacts).
    /// @note O(1) time complexity.
    /// @warning Updates internal state; call order matters in feedback networks.
    [[nodiscard]] float readAllpass(float delaySamples) noexcept;

    /// @brief Precomputed tap for `readAllpass` at a FIXED fractional delay.
    ///
    /// `readAllpass(float)` re-derives a clamp, a `std::floor`, a subtraction
    /// and a float DIVISION on every sample. In a diffusion cascade or a
    /// static feedback loop the delay does not move from sample to sample, so
    /// all four are loop-invariant. The two indices are RELATIVE offsets from
    /// the write head (`read()` computes `(writeIndex_ - 1 - offset) & mask_`),
    /// so a tap stays valid for the life of the delay setting, across any
    /// number of `write()` calls.
    struct AllpassTap {
        std::size_t index0 = 0;  ///< integer part of the delay, in samples
        std::size_t index1 = 0;  ///< index0 + 1, clamped to the maximum delay
        float coeff = 1.0f;      ///< (1 - frac) / (1 + frac)
    };

    /// @brief Precomputed tap for `readLinear` at a FIXED fractional delay.
    /// @see AllpassTap - same motivation, same relative-offset guarantee.
    struct LinearTap {
        std::size_t index0 = 0;
        std::size_t index1 = 0;
        float frac = 0.0f;
    };

    /// @brief Build a `LinearTap` for `delaySamples`.
    /// @note Bit-for-bit the same clamp/floor/frac arithmetic
    ///       `readLinear(float)` performs inline.
    [[nodiscard]] LinearTap makeLinearTap(float delaySamples) const noexcept;

    /// @brief Linear-interpolated read through a precomputed tap.
    [[nodiscard]] float readLinear(const LinearTap& tap) const noexcept;

    /// @brief Build an `AllpassTap` for `delaySamples`.
    /// @note Bit-for-bit the same clamp/floor/frac/coefficient arithmetic
    ///       `readAllpass(float)` performs inline, so
    ///       `readAllpass(makeAllpassTap(d))` and `readAllpass(d)` return
    ///       identical values for identical state.
    [[nodiscard]] AllpassTap makeAllpassTap(float delaySamples) const noexcept;

    /// @brief Allpass-interpolated read through a precomputed tap.
    /// @note Advances `allpassState_` exactly as `readAllpass(float)` does.
    [[nodiscard]] float readAllpass(const AllpassTap& tap) noexcept;

    /// @brief Fused read-then-write loop at a FIXED allpass tap.
    ///
    /// For each of `numSamples` steps this performs exactly one
    /// `readAllpass(tap)` followed by exactly one `write()`, with `fn(i, y)`
    /// choosing what is written from the sample index and the interpolated
    /// read. That is the read-before-write shape every allpass and comb
    /// structure in this library uses, and expressing it as one call is what
    /// lets `buffer_`, `mask_`, `writeIndex_` and `allpassState_` live in
    /// registers for the whole block instead of being re-loaded through
    /// `this` on every sample.
    ///
    /// Bit-identical to the equivalent `readAllpass(tap)` / `write()` pair
    /// repeated `numSamples` times: same reads, same arithmetic, same order.
    ///
    /// @param tap Precomputed tap; must come from `makeAllpassTap`.
    /// @param numSamples Iteration count.
    /// @param fn Callable `float(size_t index, float interpolatedRead)`
    ///           returning the value to write.
    template <typename Fn>
    void processFixedTap(const AllpassTap& tap, std::size_t numSamples, Fn&& fn) noexcept {
        float* const buf = buffer_.data();
        const std::size_t m = mask_;
        std::size_t w = writeIndex_;
        float state = allpassState_;

        for (std::size_t i = 0; i < numSamples; ++i) {
            const float x0 = buf[(w - 1 - tap.index0) & m];
            const float x1 = buf[(w - 1 - tap.index1) & m];
            const float y = x0 + (tap.coeff * (state - x1));
            state = y;

            buf[w] = fn(i, y);
            w = (w + 1) & m;
        }

        allpassState_ = state;
        writeIndex_ = w;
    }

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get the maximum delay in samples.
    /// @return Maximum delay samples, or 0 if not prepared.
    [[nodiscard]] size_t maxDelaySamples() const noexcept;

    /// @brief Get the current sample rate.
    /// @return Sample rate in Hz, or 0 if not prepared.
    [[nodiscard]] double sampleRate() const noexcept;

    /// @brief Peek at a sample that will be overwritten after N write() calls.
    ///
    /// This is useful for reading existing delay content before overwriting it,
    /// e.g., for additive excitation in physical modeling synthesis.
    ///
    /// @param offset Number of write() calls until this position is overwritten.
    ///               offset=0 returns the sample at current writeIndex_.
    /// @return The sample value at that position.
    ///
    /// @note O(1) time complexity, no allocations.
    /// @note FR-033: Enables reading existing content for additive excitation.
    [[nodiscard]] float peekNext(size_t offset) const noexcept;

private:
    std::vector<float> buffer_;      ///< Circular buffer (power-of-2 size)
    size_t mask_ = 0;                ///< Bitmask for wraparound (bufferSize - 1)
    size_t writeIndex_ = 0;          ///< Current write position
    float allpassState_ = 0.0f;      ///< Previous output for allpass interpolation
    double sampleRate_ = 0.0;        ///< Current sample rate
    size_t maxDelaySamples_ = 0;     ///< Maximum delay (user-requested, not buffer size)
};

// =============================================================================
// Inline Implementation (Skeleton - to be completed)
// =============================================================================

inline void DelayLine::prepare(double sampleRate, float maxDelaySeconds) noexcept {
    sampleRate_ = sampleRate;
    maxDelaySamples_ = static_cast<size_t>(sampleRate * static_cast<double>(maxDelaySeconds));

    // Buffer size must be power of 2 for efficient bitwise wrap
    // Add 1 to ensure we can always read at maxDelaySamples
    const size_t bufferSize = nextPowerOf2(maxDelaySamples_ + 1);

    buffer_.resize(bufferSize);
    mask_ = bufferSize - 1;

    reset();
}

inline void DelayLine::reset() noexcept {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writeIndex_ = 0;
    allpassState_ = 0.0f;
}

inline void DelayLine::write(float sample) noexcept {
    buffer_[writeIndex_] = sample;
    writeIndex_ = (writeIndex_ + 1) & mask_;
}

inline float DelayLine::read(size_t delaySamples) const noexcept {
    // Clamp delay to valid range [0, maxDelaySamples_]
    const size_t clampedDelay = std::min(delaySamples, maxDelaySamples_);

    // Read from position: (writeIndex_ - 1 - clampedDelay) & mask_
    // writeIndex_ points to next write position, so most recent sample is at writeIndex_ - 1
    const size_t readIndex = (writeIndex_ - 1 - clampedDelay) & mask_;
    return buffer_[readIndex];
}

inline float DelayLine::readLinear(float delaySamples) const noexcept {
    // Clamp delay to valid range [0, maxDelaySamples_]
    const float clampedDelay = std::clamp(delaySamples, 0.0f, static_cast<float>(maxDelaySamples_));

    // Split into integer and fractional parts
    const float intPart = std::floor(clampedDelay);
    const float frac = clampedDelay - intPart;

    // Read two adjacent samples
    const size_t index0 = static_cast<size_t>(intPart);
    const size_t index1 = std::min(index0 + 1, maxDelaySamples_);

    const float y0 = read(index0);
    const float y1 = read(index1);

    // Linear interpolation: y = y0 + frac * (y1 - y0)
    return y0 + frac * (y1 - y0);
}

inline float DelayLine::readCubic(float delaySamples) const noexcept {
    // Clamp delay to [1, maxDelaySamples_ - 1] to ensure neighbors exist
    const float clampedDelay = std::clamp(delaySamples, 1.0f,
        static_cast<float>(maxDelaySamples_ > 1 ? maxDelaySamples_ - 1 : 1));

    // Split into integer and fractional parts
    const float intPart = std::floor(clampedDelay);
    const float frac = clampedDelay - intPart;

    const size_t idx = static_cast<size_t>(intPart);

    // Read 4 adjacent samples: ym1, y0, y1, y2
    const float ym1 = read(idx - 1);
    const float y0  = read(idx);
    const float y1  = read(idx + 1);
    const float y2  = read(idx + 2);

    // Catmull-Rom spline coefficients (tension = 0.5)
    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

inline float DelayLine::readAllpass(float delaySamples) noexcept {
    // Clamp delay to valid range [0, maxDelaySamples_]
    const float clampedDelay = std::clamp(delaySamples, 0.0f, static_cast<float>(maxDelaySamples_));

    // Split into integer and fractional parts
    const float intPart = std::floor(clampedDelay);
    const float frac = clampedDelay - intPart;

    // Read two adjacent samples
    const size_t index0 = static_cast<size_t>(intPart);
    const size_t index1 = std::min(index0 + 1, maxDelaySamples_);

    const float x0 = read(index0);
    const float x1 = read(index1);

    // Allpass coefficient: a = (1 - frac) / (1 + frac)
    // When frac = 0, a = 1 (integer delay)
    // When frac = 0.5, a = 1/3
    // When frac -> 1, a -> 0
    const float a = (1.0f - frac) / (1.0f + frac);

    // First-order allpass interpolation formula:
    // y[n] = x0 + a * (allpassState_ - x1)
    const float y = x0 + a * (allpassState_ - x1);

    // Update state for next call
    allpassState_ = y;

    return y;
}

inline DelayLine::LinearTap DelayLine::makeLinearTap(float delaySamples) const noexcept {
    // Identical arithmetic to readLinear(float) - see that function.
    const float clampedDelay = std::clamp(delaySamples, 0.0f, static_cast<float>(maxDelaySamples_));

    const float intPart = std::floor(clampedDelay);

    LinearTap tap;
    tap.frac = clampedDelay - intPart;
    tap.index0 = static_cast<size_t>(intPart);
    tap.index1 = std::min(tap.index0 + 1, maxDelaySamples_);
    return tap;
}

inline float DelayLine::readLinear(const LinearTap& tap) const noexcept {
    const float y0 = read(tap.index0);
    const float y1 = read(tap.index1);
    return y0 + tap.frac * (y1 - y0);
}

inline DelayLine::AllpassTap DelayLine::makeAllpassTap(float delaySamples) const noexcept {
    // Identical arithmetic to readAllpass(float) - see that function.
    const float clampedDelay = std::clamp(delaySamples, 0.0f, static_cast<float>(maxDelaySamples_));

    const float intPart = std::floor(clampedDelay);
    const float frac = clampedDelay - intPart;

    AllpassTap tap;
    tap.index0 = static_cast<size_t>(intPart);
    tap.index1 = std::min(tap.index0 + 1, maxDelaySamples_);
    tap.coeff = (1.0f - frac) / (1.0f + frac);
    return tap;
}

inline float DelayLine::readAllpass(const AllpassTap& tap) noexcept {
    const float x0 = read(tap.index0);
    const float x1 = read(tap.index1);

    const float y = x0 + tap.coeff * (allpassState_ - x1);
    allpassState_ = y;
    return y;
}

inline size_t DelayLine::maxDelaySamples() const noexcept {
    return maxDelaySamples_;
}

inline double DelayLine::sampleRate() const noexcept {
    return sampleRate_;
}

inline float DelayLine::peekNext(size_t offset) const noexcept {
    // Read from position that will be overwritten after 'offset' write() calls
    const size_t readIndex = (writeIndex_ + offset) & mask_;
    return buffer_[readIndex];
}

} // namespace DSP
} // namespace Krate
