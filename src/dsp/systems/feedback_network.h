// ==============================================================================
// Layer 3: System Component - FeedbackNetwork
// ==============================================================================
// Manages feedback loops for delay effects with filtering, saturation, and
// cross-feedback routing.
//
// Feature: 019-feedback-network
// Layer: 3 (System Component)
// Dependencies:
//   - Layer 3: DelayEngine (018)
//   - Layer 2: MultimodeFilter (008), SaturationProcessor (009)
//   - Layer 1: OnePoleSmoother
//   - Layer 0: BlockContext, stereoCrossBlend
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle IX: Layered Architecture (Layer 3 depends on Layer 0-2)
// - Principle X: DSP Constraints (feedback limiting, parameter smoothing)
// - Principle XI: Performance Budget (<1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/019-feedback-network/spec.md
// ==============================================================================

#pragma once

#include "dsp/core/block_context.h"
#include "dsp/core/stereo_utils.h"
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/smoother.h"
#include "dsp/processors/multimode_filter.h"
#include "dsp/processors/saturation_processor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Iterum {
namespace DSP {

// Forward declarations for time mode from DelayEngine
enum class TimeMode : uint8_t;
enum class NoteValue : uint8_t;
enum class NoteModifier : uint8_t;

/// @brief Layer 3 System Component - Feedback Network for Delay Effects
///
/// Manages the feedback loop of a delay effect with:
/// - Adjustable feedback amount (0-120% for self-oscillation)
/// - Filter in feedback path (LP/HP/BP) for tone shaping
/// - Saturation in feedback path for warmth and limiting
/// - Freeze mode for infinite sustain
/// - Stereo cross-feedback for ping-pong effects
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 3 (composes from Layer 0-2)
/// - Principle X: DSP Constraints (feedback limiting, parameter smoothing)
/// - Principle XI: Performance Budget (<1% CPU per instance)
class FeedbackNetwork {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinFeedback = 0.0f;
    static constexpr float kMaxFeedback = 1.2f;        ///< 120% for self-oscillation
    static constexpr float kMinCrossFeedback = 0.0f;
    static constexpr float kMaxCrossFeedback = 1.0f;
    static constexpr float kSmoothingTimeMs = 20.0f;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    FeedbackNetwork() noexcept = default;
    ~FeedbackNetwork() = default;

    // Non-copyable, movable
    FeedbackNetwork(const FeedbackNetwork&) = delete;
    FeedbackNetwork& operator=(const FeedbackNetwork&) = delete;
    FeedbackNetwork(FeedbackNetwork&&) noexcept = default;
    FeedbackNetwork& operator=(FeedbackNetwork&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-007, FR-010)
    // =========================================================================

    /// @brief Prepare for processing
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        maxDelayMs_ = maxDelayMs;

        // Convert to seconds for DelayLine
        const float maxDelaySeconds = maxDelayMs / 1000.0f;

        // Prepare delay lines
        delayLineL_.prepare(sampleRate, maxDelaySeconds);
        delayLineR_.prepare(sampleRate, maxDelaySeconds);

        // Configure smoothers
        feedbackSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        delaySmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        crossFeedbackSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        inputMuteSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));

        // Initialize smoothers
        feedbackSmoother_.snapTo(feedbackAmount_);
        delaySmoother_.snapTo(0.0f);
        crossFeedbackSmoother_.snapTo(crossFeedbackAmount_);
        inputMuteSmoother_.snapTo(1.0f);  // Not muted

        // Prepare filter and saturation for each channel
        filterL_.prepare(sampleRate, maxBlockSize);
        filterR_.prepare(sampleRate, maxBlockSize);
        saturatorL_.prepare(sampleRate, maxBlockSize);
        saturatorR_.prepare(sampleRate, maxBlockSize);

        // Configure default saturation for self-oscillation limiting
        saturatorL_.setType(SaturationType::Tape);
        saturatorR_.setType(SaturationType::Tape);
        saturatorL_.setInputGain(0.0f);  // No extra drive by default
        saturatorR_.setInputGain(0.0f);

        // Allocate feedback buffers
        feedbackBufferL_.resize(maxBlockSize);
        feedbackBufferR_.resize(maxBlockSize);

        prepared_ = true;
    }

    /// @brief Reset all internal state
    void reset() noexcept {
        delayLineL_.reset();
        delayLineR_.reset();
        feedbackSmoother_.snapTo(feedbackAmount_);
        delaySmoother_.snapTo(targetDelayMs_);  // Snap to current target, not 0
        crossFeedbackSmoother_.snapTo(crossFeedbackAmount_);
        inputMuteSmoother_.snapTo(frozen_ ? 0.0f : 1.0f);
        filterL_.reset();
        filterR_.reset();
        saturatorL_.reset();
        saturatorR_.reset();

        // Clear feedback state
        lastFeedbackL_ = 0.0f;
        lastFeedbackR_ = 0.0f;

        // Reset processing state so parameters can snap again
        hasProcessed_ = false;
    }

    /// @brief Check if prepared
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // =========================================================================
    // Processing Methods (FR-008, FR-009, FR-015)
    // =========================================================================

    /// @brief Process mono audio buffer
    void process(float* buffer, size_t numSamples, const BlockContext& ctx) noexcept {
        if (!prepared_ || numSamples == 0) return;

        hasProcessed_ = true;

        // Update delay target
        delaySmoother_.setTarget(targetDelayMs_);

        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed values
            const float feedback = feedbackSmoother_.process();
            const float delayMs = delaySmoother_.process();
            const float inputGain = inputMuteSmoother_.process();

            // Convert delay to samples (subtract 1 for read-before-write timing)
            const float delaySamples = std::max(0.0f, msToSamples(delayMs) - 1.0f);

            // Read delayed sample first (read-before-write pattern)
            const float delayed = delayLineL_.readLinear(delaySamples);

            // Calculate feedback signal
            float feedbackSignal = delayed;

            // Apply filter if enabled
            if (filterEnabled_) {
                feedbackSignal = filterL_.processSample(feedbackSignal);
            }

            // Apply saturation if enabled
            if (saturationEnabled_) {
                feedbackSignal = saturatorL_.processSample(feedbackSignal);
            }

            // Scale by feedback amount
            feedbackSignal *= feedback;

            // Combine input with feedback
            const float input = buffer[i] * inputGain;
            const float toDelay = input + feedbackSignal;

            // Write mixed signal to delay line
            delayLineL_.write(toDelay);

            // Output is the delayed signal (wet only for feedback network)
            buffer[i] = delayed;
        }
    }

    /// @brief Process stereo audio buffers
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept {
        if (!prepared_ || numSamples == 0) return;

        hasProcessed_ = true;

        // Update delay target
        delaySmoother_.setTarget(targetDelayMs_);

        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed values
            const float feedback = feedbackSmoother_.process();
            const float delayMs = delaySmoother_.process();
            const float crossFeedback = crossFeedbackSmoother_.process();
            const float inputGain = inputMuteSmoother_.process();

            // Convert delay to samples (subtract 1 for read-before-write timing)
            const float delaySamples = std::max(0.0f, msToSamples(delayMs) - 1.0f);

            // Read delayed samples first (read-before-write pattern)
            const float delayedL = delayLineL_.readLinear(delaySamples);
            const float delayedR = delayLineR_.readLinear(delaySamples);

            // Calculate feedback signal
            float feedbackL = delayedL;
            float feedbackR = delayedR;

            // Apply filter if enabled
            if (filterEnabled_) {
                feedbackL = filterL_.processSample(feedbackL);
                feedbackR = filterR_.processSample(feedbackR);
            }

            // Apply saturation if enabled
            if (saturationEnabled_) {
                feedbackL = saturatorL_.processSample(feedbackL);
                feedbackR = saturatorR_.processSample(feedbackR);
            }

            // Apply cross-feedback (stereo routing)
            float crossedL, crossedR;
            stereoCrossBlend(feedbackL, feedbackR, crossFeedback, crossedL, crossedR);

            // Scale by feedback amount
            crossedL *= feedback;
            crossedR *= feedback;

            // Combine input with feedback
            const float inputL = left[i] * inputGain;
            const float inputR = right[i] * inputGain;
            const float toDelayL = inputL + crossedL;
            const float toDelayR = inputR + crossedR;

            // Write mixed signal to delay lines
            delayLineL_.write(toDelayL);
            delayLineR_.write(toDelayR);

            // Output is the delayed signal
            left[i] = delayedL;
            right[i] = delayedR;
        }
    }

    // =========================================================================
    // Feedback Parameters (FR-002, FR-011, FR-012, FR-013)
    // =========================================================================

    /// @brief Set feedback amount (0.0 - 1.2)
    void setFeedbackAmount(float amount) noexcept {
        // FR-013: Reject NaN
        if (std::isnan(amount)) return;

        // FR-012: Clamp to valid range
        amount = std::clamp(amount, kMinFeedback, kMaxFeedback);

        feedbackAmount_ = amount;
        if (!frozen_) {
            // If not yet processing, snap immediately for instant setup
            if (!hasProcessed_) {
                feedbackSmoother_.snapTo(amount);
            } else {
                feedbackSmoother_.setTarget(amount);
            }
        }
    }

    /// @brief Get current feedback amount
    [[nodiscard]] float getFeedbackAmount() const noexcept {
        return feedbackAmount_;
    }

    // =========================================================================
    // Delay Time Parameters
    // =========================================================================

    /// @brief Set delay time in milliseconds
    void setDelayTimeMs(float ms) noexcept {
        // Reject NaN
        if (std::isnan(ms)) return;

        // Clamp to valid range
        ms = std::clamp(ms, 0.0f, maxDelayMs_);
        targetDelayMs_ = ms;

        // If not yet processing, snap immediately for instant setup
        if (!hasProcessed_) {
            delaySmoother_.snapTo(ms);
        }
    }

    /// @brief Get current delay time
    [[nodiscard]] float getCurrentDelayMs() const noexcept {
        return delaySmoother_.getCurrentValue();
    }

    // =========================================================================
    // Filter Parameters (FR-003, FR-014) - US3
    // =========================================================================

    void setFilterEnabled(bool enabled) noexcept { filterEnabled_ = enabled; }
    [[nodiscard]] bool isFilterEnabled() const noexcept { return filterEnabled_; }

    void setFilterType(FilterType type) noexcept {
        filterL_.setType(type);
        filterR_.setType(type);
    }

    void setFilterCutoff(float hz) noexcept {
        filterL_.setCutoff(hz);
        filterR_.setCutoff(hz);
    }

    void setFilterResonance(float q) noexcept {
        filterL_.setResonance(q);
        filterR_.setResonance(q);
    }

    // =========================================================================
    // Saturation Parameters (FR-004, FR-014) - US2, US4
    // =========================================================================

    void setSaturationEnabled(bool enabled) noexcept { saturationEnabled_ = enabled; }
    [[nodiscard]] bool isSaturationEnabled() const noexcept { return saturationEnabled_; }

    void setSaturationType(SaturationType type) noexcept {
        saturatorL_.setType(type);
        saturatorR_.setType(type);
    }

    void setSaturationDrive(float dB) noexcept {
        saturatorL_.setInputGain(dB);
        saturatorR_.setInputGain(dB);
    }

    // =========================================================================
    // Freeze Mode (FR-005) - US5
    // =========================================================================

    void setFreeze(bool freeze) noexcept {
        if (freeze == frozen_) return;

        frozen_ = freeze;
        if (freeze) {
            // Store current feedback and set to 100%
            preFreezeAmount_ = feedbackAmount_;
            feedbackSmoother_.setTarget(1.0f);
            inputMuteSmoother_.setTarget(0.0f);  // Mute input
        } else {
            // Restore previous feedback
            feedbackSmoother_.setTarget(preFreezeAmount_);
            inputMuteSmoother_.setTarget(1.0f);  // Unmute input
        }
    }

    [[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

    // =========================================================================
    // Cross-Feedback (FR-006, FR-016) - US6
    // =========================================================================

    void setCrossFeedbackAmount(float amount) noexcept {
        // Reject NaN
        if (std::isnan(amount)) return;

        amount = std::clamp(amount, kMinCrossFeedback, kMaxCrossFeedback);
        crossFeedbackAmount_ = amount;

        // If not yet processing, snap immediately for instant setup
        if (!hasProcessed_) {
            crossFeedbackSmoother_.snapTo(amount);
        } else {
            crossFeedbackSmoother_.setTarget(amount);
        }
    }

    [[nodiscard]] float getCrossFeedbackAmount() const noexcept {
        return crossFeedbackAmount_;
    }

    // =========================================================================
    // Query
    // =========================================================================

    [[nodiscard]] size_t getLatency() const noexcept {
        // Latency comes from oversampling in filter/saturator
        // For now, return 0 (no additional latency beyond delay)
        return 0;
    }

private:
    // Layer 1 primitives
    DelayLine delayLineL_;
    DelayLine delayLineR_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother delaySmoother_;
    OnePoleSmoother crossFeedbackSmoother_;
    OnePoleSmoother inputMuteSmoother_;

    // Layer 2 processors
    MultimodeFilter filterL_;
    MultimodeFilter filterR_;
    SaturationProcessor saturatorL_;
    SaturationProcessor saturatorR_;

    // Scratch buffers
    std::vector<float> feedbackBufferL_;
    std::vector<float> feedbackBufferR_;

    // Feedback state
    float lastFeedbackL_ = 0.0f;
    float lastFeedbackR_ = 0.0f;

    // Parameters
    float feedbackAmount_ = 0.5f;
    float targetDelayMs_ = 0.0f;
    float crossFeedbackAmount_ = 0.0f;
    float preFreezeAmount_ = 0.5f;

    // Feature enable flags
    bool filterEnabled_ = false;
    bool saturationEnabled_ = false;
    bool frozen_ = false;

    // Runtime state
    double sampleRate_ = 0.0;
    float maxDelayMs_ = 0.0f;
    size_t maxBlockSize_ = 0;
    bool prepared_ = false;
    bool hasProcessed_ = false;  ///< True after first process() call

    // Helper
    [[nodiscard]] float msToSamples(float ms) const noexcept {
        return static_cast<float>(ms * sampleRate_ / 1000.0);
    }
};

} // namespace DSP
} // namespace Iterum
