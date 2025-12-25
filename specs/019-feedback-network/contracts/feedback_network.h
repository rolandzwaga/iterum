// ==============================================================================
// API Contract: FeedbackNetwork
// ==============================================================================
// This file defines the public API contract for FeedbackNetwork.
// Implementation must conform to this interface exactly.
//
// Feature: 019-feedback-network
// Layer: 3 (System Component)
// Status: CONTRACT (not implementation)
// ==============================================================================

#pragma once

#include "dsp/core/block_context.h"
#include "dsp/core/stereo_utils.h"           // stereoCrossBlend (Layer 0)
#include "dsp/primitives/smoother.h"         // OnePoleSmoother (Layer 1)
#include "dsp/processors/multimode_filter.h" // MultimodeFilter (Layer 2)
#include "dsp/processors/saturation_processor.h" // SaturationProcessor (Layer 2)
#include "dsp/systems/delay_engine.h"        // DelayEngine (Layer 3)

#include <cstddef>

namespace Iterum {
namespace DSP {

/// @brief Layer 3 System Component - Feedback Network for Delay Effects
///
/// Manages the feedback loop of a delay effect with:
/// - Adjustable feedback amount (0-120% for self-oscillation)
/// - Filter in feedback path (LP/HP/BP) for tone shaping
/// - Saturation in feedback path for warmth and limiting
/// - Freeze mode for infinite sustain
/// - Stereo cross-feedback for ping-pong effects
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 3 (composes from Layer 0-2)
/// - Principle X: DSP Constraints (feedback limiting, parameter smoothing)
/// - Principle XI: Performance Budget (<1% CPU per instance)
///
/// @par Usage
/// @code
/// FeedbackNetwork network;
/// network.prepare(44100.0, 512, 2000.0f);  // 2 second max delay
/// network.setFeedbackAmount(0.7f);         // 70% feedback
/// network.setFilterEnabled(true);
/// network.setFilterType(FilterType::Lowpass);
/// network.setFilterCutoff(2000.0f);        // Warm tape-style rolloff
///
/// // In process callback
/// network.process(left, right, numSamples, ctx);
/// @endcode
class FeedbackNetwork {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinFeedback = 0.0f;
    static constexpr float kMaxFeedback = 1.2f;  ///< 120% for self-oscillation
    static constexpr float kMinCrossFeedback = 0.0f;
    static constexpr float kMaxCrossFeedback = 1.0f;
    static constexpr float kSmoothingTimeMs = 20.0f;

    // =========================================================================
    // Lifecycle (FR-007, FR-010)
    // =========================================================================

    /// @brief Default constructor
    FeedbackNetwork() noexcept = default;

    /// @brief Destructor
    ~FeedbackNetwork() = default;

    // Non-copyable, movable
    FeedbackNetwork(const FeedbackNetwork&) = delete;
    FeedbackNetwork& operator=(const FeedbackNetwork&) = delete;
    FeedbackNetwork(FeedbackNetwork&&) noexcept = default;
    FeedbackNetwork& operator=(FeedbackNetwork&&) noexcept = default;

    /// @brief Prepare for processing (FR-007)
    ///
    /// Allocates all internal buffers. Call before any processing.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process call
    /// @param maxDelayMs Maximum delay time in milliseconds
    ///
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;

    /// @brief Reset all internal state (FR-010)
    ///
    /// Clears delay buffer, filter states, and smoother histories.
    /// Call on transport stop or when resetting effect state.
    ///
    /// @note Real-time safe (no allocations)
    void reset() noexcept;

    /// @brief Check if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Processing (FR-008, FR-009, FR-015)
    // =========================================================================

    /// @brief Process mono audio buffer (FR-008)
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @param ctx Block context for tempo sync
    ///
    /// @pre prepare() has been called
    /// @pre numSamples <= maxBlockSize from prepare()
    ///
    /// @note Real-time safe: noexcept, no allocations (FR-015)
    void process(float* buffer, size_t numSamples, const BlockContext& ctx) noexcept;

    /// @brief Process stereo audio buffers (FR-009)
    ///
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @param ctx Block context for tempo sync
    ///
    /// @pre prepare() has been called
    /// @pre numSamples <= maxBlockSize from prepare()
    ///
    /// @note Real-time safe: noexcept, no allocations (FR-015)
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;

    // =========================================================================
    // Feedback Parameters (FR-002, FR-011, FR-012, FR-013)
    // =========================================================================

    /// @brief Set feedback amount (FR-002)
    ///
    /// @param amount Feedback ratio: 0.0 (none) to 1.2 (self-oscillation)
    ///
    /// Values are clamped to [0.0, 1.2] (FR-012).
    /// NaN values are rejected, keeping previous value (FR-013).
    /// Changes are smoothed over 20ms (FR-011).
    void setFeedbackAmount(float amount) noexcept;

    /// @brief Get current feedback amount
    [[nodiscard]] float getFeedbackAmount() const noexcept;

    // =========================================================================
    // Filter Parameters (FR-003, FR-014)
    // =========================================================================

    /// @brief Enable/disable filter in feedback path (FR-014)
    void setFilterEnabled(bool enabled) noexcept;

    /// @brief Check if filter is enabled
    [[nodiscard]] bool isFilterEnabled() const noexcept;

    /// @brief Set filter type (FR-003)
    /// @param type Filter type (Lowpass, Highpass, Bandpass)
    void setFilterType(FilterType type) noexcept;

    /// @brief Set filter cutoff frequency
    /// @param hz Cutoff in Hz (clamped to [20, Nyquist/2])
    void setFilterCutoff(float hz) noexcept;

    /// @brief Set filter resonance
    /// @param q Resonance/Q factor (clamped to [0.1, 10.0])
    void setFilterResonance(float q) noexcept;

    // =========================================================================
    // Saturation Parameters (FR-004, FR-014)
    // =========================================================================

    /// @brief Enable/disable saturation in feedback path (FR-014)
    void setSaturationEnabled(bool enabled) noexcept;

    /// @brief Check if saturation is enabled
    [[nodiscard]] bool isSaturationEnabled() const noexcept;

    /// @brief Set saturation type
    /// @param type Saturation type (Tape, Tube, etc.)
    void setSaturationType(SaturationType type) noexcept;

    /// @brief Set saturation drive amount
    /// @param dB Drive in dB (0 = unity, up to +24dB)
    void setSaturationDrive(float dB) noexcept;

    // =========================================================================
    // Freeze Mode (FR-005)
    // =========================================================================

    /// @brief Enable/disable freeze mode (FR-005)
    ///
    /// When frozen:
    /// - Feedback is set to 100% (infinite sustain)
    /// - Input is muted (buffer content loops forever)
    /// - Previous feedback value is restored when unfrozen
    ///
    /// Transitions are smoothed to prevent clicks.
    ///
    /// @param freeze true to freeze, false to unfreeze
    void setFreeze(bool freeze) noexcept;

    /// @brief Check if freeze mode is active
    [[nodiscard]] bool isFrozen() const noexcept;

    // =========================================================================
    // Cross-Feedback (FR-006, FR-016)
    // =========================================================================

    /// @brief Set cross-feedback amount for stereo (FR-006, FR-016)
    ///
    /// Controls how much L/R signals cross over:
    /// - 0.0: Normal stereo (L→L, R→R)
    /// - 0.5: Mono blend (each gets half of both)
    /// - 1.0: Full ping-pong (L→R, R→L)
    ///
    /// @param amount Cross-feedback ratio [0.0, 1.0]
    ///
    /// @note Only affects stereo processing
    void setCrossFeedbackAmount(float amount) noexcept;

    /// @brief Get current cross-feedback amount
    [[nodiscard]] float getCrossFeedbackAmount() const noexcept;

    // =========================================================================
    // Delay Time (delegated to DelayEngine)
    // =========================================================================

    /// @brief Set delay time in milliseconds
    void setDelayTimeMs(float ms) noexcept;

    /// @brief Set tempo-synced delay time
    void setNoteValue(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;

    /// @brief Set time mode (Free or Synced)
    void setTimeMode(TimeMode mode) noexcept;

    /// @brief Get current delay time in milliseconds
    [[nodiscard]] float getCurrentDelayMs() const noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get total processing latency in samples
    ///
    /// Includes latency from oversampling in filter/saturator if enabled.
    [[nodiscard]] size_t getLatency() const noexcept;
};

} // namespace DSP
} // namespace Iterum
