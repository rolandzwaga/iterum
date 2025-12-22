// ==============================================================================
// Layer 1: DSP Primitive - Biquad Filter
// ==============================================================================
// Transposed Direct Form II biquad filter for audio signal processing.
// Supports LP, HP, BP, Notch, Allpass, LowShelf, HighShelf, Peak filter types.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle X: DSP Constraints (TDF2 topology for floating-point stability)
// - Principle XII: Test-First Development
//
// Reference: specs/004-biquad-filter/spec.md
// Formulas: Robert Bristow-Johnson's Audio EQ Cookbook
// ==============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace Iterum {
namespace DSP {

// =============================================================================
// Forward Declarations
// =============================================================================

struct BiquadCoefficients;
class Biquad;
class SmoothedBiquad;
template<size_t NumStages> class BiquadCascade;

// =============================================================================
// Constants
// =============================================================================

/// Minimum filter frequency in Hz
inline constexpr float kMinFilterFrequency = 1.0f;

/// Minimum Q value (very wide bandwidth)
inline constexpr float kMinQ = 0.1f;

/// Maximum Q value (near self-oscillation)
inline constexpr float kMaxQ = 30.0f;

/// Butterworth Q (critically damped, maximally flat passband)
inline constexpr float kButterworthQ = 0.7071067811865476f;

/// Denormal threshold for state flushing
inline constexpr float kDenormalThreshold = 1e-15f;

/// Default smoothing time in milliseconds
inline constexpr float kDefaultSmoothingMs = 10.0f;

// =============================================================================
// Filter Type Enumeration
// =============================================================================

/// @brief Supported filter response types.
enum class FilterType : uint8_t {
    Lowpass,      ///< 12 dB/oct lowpass, -3dB at cutoff
    Highpass,     ///< 12 dB/oct highpass, -3dB at cutoff
    Bandpass,     ///< Constant 0 dB peak gain
    Notch,        ///< Band-reject filter
    Allpass,      ///< Flat magnitude, phase shift
    LowShelf,     ///< Boost/cut below cutoff (uses gainDb)
    HighShelf,    ///< Boost/cut above cutoff (uses gainDb)
    Peak          ///< Parametric EQ bell curve (uses gainDb)
};

// =============================================================================
// Math Helpers (Internal)
// =============================================================================

namespace detail {

/// Pi constant for internal use
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;

/// Maximum filter frequency as ratio of sample rate
inline constexpr float kMaxFrequencyRatio = 0.495f;

/// Constexpr sine using Taylor series (for MSVC compatibility)
/// Accurate within ~1e-5 for typical audio frequency ranges
constexpr float constexprSin(float x) noexcept {
    // Normalize to [-pi, pi]
    while (x > kPi) x -= kTwoPi;
    while (x < -kPi) x += kTwoPi;

    // Taylor series: sin(x) = x - x^3/3! + x^5/5! - x^7/7! + x^9/9!
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;
    const float x7 = x5 * x2;
    const float x9 = x7 * x2;
    const float x11 = x9 * x2;

    return x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f +
           x9 / 362880.0f - x11 / 39916800.0f;
}

/// Constexpr cosine using Taylor series
constexpr float constexprCos(float x) noexcept {
    // Normalize to [-pi, pi]
    while (x > kPi) x -= kTwoPi;
    while (x < -kPi) x += kTwoPi;

    // Taylor series: cos(x) = 1 - x^2/2! + x^4/4! - x^6/6! + x^8/8!
    const float x2 = x * x;
    const float x4 = x2 * x2;
    const float x6 = x4 * x2;
    const float x8 = x6 * x2;
    const float x10 = x8 * x2;

    return 1.0f - x2 / 2.0f + x4 / 24.0f - x6 / 720.0f +
           x8 / 40320.0f - x10 / 3628800.0f;
}

/// Constexpr power of 10 using Taylor series for exp
/// 10^x = e^(x * ln(10))
constexpr float constexprPow10(float x) noexcept {
    const float ln10 = 2.302585093f;
    const float y = x * ln10;

    // Taylor series for exp(y)
    const float y2 = y * y;
    const float y3 = y2 * y;
    const float y4 = y3 * y;
    const float y5 = y4 * y;
    const float y6 = y5 * y;
    const float y7 = y6 * y;
    const float y8 = y7 * y;

    return 1.0f + y + y2 / 2.0f + y3 / 6.0f + y4 / 24.0f +
           y5 / 120.0f + y6 / 720.0f + y7 / 5040.0f + y8 / 40320.0f;
}

/// Constexpr square root using Newton-Raphson iteration
constexpr float constexprSqrt(float x) noexcept {
    if (x <= 0.0f) return 0.0f;

    float guess = x * 0.5f;
    for (int i = 0; i < 10; ++i) {
        guess = 0.5f * (guess + x / guess);
    }
    return guess;
}

/// Check if a float is finite using bit-level check
/// Works with -ffast-math enabled
inline bool isFiniteBits(float x) noexcept {
    const auto bits = std::bit_cast<uint32_t>(x);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// Check if a float is NaN using bit-level check
inline bool isNaNBits(float x) noexcept {
    const auto bits = std::bit_cast<uint32_t>(x);
    return ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
}

/// Flush denormal values to zero
inline constexpr float flushDenormal(float x) noexcept {
    return (x > -kDenormalThreshold && x < kDenormalThreshold) ? 0.0f : x;
}

/// Clamp frequency to valid range
inline constexpr float clampFrequency(float freq, float sampleRate) noexcept {
    // Handle invalid sample rate (would cause maxFreq < minFreq)
    if (sampleRate <= 0.0f) {
        return kMinFilterFrequency;  // Return safe default
    }
    const float maxFreq = sampleRate * kMaxFrequencyRatio;
    // Ensure valid bounds for clamp (minFreq must be <= maxFreq)
    if (maxFreq < kMinFilterFrequency) {
        return maxFreq;  // At very low sample rates, use max available
    }
    return std::clamp(freq, kMinFilterFrequency, maxFreq);
}

/// Clamp Q to valid range
inline constexpr float clampQ(float q) noexcept {
    return std::clamp(q, kMinQ, kMaxQ);
}

} // namespace detail

// =============================================================================
// Utility Functions
// =============================================================================

/// Calculate maximum filter frequency for given sample rate
[[nodiscard]] inline constexpr float maxFilterFrequency(float sampleRate) noexcept {
    return sampleRate * detail::kMaxFrequencyRatio;
}

/// Get minimum allowed filter frequency
[[nodiscard]] inline constexpr float minFilterFrequency() noexcept {
    return kMinFilterFrequency;
}

/// Get minimum allowed Q value
[[nodiscard]] inline constexpr float minQ() noexcept {
    return kMinQ;
}

/// Get maximum allowed Q value
[[nodiscard]] inline constexpr float maxQ() noexcept {
    return kMaxQ;
}

/// Get Butterworth Q value
[[nodiscard]] inline constexpr float butterworthQ() noexcept {
    return kButterworthQ;
}

/// Calculate Butterworth Q value for cascaded stages
/// @param stageIndex 0-based index of current stage
/// @param totalStages Total number of stages in cascade
/// @return Q value for maximally flat passband
[[nodiscard]] constexpr float butterworthQ(size_t stageIndex, size_t totalStages) noexcept {
    if (totalStages == 0) return kButterworthQ;
    if (totalStages == 1) return kButterworthQ;

    // Q[k] = 1 / (2 * cos(pi * (2*k + 1) / (4*N))) for N stages
    const float n = static_cast<float>(totalStages);
    const float k = static_cast<float>(stageIndex);
    const float angle = detail::kPi * (2.0f * k + 1.0f) / (4.0f * n);
    const float cosVal = detail::constexprCos(angle);

    return 1.0f / (2.0f * cosVal);
}

/// Calculate Linkwitz-Riley Q value for cascaded stages
/// @param stageIndex 0-based index of current stage
/// @param totalStages Total number of stages in cascade
/// @return Q value for flat sum at crossover
[[nodiscard]] constexpr float linkwitzRileyQ(size_t stageIndex, size_t totalStages) noexcept {
    // Linkwitz-Riley is a squared Butterworth response:
    // - LR2 (1 stage): Q = 0.5 (critically damped)
    // - LR4 (2 stages): Two cascaded Butterworth (Q = 0.7071 each)
    // - LR8 (4 stages): Four cascaded Butterworth with appropriate Q values
    if (totalStages == 1) {
        (void)stageIndex;
        return 0.5f;  // LR2: critically damped
    }
    // For LR4 and higher, use Butterworth Q values
    return butterworthQ(stageIndex, totalStages);
}

// =============================================================================
// Biquad Coefficients
// =============================================================================

/// @brief Normalized biquad filter coefficients (a0 = 1 implied).
struct BiquadCoefficients {
    float b0 = 1.0f;  ///< Feedforward coefficient 0
    float b1 = 0.0f;  ///< Feedforward coefficient 1
    float b2 = 0.0f;  ///< Feedforward coefficient 2
    float a1 = 0.0f;  ///< Feedback coefficient 1 (a0 = 1 implied)
    float a2 = 0.0f;  ///< Feedback coefficient 2

    /// Calculate coefficients for given parameters
    /// @param type Filter response type
    /// @param frequency Cutoff/center frequency in Hz
    /// @param Q Quality factor (0.1 to 30)
    /// @param gainDb Gain in dB for shelf/peak types (ignored for others)
    /// @param sampleRate Sample rate in Hz
    [[nodiscard]] static BiquadCoefficients calculate(
        FilterType type,
        float frequency,
        float Q,
        float gainDb,
        float sampleRate
    ) noexcept;

    /// Constexpr version for compile-time coefficient calculation
    [[nodiscard]] static constexpr BiquadCoefficients calculateConstexpr(
        FilterType type,
        float frequency,
        float Q,
        float gainDb,
        float sampleRate
    ) noexcept;

    /// Check if coefficients represent a stable filter
    /// @return true if filter is stable (|a2| < 1)
    [[nodiscard]] bool isStable() const noexcept {
        // Jury stability criterion for second-order IIR filter:
        // 1. |a2| < 1
        // 2. |a1| < 1 + a2
        // Use small epsilon for floating-point tolerance at stability boundary
        constexpr float epsilon = 1e-6f;
        return std::abs(a2) < 1.0f + epsilon &&
               std::abs(a1) < 1.0f + a2 + epsilon;
    }

    /// Check if this is effectively bypass (unity gain, no filtering)
    [[nodiscard]] bool isBypass() const noexcept {
        constexpr float epsilon = 1e-6f;
        return std::abs(b0 - 1.0f) < epsilon &&
               std::abs(b1) < epsilon &&
               std::abs(b2) < epsilon &&
               std::abs(a1) < epsilon &&
               std::abs(a2) < epsilon;
    }
};

// =============================================================================
// Biquad Filter Class
// =============================================================================

/// @brief Transposed Direct Form II biquad filter.
///
/// Processes audio using the TDF2 difference equations:
/// @code
/// y[n] = b0*x[n] + z1[n-1]
/// z1[n] = b1*x[n] - a1*y[n] + z2[n-1]
/// z2[n] = b2*x[n] - a2*y[n]
/// @endcode
class Biquad {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    Biquad() noexcept = default;

    /// Construct with initial coefficients
    explicit Biquad(const BiquadCoefficients& coeffs) noexcept
        : coeffs_(coeffs) {}

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Set coefficients directly
    void setCoefficients(const BiquadCoefficients& coeffs) noexcept {
        coeffs_ = coeffs;
    }

    /// Configure for specific filter type (calculates coefficients)
    void configure(
        FilterType type,
        float frequency,
        float Q,
        float gainDb,
        float sampleRate
    ) noexcept {
        coeffs_ = BiquadCoefficients::calculate(type, frequency, Q, gainDb, sampleRate);
    }

    /// Get current coefficients
    [[nodiscard]] const BiquadCoefficients& coefficients() const noexcept {
        return coeffs_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process single sample using TDF2
    /// @param input Input sample
    /// @return Filtered output sample
    [[nodiscard]] float process(float input) noexcept {
        // Check for invalid input (NaN/Inf)
        if (!detail::isFiniteBits(input)) {
            reset();
            return 0.0f;
        }

        // TDF2 difference equations
        const float output = coeffs_.b0 * input + z1_;
        z1_ = coeffs_.b1 * input - coeffs_.a1 * output + z2_;
        z2_ = coeffs_.b2 * input - coeffs_.a2 * output;

        // Flush denormals to prevent CPU spikes
        z1_ = detail::flushDenormal(z1_);
        z2_ = detail::flushDenormal(z2_);

        return output;
    }

    /// Process buffer of samples in-place
    /// @param buffer Sample buffer (modified in place)
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // State Management
    // =========================================================================

    /// Clear filter state (call when restarting to prevent clicks)
    void reset() noexcept {
        z1_ = 0.0f;
        z2_ = 0.0f;
    }

    /// Get first state variable (for debugging/analysis)
    [[nodiscard]] float getZ1() const noexcept { return z1_; }

    /// Get second state variable (for debugging/analysis)
    [[nodiscard]] float getZ2() const noexcept { return z2_; }

private:
    BiquadCoefficients coeffs_;
    float z1_ = 0.0f;
    float z2_ = 0.0f;
};

// =============================================================================
// Biquad Cascade (for steeper slopes)
// =============================================================================

/// @brief Cascade of biquad stages for steeper filter slopes.
///
/// Each stage adds 12 dB/octave to the slope:
/// - 1 stage = 12 dB/oct (2-pole)
/// - 2 stages = 24 dB/oct (4-pole)
/// - 3 stages = 36 dB/oct (6-pole)
/// - 4 stages = 48 dB/oct (8-pole)
template<size_t NumStages>
class BiquadCascade {
public:
    static_assert(NumStages >= 1 && NumStages <= 8,
        "BiquadCascade supports 1-8 stages (12-96 dB/oct)");

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Set all stages for Butterworth response (maximally flat passband)
    /// @param type Lowpass or Highpass only
    /// @param frequency Cutoff frequency in Hz
    /// @param sampleRate Sample rate in Hz
    void setButterworth(
        FilterType type,
        float frequency,
        float sampleRate
    ) noexcept {
        for (size_t i = 0; i < NumStages; ++i) {
            const float q = butterworthQ(i, NumStages);
            stages_[i].configure(type, frequency, q, 0.0f, sampleRate);
        }
    }

    /// Set all stages for Linkwitz-Riley response (flat sum at crossover)
    void setLinkwitzRiley(
        FilterType type,
        float frequency,
        float sampleRate
    ) noexcept {
        for (size_t i = 0; i < NumStages; ++i) {
            const float q = linkwitzRileyQ(i, NumStages);
            stages_[i].configure(type, frequency, q, 0.0f, sampleRate);
        }
    }

    /// Set individual stage coefficients
    void setStage(size_t index, const BiquadCoefficients& coeffs) noexcept {
        if (index < NumStages) {
            stages_[index].setCoefficients(coeffs);
        }
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process single sample through all stages
    [[nodiscard]] float process(float input) noexcept {
        float x = input;
        for (auto& stage : stages_) {
            x = stage.process(x);
        }
        return x;
    }

    /// Process buffer through all stages
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (auto& stage : stages_) {
            stage.processBlock(buffer, numSamples);
        }
    }

    // =========================================================================
    // State Management
    // =========================================================================

    /// Clear all stages
    void reset() noexcept {
        for (auto& stage : stages_) {
            stage.reset();
        }
    }

    /// Access individual stage
    [[nodiscard]] Biquad& stage(size_t index) noexcept {
        return stages_[std::min(index, NumStages - 1)];
    }

    /// Access individual stage (const)
    [[nodiscard]] const Biquad& stage(size_t index) const noexcept {
        return stages_[std::min(index, NumStages - 1)];
    }

    /// Number of stages in cascade
    [[nodiscard]] static constexpr size_t numStages() noexcept { return NumStages; }

    /// Total filter order (2 * NumStages poles)
    [[nodiscard]] static constexpr size_t order() noexcept { return 2 * NumStages; }

    /// Slope in dB/octave
    [[nodiscard]] static constexpr float slopeDbPerOctave() noexcept {
        return 6.0f * static_cast<float>(order());
    }

private:
    std::array<Biquad, NumStages> stages_;
};

// =============================================================================
// Common Cascade Type Aliases
// =============================================================================

using Biquad12dB = Biquad;               ///< 12 dB/oct (2-pole)
using Biquad24dB = BiquadCascade<2>;     ///< 24 dB/oct (4-pole)
using Biquad36dB = BiquadCascade<3>;     ///< 36 dB/oct (6-pole)
using Biquad48dB = BiquadCascade<4>;     ///< 48 dB/oct (8-pole)

// =============================================================================
// SmoothedBiquad Class
// =============================================================================

/// @brief Biquad filter with smoothed coefficient updates for click-free modulation.
class SmoothedBiquad {
public:
    // =========================================================================
    // Configuration
    // =========================================================================

    /// Set smoothing time for coefficient transitions
    /// @param milliseconds Transition time (1-100ms typical, default 10ms)
    /// @param sampleRate Current sample rate
    void setSmoothingTime(float milliseconds, float sampleRate) noexcept {
        const float timeSeconds = milliseconds * 0.001f;
        smootherB0_.setTime(timeSeconds, sampleRate);
        smootherB1_.setTime(timeSeconds, sampleRate);
        smootherB2_.setTime(timeSeconds, sampleRate);
        smootherA1_.setTime(timeSeconds, sampleRate);
        smootherA2_.setTime(timeSeconds, sampleRate);
        sampleRate_ = sampleRate;
    }

    /// Set target filter parameters (will smooth towards these)
    void setTarget(
        FilterType type,
        float frequency,
        float Q,
        float gainDb,
        float sampleRate
    ) noexcept {
        target_ = BiquadCoefficients::calculate(type, frequency, Q, gainDb, sampleRate);
        sampleRate_ = sampleRate;
    }

    /// Immediately jump to target (no smoothing, may click)
    void snapToTarget() noexcept {
        smootherB0_.reset(target_.b0);
        smootherB1_.reset(target_.b1);
        smootherB2_.reset(target_.b2);
        smootherA1_.reset(target_.a1);
        smootherA2_.reset(target_.a2);
        filter_.setCoefficients(target_);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process single sample with coefficient interpolation
    [[nodiscard]] float process(float input) noexcept {
        // Update filter with smoothed coefficients
        BiquadCoefficients smoothed;
        smoothed.b0 = smootherB0_.process(target_.b0);
        smoothed.b1 = smootherB1_.process(target_.b1);
        smoothed.b2 = smootherB2_.process(target_.b2);
        smoothed.a1 = smootherA1_.process(target_.a1);
        smoothed.a2 = smootherA2_.process(target_.a2);

        filter_.setCoefficients(smoothed);
        return filter_.process(input);
    }

    /// Process buffer with coefficient interpolation
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    /// Check if smoothing is still in progress
    [[nodiscard]] bool isSmoothing() const noexcept {
        // Use 1e-5 threshold - tighter values cause excessive smoothing time
        // For audio, coefficient precision beyond 1e-5 is inaudible
        constexpr float epsilon = 1e-5f;
        return std::abs(smootherB0_.getValue() - target_.b0) > epsilon ||
               std::abs(smootherB1_.getValue() - target_.b1) > epsilon ||
               std::abs(smootherB2_.getValue() - target_.b2) > epsilon ||
               std::abs(smootherA1_.getValue() - target_.a1) > epsilon ||
               std::abs(smootherA2_.getValue() - target_.a2) > epsilon;
    }

    /// Clear filter and smoother state
    void reset() noexcept {
        filter_.reset();
        snapToTarget();
    }

private:
    Biquad filter_;
    BiquadCoefficients target_;  // Default: b0=1, others=0 (bypass)

    // One-pole smoothers for each coefficient (inline implementation)
    struct Smoother {
        float coeff_ = 0.0f;
        float state_ = 0.0f;

        void setTime(float timeSeconds, float sampleRate) noexcept {
            if (timeSeconds <= 0.0f) {
                coeff_ = 0.0f;
            } else {
                coeff_ = std::exp(-1.0f / (timeSeconds * sampleRate));
            }
        }

        [[nodiscard]] float process(float target) noexcept {
            state_ = target + coeff_ * (state_ - target);
            return state_;
        }

        void reset(float value) noexcept {
            state_ = value;
        }

        [[nodiscard]] float getValue() const noexcept {
            return state_;
        }
    };

    Smoother smootherB0_{0.0f, 1.0f};   // Initialize to match target_.b0
    Smoother smootherB1_{0.0f, 0.0f};
    Smoother smootherB2_{0.0f, 0.0f};
    Smoother smootherA1_{0.0f, 0.0f};
    Smoother smootherA2_{0.0f, 0.0f};

    float sampleRate_ = 44100.0f;
};

// =============================================================================
// Coefficient Calculation Implementation
// =============================================================================

inline BiquadCoefficients BiquadCoefficients::calculate(
    FilterType type,
    float frequency,
    float Q,
    float gainDb,
    float sampleRate
) noexcept {
    // Return bypass for invalid sample rate
    if (sampleRate <= 0.0f) {
        return BiquadCoefficients{};  // Default bypass coefficients
    }

    // Clamp parameters to valid ranges
    frequency = detail::clampFrequency(frequency, sampleRate);
    Q = detail::clampQ(Q);

    // Common intermediate values
    const float omega = detail::kTwoPi * frequency / sampleRate;
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * Q);

    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    switch (type) {
        case FilterType::Lowpass: {
            b0 = (1.0f - cosOmega) / 2.0f;
            b1 = 1.0f - cosOmega;
            b2 = (1.0f - cosOmega) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case FilterType::Highpass: {
            b0 = (1.0f + cosOmega) / 2.0f;
            b1 = -(1.0f + cosOmega);
            b2 = (1.0f + cosOmega) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case FilterType::Bandpass: {
            b0 = alpha;
            b1 = 0.0f;
            b2 = -alpha;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case FilterType::Notch: {
            b0 = 1.0f;
            b1 = -2.0f * cosOmega;
            b2 = 1.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case FilterType::Allpass: {
            b0 = 1.0f - alpha;
            b1 = -2.0f * cosOmega;
            b2 = 1.0f + alpha;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case FilterType::LowShelf: {
            const float A = std::sqrt(std::pow(10.0f, gainDb / 20.0f));
            const float beta = std::sqrt(A) / Q;

            b0 = A * ((A + 1.0f) - (A - 1.0f) * cosOmega + beta * sinOmega);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosOmega);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cosOmega - beta * sinOmega);
            a0 = (A + 1.0f) + (A - 1.0f) * cosOmega + beta * sinOmega;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosOmega);
            a2 = (A + 1.0f) + (A - 1.0f) * cosOmega - beta * sinOmega;
            break;
        }
        case FilterType::HighShelf: {
            const float A = std::sqrt(std::pow(10.0f, gainDb / 20.0f));
            const float beta = std::sqrt(A) / Q;

            b0 = A * ((A + 1.0f) + (A - 1.0f) * cosOmega + beta * sinOmega);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosOmega);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * cosOmega - beta * sinOmega);
            a0 = (A + 1.0f) - (A - 1.0f) * cosOmega + beta * sinOmega;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosOmega);
            a2 = (A + 1.0f) - (A - 1.0f) * cosOmega - beta * sinOmega;
            break;
        }
        case FilterType::Peak: {
            const float A = std::sqrt(std::pow(10.0f, gainDb / 20.0f));

            b0 = 1.0f + alpha * A;
            b1 = -2.0f * cosOmega;
            b2 = 1.0f - alpha * A;
            a0 = 1.0f + alpha / A;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha / A;
            break;
        }
    }

    // Normalize coefficients (a0 = 1)
    BiquadCoefficients coeffs;
    const float invA0 = 1.0f / a0;
    coeffs.b0 = b0 * invA0;
    coeffs.b1 = b1 * invA0;
    coeffs.b2 = b2 * invA0;
    coeffs.a1 = a1 * invA0;
    coeffs.a2 = a2 * invA0;

    return coeffs;
}

constexpr BiquadCoefficients BiquadCoefficients::calculateConstexpr(
    FilterType type,
    float frequency,
    float Q,
    float gainDb,
    float sampleRate
) noexcept {
    // Return bypass for invalid sample rate
    if (sampleRate <= 0.0f) {
        return BiquadCoefficients{};  // Default bypass coefficients
    }

    // Clamp parameters to valid ranges
    const float maxFreq = sampleRate * detail::kMaxFrequencyRatio;
    // Ensure valid clamping bounds (maxFreq must be >= minFreq)
    if (maxFreq < kMinFilterFrequency) {
        frequency = maxFreq;
    } else {
        frequency = (frequency < kMinFilterFrequency) ? kMinFilterFrequency :
                    (frequency > maxFreq) ? maxFreq : frequency;
    }
    Q = (Q < kMinQ) ? kMinQ : (Q > kMaxQ) ? kMaxQ : Q;

    // Common intermediate values using constexpr math
    const float omega = detail::kTwoPi * frequency / sampleRate;
    const float sinOmega = detail::constexprSin(omega);
    const float cosOmega = detail::constexprCos(omega);
    const float alpha = sinOmega / (2.0f * Q);

    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    switch (type) {
        case FilterType::Lowpass: {
            b0 = (1.0f - cosOmega) / 2.0f;
            b1 = 1.0f - cosOmega;
            b2 = (1.0f - cosOmega) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case FilterType::Highpass: {
            b0 = (1.0f + cosOmega) / 2.0f;
            b1 = -(1.0f + cosOmega);
            b2 = (1.0f + cosOmega) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case FilterType::Bandpass: {
            b0 = alpha;
            b1 = 0.0f;
            b2 = -alpha;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case FilterType::Notch: {
            b0 = 1.0f;
            b1 = -2.0f * cosOmega;
            b2 = 1.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case FilterType::Allpass: {
            b0 = 1.0f - alpha;
            b1 = -2.0f * cosOmega;
            b2 = 1.0f + alpha;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case FilterType::LowShelf: {
            const float A = detail::constexprSqrt(detail::constexprPow10(gainDb / 20.0f));
            const float beta = detail::constexprSqrt(A) / Q;

            b0 = A * ((A + 1.0f) - (A - 1.0f) * cosOmega + beta * sinOmega);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosOmega);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cosOmega - beta * sinOmega);
            a0 = (A + 1.0f) + (A - 1.0f) * cosOmega + beta * sinOmega;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosOmega);
            a2 = (A + 1.0f) + (A - 1.0f) * cosOmega - beta * sinOmega;
            break;
        }
        case FilterType::HighShelf: {
            const float A = detail::constexprSqrt(detail::constexprPow10(gainDb / 20.0f));
            const float beta = detail::constexprSqrt(A) / Q;

            b0 = A * ((A + 1.0f) + (A - 1.0f) * cosOmega + beta * sinOmega);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosOmega);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * cosOmega - beta * sinOmega);
            a0 = (A + 1.0f) - (A - 1.0f) * cosOmega + beta * sinOmega;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosOmega);
            a2 = (A + 1.0f) - (A - 1.0f) * cosOmega - beta * sinOmega;
            break;
        }
        case FilterType::Peak: {
            const float A = detail::constexprSqrt(detail::constexprPow10(gainDb / 20.0f));

            b0 = 1.0f + alpha * A;
            b1 = -2.0f * cosOmega;
            b2 = 1.0f - alpha * A;
            a0 = 1.0f + alpha / A;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha / A;
            break;
        }
    }

    // Normalize coefficients (a0 = 1)
    BiquadCoefficients coeffs;
    const float invA0 = 1.0f / a0;
    coeffs.b0 = b0 * invA0;
    coeffs.b1 = b1 * invA0;
    coeffs.b2 = b2 * invA0;
    coeffs.a1 = a1 * invA0;
    coeffs.a2 = a2 * invA0;

    return coeffs;
}

} // namespace DSP
} // namespace Iterum
