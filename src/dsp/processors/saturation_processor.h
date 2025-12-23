// ==============================================================================
// Layer 2: DSP Processor - Saturation Processor
// ==============================================================================
// Analog-style saturation/waveshaping processor composing Layer 1 primitives
// (Oversampler, Biquad, OnePoleSmoother) into a unified saturation module with:
// - 5 saturation types (Tape/Tube/Transistor/Digital/Diode)
// - 2x oversampling for alias-free nonlinear processing
// - Automatic DC blocking after saturation
// - Input/output gain staging [-24dB, +24dB]
// - Dry/wet mix for parallel saturation
// - Parameter smoothing for click-free modulation
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (oversampling for nonlinear, DC blocking)
// - Principle XII: Test-First Development
//
// Reference: specs/009-saturation-processor/spec.md
// ==============================================================================

#pragma once

#include "dsp/primitives/biquad.h"
#include "dsp/primitives/smoother.h"
#include "dsp/primitives/oversampler.h"
#include "dsp/core/db_utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// SaturationType Enumeration
// =============================================================================

/// @brief Saturation algorithm type selection
///
/// Each type has distinct harmonic characteristics:
/// - Tape: Symmetric tanh, odd harmonics, warm
/// - Tube: Asymmetric polynomial, even harmonics, rich
/// - Transistor: Hard-knee soft clip, aggressive
/// - Digital: Hard clip, harsh, all harmonics
/// - Diode: Soft asymmetric, subtle warmth
enum class SaturationType : uint8_t {
    Tape = 0,       ///< tanh(x) - symmetric, odd harmonics
    Tube = 1,       ///< Asymmetric polynomial - even harmonics
    Transistor = 2, ///< Hard-knee soft clip - aggressive
    Digital = 3,    ///< Hard clip (clamp) - harsh
    Diode = 4       ///< Soft asymmetric - subtle warmth
};

// =============================================================================
// SaturationProcessor Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Saturation with oversampling and DC blocking
///
/// Provides analog-style saturation/waveshaping with 5 distinct algorithms.
/// Features:
/// - 2x oversampling for alias-free processing (FR-013, FR-014)
/// - Automatic DC blocking after saturation (FR-016, FR-017)
/// - Input/output gain staging [-24, +24] dB (FR-006, FR-007)
/// - Dry/wet mix for parallel saturation (FR-009, FR-010, FR-011)
/// - Parameter smoothing for click-free modulation (FR-008, FR-012)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, RAII)
/// - Principle IX: Layer 2 (depends only on Layer 0/1)
/// - Principle X: DSP Constraints (oversampling for nonlinear, DC blocking)
///
/// @par Usage
/// @code
/// SaturationProcessor sat;
/// sat.prepare(44100.0, 512);
/// sat.setType(SaturationType::Tape);
/// sat.setInputGain(12.0f);  // +12 dB drive
/// sat.setMix(1.0f);         // 100% wet
///
/// // In process callback
/// sat.process(buffer, numSamples);
/// @endcode
///
/// @see spec.md for full requirements
class SaturationProcessor {
public:
    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------

    static constexpr float kMinGainDb = -24.0f;        ///< Minimum gain in dB
    static constexpr float kMaxGainDb = +24.0f;        ///< Maximum gain in dB
    static constexpr float kDefaultSmoothingMs = 5.0f; ///< Default smoothing time
    static constexpr float kDCBlockerCutoffHz = 10.0f; ///< DC blocker cutoff

    // -------------------------------------------------------------------------
    // Lifecycle (FR-019, FR-021)
    // -------------------------------------------------------------------------

    /// @brief Prepare processor for given sample rate and block size
    ///
    /// MUST be called before any processing. Allocates internal buffers.
    /// Call again if sample rate changes.
    ///
    /// @param sampleRate Audio sample rate in Hz (e.g., 44100.0)
    /// @param maxBlockSize Maximum samples per process() call
    ///
    /// @note Allocates memory - call from main thread, not audio thread
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;

        // Prepare parameter smoothers (5ms smoothing time)
        inputGainSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        outputGainSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        mixSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));

        // Set initial values for smoothers (linear domain for gains)
        inputGainSmoother_.snapTo(dbToGain(inputGainDb_));
        outputGainSmoother_.snapTo(dbToGain(outputGainDb_));
        mixSmoother_.snapTo(mix_);

        // Prepare oversampler
        oversampler_.prepare(sampleRate, maxBlockSize);

        // Allocate oversampled buffer (2x for 2x oversampling)
        oversampledBuffer_.resize(maxBlockSize * 2);

        // Prepare DC blocker (10Hz highpass biquad)
        dcBlocker_.configure(FilterType::Highpass, kDCBlockerCutoffHz, 0.707f, 0.0f,
                             static_cast<float>(sampleRate));

        reset();
    }

    /// @brief Reset all internal state without reallocation
    ///
    /// Clears filter states and smoother histories.
    /// Call when audio stream restarts (e.g., transport stop/start).
    void reset() noexcept {
        // Reset smoothers to current target values
        inputGainSmoother_.snapTo(dbToGain(inputGainDb_));
        outputGainSmoother_.snapTo(dbToGain(outputGainDb_));
        mixSmoother_.snapTo(mix_);

        // Reset oversampler
        oversampler_.reset();

        // Reset DC blocker
        dcBlocker_.reset();

        // Clear oversampled buffer
        std::fill(oversampledBuffer_.begin(), oversampledBuffer_.end(), 0.0f);
    }

    // -------------------------------------------------------------------------
    // Processing (FR-020, FR-022, FR-024)
    // -------------------------------------------------------------------------

    /// @brief Process a buffer of audio samples in-place
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    ///
    /// @pre prepare() has been called
    /// @pre numSamples <= maxBlockSize from prepare()
    ///
    /// @note Real-time safe: no allocations, O(N) complexity
    void process(float* buffer, size_t numSamples) noexcept {
        // Store dry signal for mix blending
        for (size_t i = 0; i < numSamples; ++i) {
            oversampledBuffer_[i] = buffer[i];  // Temporarily use as dry buffer
        }

        // Advance smoothers first to reach target values
        // Run at base sample rate for click-free modulation
        for (size_t i = 0; i < numSamples; ++i) {
            (void)inputGainSmoother_.process();
            (void)outputGainSmoother_.process();
            (void)mixSmoother_.process();
        }

        // Cache smoothed values for use in oversampled callback
        const float inputGain = inputGainSmoother_.getCurrentValue();
        const float outputGain = outputGainSmoother_.getCurrentValue();

        // Process with oversampling
        oversampler_.process(buffer, numSamples, [this, inputGain, outputGain](float* upsampled, size_t numOversampledSamples) {
            // Apply saturation at oversampled rate
            for (size_t i = 0; i < numOversampledSamples; ++i) {
                // Apply input gain (drive)
                float signal = upsampled[i] * inputGain;

                // Apply saturation
                signal = applySaturation(signal);

                // Apply output gain (makeup)
                upsampled[i] = signal * outputGain;
            }
        });

        // Apply DC blocking after saturation
        dcBlocker_.processBlock(buffer, numSamples);

        // Blend dry/wet using smoothed mix value
        const float mix = mixSmoother_.getCurrentValue();
        if (mix < 0.9999f) {  // Skip blending if fully wet
            const float dryMix = 1.0f - mix;
            for (size_t i = 0; i < numSamples; ++i) {
                buffer[i] = oversampledBuffer_[i] * dryMix + buffer[i] * mix;
            }
        }
    }

    /// @brief Process a single sample
    ///
    /// @param input Input sample
    /// @return Processed output sample
    ///
    /// @pre prepare() has been called
    ///
    /// @note Less efficient than process() for buffers due to per-sample
    ///       oversampling overhead. Use for modular/per-sample contexts only.
    [[nodiscard]] float processSample(float input) noexcept {
        // Store dry signal for mix blending
        const float dry = input;

        // Get smoothed parameter values
        const float inputGain = inputGainSmoother_.process();
        const float outputGain = outputGainSmoother_.process();
        const float mix = mixSmoother_.process();

        // Early exit for full dry (bypass saturation entirely for efficiency)
        if (mix < 0.0001f) {
            return dry;
        }

        // Apply input gain (drive)
        float signal = input * inputGain;

        // Apply saturation
        signal = applySaturation(signal);

        // Apply output gain (makeup)
        signal *= outputGain;

        // Blend dry/wet
        return dry * (1.0f - mix) + signal * mix;
    }

    // -------------------------------------------------------------------------
    // Parameter Setters (FR-006 to FR-012)
    // -------------------------------------------------------------------------

    /// @brief Set saturation algorithm type
    ///
    /// @param type Saturation type (Tape, Tube, Transistor, Digital, Diode)
    ///
    /// @note Change is immediate (not smoothed)
    void setType(SaturationType type) noexcept {
        type_ = type;
    }

    /// @brief Set input gain (pre-saturation drive)
    ///
    /// @param gainDb Gain in dB, clamped to [kMinGainDb, kMaxGainDb]
    ///
    /// @note Smoothed over kDefaultSmoothingMs to prevent clicks (FR-008)
    void setInputGain(float gainDb) noexcept {
        inputGainDb_ = std::clamp(gainDb, kMinGainDb, kMaxGainDb);
        inputGainSmoother_.setTarget(dbToGain(inputGainDb_));
    }

    /// @brief Set output gain (post-saturation makeup)
    ///
    /// @param gainDb Gain in dB, clamped to [kMinGainDb, kMaxGainDb]
    ///
    /// @note Smoothed over kDefaultSmoothingMs to prevent clicks
    void setOutputGain(float gainDb) noexcept {
        outputGainDb_ = std::clamp(gainDb, kMinGainDb, kMaxGainDb);
        outputGainSmoother_.setTarget(dbToGain(outputGainDb_));
    }

    /// @brief Set dry/wet mix ratio
    ///
    /// @param mix Mix ratio: 0.0 = full dry, 1.0 = full wet
    ///
    /// @note When mix == 0.0, saturation is bypassed for efficiency (FR-010)
    /// @note Smoothed to prevent clicks (FR-012)
    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        mixSmoother_.setTarget(mix_);
    }

    // -------------------------------------------------------------------------
    // Parameter Getters
    // -------------------------------------------------------------------------

    /// @brief Get current saturation type
    [[nodiscard]] SaturationType getType() const noexcept {
        return type_;
    }

    /// @brief Get current input gain in dB
    [[nodiscard]] float getInputGain() const noexcept {
        return inputGainDb_;
    }

    /// @brief Get current output gain in dB
    [[nodiscard]] float getOutputGain() const noexcept {
        return outputGainDb_;
    }

    /// @brief Get current mix ratio [0.0, 1.0]
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    // -------------------------------------------------------------------------
    // Info (FR-015)
    // -------------------------------------------------------------------------

    /// @brief Get processing latency in samples
    ///
    /// @return Latency from oversampling filters
    ///
    /// @note Report this to host for delay compensation
    [[nodiscard]] size_t getLatency() const noexcept {
        return oversampler_.getLatency();
    }

private:
    // -------------------------------------------------------------------------
    // Saturation Functions (FR-001 to FR-005)
    // -------------------------------------------------------------------------

    /// @brief Tape saturation using tanh curve
    [[nodiscard]] static float saturateTape(float x) noexcept {
        // FR-001: tanh(x) - symmetric, odd harmonics
        return std::tanh(x);
    }

    /// @brief Tube saturation using asymmetric polynomial
    [[nodiscard]] static float saturateTube(float x) noexcept {
        // FR-002: Asymmetric polynomial - even harmonics
        // Classic tube-style asymmetric transfer function:
        // y = x + a*x² - b*x³, then soft limited via tanh
        // The x² term introduces even harmonics (2nd, 4th, etc.)
        const float x2 = x * x;
        const float x3 = x2 * x;
        // Coefficients tuned for musical character and even harmonic content
        const float asymmetric = x + 0.3f * x2 - 0.15f * x3;
        // Soft limit to prevent runaway for high input levels
        return std::tanh(asymmetric);
    }

    /// @brief Transistor saturation using hard-knee soft clip
    [[nodiscard]] static float saturateTransistor(float x) noexcept {
        // FR-003: Hard-knee soft clip - aggressive
        // Linear below threshold, then sharp transition to soft saturation
        constexpr float kThreshold = 0.5f;
        constexpr float kKnee = 1.0f - kThreshold;

        const float absX = std::abs(x);
        if (absX <= kThreshold) {
            // Linear region
            return x;
        }
        // Above threshold: soft clip with hard knee
        // Maps (threshold, inf) -> (threshold, 1.0)
        const float excess = absX - kThreshold;
        const float compressed = kThreshold + kKnee * std::tanh(excess / kKnee);
        return (x >= 0.0f) ? compressed : -compressed;
    }

    /// @brief Digital saturation using hard clip
    [[nodiscard]] static float saturateDigital(float x) noexcept {
        // FR-004: Hard clip (clamp) - harsh
        return std::clamp(x, -1.0f, 1.0f);
    }

    /// @brief Diode saturation using soft asymmetric curve
    [[nodiscard]] static float saturateDiode(float x) noexcept {
        // FR-005: Soft asymmetric - subtle warmth
        // Models diode conduction: soft knee in forward bias,
        // harder cutoff in reverse bias. Creates even harmonics.
        if (x >= 0.0f) {
            // Forward bias: soft exponential saturation
            // y = 1 - exp(-x), but scaled for unity gain at low levels
            return 1.0f - std::exp(-x * 1.5f);
        } else {
            // Reverse bias: harder, more linear with soft limit
            // Creates asymmetry for even harmonic content
            return x / (1.0f - 0.5f * x);  // Soft inverse curve
        }
    }

    /// @brief Apply current saturation type to sample
    [[nodiscard]] float applySaturation(float x) const noexcept {
        switch (type_) {
            case SaturationType::Tape:
                return saturateTape(x);
            case SaturationType::Tube:
                return saturateTube(x);
            case SaturationType::Transistor:
                return saturateTransistor(x);
            case SaturationType::Digital:
                return saturateDigital(x);
            case SaturationType::Diode:
                return saturateDiode(x);
            default:
                return saturateTape(x);
        }
    }

    // -------------------------------------------------------------------------
    // Private Members
    // -------------------------------------------------------------------------

    // Parameters
    SaturationType type_ = SaturationType::Tape;
    float inputGainDb_ = 0.0f;
    float outputGainDb_ = 0.0f;
    float mix_ = 1.0f;

    // Sample rate
    double sampleRate_ = 44100.0;

    // Parameter smoothers (FR-008, FR-012)
    OnePoleSmoother inputGainSmoother_;
    OnePoleSmoother outputGainSmoother_;
    OnePoleSmoother mixSmoother_;

    // DSP components
    Oversampler<2, 1> oversampler_;  // 2x oversampling (FR-013, FR-014)
    Biquad dcBlocker_;               // DC blocking filter (FR-016, FR-017, FR-018)

    // Pre-allocated buffer (FR-025)
    std::vector<float> oversampledBuffer_;
};

} // namespace DSP
} // namespace Iterum
