// ==============================================================================
// Layer 4: User Feature - FreezeMode
// ==============================================================================
// Infinite sustain of delay buffer contents with optional pitch shifting,
// diffusion, and decay control. Creates ethereal, evolving frozen textures.
//
// Composes:
// - FlexibleFeedbackNetwork (Layer 3): Feedback loop with built-in freeze
// - PitchShiftProcessor (Layer 2): 2 instances for stereo pitch shifting
// - DiffusionNetwork (Layer 2): Smearing for pad-like texture
// - OnePoleSmoother (Layer 1): Parameter smoothing
//
// Feature: 031-freeze-mode
// Layer: 4 (User Feature)
// Reference: specs/031-freeze-mode/spec.md
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
#include "dsp/primitives/smoother.h"
#include "dsp/processors/diffusion_network.h"
#include "dsp/processors/pitch_shift_processor.h"
#include "dsp/systems/delay_engine.h"            // For TimeMode enum
#include "dsp/systems/flexible_feedback_network.h"
#include "dsp/systems/i_feedback_processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// FreezeFeedbackProcessor - IFeedbackProcessor for freeze effect
// =============================================================================

/// @brief Feedback path processor for freeze mode with pitch shifting, diffusion, and decay
///
/// Implements IFeedbackProcessor to be injected into FlexibleFeedbackNetwork.
/// The processor applies:
/// 1. Pitch shifting (stereo) - optional shimmer effect
/// 2. Diffusion network (pad-like smearing)
/// 3. Shimmer mix blending (pitched vs unpitched ratio)
/// 4. Decay gain reduction (per-sample fade)
class FreezeFeedbackProcessor : public IFeedbackProcessor {
public:
    FreezeFeedbackProcessor() noexcept = default;
    ~FreezeFeedbackProcessor() override = default;

    // IFeedbackProcessor interface
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept override;
    void process(float* left, float* right, std::size_t numSamples) noexcept override;
    void reset() noexcept override;
    [[nodiscard]] std::size_t getLatencySamples() const noexcept override;

    // Pitch configuration (FR-009 to FR-012)
    void setPitchSemitones(float semitones) noexcept;
    void setPitchCents(float cents) noexcept;
    void setShimmerMix(float mix) noexcept;  // 0-1 (0 = unpitched, 1 = fully pitched)

    // Diffusion configuration (FR-017 to FR-019)
    void setDiffusionAmount(float amount) noexcept;  // 0-1
    void setDiffusionSize(float size) noexcept;      // 0-100

    // Decay configuration (FR-013 to FR-016)
    void setDecayAmount(float decay) noexcept;  // 0-1 (0 = infinite sustain, 1 = fast fade)

private:
    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;

    // Pitch shifters (stereo)
    PitchShiftProcessor pitchShifterL_;
    PitchShiftProcessor pitchShifterR_;

    // Diffusion network
    DiffusionNetwork diffusion_;

    // Parameters
    float shimmerMix_ = 0.0f;       // 0-1 (0 = unpitched, 1 = fully pitched)
    float diffusionAmount_ = 0.0f;  // 0-1
    float decayAmount_ = 0.0f;      // 0-1 (0 = infinite sustain)
    float decayGain_ = 1.0f;        // Pre-calculated per-sample gain
    float currentDecayLevel_ = 1.0f; // Running decay level (accumulated across blocks)

    // Scratch buffers
    std::vector<float> unpitchedL_;
    std::vector<float> unpitchedR_;
    std::vector<float> diffusionOutL_;
    std::vector<float> diffusionOutR_;

    // Calculate decay gain coefficient from decay amount and sample rate
    [[nodiscard]] float calculateDecayGain() const noexcept;
};

// =============================================================================
// FreezeMode Class
// =============================================================================

/// @brief Layer 4 User Feature - Freeze Mode
///
/// Provides infinite sustain of delay buffer contents with optional pitch shifting,
/// diffusion, and decay control. When freeze is engaged, input is muted and the
/// delay buffer loops continuously at 100% feedback.
///
/// @par Signal Flow (Freeze Engaged)
/// ```
/// Input (MUTED) ────────────────────────────────────────> Dry (silent)
///         x                                               │
///         │                                               │
///         v                                               │
///    ┌─────────┐                                         │
///    │  Delay  │<──── (100% - decay) feedback ───────────┤
///    │  Line   │                                         │
///    └────┬────┘                                         │
///         │                                              │
///         v (frozen loop)                                │
///    ┌───────────────────────────────────────────────────┐│
///    │ FreezeFeedbackProcessor:                          ││
///    │  ┌──────────┐  ┌───────────┐  ┌────────┐        ││
///    │  │  Pitch   │─>│ Diffusion │─>│ Decay  │        ││
///    │  │ Shifter  │  │  Network  │  │ (gain) │        ││
///    │  └──────────┘  └───────────┘  └────────┘        ││
///    │       ^                           │              ││
///    │       └─── shimmerMix blend ──────┘              ││
///    └───────────────────────────────────────────────────┘│
///         │                                              │
///         └──────────────────────────────────────────────┘
/// ```
///
/// @par User Controls
/// - Freeze Toggle: Engage/disengage freeze (FR-001 to FR-008)
/// - Pitch: ±24 semitones for shimmer effect (FR-009 to FR-012)
/// - Shimmer Mix: 0-100% blend of pitched/unpitched (FR-011)
/// - Decay: 0-100% (0 = infinite sustain, 100 = fast fade) (FR-013 to FR-016)
/// - Diffusion: 0-100% amount and size (FR-017 to FR-019)
/// - Filter: Optional lowpass/highpass/bandpass in feedback (FR-020 to FR-023)
/// - Dry/Wet Mix: 0-100% blend (FR-024)
/// - Output Level: -inf to +6dB (FR-025)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 4 composes from Layer 0-3 only
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// FreezeMode freeze;
/// freeze.prepare(44100.0, 512, 5000.0f);
/// freeze.setDelayTimeMs(500.0f);
/// freeze.setFeedbackAmount(0.6f);
/// freeze.snapParameters();
///
/// // Engage freeze
/// freeze.setFreezeEnabled(true);
///
/// // In process callback
/// freeze.process(left, right, numSamples, ctx);
/// @endcode
class FreezeMode {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    // Delay time limits
    static constexpr float kMinDelayMs = 10.0f;
    static constexpr float kMaxDelayMs = 5000.0f;
    static constexpr float kDefaultDelayMs = 500.0f;

    // Pitch limits (FR-010)
    static constexpr float kMinPitchSemitones = -24.0f;
    static constexpr float kMaxPitchSemitones = 24.0f;
    static constexpr float kDefaultPitchSemitones = 0.0f;

    static constexpr float kMinPitchCents = -100.0f;
    static constexpr float kMaxPitchCents = 100.0f;
    static constexpr float kDefaultPitchCents = 0.0f;

    // Shimmer mix (FR-011)
    static constexpr float kMinShimmerMix = 0.0f;
    static constexpr float kMaxShimmerMix = 100.0f;
    static constexpr float kDefaultShimmerMix = 0.0f;  // Default: no pitch shifting

    // Feedback
    static constexpr float kMinFeedback = 0.0f;
    static constexpr float kMaxFeedback = 1.2f;  // 120% for self-oscillation
    static constexpr float kDefaultFeedback = 0.5f;

    // Decay (FR-013)
    static constexpr float kMinDecay = 0.0f;      // Infinite sustain
    static constexpr float kMaxDecay = 100.0f;   // Fast fade
    static constexpr float kDefaultDecay = 0.0f;  // Default: infinite sustain

    // Diffusion (FR-017, FR-018)
    static constexpr float kMinDiffusion = 0.0f;
    static constexpr float kMaxDiffusion = 100.0f;
    static constexpr float kDefaultDiffusionAmount = 0.0f;
    static constexpr float kDefaultDiffusionSize = 50.0f;

    // Filter (FR-020 to FR-022)
    static constexpr float kMinFilterCutoff = 20.0f;
    static constexpr float kMaxFilterCutoff = 20000.0f;
    static constexpr float kDefaultFilterCutoff = 4000.0f;

    // Output (FR-024, FR-025)
    static constexpr float kMinDryWetMix = 0.0f;
    static constexpr float kMaxDryWetMix = 100.0f;
    static constexpr float kDefaultDryWetMix = 50.0f;

    static constexpr float kMinOutputGainDb = -96.0f;  // Effectively -infinity
    static constexpr float kMaxOutputGainDb = 6.0f;
    static constexpr float kDefaultOutputGainDb = 0.0f;

    // Internal
    static constexpr float kSmoothingTimeMs = 20.0f;
    static constexpr std::size_t kMaxDryBufferSize = 65536;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    FreezeMode() noexcept = default;
    ~FreezeMode() = default;

    // Non-copyable, movable
    FreezeMode(const FreezeMode&) = delete;
    FreezeMode& operator=(const FreezeMode&) = delete;
    FreezeMode(FreezeMode&&) noexcept = default;
    FreezeMode& operator=(FreezeMode&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    /// @brief Prepare for processing (allocates memory)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxDelayMs Maximum delay time in milliseconds
    void prepare(double sampleRate, std::size_t maxBlockSize, float maxDelayMs) noexcept;

    /// @brief Reset all internal state
    void reset() noexcept;

    /// @brief Snap all smoothers to current targets
    void snapParameters() noexcept;

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // =========================================================================
    // Freeze Control (FR-001 to FR-008)
    // =========================================================================

    /// @brief Enable/disable freeze mode
    /// @param enabled true to engage freeze (mutes input, 100% feedback)
    void setFreezeEnabled(bool enabled) noexcept;

    /// @brief Get freeze state (FR-008)
    [[nodiscard]] bool isFreezeEnabled() const noexcept;

    // =========================================================================
    // Delay Configuration
    // =========================================================================

    /// @brief Set delay time in milliseconds
    void setDelayTimeMs(float ms) noexcept;

    /// @brief Get current delay time
    [[nodiscard]] float getDelayTimeMs() const noexcept { return delayTimeMs_; }

    /// @brief Set time mode (free or synced)
    void setTimeMode(TimeMode mode) noexcept { timeMode_ = mode; }

    /// @brief Get current time mode
    [[nodiscard]] TimeMode getTimeMode() const noexcept { return timeMode_; }

    /// @brief Set note value for tempo sync
    void setNoteValue(NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept;

    /// @brief Get current note value
    [[nodiscard]] NoteValue getNoteValue() const noexcept { return noteValue_; }

    // =========================================================================
    // Feedback Configuration
    // =========================================================================

    /// @brief Set feedback amount (when not frozen)
    void setFeedbackAmount(float amount) noexcept;

    /// @brief Get feedback amount
    [[nodiscard]] float getFeedbackAmount() const noexcept { return feedbackAmount_; }

    // =========================================================================
    // Pitch Configuration (FR-009 to FR-012)
    // =========================================================================

    /// @brief Set pitch shift in semitones
    void setPitchSemitones(float semitones) noexcept;

    /// @brief Get pitch shift in semitones
    [[nodiscard]] float getPitchSemitones() const noexcept { return pitchSemitones_; }

    /// @brief Set fine pitch adjustment in cents
    void setPitchCents(float cents) noexcept;

    /// @brief Get fine pitch adjustment
    [[nodiscard]] float getPitchCents() const noexcept { return pitchCents_; }

    /// @brief Set shimmer mix (% of feedback that is pitch-shifted)
    void setShimmerMix(float percent) noexcept;

    /// @brief Get shimmer mix
    [[nodiscard]] float getShimmerMix() const noexcept { return shimmerMix_; }

    // =========================================================================
    // Decay Configuration (FR-013 to FR-016)
    // =========================================================================

    /// @brief Set decay amount (0 = infinite sustain, 100 = fast fade)
    void setDecay(float percent) noexcept;

    /// @brief Get decay amount
    [[nodiscard]] float getDecay() const noexcept { return decayAmount_; }

    // =========================================================================
    // Diffusion Configuration (FR-017 to FR-019)
    // =========================================================================

    /// @brief Set diffusion amount
    void setDiffusionAmount(float percent) noexcept;

    /// @brief Get diffusion amount
    [[nodiscard]] float getDiffusionAmount() const noexcept { return diffusionAmount_; }

    /// @brief Set diffusion size
    void setDiffusionSize(float percent) noexcept;

    /// @brief Get diffusion size
    [[nodiscard]] float getDiffusionSize() const noexcept { return diffusionSize_; }

    // =========================================================================
    // Filter Configuration (FR-020 to FR-023)
    // =========================================================================

    /// @brief Enable/disable feedback filter
    void setFilterEnabled(bool enabled) noexcept;

    /// @brief Get filter enabled state
    [[nodiscard]] bool isFilterEnabled() const noexcept { return filterEnabled_; }

    /// @brief Set filter type
    void setFilterType(FilterType type) noexcept;

    /// @brief Get filter type
    [[nodiscard]] FilterType getFilterType() const noexcept { return filterType_; }

    /// @brief Set filter cutoff frequency
    void setFilterCutoff(float hz) noexcept;

    /// @brief Get filter cutoff
    [[nodiscard]] float getFilterCutoff() const noexcept { return filterCutoffHz_; }

    // =========================================================================
    // Output Configuration (FR-024, FR-025)
    // =========================================================================

    /// @brief Set dry/wet mix
    void setDryWetMix(float percent) noexcept;

    /// @brief Get dry/wet mix
    [[nodiscard]] float getDryWetMix() const noexcept { return dryWetMix_; }

    /// @brief Set output level
    void setOutputGainDb(float dB) noexcept;

    /// @brief Get output level
    [[nodiscard]] float getOutputGainDb() const noexcept { return outputGainDb_; }

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get processing latency in samples (FR-029)
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio in-place
    void process(float* left, float* right, std::size_t numSamples,
                 const BlockContext& ctx) noexcept;

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Calculate tempo-synced delay time
    [[nodiscard]] float calculateTempoSyncedDelay(const BlockContext& ctx) const noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;
    float maxDelayMs_ = kMaxDelayMs;
    bool prepared_ = false;

    // Layer 3 - Flexible feedback network (has built-in freeze)
    FlexibleFeedbackNetwork feedbackNetwork_;

    // Freeze processor (injected into feedback network)
    FreezeFeedbackProcessor freezeProcessor_;

    // Layer 1 primitives - parameter smoothers
    OnePoleSmoother delaySmoother_;
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother outputGainSmoother_;

    // Parameters - delay
    float delayTimeMs_ = kDefaultDelayMs;
    TimeMode timeMode_ = TimeMode::Free;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;

    // Parameters - feedback
    float feedbackAmount_ = kDefaultFeedback;

    // Parameters - pitch
    float pitchSemitones_ = kDefaultPitchSemitones;
    float pitchCents_ = kDefaultPitchCents;
    float shimmerMix_ = kDefaultShimmerMix;

    // Parameters - decay
    float decayAmount_ = kDefaultDecay;

    // Parameters - diffusion
    float diffusionAmount_ = kDefaultDiffusionAmount;
    float diffusionSize_ = kDefaultDiffusionSize;

    // Parameters - filter
    bool filterEnabled_ = false;
    FilterType filterType_ = FilterType::Lowpass;
    float filterCutoffHz_ = kDefaultFilterCutoff;

    // Parameters - output
    float dryWetMix_ = kDefaultDryWetMix;
    float outputGainDb_ = kDefaultOutputGainDb;

    // Scratch buffers for dry signal storage
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;
};

// =============================================================================
// FreezeFeedbackProcessor Inline Implementations
// =============================================================================

inline void FreezeFeedbackProcessor::prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // Prepare pitch shifters
    pitchShifterL_.prepare(sampleRate, maxBlockSize);
    pitchShifterR_.prepare(sampleRate, maxBlockSize);

    // Prepare diffusion network
    diffusion_.prepare(static_cast<float>(sampleRate), maxBlockSize);

    // Allocate buffers
    unpitchedL_.resize(maxBlockSize, 0.0f);
    unpitchedR_.resize(maxBlockSize, 0.0f);
    diffusionOutL_.resize(maxBlockSize, 0.0f);
    diffusionOutR_.resize(maxBlockSize, 0.0f);

    // Calculate initial decay gain
    decayGain_ = calculateDecayGain();
}

inline void FreezeFeedbackProcessor::process(float* left, float* right, std::size_t numSamples) noexcept {
    if (numSamples == 0) return;

    // Store unpitched signal for shimmer mix blending
    for (std::size_t i = 0; i < numSamples; ++i) {
        unpitchedL_[i] = left[i];
        unpitchedR_[i] = right[i];
    }

    // Apply pitch shifting if shimmer mix > 0
    if (shimmerMix_ > 0.001f) {
        pitchShifterL_.process(left, left, numSamples);
        pitchShifterR_.process(right, right, numSamples);
    }

    // Apply diffusion to pitched signal if enabled
    if (diffusionAmount_ > 0.001f) {
        diffusion_.process(left, right, diffusionOutL_.data(), diffusionOutR_.data(), numSamples);

        // Blend pitched with diffused based on diffusion amount
        for (std::size_t i = 0; i < numSamples; ++i) {
            left[i] = left[i] * (1.0f - diffusionAmount_) + diffusionOutL_[i] * diffusionAmount_;
            right[i] = right[i] * (1.0f - diffusionAmount_) + diffusionOutR_[i] * diffusionAmount_;
        }
    }

    // Apply shimmer mix: blend between unpitched and pitched+diffused
    for (std::size_t i = 0; i < numSamples; ++i) {
        left[i] = unpitchedL_[i] * (1.0f - shimmerMix_) + left[i] * shimmerMix_;
        right[i] = unpitchedR_[i] * (1.0f - shimmerMix_) + right[i] * shimmerMix_;
    }

    // Apply decay gain (cumulative per-sample reduction for fade effect)
    // SC-003: At decay 100%, reach -60dB within 500ms
    // decayGain_ is per-sample, so we accumulate across samples
    if (decayGain_ < 0.9999f) {
        float runningGain = currentDecayLevel_;
        for (std::size_t i = 0; i < numSamples; ++i) {
            runningGain *= decayGain_;
            left[i] *= runningGain;
            right[i] *= runningGain;
        }
        currentDecayLevel_ = runningGain;
    }
}

inline void FreezeFeedbackProcessor::reset() noexcept {
    pitchShifterL_.reset();
    pitchShifterR_.reset();
    diffusion_.reset();
    currentDecayLevel_ = 1.0f;  // Reset cumulative decay
}

inline std::size_t FreezeFeedbackProcessor::getLatencySamples() const noexcept {
    return pitchShifterL_.getLatencySamples();
}

inline void FreezeFeedbackProcessor::setPitchSemitones(float semitones) noexcept {
    pitchShifterL_.setSemitones(semitones);
    pitchShifterR_.setSemitones(semitones);
}

inline void FreezeFeedbackProcessor::setPitchCents(float cents) noexcept {
    pitchShifterL_.setCents(cents);
    pitchShifterR_.setCents(cents);
}

inline void FreezeFeedbackProcessor::setShimmerMix(float mix) noexcept {
    shimmerMix_ = std::clamp(mix, 0.0f, 1.0f);
}

inline void FreezeFeedbackProcessor::setDiffusionAmount(float amount) noexcept {
    diffusionAmount_ = std::clamp(amount, 0.0f, 1.0f);
    diffusion_.setDensity(amount * 100.0f);
}

inline void FreezeFeedbackProcessor::setDiffusionSize(float size) noexcept {
    diffusion_.setSize(size);
}

inline void FreezeFeedbackProcessor::setDecayAmount(float decay) noexcept {
    decayAmount_ = std::clamp(decay, 0.0f, 1.0f);
    decayGain_ = calculateDecayGain();
}

inline float FreezeFeedbackProcessor::calculateDecayGain() const noexcept {
    // SC-003: At decay 100%, reach -60dB within 500ms
    // -60dB = 0.001 amplitude
    // At 44.1kHz, 500ms = 22050 samples
    // decayGain^22050 = 0.001
    // decayGain = 0.001^(1/22050) ≈ 0.999686

    if (decayAmount_ <= 0.0f) return 1.0f;  // Infinite sustain

    constexpr float kTargetAmplitude = 0.001f;  // -60dB
    constexpr float kMinDecayTimeMs = 500.0f;   // Fastest decay at 100%

    // Scale decay time: 100% = 500ms, lower values = longer time
    const float decayTimeMs = kMinDecayTimeMs / decayAmount_;
    const float decaySamples = static_cast<float>(decayTimeMs * sampleRate_ / 1000.0);

    return std::pow(kTargetAmplitude, 1.0f / decaySamples);
}

// =============================================================================
// FreezeMode Inline Implementations
// =============================================================================

inline void FreezeMode::prepare(double sampleRate, std::size_t maxBlockSize, float maxDelayMs) noexcept {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    maxDelayMs_ = std::min(maxDelayMs, kMaxDelayMs);

    // Prepare freeze processor FIRST (before setting on feedback network)
    freezeProcessor_.prepare(sampleRate, maxBlockSize);

    // Prepare flexible feedback network (has built-in freeze)
    feedbackNetwork_.prepare(sampleRate, maxBlockSize);
    feedbackNetwork_.setProcessor(&freezeProcessor_);
    feedbackNetwork_.setProcessorMix(100.0f);  // Full processor effect
    feedbackNetwork_.setDelayTimeMs(delayTimeMs_);
    feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
    feedbackNetwork_.setFilterEnabled(filterEnabled_);
    feedbackNetwork_.setFilterCutoff(filterCutoffHz_);
    feedbackNetwork_.setFilterType(filterType_);

    // Allocate scratch buffers
    const std::size_t bufferSize = std::max(maxBlockSize, kMaxDryBufferSize);
    dryBufferL_.resize(bufferSize);
    dryBufferR_.resize(bufferSize);

    // Configure smoothers
    const float sr = static_cast<float>(sampleRate);
    delaySmoother_.configure(kSmoothingTimeMs, sr);
    dryWetSmoother_.configure(kSmoothingTimeMs, sr);
    outputGainSmoother_.configure(kSmoothingTimeMs, sr);

    // Initialize smoothers
    delaySmoother_.snapTo(delayTimeMs_);
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    outputGainSmoother_.snapTo(dbToGain(outputGainDb_));

    // Initialize freeze processor parameters
    freezeProcessor_.setShimmerMix(shimmerMix_ / 100.0f);
    freezeProcessor_.setDiffusionAmount(diffusionAmount_ / 100.0f);
    freezeProcessor_.setDiffusionSize(diffusionSize_);
    freezeProcessor_.setDecayAmount(decayAmount_ / 100.0f);
    freezeProcessor_.setPitchSemitones(pitchSemitones_);
    freezeProcessor_.setPitchCents(pitchCents_);

    // Snap feedback network parameters
    feedbackNetwork_.snapParameters();

    prepared_ = true;
}

inline void FreezeMode::reset() noexcept {
    feedbackNetwork_.reset();
    freezeProcessor_.reset();

    delaySmoother_.snapTo(delayTimeMs_);
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    outputGainSmoother_.snapTo(dbToGain(outputGainDb_));

    feedbackNetwork_.snapParameters();
}

inline void FreezeMode::snapParameters() noexcept {
    delaySmoother_.snapTo(delayTimeMs_);
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    outputGainSmoother_.snapTo(dbToGain(outputGainDb_));

    feedbackNetwork_.setDelayTimeMs(delayTimeMs_);
    feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
    feedbackNetwork_.setFilterEnabled(filterEnabled_);
    feedbackNetwork_.setFilterCutoff(filterCutoffHz_);
    feedbackNetwork_.setFilterType(filterType_);
    feedbackNetwork_.snapParameters();

    freezeProcessor_.setShimmerMix(shimmerMix_ / 100.0f);
    freezeProcessor_.setDiffusionAmount(diffusionAmount_ / 100.0f);
    freezeProcessor_.setDiffusionSize(diffusionSize_);
    freezeProcessor_.setDecayAmount(decayAmount_ / 100.0f);
    freezeProcessor_.setPitchSemitones(pitchSemitones_);
    freezeProcessor_.setPitchCents(pitchCents_);
}

inline void FreezeMode::setFreezeEnabled(bool enabled) noexcept {
    feedbackNetwork_.setFreezeEnabled(enabled);
}

inline bool FreezeMode::isFreezeEnabled() const noexcept {
    return feedbackNetwork_.isFreezeEnabled();
}

inline void FreezeMode::setDelayTimeMs(float ms) noexcept {
    delayTimeMs_ = std::clamp(ms, kMinDelayMs, maxDelayMs_);
    delaySmoother_.setTarget(delayTimeMs_);
    feedbackNetwork_.setDelayTimeMs(delayTimeMs_);
}

inline void FreezeMode::setNoteValue(NoteValue note, NoteModifier modifier) noexcept {
    noteValue_ = note;
    noteModifier_ = modifier;
}

inline void FreezeMode::setFeedbackAmount(float amount) noexcept {
    feedbackAmount_ = std::clamp(amount, kMinFeedback, kMaxFeedback);
    feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
}

inline void FreezeMode::setPitchSemitones(float semitones) noexcept {
    pitchSemitones_ = std::clamp(semitones, kMinPitchSemitones, kMaxPitchSemitones);
    freezeProcessor_.setPitchSemitones(pitchSemitones_);
}

inline void FreezeMode::setPitchCents(float cents) noexcept {
    pitchCents_ = std::clamp(cents, kMinPitchCents, kMaxPitchCents);
    freezeProcessor_.setPitchCents(pitchCents_);
}

inline void FreezeMode::setShimmerMix(float percent) noexcept {
    shimmerMix_ = std::clamp(percent, kMinShimmerMix, kMaxShimmerMix);
    freezeProcessor_.setShimmerMix(shimmerMix_ / 100.0f);
}

inline void FreezeMode::setDecay(float percent) noexcept {
    decayAmount_ = std::clamp(percent, kMinDecay, kMaxDecay);
    freezeProcessor_.setDecayAmount(decayAmount_ / 100.0f);
}

inline void FreezeMode::setDiffusionAmount(float percent) noexcept {
    diffusionAmount_ = std::clamp(percent, kMinDiffusion, kMaxDiffusion);
    freezeProcessor_.setDiffusionAmount(diffusionAmount_ / 100.0f);
}

inline void FreezeMode::setDiffusionSize(float percent) noexcept {
    diffusionSize_ = std::clamp(percent, kMinDiffusion, kMaxDiffusion);
    freezeProcessor_.setDiffusionSize(diffusionSize_);
}

inline void FreezeMode::setFilterEnabled(bool enabled) noexcept {
    filterEnabled_ = enabled;
    feedbackNetwork_.setFilterEnabled(enabled);
}

inline void FreezeMode::setFilterType(FilterType type) noexcept {
    filterType_ = type;
    feedbackNetwork_.setFilterType(type);
}

inline void FreezeMode::setFilterCutoff(float hz) noexcept {
    filterCutoffHz_ = std::clamp(hz, kMinFilterCutoff, kMaxFilterCutoff);
    feedbackNetwork_.setFilterCutoff(filterCutoffHz_);
}

inline void FreezeMode::setDryWetMix(float percent) noexcept {
    dryWetMix_ = std::clamp(percent, kMinDryWetMix, kMaxDryWetMix);
    dryWetSmoother_.setTarget(dryWetMix_ / 100.0f);
}

inline void FreezeMode::setOutputGainDb(float dB) noexcept {
    outputGainDb_ = std::clamp(dB, kMinOutputGainDb, kMaxOutputGainDb);
    outputGainSmoother_.setTarget(dbToGain(outputGainDb_));
}

inline std::size_t FreezeMode::getLatencySamples() const noexcept {
    return feedbackNetwork_.getLatencySamples();
}

inline float FreezeMode::calculateTempoSyncedDelay(const BlockContext& ctx) const noexcept {
    const std::size_t delaySamples = ctx.tempoToSamples(noteValue_, noteModifier_);
    float delayMs = static_cast<float>(delaySamples * 1000.0 / ctx.sampleRate);
    return std::clamp(delayMs, kMinDelayMs, maxDelayMs_);
}

inline void FreezeMode::process(float* left, float* right, std::size_t numSamples,
                                 const BlockContext& ctx) noexcept {
    if (!prepared_ || numSamples == 0) return;

    // Calculate base delay time (tempo sync or free)
    float baseDelayMs = delayTimeMs_;
    if (timeMode_ == TimeMode::Synced) {
        baseDelayMs = calculateTempoSyncedDelay(ctx);
        feedbackNetwork_.setDelayTimeMs(baseDelayMs);
    }
    delaySmoother_.setTarget(baseDelayMs);

    // Process in chunks
    std::size_t samplesProcessed = 0;
    while (samplesProcessed < numSamples) {
        const std::size_t chunkSize = std::min(maxBlockSize_, numSamples - samplesProcessed);
        float* chunkLeft = left + samplesProcessed;
        float* chunkRight = right + samplesProcessed;

        // Store dry signal for mixing
        for (std::size_t i = 0; i < chunkSize; ++i) {
            dryBufferL_[i] = chunkLeft[i];
            dryBufferR_[i] = chunkRight[i];
        }

        // Process through feedback network
        feedbackNetwork_.process(chunkLeft, chunkRight, chunkSize, ctx);

        // Mix dry/wet with smoothed parameters
        for (std::size_t i = 0; i < chunkSize; ++i) {
            const float currentDryWet = dryWetSmoother_.process();
            const float currentOutputGain = outputGainSmoother_.process();

            chunkLeft[i] = (dryBufferL_[i] * (1.0f - currentDryWet) + chunkLeft[i] * currentDryWet) * currentOutputGain;
            chunkRight[i] = (dryBufferR_[i] * (1.0f - currentDryWet) + chunkRight[i] * currentDryWet) * currentOutputGain;
        }

        samplesProcessed += chunkSize;
    }
}

} // namespace DSP
} // namespace Iterum
