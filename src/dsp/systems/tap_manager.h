// ==============================================================================
// Layer 3: System Component - TapManager
// ==============================================================================
// Multi-tap delay manager with up to 16 independent delay taps.
//
// Provides:
// - Up to 16 independent delay taps (fixed array, indices 0-15)
// - Per-tap controls: time, level, pan, filter, feedback
// - Preset patterns: Quarter, Dotted Eighth, Triplet, Golden Ratio, Fibonacci
// - Tempo sync support via NoteValue
// - Click-free parameter changes (20ms smoothing)
//
// Feature: 023-tap-manager
// Layer: 3 (System Component)
// Dependencies: Layer 0 (db_utils, math_constants, note_value),
//               Layer 1 (DelayLine, Biquad, OnePoleSmoother)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (enum class, nodiscard, noexcept, C++20)
// - Principle IX: Layered Architecture (Layer 3)
// - Principle X: DSP Processing Constraints (parameter smoothing)
// - Principle XII: Test-First Development
//
// Reference: specs/023-tap-manager/spec.md
// ==============================================================================

#pragma once

#include "dsp/core/db_utils.h"
#include "dsp/core/math_constants.h"
#include "dsp/core/note_value.h"
#include "dsp/primitives/biquad.h"
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/smoother.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// Enumerations (FR-022 to FR-026)
// =============================================================================

/// @brief Preset tap timing patterns
/// @details Used by loadPattern() to configure tap times automatically
enum class TapPattern : uint8_t {
    Custom,         ///< User-defined times (no pattern)
    QuarterNote,    ///< Taps at 1×, 2×, 3×... quarter note (FR-022)
    DottedEighth,   ///< Taps at 1×, 2×, 3×... dotted eighth (0.75 × quarter) (FR-023)
    Triplet,        ///< Taps at 1×, 2×, 3×... triplet quarter (0.667 × quarter) (FR-024)
    GoldenRatio,    ///< Each tap = previous × 1.618 (φ) (FR-025)
    Fibonacci       ///< Fibonacci sequence: 1, 1, 2, 3, 5, 8... (FR-026)
};

/// @brief How a tap's delay time is specified (FR-007, FR-008)
enum class TapTimeMode : uint8_t {
    FreeRunning,    ///< Time in milliseconds (absolute) (FR-007)
    TempoSynced     ///< Time as note value (relative to BPM) (FR-008)
};

/// @brief Filter type for a tap (FR-015)
enum class TapFilterMode : uint8_t {
    Bypass,     ///< No filtering
    Lowpass,    ///< Low-pass filter (12dB/oct)
    Highpass    ///< High-pass filter (12dB/oct)
};

// =============================================================================
// Constants
// =============================================================================

/// @brief Maximum number of taps (FR-001)
static constexpr size_t kMaxTaps = 16;

/// @brief Default parameter smoothing time in ms (SC-002)
static constexpr float kTapSmoothingMs = 20.0f;

/// @brief Minimum level in dB (FR-009, FR-010: silence floor)
static constexpr float kMinLevelDb = -96.0f;

/// @brief Maximum level in dB (FR-009)
static constexpr float kMaxLevelDb = 6.0f;

/// @brief Minimum filter cutoff in Hz (FR-016)
static constexpr float kMinFilterCutoff = 20.0f;

/// @brief Maximum filter cutoff in Hz (FR-016)
static constexpr float kMaxFilterCutoff = 20000.0f;

/// @brief Minimum filter Q factor (FR-017)
static constexpr float kMinFilterQ = 0.5f;

/// @brief Maximum filter Q factor (FR-017)
static constexpr float kMaxFilterQ = 10.0f;

/// @brief Default filter cutoff in Hz
static constexpr float kDefaultFilterCutoff = 1000.0f;

/// @brief Default filter Q factor (Butterworth)
static constexpr float kDefaultFilterQ = 0.707f;

/// @brief Default tempo in BPM
static constexpr float kDefaultTempo = 120.0f;

/// @brief Dotted eighth multiplier (0.75 × quarter)
static constexpr float kDottedEighthMultiplier = 0.75f;

/// @brief Triplet multiplier (~0.667 × quarter)
static constexpr float kTripletMultiplier = 2.0f / 3.0f;

// =============================================================================
// Tap Structure (Internal)
// =============================================================================

/// @brief Internal representation of a single delay tap
/// @note This is an implementation detail, not part of public API
struct Tap {
    // Configuration
    bool enabled = false;
    TapTimeMode timeMode = TapTimeMode::FreeRunning;
    float timeMs = 0.0f;
    NoteValue noteValue = NoteValue::Quarter;
    float levelDb = 0.0f;
    float pan = 0.0f;               // -100 to +100 (L to R)
    TapFilterMode filterMode = TapFilterMode::Bypass;
    float filterCutoff = kDefaultFilterCutoff;
    float filterQ = kDefaultFilterQ;
    float feedbackAmount = 0.0f;    // 0 to 100 (%)

    // Smoothers (20ms transition time)
    OnePoleSmoother delaySmoother{};
    OnePoleSmoother levelSmoother{};
    OnePoleSmoother panSmoother{};
    OnePoleSmoother cutoffSmoother{};

    // Filter (configured via configure() method)
    Biquad filter{};
    float cachedSampleRate = 44100.0f;

    // Computed state
    float currentGain = 0.0f;       // Linear gain from levelDb
    float currentPanL = 0.707f;     // Left pan coefficient
    float currentPanR = 0.707f;     // Right pan coefficient
};

// =============================================================================
// TapManager Class (FR-001 to FR-033)
// =============================================================================

/// @brief Layer 3 System Component - Multi-tap delay manager
///
/// Manages up to 16 independent delay taps with per-tap controls for time,
/// level, pan, filter, and feedback. Supports preset patterns and tempo sync.
///
/// @par Real-Time Safety (Principle II)
/// All processing methods are noexcept and allocation-free after prepare().
/// Memory is allocated only in prepare().
///
/// @par Usage
/// @code
/// TapManager taps;
/// taps.prepare(44100.0f, 512, 5000.0f);
/// taps.setTapEnabled(0, true);
/// taps.setTapTimeMs(0, 250.0f);
/// taps.setTapLevelDb(0, 0.0f);
/// taps.process(leftIn, rightIn, leftOut, rightOut, numSamples);
/// @endcode
///
/// @see specs/023-tap-manager/spec.md
class TapManager {
public:
    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    TapManager() noexcept = default;
    ~TapManager() noexcept = default;

    // Non-copyable (owns large delay buffer)
    TapManager(const TapManager&) = delete;
    TapManager& operator=(const TapManager&) = delete;
    TapManager(TapManager&&) noexcept = default;
    TapManager& operator=(TapManager&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-031, FR-032)
    // =========================================================================

    /// @brief Prepare for processing (allocates memory)
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxDelayMs Maximum delay time in milliseconds
    /// @post All taps are initialized and disabled. Ready for process().
    /// @note This is the ONLY method that allocates memory.
    void prepare(float sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;

    /// @brief Reset all taps to initial state
    /// @post All smoothers snap to current values. Delay line cleared.
    void reset() noexcept;

    // =========================================================================
    // Tap Configuration (FR-002 to FR-019)
    // =========================================================================

    /// @brief Enable or disable a tap (FR-002, FR-003, FR-004)
    /// @param tapIndex Tap index [0, 15]
    /// @param enabled true = tap produces output
    /// @note Transitions are smooth (no clicks). Out-of-range indices ignored (FR-004a).
    void setTapEnabled(size_t tapIndex, bool enabled) noexcept;

    /// @brief Set tap delay time in milliseconds (FR-005, FR-007)
    /// @param tapIndex Tap index [0, 15]
    /// @param timeMs Delay time in ms [0, maxDelayMs]
    /// @note Sets time mode to FreeRunning. Out-of-range indices ignored.
    void setTapTimeMs(size_t tapIndex, float timeMs) noexcept;

    /// @brief Set tap delay time as note value (tempo-synced) (FR-008)
    /// @param tapIndex Tap index [0, 15]
    /// @param noteValue Note value for tempo sync
    /// @note Sets time mode to TempoSynced. Out-of-range indices ignored.
    void setTapNoteValue(size_t tapIndex, NoteValue noteValue) noexcept;

    /// @brief Set tap output level (FR-009, FR-010)
    /// @param tapIndex Tap index [0, 15]
    /// @param levelDb Level in dB [-96, +6]. At or below -96dB produces silence.
    void setTapLevelDb(size_t tapIndex, float levelDb) noexcept;

    /// @brief Set tap pan position (FR-012, FR-013)
    /// @param tapIndex Tap index [0, 15]
    /// @param pan Pan position [-100, +100] (L to R). Uses constant-power pan law.
    void setTapPan(size_t tapIndex, float pan) noexcept;

    /// @brief Set tap filter mode (FR-015)
    /// @param tapIndex Tap index [0, 15]
    /// @param mode Filter type (Bypass, Lowpass, Highpass)
    void setTapFilterMode(size_t tapIndex, TapFilterMode mode) noexcept;

    /// @brief Set tap filter cutoff frequency (FR-016)
    /// @param tapIndex Tap index [0, 15]
    /// @param cutoffHz Cutoff in Hz [20, 20000]
    void setTapFilterCutoff(size_t tapIndex, float cutoffHz) noexcept;

    /// @brief Set tap filter resonance (FR-017)
    /// @param tapIndex Tap index [0, 15]
    /// @param q Q factor [0.5, 10.0]
    void setTapFilterQ(size_t tapIndex, float q) noexcept;

    /// @brief Set tap feedback amount to master (FR-019, FR-020)
    /// @param tapIndex Tap index [0, 15]
    /// @param amount Feedback percentage [0, 100]
    void setTapFeedback(size_t tapIndex, float amount) noexcept;

    // =========================================================================
    // Pattern Configuration (FR-022 to FR-027)
    // =========================================================================

    /// @brief Load a preset pattern (FR-022 to FR-027)
    /// @param pattern Pattern type
    /// @param tapCount Number of taps to create [1, 16]
    /// @note All existing taps are disabled first. Pattern is applied based on
    ///       current tempo. Completes within 1ms (SC-008).
    void loadPattern(TapPattern pattern, size_t tapCount) noexcept;

    /// @brief Load a note-based pattern (extended preset patterns)
    /// @param noteValue Base note value (SixtyFourth to DoubleWhole)
    /// @param modifier Note modifier (None, Dotted, Triplet)
    /// @param tapCount Number of taps to create [1, 16]
    /// @note Creates evenly-spaced taps at multiples of the note duration.
    ///       All existing taps are disabled first. Pattern is applied based on
    ///       current tempo. Completes within 1ms.
    void loadNotePattern(NoteValue noteValue, NoteModifier modifier, size_t tapCount) noexcept;

    /// @brief Set tempo for tempo-synced taps (US6)
    /// @param bpm Beats per minute (must be > 0)
    /// @note Updates delay times for TempoSynced taps within 1 audio block (SC-006).
    void setTempo(float bpm) noexcept;

    // =========================================================================
    // Master Configuration (FR-028 to FR-030)
    // =========================================================================

    /// @brief Set master output level (FR-029)
    /// @param levelDb Level in dB [-96, +6]
    void setMasterLevel(float levelDb) noexcept;

    /// @brief Set dry/wet mix (FR-030)
    /// @param mix Mix percentage [0, 100] (0 = dry, 100 = wet)
    void setDryWetMix(float mix) noexcept;

    // =========================================================================
    // Processing (FR-028, FR-031, FR-032, FR-033)
    // =========================================================================

    /// @brief Process stereo audio
    /// @param leftIn Input left channel (numSamples floats)
    /// @param rightIn Input right channel (numSamples floats)
    /// @param leftOut Output left channel (numSamples floats)
    /// @param rightOut Output right channel (numSamples floats)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @post Output contains mixed dry + wet signal based on dryWetMix
    /// @note In-place processing supported (leftIn == leftOut).
    ///       All 16 taps can be active without dropouts (SC-001).
    ///       CPU < 2% for 16 active taps at 44.1kHz stereo (SC-007).
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept;

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Check if a tap is enabled
    /// @param tapIndex Tap index [0, 15]
    /// @return true if enabled, false if disabled or out-of-range
    [[nodiscard]] bool isTapEnabled(size_t tapIndex) const noexcept;

    /// @brief Get current pattern
    [[nodiscard]] TapPattern getPattern() const noexcept;

    /// @brief Get number of active (enabled) taps
    [[nodiscard]] size_t getActiveTapCount() const noexcept;

    /// @brief Get tap delay time in milliseconds
    /// @param tapIndex Tap index [0, 15]
    /// @return Current delay time, or 0.0f if out-of-range
    [[nodiscard]] float getTapTimeMs(size_t tapIndex) const noexcept;

    /// @brief Get tap level in dB
    /// @param tapIndex Tap index [0, 15]
    /// @return Current level, or kMinLevelDb if out-of-range
    [[nodiscard]] float getTapLevelDb(size_t tapIndex) const noexcept;

    /// @brief Get tap pan position
    /// @param tapIndex Tap index [0, 15]
    /// @return Current pan position, or 0.0f if out-of-range
    [[nodiscard]] float getTapPan(size_t tapIndex) const noexcept;

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Calculate delay time in samples from milliseconds
    [[nodiscard]] float msToSamples(float ms) const noexcept {
        return ms * sampleRate_ * 0.001f;
    }

    /// @brief Calculate tempo-synced delay time in milliseconds
    [[nodiscard]] float calcTempoSyncMs(NoteValue note) const noexcept {
        const float quarterNoteMs = 60000.0f / bpm_;
        // getBeatsForNote returns beats relative to quarter note (which = 1.0 beat)
        return quarterNoteMs * getBeatsForNote(note);
    }

    /// @brief Update filter coefficients for a tap
    void updateTapFilter(size_t tapIndex) noexcept;

    /// @brief Calculate constant-power pan coefficients
    void calcPanCoefficients(float pan, float& outL, float& outR) const noexcept;

    /// @brief Apply soft limiter to prevent feedback runaway (FR-021)
    [[nodiscard]] static float softLimit(float sample) noexcept {
        // Simple tanh-based soft clipper
        return std::tanh(sample);
    }

    /// @brief Generate Fibonacci number (for pattern generation)
    /// Uses 1-based indexing: fib(1)=1, fib(2)=1, fib(3)=2, fib(4)=3, fib(5)=5...
    [[nodiscard]] static size_t fibonacci(size_t n) noexcept {
        if (n <= 2) return 1;  // fib(1) = 1, fib(2) = 1
        size_t prev2 = 1;  // fib(n-2)
        size_t prev1 = 1;  // fib(n-1)
        for (size_t i = 3; i <= n; ++i) {
            const size_t current = prev1 + prev2;
            prev2 = prev1;
            prev1 = current;
        }
        return prev1;
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    float sampleRate_ = 44100.0f;
    float maxDelayMs_ = 5000.0f;
    float bpm_ = kDefaultTempo;
    TapPattern pattern_ = TapPattern::Custom;
    float masterLevelDb_ = 0.0f;
    float dryWetMix_ = 100.0f;      // 0-100%

    // State
    std::array<Tap, kMaxTaps> taps_{};
    DelayLine delayLine_;           // Shared delay buffer
    OnePoleSmoother masterLevelSmoother_;
    OnePoleSmoother dryWetSmoother_;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void TapManager::prepare(float sampleRate, size_t /*maxBlockSize*/,
                                 float maxDelayMs) noexcept {
    sampleRate_ = sampleRate;
    maxDelayMs_ = maxDelayMs;

    // Allocate delay line for maximum delay time
    const float maxDelaySeconds = maxDelayMs * 0.001f;
    delayLine_.prepare(static_cast<double>(sampleRate), maxDelaySeconds);

    // Initialize all 16 taps
    for (auto& tap : taps_) {
        tap.enabled = false;
        tap.timeMode = TapTimeMode::FreeRunning;
        tap.timeMs = 0.0f;
        tap.noteValue = NoteValue::Quarter;
        tap.levelDb = 0.0f;
        tap.pan = 0.0f;
        tap.filterMode = TapFilterMode::Bypass;
        tap.filterCutoff = kDefaultFilterCutoff;
        tap.filterQ = kDefaultFilterQ;
        tap.feedbackAmount = 0.0f;

        // Initialize smoothers (20ms smoothing time)
        tap.delaySmoother.configure(kTapSmoothingMs, sampleRate);
        tap.levelSmoother.configure(kTapSmoothingMs, sampleRate);
        tap.panSmoother.configure(kTapSmoothingMs, sampleRate);
        tap.cutoffSmoother.configure(kTapSmoothingMs, sampleRate);

        // Store sample rate for filter configuration
        tap.cachedSampleRate = sampleRate;

        // Initial computed state
        tap.currentGain = 0.0f;
        tap.currentPanL = 0.707f;
        tap.currentPanR = 0.707f;
    }

    // Master smoothers
    masterLevelSmoother_.configure(kTapSmoothingMs, sampleRate);
    dryWetSmoother_.configure(kTapSmoothingMs, sampleRate);
    dryWetSmoother_.snapTo(dryWetMix_ * 0.01f);

    pattern_ = TapPattern::Custom;
}

inline void TapManager::reset() noexcept {
    delayLine_.reset();

    for (auto& tap : taps_) {
        // Use setTarget + snapToTarget for smoother reset
        tap.delaySmoother.setTarget(msToSamples(tap.timeMs));
        tap.delaySmoother.snapToTarget();
        tap.levelSmoother.setTarget(tap.enabled ? dbToGain(tap.levelDb) : 0.0f);
        tap.levelSmoother.snapToTarget();
        tap.panSmoother.setTarget(tap.pan);
        tap.panSmoother.snapToTarget();
        tap.cutoffSmoother.setTarget(tap.filterCutoff);
        tap.cutoffSmoother.snapToTarget();
        tap.filter.reset();
    }

    masterLevelSmoother_.setTarget(dbToGain(masterLevelDb_));
    masterLevelSmoother_.snapToTarget();
    dryWetSmoother_.setTarget(dryWetMix_ * 0.01f);
    dryWetSmoother_.snapToTarget();
}

inline void TapManager::setTapEnabled(size_t tapIndex, bool enabled) noexcept {
    if (tapIndex >= kMaxTaps) return;  // FR-004a: silently ignore out-of-range
    taps_[tapIndex].enabled = enabled;
    // Level smoother will handle fade in/out
}

inline void TapManager::setTapTimeMs(size_t tapIndex, float timeMs) noexcept {
    if (tapIndex >= kMaxTaps) return;
    taps_[tapIndex].timeMode = TapTimeMode::FreeRunning;
    taps_[tapIndex].timeMs = std::clamp(timeMs, 0.0f, maxDelayMs_);
}

inline void TapManager::setTapNoteValue(size_t tapIndex, NoteValue noteValue) noexcept {
    if (tapIndex >= kMaxTaps) return;
    taps_[tapIndex].timeMode = TapTimeMode::TempoSynced;
    taps_[tapIndex].noteValue = noteValue;
    // Delay time will be calculated from tempo in process()
}

inline void TapManager::setTapLevelDb(size_t tapIndex, float levelDb) noexcept {
    if (tapIndex >= kMaxTaps) return;
    taps_[tapIndex].levelDb = std::clamp(levelDb, kMinLevelDb, kMaxLevelDb);
}

inline void TapManager::setTapPan(size_t tapIndex, float pan) noexcept {
    if (tapIndex >= kMaxTaps) return;
    taps_[tapIndex].pan = std::clamp(pan, -100.0f, 100.0f);
}

inline void TapManager::setTapFilterMode(size_t tapIndex, TapFilterMode mode) noexcept {
    if (tapIndex >= kMaxTaps) return;
    taps_[tapIndex].filterMode = mode;
    updateTapFilter(tapIndex);
}

inline void TapManager::setTapFilterCutoff(size_t tapIndex, float cutoffHz) noexcept {
    if (tapIndex >= kMaxTaps) return;
    taps_[tapIndex].filterCutoff = std::clamp(cutoffHz, kMinFilterCutoff, kMaxFilterCutoff);
    updateTapFilter(tapIndex);
}

inline void TapManager::setTapFilterQ(size_t tapIndex, float q) noexcept {
    if (tapIndex >= kMaxTaps) return;
    taps_[tapIndex].filterQ = std::clamp(q, kMinFilterQ, kMaxFilterQ);
    updateTapFilter(tapIndex);
}

inline void TapManager::setTapFeedback(size_t tapIndex, float amount) noexcept {
    if (tapIndex >= kMaxTaps) return;
    taps_[tapIndex].feedbackAmount = std::clamp(amount, 0.0f, 100.0f);
}

inline void TapManager::updateTapFilter(size_t tapIndex) noexcept {
    if (tapIndex >= kMaxTaps) return;

    auto& tap = taps_[tapIndex];
    switch (tap.filterMode) {
        case TapFilterMode::Lowpass:
            tap.filter.configure(FilterType::Lowpass, tap.filterCutoff, tap.filterQ,
                                 0.0f, tap.cachedSampleRate);
            break;
        case TapFilterMode::Highpass:
            tap.filter.configure(FilterType::Highpass, tap.filterCutoff, tap.filterQ,
                                 0.0f, tap.cachedSampleRate);
            break;
        case TapFilterMode::Bypass:
        default:
            // Filter bypassed - coefficients don't matter
            break;
    }
}

inline void TapManager::loadPattern(TapPattern pattern, size_t tapCount) noexcept {
    // Clamp tap count (FR-027)
    tapCount = std::clamp(tapCount, size_t{1}, kMaxTaps);

    // Disable all taps first
    for (auto& tap : taps_) {
        tap.enabled = false;
    }

    const float quarterNoteMs = 60000.0f / bpm_;
    pattern_ = pattern;

    // Calculate tap times based on pattern
    // Note: Pattern formulas use 1-based indexing (n = i + 1)
    for (size_t i = 0; i < tapCount; ++i) {
        const size_t n = i + 1;  // 1-based tap number
        float timeMs = 0.0f;

        switch (pattern) {
            case TapPattern::QuarterNote:
                // tap[n] = n × quarterNoteMs (500, 1000, 1500, 2000ms at 120 BPM)
                timeMs = static_cast<float>(n) * quarterNoteMs;
                break;

            case TapPattern::DottedEighth:
                // tap[n] = n × (quarterNoteMs × 0.75) (375, 750, 1125, 1500ms)
                timeMs = static_cast<float>(n) * quarterNoteMs * kDottedEighthMultiplier;
                break;

            case TapPattern::Triplet:
                // tap[n] = n × (quarterNoteMs × 0.667) (333, 667, 1000, 1333ms)
                timeMs = static_cast<float>(n) * quarterNoteMs * kTripletMultiplier;
                break;

            case TapPattern::GoldenRatio:
                // tap[1] = quarterNoteMs, tap[n] = tap[n-1] × 1.618
                if (i == 0) {
                    timeMs = quarterNoteMs;
                } else {
                    timeMs = taps_[i - 1].timeMs * kGoldenRatio;
                }
                break;

            case TapPattern::Fibonacci:
                // tap[n] = fib(n) × baseMs, where fib = 1, 1, 2, 3, 5, 8...
                timeMs = static_cast<float>(fibonacci(n)) * (quarterNoteMs * 0.25f);
                break;

            case TapPattern::Custom:
            default:
                // Custom pattern keeps existing times
                timeMs = taps_[i].timeMs;
                break;
        }

        // Clamp to max delay and enable tap
        taps_[i].timeMs = std::min(timeMs, maxDelayMs_);
        taps_[i].timeMode = TapTimeMode::FreeRunning;
        taps_[i].enabled = true;

        // Set default level with progressive decay
        taps_[i].levelDb = -3.0f * static_cast<float>(i);  // -0, -3, -6, -9...
    }
}

inline void TapManager::loadNotePattern(NoteValue noteValue, NoteModifier modifier,
                                         size_t tapCount) noexcept {
    // Clamp tap count
    tapCount = std::clamp(tapCount, size_t{1}, kMaxTaps);

    // Disable all taps first
    for (auto& tap : taps_) {
        tap.enabled = false;
    }

    // Calculate base note duration in ms
    // quarterNoteMs = 60000 / BPM
    // noteMs = quarterNoteMs * beatsForNote
    const float quarterNoteMs = 60000.0f / bpm_;
    const float beats = getBeatsForNote(noteValue, modifier);
    const float baseNoteMs = quarterNoteMs * beats;

    // Set pattern to Custom (this is a note-based pattern, not a TapPattern enum value)
    pattern_ = TapPattern::Custom;

    // Configure taps at multiples of the note duration
    for (size_t i = 0; i < tapCount; ++i) {
        const size_t n = i + 1;  // 1-based tap number
        const float timeMs = static_cast<float>(n) * baseNoteMs;

        // Clamp to max delay and enable tap
        taps_[i].timeMs = std::min(timeMs, maxDelayMs_);
        taps_[i].timeMode = TapTimeMode::FreeRunning;
        taps_[i].enabled = true;

        // Set default level with progressive decay
        taps_[i].levelDb = -3.0f * static_cast<float>(i);  // -0, -3, -6, -9...
    }
}

inline void TapManager::setTempo(float bpm) noexcept {
    if (bpm <= 0.0f) return;
    bpm_ = bpm;

    // Update delay times for tempo-synced taps (SC-006: within 1 audio block)
    for (auto& tap : taps_) {
        if (tap.timeMode == TapTimeMode::TempoSynced) {
            tap.timeMs = std::min(calcTempoSyncMs(tap.noteValue), maxDelayMs_);
        }
    }
}

inline void TapManager::setMasterLevel(float levelDb) noexcept {
    masterLevelDb_ = std::clamp(levelDb, kMinLevelDb, kMaxLevelDb);
}

inline void TapManager::setDryWetMix(float mix) noexcept {
    dryWetMix_ = std::clamp(mix, 0.0f, 100.0f);
}

inline void TapManager::calcPanCoefficients(float pan, float& outL, float& outR) const noexcept {
    // Constant-power pan law using sine/cosine (FR-013, SC-004)
    // pan: -100 (full left) to +100 (full right)
    // theta: 0 (full left) to pi/2 (full right)
    const float theta = (pan + 100.0f) * 0.005f * kPi * 0.5f;  // 0 to pi/2
    outL = std::cos(theta);
    outR = std::sin(theta);
}

inline void TapManager::process(const float* leftIn, const float* rightIn,
                                 float* leftOut, float* rightOut,
                                 size_t numSamples) noexcept {
    // Set target for master smoothers
    const float targetMasterGain = (masterLevelDb_ <= kMinLevelDb)
                                    ? 0.0f
                                    : dbToGain(masterLevelDb_);
    const float targetWetMix = dryWetMix_ * 0.01f;
    masterLevelSmoother_.setTarget(targetMasterGain);
    dryWetSmoother_.setTarget(targetWetMix);

    for (size_t i = 0; i < numSamples; ++i) {
        // Read input (mono sum for delay line)
        const float inputL = leftIn[i];
        const float inputR = rightIn[i];
        const float inputMono = (inputL + inputR) * 0.5f;

        // Accumulate feedback from all taps
        float feedbackSum = 0.0f;

        // Mix all tap outputs
        float wetL = 0.0f;
        float wetR = 0.0f;

        for (size_t t = 0; t < kMaxTaps; ++t) {
            auto& tap = taps_[t];

            // Calculate effective delay time (samples)
            float delayTimeMs = tap.timeMs;
            if (tap.timeMode == TapTimeMode::TempoSynced) {
                delayTimeMs = calcTempoSyncMs(tap.noteValue);
                delayTimeMs = std::min(delayTimeMs, maxDelayMs_);
            }
            const float targetDelaySamples = msToSamples(delayTimeMs);

            // Smooth delay time (FR-006)
            tap.delaySmoother.setTarget(targetDelaySamples);
            const float delaySamples = tap.delaySmoother.process();

            // Calculate target gain (FR-010: -96dB = silence)
            float targetGain = 0.0f;
            if (tap.enabled && tap.levelDb > kMinLevelDb) {
                targetGain = dbToGain(tap.levelDb);
            }

            // Smooth gain (FR-011)
            tap.levelSmoother.setTarget(targetGain);
            const float gain = tap.levelSmoother.process();

            // Skip processing if gain is negligible
            if (gain < 1e-6f) continue;

            // Read from delay line with interpolation (SC-003: within 1 sample accuracy)
            float sample = delayLine_.readLinear(delaySamples);

            // Apply filter (FR-015 to FR-018)
            if (tap.filterMode != TapFilterMode::Bypass) {
                // Smooth filter cutoff (FR-018)
                tap.cutoffSmoother.setTarget(tap.filterCutoff);
                const float smoothedCutoff = tap.cutoffSmoother.process();

                // Only update if cutoff changed significantly (avoid per-sample coeff calc)
                if (std::abs(smoothedCutoff - tap.cutoffSmoother.getTarget()) > 1.0f) {
                    if (tap.filterMode == TapFilterMode::Lowpass) {
                        tap.filter.configure(FilterType::Lowpass, smoothedCutoff, tap.filterQ,
                                             0.0f, tap.cachedSampleRate);
                    } else {
                        tap.filter.configure(FilterType::Highpass, smoothedCutoff, tap.filterQ,
                                             0.0f, tap.cachedSampleRate);
                    }
                }
                sample = tap.filter.process(sample);
            }

            // Apply gain
            sample *= gain;

            // Calculate pan coefficients (constant-power)
            tap.panSmoother.setTarget(tap.pan);
            const float smoothedPan = tap.panSmoother.process();
            float panL, panR;
            calcPanCoefficients(smoothedPan, panL, panR);

            // Add to stereo output
            wetL += sample * panL;
            wetR += sample * panR;

            // Accumulate feedback (FR-019, FR-020)
            feedbackSum += sample * (tap.feedbackAmount * 0.01f);
        }

        // Limit feedback to prevent runaway (FR-021)
        if (std::abs(feedbackSum) > 1.0f) {
            feedbackSum = softLimit(feedbackSum);
        }

        // Write to delay line (input + feedback)
        delayLine_.write(inputMono + feedbackSum);

        // Apply master level and mix
        const float masterGain = masterLevelSmoother_.process();
        const float wetMix = dryWetSmoother_.process();
        const float dryMix = 1.0f - wetMix;

        wetL *= masterGain;
        wetR *= masterGain;

        // Output dry/wet mix
        leftOut[i] = inputL * dryMix + wetL * wetMix;
        rightOut[i] = inputR * dryMix + wetR * wetMix;
    }
}

// Query implementations
inline bool TapManager::isTapEnabled(size_t tapIndex) const noexcept {
    if (tapIndex >= kMaxTaps) return false;
    return taps_[tapIndex].enabled;
}

inline TapPattern TapManager::getPattern() const noexcept {
    return pattern_;
}

inline size_t TapManager::getActiveTapCount() const noexcept {
    size_t count = 0;
    for (const auto& tap : taps_) {
        if (tap.enabled) ++count;
    }
    return count;
}

inline float TapManager::getTapTimeMs(size_t tapIndex) const noexcept {
    if (tapIndex >= kMaxTaps) return 0.0f;
    return taps_[tapIndex].timeMs;
}

inline float TapManager::getTapLevelDb(size_t tapIndex) const noexcept {
    if (tapIndex >= kMaxTaps) return kMinLevelDb;
    return taps_[tapIndex].levelDb;
}

inline float TapManager::getTapPan(size_t tapIndex) const noexcept {
    if (tapIndex >= kMaxTaps) return 0.0f;
    return taps_[tapIndex].pan;
}

} // namespace DSP
} // namespace Iterum
