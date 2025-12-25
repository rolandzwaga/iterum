// ==============================================================================
// Layer 3: System Component - DelayEngine
// ==============================================================================
// High-level delay wrapper with time modes, smoothing, and dry/wet mixing.
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
/// @note All process() methods are noexcept and allocation-free.
/// @note Memory is allocated only in prepare() (Principle II: Real-Time Safety).
class DelayEngine {
public:
    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    /// @brief Default constructor. Creates an uninitialized engine.
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
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process block
    /// @param maxDelayMs Maximum delay time in milliseconds
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;

    /// @brief Clear all internal state to silence.
    void reset() noexcept;

    // =========================================================================
    // Configuration Methods
    // =========================================================================

    /// @brief Set the time mode (Free or Synced).
    void setTimeMode(TimeMode mode) noexcept;

    /// @brief Set delay time in milliseconds (Free mode). (FR-002)
    void setDelayTimeMs(float ms) noexcept;

    /// @brief Set note value for tempo-synced mode. (FR-003)
    void setNoteValue(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;

    /// @brief Set dry/wet mix ratio. (FR-005)
    void setMix(float wetRatio) noexcept;

    /// @brief Enable or disable kill-dry mode. (FR-006)
    void setKillDry(bool killDry) noexcept;

    // =========================================================================
    // Processing Methods (FR-008)
    // =========================================================================

    /// @brief Process a mono audio buffer.
    void process(float* buffer, size_t numSamples, const BlockContext& ctx) noexcept;

    /// @brief Process stereo audio buffers.
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get the current smoothed delay time in milliseconds.
    [[nodiscard]] float getCurrentDelayMs() const noexcept;

    /// @brief Get the current time mode.
    [[nodiscard]] TimeMode getTimeMode() const noexcept;

    /// @brief Get the maximum delay time in milliseconds.
    [[nodiscard]] float getMaxDelayMs() const noexcept;

    /// @brief Check if the engine is prepared.
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
    float targetDelayMs_ = 0.0f;      ///< Target delay (before smoothing)
    float mix_ = 0.5f;
    bool killDry_ = false;

    // Runtime state
    double sampleRate_ = 0.0;
    float maxDelayMs_ = 0.0f;
    size_t maxBlockSize_ = 0;
    bool prepared_ = false;

    // Internal helpers
    void updateDelayTarget(const BlockContext& ctx) noexcept;
    [[nodiscard]] float msToSamples(float ms) const noexcept;
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline void DelayEngine::prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    maxDelayMs_ = maxDelayMs;

    // Convert maxDelayMs to seconds for DelayLine
    const float maxDelaySeconds = maxDelayMs / 1000.0f;

    // Prepare delay lines
    delayLine_.prepare(sampleRate, maxDelaySeconds);
    delayLineRight_.prepare(sampleRate, maxDelaySeconds);

    // Configure smoothers for 20ms smoothing time (FR-004)
    constexpr float kSmoothingTimeMs = 20.0f;
    delaySmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
    mixSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));

    // Initialize smoother values
    delaySmoother_.snapTo(0.0f);
    mixSmoother_.snapTo(mix_);

    prepared_ = true;
}

inline void DelayEngine::reset() noexcept {
    delayLine_.reset();
    delayLineRight_.reset();
    delaySmoother_.snapTo(0.0f);
    mixSmoother_.snapTo(mix_);
}

inline void DelayEngine::setTimeMode(TimeMode mode) noexcept {
    timeMode_ = mode;
}

inline void DelayEngine::setDelayTimeMs(float ms) noexcept {
    // FR-011: Reject NaN
    if (ms != ms) {  // NaN check
        return;
    }

    // FR-010: Clamp to valid range
    if (ms < 0.0f) {
        ms = 0.0f;
    }
    if (ms > maxDelayMs_) {
        ms = maxDelayMs_;
    }
    // Handle infinity
    if (ms == std::numeric_limits<float>::infinity()) {
        ms = maxDelayMs_;
    }

    targetDelayMs_ = ms;
}

inline void DelayEngine::setNoteValue(NoteValue note, NoteModifier mod) noexcept {
    noteValue_ = note;
    noteModifier_ = mod;
}

inline void DelayEngine::setMix(float wetRatio) noexcept {
    // Clamp to [0, 1]
    if (wetRatio < 0.0f) wetRatio = 0.0f;
    if (wetRatio > 1.0f) wetRatio = 1.0f;
    mix_ = wetRatio;
    mixSmoother_.setTarget(wetRatio);
}

inline void DelayEngine::setKillDry(bool killDry) noexcept {
    killDry_ = killDry;
}

inline void DelayEngine::updateDelayTarget(const BlockContext& ctx) noexcept {
    float targetMs = targetDelayMs_;

    if (timeMode_ == TimeMode::Synced) {
        // Calculate delay from tempo and note value
        // Use BlockContext::tempoToSamples then convert back to ms
        const size_t delaySamples = ctx.tempoToSamples(noteValue_, noteModifier_);
        targetMs = static_cast<float>(delaySamples * 1000.0 / ctx.sampleRate);

        // Clamp to max
        if (targetMs > maxDelayMs_) {
            targetMs = maxDelayMs_;
        }
    }

    delaySmoother_.setTarget(targetMs);
}

inline void DelayEngine::process(float* buffer, size_t numSamples, const BlockContext& ctx) noexcept {
    if (!prepared_) return;

    // Update delay target at start of block
    updateDelayTarget(ctx);

    for (size_t i = 0; i < numSamples; ++i) {
        // Get smoothed values
        const float delayMs = delaySmoother_.process();
        const float mix = mixSmoother_.process();

        // Convert delay to samples (FR-012: sub-sample accuracy via readLinear)
        const float delaySamples = msToSamples(delayMs);

        // Store input (dry signal)
        const float dry = buffer[i];

        // Write current sample to delay line FIRST
        // This allows 0-sample delay to read the current input
        delayLine_.write(dry);

        // Read delayed sample with linear interpolation
        const float wet = delayLine_.readLinear(delaySamples);

        // Mix dry and wet (FR-005, FR-006)
        const float dryCoeff = killDry_ ? 0.0f : (1.0f - mix);
        const float wetCoeff = mix;
        buffer[i] = dry * dryCoeff + wet * wetCoeff;
    }
}

inline void DelayEngine::process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept {
    if (!prepared_) return;

    // Update delay target at start of block
    updateDelayTarget(ctx);

    for (size_t i = 0; i < numSamples; ++i) {
        // Get smoothed values (shared between channels)
        const float delayMs = delaySmoother_.process();
        const float mix = mixSmoother_.process();

        // Convert delay to samples
        const float delaySamples = msToSamples(delayMs);

        // Store inputs (dry signals)
        const float dryL = left[i];
        const float dryR = right[i];

        // Write current samples to delay lines FIRST
        // This allows 0-sample delay to read the current input
        delayLine_.write(dryL);
        delayLineRight_.write(dryR);

        // Read delayed samples
        const float wetL = delayLine_.readLinear(delaySamples);
        const float wetR = delayLineRight_.readLinear(delaySamples);

        // Mix (same coefficients for both channels)
        const float dryCoeff = killDry_ ? 0.0f : (1.0f - mix);
        const float wetCoeff = mix;

        left[i] = dryL * dryCoeff + wetL * wetCoeff;
        right[i] = dryR * dryCoeff + wetR * wetCoeff;
    }
}

inline float DelayEngine::getCurrentDelayMs() const noexcept {
    return delaySmoother_.getCurrentValue();
}

inline TimeMode DelayEngine::getTimeMode() const noexcept {
    return timeMode_;
}

inline float DelayEngine::getMaxDelayMs() const noexcept {
    return maxDelayMs_;
}

inline bool DelayEngine::isPrepared() const noexcept {
    return prepared_;
}

inline float DelayEngine::msToSamples(float ms) const noexcept {
    return static_cast<float>(ms * sampleRate_ / 1000.0);
}

} // namespace DSP
} // namespace Iterum
