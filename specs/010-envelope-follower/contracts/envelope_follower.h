// ==============================================================================
// API Contract: Envelope Follower
// ==============================================================================
// This file defines the public API for the EnvelopeFollower class.
// Implementation details are intentionally omitted - this is a contract only.
//
// Layer: 2 (DSP Processor)
// Dependencies: Layer 0 (db_utils.h), Layer 1 (biquad.h)
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// DetectionMode Enumeration
// =============================================================================

/// @brief Detection algorithm type selection
enum class DetectionMode : uint8_t {
    Amplitude = 0,  ///< Full-wave rectification + asymmetric smoothing
    RMS = 1,        ///< Squared signal + smoothing + square root
    Peak = 2        ///< Instant attack, configurable release
};

// =============================================================================
// EnvelopeFollower Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Amplitude envelope tracker
///
/// Tracks the amplitude envelope of an audio signal with configurable
/// attack/release times and three detection modes.
///
/// @par Features
/// - Three detection modes: Amplitude, RMS, Peak (FR-001 to FR-003)
/// - Configurable attack time [0.1-500ms] (FR-005)
/// - Configurable release time [1-5000ms] (FR-006)
/// - Optional sidechain highpass filter [20-500Hz] (FR-008 to FR-010)
/// - Real-time safe: noexcept, no allocations in process (FR-019 to FR-021)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends only on Layer 0/1)
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// EnvelopeFollower env;
/// env.prepare(44100.0, 512);
/// env.setMode(DetectionMode::RMS);
/// env.setAttackTime(10.0f);
/// env.setReleaseTime(100.0f);
///
/// // In process callback
/// env.process(inputBuffer, outputBuffer, numSamples);
/// // Or per-sample:
/// float envelope = env.processSample(inputSample);
/// @endcode
class EnvelopeFollower {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 500.0f;
    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 5000.0f;
    static constexpr float kDefaultAttackMs = 10.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;
    static constexpr float kMinSidechainHz = 20.0f;
    static constexpr float kMaxSidechainHz = 500.0f;
    static constexpr float kDefaultSidechainHz = 80.0f;

    // =========================================================================
    // Lifecycle (FR-014, FR-015)
    // =========================================================================

    /// @brief Prepare processor for given sample rate
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @note Call before any processing; call again if sample rate changes
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset internal state without reallocation
    /// @note Clears envelope and filter state
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-016, FR-017, FR-018)
    // =========================================================================

    /// @brief Process a block of audio, writing envelope to output buffer
    /// @param input Input audio buffer (read-only)
    /// @param output Output envelope buffer (written)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(const float* input, float* output, size_t numSamples) noexcept;

    /// @brief Process a block of audio in-place (writes envelope over input)
    /// @param buffer Audio buffer (overwritten with envelope)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(float* buffer, size_t numSamples) noexcept;

    /// @brief Process a single sample and return envelope value
    /// @param input Input sample
    /// @return Current envelope value
    /// @pre prepare() has been called
    [[nodiscard]] float processSample(float input) noexcept;

    /// @brief Get current envelope value without advancing state
    /// @return Current envelope value [0.0, 1.0+]
    [[nodiscard]] float getCurrentValue() const noexcept;

    // =========================================================================
    // Parameter Setters (FR-004 to FR-010)
    // =========================================================================

    /// @brief Set detection algorithm mode
    /// @param mode Detection mode (Amplitude, RMS, Peak)
    void setMode(DetectionMode mode) noexcept;

    /// @brief Set attack time
    /// @param ms Attack time in milliseconds, clamped to [0.1, 500]
    void setAttackTime(float ms) noexcept;

    /// @brief Set release time
    /// @param ms Release time in milliseconds, clamped to [1, 5000]
    void setReleaseTime(float ms) noexcept;

    /// @brief Enable or disable sidechain highpass filter
    /// @param enabled true to enable, false to bypass
    void setSidechainEnabled(bool enabled) noexcept;

    /// @brief Set sidechain filter cutoff frequency
    /// @param hz Cutoff frequency in Hz, clamped to [20, 500]
    void setSidechainCutoff(float hz) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get current detection mode
    [[nodiscard]] DetectionMode getMode() const noexcept;

    /// @brief Get attack time in milliseconds
    [[nodiscard]] float getAttackTime() const noexcept;

    /// @brief Get release time in milliseconds
    [[nodiscard]] float getReleaseTime() const noexcept;

    /// @brief Check if sidechain filter is enabled
    [[nodiscard]] bool isSidechainEnabled() const noexcept;

    /// @brief Get sidechain filter cutoff in Hz
    [[nodiscard]] float getSidechainCutoff() const noexcept;

    // =========================================================================
    // Info (FR-013)
    // =========================================================================

    /// @brief Get processing latency in samples
    /// @return Latency (0 if no sidechain, small if sidechain enabled)
    [[nodiscard]] size_t getLatency() const noexcept;
};

} // namespace DSP
} // namespace Iterum
