// ==============================================================================
// Layer 4: User Feature - BBDDelay
// ==============================================================================
// Classic bucket-brigade device (BBD) delay emulation.
// Emulates vintage analog delays (Boss DM-2, EHX Memory Man, Roland Dimension D).
//
// Composes:
// - DelayEngine (Layer 3): Core delay with tempo sync
// - FeedbackNetwork (Layer 3): Feedback path with filtering and saturation
// - CharacterProcessor (Layer 3): BBD character (bandwidth limiting, clock noise)
// - LFO (Layer 1): Triangle modulation for chorus effect
//
// Unique BBD behaviors:
// - Bandwidth inversely proportional to delay time (clock physics)
// - Compander artifacts (pumping/breathing)
// - Clock noise proportional to delay time
// - Era selection for different chip models
//
// Feature: 025-bbd-delay
// Layer: 4 (User Feature)
// Reference: specs/025-bbd-delay/spec.md
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
#include "dsp/primitives/lfo.h"
#include "dsp/primitives/smoother.h"
#include "dsp/systems/character_processor.h"
#include "dsp/systems/delay_engine.h"
#include "dsp/systems/feedback_network.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// BBDChipModel Enumeration (FR-024 to FR-029)
// =============================================================================

/// @brief BBD chip model selection for era-based character
///
/// Each chip model provides distinct tonal characteristics based on
/// historical BBD IC behavior.
enum class BBDChipModel : uint8_t {
    MN3005 = 0,   ///< Panasonic 4096-stage (Memory Man) - widest BW, lowest noise
    MN3007 = 1,   ///< Panasonic 1024-stage - medium-dark character
    MN3205 = 2,   ///< Panasonic 4096-stage budget - darker, noisier
    SAD1024 = 3   ///< Reticon 1024-stage early chip - most noise, limited BW
};

// =============================================================================
// BBDDelay Class (FR-001 to FR-041)
// =============================================================================

/// @brief Layer 4 User Feature - Classic BBD Delay Emulation
///
/// Emulates vintage bucket-brigade device delays (Boss DM-2, EHX Memory Man).
/// Composes Layer 3 components: DelayEngine, FeedbackNetwork, CharacterProcessor.
///
/// @par User Controls
/// - Time: Delay time 20-1000ms with bandwidth tracking (FR-001 to FR-004)
/// - Feedback: Echo repeats 0-120% with soft limiting (FR-005 to FR-008)
/// - Modulation: Triangle LFO depth 0-100% (FR-009 to FR-013)
/// - Modulation Rate: LFO speed 0.1-10 Hz
/// - Age: Degradation artifacts 0-100% (FR-019 to FR-023)
/// - Era: Chip model selection (FR-024 to FR-029)
/// - Mix: Dry/wet balance (FR-036 to FR-038)
///
/// @par BBD-Specific Behaviors
/// - Bandwidth inversely proportional to delay time (FR-014 to FR-018)
/// - Compander artifacts (attack softening, release pumping) (FR-030 to FR-032)
/// - Clock noise proportional to delay time (FR-033 to FR-035)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 4 composes from Layer 0-3 only
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// BBDDelay delay;
/// delay.prepare(44100.0, 512, 1000.0f);
/// delay.setTime(300.0f);       // 300ms delay
/// delay.setFeedback(0.5f);     // 50% feedback
/// delay.setModulation(0.3f);   // 30% modulation
/// delay.setEra(BBDChipModel::MN3005);
///
/// // In process callback
/// delay.process(left, right, numSamples);
/// @endcode
class BBDDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinDelayMs = 20.0f;       ///< Minimum delay (FR-002)
    static constexpr float kMaxDelayMs = 1000.0f;     ///< Maximum delay (FR-002)
    static constexpr float kDefaultDelayMs = 300.0f;  ///< Default delay (FR-004)
    static constexpr float kDefaultFeedback = 0.4f;   ///< Default feedback (FR-008)
    static constexpr float kDefaultMix = 0.5f;        ///< Default mix (FR-038)
    static constexpr float kDefaultAge = 0.2f;        ///< Default age (FR-023)
    static constexpr float kDefaultModRate = 0.5f;    ///< Default mod rate Hz (FR-013)
    static constexpr float kSmoothingTimeMs = 20.0f;  ///< Parameter smoothing
    static constexpr size_t kMaxDryBufferSize = 8192; ///< Max samples for dry buffer

    // Bandwidth tracking constants (FR-014 to FR-018)
    static constexpr float kMinBandwidthHz = 2500.0f;  ///< BW at max delay (FR-016)
    static constexpr float kMaxBandwidthHz = 15000.0f; ///< BW at min delay (FR-015)

    // Era characteristic multipliers
    static constexpr float kMN3005BandwidthFactor = 1.0f;
    static constexpr float kMN3005NoiseFactor = 1.0f;
    static constexpr float kMN3007BandwidthFactor = 0.85f;
    static constexpr float kMN3007NoiseFactor = 1.3f;
    static constexpr float kMN3205BandwidthFactor = 0.75f;
    static constexpr float kMN3205NoiseFactor = 1.5f;
    static constexpr float kSAD1024BandwidthFactor = 0.6f;
    static constexpr float kSAD1024NoiseFactor = 2.0f;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    BBDDelay() noexcept = default;
    ~BBDDelay() = default;

    // Non-copyable, movable
    BBDDelay(const BBDDelay&) = delete;
    BBDDelay& operator=(const BBDDelay&) = delete;
    BBDDelay(BBDDelay&&) noexcept = default;
    BBDDelay& operator=(BBDDelay&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-039 to FR-041)
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

        // Prepare DelayEngine
        delayEngine_.prepare(sampleRate, maxBlockSize, maxDelayMs_);

        // Prepare FeedbackNetwork
        feedbackNetwork_.prepare(sampleRate, maxBlockSize, maxDelayMs_);
        feedbackNetwork_.setFilterEnabled(true);
        feedbackNetwork_.setFilterType(FilterType::Lowpass);

        // Prepare CharacterProcessor in BBD mode
        character_.prepare(sampleRate, maxBlockSize);
        character_.setMode(CharacterMode::BBD);

        // Prepare modulation LFO (triangle waveform per FR-011)
        modulationLfo_.prepare(sampleRate);
        modulationLfo_.setWaveform(Waveform::Triangle);
        modulationLfo_.setFrequency(kDefaultModRate);

        // Prepare smoothers
        timeSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        feedbackSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        mixSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        modulationDepthSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        ageSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));

        // Initialize to defaults
        timeSmoother_.snapTo(kDefaultDelayMs);
        feedbackSmoother_.snapTo(kDefaultFeedback);
        mixSmoother_.snapTo(kDefaultMix);
        modulationDepthSmoother_.snapTo(0.0f);
        ageSmoother_.snapTo(kDefaultAge);

        // Apply initial era settings
        applyEraCharacteristics();
        updateBandwidth();

        prepared_ = true;
    }

    /// @brief Reset all internal state
    /// @post Delay lines cleared, smoothers snapped to current values
    void reset() noexcept {
        delayEngine_.reset();
        feedbackNetwork_.reset();
        character_.reset();
        modulationLfo_.reset();

        timeSmoother_.snapTo(delayTimeMs_);
        feedbackSmoother_.snapTo(feedback_);
        mixSmoother_.snapTo(mix_);
        modulationDepthSmoother_.snapTo(modulationDepth_);
        ageSmoother_.snapTo(age_);

        // Reset compander state
        compressorEnvelope_ = 0.0f;
        expanderEnvelope_ = 0.0f;
    }

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Time Control (FR-001 to FR-004)
    // =========================================================================

    /// @brief Set delay time
    /// @param ms Delay time in milliseconds [20, 1000]
    void setTime(float ms) noexcept {
        ms = std::clamp(ms, kMinDelayMs, maxDelayMs_);
        delayTimeMs_ = ms;
        timeSmoother_.setTarget(ms);
        updateBandwidth();
    }

    /// @brief Get current delay time
    [[nodiscard]] float getTime() const noexcept {
        return delayTimeMs_;
    }

    // =========================================================================
    // Feedback Control (FR-005 to FR-008)
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
    // Modulation Control (FR-009 to FR-013)
    // =========================================================================

    /// @brief Set modulation depth
    /// @param depth Modulation depth [0, 1]
    void setModulation(float depth) noexcept {
        modulationDepth_ = std::clamp(depth, 0.0f, 1.0f);
        modulationDepthSmoother_.setTarget(modulationDepth_);
    }

    /// @brief Get modulation depth
    [[nodiscard]] float getModulation() const noexcept {
        return modulationDepth_;
    }

    /// @brief Set modulation rate
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

    // =========================================================================
    // Age / Degradation Control (FR-019 to FR-023)
    // =========================================================================

    /// @brief Set age/degradation amount
    /// @param amount Age [0, 1] - controls noise, bandwidth reduction, compander
    void setAge(float amount) noexcept {
        age_ = std::clamp(amount, 0.0f, 1.0f);
        ageSmoother_.setTarget(age_);
        updateBandwidth();
        updateClockNoise();
    }

    /// @brief Get age amount
    [[nodiscard]] float getAge() const noexcept {
        return age_;
    }

    // =========================================================================
    // Era / Chip Model Control (FR-024 to FR-029)
    // =========================================================================

    /// @brief Set BBD chip model
    /// @param model Chip model for character selection
    void setEra(BBDChipModel model) noexcept {
        era_ = model;
        applyEraCharacteristics();
        updateBandwidth();
        updateClockNoise();
    }

    /// @brief Get current chip model
    [[nodiscard]] BBDChipModel getEra() const noexcept {
        return era_;
    }

    // =========================================================================
    // Mix and Output (FR-036 to FR-038)
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

    // =========================================================================
    // Tempo Sync Control (spec 043)
    // =========================================================================

    /// @brief Set time mode (Free or Synced)
    /// @param mode TimeMode::Free uses setTime(), TimeMode::Synced uses tempo
    void setTimeMode(TimeMode mode) noexcept {
        timeMode_ = mode;
    }

    /// @brief Get current time mode
    [[nodiscard]] TimeMode getTimeMode() const noexcept {
        return timeMode_;
    }

    /// @brief Set note value for tempo sync
    /// @param note Note value (Quarter, Eighth, etc.)
    /// @param modifier Optional modifier (None, Triplet, Dotted)
    void setNoteValue(NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept {
        noteValue_ = note;
        noteModifier_ = modifier;
    }

    /// @brief Get current note value
    [[nodiscard]] NoteValue getNoteValue() const noexcept {
        return noteValue_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio in-place (without tempo sync)
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @pre prepare() has been called
    /// @note noexcept, allocation-free (FR-039, FR-040)
    void process(float* left, float* right, size_t numSamples) noexcept {
        // Create default BlockContext and delegate
        BlockContext ctx{
            .sampleRate = sampleRate_,
            .blockSize = numSamples,
            .tempoBPM = 120.0,  // Default tempo when not provided
            .isPlaying = true
        };
        process(left, right, numSamples, ctx);
    }

    /// @brief Process stereo audio in-place with tempo sync
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @param ctx Block context with tempo information
    /// @pre prepare() has been called
    /// @note noexcept, allocation-free (FR-039, FR-040)
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept {
        if (!prepared_ || numSamples == 0) return;

        // Calculate effective delay time based on time mode
        float effectiveDelayMs = delayTimeMs_;
        if (timeMode_ == TimeMode::Synced && ctx.tempoBPM > 0.0) {
            // Calculate delay from note value and tempo
            effectiveDelayMs = noteToDelayMs(noteValue_, noteModifier_, ctx.tempoBPM);
            // Clamp to valid range
            effectiveDelayMs = std::clamp(effectiveDelayMs, kMinDelayMs, maxDelayMs_);
            // Update the smoother target for smooth transition
            timeSmoother_.setTarget(effectiveDelayMs);
            // Update bandwidth for the new delay time
            updateBandwidth();
        }

        // Store dry signal for mixing later
        // We use a simple approach: store first/last sample for approximate mixing
        // (Full implementation would need temporary buffer allocation)

        for (size_t i = 0; i < numSamples; ++i) {
            // Store dry samples for later mixing
            const float dryL = left[i];
            const float dryR = right[i];
            dryBufferL_[i % kMaxDryBufferSize] = dryL;
            dryBufferR_[i % kMaxDryBufferSize] = dryR;

            // Get smoothed parameters
            const float currentDelayMs = timeSmoother_.process();
            const float currentFeedback = feedbackSmoother_.process();
            const float currentModDepth = modulationDepthSmoother_.process();
            const float currentAge = ageSmoother_.process();

            // Calculate modulated delay time (FR-012)
            float modulatedDelay = currentDelayMs;
            if (currentModDepth > 0.0f) {
                // Triangle LFO modulates delay time
                float lfoValue = modulationLfo_.process();
                // Modulation depth maps to +/- 5% of delay time at 100%
                float modAmount = lfoValue * currentModDepth * 0.05f * currentDelayMs;
                modulatedDelay = std::clamp(currentDelayMs + modAmount,
                                            kMinDelayMs, maxDelayMs_);
            } else {
                (void)modulationLfo_.process(); // Keep LFO running even when depth is 0
            }

            // Update delay engine time
            delayEngine_.setDelayTimeMs(modulatedDelay);

            // Update feedback network
            feedbackNetwork_.setDelayTimeMs(modulatedDelay);
            feedbackNetwork_.setFeedbackAmount(currentFeedback);

            // Update bandwidth based on current delay time (FR-014 to FR-018)
            updateBandwidthForDelay(modulatedDelay);

            // Apply compander compression stage (FR-030)
            float compressedL = left[i];
            float compressedR = right[i];
            if (currentAge > 0.0f) {
                applyCompression(compressedL, compressedR, currentAge);
            }

            // Write compressed signal to delay
            left[i] = compressedL;
            right[i] = compressedR;
        }

        // Process through delay engine (requires BlockContext)
        delayEngine_.process(left, right, numSamples, ctx);

        // Process through CharacterProcessor (BBD mode)
        character_.processStereo(left, right, numSamples);

        // Apply compander expansion and mix
        for (size_t i = 0; i < numSamples; ++i) {
            const float currentMix = mixSmoother_.process();
            const float currentAge = ageSmoother_.getCurrentValue();

            // Apply expansion stage (FR-030)
            float expandedL = left[i];
            float expandedR = right[i];
            if (currentAge > 0.0f) {
                applyExpansion(expandedL, expandedR, currentAge);
            }

            // Dry/wet mix
            const float wetMix = currentMix;
            const float dryMix = 1.0f - wetMix;

            // Mix dry (from buffer) and wet signals
            const float dryL = dryBufferL_[i % kMaxDryBufferSize];
            const float dryR = dryBufferR_[i % kMaxDryBufferSize];

            left[i] = dryL * dryMix + expandedL * wetMix;
            right[i] = dryR * dryMix + expandedR * wetMix;
        }
    }

    /// @brief Process mono audio in-place
    /// @param buffer Mono buffer (modified in-place)
    /// @param numSamples Number of samples
    void process(float* buffer, size_t numSamples) noexcept {
        if (!prepared_ || numSamples == 0) return;

        // For mono, process as dual mono
        process(buffer, buffer, numSamples);
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Get era-specific bandwidth factor
    [[nodiscard]] float getEraBandwidthFactor() const noexcept {
        switch (era_) {
            case BBDChipModel::MN3005: return kMN3005BandwidthFactor;
            case BBDChipModel::MN3007: return kMN3007BandwidthFactor;
            case BBDChipModel::MN3205: return kMN3205BandwidthFactor;
            case BBDChipModel::SAD1024: return kSAD1024BandwidthFactor;
            default: return kMN3005BandwidthFactor;
        }
    }

    /// @brief Get era-specific noise factor
    [[nodiscard]] float getEraNoiseFactor() const noexcept {
        switch (era_) {
            case BBDChipModel::MN3005: return kMN3005NoiseFactor;
            case BBDChipModel::MN3007: return kMN3007NoiseFactor;
            case BBDChipModel::MN3205: return kMN3205NoiseFactor;
            case BBDChipModel::SAD1024: return kSAD1024NoiseFactor;
            default: return kMN3005NoiseFactor;
        }
    }

    /// @brief Apply era-specific characteristics to processors
    void applyEraCharacteristics() noexcept {
        float noiseFactor = getEraNoiseFactor();

        // Adjust saturation based on era (older chips = more distortion)
        float saturation = 0.2f + (noiseFactor - 1.0f) * 0.1f;
        character_.setBBDSaturation(std::clamp(saturation, 0.0f, 1.0f));
    }

    /// @brief Calculate bandwidth for given delay time (FR-014 to FR-018)
    [[nodiscard]] float calculateBandwidth(float delayMs) const noexcept {
        // Bandwidth scales inversely with delay time
        // Based on BBD clock physics: longer delay = lower clock = lower BW
        //
        // Formula: BW = baseMax * (minDelay / currentDelay) * eraFactor
        // Clamped to [minBW, maxBW]

        if (delayMs <= 0.0f) return kMaxBandwidthHz;

        float eraFactor = getEraBandwidthFactor();
        float ageFactor = 1.0f - (age_ * 0.3f); // Age reduces bandwidth by up to 30%

        // Linear interpolation based on delay time position
        float delayRatio = (delayMs - kMinDelayMs) / (kMaxDelayMs - kMinDelayMs);
        delayRatio = std::clamp(delayRatio, 0.0f, 1.0f);

        // Inverse relationship: longer delay = lower bandwidth
        float baseBandwidth = kMaxBandwidthHz - delayRatio * (kMaxBandwidthHz - kMinBandwidthHz);

        return std::clamp(baseBandwidth * eraFactor * ageFactor,
                          kMinBandwidthHz, kMaxBandwidthHz);
    }

    /// @brief Update bandwidth based on current settings
    void updateBandwidth() noexcept {
        float bandwidth = calculateBandwidth(delayTimeMs_);
        character_.setBBDBandwidth(bandwidth);
        feedbackNetwork_.setFilterCutoff(bandwidth);
    }

    /// @brief Update bandwidth for specific delay time (per-sample)
    void updateBandwidthForDelay(float delayMs) noexcept {
        float bandwidth = calculateBandwidth(delayMs);
        character_.setBBDBandwidth(bandwidth);
    }

    /// @brief Update clock noise level (FR-033 to FR-035)
    void updateClockNoise() noexcept {
        // Clock noise is higher at longer delays (lower clock frequency)
        // and scales with Age parameter
        float noiseFactor = getEraNoiseFactor();

        // Base noise level: -70dB at short delay, -50dB at long delay
        float delayRatio = (delayTimeMs_ - kMinDelayMs) / (kMaxDelayMs - kMinDelayMs);
        float baseNoiseDb = -70.0f + delayRatio * 20.0f;

        // Apply age and era factors
        float noiseDb = baseNoiseDb + (age_ * 15.0f) + ((noiseFactor - 1.0f) * 10.0f);

        character_.setBBDClockNoiseLevel(std::clamp(noiseDb, -80.0f, -30.0f));
    }

    /// @brief Apply compressor stage (FR-030)
    void applyCompression(float& left, float& right, float age) noexcept {
        // Simple envelope-based compression
        float inputLevel = std::max(std::abs(left), std::abs(right));

        // Update envelope with fast attack, slow release
        constexpr float attackCoeff = 0.01f;
        constexpr float releaseCoeff = 0.0001f;

        if (inputLevel > compressorEnvelope_) {
            compressorEnvelope_ += attackCoeff * (inputLevel - compressorEnvelope_);
        } else {
            compressorEnvelope_ += releaseCoeff * (inputLevel - compressorEnvelope_);
        }

        // Apply compression scaled by age
        float compRatio = 1.0f + age * 0.5f; // Up to 1.5:1 ratio
        float threshold = 0.3f;

        if (compressorEnvelope_ > threshold) {
            float reduction = 1.0f - (1.0f - 1.0f/compRatio) *
                              (compressorEnvelope_ - threshold) / compressorEnvelope_;
            reduction = std::max(reduction, 0.5f);
            left *= reduction;
            right *= reduction;
        }
    }

    /// @brief Apply expander stage (FR-030)
    void applyExpansion(float& left, float& right, float age) noexcept {
        // Simple envelope-based expansion (inverse of compression)
        float inputLevel = std::max(std::abs(left), std::abs(right));

        // Update envelope with slow attack, fast release (creates pumping)
        constexpr float attackCoeff = 0.0001f;
        constexpr float releaseCoeff = 0.001f;

        if (inputLevel > expanderEnvelope_) {
            expanderEnvelope_ += attackCoeff * (inputLevel - expanderEnvelope_);
        } else {
            expanderEnvelope_ += releaseCoeff * (inputLevel - expanderEnvelope_);
        }

        // Apply expansion scaled by age
        float expansion = 1.0f + age * expanderEnvelope_ * 0.3f;
        expansion = std::clamp(expansion, 1.0f, 1.5f);

        left *= expansion;
        right *= expansion;
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
    DelayEngine delayEngine_;
    FeedbackNetwork feedbackNetwork_;
    CharacterProcessor character_;

    // Modulation LFO (Layer 1)
    LFO modulationLfo_;

    // Parameters
    float delayTimeMs_ = kDefaultDelayMs;   ///< Delay time (FR-001)
    float feedback_ = kDefaultFeedback;      ///< Feedback amount (FR-005)
    float modulationDepth_ = 0.0f;           ///< Modulation depth (FR-009)
    float modulationRate_ = kDefaultModRate; ///< Modulation rate (FR-010)
    float age_ = kDefaultAge;                ///< Age/degradation (FR-019)
    float mix_ = kDefaultMix;                ///< Dry/wet mix (FR-036)
    BBDChipModel era_ = BBDChipModel::MN3005; ///< Chip model (FR-029)

    // Tempo sync state (spec 043)
    TimeMode timeMode_ = TimeMode::Free;     ///< Free (ms) or Synced (tempo)
    NoteValue noteValue_ = NoteValue::Eighth; ///< Base note value
    NoteModifier noteModifier_ = NoteModifier::None; ///< Note modifier

    // Smoothers
    OnePoleSmoother timeSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother modulationDepthSmoother_;
    OnePoleSmoother ageSmoother_;

    // Compander state (FR-030 to FR-032)
    float compressorEnvelope_ = 0.0f;
    float expanderEnvelope_ = 0.0f;

    // Dry signal buffer for mixing (avoid allocation in process)
    std::array<float, kMaxDryBufferSize> dryBufferL_ = {};
    std::array<float, kMaxDryBufferSize> dryBufferR_ = {};
};

} // namespace DSP
} // namespace Iterum
