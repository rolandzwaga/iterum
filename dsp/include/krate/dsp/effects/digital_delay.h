// ==============================================================================
// Layer 4: User Feature - DigitalDelay
// ==============================================================================
// Clean digital delay with three era presets (Pristine, 80s Digital, Lo-Fi).
// Features program-dependent limiter, flexible LFO modulation, and tempo sync.
//
// Composes:
// - FeedbackNetwork (Layer 3): Delay with feedback path and filtering
// - CharacterProcessor (Layer 3): DigitalVintage mode for 80s/Lo-Fi
// - DynamicsProcessor (Layer 2): Program-dependent limiter
// - LFO (Layer 1): Modulation with 6 waveform shapes
//
// Feature: 026-digital-delay
// Layer: 4 (User Feature)
// Reference: specs/026-digital-delay/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 4 (composes only from Layer 0-3)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/dynamics_processor.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/wavefolder_processor.h>
#include <krate/dsp/systems/character_processor.h>
#include <krate/dsp/systems/feedback_network.h>

#include <algorithm>
#include <cstddef>

#include <cstdint>
#include <vector>

// Debug logging
#if defined(_DEBUG)
#include <cstdio>
#endif

namespace Krate {
namespace DSP {

// =============================================================================
// DigitalEra Enumeration (FR-005)
// =============================================================================

/// @brief Digital delay era preset selection
///
/// Each era provides distinct tonal characteristics:
/// - Pristine: Crystal-clear, transparent delay
/// - EightiesDigital: Vintage digital character (PCM42, SDE-3000)
/// - LoFi: Aggressive bit-crushed degradation
enum class DigitalEra : uint8_t {
    Pristine = 0,        ///< Clean, transparent delay (FR-006, FR-007)
    EightiesDigital = 1, ///< 80s digital character (FR-008, FR-009, FR-010)
    LoFi = 2             ///< Lo-fi degradation (FR-011, FR-012, FR-013)
};

// =============================================================================
// LimiterCharacter Enumeration (FR-019)
// =============================================================================

/// @brief Limiter knee character selection for feedback limiting
///
/// Controls how aggressively the limiter engages:
/// - Soft: Gentle limiting with gradual onset (6dB knee)
/// - Medium: Balanced limiting (3dB knee)
/// - Hard: Aggressive limiting with immediate onset (0dB knee)
enum class LimiterCharacter : uint8_t {
    Soft = 0,   ///< 6dB knee - gentle, musical limiting
    Medium = 1, ///< 3dB knee - balanced response
    Hard = 2    ///< 0dB knee - aggressive, brick-wall limiting
};

// =============================================================================
// DigitalDelay Class
// =============================================================================

/// @brief Layer 4 User Feature - Digital Delay with Era Presets
///
/// Clean digital delay with three character modes: Pristine (transparent),
/// 80s Digital (vintage), and Lo-Fi (degraded). Features include program-dependent
/// limiting, tempo sync, and flexible LFO modulation with 6 waveform shapes.
///
/// @par User Controls
/// - Time: Delay time 1-10000ms with tempo sync option (FR-001 to FR-004)
/// - Feedback: Echo repeats 0-120% with soft limiting (FR-014 to FR-019)
/// - Modulation: LFO depth 0-100%, rate 0.1-10Hz, 6 waveforms (FR-020 to FR-030)
/// - Age: Degradation intensity 0-100% (FR-041 to FR-044)
/// - Era: Character preset selection (FR-005 to FR-013)
/// - Mix: Dry/wet balance 0-100% (FR-031, FR-034)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 4 composes from Layer 0-3 only
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// DigitalDelay delay;
/// delay.prepare(44100.0, 512, 10000.0f);
/// delay.setTime(500.0f);         // 500ms delay
/// delay.setFeedback(0.5f);       // 50% feedback
/// delay.setEra(DigitalEra::Pristine);
///
/// // In process callback
/// delay.process(left, right, numSamples, ctx);
/// @endcode
class DigitalDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinDelayMs = 1.0f;         ///< Minimum delay (FR-001)
    static constexpr float kMaxDelayMs = 10000.0f;     ///< Maximum delay (FR-001)
    static constexpr float kDefaultDelayMs = 500.0f;   ///< Default delay
    static constexpr float kDefaultFeedback = 0.4f;    ///< Default feedback
    static constexpr float kDefaultMix = 0.5f;         ///< Default mix
    static constexpr float kDefaultModRate = 1.0f;     ///< Default mod rate Hz
    static constexpr float kSmoothingTimeMs = 20.0f;   ///< Parameter smoothing (FR-033)
    static constexpr float kWavefoldCrossfadeMs = 100.0f;  ///< Crossfade time for wavefold model changes
    static constexpr size_t kDefaultDryBufferSize = 8192;  ///< Default dry buffer (resized in prepare)

    // Limiter constants (per plan.md)
    static constexpr float kLimiterThresholdDb = -0.5f;  ///< Limiter threshold
    static constexpr float kLimiterRatio = 100.0f;       ///< True limiting ratio
    static constexpr float kSoftKneeDb = 6.0f;           ///< Soft knee width
    static constexpr float kMediumKneeDb = 3.0f;         ///< Medium knee width
    static constexpr float kHardKneeDb = 0.0f;           ///< Hard knee (brick wall)

    // 80s Digital era constants (FR-009, FR-010)
    static constexpr float k80sNoiseFloorDb = -80.0f;    ///< 80s era noise floor (FR-010)
    static constexpr float k80sAntiAliasHz = 14000.0f;   ///< Anti-alias filter for ~32kHz (FR-009)

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    DigitalDelay() noexcept = default;
    ~DigitalDelay() = default;

    // Non-copyable, movable
    DigitalDelay(const DigitalDelay&) = delete;
    DigitalDelay& operator=(const DigitalDelay&) = delete;
    DigitalDelay(DigitalDelay&&) noexcept = default;
    DigitalDelay& operator=(DigitalDelay&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-038 to FR-040)
    // =========================================================================

    /// @brief Prepare for processing (allocates memory) - convenience overload
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @post Ready for process() calls with default max delay
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        prepare(sampleRate, maxBlockSize, kMaxDelayMs);
    }

    /// @brief Prepare for processing (allocates memory)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxDelayMs Maximum delay time in milliseconds
    /// @post Ready for process() calls
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        maxDelayMs_ = std::min(maxDelayMs, kMaxDelayMs);

        // Prepare FeedbackNetwork (handles delay + feedback loop)
        feedbackNetwork_.prepare(sampleRate, maxBlockSize, maxDelayMs_);
        feedbackNetwork_.setFilterEnabled(false); // Pristine default

        // Prepare CharacterProcessor in Clean mode (Pristine default)
        character_.prepare(sampleRate, maxBlockSize);
        character_.setMode(CharacterMode::Clean);

        // Prepare DynamicsProcessor as limiter (high ratio + low threshold)
        limiter_.prepare(sampleRate, maxBlockSize);
        limiter_.setThreshold(kLimiterThresholdDb);
        limiter_.setRatio(kLimiterRatio);  // 100:1 = true limiting
        limiter_.setKneeWidth(kSoftKneeDb); // Default soft
        limiter_.setDetectionMode(DynamicsDetectionMode::Peak);

        // Prepare EnvelopeFollower for noise modulation
        // Amplitude mode for fast, responsive dynamics with asymmetric attack/release
        // Ultra-fast release (2ms) to track rapid changes in input dynamics
        noiseEnvelope_.prepare(sampleRate, maxBlockSize);
        noiseEnvelope_.setMode(DetectionMode::Amplitude);
        noiseEnvelope_.setAttackTime(0.1f);    // 0.1ms - near-instant response to transients
        noiseEnvelope_.setReleaseTime(2.0f);   // 2ms - ultra-fast decay for tight tracking

        // Prepare WavefolderProcessors for feedback harmonic distortion
        wavefolderL_.prepare(sampleRate, maxBlockSize);
        wavefolderR_.prepare(sampleRate, maxBlockSize);
        wavefolderL_.setMix(0.0f);  // Default off
        wavefolderR_.setMix(0.0f);
        // Secondary wavefolders for crossfading during model changes
        wavefolderL2_.prepare(sampleRate, maxBlockSize);
        wavefolderR2_.prepare(sampleRate, maxBlockSize);
        wavefolderL2_.setMix(0.0f);
        wavefolderR2_.setMix(0.0f);
        // Calculate crossfade increment based on sample rate
        wavefoldCrossfadeInc_ = crossfadeIncrement(kWavefoldCrossfadeMs, static_cast<float>(sampleRate));

        // Prepare modulation LFO (Sine default per FR-025)
        modulationLfo_.prepare(sampleRate);
        modulationLfo_.setWaveform(Waveform::Sine);
        modulationLfo_.setFrequency(kDefaultModRate);

        // Prepare smoothers
        timeSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        feedbackSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        mixSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        modulationDepthSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        ageSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        widthSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));

        // Initialize to defaults
        timeSmoother_.snapTo(kDefaultDelayMs);
        feedbackSmoother_.snapTo(kDefaultFeedback);
        mixSmoother_.snapTo(kDefaultMix);
        modulationDepthSmoother_.snapTo(0.0f);
        ageSmoother_.snapTo(0.0f);
        widthSmoother_.snapTo(100.0f);

        // Configure 80s era anti-aliasing filters (FR-009)
        // Lowpass at 14kHz to simulate ~32kHz ADC Nyquist
        antiAliasFilterL_.configure(FilterType::Lowpass, k80sAntiAliasHz, 0.707f, 0.0f,
                                    static_cast<float>(sampleRate));
        antiAliasFilterR_.configure(FilterType::Lowpass, k80sAntiAliasHz, 0.707f, 0.0f,
                                    static_cast<float>(sampleRate));

        // Allocate dry buffers sized for maxBlockSize (REGRESSION FIX: was static 8192)
        // This prevents discontinuities when processing blocks larger than 8192 samples
        dryBufferL_.resize(maxBlockSize);
        dryBufferR_.resize(maxBlockSize);
        std::fill(dryBufferL_.begin(), dryBufferL_.end(), 0.0f);
        std::fill(dryBufferR_.begin(), dryBufferR_.end(), 0.0f);

        // Allocate envelope buffer for noise modulation
        envelopeBuffer_.resize(maxBlockSize);
        std::fill(envelopeBuffer_.begin(), envelopeBuffer_.end(), 0.0f);

        // Allocate wavefold crossfade temp buffers
        wavefoldTempL_.resize(maxBlockSize);
        wavefoldTempR_.resize(maxBlockSize);
        std::fill(wavefoldTempL_.begin(), wavefoldTempL_.end(), 0.0f);
        std::fill(wavefoldTempR_.begin(), wavefoldTempR_.end(), 0.0f);

        prepared_ = true;
    }

    /// @brief Reset all internal state
    /// @post Delay lines cleared, smoothers snapped to current values
    void reset() noexcept {
        feedbackNetwork_.reset();
        character_.reset();
        limiter_.reset();
        noiseEnvelope_.reset();
        wavefolderL_.reset();
        wavefolderR_.reset();
        wavefolderL2_.reset();
        wavefolderR2_.reset();
        wavefoldCrossfadePos_ = 0.0f;
        wavefoldCrossfading_ = false;
        modulationLfo_.reset();
        antiAliasFilterL_.reset();
        antiAliasFilterR_.reset();

        timeSmoother_.snapTo(delayTimeMs_);
        feedbackSmoother_.snapTo(feedback_);
        mixSmoother_.snapTo(mix_);
        modulationDepthSmoother_.snapTo(modulationDepth_);
        ageSmoother_.snapTo(age_);
        widthSmoother_.snapTo(width_);
    }

    /// @brief Snap all parameters to current values (skip smoothing, for testing)
    void snapParameters() noexcept {
        timeSmoother_.snapTo(delayTimeMs_);
        feedbackSmoother_.snapTo(feedback_);
        mixSmoother_.snapTo(mix_);
        modulationDepthSmoother_.snapTo(modulationDepth_);
        ageSmoother_.snapTo(age_);
        widthSmoother_.snapTo(width_);

        // Snap FeedbackNetwork parameters to avoid transients in tests
        feedbackNetwork_.setDelayTimeMs(delayTimeMs_);
        feedbackNetwork_.setFeedbackAmount(feedback_);
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
    void setTime(float ms) noexcept {
        ms = std::clamp(ms, kMinDelayMs, maxDelayMs_);
        delayTimeMs_ = ms;
        timeSmoother_.setTarget(ms);
    }

    /// @brief Set delay time in milliseconds (alias for setTime)
    void setDelayTime(float ms) noexcept {
        setTime(ms);
    }

    /// @brief Get current delay time
    [[nodiscard]] float getTime() const noexcept {
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
    /// @param value Note value (quarter, eighth, etc.)
    /// @param modifier Note modifier (none, dotted, triplet)
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept {
        noteValue_ = value;
        noteModifier_ = modifier;
    }

    /// @brief Get current note value
    [[nodiscard]] NoteValue getNoteValue() const noexcept {
        return noteValue_;
    }

    // =========================================================================
    // Feedback Control (FR-014 to FR-019)
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

    /// @brief Set limiter character (FR-019)
    /// @param character Limiter knee type
    void setLimiterCharacter(LimiterCharacter character) noexcept {
        limiterCharacter_ = character;
        switch (character) {
            case LimiterCharacter::Soft:
                limiter_.setKneeWidth(kSoftKneeDb);
                break;
            case LimiterCharacter::Medium:
                limiter_.setKneeWidth(kMediumKneeDb);
                break;
            case LimiterCharacter::Hard:
                limiter_.setKneeWidth(kHardKneeDb);
                break;
        }
    }

    /// @brief Get current limiter character
    [[nodiscard]] LimiterCharacter getLimiterCharacter() const noexcept {
        return limiterCharacter_;
    }

    // =========================================================================
    // Era Control (FR-005 to FR-013)
    // =========================================================================

    /// @brief Set digital era preset
    /// @param era Era preset selection
    void setEra(DigitalEra era) noexcept {
        era_ = era;
        applyEraSettings();
    }

    /// @brief Get current era preset
    [[nodiscard]] DigitalEra getEra() const noexcept {
        return era_;
    }

    // =========================================================================
    // Age / Degradation Control (FR-041 to FR-044)
    // =========================================================================

    /// @brief Set age/degradation amount
    /// @param amount Age [0, 1] - controls degradation intensity
    void setAge(float amount) noexcept {
        age_ = std::clamp(amount, 0.0f, 1.0f);
        ageSmoother_.setTarget(age_);
        applyEraSettings();
    }

    /// @brief Get age amount
    [[nodiscard]] float getAge() const noexcept {
        return age_;
    }

    // =========================================================================
    // Modulation Control (FR-020 to FR-030)
    // =========================================================================

    /// @brief Set modulation depth (FR-021)
    /// @param depth Modulation depth [0, 1]
    void setModulationDepth(float depth) noexcept {
        modulationDepth_ = std::clamp(depth, 0.0f, 1.0f);
        modulationDepthSmoother_.setTarget(modulationDepth_);
    }

    /// @brief Get modulation depth
    [[nodiscard]] float getModulationDepth() const noexcept {
        return modulationDepth_;
    }

    /// @brief Set modulation rate (FR-022)
    /// @param rateHz Rate in Hz [0.1, 10]
    void setModulationRate(float rateHz) noexcept {
        rateHz = std::clamp(rateHz, 0.1f, 10.0f);
        modulationRate_ = rateHz;
        modulationLfo_.setFrequency(rateHz);
    }

    /// @brief Get modulation rate
    [[nodiscard]] float getModulationRate() const noexcept {
        return modulationRate_;
    }

    /// @brief Set modulation waveform (FR-023)
    /// @param waveform LFO waveform type
    void setModulationWaveform(Waveform waveform) noexcept {
        modulationWaveform_ = waveform;
        modulationLfo_.setWaveform(waveform);
    }

    /// @brief Get modulation waveform
    [[nodiscard]] Waveform getModulationWaveform() const noexcept {
        return modulationWaveform_;
    }

    // =========================================================================
    // Mix and Output (FR-031 to FR-034)
    // =========================================================================

    /// @brief Set dry/wet mix (FR-031)
    /// @param amount Mix [0, 1] (0 = dry, 1 = wet)
    void setMix(float amount) noexcept {
        mix_ = std::clamp(amount, 0.0f, 1.0f);
        mixSmoother_.setTarget(mix_);
    }

    /// @brief Get mix amount
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    // =========================================================================
    // Stereo Width Control (spec 036)
    // =========================================================================

    /// @brief Set stereo width
    /// @param percent Width percentage [0, 200] (0 = mono, 100 = original, 200 = maximum)
    void setWidth(float percent) noexcept {
        width_ = std::clamp(percent, 0.0f, 200.0f);
        widthSmoother_.setTarget(width_);
    }

    /// @brief Get stereo width
    [[nodiscard]] float getWidth() const noexcept {
        return width_;
    }

    // =========================================================================
    // Wavefold Distortion Control
    // =========================================================================

    /// @brief Set wavefold amount (0 = disabled)
    /// @param amount Amount percentage [0, 100] - maps to fold amount [0.1, 10.0]
    void setWavefoldAmount(float amount) noexcept {
        wavefoldAmount_ = std::clamp(amount, 0.0f, 100.0f);
        if (wavefoldAmount_ < 0.01f) {
            // Effectively disabled - set mix to 0 for efficient bypass
            wavefolderL_.setMix(0.0f);
            wavefolderR_.setMix(0.0f);
            wavefolderL2_.setMix(0.0f);
            wavefolderR2_.setMix(0.0f);
        } else {
            // Map 1-100% to fold amount [0.1, 10.0]
            const float foldAmount = 0.1f + (wavefoldAmount_ / 100.0f) * 9.9f;
            wavefolderL_.setFoldAmount(foldAmount);
            wavefolderR_.setFoldAmount(foldAmount);
            wavefolderL_.setMix(1.0f);
            wavefolderR_.setMix(1.0f);
            // Also update secondary (for crossfade consistency)
            wavefolderL2_.setFoldAmount(foldAmount);
            wavefolderR2_.setFoldAmount(foldAmount);
            wavefolderL2_.setMix(1.0f);
            wavefolderR2_.setMix(1.0f);
        }
    }

    /// @brief Get wavefold amount
    [[nodiscard]] float getWavefoldAmount() const noexcept {
        return wavefoldAmount_;
    }

    /// @brief Set wavefold model type with crossfade to prevent clicks
    /// @param model Wavefolder model (Simple, Serge, Buchla259, Lockhart)
    void setWavefoldModel(WavefolderModel model) noexcept {
        if (model == wavefoldModel_) {
            return;  // No change needed
        }

        // CRITICAL: Copy PRIMARY to SECONDARY first to preserve DC blocker state
        // This ensures SECONDARY starts from the same state as PRIMARY, preventing
        // discontinuities caused by divergent DC blocker states during crossfade.
        wavefolderL2_ = wavefolderL_;
        wavefolderR2_ = wavefolderR_;

        // Now set new model on secondary pair (preserves DC blocker state)
        wavefoldModel_ = model;
        wavefolderL2_.setModel(model);
        wavefolderR2_.setModel(model);
        // Parameters already copied from PRIMARY, but ensure they match current settings
        const float foldAmount = 0.1f + (wavefoldAmount_ / 100.0f) * 9.9f;
        wavefolderL2_.setFoldAmount(foldAmount);
        wavefolderR2_.setFoldAmount(foldAmount);
        wavefolderL2_.setSymmetry(wavefoldSymmetry_);
        wavefolderR2_.setSymmetry(wavefoldSymmetry_);
        wavefolderL2_.setMix(1.0f);
        wavefolderR2_.setMix(1.0f);
        // Start crossfade
        wavefoldCrossfadePos_ = 0.0f;
        wavefoldCrossfading_ = true;
    }

    /// @brief Get wavefold model type
    [[nodiscard]] WavefolderModel getWavefoldModel() const noexcept {
        return wavefoldModel_;
    }

    /// @brief Set wavefold symmetry for even/odd harmonic balance
    /// @param symmetry Symmetry value [-1, +1] (0 = symmetric/odd harmonics only)
    void setWavefoldSymmetry(float symmetry) noexcept {
        wavefoldSymmetry_ = std::clamp(symmetry, -1.0f, 1.0f);
        wavefolderL_.setSymmetry(wavefoldSymmetry_);
        wavefolderR_.setSymmetry(wavefoldSymmetry_);
        // Also update secondary (for crossfade consistency)
        wavefolderL2_.setSymmetry(wavefoldSymmetry_);
        wavefolderR2_.setSymmetry(wavefoldSymmetry_);
    }

    /// @brief Get wavefold symmetry
    [[nodiscard]] float getWavefoldSymmetry() const noexcept {
        return wavefoldSymmetry_;
    }

    // =========================================================================
    // Processing (FR-035 to FR-040)
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

        // Store dry signal for mixing (buffer sized in prepare() for maxBlockSize)
        const size_t samplesToStore = std::min(numSamples, dryBufferL_.size());
        for (size_t i = 0; i < samplesToStore; ++i) {
            dryBufferL_[i] = left[i];
            dryBufferR_[i] = right[i];
        }

        // Calculate base delay time (handle tempo sync using Layer 0 utility)
        float baseDelayMs = delayTimeMs_;
        if (timeMode_ == TimeMode::Synced) {
            // Use Layer 0 noteToDelayMs() for consistent tempo sync calculation
            baseDelayMs = noteToDelayMs(noteValue_, noteModifier_, ctx.tempoBPM);
            baseDelayMs = std::clamp(baseDelayMs, kMinDelayMs, maxDelayMs_);
        }
        timeSmoother_.setTarget(baseDelayMs);

        // Sample-by-sample processing for parameter smoothing
        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed parameters
            const float currentDelayMs = timeSmoother_.process();
            const float currentFeedback = feedbackSmoother_.process();
            const float currentModDepth = modulationDepthSmoother_.process();

            // Calculate modulated delay time (FR-020 to FR-030)
            float modulatedDelay = currentDelayMs;
            if (currentModDepth > 0.0f) {
                float lfoValue = modulationLfo_.process();
                // Modulation depth maps to +/- 10% of delay time at 100%
                float modAmount = lfoValue * currentModDepth * 0.1f * currentDelayMs;
                modulatedDelay = std::clamp(currentDelayMs + modAmount,
                                            kMinDelayMs, maxDelayMs_);
            } else {
                (void)modulationLfo_.process(); // Keep LFO running
            }

            // Update feedback network with modulated delay
            feedbackNetwork_.setDelayTimeMs(modulatedDelay);
            feedbackNetwork_.setFeedbackAmount(currentFeedback);
        }

        // Process through feedback network (has delay + feedback built-in)
        feedbackNetwork_.process(left, right, numSamples, ctx);

        // Apply wavefold distortion to feedback output (before era character)
        // WavefolderProcessor has internal DC blocking and parameter smoothing
        if (wavefoldAmount_ > 0.0f) {
            if (wavefoldCrossfading_) {
                // Crossfading between models - process both and blend
                // Copy input to temp buffers for primary wavefolder
                std::copy(left, left + numSamples, wavefoldTempL_.begin());
                std::copy(right, right + numSamples, wavefoldTempR_.begin());

                // Process temp with primary (old model)
                wavefolderL_.process(wavefoldTempL_.data(), numSamples);
                wavefolderR_.process(wavefoldTempR_.data(), numSamples);

                // Process original with secondary (new model) in-place
                wavefolderL2_.process(left, numSamples);
                wavefolderR2_.process(right, numSamples);

                // Blend sample-by-sample with equal-power crossfade
                for (size_t i = 0; i < numSamples; ++i) {
                    const auto [fadeOut, fadeIn] = equalPowerGains(wavefoldCrossfadePos_);
                    left[i] = wavefoldTempL_[i] * fadeOut + left[i] * fadeIn;
                    right[i] = wavefoldTempR_[i] * fadeOut + right[i] * fadeIn;

                    // Advance crossfade
                    wavefoldCrossfadePos_ += wavefoldCrossfadeInc_;
                    if (wavefoldCrossfadePos_ >= 1.0f) {
                        // Crossfade complete
                        wavefoldCrossfadePos_ = 1.0f;
                        wavefoldCrossfading_ = false;
                        // CRITICAL: Swap wavefolders instead of just updating model
                        // The secondary has the correct DC blocker state from processing
                        // during crossfade. Swapping ensures state continuity.
                        std::swap(wavefolderL_, wavefolderL2_);
                        std::swap(wavefolderR_, wavefolderR2_);
                        break;
                    }
                }
            } else {
                // Normal processing - single model
                wavefolderL_.process(left, numSamples);
                wavefolderR_.process(right, numSamples);
            }
        }

        // Track envelope of DRY INPUT ONLY for dither modulation (MOVED BEFORE character processing)
        // CRITICAL: We must track ONLY the dry (input) signal, NOT the wet signal
        // When user plays notes, dry is loud → envelope high → dither loud
        // When user stops, dry is silent → envelope drops → dither drops (breathing effect)
        const size_t samplesToProcess = std::min(numSamples, envelopeBuffer_.size());
        for (size_t i = 0; i < samplesToProcess; ++i) {
            const float dryMono = (dryBufferL_[i] + dryBufferR_[i]) * 0.5f;
            envelopeBuffer_[i] = noiseEnvelope_.processSample(dryMono);
        }

        // Apply anti-aliasing filter for 80s/Lo-Fi era (FR-009)
        // Simulates the Nyquist filter of early ~32kHz ADCs
        if (antiAliasEnabled_) {
            for (size_t i = 0; i < numSamples; ++i) {
                left[i] = antiAliasFilterL_.process(left[i]);
                right[i] = antiAliasFilterR_.process(right[i]);
            }
        }

        // Apply era-based character processing
        if (era_ != DigitalEra::Pristine) {
            character_.processStereo(left, right, numSamples);
        }

        // Apply limiter to prevent clipping during feedback transitions
        // Always apply: the limiter is transparent below threshold (-0.5dB) but
        // prevents distortion when feedback drops from high values. Previously,
        // this only ran when feedback_ > 1.0f, but the instant feedback_ drops,
        // the limiter would stop while the delay line still contains high-amplitude
        // self-oscillating signal, causing distorted noise during the transition.
        limiter_.process(left, numSamples);
        limiter_.process(right, numSamples);

        // Apply stereo width to wet signal (spec 036, FR-013, FR-016)
        // Width processing uses Mid/Side: mid = (L+R)/2, side = (L-R)/2 * widthFactor
        for (size_t i = 0; i < numSamples; ++i) {
            const float currentWidth = widthSmoother_.process();
            const float widthFactor = currentWidth / 100.0f;

            const float mid = (left[i] + right[i]) * 0.5f;
            const float side = (left[i] - right[i]) * 0.5f * widthFactor;

            left[i] = mid + side;
            right[i] = mid - side;
        }

        // Mix dry/wet
        for (size_t i = 0; i < samplesToStore; ++i) {
            const float currentMix = mixSmoother_.process();

            const float wetMix = currentMix;
            const float dryMix = 1.0f - wetMix;

            left[i] = dryBufferL_[i] * dryMix + left[i] * wetMix;
            right[i] = dryBufferR_[i] * dryMix + right[i] * wetMix;
        }
    }

    /// @brief Process mono audio in-place (FR-036)
    /// @param buffer Mono buffer (modified in-place)
    /// @param numSamples Number of samples
    /// @param ctx Block context
    void process(float* buffer, size_t numSamples, const BlockContext& ctx) noexcept {
        if (!prepared_ || numSamples == 0) return;

        // Process as dual mono
        process(buffer, buffer, numSamples, ctx);
    }

    /// @brief Process stereo audio with separate input/output buffers (convenience for tests)
    /// @param leftIn Left input buffer
    /// @param rightIn Right input buffer
    /// @param leftOut Left output buffer
    /// @param rightOut Right output buffer
    /// @param numSamples Number of samples per channel
    void processStereo(const float* leftIn, const float* rightIn,
                       float* leftOut, float* rightOut,
                       size_t numSamples) noexcept {
        if (!prepared_ || numSamples == 0) return;

        // Copy input to output (process() works in-place)
        for (size_t i = 0; i < numSamples; ++i) {
            leftOut[i] = leftIn[i];
            rightOut[i] = rightIn[i];
        }

        // Create default block context for testing
        BlockContext ctx{
            .sampleRate = sampleRate_,
            .blockSize = numSamples,
            .tempoBPM = 120.0,
            .isPlaying = false
        };

        // Process in-place
        process(leftOut, rightOut, numSamples, ctx);
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Apply era-specific settings to CharacterProcessor
    void applyEraSettings() noexcept {
        switch (era_) {
            case DigitalEra::Pristine:
                // Bypass character processing entirely (FR-006, FR-007, FR-042)
                character_.setMode(CharacterMode::Clean);
                character_.setDigitalDitherAmount(0.0f);  // No dither noise
                feedbackNetwork_.setFilterEnabled(false);
                antiAliasEnabled_ = false;
                break;

            case DigitalEra::EightiesDigital:
                // Vintage 80s digital character (FR-008, FR-009, FR-010)
                // Based on units like Roland SDE-3000, Lexicon PCM42
                character_.setMode(CharacterMode::DigitalVintage);
                // Bit depth: 14 -> 8 as age increases (audible quantization at high age)
                // Age=0: 14-bit (subtle), Age=1: 8-bit (clearly audible crunch)
                character_.setDigitalBitDepth(14.0f - age_ * 6.0f);
                // Dither amount - reduced to let quantization artifacts through
                character_.setDigitalDitherAmount(0.5f);
                // Sample rate reduction: 1.0 -> 2.0 as age increases (more aliasing)
                character_.setDigitalSampleRateReduction(1.0f + age_ * 1.0f);
                // High-frequency rolloff via feedback filter (80s anti-alias character)
                feedbackNetwork_.setFilterEnabled(true);
                feedbackNetwork_.setFilterType(FilterType::Lowpass);
                feedbackNetwork_.setFilterCutoff(12000.0f - age_ * 4000.0f); // 12kHz -> 8kHz
                // Enable anti-alias filter for 32kHz simulation (FR-009)
                antiAliasEnabled_ = true;
                break;

            case DigitalEra::LoFi:
                // Aggressive degradation (FR-011, FR-012, FR-013)
                character_.setMode(CharacterMode::DigitalVintage);
                // Bit depth: 16 -> 4 as age increases (aggressive)
                character_.setDigitalBitDepth(16.0f - age_ * 12.0f);
                // NO dither - noise comes from bit crushing, not dither
                character_.setDigitalDitherAmount(0.0f);
                // Sample rate reduction for aliasing artifacts
                character_.setDigitalSampleRateReduction(1.0f + age_ * 3.0f);
                // NO feedback filtering for LoFi (raw/aggressive sound)
                feedbackNetwork_.setFilterEnabled(false);
                // NO anti-aliasing for LoFi (aliasing is part of the character)
                antiAliasEnabled_ = false;
                break;
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    float maxDelayMs_ = kMaxDelayMs;
    bool prepared_ = false;

    // Layer 3 components
    FeedbackNetwork feedbackNetwork_;
    CharacterProcessor character_;

    // Layer 2 components
    DynamicsProcessor limiter_;
    EnvelopeFollower noiseEnvelope_;  ///< Tracks input amplitude for noise modulation
    WavefolderProcessor wavefolderL_; ///< Left channel wavefolder (primary)
    WavefolderProcessor wavefolderR_; ///< Right channel wavefolder (primary)
    WavefolderProcessor wavefolderL2_; ///< Left channel wavefolder (secondary, for crossfade)
    WavefolderProcessor wavefolderR2_; ///< Right channel wavefolder (secondary, for crossfade)
    float wavefoldCrossfadePos_ = 0.0f;   ///< Crossfade position (0=primary, 1=secondary)
    float wavefoldCrossfadeInc_ = 0.0f;   ///< Per-sample crossfade increment
    bool wavefoldCrossfading_ = false;     ///< True during model change crossfade

    // Modulation LFO (Layer 1)
    LFO modulationLfo_;

    // Parameters
    float delayTimeMs_ = kDefaultDelayMs;        ///< Delay time (FR-001)
    float feedback_ = kDefaultFeedback;          ///< Feedback amount (FR-014)
    float modulationDepth_ = 0.0f;               ///< Modulation depth (FR-021)
    float modulationRate_ = kDefaultModRate;     ///< Modulation rate (FR-022)
    float age_ = 0.0f;                           ///< Age/degradation (FR-041)
    float mix_ = kDefaultMix;                    ///< Dry/wet mix (FR-031)
    float width_ = 100.0f;                       ///< Stereo width (spec 036)
    float wavefoldAmount_ = 0.0f;                ///< Wavefold amount 0-100%
    float wavefoldSymmetry_ = 0.0f;              ///< Wavefold symmetry -1 to +1
    WavefolderModel wavefoldModel_ = WavefolderModel::Simple; ///< Wavefold type

    // Mode selections
    DigitalEra era_ = DigitalEra::Pristine;
    LimiterCharacter limiterCharacter_ = LimiterCharacter::Soft;
    TimeMode timeMode_ = TimeMode::Free;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;
    Waveform modulationWaveform_ = Waveform::Sine;

    // Smoothers
    OnePoleSmoother timeSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother modulationDepthSmoother_;
    OnePoleSmoother ageSmoother_;
    OnePoleSmoother widthSmoother_;

    // Dry signal buffer for mixing (allocated in prepare, not in process)
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;

    // Envelope buffer for noise modulation (allocated in prepare)
    std::vector<float> envelopeBuffer_;
    std::vector<float> wavefoldTempL_;  ///< Temp buffer for wavefold crossfade
    std::vector<float> wavefoldTempR_;  ///< Temp buffer for wavefold crossfade

    // 80s era anti-aliasing filter (FR-009)
    Biquad antiAliasFilterL_;
    Biquad antiAliasFilterR_;
    bool antiAliasEnabled_ = false;
};

} // namespace DSP
} // namespace Krate
