// ==============================================================================
// Layer 2: DSP Processor - FM/PM Synthesis Operator
// ==============================================================================
// Single FM synthesis operator (oscillator + ratio + feedback + level), the
// fundamental building block for FM/PM synthesis. Uses phase modulation
// (Yamaha DX7-style) where the modulator output is added to the carrier's
// phase, not frequency.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process/setters: noexcept, no alloc)
// - Principle III: Modern C++ (C++20, [[nodiscard]], value semantics)
// - Principle IX: Layer 2 (depends on Layer 0-1 only)
// - Principle XII: Test-First Development
//
// Reference: specs/021-fm-pm-synth-operator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/wavetable_oscillator.h>
#include <krate/dsp/primitives/wavetable_generator.h>
#include <krate/dsp/core/wavetable_data.h>
#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <bit>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// FMOperator Class (FR-001 through FR-015)
// =============================================================================

/// @brief Single FM synthesis operator (oscillator + ratio + feedback + level).
///
/// The fundamental building block for FM/PM synthesis. Uses phase modulation
/// (Yamaha DX7-style) where the modulator output is added to the carrier's
/// phase, not frequency.
///
/// @par Features
/// - Sine wave oscillation at frequency * ratio
/// - Self-modulation feedback with tanh soft limiting
/// - External phase modulation input (for operator chaining)
/// - Level-controlled output with raw output access for modulator use
///
/// @par Memory Model
/// Owns an internal WavetableData (~90 KB) for the sine wavetable.
/// Each FMOperator instance is self-contained.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and all setters are fully real-time safe.
/// prepare() is NOT real-time safe (generates wavetable).
///
/// @par DC Offset Behavior
/// FM feedback inherently produces steady-state DC offset at higher feedback
/// levels. As feedback increases, the sine transforms toward an asymmetric
/// sawtooth shape. This matches original DX7 behavior and is physically correct.
/// DC blocking is NOT implemented here—it should be applied once at the FM Voice
/// (Layer 3) output via a ~20 Hz high-pass filter, not per-operator.
class FMOperator {
public:
    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003, FR-014)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes to safe silence state (FR-001):
    /// - frequency = 0 Hz
    /// - ratio = 1.0
    /// - feedback = 0.0
    /// - level = 0.0
    /// - unprepared state
    ///
    /// process() returns 0.0 until prepare() is called (FR-014).
    FMOperator() noexcept
        : frequency_(0.0f)
        , ratio_(1.0f)
        , feedbackAmount_(0.0f)
        , level_(0.0f)
        , previousRawOutput_(0.0f)
        , sampleRate_(0.0)
        , prepared_(false) {
    }

    /// @brief Destructor.
    ~FMOperator() = default;

    /// @brief Copy and move operations.
    FMOperator(const FMOperator&) = default;
    FMOperator& operator=(const FMOperator&) = default;
    FMOperator(FMOperator&&) noexcept = default;
    FMOperator& operator=(FMOperator&&) noexcept = default;

    /// @brief Initialize the operator for the given sample rate (FR-002).
    ///
    /// Generates the internal sine wavetable and initializes the oscillator.
    /// All internal state is reset.
    ///
    /// @param sampleRate Sample rate in Hz (must be > 0)
    ///
    /// @note NOT real-time safe (allocates internally via FFT for wavetable)
    /// @note Calling prepare() multiple times is safe; state is fully reset
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Generate sine wavetable (FR-015)
        // Single harmonic at amplitude 1.0 produces a pure sine wave
        const float harmonics[] = {1.0f};
        generateMipmappedFromHarmonics(sineTable_, harmonics, 1);

        // Configure oscillator
        osc_.prepare(sampleRate);
        osc_.setWavetable(&sineTable_);

        // Reset state
        previousRawOutput_ = 0.0f;
        prepared_ = true;
    }

    /// @brief Reset phase and feedback history, preserving configuration (FR-003).
    ///
    /// After reset():
    /// - Phase starts from 0
    /// - Feedback history cleared (no feedback contribution on first sample)
    /// - frequency, ratio, feedbackAmount, level preserved
    ///
    /// Use on note-on for clean attack in polyphonic context.
    ///
    /// @note Real-time safe: noexcept, no allocations
    void reset() noexcept {
        osc_.resetPhase(0.0);
        previousRawOutput_ = 0.0f;
    }

    // =========================================================================
    // Parameter Setters (FR-004, FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Set the base frequency in Hz (FR-004).
    ///
    /// The effective oscillation frequency is `frequency * ratio`.
    ///
    /// @param hz Frequency in Hz, clamped to [0, sampleRate/2)
    ///
    /// @note NaN and Infinity inputs are sanitized to 0 Hz
    /// @note Real-time safe
    void setFrequency(float hz) noexcept {
        // Sanitize NaN/Inf to 0 Hz
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            frequency_ = 0.0f;
            return;
        }
        // Clamp to valid range
        if (hz < 0.0f) {
            frequency_ = 0.0f;
        } else {
            frequency_ = hz;
        }
    }

    /// @brief Set the frequency ratio (multiplier) (FR-005).
    ///
    /// The effective oscillation frequency is `frequency * ratio`.
    /// - Integer ratios (1, 2, 3) produce harmonic partials
    /// - Non-integer ratios (1.41, 3.5) produce inharmonic/metallic tones
    ///
    /// @param ratio Frequency multiplier, clamped to [0, 16.0]
    ///
    /// @note The effective frequency is also Nyquist-clamped
    /// @note Real-time safe
    void setRatio(float ratio) noexcept {
        // Preserve previous value for NaN/Inf
        if (detail::isNaN(ratio) || detail::isInf(ratio)) {
            return;
        }
        // Clamp to [0, 16.0]
        ratio_ = std::clamp(ratio, 0.0f, 16.0f);
    }

    /// @brief Set the self-modulation feedback amount (FR-006).
    ///
    /// Controls the intensity of self-modulation:
    /// - 0.0: Pure sine wave (no feedback)
    /// - 0.3-0.5: Progressively saw-like waveform
    /// - 1.0: Maximum harmonic richness, sawtooth-like
    ///
    /// The feedback signal is soft-limited using fastTanh to prevent
    /// instability: `feedbackPM = tanh(previousOutput * feedbackAmount)`.
    ///
    /// @param amount Feedback intensity, clamped to [0, 1]
    ///
    /// @note Real-time safe
    void setFeedback(float amount) noexcept {
        // Preserve previous value for NaN/Inf
        if (detail::isNaN(amount) || detail::isInf(amount)) {
            return;
        }
        feedbackAmount_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// @brief Set the output level (amplitude) (FR-007).
    ///
    /// Scales the operator's output AFTER the sine computation.
    /// When used as a modulator, level controls the modulation index.
    ///
    /// @param level Output amplitude, clamped to [0, 1]
    ///
    /// @note Real-time safe
    void setLevel(float level) noexcept {
        // Preserve previous value for NaN/Inf
        if (detail::isNaN(level) || detail::isInf(level)) {
            return;
        }
        level_ = std::clamp(level, 0.0f, 1.0f);
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get the current base frequency in Hz.
    [[nodiscard]] float getFrequency() const noexcept {
        return frequency_;
    }

    /// @brief Get the current frequency ratio.
    [[nodiscard]] float getRatio() const noexcept {
        return ratio_;
    }

    /// @brief Get the current feedback amount.
    [[nodiscard]] float getFeedback() const noexcept {
        return feedbackAmount_;
    }

    /// @brief Get the current output level.
    [[nodiscard]] float getLevel() const noexcept {
        return level_;
    }

    // =========================================================================
    // Processing (FR-008, FR-010, FR-011, FR-012, FR-013, FR-014)
    // =========================================================================

    /// @brief Generate one output sample (FR-008).
    ///
    /// @param phaseModInput External phase modulation in radians (FR-010).
    ///        A modulator output of +/-1.0 represents +/-1.0 radians of PM.
    ///        Default is 0.0 (no external modulation).
    ///
    /// @return Output sample, level-scaled and sanitized to [-2.0, 2.0] (FR-012)
    ///
    /// @note Returns 0.0 if prepare() has not been called (FR-014)
    /// @note Real-time safe: noexcept, no allocations (FR-013)
    ///
    /// @par Signal Flow
    /// 1. effectiveFreq = frequency * ratio (Nyquist-clamped)
    /// 2. feedbackPM = tanh(previousRawOutput * feedbackAmount) (FR-011)
    /// 3. totalPM = phaseModInput + feedbackPM
    /// 4. rawOutput = sin(phase + totalPM)
    /// 5. output = rawOutput * level
    /// 6. return sanitize(output)
    [[nodiscard]] float process(float phaseModInput = 0.0f) noexcept {
        // FR-014: Return silence if not prepared
        if (!prepared_) {
            return 0.0f;
        }

        // Sanitize phaseModInput
        if (detail::isNaN(phaseModInput) || detail::isInf(phaseModInput)) {
            phaseModInput = 0.0f;
        }

        // Step 1: Compute effective frequency (FR-005)
        float effectiveFreq = frequency_ * ratio_;

        // Nyquist clamp
        const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
        if (effectiveFreq >= nyquist) {
            effectiveFreq = nyquist - 0.001f;
        }
        if (effectiveFreq < 0.0f) {
            effectiveFreq = 0.0f;
        }

        // Step 2: Compute feedback contribution (FR-011)
        // feedbackPM = tanh(previousRawOutput * feedbackAmount)
        float feedbackPM = FastMath::fastTanh(previousRawOutput_ * feedbackAmount_);

        // Step 3: Combine modulation
        float totalPM = phaseModInput + feedbackPM;

        // Step 4: Set frequency and phase modulation on oscillator
        osc_.setFrequency(effectiveFreq);
        osc_.setPhaseModulation(totalPM);

        // Step 5: Generate raw sample
        float rawOutput = osc_.process();

        // Step 6: Store for feedback
        previousRawOutput_ = rawOutput;

        // Step 7: Apply level and sanitize
        float output = rawOutput * level_;

        return sanitize(output);
    }

    // =========================================================================
    // Output Access (FR-009)
    // =========================================================================

    /// @brief Get the most recent raw (pre-level) output (FR-009).
    ///
    /// Returns the output before level scaling, for use when this operator
    /// serves as a modulator. The raw output is the sine value directly,
    /// ranging approximately [-1, 1].
    ///
    /// @return Last raw output sample (before level scaling)
    [[nodiscard]] float lastRawOutput() const noexcept {
        return previousRawOutput_;
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Branchless output sanitization (FR-012).
    [[nodiscard]] static float sanitize(float x) noexcept {
        const auto bits = detail::opaqueFloatBits(x);  // fast-math-immune read
        const bool isNan = ((bits & 0x7F800000u) == 0x7F800000u) &&
                           ((bits & 0x007FFFFFu) != 0);
        x = isNan ? 0.0f : x;
        x = (x < -2.0f) ? -2.0f : x;
        x = (x > 2.0f) ? 2.0f : x;
        return x;
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration parameters (preserved across reset())
    float frequency_ = 0.0f;          ///< Base frequency in Hz
    float ratio_ = 1.0f;              ///< Frequency multiplier
    float feedbackAmount_ = 0.0f;     ///< Self-modulation intensity [0, 1]
    float level_ = 0.0f;              ///< Output amplitude [0, 1]

    // Internal state (reset on reset())
    float previousRawOutput_ = 0.0f;  ///< Last raw output for feedback

    // Resources (regenerated on prepare())
    WavetableData sineTable_;         ///< Mipmapped sine wavetable
    WavetableOscillator osc_;         ///< Internal oscillator engine

    // Lifecycle state
    double sampleRate_ = 0.0;         ///< Current sample rate
    bool prepared_ = false;           ///< True after prepare() called
};

} // namespace DSP
} // namespace Krate
