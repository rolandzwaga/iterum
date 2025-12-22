// ==============================================================================
// Biquad Filter - API Contract
// ==============================================================================
// Layer 1: DSP Primitive
// Constitution Principle VIII: Testing Discipline
// Constitution Principle X: DSP Processing Constraints (TDF2 topology)
//
// This file defines the PUBLIC API contract for the Biquad filter.
// Implementation will be in: src/dsp/primitives/biquad.h
// Tests will be in: tests/unit/primitives/biquad_test.cpp
//
// Reference: Robert Bristow-Johnson's Audio EQ Cookbook
// ==============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

namespace Iterum {
namespace DSP {

// ==============================================================================
// Filter Type Enumeration
// ==============================================================================

enum class FilterType : uint8_t {
    Lowpass,      // 12 dB/oct lowpass, -3dB at cutoff
    Highpass,     // 12 dB/oct highpass, -3dB at cutoff
    Bandpass,     // Constant 0 dB peak gain
    Notch,        // Band-reject filter
    Allpass,      // Flat magnitude, phase shift
    LowShelf,     // Boost/cut below cutoff (uses gainDb)
    HighShelf,    // Boost/cut above cutoff (uses gainDb)
    Peak          // Parametric EQ bell curve (uses gainDb)
};

// ==============================================================================
// Biquad Coefficients
// ==============================================================================

struct BiquadCoefficients {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    // Calculate coefficients for given parameters
    // @param type Filter response type
    // @param frequency Cutoff/center frequency in Hz (clamped to valid range)
    // @param Q Quality factor, 0.1 to 30 (clamped)
    // @param gainDb Gain in dB for shelf/peak types, ignored for others
    // @param sampleRate Sample rate in Hz
    static BiquadCoefficients calculate(
        FilterType type,
        float frequency,
        float Q,
        float gainDb,
        float sampleRate
    ) noexcept;

    // Constexpr version for compile-time coefficient calculation
    static constexpr BiquadCoefficients calculateConstexpr(
        FilterType type,
        float frequency,
        float Q,
        float gainDb,
        float sampleRate
    ) noexcept;

    // Check if coefficients represent a stable filter
    [[nodiscard]] bool isStable() const noexcept;

    // Check if this is effectively bypass (unity gain, no filtering)
    [[nodiscard]] bool isBypass() const noexcept;
};

// ==============================================================================
// Biquad Filter (Transposed Direct Form II)
// ==============================================================================

class Biquad {
public:
    // === Construction ===

    Biquad() noexcept = default;
    explicit Biquad(const BiquadCoefficients& coeffs) noexcept;

    // === Configuration ===

    // Set coefficients directly
    void setCoefficients(const BiquadCoefficients& coeffs) noexcept;

    // Configure for specific filter type (calculates coefficients)
    void configure(
        FilterType type,
        float frequency,
        float Q,
        float gainDb,
        float sampleRate
    ) noexcept;

    // Get current coefficients
    [[nodiscard]] const BiquadCoefficients& coefficients() const noexcept;

    // === Processing ===

    // Process single sample
    // @param input Input sample
    // @return Filtered output sample
    [[nodiscard]] float process(float input) noexcept;

    // Process buffer of samples in-place
    // @param buffer Sample buffer (modified in place)
    // @param numSamples Number of samples to process
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // === State Management ===

    // Clear filter state (call when restarting to prevent clicks)
    void reset() noexcept;

    // Get state for debugging/analysis
    [[nodiscard]] float getZ1() const noexcept;
    [[nodiscard]] float getZ2() const noexcept;

private:
    BiquadCoefficients coeffs_;
    float z1_ = 0.0f;
    float z2_ = 0.0f;
};

// ==============================================================================
// Smoothed Biquad (click-free parameter changes)
// ==============================================================================

class SmoothedBiquad {
public:
    // === Configuration ===

    // Set smoothing time for coefficient transitions
    // @param milliseconds Transition time (1-100ms typical, default 10ms)
    // @param sampleRate Current sample rate
    void setSmoothingTime(float milliseconds, float sampleRate) noexcept;

    // Set target filter parameters (will smooth towards these)
    void setTarget(
        FilterType type,
        float frequency,
        float Q,
        float gainDb,
        float sampleRate
    ) noexcept;

    // Immediately jump to target (no smoothing, may click)
    void snapToTarget() noexcept;

    // === Processing ===

    // Process single sample with coefficient interpolation
    [[nodiscard]] float process(float input) noexcept;

    // Process buffer with coefficient interpolation
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // === State ===

    // Check if smoothing is still in progress
    [[nodiscard]] bool isSmoothing() const noexcept;

    // Clear filter and smoother state
    void reset() noexcept;

private:
    // Implementation details hidden
    struct Impl;
    // Note: Actual implementation will use inline members, not pimpl
};

// ==============================================================================
// Biquad Cascade (for steeper slopes)
// ==============================================================================

template<size_t NumStages>
class BiquadCascade {
public:
    static_assert(NumStages >= 1 && NumStages <= 8,
        "BiquadCascade supports 1-8 stages (6-96 dB/oct)");

    // === Configuration ===

    // Set all stages for Butterworth response (maximally flat passband)
    // @param type Lowpass or Highpass only
    // @param frequency Cutoff frequency in Hz
    // @param sampleRate Sample rate in Hz
    void setButterworth(
        FilterType type,
        float frequency,
        float sampleRate
    ) noexcept;

    // Set all stages for Linkwitz-Riley response (flat sum at crossover)
    void setLinkwitzRiley(
        FilterType type,
        float frequency,
        float sampleRate
    ) noexcept;

    // Set individual stage coefficients
    void setStage(size_t index, const BiquadCoefficients& coeffs) noexcept;

    // === Processing ===

    // Process single sample through all stages
    [[nodiscard]] float process(float input) noexcept;

    // Process buffer through all stages
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // === State ===

    // Clear all stages
    void reset() noexcept;

    // Access individual stage
    [[nodiscard]] Biquad& stage(size_t index) noexcept;
    [[nodiscard]] const Biquad& stage(size_t index) const noexcept;

    // Number of stages
    [[nodiscard]] static constexpr size_t numStages() noexcept { return NumStages; }

    // Total filter order (2 * NumStages poles)
    [[nodiscard]] static constexpr size_t order() noexcept { return 2 * NumStages; }

    // Slope in dB/octave
    [[nodiscard]] static constexpr float slopeDbPerOctave() noexcept {
        return 6.0f * static_cast<float>(order());
    }

private:
    std::array<Biquad, NumStages> stages_;
};

// Common cascade type aliases
using Biquad12dB = Biquad;              // 12 dB/oct (2-pole)
using Biquad24dB = BiquadCascade<2>;    // 24 dB/oct (4-pole)
using Biquad36dB = BiquadCascade<3>;    // 36 dB/oct (6-pole)
using Biquad48dB = BiquadCascade<4>;    // 48 dB/oct (8-pole)

// ==============================================================================
// Utility Functions
// ==============================================================================

// Calculate Butterworth Q values for N cascaded stages
// @param stageIndex 0-based index of stage
// @param totalStages Total number of stages in cascade
// @return Q value for that stage
[[nodiscard]] constexpr float butterworthQ(
    size_t stageIndex,
    size_t totalStages
) noexcept;

// Calculate Linkwitz-Riley Q values
[[nodiscard]] constexpr float linkwitzRileyQ(
    size_t stageIndex,
    size_t totalStages
) noexcept;

// Frequency constraints
[[nodiscard]] constexpr float minFilterFrequency() noexcept { return 1.0f; }
[[nodiscard]] constexpr float maxFilterFrequency(float sampleRate) noexcept {
    return sampleRate * 0.495f;
}

// Q constraints
[[nodiscard]] constexpr float minQ() noexcept { return 0.1f; }
[[nodiscard]] constexpr float maxQ() noexcept { return 30.0f; }
[[nodiscard]] constexpr float butterworthQ() noexcept { return 0.7071067811865476f; }

} // namespace DSP
} // namespace Iterum
