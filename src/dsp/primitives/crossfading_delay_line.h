// ==============================================================================
// Layer 1: DSP Primitive - CrossfadingDelayLine
// ==============================================================================
// Delay line with click-free delay time changes using two-tap crossfading.
//
// When delay time changes significantly, instead of moving the read position
// (which causes pitch artifacts), this class crossfades between two taps:
// one at the old delay time and one at the new delay time.
//
// This eliminates discontinuities completely because each tap reads
// continuously from its position - we just blend between the two outputs.
//
// Reference: https://music.arts.uci.edu/dobrian/maxcookbook/abstraction-crossfading-between-delay-times
// Reference: https://www.dsprelated.com/freebooks/pasp/Time_Varying_Delay_Effects.html
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include "dsp/primitives/delay_line.h"
#include <algorithm>
#include <cmath>

namespace Iterum {
namespace DSP {

/// @brief Delay line with click-free delay time changes using two-tap crossfading.
///
/// When the delay time changes by more than a threshold, this class initiates
/// a crossfade between the old delay position (tap A) and the new position (tap B).
/// This eliminates the pitch artifacts and discontinuities that occur when
/// simply smoothing the read position.
///
/// @par How It Works
/// - Two virtual "taps" read from the same underlying delay buffer
/// - One tap (active) is at full volume, the other (inactive) is at zero
/// - When delay time changes, the inactive tap jumps to the new position
/// - A crossfade ramps down the old tap while ramping up the new tap
/// - After crossfade completes, the roles swap and we're ready for the next change
///
/// @par When to Use
/// Use this instead of plain DelayLine when:
/// - User can change delay time via UI knob/automation
/// - Large delay time jumps are possible (e.g., tempo sync changes)
/// - Click-free operation is required
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 1 (wraps DelayLine primitive)
class CrossfadingDelayLine {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Default crossfade time in milliseconds
    static constexpr float kDefaultCrossfadeTimeMs = 20.0f;

    /// Minimum crossfade time (prevents clicks from too-fast fades)
    static constexpr float kMinCrossfadeTimeMs = 5.0f;

    /// Maximum crossfade time
    static constexpr float kMaxCrossfadeTimeMs = 100.0f;

    /// Threshold for triggering crossfade (samples change)
    /// If delay change is less than this, just smooth normally
    static constexpr float kCrossfadeThresholdSamples = 100.0f;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    CrossfadingDelayLine() noexcept = default;
    ~CrossfadingDelayLine() = default;

    // Non-copyable, movable
    CrossfadingDelayLine(const CrossfadingDelayLine&) = delete;
    CrossfadingDelayLine& operator=(const CrossfadingDelayLine&) = delete;
    CrossfadingDelayLine(CrossfadingDelayLine&&) noexcept = default;
    CrossfadingDelayLine& operator=(CrossfadingDelayLine&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    /// @brief Prepare the delay line for processing.
    /// @param sampleRate Sample rate in Hz
    /// @param maxDelaySeconds Maximum delay time in seconds
    void prepare(double sampleRate, float maxDelaySeconds) noexcept {
        sampleRate_ = sampleRate;

        // Single delay buffer shared by both taps
        delayLine_.prepare(sampleRate, maxDelaySeconds);

        // Initialize both taps to same position
        tapADelaySamples_ = 0.0f;
        tapBDelaySamples_ = 0.0f;
        targetDelaySamples_ = 0.0f;

        // Tap A starts active (gain = 1.0), tap B inactive (gain = 0.0)
        tapAGain_ = 1.0f;
        tapBGain_ = 0.0f;
        activeIsTapA_ = true;
        crossfading_ = false;

        // Calculate crossfade increment per sample
        setCrossfadeTime(kDefaultCrossfadeTimeMs);
    }

    /// @brief Reset all state to silence.
    void reset() noexcept {
        delayLine_.reset();

        tapADelaySamples_ = targetDelaySamples_;
        tapBDelaySamples_ = targetDelaySamples_;
        tapAGain_ = 1.0f;
        tapBGain_ = 0.0f;
        activeIsTapA_ = true;
        crossfading_ = false;
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set the crossfade duration.
    /// @param timeMs Crossfade time in milliseconds [5, 100]
    void setCrossfadeTime(float timeMs) noexcept {
        crossfadeTimeMs_ = std::clamp(timeMs, kMinCrossfadeTimeMs, kMaxCrossfadeTimeMs);

        // Calculate per-sample increment for linear crossfade
        const float crossfadeSamples = crossfadeTimeMs_ * 0.001f * static_cast<float>(sampleRate_);
        crossfadeIncrement_ = (crossfadeSamples > 0.0f) ? (1.0f / crossfadeSamples) : 1.0f;
    }

    /// @brief Set the target delay time in samples.
    /// @param delaySamples Target delay in samples
    /// @note If change is large enough, triggers a crossfade
    ///
    /// The crossfade is triggered when the target delay drifts far enough from
    /// the active tap position (where we're currently reading). This handles both:
    /// - Sudden jumps: detected immediately
    /// - Gradual smoothed changes: detected when cumulative drift exceeds threshold
    ///
    /// Key insight: The ACTIVE tap stays fixed at the last crossfade position,
    /// while the INACTIVE tap tracks the current target. This prevents pitch
    /// artifacts from moving the read position. When drift exceeds threshold,
    /// we crossfade to the inactive tap (which is already at the target).
    void setDelaySamples(float delaySamples) noexcept {
        const float clampedDelay = std::max(0.0f, delaySamples);
        targetDelaySamples_ = clampedDelay;

        // Always update the INACTIVE tap to track the current target
        // (This tap is at 0 gain, so updating it has no audible effect)
        if (activeIsTapA_) {
            tapBDelaySamples_ = clampedDelay;
        } else {
            tapADelaySamples_ = clampedDelay;
        }

        if (crossfading_) {
            // Already crossfading - just keep updating inactive tap (done above)
            return;
        }

        // Check if target has drifted far from the ACTIVE tap position
        // This detects both sudden jumps AND gradual smoothed changes
        const float activePosition = activeIsTapA_ ? tapADelaySamples_ : tapBDelaySamples_;
        const float driftFromActive = std::abs(clampedDelay - activePosition);

        if (driftFromActive >= kCrossfadeThresholdSamples) {
            // Large drift - initiate crossfade to the inactive tap
            // The inactive tap is already at the target (updated above)
            crossfading_ = true;
        }
        // For small changes: active tap stays put (no pitch artifacts),
        // inactive tap silently tracks the target for when crossfade happens
    }

    /// @brief Set delay time in milliseconds.
    /// @param delayMs Delay time in milliseconds
    void setDelayMs(float delayMs) noexcept {
        setDelaySamples(delayMs * 0.001f * static_cast<float>(sampleRate_));
    }

    /// @brief Snap to a delay position immediately without crossfading.
    /// @param delaySamples Target delay in samples
    /// @note Use this during initialization or after reset to avoid crossfade transient.
    void snapToDelaySamples(float delaySamples) noexcept {
        const float clampedDelay = std::max(0.0f, delaySamples);
        targetDelaySamples_ = clampedDelay;
        tapADelaySamples_ = clampedDelay;
        tapBDelaySamples_ = clampedDelay;
        crossfading_ = false;
    }

    /// @brief Snap to a delay position immediately without crossfading.
    /// @param delayMs Target delay in milliseconds
    void snapToDelayMs(float delayMs) noexcept {
        snapToDelaySamples(delayMs * 0.001f * static_cast<float>(sampleRate_));
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Write a sample to the delay line.
    /// @param sample Input sample
    void write(float sample) noexcept {
        delayLine_.write(sample);
    }

    /// @brief Read from the delay line with crossfading.
    /// @return Crossfaded output from both taps
    [[nodiscard]] float read() noexcept {
        // Read from both taps
        const float tapAOutput = delayLine_.readLinear(tapADelaySamples_);
        const float tapBOutput = delayLine_.readLinear(tapBDelaySamples_);

        // Mix based on current gains
        float output = tapAOutput * tapAGain_ + tapBOutput * tapBGain_;

        // Update crossfade if in progress
        if (crossfading_) {
            if (activeIsTapA_) {
                // Fading from A to B
                tapAGain_ -= crossfadeIncrement_;
                tapBGain_ += crossfadeIncrement_;

                if (tapBGain_ >= 1.0f) {
                    // Crossfade complete - B is now active
                    tapAGain_ = 0.0f;
                    tapBGain_ = 1.0f;
                    activeIsTapA_ = false;
                    crossfading_ = false;

                    // Sync the inactive tap (A) to current target for next crossfade
                    tapADelaySamples_ = targetDelaySamples_;
                }
            } else {
                // Fading from B to A
                tapBGain_ -= crossfadeIncrement_;
                tapAGain_ += crossfadeIncrement_;

                if (tapAGain_ >= 1.0f) {
                    // Crossfade complete - A is now active
                    tapBGain_ = 0.0f;
                    tapAGain_ = 1.0f;
                    activeIsTapA_ = true;
                    crossfading_ = false;

                    // Sync the inactive tap (B) to current target for next crossfade
                    tapBDelaySamples_ = targetDelaySamples_;
                }
            }
        }

        return output;
    }

    /// @brief Process a single sample (write + read).
    /// @param input Input sample
    /// @return Delayed and crossfaded output
    [[nodiscard]] float process(float input) noexcept {
        write(input);
        return read();
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if a crossfade is currently in progress.
    [[nodiscard]] bool isCrossfading() const noexcept {
        return crossfading_;
    }

    /// @brief Get current effective delay in samples.
    [[nodiscard]] float getCurrentDelaySamples() const noexcept {
        // Weighted average based on tap gains
        return tapADelaySamples_ * tapAGain_ + tapBDelaySamples_ * tapBGain_;
    }

    /// @brief Get maximum delay in samples.
    [[nodiscard]] size_t maxDelaySamples() const noexcept {
        return delayLine_.maxDelaySamples();
    }

private:
    DelayLine delayLine_;               ///< Underlying delay buffer

    // Tap positions (in samples)
    float tapADelaySamples_ = 0.0f;     ///< Tap A read position
    float tapBDelaySamples_ = 0.0f;     ///< Tap B read position
    float targetDelaySamples_ = 0.0f;   ///< Target delay position

    // Tap gains for crossfading
    float tapAGain_ = 1.0f;             ///< Tap A output gain [0, 1]
    float tapBGain_ = 0.0f;             ///< Tap B output gain [0, 1]

    // Crossfade state
    bool activeIsTapA_ = true;          ///< Which tap is currently primary
    bool crossfading_ = false;          ///< Crossfade in progress
    float crossfadeIncrement_ = 0.01f;  ///< Per-sample gain change
    float crossfadeTimeMs_ = kDefaultCrossfadeTimeMs;

    double sampleRate_ = 44100.0;
};

} // namespace DSP
} // namespace Iterum
