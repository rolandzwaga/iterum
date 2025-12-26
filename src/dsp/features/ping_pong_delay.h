// ==============================================================================
// Layer 4: User Feature - PingPongDelay
// ==============================================================================
// Classic stereo ping-pong delay with alternating L/R bounces.
// Features L/R timing ratios, cross-feedback control, stereo width, tempo sync,
// and optional LFO modulation.
//
// Composes:
// - DelayLine (Layer 1): 2 instances for independent L/R delay buffers
// - LFO (Layer 1): 2 instances for stereo modulation (90° phase offset)
// - OnePoleSmoother (Layer 1): 8 instances for parameter smoothing
// - DynamicsProcessor (Layer 2): Feedback limiting for >100%
// - stereoCrossBlend (Layer 0): Cross-feedback routing
//
// Feature: 027-ping-pong-delay
// Layer: 4 (User Feature)
// Reference: specs/027-ping-pong-delay/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 4 (composes only from Layer 0-3)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include "dsp/core/block_context.h"
#include "dsp/core/db_utils.h"
#include "dsp/core/note_value.h"
#include "dsp/core/stereo_utils.h"
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/lfo.h"
#include "dsp/primitives/smoother.h"
#include "dsp/processors/dynamics_processor.h"
#include "dsp/systems/delay_engine.h"  // For TimeMode enum

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// LRRatio Enumeration (FR-005)
// =============================================================================

/// @brief Preset L/R timing ratios for polyrhythmic ping-pong effects
///
/// Each ratio defines multipliers for left and right delay times:
/// - OneToOne: Classic even ping-pong (L=1.0, R=1.0)
/// - TwoToOne: Right is double speed (L=1.0, R=0.5)
/// - ThreeToTwo: Polyrhythmic triplet feel (L=1.0, R=0.667)
/// - FourToThree: Subtle polyrhythm (L=1.0, R=0.75)
/// - OneToTwo: Left is double speed (L=0.5, R=1.0)
/// - TwoToThree: Inverse triplet feel (L=0.667, R=1.0)
/// - ThreeToFour: Inverse subtle polyrhythm (L=0.75, R=1.0)
enum class LRRatio : uint8_t {
    OneToOne = 0,     ///< 1:1 - Classic even ping-pong
    TwoToOne = 1,     ///< 2:1 - R is double speed
    ThreeToTwo = 2,   ///< 3:2 - Polyrhythmic triplet feel
    FourToThree = 3,  ///< 4:3 - Subtle polyrhythm
    OneToTwo = 4,     ///< 1:2 - L is double speed
    TwoToThree = 5,   ///< 2:3 - Inverse triplet feel
    ThreeToFour = 6   ///< 3:4 - Inverse subtle polyrhythm
};

// =============================================================================
// PingPongDelay Class
// =============================================================================

/// @brief Layer 4 User Feature - Ping-Pong Delay
///
/// Classic stereo ping-pong delay with alternating left/right bounces.
/// Features L/R timing ratios for polyrhythmic patterns, adjustable cross-feedback,
/// stereo width control (0-200%), tempo sync, and optional LFO modulation.
///
/// @par User Controls
/// - Time: Delay time 1-10000ms with tempo sync option (FR-001 to FR-004)
/// - Ratio: L/R timing ratios for polyrhythmic effects (FR-005 to FR-008)
/// - Cross-Feedback: 0% (dual mono) to 100% (full ping-pong) (FR-009 to FR-013)
/// - Width: Stereo width 0% (mono) to 200% (ultra-wide) (FR-014 to FR-018)
/// - Modulation: LFO depth 0-100%, rate 0.1-10Hz (FR-019 to FR-023)
/// - Mix: Dry/wet balance 0-100% (FR-024, FR-027)
/// - Output Level: -inf to +12dB (FR-025)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 4 composes from Layer 0-3 only
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// PingPongDelay delay;
/// delay.prepare(44100.0, 512, 2000.0f);
/// delay.setDelayTimeMs(500.0f);
/// delay.setFeedback(0.5f);
/// delay.setCrossFeedback(1.0f);  // Full ping-pong
/// delay.setLRRatio(LRRatio::TwoToOne);
///
/// // In process callback
/// delay.process(left, right, numSamples, ctx);
/// @endcode
class PingPongDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinDelayMs = 1.0f;           ///< Minimum delay (FR-001)
    static constexpr float kMaxDelayMs = 10000.0f;       ///< Maximum delay (FR-001)
    static constexpr float kDefaultDelayMs = 500.0f;     ///< Default delay
    static constexpr float kDefaultFeedback = 0.5f;      ///< Default feedback
    static constexpr float kDefaultCrossFeedback = 1.0f; ///< Default cross-feedback (full ping-pong)
    static constexpr float kDefaultWidth = 100.0f;       ///< Default width (natural stereo)
    static constexpr float kDefaultMix = 0.5f;           ///< Default mix
    static constexpr float kSmoothingTimeMs = 20.0f;     ///< Parameter smoothing (FR-026)
    static constexpr size_t kMaxDryBufferSize = 8192;    ///< Max samples for dry buffer

    // Limiter constants (for feedback > 100%)
    static constexpr float kLimiterThresholdDb = -0.5f;  ///< Limiter threshold
    static constexpr float kLimiterRatio = 100.0f;       ///< True limiting ratio
    static constexpr float kLimiterKneeDb = 6.0f;        ///< Soft knee width

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    PingPongDelay() noexcept = default;
    ~PingPongDelay() = default;

    // Non-copyable, movable
    PingPongDelay(const PingPongDelay&) = delete;
    PingPongDelay& operator=(const PingPongDelay&) = delete;
    PingPongDelay(PingPongDelay&&) noexcept = default;
    PingPongDelay& operator=(PingPongDelay&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    /// @brief Prepare for processing (allocates memory)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxDelayMs Maximum delay time in milliseconds
    /// @post Ready for process() calls
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        maxDelayMs_ = std::min(maxDelayMs, kMaxDelayMs);

        // Convert to seconds for DelayLine
        const float maxDelaySeconds = maxDelayMs_ / 1000.0f;

        // Prepare delay lines (Layer 1)
        delayLineL_.prepare(sampleRate, maxDelaySeconds);
        delayLineR_.prepare(sampleRate, maxDelaySeconds);

        // Prepare modulation LFOs (Layer 1)
        lfoL_.prepare(sampleRate);
        lfoR_.prepare(sampleRate);
        lfoL_.setWaveform(Waveform::Sine);
        lfoR_.setWaveform(Waveform::Sine);
        lfoR_.setPhaseOffset(90.0f);  // 90° phase offset for stereo modulation

        // Prepare limiter (Layer 2) - for feedback > 100%
        limiter_.prepare(sampleRate, maxBlockSize);
        limiter_.setThreshold(kLimiterThresholdDb);
        limiter_.setRatio(kLimiterRatio);
        limiter_.setKneeWidth(kLimiterKneeDb);
        limiter_.setDetectionMode(DynamicsDetectionMode::Peak);

        // Configure smoothers (Layer 1)
        timeSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        feedbackSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        crossFeedbackSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        widthSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        mixSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        outputLevelSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        modulationDepthSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));

        // Initialize to defaults
        timeSmoother_.snapTo(kDefaultDelayMs);
        feedbackSmoother_.snapTo(kDefaultFeedback);
        crossFeedbackSmoother_.snapTo(kDefaultCrossFeedback);
        widthSmoother_.snapTo(kDefaultWidth);
        mixSmoother_.snapTo(kDefaultMix);
        outputLevelSmoother_.snapTo(1.0f);
        modulationDepthSmoother_.snapTo(0.0f);

        prepared_ = true;
    }

    /// @brief Reset all internal state
    /// @post Delay lines cleared, smoothers snapped to current values
    void reset() noexcept {
        delayLineL_.reset();
        delayLineR_.reset();
        lfoL_.reset();
        lfoR_.reset();
        limiter_.reset();

        // Snap all smoothers to their current target values for instant response
        timeSmoother_.snapTo(delayTimeMs_);
        feedbackSmoother_.snapTo(feedback_);
        crossFeedbackSmoother_.snapTo(crossFeedback_);
        widthSmoother_.snapTo(width_);
        mixSmoother_.snapTo(mix_);
        outputLevelSmoother_.snapTo(dbToGain(outputLevelDb_));
        modulationDepthSmoother_.snapTo(modulationDepth_);
    }

    /// @brief Snap all smoothers to current targets (for immediate parameter application)
    /// @note Call after setting multiple parameters for tests or preset loads
    void snapParameters() noexcept {
        timeSmoother_.snapTo(delayTimeMs_);
        feedbackSmoother_.snapTo(feedback_);
        crossFeedbackSmoother_.snapTo(crossFeedback_);
        widthSmoother_.snapTo(width_);
        mixSmoother_.snapTo(mix_);
        outputLevelSmoother_.snapTo(dbToGain(outputLevelDb_));
        modulationDepthSmoother_.snapTo(modulationDepth_);
    }

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Time Control (FR-001 to FR-004)
    // =========================================================================

    /// @brief Set delay time in milliseconds
    /// @param ms Delay time in milliseconds [1, 10000]
    void setDelayTimeMs(float ms) noexcept {
        ms = std::clamp(ms, kMinDelayMs, maxDelayMs_);
        delayTimeMs_ = ms;
        timeSmoother_.setTarget(ms);
    }

    /// @brief Get current delay time
    [[nodiscard]] float getDelayTimeMs() const noexcept {
        return delayTimeMs_;
    }

    /// @brief Set time mode (free or synced)
    /// @param mode TimeMode::Free or TimeMode::Synced
    void setTimeMode(TimeMode mode) noexcept {
        timeMode_ = mode;
    }

    /// @brief Get current time mode
    [[nodiscard]] TimeMode getTimeMode() const noexcept {
        return timeMode_;
    }

    /// @brief Set note value for tempo sync (FR-003)
    /// @param note Note value (quarter, eighth, etc.)
    /// @param modifier Note modifier (none, dotted, triplet)
    void setNoteValue(NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept {
        noteValue_ = note;
        noteModifier_ = modifier;
    }

    /// @brief Get current note value
    [[nodiscard]] NoteValue getNoteValue() const noexcept {
        return noteValue_;
    }

    // =========================================================================
    // L/R Ratio Control (FR-005 to FR-008)
    // =========================================================================

    /// @brief Set L/R timing ratio
    /// @param ratio Preset ratio selection
    void setLRRatio(LRRatio ratio) noexcept {
        lrRatio_ = ratio;
    }

    /// @brief Get current L/R ratio
    [[nodiscard]] LRRatio getLRRatio() const noexcept {
        return lrRatio_;
    }

    // =========================================================================
    // Feedback Control (FR-009 to FR-013)
    // =========================================================================

    /// @brief Set feedback amount
    /// @param amount Feedback [0, 1.2] (>1.0 enables self-oscillation with limiting)
    void setFeedback(float amount) noexcept {
        feedback_ = std::clamp(amount, 0.0f, 1.2f);
        feedbackSmoother_.setTarget(feedback_);
    }

    /// @brief Get current feedback amount
    [[nodiscard]] float getFeedback() const noexcept {
        return feedback_;
    }

    /// @brief Set cross-feedback amount
    /// @param amount Cross-feedback [0, 1] (0 = dual mono, 1 = full ping-pong)
    void setCrossFeedback(float amount) noexcept {
        crossFeedback_ = std::clamp(amount, 0.0f, 1.0f);
        crossFeedbackSmoother_.setTarget(crossFeedback_);
    }

    /// @brief Get current cross-feedback amount
    [[nodiscard]] float getCrossFeedback() const noexcept {
        return crossFeedback_;
    }

    // =========================================================================
    // Stereo Width Control (FR-014 to FR-018)
    // =========================================================================

    /// @brief Set stereo width
    /// @param widthPercent Width [0, 200] (0 = mono, 100 = natural, 200 = ultra-wide)
    void setWidth(float widthPercent) noexcept {
        width_ = std::clamp(widthPercent, 0.0f, 200.0f);
        widthSmoother_.setTarget(width_);
    }

    /// @brief Get current stereo width
    [[nodiscard]] float getWidth() const noexcept {
        return width_;
    }

    // =========================================================================
    // Modulation Control (FR-019 to FR-023)
    // =========================================================================

    /// @brief Set modulation depth
    /// @param depth Modulation depth [0, 1]
    void setModulationDepth(float depth) noexcept {
        modulationDepth_ = std::clamp(depth, 0.0f, 1.0f);
        modulationDepthSmoother_.setTarget(modulationDepth_);
    }

    /// @brief Get modulation depth
    [[nodiscard]] float getModulationDepth() const noexcept {
        return modulationDepth_;
    }

    /// @brief Set modulation rate
    /// @param rateHz Rate in Hz [0.1, 10]
    void setModulationRate(float rateHz) noexcept {
        rateHz = std::clamp(rateHz, 0.1f, 10.0f);
        modulationRate_ = rateHz;
        lfoL_.setFrequency(rateHz);
        lfoR_.setFrequency(rateHz);
    }

    /// @brief Get modulation rate
    [[nodiscard]] float getModulationRate() const noexcept {
        return modulationRate_;
    }

    // =========================================================================
    // Mix and Output (FR-024 to FR-027)
    // =========================================================================

    /// @brief Set dry/wet mix
    /// @param amount Mix [0, 1] (0 = dry, 1 = wet)
    void setMix(float amount) noexcept {
        mix_ = std::clamp(amount, 0.0f, 1.0f);
        mixSmoother_.setTarget(mix_);
    }

    /// @brief Get mix amount
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    /// @brief Set output level
    /// @param dB Output level in dB [-120, +12]
    void setOutputLevel(float dB) noexcept {
        outputLevelDb_ = std::clamp(dB, -120.0f, 12.0f);
        outputLevelSmoother_.setTarget(dbToGain(outputLevelDb_));
    }

    /// @brief Get output level
    [[nodiscard]] float getOutputLevel() const noexcept {
        return outputLevelDb_;
    }

    // =========================================================================
    // Processing (FR-028 to FR-034)
    // =========================================================================

    /// @brief Process stereo audio in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @param ctx Block context with tempo/transport info
    /// @pre prepare() has been called
    /// @note noexcept, allocation-free
    void process(float* left, float* right, size_t numSamples,
                 const BlockContext& ctx) noexcept {
        if (!prepared_ || numSamples == 0) return;

        // Store dry signal for mixing
        for (size_t i = 0; i < numSamples && i < kMaxDryBufferSize; ++i) {
            dryBufferL_[i] = left[i];
            dryBufferR_[i] = right[i];
        }

        // Calculate base delay time (tempo sync or free)
        float baseDelayMs = delayTimeMs_;
        if (timeMode_ == TimeMode::Synced) {
            baseDelayMs = calculateTempoSyncedDelay(ctx);
        }

        // Get ratio multipliers
        float leftMult, rightMult;
        getRatioMultipliers(lrRatio_, leftMult, rightMult);

        // Sample-by-sample processing
        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed parameters
            const float currentDelayMs = timeSmoother_.process();
            const float currentFeedback = feedbackSmoother_.process();
            const float currentCrossFeedback = crossFeedbackSmoother_.process();
            const float currentWidth = widthSmoother_.process();
            const float currentMix = mixSmoother_.process();
            const float currentOutputGain = outputLevelSmoother_.process();
            const float currentModDepth = modulationDepthSmoother_.process();

            // Calculate L/R delay times with ratio
            float useDelayMs = (timeMode_ == TimeMode::Synced) ? baseDelayMs : currentDelayMs;
            float leftDelayMs = useDelayMs * leftMult;
            float rightDelayMs = useDelayMs * rightMult;

            // Apply modulation if enabled
            if (currentModDepth > 0.0f) {
                float modL = lfoL_.process() * currentModDepth * 0.1f * leftDelayMs;
                float modR = lfoR_.process() * currentModDepth * 0.1f * rightDelayMs;
                leftDelayMs = std::clamp(leftDelayMs + modL, kMinDelayMs, maxDelayMs_);
                rightDelayMs = std::clamp(rightDelayMs + modR, kMinDelayMs, maxDelayMs_);
            } else {
                // Keep LFOs running even when not applied
                (void)lfoL_.process();
                (void)lfoR_.process();
            }

            // Convert to samples
            float leftDelaySamples = msToSamples(leftDelayMs);
            float rightDelaySamples = msToSamples(rightDelayMs);

            // Read delayed samples
            float delayedL = delayLineL_.readLinear(leftDelaySamples);
            float delayedR = delayLineR_.readLinear(rightDelaySamples);

            // Apply feedback
            float feedbackL = delayedL * currentFeedback;
            float feedbackR = delayedR * currentFeedback;

            // Apply limiting if feedback > 100%
            if (currentFeedback > 1.0f) {
                feedbackL = softLimit(feedbackL);
                feedbackR = softLimit(feedbackR);
            }

            // Cross inputs for ping-pong routing (stereoCrossBlend from Layer 0)
            // At crossAmount=1.0: left input → right delay, right input → left delay
            // This makes first echo appear on opposite channel with correct timing
            float writeL, writeR;
            stereoCrossBlend(left[i] + feedbackL, right[i] + feedbackR,
                           currentCrossFeedback, writeL, writeR);
            delayLineL_.write(writeL);
            delayLineR_.write(writeR);

            // Apply stereo width using M/S technique
            // Delay outputs go directly to their respective channels (no output crossing)
            const float mid = (delayedL + delayedR) * 0.5f;
            const float side = (delayedL - delayedR) * 0.5f * (currentWidth / 100.0f);
            float wetL = mid + side;
            float wetR = mid - side;

            // Mix dry/wet
            const size_t bufIdx = i % kMaxDryBufferSize;
            left[i] = (dryBufferL_[bufIdx] * (1.0f - currentMix) + wetL * currentMix) * currentOutputGain;
            right[i] = (dryBufferR_[bufIdx] * (1.0f - currentMix) + wetR * currentMix) * currentOutputGain;
        }
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Get L/R multipliers for a ratio preset
    static void getRatioMultipliers(LRRatio ratio, float& leftMult, float& rightMult) noexcept {
        switch (ratio) {
            case LRRatio::OneToOne:
                leftMult = 1.0f; rightMult = 1.0f;
                break;
            case LRRatio::TwoToOne:
                leftMult = 1.0f; rightMult = 0.5f;
                break;
            case LRRatio::ThreeToTwo:
                leftMult = 1.0f; rightMult = 2.0f / 3.0f;
                break;
            case LRRatio::FourToThree:
                leftMult = 1.0f; rightMult = 0.75f;
                break;
            case LRRatio::OneToTwo:
                leftMult = 0.5f; rightMult = 1.0f;
                break;
            case LRRatio::TwoToThree:
                leftMult = 2.0f / 3.0f; rightMult = 1.0f;
                break;
            case LRRatio::ThreeToFour:
                leftMult = 0.75f; rightMult = 1.0f;
                break;
            default:
                leftMult = 1.0f; rightMult = 1.0f;
                break;
        }
    }

    /// @brief Calculate tempo-synced delay time
    [[nodiscard]] float calculateTempoSyncedDelay(const BlockContext& ctx) const noexcept {
        size_t delaySamples = ctx.tempoToSamples(noteValue_, noteModifier_);
        return static_cast<float>(delaySamples * 1000.0 / ctx.sampleRate);
    }

    /// @brief Convert milliseconds to samples
    [[nodiscard]] float msToSamples(float ms) const noexcept {
        return static_cast<float>(ms * sampleRate_ / 1000.0);
    }

    /// @brief Simple soft limiter for feedback > 100%
    [[nodiscard]] static float softLimit(float x) noexcept {
        // tanh-based soft limiting
        return std::tanh(x);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    float maxDelayMs_ = kMaxDelayMs;
    bool prepared_ = false;

    // Layer 1 primitives
    DelayLine delayLineL_;
    DelayLine delayLineR_;
    LFO lfoL_;
    LFO lfoR_;

    // Layer 2 processors
    DynamicsProcessor limiter_;

    // Smoothers
    OnePoleSmoother timeSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother crossFeedbackSmoother_;
    OnePoleSmoother widthSmoother_;
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother outputLevelSmoother_;
    OnePoleSmoother modulationDepthSmoother_;

    // Parameters
    float delayTimeMs_ = kDefaultDelayMs;
    float feedback_ = kDefaultFeedback;
    float crossFeedback_ = kDefaultCrossFeedback;
    float width_ = kDefaultWidth;
    float mix_ = kDefaultMix;
    float outputLevelDb_ = 0.0f;
    float modulationDepth_ = 0.0f;
    float modulationRate_ = 1.0f;

    // Mode selections
    TimeMode timeMode_ = TimeMode::Free;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;
    LRRatio lrRatio_ = LRRatio::OneToOne;

    // Dry signal buffer for mixing (avoid allocation in process)
    std::array<float, kMaxDryBufferSize> dryBufferL_ = {};
    std::array<float, kMaxDryBufferSize> dryBufferR_ = {};
};

} // namespace DSP
} // namespace Iterum
