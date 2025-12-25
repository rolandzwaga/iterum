// ==============================================================================
// Layer 3: System Component - Tap Manager (API Contract)
// ==============================================================================
// This file defines the public API contract for TapManager.
// Implementation will be in src/dsp/systems/tap_manager.h
//
// Features:
// - Up to 16 independent delay taps
// - Per-tap controls: time, level, pan, filter, feedback
// - Preset patterns: Quarter, Dotted Eighth, Triplet, Golden Ratio, Fibonacci
// - Tempo sync support via NoteValue
// - Click-free parameter changes (20ms smoothing)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 3 (depends only on Layer 0/1)
// - Principle XII: Test-First Development
//
// Reference: specs/023-tap-manager/spec.md
// ==============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Preset tap timing patterns
enum class TapPattern : uint8_t {
    Custom,         ///< User-defined times (no pattern)
    QuarterNote,    ///< Taps at 1×, 2×, 3×... quarter note
    DottedEighth,   ///< Taps at 1×, 2×, 3×... dotted eighth (0.75 × quarter)
    Triplet,        ///< Taps at 1×, 2×, 3×... triplet quarter (0.667 × quarter)
    GoldenRatio,    ///< Each tap = previous × 1.618 (φ)
    Fibonacci       ///< Fibonacci sequence: 1, 1, 2, 3, 5, 8...
};

/// @brief How a tap's delay time is specified
enum class TapTimeMode : uint8_t {
    FreeRunning,    ///< Time in milliseconds (absolute)
    TempoSynced     ///< Time as note value (relative to BPM)
};

/// @brief Filter type for a tap
enum class TapFilterMode : uint8_t {
    Bypass,     ///< No filtering
    Lowpass,    ///< Low-pass filter (12dB/oct)
    Highpass    ///< High-pass filter (12dB/oct)
};

// =============================================================================
// Constants
// =============================================================================

static constexpr size_t kMaxTaps = 16;              ///< Maximum number of taps
static constexpr float kDefaultSmoothingMs = 20.0f; ///< Default parameter smoothing time
static constexpr float kMinLevelDb = -96.0f;        ///< Minimum level (silence)
static constexpr float kMaxLevelDb = 6.0f;          ///< Maximum level (+6dB)
static constexpr float kMinFilterCutoff = 20.0f;    ///< Minimum filter cutoff (Hz)
static constexpr float kMaxFilterCutoff = 20000.0f; ///< Maximum filter cutoff (Hz)
static constexpr float kMinFilterQ = 0.5f;          ///< Minimum filter Q
static constexpr float kMaxFilterQ = 10.0f;         ///< Maximum filter Q

// =============================================================================
// TapManager Class (API Contract)
// =============================================================================

/// @brief Layer 3 System Component - Multi-tap delay manager
///
/// Manages up to 16 independent delay taps with per-tap controls for time,
/// level, pan, filter, and feedback. Supports preset patterns and tempo sync.
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
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
class TapManager {
public:
    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    TapManager() noexcept = default;
    ~TapManager() noexcept = default;

    // Non-copyable (owns large delay buffer)
    TapManager(const TapManager&) = delete;
    TapManager& operator=(const TapManager&) = delete;
    TapManager(TapManager&&) noexcept = default;
    TapManager& operator=(TapManager&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle Methods
    // -------------------------------------------------------------------------

    /// @brief Prepare for processing
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxDelayMs Maximum delay time in milliseconds
    /// @post All taps are initialized and disabled. Ready for process().
    void prepare(float sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;

    /// @brief Reset all taps to initial state
    /// @post All smoothers snap to current values. Delay line cleared.
    void reset() noexcept;

    // -------------------------------------------------------------------------
    // Tap Configuration
    // -------------------------------------------------------------------------

    /// @brief Enable or disable a tap
    /// @param tapIndex Tap index [0, 15]
    /// @param enabled true = tap produces output
    /// @note Transitions are smooth (no clicks)
    void setTapEnabled(size_t tapIndex, bool enabled) noexcept;

    /// @brief Set tap delay time in milliseconds
    /// @param tapIndex Tap index [0, 15]
    /// @param timeMs Delay time in ms [0, maxDelayMs]
    /// @note Sets time mode to FreeRunning
    void setTapTimeMs(size_t tapIndex, float timeMs) noexcept;

    /// @brief Set tap delay time as note value (tempo-synced)
    /// @param tapIndex Tap index [0, 15]
    /// @param noteValue Note value for tempo sync
    /// @note Sets time mode to TempoSynced
    void setTapNoteValue(size_t tapIndex, /* NoteValue */ int noteValue) noexcept;

    /// @brief Set tap output level
    /// @param tapIndex Tap index [0, 15]
    /// @param levelDb Level in dB [-96, +6]
    void setTapLevelDb(size_t tapIndex, float levelDb) noexcept;

    /// @brief Set tap pan position
    /// @param tapIndex Tap index [0, 15]
    /// @param pan Pan position [-100, +100] (L to R)
    void setTapPan(size_t tapIndex, float pan) noexcept;

    /// @brief Set tap filter mode
    /// @param tapIndex Tap index [0, 15]
    /// @param mode Filter type (Bypass, Lowpass, Highpass)
    void setTapFilterMode(size_t tapIndex, TapFilterMode mode) noexcept;

    /// @brief Set tap filter cutoff frequency
    /// @param tapIndex Tap index [0, 15]
    /// @param cutoffHz Cutoff in Hz [20, 20000]
    void setTapFilterCutoff(size_t tapIndex, float cutoffHz) noexcept;

    /// @brief Set tap filter resonance
    /// @param tapIndex Tap index [0, 15]
    /// @param q Q factor [0.5, 10.0]
    void setTapFilterQ(size_t tapIndex, float q) noexcept;

    /// @brief Set tap feedback amount to master
    /// @param tapIndex Tap index [0, 15]
    /// @param amount Feedback percentage [0, 100]
    void setTapFeedback(size_t tapIndex, float amount) noexcept;

    // -------------------------------------------------------------------------
    // Pattern Configuration
    // -------------------------------------------------------------------------

    /// @brief Load a preset pattern
    /// @param pattern Pattern type
    /// @param tapCount Number of taps to create [1, 16]
    /// @note All existing taps are disabled first
    void loadPattern(TapPattern pattern, size_t tapCount) noexcept;

    /// @brief Set tempo for tempo-synced taps
    /// @param bpm Beats per minute (must be > 0)
    void setTempo(float bpm) noexcept;

    // -------------------------------------------------------------------------
    // Master Configuration
    // -------------------------------------------------------------------------

    /// @brief Set master output level
    /// @param levelDb Level in dB [-96, +6]
    void setMasterLevel(float levelDb) noexcept;

    /// @brief Set dry/wet mix
    /// @param mix Mix percentage [0, 100] (0 = dry, 100 = wet)
    void setDryWetMix(float mix) noexcept;

    // -------------------------------------------------------------------------
    // Processing
    // -------------------------------------------------------------------------

    /// @brief Process stereo audio
    /// @param leftIn Input left channel (numSamples floats)
    /// @param rightIn Input right channel (numSamples floats)
    /// @param leftOut Output left channel (numSamples floats)
    /// @param rightOut Output right channel (numSamples floats)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note In-place processing supported (leftIn == leftOut, etc.)
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept;

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    /// @brief Check if a tap is enabled
    [[nodiscard]] bool isTapEnabled(size_t tapIndex) const noexcept;

    /// @brief Get current pattern
    [[nodiscard]] TapPattern getPattern() const noexcept;

    /// @brief Get number of active (enabled) taps
    [[nodiscard]] size_t getActiveTapCount() const noexcept;

    /// @brief Get tap delay time in milliseconds
    [[nodiscard]] float getTapTimeMs(size_t tapIndex) const noexcept;

    /// @brief Get tap level in dB
    [[nodiscard]] float getTapLevelDb(size_t tapIndex) const noexcept;

    /// @brief Get tap pan position
    [[nodiscard]] float getTapPan(size_t tapIndex) const noexcept;
};

} // namespace DSP
} // namespace Iterum
