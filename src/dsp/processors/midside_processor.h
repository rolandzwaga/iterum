// ==============================================================================
// Layer 2: DSP Processor - Mid/Side Processor
// ==============================================================================
// Stereo Mid/Side encoder, decoder, and manipulator for stereo field control.
//
// Features:
// - M/S encoding: Mid = (L + R) / 2, Side = (L - R) / 2
// - M/S decoding: L = Mid + Side, R = Mid - Side
// - Width control (0-200%) via Side channel scaling
// - Independent Mid and Side gain controls (-96dB to +24dB)
// - Solo modes for monitoring Mid or Side independently
// - Click-free parameter transitions using OnePoleSmoother
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle XII: Test-First Development
//
// Reference: specs/014-midside-processor/spec.md
// ==============================================================================

#pragma once

#include "dsp/primitives/smoother.h"
#include "dsp/core/db_utils.h"

#include <algorithm>
#include <cstddef>

namespace Iterum {
namespace DSP {

// =============================================================================
// MidSideProcessor Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Stereo Mid/Side encoder, decoder, and manipulator
///
/// Provides:
/// - M/S encoding: Mid = (L + R) / 2, Side = (L - R) / 2
/// - M/S decoding: L = Mid + Side, R = Mid - Side
/// - Width control (0-200%) via Side channel scaling
/// - Independent Mid and Side gain controls
/// - Solo modes for monitoring Mid or Side independently
///
/// All parameter changes are smoothed to prevent clicks.
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
///
/// @par Usage
/// @code
/// MidSideProcessor ms;
/// ms.prepare(44100.0f, 512);
/// ms.setWidth(150.0f);  // 150% width
/// ms.process(leftIn, rightIn, leftOut, rightOut, numSamples);
/// @endcode
class MidSideProcessor {
public:
    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------

    static constexpr float kMinWidth = 0.0f;      ///< Minimum width (mono)
    static constexpr float kMaxWidth = 200.0f;    ///< Maximum width (enhanced stereo)
    static constexpr float kDefaultWidth = 100.0f; ///< Unity width (bypass)

    static constexpr float kMinGainDb = -96.0f;   ///< Minimum gain in dB
    static constexpr float kMaxGainDb = 24.0f;    ///< Maximum gain in dB
    static constexpr float kDefaultGainDb = 0.0f; ///< Unity gain

    static constexpr float kDefaultSmoothingMs = 10.0f; ///< Default smoothing time

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    MidSideProcessor() noexcept = default;
    ~MidSideProcessor() noexcept = default;

    // Copyable (stateless except for smoothers which are trivially copyable)
    MidSideProcessor(const MidSideProcessor&) = default;
    MidSideProcessor& operator=(const MidSideProcessor&) = default;
    MidSideProcessor(MidSideProcessor&&) noexcept = default;
    MidSideProcessor& operator=(MidSideProcessor&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle Methods
    // -------------------------------------------------------------------------

    /// @brief Prepare processor for given sample rate
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxBlockSize Maximum samples per process() call (unused, for API consistency)
    /// @post Smoothers are initialized. Ready for process().
    void prepare(float sampleRate, size_t maxBlockSize) noexcept {
        (void)maxBlockSize;  // Unused but kept for API consistency
        sampleRate_ = sampleRate;

        // Configure parameter smoothers
        widthSmoother_.configure(kDefaultSmoothingMs, sampleRate);
        midGainSmoother_.configure(kDefaultSmoothingMs, sampleRate);
        sideGainSmoother_.configure(kDefaultSmoothingMs, sampleRate);
        soloMidSmoother_.configure(kDefaultSmoothingMs, sampleRate);
        soloSideSmoother_.configure(kDefaultSmoothingMs, sampleRate);

        // Initialize smoothers to current target values
        reset();
    }

    /// @brief Reset smoothers to snap to current target values
    /// @post No interpolation occurs on next process() call
    /// @note Call after sample rate change or transport reset
    void reset() noexcept {
        // Convert width from percent to factor (0-200% -> 0.0-2.0)
        widthSmoother_.snapTo(width_ / 100.0f);
        midGainSmoother_.snapTo(dbToGain(midGainDb_));
        sideGainSmoother_.snapTo(dbToGain(sideGainDb_));
        soloMidSmoother_.snapTo(soloMid_ ? 1.0f : 0.0f);
        soloSideSmoother_.snapTo(soloSide_ ? 1.0f : 0.0f);
    }

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// @brief Set stereo width
    /// @param widthPercent Width in percent [0%, 200%]
    ///        - 0% = mono (Side removed)
    ///        - 100% = unity (original stereo image)
    ///        - 200% = maximum width (Side doubled)
    /// @note Values outside range are clamped
    void setWidth(float widthPercent) noexcept {
        width_ = std::clamp(widthPercent, kMinWidth, kMaxWidth);
        widthSmoother_.setTarget(width_ / 100.0f);  // Convert to factor
    }

    /// @brief Set mid channel gain
    /// @param gainDb Gain in decibels [-96dB, +24dB]
    /// @note Values outside range are clamped
    void setMidGain(float gainDb) noexcept {
        midGainDb_ = std::clamp(gainDb, kMinGainDb, kMaxGainDb);
        midGainSmoother_.setTarget(dbToGain(midGainDb_));
    }

    /// @brief Set side channel gain
    /// @param gainDb Gain in decibels [-96dB, +24dB]
    /// @note Values outside range are clamped
    void setSideGain(float gainDb) noexcept {
        sideGainDb_ = std::clamp(gainDb, kMinGainDb, kMaxGainDb);
        sideGainSmoother_.setTarget(dbToGain(sideGainDb_));
    }

    /// @brief Enable/disable mid channel solo
    /// @param enabled true = output Mid only, false = normal operation
    /// @note If both soloMid and soloSide are enabled, soloMid takes precedence
    void setSoloMid(bool enabled) noexcept {
        soloMid_ = enabled;
        soloMidSmoother_.setTarget(enabled ? 1.0f : 0.0f);
    }

    /// @brief Enable/disable side channel solo
    /// @param enabled true = output Side only, false = normal operation
    /// @note If both soloMid and soloSide are enabled, soloMid takes precedence
    void setSoloSide(bool enabled) noexcept {
        soloSide_ = enabled;
        soloSideSmoother_.setTarget(enabled ? 1.0f : 0.0f);
    }

    // -------------------------------------------------------------------------
    // Processing
    // -------------------------------------------------------------------------

    /// @brief Process stereo audio through M/S matrix
    /// @param leftIn Input left channel (numSamples floats)
    /// @param rightIn Input right channel (numSamples floats)
    /// @param leftOut Output left channel (numSamples floats)
    /// @param rightOut Output right channel (numSamples floats)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note In-place processing supported (leftIn == leftOut, etc.)
    /// @note Mono input (leftIn == rightIn content) produces mono output at width < 200%
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed parameter values (advances smoother state)
            const float width = widthSmoother_.process();
            const float midGain = midGainSmoother_.process();
            const float sideGain = sideGainSmoother_.process();
            const float soloMidAmount = soloMidSmoother_.process();
            const float soloSideAmount = soloSideSmoother_.process();

            // Read input samples (before potentially overwriting in-place)
            const float L = leftIn[i];
            const float R = rightIn[i];

            // FR-001: Encode to Mid/Side
            // Mid = (L + R) / 2, Side = (L - R) / 2
            float mid = (L + R) * 0.5f;
            float side = (L - R) * 0.5f;

            // Apply gains
            mid *= midGain;
            side *= sideGain;

            // Apply width scaling to Side channel
            // FR-005: effectiveSide = Side * (width / 100%)
            // width is already in factor form (0.0-2.0)
            side *= width;

            // Handle solo modes with smooth crossfade (not hard threshold)
            // FR-017: When both solos enabled, soloMid takes precedence
            // FR-018: Solo mode changes MUST be smoothed to prevent clicks
            // Use crossfade: at soloMidAmount=0, full mix; at soloMidAmount=1, side=0
            const float soloMidFade = soloMidAmount;  // 0 = normal, 1 = mid only
            const float soloSideFade = soloSideAmount * (1.0f - soloMidFade);  // Precedence to mid

            // Crossfade: reduce side for soloMid, reduce mid for soloSide
            side *= (1.0f - soloMidFade);
            mid *= (1.0f - soloSideFade);

            // FR-002: Decode to Left/Right
            // L = Mid + Side, R = Mid - Side
            leftOut[i] = mid + side;
            rightOut[i] = mid - side;
        }
    }

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    /// @brief Get current width setting
    /// @return Width in percent [0%, 200%]
    [[nodiscard]] float getWidth() const noexcept { return width_; }

    /// @brief Get current mid gain setting
    /// @return Mid gain in dB [-96dB, +24dB]
    [[nodiscard]] float getMidGain() const noexcept { return midGainDb_; }

    /// @brief Get current side gain setting
    /// @return Side gain in dB [-96dB, +24dB]
    [[nodiscard]] float getSideGain() const noexcept { return sideGainDb_; }

    /// @brief Check if mid solo is enabled
    /// @return true if mid solo active
    [[nodiscard]] bool isSoloMidEnabled() const noexcept { return soloMid_; }

    /// @brief Check if side solo is enabled
    /// @return true if side solo active
    [[nodiscard]] bool isSoloSideEnabled() const noexcept { return soloSide_; }

private:
    // -------------------------------------------------------------------------
    // Member Variables
    // -------------------------------------------------------------------------

    // Sample rate
    float sampleRate_ = 44100.0f;

    // Target parameter values
    float width_ = kDefaultWidth;         ///< Width in percent [0%, 200%]
    float midGainDb_ = kDefaultGainDb;    ///< Mid gain in dB
    float sideGainDb_ = kDefaultGainDb;   ///< Side gain in dB
    bool soloMid_ = false;                ///< Solo mid flag
    bool soloSide_ = false;               ///< Solo side flag

    // Parameter smoothers (5 total - matches plan.md)
    OnePoleSmoother widthSmoother_;       ///< Smoothed width factor [0.0-2.0]
    OnePoleSmoother midGainSmoother_;     ///< Smoothed mid gain (linear)
    OnePoleSmoother sideGainSmoother_;    ///< Smoothed side gain (linear)
    OnePoleSmoother soloMidSmoother_;     ///< Smoothed solo mid [0.0-1.0]
    OnePoleSmoother soloSideSmoother_;    ///< Smoothed solo side [0.0-1.0]
};

} // namespace DSP
} // namespace Iterum

