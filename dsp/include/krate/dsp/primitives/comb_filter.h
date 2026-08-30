// ==============================================================================
// Layer 1: DSP Primitives - Comb Filters
// ==============================================================================
// FeedforwardComb, FeedbackComb, and SchroederAllpass comb filters for
// modulation effects, physical modeling, and reverb diffusion.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
//
// Reference: specs/074-comb-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/delay_line.h>

#include <algorithm>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Minimum feedback/coefficient boundary (exclusive of -1.0)
inline constexpr float kMinCombCoeff = -0.9999f;

/// Maximum feedback/coefficient boundary (exclusive of +1.0)
inline constexpr float kMaxCombCoeff = 0.9999f;

/// Minimum gain for FeedforwardComb
inline constexpr float kMinFeedforwardGain = 0.0f;

/// Maximum gain for FeedforwardComb
inline constexpr float kMaxFeedforwardGain = 1.0f;

/// Minimum damping coefficient
inline constexpr float kMinDamping = 0.0f;

/// Maximum damping coefficient
inline constexpr float kMaxDamping = 1.0f;

/// Minimum delay in samples (must be >= 1.0)
inline constexpr float kMinDelaySamples = 1.0f;

// =============================================================================
// FeedforwardComb Class
// =============================================================================

/// @brief Feedforward (FIR) comb filter for flanger/chorus effects.
///
/// Implements the difference equation:
/// @code
/// y[n] = x[n] + g * x[n-D]
/// @endcode
///
/// Creates spectral notches at frequencies f = (2k-1)/(2*D*T) where:
/// - k = 1, 2, 3, ... (harmonic number)
/// - D = delay in samples
/// - T = sample period (1/sampleRate)
///
/// Primary use cases: Flanger, chorus, comb EQ effects
///
/// @par Constitution Compliance
/// - Real-time safe: noexcept, no allocations in process(), no locks
/// - Layer 1: Depends only on Layer 0 (db_utils.h) and DelayLine
///
/// @par Example Usage
/// @code
/// FeedforwardComb comb;
/// comb.prepare(44100.0, 0.05f);  // 50ms max delay
/// comb.setGain(0.7f);
/// comb.setDelayMs(5.0f);         // 5ms delay for flanger
///
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = comb.process(input[i]);
/// }
/// @endcode
class FeedforwardComb {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Default constructor - creates unprepared filter
    FeedforwardComb() noexcept = default;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Initialize filter for a given sample rate and maximum delay.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxDelaySeconds Maximum delay time in seconds
    /// @post Filter is ready for processing
    /// @note FR-015
    void prepare(double sampleRate, float maxDelaySeconds) noexcept {
        sampleRate_ = sampleRate;
        delay_.prepare(sampleRate, maxDelaySeconds);
    }

    /// Clear filter state to zero.
    /// @post DelayLine cleared, ready for new audio
    /// @note FR-016
    void reset() noexcept {
        delay_.reset();
    }

    /// Set the feedforward gain.
    /// @param g Gain value, clamped to [0.0, 1.0]
    /// @note FR-003
    void setGain(float g) noexcept {
        gain_ = std::clamp(g, kMinFeedforwardGain, kMaxFeedforwardGain);
    }

    /// Get the current gain value.
    /// @return Current gain in range [0.0, 1.0]
    [[nodiscard]] float getGain() const noexcept {
        return gain_;
    }

    /// Set delay time in samples.
    /// @param samples Delay in samples, clamped to [1.0, maxDelaySamples]
    /// @note FR-019
    void setDelaySamples(float samples) noexcept {
        const float maxDelay = static_cast<float>(delay_.maxDelaySamples());
        delaySamples_ = std::clamp(samples, kMinDelaySamples, maxDelay);
    }

    /// Set delay time in milliseconds.
    /// @param ms Delay in milliseconds
    /// @note FR-019
    void setDelayMs(float ms) noexcept {
        if (sampleRate_ > 0.0) {
            const float samples = static_cast<float>(ms * 0.001 * sampleRate_);
            setDelaySamples(samples);
        }
    }

    /// Get the current delay in samples.
    /// @return Current delay in samples
    [[nodiscard]] float getDelaySamples() const noexcept {
        return delaySamples_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note FR-001, FR-017, FR-020, FR-021: Real-time safe, NaN/Inf handling
    [[nodiscard]] float process(float input) noexcept {
        // FR-021: NaN/Inf check
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // Unprepared filter bypass
        if (sampleRate_ == 0.0) {
            return input;
        }

        // FR-001: y[n] = x[n] + g * x[n-D]
        // Write first so read(D) returns sample from D samples ago
        delay_.write(input);
        const float delayed = delay_.readLinear(delaySamples_);
        const float output = input + gain_ * delayed;

        return output;
    }

    /// Process a block of samples in-place.
    /// @param buffer Sample buffer (modified in place)
    /// @param numSamples Number of samples to process
    /// @note FR-018: Block processing, identical to N x process()
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (buffer == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    DelayLine delay_;
    float gain_ = 0.5f;
    float delaySamples_ = 1.0f;
    double sampleRate_ = 0.0;
};

// =============================================================================
// FeedbackComb Class
// =============================================================================

/// @brief Feedback (IIR) comb filter for Karplus-Strong and reverb.
///
/// Implements the difference equation with optional damping:
/// @code
/// y[n] = x[n] + g * LP(y[n-D])
/// where LP(x) = (1-d)*x + d*LP_prev  (one-pole lowpass)
/// @endcode
///
/// Creates resonant peaks at frequencies f = k/(D*T) where:
/// - k = 0, 1, 2, 3, ... (harmonic number)
/// - D = delay in samples
/// - T = sample period (1/sampleRate)
///
/// Primary use cases: Karplus-Strong synthesis, reverb comb banks
///
/// @par Constitution Compliance
/// - Real-time safe: noexcept, no allocations in process(), no locks
/// - Layer 1: Depends only on Layer 0 (db_utils.h) and DelayLine
///
/// @par Example Usage
/// @code
/// FeedbackComb comb;
/// comb.prepare(44100.0, 0.1f);    // 100ms max delay
/// comb.setFeedback(0.95f);
/// comb.setDamping(0.3f);          // Some high-frequency damping
/// comb.setDelayMs(10.0f);
///
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = comb.process(input[i]);
/// }
/// @endcode
class FeedbackComb {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Default constructor - creates unprepared filter
    FeedbackComb() noexcept = default;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Initialize filter for a given sample rate and maximum delay.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxDelaySeconds Maximum delay time in seconds
    /// @post Filter is ready for processing
    /// @note FR-015
    void prepare(double sampleRate, float maxDelaySeconds) noexcept {
        sampleRate_ = sampleRate;
        delay_.prepare(sampleRate, maxDelaySeconds);
    }

    /// Clear filter state to zero.
    /// @post DelayLine and dampingState cleared
    /// @note FR-016
    void reset() noexcept {
        delay_.reset();
        dampingState_ = 0.0f;
    }

    /// Set the feedback gain.
    /// @param g Feedback value, clamped to [-0.9999, 0.9999]
    /// @note FR-007
    void setFeedback(float g) noexcept {
        feedback_ = std::clamp(g, kMinCombCoeff, kMaxCombCoeff);
    }

    /// Get the current feedback value.
    /// @return Current feedback in range [-0.9999, 0.9999]
    [[nodiscard]] float getFeedback() const noexcept {
        return feedback_;
    }

    /// Set the damping coefficient.
    /// @param d Damping value, clamped to [0.0, 1.0] (0=bright, 1=dark)
    /// @note FR-010
    void setDamping(float d) noexcept {
        damping_ = std::clamp(d, kMinDamping, kMaxDamping);
    }

    /// Get the current damping value.
    /// @return Current damping in range [0.0, 1.0]
    [[nodiscard]] float getDamping() const noexcept {
        return damping_;
    }

    /// Set delay time in samples.
    /// @param samples Delay in samples, clamped to [1.0, maxDelaySamples]
    /// @note FR-019
    void setDelaySamples(float samples) noexcept {
        const float maxDelay = static_cast<float>(delay_.maxDelaySamples());
        delaySamples_ = std::clamp(samples, kMinDelaySamples, maxDelay);
    }

    /// Set delay time in milliseconds.
    /// @param ms Delay in milliseconds
    /// @note FR-019
    void setDelayMs(float ms) noexcept {
        if (sampleRate_ > 0.0) {
            const float samples = static_cast<float>(ms * 0.001 * sampleRate_);
            setDelaySamples(samples);
        }
    }

    /// Get the current delay in samples.
    /// @return Current delay in samples
    [[nodiscard]] float getDelaySamples() const noexcept {
        return delaySamples_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note FR-005, FR-008, FR-017, FR-020, FR-021, FR-022
    [[nodiscard]] float process(float input) noexcept {
        // FR-021: NaN/Inf check
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // Unprepared filter bypass
        if (sampleRate_ == 0.0) {
            return input;
        }

        // Read delayed feedback signal (from D samples ago)
        // Since we read-before-write, use delaySamples_-1 to get the correct timing
        // delaySamples_ is already clamped to [1, max] so delaySamples_-1 >= 0
        const float readDelay = delaySamples_ - 1.0f;
        const float delayed = delay_.readLinear(std::max(0.0f, readDelay));

        // FR-010: One-pole lowpass damping filter
        // LP(x) = (1-d)*x + d*LP_prev
        const float damped = (1.0f - damping_) * delayed + damping_ * dampingState_;
        dampingState_ = damped;

        // FR-022: Flush denormals in damping state
        dampingState_ = detail::flushDenormal(dampingState_);

        // FR-005: y[n] = x[n] + g * LP(y[n-D])
        float output = input + feedback_ * damped;

        // FR-022: Flush denormals in output before writing to delay
        output = detail::flushDenormal(output);

        // Write output to delay line for feedback
        delay_.write(output);

        return output;
    }

    /// @brief Precomputed read tap for the CURRENT delay setting.
    ///
    /// `process()` re-derives a clamp, a `std::floor`, a float-to-index
    /// conversion and two range clamps inside `readLinear` on every sample,
    /// from a `delaySamples_` that a block-rate caller has already fixed for
    /// the whole block. `makeTap()` does that arithmetic once; the result is a
    /// pair of RELATIVE offsets from the write head plus the interpolation
    /// fraction, so it stays valid across any number of `process` calls made
    /// at the same delay setting.
    [[nodiscard]] DelayLine::LinearTap makeTap() const noexcept {
        return delay_.makeLinearTap(std::max(0.0f, delaySamples_ - 1.0f));
    }

    /// @brief `process()` with the read tap supplied.
    ///
    /// Bit-identical to `process(input)` when `tap` came from `makeTap()` at
    /// the current delay setting AND `input` is finite. The finiteness check is
    /// the caller's here: `process()` resets the whole filter on a non-finite
    /// input, which is a decision a block-rate caller has to take before it
    /// commits to a hoisted loop.
    [[nodiscard]] float process(float input, const DelayLine::LinearTap& tap) noexcept {
        const float delayed = delay_.readLinear(tap);

        const float damped = (1.0f - damping_) * delayed + damping_ * dampingState_;
        dampingState_ = detail::flushDenormal(damped);

        float output = input + feedback_ * damped;
        output = detail::flushDenormal(output);

        delay_.write(output);

        return output;
    }

    /// Process a block of samples in-place.
    /// @param buffer Sample buffer (modified in place)
    /// @param numSamples Number of samples to process
    /// @note FR-018: Block processing, identical to N x process()
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (buffer == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    DelayLine delay_;
    float feedback_ = 0.5f;
    float damping_ = 0.0f;
    float dampingState_ = 0.0f;
    float delaySamples_ = 1.0f;
    double sampleRate_ = 0.0;
};

// =============================================================================
// SchroederAllpass Class
// =============================================================================

/// @brief Schroeder allpass filter for reverb diffusion.
///
/// Implements the difference equation:
/// @code
/// y[n] = -g*x[n] + x[n-D] + g*y[n-D]
/// @endcode
///
/// Provides flat magnitude response (unity gain at all frequencies) while
/// dispersing phase, creating the characteristic smeared quality of reverberant sound.
///
/// Primary use cases: Reverb diffusion networks, decorrelation
///
/// @par Constitution Compliance
/// - Real-time safe: noexcept, no allocations in process(), no locks
/// - Layer 1: Depends only on Layer 0 (db_utils.h) and DelayLine
///
/// @par Example Usage
/// @code
/// SchroederAllpass ap;
/// ap.prepare(44100.0, 0.1f);     // 100ms max delay
/// ap.setCoefficient(0.7f);
/// ap.setDelayMs(30.0f);
///
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = ap.process(input[i]);
/// }
/// @endcode
class SchroederAllpass {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Default constructor - creates unprepared filter
    SchroederAllpass() noexcept = default;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Initialize filter for a given sample rate and maximum delay.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxDelaySeconds Maximum delay time in seconds
    /// @post Filter is ready for processing
    /// @note FR-015
    void prepare(double sampleRate, float maxDelaySeconds) noexcept {
        sampleRate_ = sampleRate;
        delay_.prepare(sampleRate, maxDelaySeconds);
    }

    /// Clear filter state to zero.
    /// @post DelayLine cleared
    /// @note FR-016
    void reset() noexcept {
        delay_.reset();
    }

    /// Set the allpass coefficient.
    /// @param g Coefficient value, clamped to [-0.9999, 0.9999]
    /// @note FR-013
    void setCoefficient(float g) noexcept {
        coefficient_ = std::clamp(g, kMinCombCoeff, kMaxCombCoeff);
    }

    /// Get the current coefficient value.
    /// @return Current coefficient in range [-0.9999, 0.9999]
    [[nodiscard]] float getCoefficient() const noexcept {
        return coefficient_;
    }

    /// Set delay time in samples.
    /// @param samples Delay in samples, clamped to [1.0, maxDelaySamples]
    /// @note FR-019
    void setDelaySamples(float samples) noexcept {
        const float maxDelay = static_cast<float>(delay_.maxDelaySamples());
        delaySamples_ = std::clamp(samples, kMinDelaySamples, maxDelay);
    }

    /// Set delay time in milliseconds.
    /// @param ms Delay in milliseconds
    /// @note FR-019
    void setDelayMs(float ms) noexcept {
        if (sampleRate_ > 0.0) {
            const float samples = static_cast<float>(ms * 0.001 * sampleRate_);
            setDelaySamples(samples);
        }
    }

    /// Get the current delay in samples.
    /// @return Current delay in samples
    [[nodiscard]] float getDelaySamples() const noexcept {
        return delaySamples_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note FR-011, FR-014, FR-017, FR-020, FR-021, FR-022
    [[nodiscard]] float process(float input) noexcept {
        // FR-021: NaN/Inf check
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // Unprepared filter bypass
        if (sampleRate_ == 0.0) {
            return input;
        }

        // FR-011: Schroeder allpass: y[n] = -g*x[n] + x[n-D] + g*y[n-D]
        // Standard single-delay-line implementation using combined buffer:
        // Store w[n] = x[n] + g*y[n] in delay line
        // Then w[n-D] = x[n-D] + g*y[n-D]
        // And y[n] = -g*x[n] + w[n-D] = -g*x[n] + x[n-D] + g*y[n-D]
        //
        // For correct timing with read-before-write, we use delaySamples_-1
        // to compensate for the delay line semantics

        // Read the delayed combined signal (contains x[n-D] + g*y[n-D])
        const float readDelay = std::max(0.0f, delaySamples_ - 1.0f);
        const float delayedW = delay_.readLinear(readDelay);

        // y[n] = -g*x[n] + w[n-D]
        const float output = -coefficient_ * input + delayedW;

        // w[n] = x[n] + g*y[n]
        float writeValue = input + coefficient_ * output;

        // FR-022: Flush denormals before writing to delay line
        writeValue = detail::flushDenormal(writeValue);

        delay_.write(writeValue);

        return output;
    }

    /// Process a block of samples in-place.
    /// @param buffer Sample buffer (modified in place)
    /// @param numSamples Number of samples to process
    /// @note FR-018: Block processing, identical to N x process()
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (buffer == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    DelayLine delay_;
    float coefficient_ = 0.7f;
    float delaySamples_ = 1.0f;
    double sampleRate_ = 0.0;
};

} // namespace DSP
} // namespace Krate
