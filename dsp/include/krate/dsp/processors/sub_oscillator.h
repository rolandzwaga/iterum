// ==============================================================================
// Layer 2: DSP Processor - Sub-Oscillator
// ==============================================================================
// Frequency-divided sub-oscillator that tracks a master oscillator via
// flip-flop division, replicating the classic analog sub-oscillator behavior
// found in Moog, Sequential, and Oberheim hardware synthesizers.
//
// Supports three waveforms (square with minBLEP, sine, triangle) at
// one-octave (divide-by-2) or two-octave (divide-by-4) depths, with an
// equal-power mix control for blending with the main oscillator output.
//
// Architecture note: The sub-oscillator does NOT own a PolyBlepOscillator.
// It receives masterPhaseWrapped (bool) and masterPhaseIncrement (float) as
// parameters to process(). The flip-flop toggle drives both the square
// waveform output and the sine/triangle phase resynchronization.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process/processMixed: noexcept, no alloc)
// - Principle III: Modern C++ (C++20, [[nodiscard]], constexpr, RAII)
// - Principle IX: Layer 2 (depends on Layer 0 + Layer 1 only)
// - Principle XII: Test-First Development
//
// Reference: specs/019-sub-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/minblep_table.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// SubOctave Enumeration (FR-001)
// =============================================================================

/// @brief Frequency division depth for the SubOscillator.
///
/// File-scope enum in Krate::DSP namespace, shared by downstream components.
enum class SubOctave : uint8_t {
    OneOctave = 0,   ///< Divide master frequency by 2 (one octave below)
    TwoOctaves = 1   ///< Divide master frequency by 4 (two octaves below)
};

// =============================================================================
// SubWaveform Enumeration (FR-002)
// =============================================================================

/// @brief Waveform type for the SubOscillator.
///
/// File-scope enum in Krate::DSP namespace, shared by downstream components.
enum class SubWaveform : uint8_t {
    Square = 0,    ///< Classic analog flip-flop output with minBLEP correction
    Sine = 1,      ///< Digital sine at sub frequency via phase accumulator
    Triangle = 2   ///< Digital triangle at sub frequency via phase accumulator
};

// =============================================================================
// SubOscillator Class (FR-003)
// =============================================================================

/// @brief Frequency-divided sub-oscillator tracking a master oscillator (Layer 2).
///
/// Implements frequency division using a flip-flop state machine, replicating
/// the classic analog sub-oscillator behavior of Moog, Sequential, and Oberheim
/// synthesizers. Supports square (flip-flop with minBLEP), sine, and triangle
/// waveforms at one-octave (divide-by-2) or two-octave (divide-by-4) depths.
///
/// @par Ownership Model
/// Constructor takes a `const MinBlepTable*` (caller owns lifetime).
/// Multiple SubOscillator instances can share one MinBlepTable (read-only
/// after prepare). Each instance maintains its own Residual buffer.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and processMixed() are fully real-time safe.
///
/// @par Usage
/// @code
/// MinBlepTable table;
/// table.prepare(64, 8);
///
/// PolyBlepOscillator master;
/// master.prepare(44100.0);
/// master.setFrequency(440.0f);
/// master.setWaveform(OscWaveform::Sawtooth);
///
/// SubOscillator sub(&table);
/// sub.prepare(44100.0);
/// sub.setOctave(SubOctave::OneOctave);
/// sub.setWaveform(SubWaveform::Square);
/// sub.setMix(0.5f);
///
/// for (int i = 0; i < numSamples; ++i) {
///     float mainOut = master.process();
///     bool wrapped = master.phaseWrapped();
///     float phaseInc = 440.0f / 44100.0f;
///     output[i] = sub.processMixed(mainOut, wrapped, phaseInc);
/// }
/// @endcode
class SubOscillator {
public:
    // =========================================================================
    // Constructor (FR-003)
    // =========================================================================

    /// @brief Construct with a pointer to a shared MinBlepTable.
    /// @param table Pointer to prepared MinBlepTable (caller owns lifetime).
    ///        May be nullptr; prepare() will validate before use.
    explicit SubOscillator(const MinBlepTable* table = nullptr) noexcept
        : table_(table) {
    }

    ~SubOscillator() = default;

    SubOscillator(const SubOscillator&) = default;
    SubOscillator& operator=(const SubOscillator&) = default;
    SubOscillator(SubOscillator&&) noexcept = default;
    SubOscillator& operator=(SubOscillator&&) noexcept = default;

    // =========================================================================
    // Lifecycle (FR-004, FR-005)
    // =========================================================================

    /// @brief Initialize for the given sample rate. NOT real-time safe.
    ///
    /// Initializes flip-flop states to false, phase accumulator to 0.0,
    /// and the minBLEP residual buffer. Sets prepared_ to false if the
    /// MinBlepTable pointer is nullptr, not prepared, or has length > 64.
    ///
    /// @param sampleRate Sample rate in Hz
    inline void prepare(double sampleRate) noexcept {
        // FR-004: Validate table pointer
        if (table_ == nullptr || !table_->isPrepared() || table_->length() > 64) {
            prepared_ = false;
            return;
        }

        sampleRate_ = static_cast<float>(sampleRate);

        // Initialize flip-flop states to false (FR-031)
        flipFlop1_ = false;
        flipFlop2_ = false;

        // Initialize phase accumulator
        subPhase_.reset();
        subPhase_.increment = 0.0;

        // Initialize master phase estimate
        masterPhaseEstimate_ = 0.0;

        // Initialize residual buffer
        residual_ = MinBlepTable::Residual(*table_);

        prepared_ = true;
    }

    /// @brief Reset state without changing configuration.
    ///
    /// Resets flip-flop states to false, sub phase to 0.0, clears residual.
    /// Preserves: octave, waveform, mix, sample rate.
    inline void reset() noexcept {
        flipFlop1_ = false;
        flipFlop2_ = false;
        subPhase_.reset();
        masterPhaseEstimate_ = 0.0;
        residual_.reset();
    }

    // =========================================================================
    // Parameter Setters (FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Select the frequency division mode.
    /// @param octave OneOctave (master/2) or TwoOctaves (master/4)
    inline void setOctave(SubOctave octave) noexcept {
        octave_ = octave;
    }

    /// @brief Select the sub-oscillator waveform type.
    /// @param waveform Square, Sine, or Triangle
    inline void setWaveform(SubWaveform waveform) noexcept {
        waveform_ = waveform;
    }

    /// @brief Set the dry/wet balance.
    /// @param mix 0.0 = main only, 1.0 = sub only. Clamped to [0, 1].
    ///        NaN/Inf ignored (previous value retained).
    inline void setMix(float mix) noexcept {
        // FR-008: NaN and Inf are ignored
        if (detail::isNaN(mix) || detail::isInf(mix)) {
            return;
        }
        // Clamp to [0.0, 1.0]
        mix_ = (mix < 0.0f) ? 0.0f : ((mix > 1.0f) ? 1.0f : mix);
        // Cache equal-power gains (FR-020, FR-021)
        equalPowerGains(mix_, mainGain_, subGain_);
    }

    // =========================================================================
    // Processing (FR-009, FR-010)
    // =========================================================================

    /// @brief Generate one sample of sub-oscillator output.
    ///
    /// @param masterPhaseWrapped true if the master oscillator's phase
    ///        wrapped (crossed 1.0) on this sample
    /// @param masterPhaseIncrement The master's instantaneous phase
    ///        increment (frequency / sampleRate) for this sample
    /// @return Sub-oscillator output sample. Sanitized to [-2.0, 2.0].
    ///         Returns 0.0 if not prepared.
    [[nodiscard]] inline float process(bool masterPhaseWrapped,
                                       float masterPhaseIncrement) noexcept {
        if (!prepared_) {
            return 0.0f;
        }

        const double masterInc = static_cast<double>(masterPhaseIncrement);

        // Track master phase internally for sub-sample offset computation
        masterPhaseEstimate_ += masterInc;

        // === Flip-Flop Division ===
        bool prevFlipFlop1 = flipFlop1_;
        bool outputFlipFlopChanged = false;
        bool outputFlipFlopRisingEdge = false;
        float subsampleOffset = 0.0f;

        if (masterPhaseWrapped) {
            // Compute sub-sample offset from master phase
            // After wrapping, masterPhaseEstimate_ is > 1.0 (pre-wrap value)
            // Wrap it and compute offset
            masterPhaseEstimate_ = wrapPhase(masterPhaseEstimate_);
            subsampleOffset = static_cast<float>(
                subsamplePhaseWrapOffset(masterPhaseEstimate_,
                                         masterInc));
            // Clamp offset to valid range
            if (subsampleOffset < 0.0f) subsampleOffset = 0.0f;
            if (subsampleOffset >= 1.0f) subsampleOffset = 1.0f - 1e-7f;

            // First-stage flip-flop toggles on every master wrap (FR-011)
            flipFlop1_ = !flipFlop1_;

            if (octave_ == SubOctave::OneOctave) {
                // OneOctave: output from flipFlop1
                outputFlipFlopChanged = true;
                outputFlipFlopRisingEdge = flipFlop1_; // true if now true
            } else {
                // TwoOctaves: second-stage flip-flop toggles on rising edge
                // of first-stage (FR-012)
                if (flipFlop1_ && !prevFlipFlop1) {
                    // Rising edge of flipFlop1
                    bool prevFlipFlop2 = flipFlop2_;
                    flipFlop2_ = !flipFlop2_;
                    outputFlipFlopChanged = true;
                    outputFlipFlopRisingEdge = flipFlop2_ && !prevFlipFlop2;
                }
            }
        }

        // Get the current output flip-flop state
        bool outputFlipFlop = (octave_ == SubOctave::OneOctave) ? flipFlop1_ : flipFlop2_;

        // === Phase resync for Sine/Triangle (FR-019) ===
        // Reset sub phase on rising edge of the output flip-flop
        if (outputFlipFlopChanged && outputFlipFlopRisingEdge) {
            subPhase_.phase = 0.0;
        }

        // === Waveform Generation ===
        float output = 0.0f;

        switch (waveform_) {
            case SubWaveform::Square: {
                // FR-013: Square from flip-flop state with minBLEP
                float rawSquare = outputFlipFlop ? 1.0f : -1.0f;

                // Apply minBLEP correction at toggle points
                if (outputFlipFlopChanged) {
                    // Amplitude of the step: +2 for false->true, -2 for true->false
                    float blepAmplitude = outputFlipFlop ? 2.0f : -2.0f;
                    residual_.addBlep(subsampleOffset, blepAmplitude);
                }

                output = rawSquare + residual_.consume();
                break;
            }

            case SubWaveform::Sine: {
                // FR-015, FR-016, FR-017: Sine from phase accumulator
                // Delta-phase tracking: sub increment = master increment / octave factor
                double octaveFactor = (octave_ == SubOctave::OneOctave) ? 2.0 : 4.0;
                subPhase_.increment = masterInc / octaveFactor;
                subPhase_.phase = wrapPhase(subPhase_.phase + subPhase_.increment);

                output = std::sin(static_cast<float>(kTwoPi) *
                                  static_cast<float>(subPhase_.phase));

                // Still consume residual (in case waveform was recently changed from Square)
                output += residual_.consume();
                break;
            }

            case SubWaveform::Triangle: {
                // FR-015, FR-016, FR-018: Triangle from phase accumulator
                double octaveFactor = (octave_ == SubOctave::OneOctave) ? 2.0 : 4.0;
                subPhase_.increment = masterInc / octaveFactor;
                subPhase_.phase = wrapPhase(subPhase_.phase + subPhase_.increment);

                float phase = static_cast<float>(subPhase_.phase);
                // FR-018: Piecewise linear triangle
                if (phase < 0.5f) {
                    output = 4.0f * phase - 1.0f;
                } else {
                    output = 3.0f - 4.0f * phase;
                }

                // Still consume residual
                output += residual_.consume();
                break;
            }

            default:
                output = residual_.consume();
                break;
        }

        // FR-029, FR-030: Sanitize output
        return sanitize(output);
    }

    /// @brief Generate one mixed sample (main + sub with equal-power crossfade).
    ///
    /// @param mainOutput The main oscillator's output for this sample
    /// @param masterPhaseWrapped true if the master's phase wrapped
    /// @param masterPhaseIncrement The master's phase increment
    /// @return Mixed output = mainOutput * mainGain + subOutput * subGain
    [[nodiscard]] inline float processMixed(float mainOutput,
                                            bool masterPhaseWrapped,
                                            float masterPhaseIncrement) noexcept {
        float subOutput = process(masterPhaseWrapped, masterPhaseIncrement);
        float mixed = mainOutput * mainGain_ + subOutput * subGain_;
        return sanitize(mixed);
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Branchless output sanitization (FR-029).
    [[nodiscard]] static inline float sanitize(float x) noexcept {
        const auto bits = detail::opaqueFloatBits(x);  // fast-math-immune read
        const bool isNan = ((bits & 0x7F800000u) == 0x7F800000u) &&
                           ((bits & 0x007FFFFFu) != 0);
        x = isNan ? 0.0f : x;
        x = (x < -2.0f) ? -2.0f : x;
        x = (x > 2.0f) ? 2.0f : x;
        return x;
    }

    // =========================================================================
    // Internal State
    // =========================================================================

    const MinBlepTable* table_ = nullptr;
    MinBlepTable::Residual residual_;
    PhaseAccumulator subPhase_;

    double masterPhaseEstimate_ = 0.0;

    float sampleRate_ = 0.0f;
    float mix_ = 0.0f;
    float mainGain_ = 1.0f;
    float subGain_ = 0.0f;

    bool flipFlop1_ = false;
    bool flipFlop2_ = false;

    SubOctave octave_ = SubOctave::OneOctave;
    SubWaveform waveform_ = SubWaveform::Square;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
