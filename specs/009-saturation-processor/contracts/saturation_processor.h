// ==============================================================================
// API Contract: Saturation Processor
// ==============================================================================
// This file defines the public API contract for SaturationProcessor.
// Implementation MUST match this contract exactly.
//
// Layer: 2 (DSP Processors)
// Spec: specs/009-saturation-processor/spec.md
// ==============================================================================

#pragma once

#include "dsp/primitives/biquad.h"
#include "dsp/primitives/smoother.h"
#include "dsp/primitives/oversampler.h"
#include "dsp/core/db_utils.h"

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
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state without reallocation
    ///
    /// Clears filter states and smoother histories.
    /// Call when audio stream restarts (e.g., transport stop/start).
    void reset() noexcept;

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
    void process(float* buffer, size_t numSamples) noexcept;

    /// @brief Process a single sample
    ///
    /// @param input Input sample
    /// @return Processed output sample
    ///
    /// @pre prepare() has been called
    ///
    /// @note Less efficient than process() for buffers due to per-sample
    ///       oversampling overhead. Use for modular/per-sample contexts only.
    [[nodiscard]] float processSample(float input) noexcept;

    // -------------------------------------------------------------------------
    // Parameter Setters (FR-006 to FR-012)
    // -------------------------------------------------------------------------

    /// @brief Set saturation algorithm type
    ///
    /// @param type Saturation type (Tape, Tube, Transistor, Digital, Diode)
    ///
    /// @note Change is immediate (not smoothed)
    void setType(SaturationType type) noexcept;

    /// @brief Set input gain (pre-saturation drive)
    ///
    /// @param gainDb Gain in dB, clamped to [kMinGainDb, kMaxGainDb]
    ///
    /// @note Smoothed over kDefaultSmoothingMs to prevent clicks (FR-008)
    void setInputGain(float gainDb) noexcept;

    /// @brief Set output gain (post-saturation makeup)
    ///
    /// @param gainDb Gain in dB, clamped to [kMinGainDb, kMaxGainDb]
    ///
    /// @note Smoothed over kDefaultSmoothingMs to prevent clicks
    void setOutputGain(float gainDb) noexcept;

    /// @brief Set dry/wet mix ratio
    ///
    /// @param mix Mix ratio: 0.0 = full dry, 1.0 = full wet
    ///
    /// @note When mix == 0.0, saturation is bypassed for efficiency (FR-010)
    /// @note Smoothed to prevent clicks (FR-012)
    void setMix(float mix) noexcept;

    // -------------------------------------------------------------------------
    // Parameter Getters
    // -------------------------------------------------------------------------

    /// @brief Get current saturation type
    [[nodiscard]] SaturationType getType() const noexcept;

    /// @brief Get current input gain in dB
    [[nodiscard]] float getInputGain() const noexcept;

    /// @brief Get current output gain in dB
    [[nodiscard]] float getOutputGain() const noexcept;

    /// @brief Get current mix ratio [0.0, 1.0]
    [[nodiscard]] float getMix() const noexcept;

    // -------------------------------------------------------------------------
    // Info (FR-015)
    // -------------------------------------------------------------------------

    /// @brief Get processing latency in samples
    ///
    /// @return Latency from oversampling filters
    ///
    /// @note Report this to host for delay compensation
    [[nodiscard]] size_t getLatency() const noexcept;

private:
    // -------------------------------------------------------------------------
    // Saturation Functions (FR-001 to FR-005)
    // -------------------------------------------------------------------------

    /// @brief Tape saturation using tanh curve
    [[nodiscard]] static float saturateTape(float x) noexcept;

    /// @brief Tube saturation using asymmetric polynomial
    [[nodiscard]] static float saturateTube(float x) noexcept;

    /// @brief Transistor saturation using hard-knee soft clip
    [[nodiscard]] static float saturateTransistor(float x) noexcept;

    /// @brief Digital saturation using hard clip
    [[nodiscard]] static float saturateDigital(float x) noexcept;

    /// @brief Diode saturation using soft asymmetric curve
    [[nodiscard]] static float saturateDiode(float x) noexcept;

    /// @brief Apply current saturation type to sample
    [[nodiscard]] float applySaturation(float x) const noexcept;

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
