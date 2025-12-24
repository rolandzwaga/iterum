// ==============================================================================
// Layer 2: DSP Processor - DiffusionNetwork
// ==============================================================================
// 8-stage Schroeder allpass diffusion network for creating smeared,
// reverb-like textures.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process())
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 2 (composes Layer 1 primitives only)
// - Principle X: DSP Constraints (allpass preserves spectrum)
// - Principle XII: Test-First Development
//
// Reference: specs/015-diffusion-network/spec.md
// ==============================================================================

#pragma once

#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/smoother.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Iterum {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Number of diffusion stages
inline constexpr size_t kNumDiffusionStages = 8;

/// Allpass coefficient (golden ratio inverse ≈ 0.618)
inline constexpr float kAllpassCoeff = 0.618033988749895f;

/// Base delay time at size=100% in milliseconds
inline constexpr float kBaseDelayMs = 3.2f;

/// Maximum modulation depth in milliseconds
inline constexpr float kMaxModDepthMs = 2.0f;

/// Default parameter smoothing time in milliseconds
inline constexpr float kDiffusionSmoothingMs = 10.0f;

/// Irrational delay ratios per stage (based on square roots, similar to Lexicon)
inline constexpr std::array<float, kNumDiffusionStages> kDelayRatiosL = {
    1.000f, 1.127f, 1.414f, 1.732f, 2.236f, 2.828f, 3.317f, 4.123f
};

/// Stereo decorrelation multiplier for right channel
inline constexpr float kStereoOffset = 1.127f;

/// Pi constant
inline constexpr float kPi = 3.14159265358979323846f;

/// Two Pi constant
inline constexpr float kTwoPi = 2.0f * kPi;

// =============================================================================
// AllpassStage
// =============================================================================

/// @brief Single Schroeder allpass filter stage for diffusion network.
///
/// Implements the Schroeder allpass using the single-delay-line formulation:
///   v[n] = x[n] + g * v[n-D]
///   y[n] = -g * v[n] + v[n-D]
///
/// This is algebraically equivalent to y[n] = -g*x[n] + x[n-D] + g*y[n-D]
/// but uses only ONE delay line, which improves energy preservation with
/// fractional delays (only one interpolation operation per sample).
///
/// Where:
/// - g = allpass coefficient (0.618, golden ratio inverse)
/// - D = delay time in samples
/// - x[n] = input
/// - y[n] = output
///
/// Uses DelayLine with allpass interpolation for energy-preserving fractional delays.
/// Note: Allpass interpolation is preferred over linear interpolation because it has
/// unity magnitude response at all frequencies, preserving the allpass property.
/// Linear interpolation acts as a lowpass filter, causing energy loss.
class AllpassStage {
public:
    /// @brief Default constructor.
    AllpassStage() noexcept = default;

    /// @brief Prepare the stage for processing.
    /// @param sampleRate Sample rate in Hz
    /// @param maxDelaySeconds Maximum delay time in seconds
    void prepare(float sampleRate, float maxDelaySeconds) noexcept {
        sampleRate_ = sampleRate;
        maxDelaySamples_ = sampleRate * maxDelaySeconds;
        delayLine_.prepare(static_cast<double>(sampleRate), maxDelaySeconds);
        reset();
    }

    /// @brief Reset internal state to silence.
    void reset() noexcept {
        delayLine_.reset();
    }

    /// @brief Process a single sample through the allpass filter.
    /// @param input Input sample
    /// @param delaySamples Delay time in samples (fractional allowed)
    /// @return Filtered output sample
    [[nodiscard]] float process(float input, float delaySamples) noexcept {
        // Clamp delay to valid range (minimum 1 sample for proper allpass behavior)
        const float clampedDelay = std::clamp(delaySamples, 1.0f, maxDelaySamples_);

        // Read delayed value from single delay line (v[n-D])
        // Use allpass interpolation for fractional delays - this preserves energy
        // (unity magnitude at all frequencies) unlike linear interpolation which
        // acts as a lowpass filter and causes high-frequency attenuation.
        //
        // Note: We use (clampedDelay - 1) because DelayLine::read() assumes a
        // write-before-read pattern, but we use read-before-write. The -1 adjustment
        // accounts for this: read(D-1) after the previous write gives us v[n-D].
        const float delayedV = delayLine_.readAllpass(clampedDelay - 1.0f);

        // Single-delay-line allpass formulation:
        // v[n] = x[n] + g * v[n-D]
        // y[n] = -g * v[n] + v[n-D]
        const float v = input + kAllpassCoeff * delayedV;
        const float output = -kAllpassCoeff * v + delayedV;

        // Write v to delay line
        delayLine_.write(v);

        return output;
    }

private:
    DelayLine delayLine_;
    float sampleRate_ = 44100.0f;
    float maxDelaySamples_ = 0.0f;
};

// =============================================================================
// DiffusionNetwork
// =============================================================================

/// @brief 8-stage Schroeder allpass diffusion network.
///
/// Creates smeared, reverb-like textures by cascading allpass filters with
/// mutually irrational delay time ratios. Preserves frequency spectrum while
/// temporally diffusing the signal.
///
/// Parameters:
/// - Size: Scales all delay times [0%, 100%]
/// - Density: Number of active stages [0%, 100%] → 0-8 stages
/// - Width: Stereo decorrelation [0%, 100%]
/// - ModDepth: LFO modulation depth [0%, 100%]
/// - ModRate: LFO rate [0.1Hz, 5Hz]
///
/// Thread Safety:
/// - Setters can be called from any thread
/// - process() must be called from audio thread only
/// - All methods are noexcept and allocation-free after prepare()
class DiffusionNetwork {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinSize = 0.0f;
    static constexpr float kMaxSize = 100.0f;
    static constexpr float kDefaultSize = 50.0f;

    static constexpr float kMinDensity = 0.0f;
    static constexpr float kMaxDensity = 100.0f;
    static constexpr float kDefaultDensity = 100.0f;

    static constexpr float kMinWidth = 0.0f;
    static constexpr float kMaxWidth = 100.0f;
    static constexpr float kDefaultWidth = 100.0f;

    static constexpr float kMinModDepth = 0.0f;
    static constexpr float kMaxModDepth = 100.0f;
    static constexpr float kDefaultModDepth = 0.0f;

    static constexpr float kMinModRate = 0.1f;
    static constexpr float kMaxModRate = 5.0f;
    static constexpr float kDefaultModRate = 1.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    DiffusionNetwork() noexcept = default;

    /// @brief Prepare the network for processing.
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call (unused)
    void prepare(float sampleRate, size_t maxBlockSize) noexcept {
        (void)maxBlockSize;  // Unused, for API consistency

        sampleRate_ = sampleRate;

        // Calculate max delay needed (stage 8 at size=100% with modulation)
        const float maxRatio = kDelayRatiosL[kNumDiffusionStages - 1] * kStereoOffset;
        const float maxDelayMs = kBaseDelayMs * maxRatio + kMaxModDepthMs;
        const float maxDelaySeconds = maxDelayMs * 0.001f;

        // Prepare all stages
        for (size_t i = 0; i < kNumDiffusionStages; ++i) {
            stagesL_[i].prepare(sampleRate, maxDelaySeconds);
            stagesR_[i].prepare(sampleRate, maxDelaySeconds);
        }

        // Initialize LFO phase tracking
        lfoPhase_ = 0.0f;
        lfoPhaseIncrement_ = kTwoPi * kDefaultModRate / sampleRate;

        // Prepare smoothers
        sizeSmoother_ = OnePoleSmoother(kDefaultSize / 100.0f);
        sizeSmoother_.configure(kDiffusionSmoothingMs, sampleRate);

        densitySmoother_ = OnePoleSmoother(kDefaultDensity / 100.0f);
        densitySmoother_.configure(kDiffusionSmoothingMs, sampleRate);

        widthSmoother_ = OnePoleSmoother(kDefaultWidth / 100.0f);
        widthSmoother_.configure(kDiffusionSmoothingMs, sampleRate);

        modDepthSmoother_ = OnePoleSmoother(kDefaultModDepth / 100.0f);
        modDepthSmoother_.configure(kDiffusionSmoothingMs, sampleRate);

        // Initialize per-stage enable smoothers for density crossfade
        for (size_t i = 0; i < kNumDiffusionStages; ++i) {
            stageEnableSmoothers_[i] = OnePoleSmoother(1.0f);  // All enabled by default
            stageEnableSmoothers_[i].configure(kDiffusionSmoothingMs, sampleRate);
        }

        // Set initial values
        size_ = kDefaultSize;
        density_ = kDefaultDensity;
        width_ = kDefaultWidth;
        modDepth_ = kDefaultModDepth;
        modRate_ = kDefaultModRate;

        updateDensityTargets();

        reset();
    }

    /// @brief Reset all internal state.
    void reset() noexcept {
        for (size_t i = 0; i < kNumDiffusionStages; ++i) {
            stagesL_[i].reset();
            stagesR_[i].reset();
        }
        lfoPhase_ = 0.0f;

        // Snap smoothers to current targets
        sizeSmoother_.snapToTarget();
        densitySmoother_.snapToTarget();
        widthSmoother_.snapToTarget();
        modDepthSmoother_.snapToTarget();

        for (size_t i = 0; i < kNumDiffusionStages; ++i) {
            stageEnableSmoothers_[i].snapToTarget();
        }
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set diffusion size (delay time scaling).
    /// @param sizePercent Size in percent [0%, 100%]
    void setSize(float sizePercent) noexcept {
        size_ = std::clamp(sizePercent, kMinSize, kMaxSize);
        sizeSmoother_.setTarget(size_ / 100.0f);
    }

    /// @brief Set diffusion density (number of active stages).
    /// @param densityPercent Density in percent [0%, 100%]
    void setDensity(float densityPercent) noexcept {
        density_ = std::clamp(densityPercent, kMinDensity, kMaxDensity);
        densitySmoother_.setTarget(density_ / 100.0f);
        updateDensityTargets();
    }

    /// @brief Set stereo width.
    /// @param widthPercent Width in percent [0%, 100%]
    void setWidth(float widthPercent) noexcept {
        width_ = std::clamp(widthPercent, kMinWidth, kMaxWidth);
        widthSmoother_.setTarget(width_ / 100.0f);
    }

    /// @brief Set modulation depth.
    /// @param depthPercent Depth in percent [0%, 100%]
    void setModDepth(float depthPercent) noexcept {
        modDepth_ = std::clamp(depthPercent, kMinModDepth, kMaxModDepth);
        modDepthSmoother_.setTarget(modDepth_ / 100.0f);
    }

    /// @brief Set modulation rate.
    /// @param rateHz Rate in Hz [0.1Hz, 5Hz]
    void setModRate(float rateHz) noexcept {
        modRate_ = std::clamp(rateHz, kMinModRate, kMaxModRate);
        lfoPhaseIncrement_ = kTwoPi * modRate_ / sampleRate_;
    }

    // =========================================================================
    // Queries
    // =========================================================================

    [[nodiscard]] float getSize() const noexcept { return size_; }
    [[nodiscard]] float getDensity() const noexcept { return density_; }
    [[nodiscard]] float getWidth() const noexcept { return width_; }
    [[nodiscard]] float getModDepth() const noexcept { return modDepth_; }
    [[nodiscard]] float getModRate() const noexcept { return modRate_; }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio through diffusion network.
    /// @param leftIn Input left channel
    /// @param rightIn Input right channel
    /// @param leftOut Output left channel
    /// @param rightOut Output right channel
    /// @param numSamples Number of samples to process
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept {
        // Early exit for zero-length input
        if (numSamples == 0) return;

        for (size_t n = 0; n < numSamples; ++n) {
            // Update smoothed parameters
            const float size = sizeSmoother_.process();
            const float width = widthSmoother_.process();
            const float modDepth = modDepthSmoother_.process();

            // Get current input samples
            float sampleL = leftIn[n];
            float sampleR = rightIn[n];

            // Size=0% means bypass
            if (size < 0.001f) {
                leftOut[n] = sampleL;
                rightOut[n] = sampleR;
                continue;
            }

            // Process through each stage
            for (size_t i = 0; i < kNumDiffusionStages; ++i) {
                // Get stage enable level (for density crossfade)
                const float stageEnable = stageEnableSmoothers_[i].process();

                // Skip fully disabled stages
                if (stageEnable < 0.001f) continue;

                // Calculate modulated delay time for this stage
                // We use the base LFO value and apply per-stage phase offset manually
                // Phase offset: i * 45° = i * π/4 radians
                const float stagePhaseOffset = static_cast<float>(i) * (kPi / 4.0f);
                const float lfoValue = std::sin(lfoPhase_ + stagePhaseOffset);
                const float modMs = modDepth * kMaxModDepthMs * lfoValue;

                // Base delay time scaled by size
                const float baseDelayMs = kBaseDelayMs * size;

                // Left channel delay
                const float delayMsL = baseDelayMs * kDelayRatiosL[i] + modMs;
                const float delaySamplesL = delayMsL * 0.001f * sampleRate_;

                // Right channel delay (with stereo offset)
                const float delayMsR = baseDelayMs * kDelayRatiosL[i] * kStereoOffset + modMs;
                const float delaySamplesR = delayMsR * 0.001f * sampleRate_;

                // Process through allpass stages
                const float outL = stagesL_[i].process(sampleL, delaySamplesL);
                const float outR = stagesR_[i].process(sampleR, delaySamplesR);

                // Crossfade based on stage enable level
                sampleL = sampleL + stageEnable * (outL - sampleL);
                sampleR = sampleR + stageEnable * (outR - sampleR);
            }

            // Apply stereo width
            // Width = 0%: mono (average)
            // Width = 100%: full stereo
            const float mid = (sampleL + sampleR) * 0.5f;
            const float side = (sampleL - sampleR) * 0.5f;
            sampleL = mid + side * width;
            sampleR = mid - side * width;

            // Write output
            leftOut[n] = sampleL;
            rightOut[n] = sampleR;

            // Advance LFO phase
            lfoPhase_ += lfoPhaseIncrement_;
            if (lfoPhase_ >= kTwoPi) {
                lfoPhase_ -= kTwoPi;
            }
        }
    }

private:
    /// @brief Update stage enable targets based on density setting.
    void updateDensityTargets() noexcept {
        // Density maps to active stages:
        // 0% = 0 stages (bypass)
        // 25% = 2 stages
        // 50% = 4 stages
        // 75% = 6 stages
        // 100% = 8 stages

        const float normalizedDensity = density_ / 100.0f;
        const float numActiveStages = normalizedDensity * static_cast<float>(kNumDiffusionStages);

        for (size_t i = 0; i < kNumDiffusionStages; ++i) {
            const float stageThreshold = static_cast<float>(i);
            float enable = 0.0f;

            if (numActiveStages > stageThreshold + 1.0f) {
                enable = 1.0f;  // Fully enabled
            } else if (numActiveStages > stageThreshold) {
                enable = numActiveStages - stageThreshold;  // Crossfade
            }

            stageEnableSmoothers_[i].setTarget(enable);
        }
    }

    // Stage arrays
    std::array<AllpassStage, kNumDiffusionStages> stagesL_;
    std::array<AllpassStage, kNumDiffusionStages> stagesR_;

    // Modulation (manual phase tracking for per-stage offset support)
    float lfoPhase_ = 0.0f;
    float lfoPhaseIncrement_ = 0.0f;

    // Parameter smoothers
    OnePoleSmoother sizeSmoother_;
    OnePoleSmoother densitySmoother_;
    OnePoleSmoother widthSmoother_;
    OnePoleSmoother modDepthSmoother_;
    std::array<OnePoleSmoother, kNumDiffusionStages> stageEnableSmoothers_;

    // Parameters
    float size_ = kDefaultSize;
    float density_ = kDefaultDensity;
    float width_ = kDefaultWidth;
    float modDepth_ = kDefaultModDepth;
    float modRate_ = kDefaultModRate;

    // State
    float sampleRate_ = 44100.0f;
};

} // namespace DSP
} // namespace Iterum
