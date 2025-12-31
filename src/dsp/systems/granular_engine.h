// Layer 3: System Component - Granular Engine
// Part of Granular Delay feature (spec 034)
#pragma once

#include "dsp/core/grain_envelope.h"
#include "dsp/core/random.h"
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/grain_pool.h"
#include "dsp/primitives/smoother.h"
#include "dsp/processors/grain_processor.h"
#include "dsp/processors/grain_scheduler.h"

#include <cmath>

namespace Iterum::DSP {

/// Core granular processing engine combining pool, scheduler, and buffer.
/// This is the main granular synthesis component that manages all grain
/// lifecycle and processing.
class GranularEngine {
public:
    static constexpr float kDefaultMaxDelaySeconds = 2.0f;
    static constexpr float kDefaultSmoothTimeMs = 20.0f;
    static constexpr float kFreezeCrossfadeMs = 50.0f;

    /// Prepare engine for processing
    /// @param sampleRate Current sample rate
    /// @param maxDelaySeconds Maximum delay buffer length in seconds
    void prepare(double sampleRate, float maxDelaySeconds = kDefaultMaxDelaySeconds) noexcept {
        sampleRate_ = sampleRate;

        // Prepare delay buffers
        delayL_.prepare(sampleRate, maxDelaySeconds);
        delayR_.prepare(sampleRate, maxDelaySeconds);

        // Prepare grain components
        pool_.prepare(sampleRate);
        scheduler_.prepare(sampleRate);
        processor_.prepare(sampleRate);

        // Configure parameter smoothers
        grainSizeSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));
        pitchSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));
        positionSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));

        // Configure gain scaling smoother (very fast response to track grain count changes)
        // Use 2ms for fast response while still avoiding clicks
        gainScaleSmoother_.configure(2.0f, static_cast<float>(sampleRate));

        // Configure freeze crossfade
        freezeCrossfade_.configure(kFreezeCrossfadeMs, static_cast<float>(sampleRate));

        reset();
    }

    /// Reset engine state
    void reset() noexcept {
        delayL_.reset();
        delayR_.reset();
        pool_.reset();
        scheduler_.reset();
        processor_.reset();

        // Snap smoothers to current values
        grainSizeSmoother_.snapTo(grainSizeMs_);
        pitchSmoother_.snapTo(pitchSemitones_);
        positionSmoother_.snapTo(positionMs_);

        // Snap gain scaling to 1.0 (no grains active after reset)
        gainScaleSmoother_.snapTo(1.0f);

        freezeCrossfade_.snapTo(frozen_ ? 1.0f : 0.0f);
        currentSample_ = 0;
    }

    // === Parameter Setters ===

    /// Set grain size in milliseconds (10-500ms)
    void setGrainSize(float ms) noexcept {
        grainSizeMs_ = std::clamp(ms, 10.0f, 500.0f);
        grainSizeSmoother_.setTarget(grainSizeMs_);
    }

    /// Set grain density (grains per second, 1-100 Hz)
    void setDensity(float grainsPerSecond) noexcept {
        density_ = std::clamp(grainsPerSecond, 1.0f, 100.0f);
        scheduler_.setDensity(density_);
    }

    /// Set base pitch shift in semitones (-24 to +24)
    void setPitch(float semitones) noexcept {
        pitchSemitones_ = std::clamp(semitones, -24.0f, 24.0f);
        pitchSmoother_.setTarget(pitchSemitones_);
    }

    /// Set pitch spray/randomization (0-1)
    void setPitchSpray(float amount) noexcept {
        pitchSpray_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Set base delay position in milliseconds (0-2000ms)
    void setPosition(float ms) noexcept {
        positionMs_ = std::clamp(ms, 0.0f, 2000.0f);
        positionSmoother_.setTarget(positionMs_);
    }

    /// Set position spray/randomization (0-1)
    void setPositionSpray(float amount) noexcept {
        positionSpray_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Set reverse playback probability (0-1)
    void setReverseProbability(float probability) noexcept {
        reverseProbability_ = std::clamp(probability, 0.0f, 1.0f);
    }

    /// Set pan spray/randomization (0-1)
    void setPanSpray(float amount) noexcept {
        panSpray_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Set timing jitter (0-1)
    /// Controls randomness of grain timing: 0 = regular, 1 = maximum randomness (±50%)
    void setJitter(float amount) noexcept {
        scheduler_.setJitter(amount);
    }

    /// Set envelope type for new grains
    void setEnvelopeType(GrainEnvelopeType type) noexcept {
        envelopeType_ = type;
        processor_.setEnvelopeType(type);
    }

    /// Set pitch quantization mode (Phase 2.2)
    void setPitchQuantMode(PitchQuantMode mode) noexcept {
        pitchQuantMode_ = mode;
    }

    /// Get current pitch quantization mode
    [[nodiscard]] PitchQuantMode getPitchQuantMode() const noexcept {
        return pitchQuantMode_;
    }

    /// Set texture/chaos amount (Phase 2.3)
    /// Controls grain amplitude variation: 0 = uniform, 1 = maximum variation
    void setTexture(float amount) noexcept {
        texture_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Get current texture amount
    [[nodiscard]] float getTexture() const noexcept {
        return texture_;
    }

    /// Enable/disable freeze mode
    void setFreeze(bool frozen) noexcept {
        if (frozen != frozen_) {
            frozen_ = frozen;
            freezeCrossfade_.setTarget(frozen ? 1.0f : 0.0f);
        }
    }

    /// Check if frozen
    [[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

    // === Processing ===

    /// Process stereo audio (most common case)
    /// @param inputL Left input sample
    /// @param inputR Right input sample
    /// @param outputL Reference to left output sample
    /// @param outputR Reference to right output sample
    void process(float inputL, float inputR,
                 float& outputL, float& outputR) noexcept {
        // Get smoothed parameters
        const float smoothedGrainSize = grainSizeSmoother_.process();
        const float smoothedPitch = pitchSmoother_.process();
        const float smoothedPosition = positionSmoother_.process();
        const float freezeAmount = freezeCrossfade_.process();

        // Write to delay buffers (unless fully frozen)
        if (freezeAmount < 1.0f) {
            // Crossfade: blend new input with existing buffer during transition
            const float writeAmount = 1.0f - freezeAmount;
            delayL_.write(inputL * writeAmount);
            delayR_.write(inputR * writeAmount);
        } else if (!frozen_) {
            // Not frozen, write normally
            delayL_.write(inputL);
            delayR_.write(inputR);
        }

        // Check if we should trigger a new grain
        if (scheduler_.process()) {
            triggerNewGrain(smoothedGrainSize, smoothedPitch, smoothedPosition);
        }

        // Process all active grains
        float sumL = 0.0f;
        float sumR = 0.0f;
        size_t activeCount = 0;

        for (Grain* grain : pool_.activeGrains()) {
            if (grain->active) {
                auto [grainL, grainR] = processor_.processGrain(*grain, delayL_, delayR_);
                sumL += grainL;
                sumR += grainR;
                ++activeCount;

                // Check if grain completed
                if (processor_.isGrainComplete(*grain)) {
                    pool_.releaseGrain(grain);
                }
            }
        }

        // Apply 1/sqrt(n) gain scaling to prevent output explosion from overlapping grains
        // This keeps output level roughly constant regardless of how many grains overlap
        // Smooth the gain factor to avoid clicks when grain count changes
        const float targetGain = (activeCount > 0)
            ? 1.0f / std::sqrt(static_cast<float>(activeCount))
            : 1.0f;
        gainScaleSmoother_.setTarget(targetGain);
        const float smoothedGain = gainScaleSmoother_.process();

        outputL = sumL * smoothedGain;
        outputR = sumR * smoothedGain;

        ++currentSample_;
    }

    /// Get current active grain count
    [[nodiscard]] size_t activeGrainCount() const noexcept {
        return pool_.activeCount();
    }

    /// Seed RNG for reproducible behavior (testing)
    void seed(uint32_t seedValue) noexcept {
        rng_ = Xorshift32(seedValue);
        scheduler_.seed(seedValue + 1);
    }

private:
    void triggerNewGrain(float grainSizeMs, float pitchSemitones,
                         float positionMs) noexcept {
        Grain* grain = pool_.acquireGrain(currentSample_);
        if (grain == nullptr) {
            return;  // No grain available (shouldn't happen with stealing)
        }

        // Apply randomization (spray)
        float effectivePitch = pitchSemitones;
        if (pitchSpray_ > 0.0f) {
            // Random offset: +/- (pitchSpray * 24) semitones
            effectivePitch += pitchSpray_ * 24.0f * rng_.nextFloat();
        }

        // Apply pitch quantization (Phase 2.2)
        effectivePitch = quantizePitch(effectivePitch, pitchQuantMode_);

        float effectivePositionMs = positionMs;
        if (positionSpray_ > 0.0f) {
            // Random offset: 0 to (positionSpray * positionMs)
            effectivePositionMs += positionSpray_ * positionMs * rng_.nextUnipolar();
        }

        float pan = 0.0f;
        if (panSpray_ > 0.0f) {
            // Random pan: -panSpray to +panSpray
            pan = panSpray_ * rng_.nextFloat();
        }

        const bool reverse = (rng_.nextUnipolar() < reverseProbability_);

        // Convert position from ms to samples
        const float positionSamples =
            effectivePositionMs * static_cast<float>(sampleRate_) / 1000.0f;

        GrainParams params{
            .grainSizeMs = grainSizeMs,
            .pitchSemitones = effectivePitch,
            .positionSamples = positionSamples,
            .pan = pan,
            .reverse = reverse,
            .envelopeType = envelopeType_
        };

        processor_.initializeGrain(*grain, params);

        // Apply texture-based amplitude variation (Phase 2.3)
        // At texture=0: amplitude is always 1.0 (uniform)
        // At texture=1: amplitude ranges from 0.2 to 1.0 (maximum variation)
        if (texture_ > 0.0f) {
            const float minAmplitude = 1.0f - texture_ * 0.8f;  // Range: 1.0 to 0.2
            grain->amplitude = minAmplitude + rng_.nextUnipolar() * (1.0f - minAmplitude);
        }
    }

    // Components
    DelayLine delayL_;
    DelayLine delayR_;
    GrainPool pool_;
    GrainScheduler scheduler_;
    GrainProcessor processor_;
    Xorshift32 rng_{54321};

    // Parameter smoothers
    OnePoleSmoother grainSizeSmoother_;
    OnePoleSmoother pitchSmoother_;
    OnePoleSmoother positionSmoother_;

    // Output gain scaling smoother (1/sqrt(n) compensation for overlapping grains)
    OnePoleSmoother gainScaleSmoother_;

    // Freeze crossfade
    LinearRamp freezeCrossfade_;
    bool frozen_ = false;

    // Current parameter values (raw, pre-smoothing)
    float grainSizeMs_ = 100.0f;
    float density_ = 10.0f;
    float pitchSemitones_ = 0.0f;
    float pitchSpray_ = 0.0f;
    float positionMs_ = 500.0f;
    float positionSpray_ = 0.0f;
    float reverseProbability_ = 0.0f;
    float panSpray_ = 0.0f;
    GrainEnvelopeType envelopeType_ = GrainEnvelopeType::Hann;
    PitchQuantMode pitchQuantMode_ = PitchQuantMode::Off;  // Phase 2.2
    float texture_ = 0.0f;  // Phase 2.3: grain amplitude variation

    size_t currentSample_ = 0;
    double sampleRate_ = 44100.0;
};

}  // namespace Iterum::DSP
