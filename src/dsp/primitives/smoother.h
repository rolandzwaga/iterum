// ==============================================================================
// Layer 1: DSP Primitive - Parameter Smoother
// ==============================================================================
// Real-time safe parameter interpolation primitives for audio applications.
// Provides three smoother types:
// - OnePoleSmoother: Exponential approach for most parameters
// - LinearRamp: Constant rate for tape-like pitch effects
// - SlewLimiter: Maximum rate limiting with separate rise/fall rates
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (constexpr, RAII, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle X: DSP Constraints (sample-accurate transitions)
// - Principle XII: Test-First Development
//
// Reference: specs/005-parameter-smoother/spec.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Layer 0 dependency for shared math utilities
#include "dsp/core/db_utils.h"

namespace Iterum {
namespace DSP {

// =============================================================================
// Compiler Compatibility Macros
// =============================================================================

/// @brief Cross-platform noinline attribute to prevent function inlining.
/// Required to prevent branch elimination with NaN checks under /fp:fast.
#if defined(_MSC_VER)
#define ITERUM_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define ITERUM_NOINLINE __attribute__((noinline))
#else
#define ITERUM_NOINLINE
#endif

// =============================================================================
// Constants
// =============================================================================

/// Default smoothing time in milliseconds (5ms is standard for most parameters)
inline constexpr float kDefaultSmoothingTimeMs = 5.0f;

/// Threshold for detecting smoothing completion (below audible for normalized values)
inline constexpr float kCompletionThreshold = 0.0001f;

/// Minimum allowed smoothing time in milliseconds
inline constexpr float kMinSmoothingTimeMs = 0.1f;

/// Maximum allowed smoothing time in milliseconds
inline constexpr float kMaxSmoothingTimeMs = 1000.0f;

/// Threshold below which values are flushed to zero (denormal prevention)
inline constexpr float kDenormalThreshold = 1e-15f;

// =============================================================================
// Math Helpers (Internal)
// =============================================================================
// Note: constexprExp and isNaN are provided by dsp/core/db_utils.h (Layer 0)

namespace detail {

/// @brief Platform-independent infinity check using bit manipulation.
/// Uses memcpy for bit extraction (works with any optimization level).
/// @note The calling function should be marked noinline to prevent branch elimination.
/// @param x Value to check
/// @return true if x is positive or negative infinity
[[nodiscard]] inline bool isInf(float x) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    // Infinity: exponent = 0xFF, mantissa = 0
    return (bits & 0x7FFFFFFFu) == 0x7F800000u;
}

/// @brief Flush denormal values to zero.
/// @param x Value to check
/// @return 0 if |x| < threshold, otherwise x
[[nodiscard]] inline float flushDenormal(float x) noexcept {
    return (std::abs(x) < kDenormalThreshold) ? 0.0f : x;
}

}  // namespace detail

// =============================================================================
// Utility Functions
// =============================================================================

/// @brief Calculate one-pole smoothing coefficient from time constant and sample rate.
/// The coefficient determines the exponential smoothing rate.
/// Formula: coeff = exp(-1.0 / (tau * sampleRate))
/// where tau is derived from smoothTimeMs (time to reach ~99% = 5 * tau)
/// @param smoothTimeMs Time to reach 99% of target (in milliseconds)
/// @param sampleRate Sample rate in Hz
/// @return Coefficient for one-pole filter (0.0 to 1.0)
[[nodiscard]] constexpr float calculateOnePolCoefficient(
    float smoothTimeMs,
    float sampleRate
) noexcept {
    // Clamp smoothing time to valid range
    const float clampedTime = (smoothTimeMs < kMinSmoothingTimeMs) ? kMinSmoothingTimeMs
                            : (smoothTimeMs > kMaxSmoothingTimeMs) ? kMaxSmoothingTimeMs
                            : smoothTimeMs;

    // Time to 99% ≈ 5 * tau, so tau = smoothTimeMs / 5.0
    // But we specify smoothTimeMs as time to 99%, so:
    // coefficient = exp(-1.0 / (tau * sampleRate))
    // where tau = smoothTimeMs / (5.0 * 1000.0) (convert ms to seconds, divide by 5)
    // Simplified: coeff = exp(-5000.0 / (smoothTimeMs * sampleRate))
    const float exponent = -5000.0f / (clampedTime * sampleRate);
    return detail::constexprExp(exponent);
}

/// @brief Calculate linear ramp increment per sample.
/// @param delta Total change (target - current)
/// @param rampTimeMs Duration of ramp in milliseconds
/// @param sampleRate Sample rate in Hz
/// @return Per-sample increment value
[[nodiscard]] constexpr float calculateLinearIncrement(
    float delta,
    float rampTimeMs,
    float sampleRate
) noexcept {
    if (rampTimeMs <= 0.0f) return delta;  // Instant transition
    const float numSamples = rampTimeMs * 0.001f * sampleRate;
    return (numSamples > 0.0f) ? delta / numSamples : delta;
}

/// @brief Convert rate from units/ms to units/sample.
/// @param unitsPerMs Rate in units per millisecond
/// @param sampleRate Sample rate in Hz
/// @return Rate in units per sample
[[nodiscard]] constexpr float calculateSlewRate(
    float unitsPerMs,
    float sampleRate
) noexcept {
    // Convert ms to seconds: unitsPerMs / 1000 = unitsPerSec
    // Divide by sampleRate to get unitsPerSample
    return unitsPerMs / (sampleRate * 0.001f);
}

// =============================================================================
// OnePoleSmoother
// =============================================================================

/// @brief Exponential smoothing for audio parameters.
///
/// Uses first-order IIR filter topology for natural exponential approach.
/// Formula: output = target + coefficient * (output - target)
///
/// Use for: gain, filter cutoff, mix levels, most UI parameters.
/// Characteristic: Fast initial response, asymptotic approach to target.
class OnePoleSmoother {
public:
    /// @brief Default constructor - initializes to 0 with default smoothing time.
    OnePoleSmoother() noexcept
        : coefficient_(0.0f)
        , current_(0.0f)
        , target_(0.0f)
        , timeMs_(kDefaultSmoothingTimeMs)
        , sampleRate_(44100.0f) {
        coefficient_ = calculateOnePolCoefficient(timeMs_, sampleRate_);
    }

    /// @brief Construct with initial value.
    /// @param initialValue Starting value for both current and target
    explicit OnePoleSmoother(float initialValue) noexcept
        : coefficient_(0.0f)
        , current_(initialValue)
        , target_(initialValue)
        , timeMs_(kDefaultSmoothingTimeMs)
        , sampleRate_(44100.0f) {
        coefficient_ = calculateOnePolCoefficient(timeMs_, sampleRate_);
    }

    /// @brief Configure smoothing time and sample rate.
    /// @param smoothTimeMs Time to reach 99% of target (in milliseconds)
    /// @param sampleRate Sample rate in Hz
    void configure(float smoothTimeMs, float sampleRate) noexcept {
        timeMs_ = smoothTimeMs;
        sampleRate_ = sampleRate;
        coefficient_ = calculateOnePolCoefficient(timeMs_, sampleRate_);
    }

    /// @brief Set the target value to approach.
    /// NaN is treated as 0, infinity is clamped.
    /// Uses noinline to ensure NaN detection works under /fp:fast.
    /// @param target New target value
    ITERUM_NOINLINE void setTarget(float target) noexcept {
        if (detail::isNaN(target)) {
            target_ = 0.0f;
            current_ = 0.0f;
            return;
        }
        if (detail::isInf(target)) {
            target_ = (target > 0.0f) ? 1e10f : -1e10f;
            return;
        }
        target_ = target;
    }

    /// @brief Get the current target value.
    /// @return Current target
    [[nodiscard]] float getTarget() const noexcept {
        return target_;
    }

    /// @brief Get the current smoothed value without advancing state.
    /// @return Current smoothed value
    [[nodiscard]] float getCurrentValue() const noexcept {
        return current_;
    }

    /// @brief Process one sample and return smoothed value.
    /// @return Current smoothed value after one step
    [[nodiscard]] float process() noexcept {
        // Check if already complete
        if (std::abs(current_ - target_) < kCompletionThreshold) {
            current_ = target_;
            return current_;
        }

        // Exponential smoothing: output = target + coeff * (output - target)
        current_ = target_ + coefficient_ * (current_ - target_);

        // Flush denormals
        current_ = detail::flushDenormal(current_);

        return current_;
    }

    /// @brief Process a block of samples, writing smoothed values.
    /// @param output Buffer to write smoothed values
    /// @param numSamples Number of samples to process
    void processBlock(float* output, size_t numSamples) noexcept {
        if (isComplete()) {
            // Fill with constant value for efficiency
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = target_;
            }
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    /// @brief Check if smoothing is complete (within threshold of target).
    /// @return true if current value is within kCompletionThreshold of target
    [[nodiscard]] bool isComplete() const noexcept {
        return std::abs(current_ - target_) < kCompletionThreshold;
    }

    /// @brief Immediately set current value to target (no smoothing).
    void snapToTarget() noexcept {
        current_ = target_;
    }

    /// @brief Immediately set both current and target to a new value.
    /// @param value New value for both current and target
    void snapTo(float value) noexcept {
        if (detail::isNaN(value)) {
            value = 0.0f;
        }
        if (detail::isInf(value)) {
            value = (value > 0.0f) ? 1e10f : -1e10f;
        }
        current_ = value;
        target_ = value;
    }

    /// @brief Reset to initial state (value 0, target 0).
    void reset() noexcept {
        current_ = 0.0f;
        target_ = 0.0f;
    }

    /// @brief Update sample rate (recalculates coefficient).
    /// @param sampleRate New sample rate in Hz
    void setSampleRate(float sampleRate) noexcept {
        sampleRate_ = sampleRate;
        coefficient_ = calculateOnePolCoefficient(timeMs_, sampleRate_);
    }

private:
    float coefficient_;  ///< Smoothing coefficient (0.0-1.0)
    float current_;      ///< Current smoothed value
    float target_;       ///< Target value to approach
    float timeMs_;       ///< Configured smoothing time in ms
    float sampleRate_;   ///< Sample rate in Hz
};

// =============================================================================
// LinearRamp
// =============================================================================

/// @brief Constant-rate parameter changes for predictable transitions.
///
/// The rate of change is constant throughout the transition.
///
/// Use for: delay time (creates tape-like pitch effect), crossfades.
/// Characteristic: Predictable duration, constant rate of change.
class LinearRamp {
public:
    /// @brief Default constructor - initializes to 0 with default ramp time.
    LinearRamp() noexcept
        : increment_(0.0f)
        , current_(0.0f)
        , target_(0.0f)
        , rampTimeMs_(kDefaultSmoothingTimeMs)
        , sampleRate_(44100.0f) {
    }

    /// @brief Construct with initial value.
    /// @param initialValue Starting value for both current and target
    explicit LinearRamp(float initialValue) noexcept
        : increment_(0.0f)
        , current_(initialValue)
        , target_(initialValue)
        , rampTimeMs_(kDefaultSmoothingTimeMs)
        , sampleRate_(44100.0f) {
    }

    /// @brief Configure ramp time and sample rate.
    /// @param rampTimeMs Time to complete full 0-1 ramp (in milliseconds)
    /// @param sampleRate Sample rate in Hz
    void configure(float rampTimeMs, float sampleRate) noexcept {
        rampTimeMs_ = rampTimeMs;
        sampleRate_ = sampleRate;
        // Recalculate increment if we have an active transition
        if (current_ != target_) {
            increment_ = calculateLinearIncrement(target_ - current_, rampTimeMs_, sampleRate_);
        }
    }

    /// @brief Set the target value to ramp toward.
    /// Recalculates increment based on distance to target.
    /// Uses noinline to ensure NaN detection works under /fp:fast.
    /// @param target New target value
    ITERUM_NOINLINE void setTarget(float target) noexcept {
        if (detail::isNaN(target)) {
            target_ = 0.0f;
            current_ = 0.0f;
            increment_ = 0.0f;
            return;
        }
        if (detail::isInf(target)) {
            target = (target > 0.0f) ? 1e10f : -1e10f;
        }
        target_ = target;
        increment_ = calculateLinearIncrement(target_ - current_, rampTimeMs_, sampleRate_);
    }

    /// @brief Get the current target value.
    /// @return Current target
    [[nodiscard]] float getTarget() const noexcept {
        return target_;
    }

    /// @brief Get the current ramped value without advancing state.
    /// @return Current value
    [[nodiscard]] float getCurrentValue() const noexcept {
        return current_;
    }

    /// @brief Process one sample and return ramped value.
    /// @return Current ramped value after one step
    [[nodiscard]] float process() noexcept {
        // Check if at target
        if (current_ == target_) {
            return current_;
        }

        // Apply increment
        current_ += increment_;

        // Clamp to prevent overshoot
        if ((increment_ > 0.0f && current_ > target_) ||
            (increment_ < 0.0f && current_ < target_)) {
            current_ = target_;
        }

        // Flush denormals
        current_ = detail::flushDenormal(current_);

        return current_;
    }

    /// @brief Process a block of samples, writing ramped values.
    /// @param output Buffer to write ramped values
    /// @param numSamples Number of samples to process
    void processBlock(float* output, size_t numSamples) noexcept {
        if (isComplete()) {
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = target_;
            }
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    /// @brief Check if ramp is complete (at target).
    /// @return true if current value equals target
    [[nodiscard]] bool isComplete() const noexcept {
        return current_ == target_;
    }

    /// @brief Immediately set current value to target (no ramping).
    void snapToTarget() noexcept {
        current_ = target_;
        increment_ = 0.0f;
    }

    /// @brief Immediately set both current and target to a new value.
    /// @param value New value for both current and target
    void snapTo(float value) noexcept {
        if (detail::isNaN(value)) {
            value = 0.0f;
        }
        if (detail::isInf(value)) {
            value = (value > 0.0f) ? 1e10f : -1e10f;
        }
        current_ = value;
        target_ = value;
        increment_ = 0.0f;
    }

    /// @brief Reset to initial state (value 0, target 0).
    void reset() noexcept {
        current_ = 0.0f;
        target_ = 0.0f;
        increment_ = 0.0f;
    }

    /// @brief Update sample rate (affects ramp rate).
    /// @param sampleRate New sample rate in Hz
    void setSampleRate(float sampleRate) noexcept {
        sampleRate_ = sampleRate;
        if (current_ != target_) {
            increment_ = calculateLinearIncrement(target_ - current_, rampTimeMs_, sampleRate_);
        }
    }

private:
    float increment_;    ///< Per-sample increment
    float current_;      ///< Current ramped value
    float target_;       ///< Target value to reach
    float rampTimeMs_;   ///< Configured ramp time in ms
    float sampleRate_;   ///< Sample rate in Hz
};

// =============================================================================
// SlewLimiter
// =============================================================================

/// @brief Rate-limited parameter changes with separate rise/fall rates.
///
/// Limits the maximum rate of change per sample. Small changes happen
/// instantly if within the rate limit.
///
/// Use for: feedback amount (prevent sudden jumps), physical controller smoothing.
/// Characteristic: Maximum rate of change, different up/down speeds possible.
class SlewLimiter {
public:
    /// @brief Default constructor - initializes to 0 with default rates.
    SlewLimiter() noexcept
        : riseRate_(0.01f)
        , fallRate_(0.01f)
        , current_(0.0f)
        , target_(0.0f)
        , riseRatePerMs_(1.0f)
        , fallRatePerMs_(1.0f)
        , sampleRate_(44100.0f) {
        riseRate_ = calculateSlewRate(riseRatePerMs_, sampleRate_);
        fallRate_ = calculateSlewRate(fallRatePerMs_, sampleRate_);
    }

    /// @brief Construct with initial value.
    /// @param initialValue Starting value for both current and target
    explicit SlewLimiter(float initialValue) noexcept
        : riseRate_(0.01f)
        , fallRate_(0.01f)
        , current_(initialValue)
        , target_(initialValue)
        , riseRatePerMs_(1.0f)
        , fallRatePerMs_(1.0f)
        , sampleRate_(44100.0f) {
        riseRate_ = calculateSlewRate(riseRatePerMs_, sampleRate_);
        fallRate_ = calculateSlewRate(fallRatePerMs_, sampleRate_);
    }

    /// @brief Configure slew rates and sample rate (asymmetric).
    /// @param riseRatePerMs Maximum rise rate in units per millisecond
    /// @param fallRatePerMs Maximum fall rate in units per millisecond
    /// @param sampleRate Sample rate in Hz
    void configure(float riseRatePerMs, float fallRatePerMs, float sampleRate) noexcept {
        riseRatePerMs_ = (riseRatePerMs > 0.0f) ? riseRatePerMs : 0.0001f;
        fallRatePerMs_ = (fallRatePerMs > 0.0f) ? fallRatePerMs : 0.0001f;
        sampleRate_ = sampleRate;
        riseRate_ = calculateSlewRate(riseRatePerMs_, sampleRate_);
        fallRate_ = calculateSlewRate(fallRatePerMs_, sampleRate_);
    }

    /// @brief Configure with symmetric rise/fall rate.
    /// @param ratePerMs Maximum rate in units per millisecond (both directions)
    /// @param sampleRate Sample rate in Hz
    void configure(float ratePerMs, float sampleRate) noexcept {
        configure(ratePerMs, ratePerMs, sampleRate);
    }

    /// @brief Set the target value to approach (rate-limited).
    /// Uses noinline to ensure NaN detection works under /fp:fast.
    /// @param target New target value
    ITERUM_NOINLINE void setTarget(float target) noexcept {
        if (detail::isNaN(target)) {
            target_ = 0.0f;
            current_ = 0.0f;
            return;
        }
        if (detail::isInf(target)) {
            target = (target > 0.0f) ? 1e10f : -1e10f;
        }
        target_ = target;
    }

    /// @brief Get the current target value.
    /// @return Current target
    [[nodiscard]] float getTarget() const noexcept {
        return target_;
    }

    /// @brief Get the current limited value without advancing state.
    /// @return Current value
    [[nodiscard]] float getCurrentValue() const noexcept {
        return current_;
    }

    /// @brief Process one sample and return rate-limited value.
    /// @return Current limited value after one step
    [[nodiscard]] float process() noexcept {
        const float delta = target_ - current_;

        if (delta > riseRate_) {
            current_ += riseRate_;
        } else if (delta < -fallRate_) {
            current_ -= fallRate_;
        } else {
            current_ = target_;  // Within rate limit, snap to target
        }

        // Flush denormals
        current_ = detail::flushDenormal(current_);

        return current_;
    }

    /// @brief Process a block of samples, writing rate-limited values.
    /// @param output Buffer to write limited values
    /// @param numSamples Number of samples to process
    void processBlock(float* output, size_t numSamples) noexcept {
        if (isComplete()) {
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = target_;
            }
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    /// @brief Check if limiting is complete (at target).
    /// @return true if current value equals target
    [[nodiscard]] bool isComplete() const noexcept {
        return current_ == target_;
    }

    /// @brief Immediately set current value to target (no limiting).
    void snapToTarget() noexcept {
        current_ = target_;
    }

    /// @brief Immediately set both current and target to a new value.
    /// @param value New value for both current and target
    void snapTo(float value) noexcept {
        if (detail::isNaN(value)) {
            value = 0.0f;
        }
        if (detail::isInf(value)) {
            value = (value > 0.0f) ? 1e10f : -1e10f;
        }
        current_ = value;
        target_ = value;
    }

    /// @brief Reset to initial state (value 0, target 0).
    void reset() noexcept {
        current_ = 0.0f;
        target_ = 0.0f;
    }

    /// @brief Update sample rate (affects rates).
    /// @param sampleRate New sample rate in Hz
    void setSampleRate(float sampleRate) noexcept {
        sampleRate_ = sampleRate;
        riseRate_ = calculateSlewRate(riseRatePerMs_, sampleRate_);
        fallRate_ = calculateSlewRate(fallRatePerMs_, sampleRate_);
    }

private:
    float riseRate_;       ///< Max positive rate per sample
    float fallRate_;       ///< Max negative rate per sample
    float current_;        ///< Current limited value
    float target_;         ///< Target value to approach
    float riseRatePerMs_;  ///< User-specified rise rate
    float fallRatePerMs_;  ///< User-specified fall rate
    float sampleRate_;     ///< Sample rate in Hz
};

}  // namespace DSP
}  // namespace Iterum
