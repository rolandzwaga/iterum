// Layer 2: DSP Processor - Grain Scheduler
// Part of Granular Delay feature (spec 034)
#pragma once

#include "dsp/core/random.h"

#include <algorithm>
#include <cstdint>

namespace Iterum::DSP {

/// Scheduling mode for grain triggering
enum class SchedulingMode : uint8_t {
    Asynchronous,  ///< Stochastic timing based on density (default)
    Synchronous    ///< Regular intervals for pitched output
};

/// Controls when grains are triggered based on density settings.
/// Supports both synchronous (regular) and asynchronous (stochastic) modes.
class GrainScheduler {
public:
    /// Prepare scheduler for processing
    /// @param sampleRate Current sample rate
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        reset();
    }

    /// Reset scheduler state
    void reset() noexcept {
        samplesUntilNextGrain_ = 0.0f;
        calculateInteronset();
    }

    /// Set grain density (grains per second)
    /// @param grainsPerSecond Trigger rate (1-100 typical range)
    void setDensity(float grainsPerSecond) noexcept {
        density_ = std::max(0.1f, grainsPerSecond);
        calculateInteronset();
    }

    /// Get current density
    [[nodiscard]] float getDensity() const noexcept { return density_; }

    /// Set scheduling mode
    /// @param mode Synchronous or Asynchronous
    void setMode(SchedulingMode mode) noexcept { mode_ = mode; }

    /// Get current scheduling mode
    [[nodiscard]] SchedulingMode getMode() const noexcept { return mode_; }

    /// Set jitter amount (0-1)
    /// Controls timing randomness: 0 = regular intervals, 1 = maximum randomness (±50%)
    /// @param amount Jitter amount (0.0 to 1.0)
    void setJitter(float amount) noexcept {
        jitter_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Get current jitter amount
    [[nodiscard]] float getJitter() const noexcept { return jitter_; }

    /// Process one sample
    /// @return true if a new grain should be triggered this sample
    [[nodiscard]] bool process() noexcept {
        samplesUntilNextGrain_ -= 1.0f;

        if (samplesUntilNextGrain_ <= 0.0f) {
            // Time for a new grain
            if (mode_ == SchedulingMode::Asynchronous && jitter_ > 0.0f) {
                // Add user-controllable jitter for stochastic timing
                // jitter_ = 0: no variation (like sync mode)
                // jitter_ = 1: ±50% variation (maximum randomness)
                const float randomOffset = rng_.nextFloat();  // -1 to +1
                const float jitterRange = jitter_ * 0.5f;     // Max ±50%
                samplesUntilNextGrain_ = interonsetSamples_ * (1.0f + randomOffset * jitterRange);
            } else {
                // Regular intervals (sync mode or jitter = 0)
                samplesUntilNextGrain_ = interonsetSamples_;
            }
            return true;
        }

        return false;
    }

    /// Seed RNG for reproducible behavior (useful for testing)
    /// @param seedValue Seed for random number generator
    void seed(uint32_t seedValue) noexcept { rng_ = Xorshift32(seedValue); }

private:
    void calculateInteronset() noexcept {
        // Interonset interval = samples per grain = sampleRate / density
        interonsetSamples_ = static_cast<float>(sampleRate_) / density_;
    }

    float samplesUntilNextGrain_ = 0.0f;
    float interonsetSamples_ = 4410.0f;  // Default ~10 grains/sec at 44.1kHz
    float density_ = 10.0f;              // grains per second
    float jitter_ = 0.5f;                // Default jitter amount (0-1), 0.5 = ±25%
    SchedulingMode mode_ = SchedulingMode::Asynchronous;
    Xorshift32 rng_{12345};
    double sampleRate_ = 44100.0;
};

}  // namespace Iterum::DSP
