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

        // Configure smoothers
        feedbackSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));
        dryWetSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));

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

    /// Set timing jitter (0-1)
    /// Controls randomness of grain timing: 0 = regular, 1 = maximum randomness
    void setJitter(float amount) noexcept { engine_.setJitter(amount); }

    /// Set pitch quantization mode (Phase 2.2)
    void setPitchQuantMode(PitchQuantMode mode) noexcept { engine_.setPitchQuantMode(mode); }

    /// Set texture/chaos amount (Phase 2.3)
    /// Controls grain amplitude variation: 0 = uniform, 1 = maximum variation
    void setTexture(float amount) noexcept { engine_.setTexture(amount); }

    /// Set stereo width (Phase 2.4)
    /// 0 = mono (L and R identical), 1 = full stereo (normal stereo image)
    void setStereoWidth(float amount) noexcept {
        stereoWidth_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Get current stereo width
    [[nodiscard]] float getStereoWidth() const noexcept { return stereoWidth_; }

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

            // Mix input with feedback
            float inputL = leftIn[i];
            float inputR = rightIn[i];

            // Add feedback with ALWAYS-ON soft limiting to prevent runaway
            // This is critical for stability with overlapping grains
            if (feedback > 0.0f) {
                // Always apply tanh to prevent accumulation, even at low feedback
                // Scale by 2.0 before tanh to preserve more dynamic range at low levels
                const float fbL = std::tanh(feedbackL_ * feedback * 0.5f) * 2.0f;
                const float fbR = std::tanh(feedbackR_ * feedback * 0.5f) * 2.0f;

                inputL += fbL;
                inputR += fbR;
            }

            // Process through granular engine
            float wetL = 0.0f;
            float wetR = 0.0f;
            engine_.process(inputL, inputR, wetL, wetR);

            // Apply soft limiter to wet output before storing for feedback
            // This prevents extreme values from entering the feedback loop
            const float limitedWetL = std::tanh(wetL * 0.5f) * 2.0f;
            const float limitedWetR = std::tanh(wetR * 0.5f) * 2.0f;

            // Store limited values for feedback
            feedbackL_ = limitedWetL;
            feedbackR_ = limitedWetR;

            // Dry/wet mix (use limited wet for output as well)
            const float dryL = leftIn[i] * (1.0f - dryWet);
            const float dryR = rightIn[i] * (1.0f - dryWet);

            float mixedL = dryL + limitedWetL * dryWet;
            float mixedR = dryR + limitedWetR * dryWet;

            // Apply stereo width (Phase 2.4)
            // At width=0: mono (L == R == mid)
            // At width=1: full stereo (unchanged)
            if (stereoWidth_ < 1.0f) {
                const float mid = (mixedL + mixedR) * 0.5f;
                const float side = (mixedL - mixedR) * 0.5f;
                mixedL = mid + stereoWidth_ * side;
                mixedR = mid - stereoWidth_ * side;
            }

            leftOut[i] = mixedL;
            rightOut[i] = mixedR;
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

    // Smoothers
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother dryWetSmoother_;

    // Raw parameter values
    float feedback_ = 0.0f;
    float dryWet_ = 0.5f;

    double sampleRate_ = 44100.0;

    // Tempo sync state (spec 038)
    TimeMode timeMode_ = TimeMode::Free;  // Default: Free mode (milliseconds)
    int noteValueIndex_ = 4;              // Default: 1/8 note (index 4)

    // Phase 2.4: Stereo width (0 = mono, 1 = full stereo)
    float stereoWidth_ = 1.0f;
};

}  // namespace Iterum::DSP
