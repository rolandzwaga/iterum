// ==============================================================================
// Layer 1: DSP Primitive - Noise Oscillator
// ==============================================================================
// Lightweight noise oscillator primitive providing eight noise algorithms
// for oscillator-level composition.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0/1)
// - Principle XII: Test-First Development
//
// Spec: specs/023-noise-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/pink_noise_filter.h>

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Lightweight noise oscillator providing eight noise colors.
///
/// Layer 1 primitive for oscillator-level composition. Distinct from
/// Layer 2 NoiseGenerator which provides effects-oriented noise types.
///
/// @par Supported Noise Colors
/// - White: Flat spectrum (0 dB/octave)
/// - Pink: -3 dB/octave (equal energy per octave)
/// - Brown: -6 dB/octave (Brownian motion)
/// - Blue: +3 dB/octave (differentiated pink)
/// - Violet: +6 dB/octave (differentiated white)
/// - Grey: Inverse A-weighting (perceptually flat loudness)
/// - Velvet: Sparse random +/-1 impulse train (smooth, textural)
/// - RadioStatic: White band-limited to ~5 kHz (AM radio character)
///
/// @par Thread Safety
/// Single-threaded model. All methods called from audio thread.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe:
/// - No memory allocation
/// - No locks or blocking
/// - No exceptions (noexcept)
/// - No I/O
///
/// @par Dependencies
/// Layer 0: random.h (Xorshift32), pattern_freeze_types.h (NoiseColor)
/// Layer 1: pink_noise_filter.h (PinkNoiseFilter), biquad.h (Biquad)
///
/// @par Usage
/// @code
/// NoiseOscillator osc;
/// osc.prepare(44100.0);
/// osc.setSeed(12345);
/// osc.setColor(NoiseColor::Pink);
///
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = osc.process();
/// }
/// @endcode
class NoiseOscillator {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. Must call prepare() before processing.
    NoiseOscillator() noexcept = default;

    /// @brief Destructor.
    ~NoiseOscillator() = default;

    // Copyable and movable (value semantics, no heap allocation)
    NoiseOscillator(const NoiseOscillator&) noexcept = default;
    NoiseOscillator& operator=(const NoiseOscillator&) noexcept = default;
    NoiseOscillator(NoiseOscillator&&) noexcept = default;
    NoiseOscillator& operator=(NoiseOscillator&&) noexcept = default;

    // =========================================================================
    // Configuration (FR-003, FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Initialize oscillator for given sample rate.
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @post Oscillator is ready for processing
    void prepare(double sampleRate) noexcept;

    /// @brief Reset internal state, restart sequence from seed.
    /// @post Filter state cleared, PRNG reset to initial seed state
    /// @note Real-time safe
    void reset() noexcept;

    /// @brief Set noise color/algorithm.
    /// @param color Noise color to generate
    /// @post Filter state reset to zero, PRNG state preserved
    /// @note Real-time safe
    void setColor(NoiseColor color) noexcept;

    /// @brief Set PRNG seed for deterministic sequences.
    /// @param seed Seed value (0 uses default seed)
    /// @post PRNG reseeded, filter state preserved
    /// @note Real-time safe
    void setSeed(uint32_t seed) noexcept;

    // =========================================================================
    // Processing (FR-007, FR-008, FR-015)
    // =========================================================================

    /// @brief Generate single noise sample.
    /// @return Noise sample in range [-1.0, 1.0]
    /// @pre prepare() has been called
    /// @note Real-time safe, zero allocation
    [[nodiscard]] float process() noexcept;

    /// @brief Generate block of noise samples.
    /// @param output Output buffer to fill
    /// @param numSamples Number of samples to generate
    /// @pre prepare() has been called
    /// @pre output has capacity for numSamples
    /// @note Real-time safe, zero allocation
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Get current noise color.
    /// @return Current color setting
    [[nodiscard]] NoiseColor color() const noexcept { return color_; }

    /// @brief Get current PRNG seed.
    /// @return Current seed value
    [[nodiscard]] uint32_t seed() const noexcept { return seed_; }

    /// @brief Get sample rate.
    /// @return Sample rate in Hz
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

private:
    // =========================================================================
    // Internal State
    // =========================================================================

    // Sample rate
    double sampleRate_ = 44100.0;

    // Configuration
    NoiseColor color_ = NoiseColor::White;
    uint32_t seed_ = 1;

    // PRNG (Layer 0)
    Xorshift32 rng_{1};

    // Pink noise filter (extracted Layer 1 primitive)
    PinkNoiseFilter pinkFilter_;

    // Brown noise integrator state
    float brown_ = 0.0f;

    // Differentiator states (for blue/violet)
    float prevPink_ = 0.0f;   // Previous pink sample for blue
    float prevWhite_ = 0.0f;  // Previous white sample for violet

    // Grey noise filter state (inverse A-weighting)
    // Low-shelf at 200Hz (+15dB) and high-shelf at 6kHz (+4dB)
    Biquad greyLowShelf_;
    Biquad greyHighShelf_;

    // Velvet impulse density in impulses/second. Fixed by design: this class
    // deliberately exposes no density setter (the Layer 2 NoiseGenerator owns
    // the tunable variant via setVelvetDensity) and adding one is out of scope.
    static constexpr float kVelvetDensityHz = 2000.0f;

    // Radio static band-limiting filter (~5kHz AM radio bandwidth)
    Biquad radioLowPass_;

    // =========================================================================
    // Internal Processing
    // =========================================================================

    /// @brief Generate white noise sample from PRNG.
    [[nodiscard]] float processWhite() noexcept;

    /// @brief Generate pink noise via Paul Kellet filter.
    [[nodiscard]] float processPink(float white) noexcept;

    /// @brief Generate brown noise via leaky integrator.
    [[nodiscard]] float processBrown(float white) noexcept;

    /// @brief Generate blue noise via differentiated pink.
    [[nodiscard]] float processBlue(float pink) noexcept;

    /// @brief Generate violet noise via differentiated white.
    [[nodiscard]] float processViolet(float white) noexcept;

    /// @brief Generate grey noise via inverse A-weighting.
    [[nodiscard]] float processGrey(float white) noexcept;

    /// @brief Generate velvet noise as a sparse +/-1 impulse train.
    [[nodiscard]] float processVelvet(float white) noexcept;

    /// @brief Generate radio static via band-limited white noise.
    [[nodiscard]] float processRadioStatic(float white) noexcept;

    /// @brief Reset filter state (called on color change).
    void resetFilterState() noexcept;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void NoiseOscillator::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;

    // Configure grey noise filters (inverse A-weighting approximation)
    // Low-shelf at 200Hz with +15dB boost (compensates for A-weighting rolloff)
    // High-shelf at 6kHz with +4dB boost (compensates for HF rolloff)
    greyLowShelf_.configure(FilterType::LowShelf, 200.0f, 0.707f, 15.0f,
                            static_cast<float>(sampleRate));
    greyHighShelf_.configure(FilterType::HighShelf, 6000.0f, 0.707f, 4.0f,
                             static_cast<float>(sampleRate));

    // Configure radio static low-pass filter (~5kHz cutoff for AM radio bandwidth)
    radioLowPass_.configure(FilterType::Lowpass, 5000.0f, 0.707f, 0.0f,
                            static_cast<float>(sampleRate));
}

inline void NoiseOscillator::reset() noexcept {
    // Reset PRNG to initial seed
    rng_.seed(seed_);

    // Reset all filter state
    resetFilterState();
}

inline void NoiseOscillator::setColor(NoiseColor color) noexcept {
    color_ = color;
    // Reset filter state to ensure correct spectral characteristics immediately
    // PRNG state is preserved (per spec clarification)
    resetFilterState();
}

inline void NoiseOscillator::setSeed(uint32_t seed) noexcept {
    seed_ = seed;
    rng_.seed(seed);  // Xorshift32 handles seed=0 by using default
}

inline float NoiseOscillator::process() noexcept {
    // Generate white noise as base
    float white = processWhite();

    switch (color_) {
        case NoiseColor::White:
            return white;

        case NoiseColor::Pink:
            return processPink(white);

        case NoiseColor::Brown:
            return processBrown(white);

        case NoiseColor::Blue: {
            // Blue noise requires pink as intermediate
            float pink = processPink(white);
            return processBlue(pink);
        }

        case NoiseColor::Violet:
            return processViolet(white);

        case NoiseColor::Grey:
            return processGrey(white);

        case NoiseColor::Velvet:
            return processVelvet(white);

        case NoiseColor::RadioStatic:
            return processRadioStatic(white);

        default:
            // Unknown color - return white noise as fallback
            return white;
    }
}

inline void NoiseOscillator::processBlock(float* output, size_t numSamples) noexcept {
    // Delegate to process() to guarantee bit-identical output regardless of
    // compiler FMA contraction differences between loop and scalar contexts.
    // Branch prediction handles the per-sample switch trivially since color
    // never changes mid-block.
    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = process();
    }
}

inline float NoiseOscillator::processWhite() noexcept {
    // FR-009, FR-017: Use Xorshift32 for white noise
    // nextFloat() already returns [-1, 1]
    return rng_.nextFloat();
}

inline float NoiseOscillator::processPink(float white) noexcept {
    // FR-010: Use PinkNoiseFilter (Paul Kellet algorithm)
    return pinkFilter_.process(white);
}

inline float NoiseOscillator::processBrown(float white) noexcept {
    // FR-011: Leaky integrator with leak coefficient 0.99
    constexpr float kLeak = 0.99f;
    brown_ = kLeak * brown_ + (1.0f - kLeak) * white;

    // Scale output to usable level and clamp to [-1, 1]
    float output = brown_ * 5.0f;
    if (output > 1.0f) output = 1.0f;
    if (output < -1.0f) output = -1.0f;
    return output;
}

inline float NoiseOscillator::processBlue(float pink) noexcept {
    // FR-012: Differentiate pink noise for +3dB/octave
    // y[n] = x[n] - x[n-1]
    float blue = (pink - prevPink_) * 0.7f;  // 0.7 normalization per research.md
    prevPink_ = pink;

    // Clamp to [-1, 1]
    if (blue > 1.0f) blue = 1.0f;
    if (blue < -1.0f) blue = -1.0f;
    return blue;
}

inline float NoiseOscillator::processViolet(float white) noexcept {
    // FR-013: Differentiate white noise for +6dB/octave
    // y[n] = x[n] - x[n-1]
    float violet = (white - prevWhite_) * 0.5f;  // 0.5 normalization per research.md
    prevWhite_ = white;

    // Clamp to [-1, 1]
    if (violet > 1.0f) violet = 1.0f;
    if (violet < -1.0f) violet = -1.0f;
    return violet;
}

inline float NoiseOscillator::processGrey(float white) noexcept {
    // FR-019: Inverse A-weighting via dual biquad shelf cascade
    // Low-shelf (+15dB @ 200Hz) then high-shelf (+4dB @ 6kHz)
    float grey = greyLowShelf_.process(white);
    grey = greyHighShelf_.process(grey);

    // Clamp to [-1, 1]
    if (grey > 1.0f) grey = 1.0f;
    if (grey < -1.0f) grey = -1.0f;
    return grey;
}

inline float NoiseOscillator::processVelvet(float white) noexcept {
    // FR-098: Sparse random impulse train. Mirrors the Layer 2 NoiseGenerator
    // velvet law (processors/noise_generator.h:521-534): an impulse fires with
    // probability density/sampleRate, otherwise the output is exactly zero.
    //
    // Reuse the draw already made: processWhite() returns rng_.nextFloat(),
    // i.e. next() * kToFloat * 2 - 1, so (white + 1) * 0.5 reproduces that same
    // unipolar [0, 1] draw without consuming a second RNG value.
    const float impulseProb = kVelvetDensityHz / static_cast<float>(sampleRate_);
    const float randVal = (white + 1.0f) * 0.5f;

    if (randVal < impulseProb) {
        // Impulse with random polarity (+1 or -1); the extra draw is consumed
        // only on impulse samples, exactly as in noise_generator.h.
        return (rng_.nextUnipolar() < 0.5f) ? 1.0f : -1.0f;
    }
    return 0.0f;
}

inline float NoiseOscillator::processRadioStatic(float white) noexcept {
    // FR-098: White noise band-limited to ~5kHz (AM radio bandwidth), using the
    // same filter configuration as the Layer 2 NoiseGenerator
    // (processors/noise_generator.h:180).
    float radio = radioLowPass_.process(white);

    // Clamp to [-1, 1]
    if (radio > 1.0f) radio = 1.0f;
    if (radio < -1.0f) radio = -1.0f;
    return radio;
}

inline void NoiseOscillator::resetFilterState() noexcept {
    // Reset pink noise filter
    pinkFilter_.reset();

    // Reset brown noise integrator
    brown_ = 0.0f;

    // Reset differentiator states
    prevPink_ = 0.0f;
    prevWhite_ = 0.0f;

    // Reset grey noise filters
    greyLowShelf_.reset();
    greyHighShelf_.reset();

    // Reset radio static band-limiting filter
    radioLowPass_.reset();
}

} // namespace DSP
} // namespace Krate
