// ==============================================================================
// Layer 2: DSP Processor - Envelope Follower
// ==============================================================================
// Tracks the amplitude envelope of an audio signal with configurable
// attack/release times and three detection modes (Amplitude, RMS, Peak).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (sample-accurate, denormal handling)
// - Principle XII: Test-First Development
//
// Reference: specs/010-envelope-follower/spec.md
// ==============================================================================

#pragma once

#include "dsp/core/db_utils.h"
#include "dsp/primitives/biquad.h"

#include <algorithm>
#include <cmath>
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
    Peak = 2        ///< Instant attack (at min attack time), configurable release
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
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        (void)maxBlockSize;  // Not needed for this processor
        sampleRate_ = static_cast<float>(sampleRate);

        // Recalculate coefficients for new sample rate
        updateAttackCoeff();
        updateReleaseCoeff();

        // Configure sidechain filter
        sidechainFilter_.configure(
            FilterType::Highpass,
            sidechainCutoffHz_,
            kButterworthQ,
            0.0f,
            sampleRate_
        );

        reset();
    }

    /// @brief Reset internal state without reallocation
    /// @note Clears envelope and filter state
    void reset() noexcept {
        envelope_ = 0.0f;
        squaredEnvelope_ = 0.0f;
        sidechainFilter_.reset();
    }

    // =========================================================================
    // Processing (FR-016, FR-017, FR-018)
    // =========================================================================

    /// @brief Process a block of audio, writing envelope to output buffer
    /// @param input Input audio buffer (read-only)
    /// @param output Output envelope buffer (written)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(const float* input, float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = processSample(input[i]);
        }
    }

    /// @brief Process a block of audio in-place (writes envelope over input)
    /// @param buffer Audio buffer (overwritten with envelope)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = processSample(buffer[i]);
        }
    }

    /// @brief Process a single sample and return envelope value
    /// @param input Input sample
    /// @return Current envelope value
    /// @pre prepare() has been called
    [[nodiscard]] float processSample(float input) noexcept {
        // Handle NaN input (T094)
        if (detail::isNaN(input)) {
            input = 0.0f;
        }

        // Handle Inf input (T095)
        if (detail::isInf(input)) {
            input = (input > 0.0f) ? 1e10f : -1e10f;
        }

        // Apply sidechain filter if enabled
        float sample = input;
        if (sidechainEnabled_) {
            sample = sidechainFilter_.process(sample);
        }

        // Detection based on mode
        switch (mode_) {
            case DetectionMode::Amplitude:
                processAmplitude(sample);
                break;

            case DetectionMode::RMS:
                processRMS(sample);
                break;

            case DetectionMode::Peak:
                processPeak(sample);
                break;
        }

        // Flush denormals (SC-007, T096)
        envelope_ = detail::flushDenormal(envelope_);
        squaredEnvelope_ = detail::flushDenormal(squaredEnvelope_);

        return envelope_;
    }

    /// @brief Get current envelope value without advancing state
    /// @return Current envelope value [0.0, 1.0+]
    [[nodiscard]] float getCurrentValue() const noexcept {
        return envelope_;
    }

    // =========================================================================
    // Parameter Setters (FR-004 to FR-010)
    // =========================================================================

    /// @brief Set detection algorithm mode
    /// @param mode Detection mode (Amplitude, RMS, Peak)
    void setMode(DetectionMode mode) noexcept {
        if (mode_ == mode) return;

        // When switching modes, sync internal state to prevent discontinuities
        // RMS mode uses squaredEnvelope_, others use envelope_
        if (mode_ == DetectionMode::RMS && mode != DetectionMode::RMS) {
            // Switching FROM RMS: squaredEnvelope_ is valid, envelope_ may be stale
            // envelope_ is already sqrt(squaredEnvelope_), which is maintained in processRMS
        } else if (mode_ != DetectionMode::RMS && mode == DetectionMode::RMS) {
            // Switching TO RMS: sync squaredEnvelope_ from envelope_
            squaredEnvelope_ = envelope_ * envelope_;
        }

        mode_ = mode;
    }

    /// @brief Set attack time
    /// @param ms Attack time in milliseconds, clamped to [0.1, 500]
    void setAttackTime(float ms) noexcept {
        attackTimeMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
        updateAttackCoeff();
    }

    /// @brief Set release time
    /// @param ms Release time in milliseconds, clamped to [1, 5000]
    void setReleaseTime(float ms) noexcept {
        releaseTimeMs_ = std::clamp(ms, kMinReleaseMs, kMaxReleaseMs);
        updateReleaseCoeff();
    }

    /// @brief Enable or disable sidechain highpass filter
    /// @param enabled true to enable, false to bypass
    void setSidechainEnabled(bool enabled) noexcept {
        sidechainEnabled_ = enabled;
    }

    /// @brief Set sidechain filter cutoff frequency
    /// @param hz Cutoff frequency in Hz, clamped to [20, 500]
    void setSidechainCutoff(float hz) noexcept {
        sidechainCutoffHz_ = std::clamp(hz, kMinSidechainHz, kMaxSidechainHz);
        sidechainFilter_.configure(
            FilterType::Highpass,
            sidechainCutoffHz_,
            kButterworthQ,
            0.0f,
            sampleRate_
        );
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get current detection mode
    [[nodiscard]] DetectionMode getMode() const noexcept {
        return mode_;
    }

    /// @brief Get attack time in milliseconds
    [[nodiscard]] float getAttackTime() const noexcept {
        return attackTimeMs_;
    }

    /// @brief Get release time in milliseconds
    [[nodiscard]] float getReleaseTime() const noexcept {
        return releaseTimeMs_;
    }

    /// @brief Check if sidechain filter is enabled
    [[nodiscard]] bool isSidechainEnabled() const noexcept {
        return sidechainEnabled_;
    }

    /// @brief Get sidechain filter cutoff in Hz
    [[nodiscard]] float getSidechainCutoff() const noexcept {
        return sidechainCutoffHz_;
    }

    // =========================================================================
    // Info (FR-013)
    // =========================================================================

    /// @brief Get processing latency in samples
    /// @return Latency (0 - Biquad filter is zero-latency)
    [[nodiscard]] size_t getLatency() const noexcept {
        return 0;  // Biquad (TDF2) has zero latency
    }

private:
    // =========================================================================
    // Detection Mode Processing
    // =========================================================================

    /// Process Amplitude mode: full-wave rectification + asymmetric smoothing
    void processAmplitude(float sample) noexcept {
        const float rectified = std::abs(sample);

        // Asymmetric smoothing: attack when rising, release when falling
        if (rectified > envelope_) {
            // Attack: envelope rising toward input
            envelope_ = rectified + attackCoeff_ * (envelope_ - rectified);
        } else {
            // Release: envelope falling toward input
            envelope_ = rectified + releaseCoeff_ * (envelope_ - rectified);
        }
    }

    /// Process RMS mode: squared signal + smoothing + square root
    /// Uses a blended coefficient for perceptually-meaningful RMS that responds
    /// to both fast transients (attack) and smooth decay (release)
    void processRMS(float sample) noexcept {
        const float squared = sample * sample;

        // For true RMS, we need to compute the mean of squared values
        // Using asymmetric smoothing would bias toward peaks
        // Instead, use a blended coefficient that averages attack and release
        // This gives accurate RMS while still allowing some response to transients
        //
        // Technical note: True RMS of sine = peak/sqrt(2) = 0.707
        // With attack=10ms and release=100ms, the blended coeff gives ~50ms response
        const float rmsCoeff = attackCoeff_ * 0.25f + releaseCoeff_ * 0.75f;
        squaredEnvelope_ = squared + rmsCoeff * (squaredEnvelope_ - squared);

        // Output is square root of smoothed squared envelope
        envelope_ = std::sqrt(squaredEnvelope_);
    }

    /// Process Peak mode: instant attack (at min attack), configurable release
    void processPeak(float sample) noexcept {
        const float rectified = std::abs(sample);

        // Peak mode: instant capture when input exceeds envelope
        // (when attack time is at minimum), otherwise use attack coeff
        if (rectified > envelope_) {
            if (attackTimeMs_ <= kMinAttackMs + 0.01f) {
                // Near-instant attack
                envelope_ = rectified;
            } else {
                // Use attack coefficient
                envelope_ = rectified + attackCoeff_ * (envelope_ - rectified);
            }
        } else {
            // Release: exponential decay
            envelope_ = rectified + releaseCoeff_ * (envelope_ - rectified);
        }
    }

    // =========================================================================
    // Coefficient Calculation
    // =========================================================================

    /// Calculate one-pole coefficient from time constant (63.2% settling)
    /// Formula: coeff = exp(-1.0 / (time_ms * 0.001 * sampleRate))
    [[nodiscard]] float calculateCoefficient(float timeMs) const noexcept {
        if (sampleRate_ <= 0.0f || timeMs <= 0.0f) {
            return 0.0f;
        }
        const float timeSamples = timeMs * 0.001f * sampleRate_;
        return detail::constexprExp(-1.0f / timeSamples);
    }

    void updateAttackCoeff() noexcept {
        attackCoeff_ = calculateCoefficient(attackTimeMs_);
    }

    void updateReleaseCoeff() noexcept {
        releaseCoeff_ = calculateCoefficient(releaseTimeMs_);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Detection mode
    DetectionMode mode_ = DetectionMode::Amplitude;

    // Time parameters
    float attackTimeMs_ = kDefaultAttackMs;
    float releaseTimeMs_ = kDefaultReleaseMs;

    // Coefficients (recalculated when time or sample rate changes)
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;

    // Envelope state
    float envelope_ = 0.0f;
    float squaredEnvelope_ = 0.0f;  // For RMS mode

    // Sample rate
    float sampleRate_ = 44100.0f;

    // Sidechain filter (US5)
    bool sidechainEnabled_ = false;
    float sidechainCutoffHz_ = kDefaultSidechainHz;
    Biquad sidechainFilter_;
};

}  // namespace DSP
}  // namespace Iterum
