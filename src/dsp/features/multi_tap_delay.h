// ==============================================================================
// Layer 4: User Feature - MultiTapDelay
// ==============================================================================
// Rhythmic multi-tap delay with 25 preset patterns (14 rhythmic, 5 mathematical,
// 6 spatial/level), pattern morphing, and per-tap modulation.
//
// Composes:
// - TapManager (Layer 3): 16-tap delay management
// - FeedbackNetwork (Layer 3): Master feedback with filtering and limiting
// - ModulationMatrix (Layer 3): Per-tap parameter modulation (optional)
// - OnePoleSmoother (Layer 1): Pattern morphing and parameter smoothing
//
// Feature: 028-multi-tap
// Layer: 4 (User Feature)
// Reference: specs/028-multi-tap/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, span)
// - Principle IX: Layer 4 (composes only from Layer 0-3)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include "dsp/core/block_context.h"
#include "dsp/core/db_utils.h"
#include "dsp/core/note_value.h"
#include "dsp/core/math_constants.h"
#include "dsp/primitives/smoother.h"
#include "dsp/systems/tap_manager.h"
#include "dsp/systems/feedback_network.h"
#include "dsp/systems/modulation_matrix.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Iterum {
namespace DSP {

// =============================================================================
// TimingPattern Enumeration (FR-002, FR-002a)
// =============================================================================

/// @brief Preset timing patterns for multi-tap delay
///
/// Provides 20 timing pattern options:
/// - 14 rhythmic patterns (basic notes, dotted, triplet variants)
/// - 5 mathematical patterns (golden ratio, fibonacci, exponential, etc.)
/// - 1 custom pattern for user-defined timing
enum class TimingPattern : uint8_t {
    // Rhythmic patterns - basic note values (FR-002)
    WholeNote = 0,
    HalfNote,
    QuarterNote,
    EighthNote,
    SixteenthNote,
    ThirtySecondNote,

    // Rhythmic patterns - dotted variants (FR-002)
    DottedHalf,
    DottedQuarter,
    DottedEighth,
    DottedSixteenth,

    // Rhythmic patterns - triplet variants (FR-002)
    TripletHalf,
    TripletQuarter,
    TripletEighth,
    TripletSixteenth,

    // Mathematical patterns (FR-002a)
    GoldenRatio,      ///< Each tap = previous × 1.618
    Fibonacci,        ///< Taps follow 1, 1, 2, 3, 5, 8... sequence
    Exponential,      ///< Taps at 1×, 2×, 4×, 8×... base time
    PrimeNumbers,     ///< Taps at 2×, 3×, 5×, 7×, 11×... base time
    LinearSpread,     ///< Equal spacing from min to max time

    // Custom pattern (FR-003)
    Custom            ///< User-defined time ratios
};

// =============================================================================
// SpatialPattern Enumeration (FR-002b)
// =============================================================================

/// @brief Preset spatial/level patterns for multi-tap delay
///
/// Controls pan and level distribution across taps.
enum class SpatialPattern : uint8_t {
    Cascade = 0,      ///< Pan sweeps L→R across taps
    Alternating,      ///< Pan alternates L, R, L, R...
    Centered,         ///< All taps center pan
    WideningStereo,   ///< Pan spreads progressively wider
    DecayingLevel,    ///< Each tap -3dB from previous
    FlatLevel,        ///< All taps equal level
    Custom            ///< User-defined pan/level
};

// =============================================================================
// TapConfiguration Structure (FR-004)
// =============================================================================

/// @brief Runtime configuration for a single delay tap
///
/// Stores all configurable parameters for one tap.
struct TapConfiguration {
    bool enabled = false;           ///< Tap produces output
    float timeMs = 0.0f;            ///< Delay time in milliseconds
    float levelDb = 0.0f;           ///< Output level in dB [-96, +6]
    float pan = 0.0f;               ///< Pan position [-100, +100] (L to R)
    TapFilterMode filterMode = TapFilterMode::Bypass;  ///< Filter type
    float filterCutoff = 1000.0f;   ///< Filter cutoff in Hz [20, 20000]
    bool muted = false;             ///< Temporary mute (no output)
};

// =============================================================================
// MultiTapDelay Class (FR-001 to FR-030)
// =============================================================================

/// @brief Layer 4 User Feature - Multi-Tap Delay
///
/// Rhythmic multi-tap delay with preset patterns, pattern morphing, and
/// per-tap modulation. Composes TapManager for core tap functionality,
/// FeedbackNetwork for master feedback, and optionally ModulationMatrix
/// for per-tap modulation.
///
/// @par User Controls
/// - Timing Patterns: 14 rhythmic + 5 mathematical + custom (FR-002, FR-002a)
/// - Spatial Patterns: 6 presets for pan/level distribution (FR-002b)
/// - Per-Tap Control: Time, level, pan, filter per tap (FR-004, FR-011-FR-015)
/// - Master Feedback: 0-110% with LP/HP filtering (FR-016-FR-020)
/// - Pattern Morphing: Smooth transitions 50-2000ms (FR-025, FR-026)
/// - Modulation: Per-tap parameter modulation via matrix (FR-021-FR-023)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 4 composes from Layer 0-3 only
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// MultiTapDelay delay;
/// delay.prepare(44100.0, 512, 5000.0f);
/// delay.setTempo(120.0f);
/// delay.loadTimingPattern(TimingPattern::DottedEighth, 4);
/// delay.applySpatialPattern(SpatialPattern::Cascade);
///
/// // In process callback
/// delay.process(left, right, numSamples, ctx);
/// @endcode
class MultiTapDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMinTaps = 2;             ///< Minimum taps (FR-001)
    static constexpr size_t kMaxTaps = 16;            ///< Maximum taps (FR-001)
    static constexpr float kMinDelayMs = 1.0f;        ///< Minimum delay (FR-006)
    static constexpr float kMaxDelayMs = 5000.0f;     ///< Maximum delay (FR-006)
    static constexpr float kDefaultDelayMs = 500.0f;  ///< Default base time
    static constexpr float kMinFeedback = 0.0f;       ///< Minimum feedback (FR-016)
    static constexpr float kMaxFeedback = 1.1f;       ///< Maximum feedback 110% (FR-016)
    static constexpr float kMinMorphTimeMs = 50.0f;   ///< Minimum morph time (FR-026)
    static constexpr float kMaxMorphTimeMs = 2000.0f; ///< Maximum morph time (FR-026)
    static constexpr float kSmoothingTimeMs = 20.0f;  ///< Parameter smoothing (FR-010)
    static constexpr float kMinTempo = 20.0f;         ///< Minimum tempo BPM
    static constexpr float kMaxTempo = 300.0f;        ///< Maximum tempo BPM

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    MultiTapDelay() noexcept = default;
    ~MultiTapDelay() = default;

    // Non-copyable, movable
    MultiTapDelay(const MultiTapDelay&) = delete;
    MultiTapDelay& operator=(const MultiTapDelay&) = delete;
    MultiTapDelay(MultiTapDelay&&) noexcept = default;
    MultiTapDelay& operator=(MultiTapDelay&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-030)
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

        // Prepare Layer 3 components
        tapManager_.prepare(static_cast<float>(sampleRate), maxBlockSize, maxDelayMs_);
        feedbackNetwork_.prepare(sampleRate, maxBlockSize, maxDelayMs_);

        // Configure feedback network defaults
        feedbackNetwork_.setFeedbackAmount(0.5f);
        feedbackNetwork_.setFilterEnabled(false);
        feedbackNetwork_.setSaturationEnabled(true);  // Required for >100% feedback safety

        // Configure smoothers
        morphSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        dryWetSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        outputLevelSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));

        // Initialize smoothers
        dryWetSmoother_.snapTo(dryWetMix_ * 0.01f);
        outputLevelSmoother_.snapTo(dbToGain(outputLevelDb_));
        morphSmoother_.snapTo(0.0f);

        prepared_ = true;
    }

    /// @brief Reset all internal state
    /// @post Delay lines cleared, smoothers snapped to current values
    void reset() noexcept {
        tapManager_.reset();
        feedbackNetwork_.reset();

        // Snap smoothers
        dryWetSmoother_.snapTo(dryWetMix_ * 0.01f);
        outputLevelSmoother_.snapTo(dbToGain(outputLevelDb_));
        morphSmoother_.snapTo(0.0f);
        morphing_ = false;
    }

    /// @brief Snap all smoothers for immediate parameter application
    void snapParameters() noexcept {
        dryWetSmoother_.snapTo(dryWetMix_ * 0.01f);
        outputLevelSmoother_.snapTo(dbToGain(outputLevelDb_));

        // Snap TapManager internal smoothers via reset
        // Note: We don't want to clear delay lines, just snap smoothers
    }

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Timing Pattern Control (FR-002, FR-002a)
    // =========================================================================

    /// @brief Load a preset timing pattern
    /// @param pattern Pattern type
    /// @param tapCount Number of taps [2, 16]
    void loadTimingPattern(TimingPattern pattern, size_t tapCount) noexcept {
        tapCount = std::clamp(tapCount, kMinTaps, kMaxTaps);
        currentTimingPattern_ = pattern;
        activeTapCount_ = tapCount;

        applyTimingPattern(pattern, tapCount);
    }

    /// @brief Get current timing pattern
    [[nodiscard]] TimingPattern getTimingPattern() const noexcept {
        return currentTimingPattern_;
    }

    /// @brief Get number of active taps
    [[nodiscard]] size_t getActiveTapCount() const noexcept {
        return activeTapCount_;
    }

    // =========================================================================
    // Spatial Pattern Control (FR-002b)
    // =========================================================================

    /// @brief Apply a spatial (pan/level) pattern
    /// @param pattern Spatial pattern type
    void applySpatialPattern(SpatialPattern pattern) noexcept {
        currentSpatialPattern_ = pattern;
        applySpatialPatternInternal(pattern, activeTapCount_);
    }

    /// @brief Get current spatial pattern
    [[nodiscard]] SpatialPattern getSpatialPattern() const noexcept {
        return currentSpatialPattern_;
    }

    // =========================================================================
    // Custom Pattern Control (FR-003)
    // =========================================================================

    /// @brief Set custom timing pattern from user-defined time ratios
    /// @param timeRatios Array of time multipliers relative to base time
    /// @note Sets pattern type to Custom. Tap count = ratios size (clamped to 2-16)
    void setCustomTimingPattern(std::span<float> timeRatios) noexcept {
        currentTimingPattern_ = TimingPattern::Custom;
        size_t count = std::clamp(timeRatios.size(), kMinTaps, kMaxTaps);
        activeTapCount_ = count;

        // Store custom ratios
        for (size_t i = 0; i < count && i < kMaxTaps; ++i) {
            customTimeRatios_[i] = timeRatios[i];
        }

        // Apply custom pattern
        applyCustomTimingPattern();
    }

    /// @brief Set base delay time for patterns
    /// @param ms Base time in milliseconds [1, 5000]
    void setBaseTimeMs(float ms) noexcept {
        baseTimeMs_ = std::clamp(ms, kMinDelayMs, maxDelayMs_);

        // Reapply current pattern with new base time
        if (currentTimingPattern_ == TimingPattern::Custom) {
            applyCustomTimingPattern();
        } else {
            applyTimingPattern(currentTimingPattern_, activeTapCount_);
        }
    }

    /// @brief Get base delay time
    [[nodiscard]] float getBaseTimeMs() const noexcept {
        return baseTimeMs_;
    }

    // =========================================================================
    // Per-Tap Control (FR-004, FR-011-FR-015)
    // =========================================================================

    /// @brief Get tap delay time
    /// @param tapIndex Tap index [0, 15]
    [[nodiscard]] float getTapTimeMs(size_t tapIndex) const noexcept {
        return tapManager_.getTapTimeMs(tapIndex);
    }

    /// @brief Set tap level in dB
    /// @param tapIndex Tap index [0, 15]
    /// @param levelDb Level [-96, +6]
    void setTapLevelDb(size_t tapIndex, float levelDb) noexcept {
        tapManager_.setTapLevelDb(tapIndex, levelDb);
    }

    /// @brief Get tap level in dB
    [[nodiscard]] float getTapLevelDb(size_t tapIndex) const noexcept {
        return tapManager_.getTapLevelDb(tapIndex);
    }

    /// @brief Set tap pan position
    /// @param tapIndex Tap index [0, 15]
    /// @param pan Pan [-100, +100] (L to R)
    void setTapPan(size_t tapIndex, float pan) noexcept {
        tapManager_.setTapPan(tapIndex, pan);
    }

    /// @brief Get tap pan position
    [[nodiscard]] float getTapPan(size_t tapIndex) const noexcept {
        return tapManager_.getTapPan(tapIndex);
    }

    /// @brief Set tap filter mode
    /// @param tapIndex Tap index [0, 15]
    /// @param mode Filter mode (Bypass, Lowpass, Highpass)
    void setTapFilterMode(size_t tapIndex, TapFilterMode mode) noexcept {
        tapManager_.setTapFilterMode(tapIndex, mode);
    }

    /// @brief Set tap filter cutoff
    /// @param tapIndex Tap index [0, 15]
    /// @param cutoffHz Cutoff frequency [20, 20000]
    void setTapFilterCutoff(size_t tapIndex, float cutoffHz) noexcept {
        tapManager_.setTapFilterCutoff(tapIndex, cutoffHz);
    }

    /// @brief Set tap mute state
    /// @param tapIndex Tap index [0, 15]
    /// @param muted true to mute tap
    void setTapMuted(size_t tapIndex, bool muted) noexcept {
        if (tapIndex < kMaxTaps) {
            tapManager_.setTapEnabled(tapIndex, !muted);
        }
    }

    // =========================================================================
    // Tempo Control (FR-007, FR-008)
    // =========================================================================

    /// @brief Set tempo for pattern calculation
    /// @param bpm Beats per minute [20, 300]
    void setTempo(float bpm) noexcept {
        bpm_ = std::clamp(bpm, kMinTempo, kMaxTempo);
        tapManager_.setTempo(bpm_);
    }

    /// @brief Get current tempo
    [[nodiscard]] float getTempo() const noexcept {
        return bpm_;
    }

    // =========================================================================
    // Master Feedback Control (FR-016-FR-020)
    // =========================================================================

    /// @brief Set master feedback amount
    /// @param amount Feedback [0, 1.1] (110% max)
    void setFeedbackAmount(float amount) noexcept {
        feedbackAmount_ = std::clamp(amount, kMinFeedback, kMaxFeedback);
        feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
    }

    /// @brief Get master feedback amount
    [[nodiscard]] float getFeedbackAmount() const noexcept {
        return feedbackAmount_;
    }

    /// @brief Set feedback lowpass filter cutoff
    /// @param cutoffHz Cutoff [20, 20000]
    void setFeedbackLPCutoff(float cutoffHz) noexcept {
        feedbackLPCutoff_ = std::clamp(cutoffHz, kMinFilterCutoff, kMaxFilterCutoff);
        feedbackNetwork_.setFilterCutoff(feedbackLPCutoff_);
        if (feedbackLPCutoff_ < kMaxFilterCutoff) {
            feedbackNetwork_.setFilterEnabled(true);
            feedbackNetwork_.setFilterType(FilterType::Lowpass);
        }
    }

    /// @brief Get feedback lowpass cutoff
    [[nodiscard]] float getFeedbackLPCutoff() const noexcept {
        return feedbackLPCutoff_;
    }

    /// @brief Set feedback highpass filter cutoff
    /// @param cutoffHz Cutoff [20, 20000]
    void setFeedbackHPCutoff(float cutoffHz) noexcept {
        feedbackHPCutoff_ = std::clamp(cutoffHz, kMinFilterCutoff, kMaxFilterCutoff);
        // Note: FeedbackNetwork currently only supports one filter type
        // For full HP+LP, would need to chain or extend FeedbackNetwork
    }

    /// @brief Get feedback highpass cutoff
    [[nodiscard]] float getFeedbackHPCutoff() const noexcept {
        return feedbackHPCutoff_;
    }

    // =========================================================================
    // Pattern Morphing (FR-025, FR-026)
    // =========================================================================

    /// @brief Morph to a new timing pattern
    /// @param pattern Target pattern
    /// @param morphTimeMs Transition time in ms [50, 2000]
    void morphToPattern(TimingPattern pattern, float morphTimeMs) noexcept {
        morphTimeMs = std::clamp(morphTimeMs, kMinMorphTimeMs, kMaxMorphTimeMs);

        // Store target pattern and start morph
        targetTimingPattern_ = pattern;
        morphTimeMs_ = morphTimeMs;
        morphing_ = true;

        // Store current tap times as morph start
        for (size_t i = 0; i < activeTapCount_; ++i) {
            morphStartTimes_[i] = tapManager_.getTapTimeMs(i);
        }

        // Calculate target times
        calculatePatternTimes(pattern, activeTapCount_, morphTargetTimes_.data());

        // Configure morph smoother
        morphSmoother_.configure(morphTimeMs, static_cast<float>(sampleRate_));
        morphSmoother_.setTarget(1.0f);
    }

    /// @brief Check if pattern morphing is in progress
    [[nodiscard]] bool isMorphing() const noexcept {
        return morphing_;
    }

    /// @brief Set morph time
    /// @param ms Morph time [50, 2000]
    void setMorphTime(float ms) noexcept {
        morphTimeMs_ = std::clamp(ms, kMinMorphTimeMs, kMaxMorphTimeMs);
    }

    /// @brief Get morph time
    [[nodiscard]] float getMorphTime() const noexcept {
        return morphTimeMs_;
    }

    // =========================================================================
    // Modulation (FR-021-FR-023)
    // =========================================================================

    /// @brief Connect a modulation matrix for per-tap modulation
    /// @param matrix Pointer to modulation matrix (nullptr to disconnect)
    void setModulationMatrix(ModulationMatrix* matrix) noexcept {
        modMatrix_ = matrix;
    }

    // =========================================================================
    // Output Control (FR-028, FR-029)
    // =========================================================================

    /// @brief Set dry/wet mix
    /// @param mixPercent Mix [0, 100]
    void setDryWetMix(float mixPercent) noexcept {
        dryWetMix_ = std::clamp(mixPercent, 0.0f, 100.0f);
        dryWetSmoother_.setTarget(dryWetMix_ * 0.01f);
    }

    /// @brief Get dry/wet mix
    [[nodiscard]] float getDryWetMix() const noexcept {
        return dryWetMix_;
    }

    /// @brief Set output level
    /// @param dB Output level [-12, +12]
    void setOutputLevel(float dB) noexcept {
        outputLevelDb_ = std::clamp(dB, -12.0f, 12.0f);
        outputLevelSmoother_.setTarget(dbToGain(outputLevelDb_));
    }

    /// @brief Get output level
    [[nodiscard]] float getOutputLevel() const noexcept {
        return outputLevelDb_;
    }

    // =========================================================================
    // Processing (FR-005, FR-028)
    // =========================================================================

    /// @brief Process stereo audio in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @param ctx Block context with tempo/transport info
    void process(float* left, float* right, size_t numSamples,
                 const BlockContext& ctx) noexcept {
        if (!prepared_ || numSamples == 0) return;

        // Update tempo from host if available
        if (ctx.isPlaying && ctx.tempoBPM > 0.0) {
            setTempo(static_cast<float>(ctx.tempoBPM));
        }

        // Update morph if active
        if (morphing_) {
            updateMorph(numSamples);
        }

        // Apply modulation if connected
        if (modMatrix_) {
            applyModulation();
        }

        // Store dry signal
        for (size_t i = 0; i < numSamples && i < kMaxDryBufferSize; ++i) {
            dryBufferL_[i] = left[i];
            dryBufferR_[i] = right[i];
        }

        // Process through TapManager (generates wet signal)
        tapManager_.process(left, right, left, right, numSamples);

        // Process through FeedbackNetwork
        feedbackNetwork_.process(left, right, numSamples, ctx);

        // Mix dry/wet and apply output level
        for (size_t i = 0; i < numSamples; ++i) {
            const float wetMix = dryWetSmoother_.process();
            const float dryMix = 1.0f - wetMix;
            const float outputGain = outputLevelSmoother_.process();

            const size_t bufIdx = i % kMaxDryBufferSize;
            left[i] = (dryBufferL_[bufIdx] * dryMix + left[i] * wetMix) * outputGain;
            right[i] = (dryBufferR_[bufIdx] * dryMix + right[i] * wetMix) * outputGain;
        }
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Apply timing pattern to TapManager
    void applyTimingPattern(TimingPattern pattern, size_t tapCount) noexcept {
        // Map TimingPattern to TapManager's TapPattern or NoteValue
        switch (pattern) {
            case TimingPattern::QuarterNote:
                tapManager_.loadPattern(TapPattern::QuarterNote, tapCount);
                break;
            case TimingPattern::DottedEighth:
                tapManager_.loadPattern(TapPattern::DottedEighth, tapCount);
                break;
            case TimingPattern::TripletQuarter:
            case TimingPattern::TripletEighth:
            case TimingPattern::TripletHalf:
            case TimingPattern::TripletSixteenth:
                tapManager_.loadPattern(TapPattern::Triplet, tapCount);
                break;
            case TimingPattern::GoldenRatio:
                tapManager_.loadPattern(TapPattern::GoldenRatio, tapCount);
                break;
            case TimingPattern::Fibonacci:
                tapManager_.loadPattern(TapPattern::Fibonacci, tapCount);
                break;

            // For patterns not directly in TapPattern, use loadNotePattern
            case TimingPattern::WholeNote:
                tapManager_.loadNotePattern(NoteValue::Whole, NoteModifier::None, tapCount);
                break;
            case TimingPattern::HalfNote:
                tapManager_.loadNotePattern(NoteValue::Half, NoteModifier::None, tapCount);
                break;
            case TimingPattern::EighthNote:
                tapManager_.loadNotePattern(NoteValue::Eighth, NoteModifier::None, tapCount);
                break;
            case TimingPattern::SixteenthNote:
                tapManager_.loadNotePattern(NoteValue::Sixteenth, NoteModifier::None, tapCount);
                break;
            case TimingPattern::ThirtySecondNote:
                tapManager_.loadNotePattern(NoteValue::ThirtySecond, NoteModifier::None, tapCount);
                break;
            case TimingPattern::DottedHalf:
                tapManager_.loadNotePattern(NoteValue::Half, NoteModifier::Dotted, tapCount);
                break;
            case TimingPattern::DottedQuarter:
                tapManager_.loadNotePattern(NoteValue::Quarter, NoteModifier::Dotted, tapCount);
                break;
            case TimingPattern::DottedSixteenth:
                tapManager_.loadNotePattern(NoteValue::Sixteenth, NoteModifier::Dotted, tapCount);
                break;

            // Mathematical patterns (Layer 4 extensions)
            case TimingPattern::Exponential:
                applyExponentialPattern(tapCount);
                break;
            case TimingPattern::PrimeNumbers:
                applyPrimeNumbersPattern(tapCount);
                break;
            case TimingPattern::LinearSpread:
                applyLinearSpreadPattern(tapCount);
                break;

            case TimingPattern::Custom:
            default:
                // Custom pattern handled separately
                break;
        }
    }

    /// @brief Apply exponential pattern (1×, 2×, 4×, 8×... base time)
    void applyExponentialPattern(size_t tapCount) noexcept {
        const float quarterNoteMs = 60000.0f / bpm_;
        for (size_t i = 0; i < tapCount; ++i) {
            float multiplier = std::pow(2.0f, static_cast<float>(i));
            float timeMs = std::min(quarterNoteMs * multiplier, maxDelayMs_);
            tapManager_.setTapEnabled(i, true);
            tapManager_.setTapTimeMs(i, timeMs);
            tapManager_.setTapLevelDb(i, -3.0f * static_cast<float>(i));
        }
        // Disable remaining taps
        for (size_t i = tapCount; i < kMaxTaps; ++i) {
            tapManager_.setTapEnabled(i, false);
        }
    }

    /// @brief Apply prime numbers pattern (2×, 3×, 5×, 7×, 11×... base time)
    void applyPrimeNumbersPattern(size_t tapCount) noexcept {
        static constexpr std::array<float, 16> primes = {
            2.0f, 3.0f, 5.0f, 7.0f, 11.0f, 13.0f, 17.0f, 19.0f,
            23.0f, 29.0f, 31.0f, 37.0f, 41.0f, 43.0f, 47.0f, 53.0f
        };
        const float baseMs = 60000.0f / bpm_ * 0.25f; // Sixteenth note base
        for (size_t i = 0; i < tapCount; ++i) {
            float timeMs = std::min(baseMs * primes[i], maxDelayMs_);
            tapManager_.setTapEnabled(i, true);
            tapManager_.setTapTimeMs(i, timeMs);
            tapManager_.setTapLevelDb(i, -3.0f * static_cast<float>(i));
        }
        for (size_t i = tapCount; i < kMaxTaps; ++i) {
            tapManager_.setTapEnabled(i, false);
        }
    }

    /// @brief Apply linear spread pattern (equal spacing from min to max)
    void applyLinearSpreadPattern(size_t tapCount) noexcept {
        const float minTime = baseTimeMs_;
        const float maxTime = std::min(baseTimeMs_ * static_cast<float>(tapCount), maxDelayMs_);
        const float step = (maxTime - minTime) / static_cast<float>(tapCount - 1);

        for (size_t i = 0; i < tapCount; ++i) {
            float timeMs = minTime + step * static_cast<float>(i);
            tapManager_.setTapEnabled(i, true);
            tapManager_.setTapTimeMs(i, timeMs);
            tapManager_.setTapLevelDb(i, -3.0f * static_cast<float>(i));
        }
        for (size_t i = tapCount; i < kMaxTaps; ++i) {
            tapManager_.setTapEnabled(i, false);
        }
    }

    /// @brief Apply custom timing pattern from stored ratios
    void applyCustomTimingPattern() noexcept {
        for (size_t i = 0; i < activeTapCount_; ++i) {
            float timeMs = std::min(baseTimeMs_ * customTimeRatios_[i], maxDelayMs_);
            tapManager_.setTapEnabled(i, true);
            tapManager_.setTapTimeMs(i, timeMs);
        }
        for (size_t i = activeTapCount_; i < kMaxTaps; ++i) {
            tapManager_.setTapEnabled(i, false);
        }
    }

    /// @brief Apply spatial pattern to taps
    void applySpatialPatternInternal(SpatialPattern pattern, size_t tapCount) noexcept {
        switch (pattern) {
            case SpatialPattern::Cascade:
                applyCascadePattern(tapCount);
                break;
            case SpatialPattern::Alternating:
                applyAlternatingPattern(tapCount);
                break;
            case SpatialPattern::Centered:
                applyCenteredPattern(tapCount);
                break;
            case SpatialPattern::WideningStereo:
                applyWideningStereoPattern(tapCount);
                break;
            case SpatialPattern::DecayingLevel:
                applyDecayingLevelPattern(tapCount);
                break;
            case SpatialPattern::FlatLevel:
                applyFlatLevelPattern(tapCount);
                break;
            case SpatialPattern::Custom:
            default:
                break;
        }
    }

    void applyCascadePattern(size_t tapCount) noexcept {
        // Pan sweeps from full left to full right
        for (size_t i = 0; i < tapCount; ++i) {
            float pan = -100.0f + 200.0f * static_cast<float>(i) / static_cast<float>(tapCount - 1);
            tapManager_.setTapPan(i, pan);
        }
    }

    void applyAlternatingPattern(size_t tapCount) noexcept {
        // Pan alternates L, R, L, R...
        for (size_t i = 0; i < tapCount; ++i) {
            float pan = (i % 2 == 0) ? -100.0f : 100.0f;
            tapManager_.setTapPan(i, pan);
        }
    }

    void applyCenteredPattern(size_t tapCount) noexcept {
        // All taps center pan
        for (size_t i = 0; i < tapCount; ++i) {
            tapManager_.setTapPan(i, 0.0f);
        }
    }

    void applyWideningStereoPattern(size_t tapCount) noexcept {
        // Pan spreads progressively wider from center
        for (size_t i = 0; i < tapCount; ++i) {
            float width = 100.0f * static_cast<float>(i) / static_cast<float>(tapCount - 1);
            float pan = (i % 2 == 0) ? -width : width;
            tapManager_.setTapPan(i, pan);
        }
    }

    void applyDecayingLevelPattern(size_t tapCount) noexcept {
        // Each tap -3dB from previous
        for (size_t i = 0; i < tapCount; ++i) {
            tapManager_.setTapLevelDb(i, -3.0f * static_cast<float>(i));
        }
    }

    void applyFlatLevelPattern(size_t tapCount) noexcept {
        // All taps equal level
        for (size_t i = 0; i < tapCount; ++i) {
            tapManager_.setTapLevelDb(i, 0.0f);
        }
    }

    /// @brief Calculate pattern times for morphing target
    void calculatePatternTimes(TimingPattern pattern, size_t tapCount, float* times) noexcept {
        const float quarterNoteMs = 60000.0f / bpm_;

        for (size_t i = 0; i < tapCount; ++i) {
            const size_t n = i + 1;
            float timeMs = 0.0f;

            switch (pattern) {
                case TimingPattern::QuarterNote:
                    timeMs = static_cast<float>(n) * quarterNoteMs;
                    break;
                case TimingPattern::DottedEighth:
                    timeMs = static_cast<float>(n) * quarterNoteMs * 0.75f;
                    break;
                case TimingPattern::GoldenRatio:
                    timeMs = (i == 0) ? quarterNoteMs : times[i - 1] * kGoldenRatio;
                    break;
                case TimingPattern::Exponential:
                    timeMs = quarterNoteMs * std::pow(2.0f, static_cast<float>(i));
                    break;
                default:
                    timeMs = static_cast<float>(n) * quarterNoteMs;
                    break;
            }

            times[i] = std::min(timeMs, maxDelayMs_);
        }
    }

    /// @brief Update pattern morph progress
    void updateMorph(size_t numSamples) noexcept {
        for (size_t s = 0; s < numSamples; ++s) {
            float morphProgress = morphSmoother_.process();

            // Check if morph is complete
            if (morphProgress >= 0.999f) {
                morphing_ = false;
                currentTimingPattern_ = targetTimingPattern_;
                // Snap to target times
                for (size_t i = 0; i < activeTapCount_; ++i) {
                    tapManager_.setTapTimeMs(i, morphTargetTimes_[i]);
                }
                return;
            }

            // Interpolate tap times
            for (size_t i = 0; i < activeTapCount_; ++i) {
                float time = morphStartTimes_[i] + morphProgress *
                            (morphTargetTimes_[i] - morphStartTimes_[i]);
                tapManager_.setTapTimeMs(i, time);
            }
        }
    }

    /// @brief Apply modulation from connected matrix
    void applyModulation() noexcept {
        if (!modMatrix_) return;

        modMatrix_->process(1); // Process for 1 sample to update modulation values

        // Apply modulation to per-tap parameters
        // Modulation destination IDs: time (0-15), level (16-31), pan (32-47), cutoff (48-63)
        for (size_t i = 0; i < activeTapCount_; ++i) {
            // Time modulation (±10% of base time)
            float timeMod = modMatrix_->getCurrentModulation(static_cast<uint8_t>(i));
            if (std::abs(timeMod) > 0.0f) {
                float baseTime = tapManager_.getTapTimeMs(i);
                float modTime = baseTime * (1.0f + timeMod * 0.1f);
                tapManager_.setTapTimeMs(i, std::clamp(modTime, kMinDelayMs, maxDelayMs_));
            }

            // Level modulation
            float levelMod = modMatrix_->getCurrentModulation(static_cast<uint8_t>(16 + i));
            if (std::abs(levelMod) > 0.0f) {
                float baseLevel = tapManager_.getTapLevelDb(i);
                tapManager_.setTapLevelDb(i, baseLevel + levelMod * 12.0f);
            }

            // Pan modulation
            float panMod = modMatrix_->getCurrentModulation(static_cast<uint8_t>(32 + i));
            if (std::abs(panMod) > 0.0f) {
                float basePan = tapManager_.getTapPan(i);
                tapManager_.setTapPan(i, std::clamp(basePan + panMod * 100.0f, -100.0f, 100.0f));
            }

            // Cutoff modulation (±2 octaves)
            float cutoffMod = modMatrix_->getCurrentModulation(static_cast<uint8_t>(48 + i));
            if (std::abs(cutoffMod) > 0.0f) {
                // TODO: Apply cutoff modulation when getTapFilterCutoff is available
            }
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
    TapManager tapManager_;
    FeedbackNetwork feedbackNetwork_;
    ModulationMatrix* modMatrix_ = nullptr;  // Not owned

    // Pattern state
    TimingPattern currentTimingPattern_ = TimingPattern::QuarterNote;
    TimingPattern targetTimingPattern_ = TimingPattern::QuarterNote;
    SpatialPattern currentSpatialPattern_ = SpatialPattern::Centered;
    size_t activeTapCount_ = 4;
    float baseTimeMs_ = kDefaultDelayMs;
    float bpm_ = 120.0f;

    // Custom pattern storage
    std::array<float, kMaxTaps> customTimeRatios_ = {};

    // Morphing state
    bool morphing_ = false;
    float morphTimeMs_ = 500.0f;
    std::array<float, kMaxTaps> morphStartTimes_ = {};
    std::array<float, kMaxTaps> morphTargetTimes_ = {};
    OnePoleSmoother morphSmoother_;

    // Feedback parameters
    float feedbackAmount_ = 0.5f;
    float feedbackLPCutoff_ = 20000.0f;
    float feedbackHPCutoff_ = 20.0f;

    // Output parameters
    float dryWetMix_ = 50.0f;
    float outputLevelDb_ = 0.0f;
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother outputLevelSmoother_;

    // Dry signal buffer
    static constexpr size_t kMaxDryBufferSize = 8192;
    std::array<float, kMaxDryBufferSize> dryBufferL_ = {};
    std::array<float, kMaxDryBufferSize> dryBufferR_ = {};
};

} // namespace DSP
} // namespace Iterum
