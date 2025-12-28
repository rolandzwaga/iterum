// ==============================================================================
// FlexibleFeedbackNetwork - Feedback Loop with Processor Injection
// ==============================================================================
// Layer 3: System Components
//
// A feedback network that supports injecting arbitrary processors (via
// IFeedbackProcessor interface) into the feedback path. Enables advanced
// effects like shimmer (pitch shifting in feedback) and freeze mode.
//
// Features:
// - Processor injection with mix control
// - Freeze mode (100% feedback + input mute)
// - Feedback filtering (via MultimodeFilter)
// - Feedback limiting for >100% feedback stability
// - Latency reporting (aggregates injected processor latency)
// - Hot-swap with crossfade
//
// Design Notes:
// - Hybrid sample-by-sample delay loop with block-based processor. Within a
//   block, feedback uses the raw delay output for immediate responsiveness.
//   The processor runs on the block output, and its result feeds back into
//   the NEXT block. This gives the best compromise between responsiveness
//   (no delay for basic feedback) and processor support (one-block latency).
// - At 512 samples/44.1kHz, processor effects have ~11.6ms feedback latency.
//
// All operations are real-time safe (no allocations in process)
// ==============================================================================
#pragma once

#include "dsp/systems/i_feedback_processor.h"
#include "dsp/primitives/crossfading_delay_line.h"
#include "dsp/primitives/smoother.h"
#include "dsp/processors/multimode_filter.h"
#include "dsp/processors/dynamics_processor.h"
#include "dsp/core/block_context.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace Iterum::DSP {

/// @brief Feedback network with injectable processor support
///
/// Unlike the simpler FeedbackNetwork (spec 019), this component allows
/// arbitrary processing in the feedback path via IFeedbackProcessor.
/// This enables effects like shimmer delay (pitch shifting) and freeze mode.
class FlexibleFeedbackNetwork {
public:
    /// @brief Maximum delay time in milliseconds
    static constexpr float kMaxDelayMs = 10000.0f;

    /// @brief Default constructor
    FlexibleFeedbackNetwork() = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Prepare the network for audio processing
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Prepare delay lines (10 second max delay)
        constexpr float kMaxDelaySeconds = kMaxDelayMs / 1000.0f;
        delayL_.prepare(sampleRate, kMaxDelaySeconds);
        delayR_.prepare(sampleRate, kMaxDelaySeconds);

        // Pre-allocate processing buffers (sized to maxBlockSize)
        feedbackL_.resize(maxBlockSize, 0.0f);
        feedbackR_.resize(maxBlockSize, 0.0f);
        processedL_.resize(maxBlockSize, 0.0f);
        processedR_.resize(maxBlockSize, 0.0f);
        oldProcessedL_.resize(maxBlockSize, 0.0f);
        oldProcessedR_.resize(maxBlockSize, 0.0f);

        // Configure smoothers (20ms smoothing time)
        constexpr float kSmoothTimeMs = 20.0f;
        feedbackSmoother_.configure(kSmoothTimeMs, static_cast<float>(sampleRate));
        processorMixSmoother_.configure(kSmoothTimeMs, static_cast<float>(sampleRate));
        freezeMixSmoother_.configure(kSmoothTimeMs, static_cast<float>(sampleRate));
        delayTimeSmoother_.configure(kSmoothTimeMs, static_cast<float>(sampleRate));

        // Prepare filters
        filterL_.prepare(sampleRate, maxBlockSize);
        filterR_.prepare(sampleRate, maxBlockSize);
        filterL_.setType(FilterType::Lowpass);
        filterR_.setType(FilterType::Lowpass);
        filterL_.setCutoff(4000.0f);
        filterR_.setCutoff(4000.0f);

        // Prepare limiters for >100% feedback stability
        limiterL_.prepare(sampleRate, maxBlockSize);
        limiterR_.prepare(sampleRate, maxBlockSize);
        limiterL_.setDetectionMode(DynamicsDetectionMode::Peak);
        limiterR_.setDetectionMode(DynamicsDetectionMode::Peak);
        limiterL_.setThreshold(0.0f);  // 0 dB threshold
        limiterR_.setThreshold(0.0f);
        limiterL_.setRatio(100.0f);    // Hard limiting (100:1)
        limiterR_.setRatio(100.0f);
        limiterL_.setAttackTime(0.1f);   // Fast attack
        limiterR_.setAttackTime(0.1f);
        limiterL_.setReleaseTime(50.0f);
        limiterR_.setReleaseTime(50.0f);

        // Initialize smoothers to target values
        snapParameters();

        // Prepare injected processor if set
        if (processor_) {
            processor_->prepare(sampleRate, maxBlockSize);
        }
    }

    /// @brief Reset all internal state
    void reset() noexcept {
        delayL_.reset();
        delayR_.reset();

        filterL_.reset();
        filterR_.reset();

        limiterL_.reset();
        limiterR_.reset();

        // Clear all processing buffers
        std::fill(feedbackL_.begin(), feedbackL_.end(), 0.0f);
        std::fill(feedbackR_.begin(), feedbackR_.end(), 0.0f);
        std::fill(processedL_.begin(), processedL_.end(), 0.0f);
        std::fill(processedR_.begin(), processedR_.end(), 0.0f);
        std::fill(oldProcessedL_.begin(), oldProcessedL_.end(), 0.0f);
        std::fill(oldProcessedR_.begin(), oldProcessedR_.end(), 0.0f);

        // Clear processed feedback state
        lastProcessedFeedbackL_ = 0.0f;
        lastProcessedFeedbackR_ = 0.0f;

        // Reset injected processor
        if (processor_) {
            processor_->reset();
        }

        // Reset crossfade state
        crossfadePosition_ = 0.0f;
        oldProcessor_ = nullptr;
    }

    // -------------------------------------------------------------------------
    // Processing
    // -------------------------------------------------------------------------

    /// @brief Process stereo audio through the feedback network
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @param ctx Block context for tempo sync
    void process(float* left, float* right, std::size_t numSamples,
                 [[maybe_unused]] const BlockContext& ctx) noexcept {
        if (numSamples == 0 || !left || !right) return;

        // Process sample-by-sample for correct feedback loop behavior
        for (std::size_t i = 0; i < numSamples; ++i) {
            // Get smoothed parameters
            const float feedback = feedbackSmoother_.process();
            const float freezeMix = freezeMixSmoother_.process();
            const float delayTimeSamples = delayTimeSmoother_.process();

            // In freeze mode, mute input
            const float inputL = left[i] * (1.0f - freezeMix);
            const float inputR = right[i] * (1.0f - freezeMix);

            // Effective feedback amount (interpolate to 100% in freeze mode)
            const float effectiveFeedback = feedback + freezeMix * (1.0f - feedback);

            // Convert delay time to ms for CrossfadingDelayLine
            const float delayMs = delayTimeSamples / static_cast<float>(sampleRate_) * 1000.0f;

            // Set delay time on crossfading delay lines (handles large changes smoothly)
            delayL_.setDelayMs(delayMs);
            delayR_.setDelayMs(delayMs);

            // Read delayed sample FIRST (read-before-write pattern)
            // CrossfadingDelayLine handles the crossfading internally
            const float delayedL = delayL_.read();
            const float delayedR = delayR_.read();

            // Calculate feedback signal using processed feedback from previous block
            const float feedbackSignalL = lastProcessedFeedbackL_ * effectiveFeedback;
            const float feedbackSignalR = lastProcessedFeedbackR_ * effectiveFeedback;

            // Combine input with feedback and write to delay line
            delayL_.write(inputL + feedbackSignalL);
            delayR_.write(inputR + feedbackSignalR);

            // Store delay output for block-based processing (processor, filter, limiter)
            feedbackL_[i] = delayedL;
            feedbackR_[i] = delayedR;

            // Update per-sample feedback for within-block responsiveness.
            // When a processor is active, this provides "raw" feedback within the block,
            // while the end-of-block update (after processor) provides processed feedback
            // for the next block. This is a compromise: within-block gets immediate raw
            // feedback, cross-block gets processed feedback with one-block latency.
            lastProcessedFeedbackL_ = delayedL;
            lastProcessedFeedbackR_ = delayedR;
        }

        // Apply injected processor to feedback signal (if present)
        if (processor_) {
            std::copy(feedbackL_.begin(), feedbackL_.begin() + numSamples, processedL_.begin());
            std::copy(feedbackR_.begin(), feedbackR_.begin() + numSamples, processedR_.begin());

            processor_->process(processedL_.data(), processedR_.data(), numSamples);

            // Handle crossfade if hot-swapping
            if (oldProcessor_ && crossfadePosition_ < crossfadeSamples_) {
                std::copy(feedbackL_.begin(), feedbackL_.begin() + numSamples, oldProcessedL_.begin());
                std::copy(feedbackR_.begin(), feedbackR_.begin() + numSamples, oldProcessedR_.begin());

                oldProcessor_->process(oldProcessedL_.data(), oldProcessedR_.data(), numSamples);

                for (std::size_t i = 0; i < numSamples; ++i) {
                    const float fadePos = (crossfadePosition_ + static_cast<float>(i)) / crossfadeSamples_;
                    const float newGain = std::min(1.0f, fadePos);
                    const float oldGain = 1.0f - newGain;
                    processedL_[i] = processedL_[i] * newGain + oldProcessedL_[i] * oldGain;
                    processedR_[i] = processedR_[i] * newGain + oldProcessedR_[i] * oldGain;
                }

                crossfadePosition_ += static_cast<float>(numSamples);
                if (crossfadePosition_ >= crossfadeSamples_) {
                    oldProcessor_ = nullptr;
                }
            }

            // Mix processed with dry feedback based on processor mix (smoothed per-sample)
            for (std::size_t i = 0; i < numSamples; ++i) {
                const float mix = processorMixSmoother_.process();
                feedbackL_[i] = feedbackL_[i] * (1.0f - mix) + processedL_[i] * mix;
                feedbackR_[i] = feedbackR_[i] * (1.0f - mix) + processedR_[i] * mix;
            }
        }

        // Apply filter to feedback if enabled
        if (filterEnabled_) {
            filterL_.process(feedbackL_.data(), numSamples);
            filterR_.process(feedbackR_.data(), numSamples);
        }

        // Apply limiting if feedback > 100%
        if (feedbackAmount_ > 1.0f) {
            limiterL_.process(feedbackL_.data(), numSamples);
            limiterR_.process(feedbackR_.data(), numSamples);

            // Apply soft clipping as safety net (catches transients during attack time)
            for (std::size_t i = 0; i < numSamples; ++i) {
                feedbackL_[i] = std::tanh(feedbackL_[i]);
                feedbackR_[i] = std::tanh(feedbackR_[i]);
            }
        }

        // Copy processed feedback to output
        for (std::size_t i = 0; i < numSamples; ++i) {
            left[i] = feedbackL_[i];
            right[i] = feedbackR_[i];
        }

        // Store last PROCESSED feedback value for next block's feedback signal
        lastProcessedFeedbackL_ = feedbackL_[numSamples - 1];
        lastProcessedFeedbackR_ = feedbackR_[numSamples - 1];
    }

    // -------------------------------------------------------------------------
    // Processor Injection
    // -------------------------------------------------------------------------

    /// @brief Set the processor to use in the feedback path
    /// @param processor Pointer to processor (ownership not transferred)
    /// @param crossfadeMs Crossfade time for hot-swap (0 = immediate)
    /// @note Pass nullptr to remove processor
    void setProcessor(IFeedbackProcessor* processor,
                      float crossfadeMs = 50.0f) noexcept {
        if (processor == processor_) return;

        if (crossfadeMs > 0.0f && processor_ != nullptr) {
            // Hot-swap with crossfade
            oldProcessor_ = processor_;
            crossfadeSamples_ = msToSamples(crossfadeMs);
            crossfadePosition_ = 0.0f;
        }

        processor_ = processor;

        // Prepare new processor if we have a valid sample rate
        if (processor_ && sampleRate_ > 0) {
            processor_->prepare(sampleRate_, maxBlockSize_);
        }
    }

    /// @brief Set the mix amount for the injected processor
    /// @param mix Mix amount (0-100%, 0 = bypass processor)
    void setProcessorMix(float mix) noexcept {
        processorMix_ = std::clamp(mix / 100.0f, 0.0f, 1.0f);
        processorMixSmoother_.setTarget(processorMix_);
    }

    // -------------------------------------------------------------------------
    // Feedback Parameters
    // -------------------------------------------------------------------------

    /// @brief Set the feedback amount
    /// @param amount Feedback amount (0-120%, >100% requires limiting)
    void setFeedbackAmount(float amount) noexcept {
        feedbackAmount_ = std::clamp(amount, 0.0f, 1.2f);
        feedbackSmoother_.setTarget(feedbackAmount_);
    }

    /// @brief Set the delay time in milliseconds
    /// @param ms Delay time (0 to kMaxDelayMs)
    void setDelayTimeMs(float ms) noexcept {
        delayTimeMs_ = std::clamp(ms, 0.0f, kMaxDelayMs);
        delayTimeSmoother_.setTarget(msToSamples(delayTimeMs_));
    }

    // -------------------------------------------------------------------------
    // Freeze Mode
    // -------------------------------------------------------------------------

    /// @brief Enable/disable freeze mode
    /// @param enabled True to freeze (100% feedback, mute input)
    void setFreezeEnabled(bool enabled) noexcept {
        freezeEnabled_ = enabled;
        freezeMixSmoother_.setTarget(enabled ? 1.0f : 0.0f);
    }

    /// @brief Check if freeze mode is active
    [[nodiscard]] bool isFreezeEnabled() const noexcept {
        return freezeEnabled_;
    }

    // -------------------------------------------------------------------------
    // Filter
    // -------------------------------------------------------------------------

    /// @brief Enable/disable the feedback filter
    /// @param enabled True to enable filtering
    void setFilterEnabled(bool enabled) noexcept {
        filterEnabled_ = enabled;
    }

    /// @brief Set the filter cutoff frequency
    /// @param hz Cutoff frequency in Hz
    void setFilterCutoff(float hz) noexcept {
        filterL_.setCutoff(hz);
        filterR_.setCutoff(hz);
    }

    /// @brief Set the filter type
    /// @param type Filter type (lowpass, highpass, etc.)
    void setFilterType(FilterType type) noexcept {
        filterL_.setType(type);
        filterR_.setType(type);
    }

    // -------------------------------------------------------------------------
    // Latency
    // -------------------------------------------------------------------------

    /// @brief Get total latency (delay line + injected processor)
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        std::size_t latency = 0;

        // Add processor latency if present
        if (processor_) {
            latency += processor_->getLatencySamples();
        }

        return latency;
    }

    // -------------------------------------------------------------------------
    // Parameter Snapping
    // -------------------------------------------------------------------------

    /// @brief Snap all smoothed parameters to their targets
    void snapParameters() noexcept {
        feedbackSmoother_.snapTo(feedbackAmount_);
        processorMixSmoother_.snapTo(processorMix_);
        freezeMixSmoother_.snapTo(freezeEnabled_ ? 1.0f : 0.0f);
        delayTimeSmoother_.snapTo(msToSamples(delayTimeMs_));

        // Also snap CrossfadingDelayLines to avoid crossfade transient on init
        delayL_.snapToDelayMs(delayTimeMs_);
        delayR_.snapToDelayMs(delayTimeMs_);
    }

private:
    // Sample rate
    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;

    // Delay lines (stereo) - CrossfadingDelayLine for click-free delay time changes
    CrossfadingDelayLine delayL_;
    CrossfadingDelayLine delayR_;

    // Injected processor
    IFeedbackProcessor* processor_ = nullptr;
    IFeedbackProcessor* oldProcessor_ = nullptr;  // For crossfade
    float crossfadeSamples_ = 0.0f;
    float crossfadePosition_ = 0.0f;

    // Parameter smoothers
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother processorMixSmoother_;
    OnePoleSmoother freezeMixSmoother_;
    OnePoleSmoother delayTimeSmoother_;

    // Target values
    float feedbackAmount_ = 0.5f;
    float processorMix_ = 1.0f;
    float delayTimeMs_ = 500.0f;
    bool freezeEnabled_ = false;

    // Filter
    bool filterEnabled_ = false;
    MultimodeFilter filterL_;
    MultimodeFilter filterR_;

    // Limiter for >100% feedback
    DynamicsProcessor limiterL_;
    DynamicsProcessor limiterR_;

    // Pre-allocated buffers (resized in prepare() to maxBlockSize)
    std::vector<float> feedbackL_;
    std::vector<float> feedbackR_;
    std::vector<float> processedL_;
    std::vector<float> processedR_;
    std::vector<float> oldProcessedL_;
    std::vector<float> oldProcessedR_;

    // Last processed feedback (for block-based processor feedback path)
    float lastProcessedFeedbackL_ = 0.0f;
    float lastProcessedFeedbackR_ = 0.0f;

    // Helper methods
    float msToSamples(float ms) const noexcept {
        return static_cast<float>(ms * sampleRate_ / 1000.0);
    }
};

} // namespace Iterum::DSP
