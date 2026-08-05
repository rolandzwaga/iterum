// ==============================================================================
// Layer 1: DSP Primitive - PolyBLEP Oscillator
// ==============================================================================
// Band-limited audio-rate oscillator using polynomial band-limited step
// (PolyBLEP) correction for anti-aliased waveform generation. Supports sine,
// sawtooth, square, pulse (variable width), and triangle waveforms.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations, no locks)
// - Principle III: Modern C++ (C++20, constexpr, [[nodiscard]], value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0: polyblep.h, phase_utils.h,
//   math_constants.h, db_utils.h)
// - Principle XII: Test-First Development
//
// Reference: specs/015-polyblep-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/polyblep.h>
#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// OscWaveform Enumeration (FR-001, FR-002)
// =============================================================================

/// @brief Waveform types for the PolyBLEP oscillator.
///
/// File-scope enum in Krate::DSP namespace, shared by downstream components
/// (sync oscillator, sub-oscillator, unison engine).
///
/// Values are sequential starting from 0, usable as array indices.
enum class OscWaveform : uint8_t {
    Sine = 0,       ///< Pure sine wave (no PolyBLEP correction needed)
    Sawtooth = 1,   ///< Band-limited sawtooth with PolyBLEP at wrap
    Square = 2,     ///< Band-limited square with PolyBLEP at both edges
    Pulse = 3,      ///< Band-limited pulse with variable width, PolyBLEP at both edges
    Triangle = 4    ///< Band-limited triangle via leaky-integrated PolyBLEP square
};

// =============================================================================
// PolyBlepOscillator Class (FR-003)
// =============================================================================

/// @brief Band-limited audio-rate oscillator using PolyBLEP anti-aliasing.
///
/// Generates sine, sawtooth, square, pulse, and triangle waveforms at audio
/// rates with polynomial band-limited step (PolyBLEP) correction to reduce
/// aliasing at waveform discontinuities.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread
/// (typically the audio thread). No internal synchronization.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe: no allocation,
/// no exceptions, no blocking, no I/O.
///
/// @par Usage
/// @code
/// PolyBlepOscillator osc;
/// osc.prepare(44100.0);
/// osc.setFrequency(440.0f);
/// osc.setWaveform(OscWaveform::Sawtooth);
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = osc.process();
/// }
/// @endcode
class PolyBlepOscillator {
public:
    // =========================================================================
    // Lifecycle (FR-004, FR-005)
    // =========================================================================

    PolyBlepOscillator() noexcept = default;
    ~PolyBlepOscillator() = default;

    // Copyable and movable (value semantics, no heap allocation)
    PolyBlepOscillator(const PolyBlepOscillator&) noexcept = default;
    PolyBlepOscillator& operator=(const PolyBlepOscillator&) noexcept = default;
    PolyBlepOscillator(PolyBlepOscillator&&) noexcept = default;
    PolyBlepOscillator& operator=(PolyBlepOscillator&&) noexcept = default;

    /// @brief Initialize the oscillator for the given sample rate.
    /// Resets all internal state. NOT real-time safe.
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0, 48000.0, 96000.0)
    inline void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        frequency_ = 440.0f;
        pulseWidth_ = 0.5f;
        waveform_ = OscWaveform::Sine;
        dt_ = 0.0f;
        integrator_ = 0.0f;
        fmOffset_ = 0.0f;
        pmOffset_ = 0.0f;
        phaseWrapped_ = false;
        phaseAcc_.reset();
        phaseAcc_.increment = 0.0;

        // Set default frequency
        updatePhaseIncrement();
    }

    /// @brief Reset phase and internal state without changing configuration.
    /// Resets: phase to 0, integrator to 0, phaseWrapped to false, FM/PM to 0.
    /// Preserves: frequency, waveform, pulse width, sample rate.
    inline void reset() noexcept {
        phaseAcc_.reset();
        integrator_ = 0.0f;
        fmOffset_ = 0.0f;
        pmOffset_ = 0.0f;
        phaseWrapped_ = false;
    }

    // =========================================================================
    // Parameter Setters (FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Set the oscillator frequency in Hz.
    /// Silently clamped to [0, sampleRate/2) to satisfy PolyBLEP precondition.
    /// @param hz Frequency in Hz
    inline void setFrequency(float hz) noexcept {
        // Guard against NaN/Inf (FR-033)
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            hz = 0.0f;
        }
        // Clamp to [0, sampleRate/2) (FR-006)
        const float nyquist = sampleRate_ * 0.5f;
        frequency_ = (hz < 0.0f) ? 0.0f : ((hz >= nyquist) ? (nyquist - 0.001f) : hz);
        updatePhaseIncrement();
    }

    /// @brief Select the active waveform.
    /// When switching away from or to Triangle, the leaky integrator state is cleared.
    /// Phase is maintained for continuity.
    /// @param waveform The waveform to generate
    inline void setWaveform(OscWaveform waveform) noexcept {
        // Clear integrator when entering or leaving Triangle (FR-007)
        if (waveform_ == OscWaveform::Triangle || waveform == OscWaveform::Triangle) {
            integrator_ = 0.0f;
        }
        waveform_ = waveform;
    }

    /// @brief Set the pulse width for the Pulse waveform.
    /// Silently clamped to [0.01, 0.99]. Has no effect on other waveforms.
    /// @param width Duty cycle (0.5 = square wave)
    inline void setPulseWidth(float width) noexcept {
        // Clamp to [0.01, 0.99] (FR-008)
        pulseWidth_ = (width < 0.01f) ? 0.01f : ((width > 0.99f) ? 0.99f : width);
    }

    // =========================================================================
    // Processing (FR-009, FR-010, FR-011 through FR-016)
    // =========================================================================

    /// @brief Generate and return one sample of anti-aliased output.
    /// Real-time safe: no allocation, no exceptions, no blocking, no I/O.
    /// @return Audio sample, nominally in [-1, 1] with small PolyBLEP overshoot
    [[nodiscard]] inline float process() noexcept {
        // Compute effective frequency with FM offset (FR-021)
        float effectiveFreq = frequency_ + fmOffset_;

        // Guard against NaN/Inf in effective frequency
        if (detail::isNaN(effectiveFreq) || detail::isInf(effectiveFreq)) {
            effectiveFreq = 0.0f;
        }

        // Clamp effective frequency to [0, sampleRate/2)
        const float nyquist = sampleRate_ * 0.5f;
        effectiveFreq = (effectiveFreq < 0.0f) ? 0.0f
                      : ((effectiveFreq >= nyquist) ? (nyquist - 0.001f) : effectiveFreq);

        // Compute dt for this sample
        const float dt = (sampleRate_ > 0.0f) ? (effectiveFreq / sampleRate_) : 0.0f;

        // Read current phase and apply PM offset (FR-020)
        // Guard against NaN/Inf in PM offset (FR-033)
        float safePmOffset = (detail::isNaN(pmOffset_) || detail::isInf(pmOffset_))
                           ? 0.0f : pmOffset_;
        float pmNormalized = safePmOffset / kTwoPi;
        double effectivePhase = wrapPhase(phaseAcc_.phase + static_cast<double>(pmNormalized));
        const float t = static_cast<float>(effectivePhase);

        // Generate waveform output
        float output = 0.0f;

        switch (waveform_) {
            case OscWaveform::Sine:
                // FR-011: sin(2*pi*phase)
                output = std::sin(kTwoPi * t);
                break;

            case OscWaveform::Sawtooth:
                // FR-012: naive sawtooth minus PolyBLEP correction at wrap
                // Uses 4-point PolyBLEP for improved alias suppression (>= 40 dB)
                // Scale by 2: polyBlep4 corrects a unit step (0->1), but the
                // sawtooth has a step of amplitude 2 (+1 to -1 at wrap).
                output = 2.0f * t - 1.0f;
                output -= 2.0f * polyBlep4(t, dt);
                break;

            case OscWaveform::Square: {
                // FR-013: naive square with PolyBLEP at both edges
                // Rising edge at phase=0 (from -1 to +1): ADD correction
                // Falling edge at phase=0.5 (from +1 to -1): SUBTRACT correction
                // Scale by 2: polyBlep4 corrects a unit step, but each square
                // edge has amplitude 2 (from -1 to +1 or +1 to -1).
                output = (t < 0.5f) ? 1.0f : -1.0f;
                output += 2.0f * polyBlep4(t, dt);
                output -= 2.0f * polyBlep4(static_cast<float>(wrapPhase(effectivePhase + 0.5)), dt);
                break;
            }

            case OscWaveform::Pulse: {
                // FR-014: naive pulse with PolyBLEP at rising and falling edges
                // Rising edge at phase=0 (from -1 to +1): ADD correction
                // Falling edge at phase=pw (from +1 to -1): SUBTRACT correction
                // Scale by 2: same reasoning as Square.
                output = (t < pulseWidth_) ? 1.0f : -1.0f;
                output += 2.0f * polyBlep4(t, dt);
                output -= 2.0f * polyBlep4(
                    static_cast<float>(wrapPhase(effectivePhase + 1.0 - static_cast<double>(pulseWidth_))),
                    dt);
                break;
            }

            case OscWaveform::Triangle: {
                // FR-015: integrate PolyBLEP-corrected square wave with leaky integrator
                // First compute the PolyBLEP-corrected square (50% duty cycle)
                // Scale by 2: same reasoning as Square.
                float square = (t < 0.5f) ? 1.0f : -1.0f;
                square += 2.0f * polyBlep4(t, dt);
                square -= 2.0f * polyBlep4(static_cast<float>(wrapPhase(effectivePhase + 0.5)), dt);

                // Leaky integrator with frequency-dependent coefficient
                float leak = 1.0f - (4.0f * effectiveFreq / sampleRate_);
                // Clamp leak to prevent instability at very high frequencies
                leak = (leak < 0.0f) ? 0.0f : leak;

                float scale = 4.0f * dt;
                // Anti-denormal constant (FR-035)
                constexpr float kAntiDenormal = 1e-18f;
                integrator_ = leak * integrator_ + scale * square + kAntiDenormal;
                output = integrator_;
                break;
            }
        }

        // Advance phase for next sample
        // Temporarily set increment for this sample (handles FM)
        phaseAcc_.increment = static_cast<double>(dt);
        phaseWrapped_ = phaseAcc_.advance();

        // Reset modulation offsets (FR-020, FR-021: do not accumulate)
        fmOffset_ = 0.0f;
        pmOffset_ = 0.0f;

        // Branchless output sanitization (FR-036)
        return sanitize(output);
    }

    /// @brief Generate numSamples into the provided buffer.
    /// Result is identical to calling process() that many times (SC-008).
    /// @param output Pointer to output buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate (0 = no-op)
    inline void processBlock(float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    // =========================================================================
    // Phase Access (FR-017, FR-018, FR-019)
    // =========================================================================

    /// @brief Get the current phase position.
    /// @return Phase in [0, 1), representing the oscillator's position in the cycle
    [[nodiscard]] inline double phase() const noexcept {
        return phaseAcc_.phase;
    }

    /// @brief Check if the most recent process() call produced a phase wrap.
    /// @return true if the phase crossed from near-1.0 to near-0.0
    [[nodiscard]] inline bool phaseWrapped() const noexcept {
        return phaseWrapped_;
    }

    /// @brief Force the phase to a specific position.
    /// Value is wrapped to [0, 1) if outside range.
    /// When used for hard sync, the Triangle integrator state is preserved (FR-019).
    /// @param newPhase Phase position (default: 0.0)
    inline void resetPhase(double newPhase = 0.0) noexcept {
        phaseAcc_.phase = wrapPhase(newPhase);
    }

    // =========================================================================
    // Modulation Inputs (FR-020, FR-021)
    // =========================================================================

    /// @brief Add a phase modulation offset for the current sample.
    /// Converted from radians to normalized [0, 1) internally.
    /// Does NOT accumulate between samples -- set before each process() call.
    /// @param radians Phase offset in radians
    inline void setPhaseModulation(float radians) noexcept {
        pmOffset_ = radians;
    }

    /// @brief Add a frequency modulation offset for the current sample.
    /// Effective frequency is clamped to [0, sampleRate/2).
    /// Does NOT accumulate between samples -- set before each process() call.
    /// @param hz Frequency offset in Hz (can be negative)
    inline void setFrequencyModulation(float hz) noexcept {
        fmOffset_ = hz;
    }

private:
    /// @brief Update the cached phase increment from current frequency and sample rate.
    inline void updatePhaseIncrement() noexcept {
        dt_ = (sampleRate_ > 0.0f) ? (frequency_ / sampleRate_) : 0.0f;
        phaseAcc_.increment = static_cast<double>(dt_);
    }

    /// @brief Branchless output sanitization (FR-036).
    /// NaN detection via bit manipulation, clamp to [-2.0, 2.0].
    [[nodiscard]] static inline float sanitize(float x) noexcept {
        // NaN check via bit manipulation, read through the fast-math barrier
        // (a plain bit_cast is foldable under newer fast-math compilers)
        const auto bits = detail::opaqueFloatBits(x);
        const bool isNan = ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
        x = isNan ? 0.0f : x;

        // Clamp to [-2.0, 2.0] (FR-033)
        x = (x < -2.0f) ? -2.0f : x;
        x = (x > 2.0f) ? 2.0f : x;
        return x;
    }

    // Internal state (cache-friendly layout, hot-path data first)
    PhaseAccumulator phaseAcc_;                         // Phase state (16 bytes)
    float dt_ = 0.0f;                                  // Cached phase increment as float
    float sampleRate_ = 0.0f;                          // Sample rate in Hz
    float frequency_ = 440.0f;                         // Base frequency in Hz
    float pulseWidth_ = 0.5f;                          // Pulse width [0.01, 0.99]
    float integrator_ = 0.0f;                          // Leaky integrator state (Triangle)
    float fmOffset_ = 0.0f;                            // FM offset in Hz (per-sample)
    float pmOffset_ = 0.0f;                            // PM offset in radians (per-sample)
    OscWaveform waveform_ = OscWaveform::Sine;         // Active waveform
    bool phaseWrapped_ = false;                        // Last process() produced a wrap
};

} // namespace DSP
} // namespace Krate
