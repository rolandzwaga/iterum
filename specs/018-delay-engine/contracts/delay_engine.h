// ==============================================================================
// API Contract: DelayEngine (Layer 3)
// ==============================================================================
// This file defines the public interface contract for DelayEngine.
// Implementation must match this interface exactly.
//
// Feature: 018-delay-engine
// Layer: 3 (System Component)
// Dependencies: Layer 0 (BlockContext, NoteValue), Layer 1 (DelayLine, OnePoleSmoother)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (enum class, nodiscard, noexcept)
// - Principle IX: Layered Architecture (Layer 3 depends only on Layer 0-1)
// - Principle XII: Test-First Development
//
// Reference: specs/018-delay-engine/spec.md
// ==============================================================================

#pragma once

#include "dsp/core/block_context.h"
#include "dsp/core/note_value.h"
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/smoother.h"

#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// TimeMode Enumeration (FR-002, FR-003)
// =============================================================================

/// @brief Determines how delay time is specified.
enum class TimeMode : uint8_t {
    Free,    ///< Delay time in milliseconds (FR-002)
    Synced   ///< Delay time from NoteValue + host tempo (FR-003)
};

// =============================================================================
// DelayEngine Class
// =============================================================================

/// @brief Layer 3 wrapper for DelayLine with time modes and dry/wet mixing.
///
/// Provides a high-level interface for delay effects with:
/// - Free mode: delay time in milliseconds
/// - Synced mode: delay time from NoteValue + BlockContext tempo
/// - Smooth parameter transitions (no clicks)
/// - Dry/wet mix with kill-dry option
///
/// @note All process() methods are noexcept and allocation-free (FR-003).
/// @note Memory is allocated only in prepare() (Principle II: Real-Time Safety).
///
/// @example Basic usage:
/// @code
/// DelayEngine delay;
/// delay.prepare(44100.0, 512, 2000.0f);  // 2 second max delay
///
/// delay.setTimeMode(TimeMode::Free);
/// delay.setDelayTimeMs(250.0f);
/// delay.setMix(0.5f);
///
/// BlockContext ctx;
/// ctx.sampleRate = 44100.0;
/// delay.process(buffer, numSamples, ctx);
/// @endcode
///
/// @example Tempo-synced delay:
/// @code
/// DelayEngine delay;
/// delay.prepare(44100.0, 512, 2000.0f);
///
/// delay.setTimeMode(TimeMode::Synced);
/// delay.setNoteValue(NoteValue::Quarter, NoteModifier::Dotted);
/// delay.setMix(0.7f);
///
/// BlockContext ctx;
/// ctx.sampleRate = 44100.0;
/// ctx.tempoBPM = 120.0;
/// delay.process(buffer, numSamples, ctx);
/// @endcode
class DelayEngine {
public:
    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    /// @brief Default constructor. Creates an uninitialized engine.
    /// @note prepare() must be called before processing.
    DelayEngine() noexcept = default;

    /// @brief Destructor.
    ~DelayEngine() = default;

    // Non-copyable, movable
    DelayEngine(const DelayEngine&) = delete;
    DelayEngine& operator=(const DelayEngine&) = delete;
    DelayEngine(DelayEngine&&) noexcept = default;
    DelayEngine& operator=(DelayEngine&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-007, FR-009)
    // =========================================================================

    /// @brief Prepare the engine for processing.
    ///
    /// Allocates internal buffers based on sample rate and maximum delay time.
    /// Must be called before setActive(true) in the VST3 lifecycle.
    ///
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0, 48000.0, 96000.0)
    /// @param maxBlockSize Maximum samples per process block
    /// @param maxDelayMs Maximum delay time in milliseconds
    ///
    /// @note This method allocates memory and should NOT be called during processing.
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;

    /// @brief Clear all internal state to silence.
    ///
    /// Resets delay buffers and smoothers without reallocating.
    /// Use when starting playback to prevent artifacts from previous audio.
    void reset() noexcept;

    // =========================================================================
    // Configuration Methods
    // =========================================================================

    /// @brief Set the time mode (Free or Synced).
    /// @param mode TimeMode::Free for milliseconds, TimeMode::Synced for tempo-based
    void setTimeMode(TimeMode mode) noexcept;

    /// @brief Set delay time in milliseconds (Free mode). (FR-002)
    ///
    /// @param ms Delay time in milliseconds
    ///
    /// @note Clamped to [0, maxDelayMs] (FR-010)
    /// @note NaN values are rejected (FR-011)
    /// @note Changes are smoothed to prevent clicks (FR-004)
    void setDelayTimeMs(float ms) noexcept;

    /// @brief Set note value for tempo-synced mode. (FR-003)
    ///
    /// @param note Base note value (Quarter, Eighth, etc.)
    /// @param mod Note modifier (None, Dotted, Triplet)
    ///
    /// @note Actual delay time is calculated from BlockContext tempo during process()
    void setNoteValue(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;

    /// @brief Set dry/wet mix ratio. (FR-005)
    ///
    /// @param wetRatio Mix ratio (0.0 = fully dry, 1.0 = fully wet)
    ///
    /// @note Clamped to [0.0, 1.0]
    /// @note Changes are smoothed to prevent clicks
    void setMix(float wetRatio) noexcept;

    /// @brief Enable or disable kill-dry mode. (FR-006)
    ///
    /// When enabled, the dry signal is removed regardless of mix setting.
    /// Useful for parallel (aux send/return) configurations.
    ///
    /// @param killDry true to remove dry signal, false for normal mixing
    void setKillDry(bool killDry) noexcept;

    // =========================================================================
    // Processing Methods (FR-008)
    // =========================================================================

    /// @brief Process a mono audio buffer.
    ///
    /// @param buffer Audio buffer to process (in-place)
    /// @param numSamples Number of samples in buffer
    /// @param ctx Block context with tempo and sample rate info
    ///
    /// @note This method is noexcept and allocation-free (FR-003)
    /// @note Uses linear interpolation for sub-sample accuracy (FR-012)
    void process(float* buffer, size_t numSamples, const BlockContext& ctx) noexcept;

    /// @brief Process stereo audio buffers.
    ///
    /// @param left Left channel buffer to process (in-place)
    /// @param right Right channel buffer to process (in-place)
    /// @param numSamples Number of samples per channel
    /// @param ctx Block context with tempo and sample rate info
    ///
    /// @note Both channels receive identical delay/mix settings
    /// @note For ping-pong or stereo width, use 024-stereo-field
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get the current smoothed delay time in milliseconds.
    /// @return Current delay time after smoothing
    [[nodiscard]] float getCurrentDelayMs() const noexcept;

    /// @brief Get the current time mode.
    /// @return TimeMode::Free or TimeMode::Synced
    [[nodiscard]] TimeMode getTimeMode() const noexcept;

    /// @brief Get the maximum delay time in milliseconds.
    /// @return Maximum delay configured in prepare()
    [[nodiscard]] float getMaxDelayMs() const noexcept;

    /// @brief Check if the engine is prepared.
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // Layer 1 primitives (FR-001)
    DelayLine delayLine_;              ///< Mono delay buffer
    DelayLine delayLineRight_;         ///< Right channel for stereo
    OnePoleSmoother delaySmoother_;    ///< Smooth delay time changes (FR-004)
    OnePoleSmoother mixSmoother_;      ///< Smooth mix changes

    // Configuration state
    TimeMode timeMode_ = TimeMode::Free;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;
    float delayTimeMs_ = 0.0f;
    float mix_ = 0.5f;
    bool killDry_ = false;

    // Runtime state
    double sampleRate_ = 0.0;
    float maxDelayMs_ = 0.0f;
    size_t maxBlockSize_ = 0;
    bool prepared_ = false;

    // Internal helpers
    void updateDelayTarget(const BlockContext& ctx) noexcept;
    float msToSamples(float ms) const noexcept;
};

} // namespace DSP
} // namespace Iterum
