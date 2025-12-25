// ==============================================================================
// Layer 4: User Feature - TapeDelay
// ==============================================================================
// Classic tape delay emulation composing Layer 3 components.
// Emulates vintage tape echo units (Roland RE-201, Echoplex, Watkins Copicat).
//
// Composes:
// - TapManager (Layer 3): Multi-head echo patterns
// - FeedbackNetwork (Layer 3): Feedback with filtering and saturation
// - CharacterProcessor (Layer 3): Tape character (wow/flutter, hiss, rolloff)
//
// Feature: 024-tape-delay
// Layer: 4 (User Feature)
// Reference: specs/024-tape-delay/spec.md
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
#include "dsp/primitives/smoother.h"
#include "dsp/systems/character_processor.h"
#include "dsp/systems/feedback_network.h"
#include "dsp/systems/tap_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// TapeHead Structure (FR-015 to FR-020)
// =============================================================================

/// @brief Configuration for a single tape playback head
///
/// Represents one of the 3 playback heads (like RE-201 Space Echo).
/// Head timing is relative to Motor Speed via the ratio field.
struct TapeHead {
    float ratio = 1.0f;        ///< Timing ratio (1.0, 1.5, 2.0 typical)
    float levelDb = 0.0f;      ///< Output level [-96, +6] dB (FR-017)
    float pan = 0.0f;          ///< Stereo position [-100, +100] (FR-018)
    bool enabled = true;       ///< Head output enable (FR-016)
};

// =============================================================================
// MotorController Class (FR-001 to FR-004)
// =============================================================================

/// @brief Manages delay time with motor inertia simulation
///
/// Provides realistic tape machine behavior where delay time changes smoothly
/// with pitch artifacts, like a real tape motor speeding up or slowing down.
///
/// @par Motor Inertia
/// - Default transition time: 300ms (configurable 100-1000ms)
/// - Creates pitch sweep during transitions (tape speed-up/slow-down effect)
class MotorController {
public:
    static constexpr float kDefaultInertiaMs = 300.0f;
    static constexpr float kMinInertiaMs = 100.0f;
    static constexpr float kMaxInertiaMs = 1000.0f;

    MotorController() noexcept = default;

    /// @brief Prepare for processing
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process call
    void prepare(float sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        delaySmoother_.configure(kDefaultInertiaMs, sampleRate);
        delaySmoother_.snapTo(0.0f);
        inertiaTimeMs_ = kDefaultInertiaMs;
    }

    /// @brief Reset to current target
    void reset() noexcept {
        delaySmoother_.snapToTarget();
    }

    /// @brief Set target delay time in milliseconds
    /// @param ms Delay time [0, maxDelay]
    void setTargetDelayMs(float ms) noexcept {
        targetDelayMs_ = ms;
        delaySmoother_.setTarget(ms);
    }

    /// @brief Get current (smoothed) delay time
    [[nodiscard]] float getCurrentDelayMs() const noexcept {
        return delaySmoother_.getCurrentValue();
    }

    /// @brief Get target delay time
    [[nodiscard]] float getTargetDelayMs() const noexcept {
        return targetDelayMs_;
    }

    /// @brief Set motor inertia time
    /// @param ms Transition time [100, 1000]
    void setInertiaTimeMs(float ms) noexcept {
        ms = std::clamp(ms, kMinInertiaMs, kMaxInertiaMs);
        inertiaTimeMs_ = ms;
        delaySmoother_.configure(ms, sampleRate_);
    }

    /// @brief Immediately snap to target (bypasses inertia)
    void snapToTarget() noexcept {
        delaySmoother_.snapToTarget();
    }

    /// @brief Check if currently transitioning
    [[nodiscard]] bool isTransitioning() const noexcept {
        return !delaySmoother_.isComplete();
    }

    /// @brief Process one sample and return smoothed delay
    [[nodiscard]] float process() noexcept {
        return delaySmoother_.process();
    }

private:
    float sampleRate_ = 44100.0f;
    float targetDelayMs_ = 0.0f;
    float inertiaTimeMs_ = kDefaultInertiaMs;
    OnePoleSmoother delaySmoother_;
};

// =============================================================================
// TapeDelay Class (FR-001 to FR-036)
// =============================================================================

/// @brief Layer 4 User Feature - Classic Tape Delay Emulation
///
/// Emulates vintage tape echo units (Roland RE-201, Echoplex, Watkins Copicat).
/// Composes Layer 3 components: TapManager, FeedbackNetwork, CharacterProcessor.
///
/// @par User Controls
/// - Motor Speed: Delay time with motor inertia (FR-001 to FR-004)
/// - Wear: Wow/flutter depth + hiss level (FR-005 to FR-009)
/// - Saturation: Tape drive amount (FR-010 to FR-014)
/// - Age: EQ rolloff + noise + degradation (FR-021 to FR-025)
/// - Echo Heads: 3 playback heads at fixed ratios (FR-015 to FR-020)
/// - Feedback: Echo repeats with filtering (FR-026 to FR-030)
/// - Mix: Dry/wet balance (FR-031 to FR-033)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 4 composes from Layer 0-3 only
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// TapeDelay delay;
/// delay.prepare(44100.0, 512, 2000.0f);
/// delay.setMotorSpeed(500.0f);  // 500ms delay
/// delay.setWear(0.3f);          // Moderate wow/flutter
/// delay.setFeedback(0.5f);      // 50% feedback
///
/// // In process callback
/// delay.process(left, right, numSamples);
/// @endcode
class TapeDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kNumHeads = 3;           ///< Number of playback heads
    static constexpr float kMinDelayMs = 20.0f;      ///< Minimum delay (FR-002)
    static constexpr float kMaxDelayMs = 2000.0f;    ///< Maximum delay (FR-002)
    static constexpr float kHeadRatio1 = 1.0f;       ///< Head 1 timing ratio
    static constexpr float kHeadRatio2 = 1.5f;       ///< Head 2 timing ratio
    static constexpr float kHeadRatio3 = 2.0f;       ///< Head 3 timing ratio
    static constexpr float kSmoothingTimeMs = 20.0f; ///< Parameter smoothing

    // FR-007: Wow rate scaling constants
    // Wow rate scales inversely with delay time (tape speed)
    // At short delay (fast tape): higher wow rate (~1-2 Hz)
    // At long delay (slow tape): lower wow rate (~0.2-0.4 Hz)
    static constexpr float kBaseWowRate = 0.5f;      ///< Base wow rate at reference delay
    static constexpr float kReferenceDelayMs = 500.0f; ///< Reference delay for base rate
    static constexpr float kMinWowRate = 0.15f;      ///< Minimum wow rate (slowest tape)
    static constexpr float kMaxWowRate = 2.0f;       ///< Maximum wow rate (fastest tape)

    // FR-023: Splice artifact constants
    static constexpr float kSpliceClickDurationMs = 2.0f;  ///< Duration of splice click
    static constexpr float kSpliceMaxLevel = 0.03f;  ///< Maximum splice artifact level

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    TapeDelay() noexcept {
        // Initialize heads with default ratios
        heads_[0].ratio = kHeadRatio1;
        heads_[1].ratio = kHeadRatio2;
        heads_[2].ratio = kHeadRatio3;
    }

    ~TapeDelay() = default;

    // Non-copyable, movable
    TapeDelay(const TapeDelay&) = delete;
    TapeDelay& operator=(const TapeDelay&) = delete;
    TapeDelay(TapeDelay&&) noexcept = default;
    TapeDelay& operator=(TapeDelay&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-034 to FR-036)
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

        // Prepare motor controller
        motor_.prepare(static_cast<float>(sampleRate), maxBlockSize);

        // Prepare TapManager with 3 heads at fixed ratios
        tapManager_.prepare(static_cast<float>(sampleRate), maxBlockSize, maxDelayMs_);

        // Configure the 3 tape heads in TapManager
        for (size_t i = 0; i < kNumHeads; ++i) {
            tapManager_.setTapEnabled(i, heads_[i].enabled);
            tapManager_.setTapLevelDb(i, heads_[i].levelDb);
            tapManager_.setTapPan(i, heads_[i].pan);
        }

        // Prepare feedback network
        feedbackNetwork_.prepare(sampleRate, maxBlockSize, maxDelayMs_);
        feedbackNetwork_.setFilterEnabled(true);
        feedbackNetwork_.setFilterType(FilterType::Lowpass);
        feedbackNetwork_.setFilterCutoff(8000.0f);  // Progressive darkening

        // Prepare character processor in Tape mode
        character_.prepare(sampleRate, maxBlockSize);
        character_.setMode(CharacterMode::Tape);

        // Prepare smoothers
        feedbackSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        mixSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        outputLevelSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));

        feedbackSmoother_.snapTo(feedback_);
        mixSmoother_.snapTo(mix_);
        outputLevelSmoother_.snapTo(dbToGain(outputLevelDb_));

        prepared_ = true;
    }

    /// @brief Reset all internal state
    /// @post Delay lines cleared, smoothers snapped to current values
    void reset() noexcept {
        motor_.reset();
        tapManager_.reset();
        feedbackNetwork_.reset();
        character_.reset();

        feedbackSmoother_.snapTo(feedback_);
        mixSmoother_.snapTo(mix_);
        outputLevelSmoother_.snapTo(dbToGain(outputLevelDb_));

        // Reset splice artifact state
        spliceSampleCounter_ = 0;
    }

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Motor Speed / Delay Time (FR-001 to FR-004)
    // =========================================================================

    /// @brief Set delay time (Motor Speed control)
    /// @param ms Delay time in milliseconds [20, 2000]
    /// @note Changes smoothly with motor inertia (200-500ms transition)
    /// @note FR-007: Also updates wow rate (inversely proportional to delay)
    void setMotorSpeed(float ms) noexcept {
        ms = std::clamp(ms, kMinDelayMs, maxDelayMs_);
        motor_.setTargetDelayMs(ms);
        updateHeadDelayTimes();
        updateWowRate();           // FR-007: Update wow rate for new speed
        updateSpliceInterval();    // FR-023: Update splice timing
        updateCharacter();         // Apply the new wow rate to CharacterProcessor
    }

    /// @brief Get current (smoothed) delay time
    [[nodiscard]] float getCurrentDelayMs() const noexcept {
        return motor_.getCurrentDelayMs();
    }

    /// @brief Get target delay time
    [[nodiscard]] float getTargetDelayMs() const noexcept {
        return motor_.getTargetDelayMs();
    }

    /// @brief Set motor inertia time
    /// @param ms Transition time in milliseconds [100, 1000]
    void setMotorInertia(float ms) noexcept {
        motor_.setInertiaTimeMs(ms);
    }

    // =========================================================================
    // Wear (Wow/Flutter/Hiss) (FR-005 to FR-009)
    // =========================================================================

    /// @brief Set wear amount
    /// @param amount Wear [0, 1] - controls wow/flutter depth and hiss level
    void setWear(float amount) noexcept {
        wear_ = std::clamp(amount, 0.0f, 1.0f);
        updateCharacter();
    }

    /// @brief Get current wear amount
    [[nodiscard]] float getWear() const noexcept {
        return wear_;
    }

    /// @brief Get current wow rate (FR-007: scales inversely with Motor Speed)
    /// @return Wow rate in Hz (lower at slow tape speed, higher at fast tape speed)
    [[nodiscard]] float getWowRate() const noexcept {
        return currentWowRate_;
    }

    // =========================================================================
    // Saturation (FR-010 to FR-014)
    // =========================================================================

    /// @brief Set tape saturation amount
    /// @param amount Saturation [0, 1] - controls tape drive/warmth
    void setSaturation(float amount) noexcept {
        saturation_ = std::clamp(amount, 0.0f, 1.0f);
        updateCharacter();
    }

    /// @brief Get current saturation amount
    [[nodiscard]] float getSaturation() const noexcept {
        return saturation_;
    }

    // =========================================================================
    // Age / Degradation (FR-021 to FR-025)
    // =========================================================================

    /// @brief Set age/degradation amount
    /// @param amount Age [0, 1] - controls EQ rolloff, noise, degradation, and artifact intensity
    /// @note FR-024: Age controls splice artifact intensity when splice is enabled (unless manually overridden)
    void setAge(float amount) noexcept {
        age_ = std::clamp(amount, 0.0f, 1.0f);
        // FR-024: Age controls artifact intensity when splice is enabled
        // (but only if intensity wasn't manually set via setSpliceIntensity)
        if (spliceEnabled_ && !spliceIntensityManual_) {
            spliceIntensity_ = age_;
        }
        updateCharacter();
    }

    /// @brief Get current age amount
    [[nodiscard]] float getAge() const noexcept {
        return age_;
    }

    // =========================================================================
    // Splice Artifacts (FR-023, FR-024)
    // =========================================================================

    /// @brief Enable/disable splice artifacts
    /// @param enabled Whether splice artifacts are active
    /// @note FR-023: Optional periodic transients at tape loop point
    void setSpliceEnabled(bool enabled) noexcept {
        spliceEnabled_ = enabled;
        if (enabled) {
            // When enabled, splice intensity is controlled by Age (unless manual override is active)
            if (!spliceIntensityManual_) {
                spliceIntensity_ = age_;
            }
        } else {
            spliceIntensity_ = 0.0f;
            spliceIntensityManual_ = false;  // Reset manual flag when disabled
        }
    }

    /// @brief Check if splice artifacts are enabled
    [[nodiscard]] bool isSpliceEnabled() const noexcept {
        return spliceEnabled_;
    }

    /// @brief Set splice artifact intensity manually
    /// @param intensity Intensity [0, 1]
    /// @note Overrides Age-based intensity control until setSpliceEnabled(false) is called
    void setSpliceIntensity(float intensity) noexcept {
        spliceIntensity_ = std::clamp(intensity, 0.0f, 1.0f);
        spliceIntensityManual_ = true;  // Mark as manually controlled
    }

    /// @brief Get current splice artifact intensity
    /// @return Intensity [0, 1] (0 when splice disabled or Age is 0)
    [[nodiscard]] float getSpliceIntensity() const noexcept {
        return spliceIntensity_;
    }

    // =========================================================================
    // Echo Heads (FR-015 to FR-020)
    // =========================================================================

    /// @brief Set head enabled state
    /// @param headIndex Head index [0, 2]
    /// @param enabled Whether head contributes to output
    void setHeadEnabled(size_t headIndex, bool enabled) noexcept {
        if (headIndex >= kNumHeads) return;
        heads_[headIndex].enabled = enabled;
        tapManager_.setTapEnabled(headIndex, enabled);
    }

    /// @brief Set head output level
    /// @param headIndex Head index [0, 2]
    /// @param levelDb Level in dB [-96, +6]
    void setHeadLevel(size_t headIndex, float levelDb) noexcept {
        if (headIndex >= kNumHeads) return;
        levelDb = std::clamp(levelDb, -96.0f, 6.0f);
        heads_[headIndex].levelDb = levelDb;
        tapManager_.setTapLevelDb(headIndex, levelDb);
    }

    /// @brief Set head pan position
    /// @param headIndex Head index [0, 2]
    /// @param pan Pan position [-100, +100]
    void setHeadPan(size_t headIndex, float pan) noexcept {
        if (headIndex >= kNumHeads) return;
        pan = std::clamp(pan, -100.0f, 100.0f);
        heads_[headIndex].pan = pan;
        tapManager_.setTapPan(headIndex, pan);
    }

    /// @brief Get head configuration
    /// @param headIndex Head index [0, 2]
    /// @return Copy of head configuration
    [[nodiscard]] TapeHead getHead(size_t headIndex) const noexcept {
        if (headIndex >= kNumHeads) return TapeHead{};
        return heads_[headIndex];
    }

    /// @brief Check if head is enabled
    [[nodiscard]] bool isHeadEnabled(size_t headIndex) const noexcept {
        if (headIndex >= kNumHeads) return false;
        return heads_[headIndex].enabled;
    }

    // =========================================================================
    // Feedback (FR-026 to FR-030)
    // =========================================================================

    /// @brief Set feedback amount
    /// @param amount Feedback [0, 1.2] (>1.0 enables self-oscillation)
    void setFeedback(float amount) noexcept {
        feedback_ = std::clamp(amount, 0.0f, 1.2f);
        feedbackSmoother_.setTarget(feedback_);
    }

    /// @brief Get current feedback amount
    [[nodiscard]] float getFeedback() const noexcept {
        return feedback_;
    }

    // =========================================================================
    // Mix (FR-031)
    // =========================================================================

    /// @brief Set dry/wet mix
    /// @param amount Mix [0, 1] (0 = dry, 1 = wet)
    void setMix(float amount) noexcept {
        mix_ = std::clamp(amount, 0.0f, 1.0f);
        mixSmoother_.setTarget(mix_);
    }

    /// @brief Get current mix amount
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    // =========================================================================
    // Output Level (FR-032)
    // =========================================================================

    /// @brief Set output level
    /// @param dB Output level in dB [-96, +12]
    void setOutputLevel(float dB) noexcept {
        outputLevelDb_ = std::clamp(dB, -96.0f, 12.0f);
        outputLevelSmoother_.setTarget(dbToGain(outputLevelDb_));
    }

    /// @brief Get current output level
    [[nodiscard]] float getOutputLevel() const noexcept {
        return outputLevelDb_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @pre prepare() has been called
    /// @note noexcept, allocation-free (FR-034, FR-035)
    void process(float* left, float* right, size_t numSamples) noexcept {
        if (!prepared_ || numSamples == 0) return;

        BlockContext ctx;

        // Calculate splice click duration in samples
        const size_t spliceClickSamples = static_cast<size_t>(
            kSpliceClickDurationMs * 0.001 * sampleRate_);

        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed motor delay and update head times
            const float currentDelayMs = motor_.process();

            // Store dry samples
            const float dryL = left[i];
            const float dryR = right[i];

            // Update head delay times based on motor speed
            for (size_t h = 0; h < kNumHeads; ++h) {
                const float headDelay = currentDelayMs * heads_[h].ratio;
                tapManager_.setTapTimeMs(h, std::min(headDelay, maxDelayMs_));
            }

            // Update feedback network delay
            feedbackNetwork_.setDelayTimeMs(currentDelayMs);
            feedbackNetwork_.setFeedbackAmount(feedbackSmoother_.process());
        }

        // Process through TapManager (multi-head delay)
        tapManager_.process(left, right, left, right, numSamples);

        // Process through CharacterProcessor (tape character)
        character_.processStereo(left, right, numSamples);

        // FR-023: Add splice artifacts if enabled
        if (spliceEnabled_ && spliceIntensity_ > 0.0f && spliceIntervalSamples_ > 0) {
            for (size_t i = 0; i < numSamples; ++i) {
                // Check if we're within a splice click window
                if (spliceSampleCounter_ < spliceClickSamples) {
                    const float spliceArtifact = generateSpliceClick(
                        spliceSampleCounter_, spliceClickSamples);
                    left[i] += spliceArtifact;
                    right[i] += spliceArtifact;
                }

                // Increment counter and wrap at splice interval
                spliceSampleCounter_++;
                if (spliceSampleCounter_ >= spliceIntervalSamples_) {
                    spliceSampleCounter_ = 0;
                }
            }
        }

        // Apply mix and output level
        for (size_t i = 0; i < numSamples; ++i) {
            const float dryL = left[i];
            const float dryR = right[i];

            const float wetMix = mixSmoother_.process();
            const float dryMix = 1.0f - wetMix;
            const float outputGain = outputLevelSmoother_.process();

            left[i] = (dryL * dryMix + left[i] * wetMix) * outputGain;
            right[i] = (dryR * dryMix + right[i] * wetMix) * outputGain;
        }
    }

    /// @brief Process mono audio in-place
    /// @param buffer Mono buffer (modified in-place)
    /// @param numSamples Number of samples
    void process(float* buffer, size_t numSamples) noexcept {
        if (!prepared_ || numSamples == 0) return;

        // Calculate splice click duration in samples
        const size_t spliceClickSamples = static_cast<size_t>(
            kSpliceClickDurationMs * 0.001 * sampleRate_);

        // For mono, process as dual mono
        for (size_t i = 0; i < numSamples; ++i) {
            const float currentDelayMs = motor_.process();

            for (size_t h = 0; h < kNumHeads; ++h) {
                const float headDelay = currentDelayMs * heads_[h].ratio;
                tapManager_.setTapTimeMs(h, std::min(headDelay, maxDelayMs_));
            }
        }

        // Process mono through tap manager
        tapManager_.process(buffer, buffer, buffer, buffer, numSamples);

        // Process through character
        character_.process(buffer, numSamples);

        // FR-023: Add splice artifacts if enabled
        if (spliceEnabled_ && spliceIntensity_ > 0.0f && spliceIntervalSamples_ > 0) {
            for (size_t i = 0; i < numSamples; ++i) {
                // Check if we're within a splice click window
                if (spliceSampleCounter_ < spliceClickSamples) {
                    const float spliceArtifact = generateSpliceClick(
                        spliceSampleCounter_, spliceClickSamples);
                    buffer[i] += spliceArtifact;
                }

                // Increment counter and wrap at splice interval
                spliceSampleCounter_++;
                if (spliceSampleCounter_ >= spliceIntervalSamples_) {
                    spliceSampleCounter_ = 0;
                }
            }
        }

        // Apply output level
        for (size_t i = 0; i < numSamples; ++i) {
            const float outputGain = outputLevelSmoother_.process();
            buffer[i] *= outputGain;
        }
    }

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get number of active (enabled) heads
    [[nodiscard]] size_t getActiveHeadCount() const noexcept {
        size_t count = 0;
        for (size_t i = 0; i < kNumHeads; ++i) {
            if (heads_[i].enabled) ++count;
        }
        return count;
    }

    /// @brief Check if currently transitioning (motor inertia active)
    [[nodiscard]] bool isTransitioning() const noexcept {
        return motor_.isTransitioning();
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Update head delay times based on motor speed
    void updateHeadDelayTimes() noexcept {
        const float baseDelay = motor_.getTargetDelayMs();
        for (size_t i = 0; i < kNumHeads; ++i) {
            const float headDelay = baseDelay * heads_[i].ratio;
            tapManager_.setTapTimeMs(i, std::min(headDelay, maxDelayMs_));
        }
    }

    /// @brief Update CharacterProcessor from Wear, Saturation, Age controls
    void updateCharacter() noexcept {
        // FR-007: Calculate wow rate based on motor speed
        // Wow rate scales inversely with delay time (proportional to tape speed)
        // Shorter delay = faster tape = higher wow rate
        // Longer delay = slower tape = lower wow rate
        updateWowRate();

        // Wear controls wow/flutter depth and hiss
        // Wear 0-1 maps to:
        // - Wow depth: 0-0.5 (moderate at max)
        // - Flutter depth: 0-0.3
        // - Hiss level: -80dB to -40dB
        character_.setTapeWowDepth(wear_ * 0.5f);
        character_.setTapeFlutterDepth(wear_ * 0.3f);
        character_.setTapeHissLevel(-80.0f + wear_ * 40.0f);

        // Apply the calculated wow rate (FR-007)
        character_.setTapeWowRate(currentWowRate_);

        // Saturation controls tape drive
        character_.setTapeSaturation(saturation_);

        // Age controls rolloff and hiss boost
        // Rolloff: 12kHz down to 4kHz as age increases
        const float rolloffHz = 12000.0f - age_ * 8000.0f;
        character_.setTapeRolloffFreq(rolloffHz);

        // Boost hiss further with age
        if (age_ > 0.0f) {
            const float ageHissBoost = age_ * 10.0f;  // Extra 0-10dB
            character_.setTapeHissLevel(-80.0f + wear_ * 40.0f + ageHissBoost);
        }
    }

    /// @brief Update wow rate based on current motor speed (FR-007)
    void updateWowRate() noexcept {
        const float targetDelay = motor_.getTargetDelayMs();
        if (targetDelay <= 0.0f) {
            currentWowRate_ = kMaxWowRate;
            return;
        }

        // Wow rate scales inversely with delay time
        // At reference delay (500ms): base rate (0.5 Hz)
        // At shorter delay: higher rate (faster tape)
        // At longer delay: lower rate (slower tape)
        //
        // Formula: rate = baseRate * (referenceDelay / currentDelay)
        // This gives inverse scaling with delay time
        const float ratio = kReferenceDelayMs / targetDelay;
        currentWowRate_ = std::clamp(kBaseWowRate * ratio, kMinWowRate, kMaxWowRate);
    }

    /// @brief Update splice interval based on current delay time
    void updateSpliceInterval() noexcept {
        const float targetDelay = motor_.getTargetDelayMs();
        if (targetDelay <= 0.0f || sampleRate_ <= 0.0) {
            spliceIntervalSamples_ = 0;
            return;
        }
        // Splice occurs once per tape loop (at the delay time interval)
        spliceIntervalSamples_ = static_cast<size_t>(targetDelay * 0.001 * sampleRate_);
    }

    /// @brief Generate a splice artifact sample
    /// @param sampleInClick Sample position within the click (0 to clickDurationSamples)
    /// @param clickDurationSamples Total duration of the click in samples
    /// @return Artifact sample value
    [[nodiscard]] float generateSpliceClick(size_t sampleInClick, size_t clickDurationSamples) const noexcept {
        if (clickDurationSamples == 0) return 0.0f;

        // Generate a short transient/click
        // Using a simple decaying impulse with some randomness
        const float position = static_cast<float>(sampleInClick) / static_cast<float>(clickDurationSamples);

        // Quick attack, exponential decay envelope
        float envelope = 0.0f;
        if (position < 0.1f) {
            // Attack phase
            envelope = position / 0.1f;
        } else {
            // Decay phase
            envelope = std::exp(-5.0f * (position - 0.1f));
        }

        // Add some "click" character with a mix of impulse and noise
        float click = 0.0f;
        if (sampleInClick == 0) {
            click = 1.0f;  // Initial impulse
        } else {
            // Decaying noise-like component
            // Simple pseudo-random using sample position
            const uint32_t seed = static_cast<uint32_t>(sampleInClick * 1103515245 + 12345);
            const float noise = (static_cast<float>(seed % 1000) / 500.0f) - 1.0f;
            click = noise * 0.5f;
        }

        return click * envelope * spliceIntensity_ * kSpliceMaxLevel;
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    float maxDelayMs_ = kMaxDelayMs;
    bool prepared_ = false;

    // Motor controller (inertia)
    MotorController motor_;

    // Layer 3 components
    TapManager tapManager_;
    FeedbackNetwork feedbackNetwork_;
    CharacterProcessor character_;

    // Tape heads (3 fixed-ratio heads)
    std::array<TapeHead, kNumHeads> heads_;

    // Parameters
    float wear_ = 0.0f;           // Wow/flutter depth (0-1)
    float saturation_ = 0.0f;     // Tape drive (0-1)
    float age_ = 0.0f;            // Degradation (0-1)
    float feedback_ = 0.5f;       // Feedback amount (0-1.2)
    float mix_ = 0.5f;            // Dry/wet (0-1)
    float outputLevelDb_ = 0.0f;  // Output level in dB

    // FR-007: Wow rate state (scales with motor speed)
    float currentWowRate_ = kBaseWowRate;

    // FR-023: Splice artifact state
    bool spliceEnabled_ = false;      // Splice artifacts disabled by default
    float spliceIntensity_ = 0.0f;    // Intensity [0, 1], controlled by Age
    bool spliceIntensityManual_ = false; // True if intensity was set manually via setSpliceIntensity
    size_t spliceSampleCounter_ = 0;  // Counter for splice timing
    size_t spliceIntervalSamples_ = 0; // Samples between splice points

    // Smoothers
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother outputLevelSmoother_;
};

} // namespace DSP
} // namespace Iterum
