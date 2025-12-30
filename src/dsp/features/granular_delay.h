// Layer 4: User Feature - Granular Delay
// Part of Granular Delay feature (spec 034)
// Tempo sync additions: spec 038
#pragma once

#include "dsp/core/db_utils.h"
#include "dsp/core/grain_envelope.h"
#include "dsp/core/block_context.h"
#include "dsp/core/note_value.h"
#include "dsp/primitives/smoother.h"
#include "dsp/systems/delay_engine.h"
#include "dsp/systems/granular_engine.h"

#include <algorithm>
#include <cmath>

namespace Iterum::DSP {

/// Complete granular delay effect with all user-facing parameters.
/// Breaks incoming audio into grains and reassembles with pitch shifting,
/// position randomization, reverse playback, and density control.
class GranularDelay {
public:
    static constexpr float kDefaultSmoothTimeMs = 20.0f;
    static constexpr float kMaxDelaySeconds = 2.0f;

    /// Prepare effect for processing
    /// @param sampleRate Current sample rate
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Prepare granular engine
        engine_.prepare(sampleRate, kMaxDelaySeconds);

        // Configure output smoothers
        feedbackSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));
        dryWetSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));
        gainSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));

        reset();
    }

    /// Reset effect state
    void reset() noexcept {
        engine_.reset();

        // Reset feedback state
        feedbackL_ = 0.0f;
        feedbackR_ = 0.0f;

        // Snap smoothers to current values
        feedbackSmoother_.snapTo(feedback_);
        dryWetSmoother_.snapTo(dryWet_);
        gainSmoother_.snapTo(outputGainLinear_);
    }

    // === Core Parameters ===

    /// Set grain size in milliseconds (10-500ms)
    void setGrainSize(float ms) noexcept { engine_.setGrainSize(ms); }

    /// Set grain density (grains per second, 1-100 Hz)
    void setDensity(float grainsPerSec) noexcept { engine_.setDensity(grainsPerSec); }

    /// Set base delay time in milliseconds (0-2000ms)
    void setDelayTime(float ms) noexcept { engine_.setPosition(ms); }

    /// Set position spray/randomization (0-1)
    void setPositionSpray(float amount) noexcept { engine_.setPositionSpray(amount); }

    // === Pitch Parameters ===

    /// Set base pitch shift in semitones (-24 to +24)
    void setPitch(float semitones) noexcept { engine_.setPitch(semitones); }

    /// Set pitch spray/randomization (0-1)
    void setPitchSpray(float amount) noexcept { engine_.setPitchSpray(amount); }

    // === Modifiers ===

    /// Set reverse playback probability (0-1)
    void setReverseProbability(float prob) noexcept { engine_.setReverseProbability(prob); }

    /// Set pan spray/randomization (0-1)
    void setPanSpray(float amount) noexcept { engine_.setPanSpray(amount); }

    /// Set grain envelope type
    void setEnvelopeType(GrainEnvelopeType type) noexcept { engine_.setEnvelopeType(type); }

    // === Global Controls ===

    /// Enable/disable freeze mode
    void setFreeze(bool frozen) noexcept { engine_.setFreeze(frozen); }

    /// Check if frozen
    [[nodiscard]] bool isFrozen() const noexcept { return engine_.isFrozen(); }

    /// Set feedback amount (0-1.2)
    void setFeedback(float amount) noexcept {
        feedback_ = std::clamp(amount, 0.0f, 1.2f);
        feedbackSmoother_.setTarget(feedback_);
    }

    /// Set dry/wet mix (0-1)
    void setDryWet(float mix) noexcept {
        dryWet_ = std::clamp(mix, 0.0f, 1.0f);
        dryWetSmoother_.setTarget(dryWet_);
    }

    /// Set output gain in dB (-inf to +6)
    void setOutputGain(float dB) noexcept {
        outputGainLinear_ = dbToGain(std::clamp(dB, -96.0f, 6.0f));
        gainSmoother_.setTarget(outputGainLinear_);
    }

    // === Tempo Sync Controls (spec 038) ===

    /// Set time mode: 0 = Free (ms), 1 = Synced (note value + tempo)
    /// @param mode 0 for Free, 1 for Synced
    void setTimeMode(int mode) noexcept {
        timeMode_ = (mode == 1) ? TimeMode::Synced : TimeMode::Free;
    }

    /// Set note value index for tempo sync (0-9)
    /// Maps to: 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1
    /// @param index Dropdown index (0-9), default 4 = 1/8 note
    void setNoteValue(int index) noexcept {
        noteValueIndex_ = std::clamp(index, 0, 9);
    }

    // === Processing ===

    /// Process a block of stereo audio with tempo context (spec 038)
    /// When in Synced mode, position is calculated from note value + tempo
    /// @param leftIn Input left channel buffer
    /// @param rightIn Input right channel buffer
    /// @param leftOut Output left channel buffer
    /// @param rightOut Output right channel buffer
    /// @param numSamples Number of samples to process
    /// @param ctx Block context containing tempo information
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples,
                 const BlockContext& ctx) noexcept {
        // Update position from tempo if in synced mode (FR-003)
        if (timeMode_ == TimeMode::Synced) {
            // Get tempo with fallback to 120 BPM if unavailable (FR-007)
            double tempo = ctx.tempoBPM;
            if (tempo <= 0.0) {
                tempo = 120.0;  // Fallback default
            }

            // Calculate position from note value and tempo
            float syncedMs = dropdownToDelayMs(noteValueIndex_, tempo);

            // Clamp to max delay buffer (FR-006)
            syncedMs = std::clamp(syncedMs, 0.0f, kMaxDelaySeconds * 1000.0f);

            engine_.setPosition(syncedMs);
        }
        // In Free mode (FR-004), position is set via setDelayTime() - no change needed

        // Delegate to core processing
        processCore(leftIn, rightIn, leftOut, rightOut, numSamples);
    }

    /// Process a block of stereo audio (legacy overload without tempo context)
    /// Uses Free mode behavior - position is set via setDelayTime()
    /// @param leftIn Input left channel buffer
    /// @param rightIn Input right channel buffer
    /// @param leftOut Output left channel buffer
    /// @param rightOut Output right channel buffer
    /// @param numSamples Number of samples to process
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept {
        processCore(leftIn, rightIn, leftOut, rightOut, numSamples);
    }

private:
    /// Core processing loop (shared by both process overloads)
    void processCore(const float* leftIn, const float* rightIn,
                     float* leftOut, float* rightOut,
                     size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed parameters
            const float feedback = feedbackSmoother_.process();
            const float dryWet = dryWetSmoother_.process();
            const float gain = gainSmoother_.process();

            // Mix input with feedback
            float inputL = leftIn[i];
            float inputR = rightIn[i];

            // Add feedback (soft-limit if > 1.0)
            if (feedback > 0.0f) {
                float fbL = feedbackL_ * feedback;
                float fbR = feedbackR_ * feedback;

                // Soft-limit feedback to prevent runaway
                if (feedback > 1.0f) {
                    fbL = std::tanh(fbL);
                    fbR = std::tanh(fbR);
                }

                inputL += fbL;
                inputR += fbR;
            }

            // Process through granular engine
            float wetL = 0.0f;
            float wetR = 0.0f;
            engine_.process(inputL, inputR, wetL, wetR);

            // Store for feedback
            feedbackL_ = wetL;
            feedbackR_ = wetR;

            // Dry/wet mix
            const float dryL = leftIn[i] * (1.0f - dryWet);
            const float dryR = rightIn[i] * (1.0f - dryWet);
            const float mixL = dryL + wetL * dryWet;
            const float mixR = dryR + wetR * dryWet;

            // Apply output gain
            leftOut[i] = mixL * gain;
            rightOut[i] = mixR * gain;
        }
    }

public:
    /// Get latency in samples
    /// Granular delay has no inherent latency (grains tap delay buffer)
    [[nodiscard]] size_t getLatencySamples() const noexcept { return 0; }

    /// Get current active grain count
    [[nodiscard]] size_t activeGrainCount() const noexcept {
        return engine_.activeGrainCount();
    }

    /// Seed RNG for reproducible behavior (testing)
    void seed(uint32_t seedValue) noexcept { engine_.seed(seedValue); }

private:
    GranularEngine engine_;

    // Feedback state
    float feedbackL_ = 0.0f;
    float feedbackR_ = 0.0f;

    // Output smoothers
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother gainSmoother_;

    // Raw parameter values
    float feedback_ = 0.0f;
    float dryWet_ = 0.5f;
    float outputGainLinear_ = 1.0f;

    double sampleRate_ = 44100.0;

    // Tempo sync state (spec 038)
    TimeMode timeMode_ = TimeMode::Free;  // Default: Free mode (milliseconds)
    int noteValueIndex_ = 4;              // Default: 1/8 note (index 4)
};

}  // namespace Iterum::DSP
