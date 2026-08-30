// ==============================================================================
// Layer 2: DSP Processor - Phase Distortion Oscillator
// ==============================================================================
// Casio CZ-style Phase Distortion oscillator implementing 8 waveform types
// with DCW (Digitally Controlled Wave) morphing. At distortion=0.0, all
// waveforms produce pure sine. At distortion=1.0, each produces its
// characteristic shape (saw, square, pulse, etc.) or resonant peak.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process/setters: noexcept, no alloc)
// - Principle III: Modern C++ (C++20, [[nodiscard]], value semantics)
// - Principle IX: Layer 2 (depends on Layer 0-1 only)
// - Principle XII: Test-First Development
//
// Reference: specs/024-phase-distortion-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/wavetable_oscillator.h>
#include <krate/dsp/primitives/wavetable_generator.h>
#include <krate/dsp/core/wavetable_data.h>
#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/interpolation.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// PDWaveform Enumeration (FR-002)
// =============================================================================

/// @brief Waveform types for Phase Distortion synthesis.
///
/// Non-resonant waveforms (0-4) use piecewise-linear phase transfer functions.
/// Resonant waveforms (5-7) use windowed sync technique for filter-like timbres.
enum class PDWaveform : uint8_t {
    Saw = 0,              ///< Sawtooth via two-segment phase transfer
    Square = 1,           ///< Square wave via four-segment phase transfer
    Pulse = 2,            ///< Variable-width pulse via asymmetric duty cycle
    DoubleSine = 3,       ///< Octave-doubled tone via phase doubling
    HalfSine = 4,         ///< Half-wave rectified tone via phase reflection
    ResonantSaw = 5,      ///< Resonant peak with falling sawtooth window
    ResonantTriangle = 6, ///< Resonant peak with triangle window
    ResonantTrapezoid = 7 ///< Resonant peak with trapezoid window
};

/// @brief Number of waveform types in PDWaveform enum.
inline constexpr size_t kNumPDWaveforms = 8;

// =============================================================================
// PhaseDistortionOscillator Class (FR-001)
// =============================================================================

/// @brief Casio CZ-style Phase Distortion oscillator at Layer 2.
///
/// Generates audio by reading a cosine wavetable at variable rates determined
/// by piecewise-linear phase transfer functions (non-resonant waveforms) or
/// windowed sync technique (resonant waveforms).
///
/// @par Features
/// - 8 waveform types with characteristic timbres
/// - DCW (distortion) parameter morphs from sine to full waveform shape
/// - Phase modulation input for FM/PM synthesis integration
/// - Automatic mipmap anti-aliasing via internal WavetableOscillator
///
/// @par Memory Model
/// Owns internal WavetableData (~90 KB) for the cosine wavetable.
/// Each PhaseDistortionOscillator instance is self-contained.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// - prepare(): NOT real-time safe (generates wavetable)
/// - reset(), setters, process(), processBlock(): Real-time safe
///
/// @par Layer Dependencies
/// - Layer 0: phase_utils.h, math_constants.h, db_utils.h, interpolation.h, wavetable_data.h
/// - Layer 1: wavetable_oscillator.h, wavetable_generator.h
class PhaseDistortionOscillator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @brief Default maximum resonance factor for resonant waveforms.
    /// At distortion=1.0, resonanceMultiplier = 1 + maxResonanceFactor = 9.0
    static constexpr float kDefaultMaxResonanceFactor = 8.0f;

    // =========================================================================
    // Lifecycle (FR-016, FR-017, FR-029)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes to safe silence state:
    /// - frequency = 440 Hz
    /// - distortion = 0.0 (pure sine)
    /// - waveform = Saw
    /// - unprepared state (process() returns 0.0)
    PhaseDistortionOscillator() noexcept
        : frequency_(440.0f)
        , distortion_(0.0f)
        , waveform_(PDWaveform::Saw)
        , maxResonanceFactor_(kDefaultMaxResonanceFactor)
        , sampleRate_(0.0f)
        , phaseWrapped_(false)
        , prepared_(false) {
    }

    /// @brief Destructor.
    ~PhaseDistortionOscillator() = default;

    /// @brief Copy and move operations.
    PhaseDistortionOscillator(const PhaseDistortionOscillator&) = default;
    PhaseDistortionOscillator& operator=(const PhaseDistortionOscillator&) = default;
    PhaseDistortionOscillator(PhaseDistortionOscillator&&) noexcept = default;
    PhaseDistortionOscillator& operator=(PhaseDistortionOscillator&&) noexcept = default;

    /// @brief Initialize the oscillator for the given sample rate (FR-016).
    ///
    /// Generates the internal cosine wavetable and initializes the oscillator.
    /// All internal state is reset. Memory allocation occurs here.
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000 supported)
    ///
    /// @note NOT real-time safe (generates wavetable via FFT)
    /// @note Calling prepare() multiple times is safe; state is fully reset
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Generate cosine wavetable (FR-003)
        // Single harmonic at amplitude 1.0 produces a sine wave
        // We use sine table and add 0.25 phase offset to get cosine
        const float harmonics[] = {1.0f};
        generateMipmappedFromHarmonics(cosineTable_, harmonics, 1);

        // Configure internal oscillator for cosine lookup
        osc_.prepare(sampleRate);
        osc_.setWavetable(&cosineTable_);

        // Reset state
        phaseAcc_.reset();
        phaseAcc_.increment = calculatePhaseIncrement(frequency_, sampleRate_);
        phaseWrapped_ = false;
        prepared_ = true;
    }

    /// @brief Reset phase and internal state without changing configuration (FR-017).
    ///
    /// After reset():
    /// - Phase starts from 0
    /// - Configuration preserved: frequency, distortion, waveform
    ///
    /// Use on note-on for clean attack in polyphonic context.
    ///
    /// @note Real-time safe: noexcept, no allocations
    void reset() noexcept {
        phaseAcc_.reset();
        osc_.resetPhase(0.0);
        phaseWrapped_ = false;
    }

    // =========================================================================
    // Parameter Setters (FR-018, FR-019, FR-020)
    // =========================================================================

    /// @brief Set the fundamental frequency in Hz (FR-018).
    ///
    /// @param hz Frequency in Hz, clamped to [0, sampleRate/2)
    ///
    /// @note NaN and Infinity inputs are sanitized to 0 Hz
    /// @note Negative frequencies are clamped to 0 Hz
    /// @note Real-time safe
    void setFrequency(float hz) noexcept {
        // Sanitize NaN/Inf to 0 Hz
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            frequency_ = 0.0f;
            if (prepared_) {
                phaseAcc_.increment = 0.0;
            }
            return;
        }
        // Clamp to valid range
        if (hz < 0.0f) {
            frequency_ = 0.0f;
        } else if (sampleRate_ > 0.0f && hz >= sampleRate_ * 0.5f) {
            frequency_ = sampleRate_ * 0.5f - 0.001f;
        } else {
            frequency_ = hz;
        }
        if (prepared_) {
            phaseAcc_.increment = calculatePhaseIncrement(frequency_, sampleRate_);
        }
    }

    /// @brief Set the waveform type (FR-019).
    ///
    /// @param waveform Waveform type from PDWaveform enum
    ///
    /// @note Change takes effect on next process() call
    /// @note Phase is preserved to minimize discontinuities
    /// @note Real-time safe
    void setWaveform(PDWaveform waveform) noexcept {
        waveform_ = waveform;
    }

    /// @brief Set the distortion (DCW) amount (FR-020).
    ///
    /// @param amount Distortion intensity [0, 1]
    ///        - 0.0: Pure sine wave (regardless of waveform)
    ///        - 1.0: Full characteristic waveform shape
    ///
    /// @note NaN and Infinity inputs preserve previous value
    /// @note Out-of-range values are clamped to [0, 1]
    /// @note Real-time safe
    void setDistortion(float amount) noexcept {
        // Preserve previous value for NaN/Inf
        if (detail::isNaN(amount) || detail::isInf(amount)) {
            return;
        }
        distortion_ = std::clamp(amount, 0.0f, 1.0f);
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get the current frequency in Hz.
    [[nodiscard]] float getFrequency() const noexcept {
        return frequency_;
    }

    /// @brief Get the current waveform type.
    [[nodiscard]] PDWaveform getWaveform() const noexcept {
        return waveform_;
    }

    /// @brief Get the current distortion amount.
    [[nodiscard]] float getDistortion() const noexcept {
        return distortion_;
    }

    // =========================================================================
    // Processing (FR-021, FR-022, FR-026, FR-027, FR-028, FR-029)
    // =========================================================================

    /// @brief Generate one output sample (FR-021).
    ///
    /// @param phaseModInput External phase modulation in radians (FR-026).
    ///        Added to linear phase BEFORE phase distortion transfer function.
    ///        Default is 0.0 (no external modulation).
    ///
    /// @return Output sample, sanitized to [-2.0, 2.0] (FR-028)
    ///
    /// @note Returns 0.0 if prepare() has not been called (FR-029)
    /// @note Real-time safe: noexcept, no allocations (FR-027)
    [[nodiscard]] float process(float phaseModInput = 0.0f) noexcept {
        // FR-029: Return silence if not prepared
        if (!prepared_) {
            return 0.0f;
        }

        // Sanitize phaseModInput
        if (detail::isNaN(phaseModInput) || detail::isInf(phaseModInput)) {
            phaseModInput = 0.0f;
        }

        // Get current phase [0, 1)
        float phi = static_cast<float>(phaseAcc_.phase);

        // Add phase modulation (convert from radians to normalized [0,1))
        // FR-026: Phase modulation is added BEFORE phase distortion
        float pmNormalized = phaseModInput / kTwoPi;
        phi += pmNormalized;
        // Wrap to [0, 1)
        while (phi >= 1.0f) phi -= 1.0f;
        while (phi < 0.0f) phi += 1.0f;

        // Generate output based on waveform type
        float output = 0.0f;

        switch (waveform_) {
            case PDWaveform::Saw:
                output = computeSawOutput(phi);
                break;
            case PDWaveform::Square:
                output = computeSquareOutput(phi);
                break;
            case PDWaveform::Pulse:
                output = computePulseOutput(phi);
                break;
            case PDWaveform::DoubleSine:
                output = computeDoubleSineOutput(phi);
                break;
            case PDWaveform::HalfSine:
                output = computeHalfSineOutput(phi);
                break;
            case PDWaveform::ResonantSaw:
                output = computeResonantSaw(phi);
                break;
            case PDWaveform::ResonantTriangle:
                output = computeResonantTriangle(phi);
                break;
            case PDWaveform::ResonantTrapezoid:
                output = computeResonantTrapezoid(phi);
                break;
        }

        // Advance phase accumulator
        phaseWrapped_ = phaseAcc_.advance();

        return sanitize(output);
    }

    /// @brief Generate multiple samples at constant parameters (FR-022).
    ///
    /// Produces output identical to calling process() numSamples times.
    ///
    /// @param output Output buffer to fill
    /// @param numSamples Number of samples to generate
    ///
    /// @note Real-time safe: noexcept, no allocations (FR-027)
    void processBlock(float* output, size_t numSamples) noexcept {
        if (output == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    // =========================================================================
    // Phase Access (FR-023, FR-024, FR-025)
    // =========================================================================

    /// @brief Get the current phase position (FR-023).
    ///
    /// @return Phase in [0, 1) range
    [[nodiscard]] double phase() const noexcept {
        return phaseAcc_.phase;
    }

    /// @brief Check if the most recent process() caused a phase wrap (FR-024).
    ///
    /// @return true if phase wrapped from near-1.0 to near-0.0
    [[nodiscard]] bool phaseWrapped() const noexcept {
        return phaseWrapped_;
    }

    /// @brief Force the phase to a specific position (FR-025).
    ///
    /// @param newPhase Phase position, wrapped to [0, 1)
    void resetPhase(double newPhase = 0.0) noexcept {
        phaseAcc_.phase = wrapPhase(newPhase);
    }

    // =========================================================================
    // Advanced Configuration
    // =========================================================================

    /// @brief Set the maximum resonance factor for resonant waveforms.
    ///
    /// Controls how high the resonant frequency goes at full distortion.
    /// resonanceMultiplier = 1 + distortion * maxResonanceFactor
    ///
    /// @param factor Maximum factor [1, 16], default 8.0
    ///
    /// @note Real-time safe
    void setMaxResonanceFactor(float factor) noexcept {
        if (detail::isNaN(factor) || detail::isInf(factor)) {
            return;
        }
        maxResonanceFactor_ = std::clamp(factor, 1.0f, 16.0f);
    }

    /// @brief Get the current maximum resonance factor.
    [[nodiscard]] float getMaxResonanceFactor() const noexcept {
        return maxResonanceFactor_;
    }

private:
    // =========================================================================
    // Phase Transfer Functions (Non-Resonant)
    // =========================================================================

    /// @brief Compute Saw phase transfer function (FR-006).
    [[nodiscard]] float computeSawPhase(float phi) const noexcept {
        // d ranges from 0.5 (distortion=0) to 0.01 (distortion=1)
        float d = 0.5f - (distortion_ * 0.49f);

        float phiPrime;
        if (phi < d) {
            phiPrime = phi * (0.5f / d);
        } else {
            phiPrime = 0.5f + (phi - d) * (0.5f / (1.0f - d));
        }

        return phiPrime;
    }

    /// @brief Compute Saw waveform output.
    [[nodiscard]] float computeSawOutput(float phi) const noexcept {
        float distortedPhase = computeSawPhase(phi);
        return lookupCosine(distortedPhase);
    }

    /// @brief Compute Square phase transfer function (FR-007).
    [[nodiscard]] float computeSquarePhase(float phi) const noexcept {
        float d = 0.5f - (distortion_ * 0.49f);

        float phiPrime;
        if (phi < d) {
            phiPrime = phi * (0.5f / d);
        } else if (phi < 0.5f) {
            phiPrime = 0.5f;  // Flat
        } else if (phi < 0.5f + d) {
            phiPrime = 0.5f + (phi - 0.5f) * (0.5f / d);
        } else {
            phiPrime = 1.0f;  // Flat (wraps to 0 in cosine)
        }

        return phiPrime;
    }

    /// @brief Compute Square waveform output.
    [[nodiscard]] float computeSquareOutput(float phi) const noexcept {
        float distortedPhase = computeSquarePhase(phi);
        return lookupCosine(distortedPhase);
    }

    /// @brief Compute Pulse phase transfer function (FR-008).
    [[nodiscard]] float computePulsePhase(float phi) const noexcept {
        // Duty cycle: 50% at distortion=0, 5% at distortion=1
        float duty = 0.5f - (distortion_ * 0.45f);

        float phiPrime;
        if (phi < duty) {
            phiPrime = phi * (0.5f / duty);
        } else if (phi < 0.5f) {
            phiPrime = 0.5f;
        } else if (phi < 0.5f + duty) {
            phiPrime = 0.5f + (phi - 0.5f) * (0.5f / duty);
        } else {
            phiPrime = 1.0f;
        }

        return phiPrime;
    }

    /// @brief Compute Pulse waveform output.
    [[nodiscard]] float computePulseOutput(float phi) const noexcept {
        float distortedPhase = computePulsePhase(phi);
        return lookupCosine(distortedPhase);
    }

    /// @brief Compute DoubleSine phase transfer function (FR-009).
    [[nodiscard]] float computeDoubleSinePhase(float phi) const noexcept {
        // Distorted phase = doubled phase mod 1
        float phiDistorted = std::fmod(2.0f * phi, 1.0f);
        // Blend between linear and distorted
        return Interpolation::linearInterpolate(phi, phiDistorted, distortion_);
    }

    /// @brief Compute DoubleSine waveform output.
    [[nodiscard]] float computeDoubleSineOutput(float phi) const noexcept {
        float distortedPhase = computeDoubleSinePhase(phi);
        return lookupCosine(distortedPhase);
    }

    /// @brief Compute HalfSine phase transfer function (FR-010).
    /// Maps the second half of the cycle back, creating a half-wave rectified-like tone.
    /// At full distortion, produces asymmetric output with even harmonics.
    [[nodiscard]] float computeHalfSinePhase(float phi) const noexcept {
        // Distorted phase as per spec: phi_distorted = (phi < 0.5) ? phi : (1.0 - (phi - 0.5) * 2.0)
        // At phi=0.0, phiDistorted=0.0
        // At phi=0.5, phiDistorted=0.5
        // At phi=0.75, phiDistorted=0.0
        // At phi=1.0, phiDistorted=-1.0 (wraps to 0.0)
        // This creates a waveform that rises to peak then falls to zero in second half
        float phiDistorted;
        if (phi < 0.5f) {
            // First half: normal
            phiDistorted = phi;
        } else {
            // Second half: compress back towards 0
            phiDistorted = 1.0f - (phi - 0.5f) * 2.0f;
            // Wrap to [0, 1)
            if (phiDistorted < 0.0f) phiDistorted = 0.0f;
        }
        // Blend between linear and distorted
        return Interpolation::linearInterpolate(phi, phiDistorted, distortion_);
    }

    /// @brief Compute HalfSine waveform output.
    [[nodiscard]] float computeHalfSineOutput(float phi) const noexcept {
        float distortedPhase = computeHalfSinePhase(phi);
        return lookupCosine(distortedPhase);
    }

    // =========================================================================
    // Resonant Waveform Functions (FR-011 through FR-015a)
    // =========================================================================

    /// @brief Compute ResonantSaw waveform (FR-011, FR-012).
    /// At distortion=0, produces pure sine. At distortion=1, full resonant effect.
    [[nodiscard]] float computeResonantSaw(float phi) const noexcept {
        // Fast path for pure sine (distortion=0) - FR-004
        if (distortion_ <= 0.0f) {
            return lookupCosine(phi);
        }

        // Fast path for full resonant (distortion=1)
        if (distortion_ >= 1.0f) {
            float window = 1.0f - phi;
            float resonanceMult = 1.0f + maxResonanceFactor_;
            float resonantPhase = resonanceMult * phi;
            return window * lookupCosine(resonantPhase);
        }

        // Blended path
        float sineOutput = lookupCosine(phi);
        float window = 1.0f - phi;
        float resonanceMult = 1.0f + distortion_ * maxResonanceFactor_;
        float resonantPhase = resonanceMult * phi;
        float resonantOutput = window * lookupCosine(resonantPhase);
        return Interpolation::linearInterpolate(sineOutput, resonantOutput, distortion_);
    }

    /// @brief Compute ResonantTriangle waveform (FR-011, FR-013).
    /// At distortion=0, produces pure sine. At distortion=1, full resonant effect.
    [[nodiscard]] float computeResonantTriangle(float phi) const noexcept {
        // Fast path for pure sine (distortion=0) - FR-004
        if (distortion_ <= 0.0f) {
            return lookupCosine(phi);
        }

        // Compute window (needed for both paths)
        float window = 1.0f - std::abs(2.0f * phi - 1.0f);

        // Fast path for full resonant (distortion=1)
        if (distortion_ >= 1.0f) {
            float resonanceMult = 1.0f + maxResonanceFactor_;
            float resonantPhase = resonanceMult * phi;
            return window * lookupCosine(resonantPhase);
        }

        // Blended path
        float sineOutput = lookupCosine(phi);
        float resonanceMult = 1.0f + distortion_ * maxResonanceFactor_;
        float resonantPhase = resonanceMult * phi;
        float resonantOutput = window * lookupCosine(resonantPhase);
        return Interpolation::linearInterpolate(sineOutput, resonantOutput, distortion_);
    }

    /// @brief Compute ResonantTrapezoid waveform (FR-011, FR-014).
    /// At distortion=0, produces pure sine. At distortion=1, full resonant effect.
    [[nodiscard]] float computeResonantTrapezoid(float phi) const noexcept {
        // Fast path for pure sine (distortion=0) - FR-004
        if (distortion_ <= 0.0f) {
            return lookupCosine(phi);
        }

        // Compute trapezoid window (needed for both paths)
        float window;
        if (phi < 0.25f) {
            window = 4.0f * phi;  // Rising edge
        } else if (phi < 0.75f) {
            window = 1.0f;  // Flat top
        } else {
            window = 4.0f * (1.0f - phi);  // Falling edge
        }

        // Fast path for full resonant (distortion=1)
        if (distortion_ >= 1.0f) {
            float resonanceMult = 1.0f + maxResonanceFactor_;
            float resonantPhase = resonanceMult * phi;
            return window * lookupCosine(resonantPhase);
        }

        // Blended path
        float sineOutput = lookupCosine(phi);
        float resonanceMult = 1.0f + distortion_ * maxResonanceFactor_;
        float resonantPhase = resonanceMult * phi;
        float resonantOutput = window * lookupCosine(resonantPhase);
        return Interpolation::linearInterpolate(sineOutput, resonantOutput, distortion_);
    }

    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Look up cosine value using the wavetable.
    /// Uses sine table with 0.25 phase offset to get cosine.
    [[nodiscard]] float lookupCosine(float normalizedPhase) const noexcept {
        // Sine table with 0.25 offset = cosine
        // cos(2*pi*phi) = sin(2*pi*(phi + 0.25))
        float cosPhase = normalizedPhase + 0.25f;
        if (cosPhase >= 1.0f) cosPhase -= 1.0f;

        // Direct table lookup at the computed cosine phase
        const float* table = cosineTable_.getLevel(0);  // Use level 0 for best quality
        if (table == nullptr) {
            return 0.0f;
        }

        // Direct cubic Hermite interpolation
        size_t tableSize = kDefaultTableSize;
        double tablePhase = static_cast<double>(cosPhase) * static_cast<double>(tableSize);
        auto intPhase = static_cast<size_t>(tablePhase);
        auto fracPhase = static_cast<float>(tablePhase - static_cast<double>(intPhase));

        if (intPhase >= tableSize) intPhase = tableSize - 1;

        const float* p = table + intPhase;
        return Interpolation::cubicHermiteInterpolate(p[-1], p[0], p[1], p[2], fracPhase);
    }

    /// @brief Branchless output sanitization (FR-028).
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
    float frequency_ = 440.0f;            ///< Fundamental frequency in Hz
    float distortion_ = 0.0f;             ///< DCW parameter [0, 1]
    PDWaveform waveform_ = PDWaveform::Saw; ///< Current waveform type
    float maxResonanceFactor_ = kDefaultMaxResonanceFactor; ///< Max resonance for resonant waveforms

    // Resources (regenerated on prepare())
    WavetableData cosineTable_;           ///< Mipmapped cosine wavetable
    WavetableOscillator osc_;             ///< Internal oscillator (for future use)
    PhaseAccumulator phaseAcc_;           ///< Phase tracking

    // Lifecycle state
    float sampleRate_ = 0.0f;             ///< Current sample rate
    bool phaseWrapped_ = false;           ///< True if last process() wrapped
    bool prepared_ = false;               ///< True after prepare() called
};

} // namespace DSP
} // namespace Krate
