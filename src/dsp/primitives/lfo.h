// ==============================================================================
// Layer 1: DSP Primitive - LFO (Low Frequency Oscillator)
// ==============================================================================
// Wavetable-based low frequency oscillator for modulation.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
//
// Reference: specs/003-lfo/spec.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

inline constexpr float kMinFrequency = 0.01f;   // Minimum LFO frequency (Hz)
inline constexpr float kMaxFrequency = 20.0f;   // Maximum LFO frequency (Hz)
inline constexpr float kMinBPM = 1.0f;          // Minimum tempo (BPM)
inline constexpr float kMaxBPM = 999.0f;        // Maximum tempo (BPM)
inline constexpr size_t kTableSize = 2048;      // Wavetable size (power of 2)
inline constexpr size_t kTableMask = kTableSize - 1;  // Bitmask for wrapping

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Available LFO waveform shapes.
enum class Waveform : uint8_t {
    Sine = 0,        ///< Smooth sinusoidal wave (default)
    Triangle,        ///< Linear ramp up and down (0->1->-1->0)
    Sawtooth,        ///< Linear ramp from -1 to +1, instant reset
    Square,          ///< Binary alternation +1 / -1
    SampleHold,      ///< Random value held for each cycle
    SmoothRandom     ///< Interpolated random values
};

/// @brief Musical note divisions for tempo sync.
enum class NoteValue : uint8_t {
    Whole = 0,       ///< 1/1 note (4 beats)
    Half,            ///< 1/2 note (2 beats)
    Quarter,         ///< 1/4 note (1 beat) - default
    Eighth,          ///< 1/8 note (0.5 beats)
    Sixteenth,       ///< 1/16 note (0.25 beats)
    ThirtySecond     ///< 1/32 note (0.125 beats)
};

/// @brief Timing modifiers for note values.
enum class NoteModifier : uint8_t {
    None = 0,        ///< Normal duration (default)
    Dotted,          ///< 1.5x duration
    Triplet          ///< 2/3x duration
};

// =============================================================================
// LFO Class
// =============================================================================

/// @brief Wavetable-based low frequency oscillator for modulation.
class LFO {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    LFO() noexcept = default;
    ~LFO() = default;

    // Non-copyable, movable
    LFO(const LFO&) = delete;
    LFO& operator=(const LFO&) = delete;
    LFO(LFO&&) noexcept = default;
    LFO& operator=(LFO&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Prepare the LFO for processing.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        generateWavetables();
        updatePhaseIncrement();
        reset();
    }

    /// @brief Reset the LFO to initial state.
    void reset() noexcept {
        phase_ = 0.0;
        // Initialize random state with a deterministic seed
        randomState_ = 12345u;
        currentRandom_ = nextRandomValue();
        previousRandom_ = currentRandom_;
        targetRandom_ = nextRandomValue();
    }

    // =========================================================================
    // Processing (real-time safe)
    // =========================================================================

    /// @brief Generate one sample of LFO output.
    [[nodiscard]] float process() noexcept {
        // Calculate effective phase including offset
        double effectivePhase = phase_ + phaseOffsetNorm_;
        if (effectivePhase >= 1.0) {
            effectivePhase -= 1.0;
        }

        float output = 0.0f;

        switch (waveform_) {
            case Waveform::Sine:
            case Waveform::Triangle:
            case Waveform::Sawtooth:
            case Waveform::Square:
                output = readWavetable(static_cast<size_t>(waveform_), effectivePhase);
                break;

            case Waveform::SampleHold:
                output = currentRandom_;
                break;

            case Waveform::SmoothRandom:
                // Linear interpolation between previous and target
                output = previousRandom_ +
                         static_cast<float>(effectivePhase) * (targetRandom_ - previousRandom_);
                break;
        }

        // Advance phase
        double oldPhase = phase_;
        phase_ += phaseIncrement_;

        // Check for phase wrap (cycle complete)
        if (phase_ >= 1.0) {
            phase_ -= 1.0;

            // Update random state at cycle boundary
            if (waveform_ == Waveform::SampleHold) {
                currentRandom_ = nextRandomValue();
            } else if (waveform_ == Waveform::SmoothRandom) {
                previousRandom_ = targetRandom_;
                targetRandom_ = nextRandomValue();
            }
        }

        return output;
    }

    /// @brief Generate a block of LFO output.
    void processBlock(float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set the LFO waveform.
    void setWaveform(Waveform waveform) noexcept {
        waveform_ = waveform;
    }

    /// @brief Set the LFO frequency in Hz.
    void setFrequency(float hz) noexcept {
        frequency_ = std::clamp(hz, kMinFrequency, kMaxFrequency);
        if (!tempoSync_) {
            updatePhaseIncrement();
        }
    }

    /// @brief Set the phase offset.
    void setPhaseOffset(float degrees) noexcept {
        // Normalize to [0, 360) range
        float wrapped = std::fmod(degrees, 360.0f);
        if (wrapped < 0.0f) {
            wrapped += 360.0f;
        }
        phaseOffsetDeg_ = wrapped;
        phaseOffsetNorm_ = wrapped / 360.0;
    }

    /// @brief Enable or disable tempo sync mode.
    void setTempoSync(bool enabled) noexcept {
        tempoSync_ = enabled;
        if (tempoSync_) {
            updateTempoSyncFrequency();
        }
        updatePhaseIncrement();
    }

    /// @brief Set the tempo for sync mode.
    void setTempo(float bpm) noexcept {
        bpm_ = std::clamp(bpm, kMinBPM, kMaxBPM);
        if (tempoSync_) {
            updateTempoSyncFrequency();
            updatePhaseIncrement();
        }
    }

    /// @brief Set the note value for tempo sync.
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept {
        noteValue_ = value;
        noteModifier_ = modifier;
        if (tempoSync_) {
            updateTempoSyncFrequency();
            updatePhaseIncrement();
        }
    }

    // =========================================================================
    // Control
    // =========================================================================

    /// @brief Retrigger the LFO phase.
    void retrigger() noexcept {
        if (retriggerEnabled_) {
            phase_ = 0.0;
            // Re-initialize random state for consistent retrigger behavior
            if (waveform_ == Waveform::SampleHold || waveform_ == Waveform::SmoothRandom) {
                currentRandom_ = nextRandomValue();
                previousRandom_ = currentRandom_;
                targetRandom_ = nextRandomValue();
            }
        }
    }

    /// @brief Enable or disable retrigger functionality.
    void setRetriggerEnabled(bool enabled) noexcept {
        retriggerEnabled_ = enabled;
    }

    // =========================================================================
    // Query Methods
    // =========================================================================

    [[nodiscard]] Waveform waveform() const noexcept {
        return waveform_;
    }

    [[nodiscard]] float frequency() const noexcept {
        if (tempoSync_) {
            return tempoSyncFrequency_;
        }
        return frequency_;
    }

    [[nodiscard]] float phaseOffset() const noexcept {
        return phaseOffsetDeg_;
    }

    [[nodiscard]] bool tempoSyncEnabled() const noexcept {
        return tempoSync_;
    }

    [[nodiscard]] bool retriggerEnabled() const noexcept {
        return retriggerEnabled_;
    }

    [[nodiscard]] double sampleRate() const noexcept {
        return sampleRate_;
    }

private:
    // =========================================================================
    // Wavetable Generation
    // =========================================================================

    void generateWavetables() noexcept {
        // Resize wavetables
        for (auto& table : wavetables_) {
            table.resize(kTableSize);
        }

        constexpr double twoPi = 2.0 * std::numbers::pi;

        // Generate Sine wavetable
        for (size_t i = 0; i < kTableSize; ++i) {
            double phase = static_cast<double>(i) / static_cast<double>(kTableSize);
            wavetables_[static_cast<size_t>(Waveform::Sine)][i] =
                static_cast<float>(std::sin(twoPi * phase));
        }

        // Generate Triangle wavetable (0->1->0->-1->0 but starting at 0)
        // Shape: rise from 0 to 1 in first quarter, fall to -1 by 3/4, rise back to 0
        for (size_t i = 0; i < kTableSize; ++i) {
            double phase = static_cast<double>(i) / static_cast<double>(kTableSize);
            float value;
            if (phase < 0.25) {
                // 0 to 1
                value = static_cast<float>(phase * 4.0);
            } else if (phase < 0.75) {
                // 1 to -1
                value = static_cast<float>(2.0 - phase * 4.0);
            } else {
                // -1 to 0
                value = static_cast<float>(phase * 4.0 - 4.0);
            }
            wavetables_[static_cast<size_t>(Waveform::Triangle)][i] = value;
        }

        // Generate Sawtooth wavetable (-1 to +1)
        for (size_t i = 0; i < kTableSize; ++i) {
            double phase = static_cast<double>(i) / static_cast<double>(kTableSize);
            wavetables_[static_cast<size_t>(Waveform::Sawtooth)][i] =
                static_cast<float>(2.0 * phase - 1.0);
        }

        // Generate Square wavetable (+1 for first half, -1 for second half)
        for (size_t i = 0; i < kTableSize; ++i) {
            double phase = static_cast<double>(i) / static_cast<double>(kTableSize);
            wavetables_[static_cast<size_t>(Waveform::Square)][i] =
                (phase < 0.5) ? 1.0f : -1.0f;
        }
    }

    // =========================================================================
    // Wavetable Reading with Linear Interpolation
    // =========================================================================

    [[nodiscard]] float readWavetable(size_t tableIndex, double phase) const noexcept {
        const auto& table = wavetables_[tableIndex];

        // Scale phase to table index
        double scaledPhase = phase * static_cast<double>(kTableSize);
        size_t index0 = static_cast<size_t>(scaledPhase);
        size_t index1 = (index0 + 1) & kTableMask;  // Wrap using bitmask
        float frac = static_cast<float>(scaledPhase - static_cast<double>(index0));

        // Linear interpolation
        return table[index0] + frac * (table[index1] - table[index0]);
    }

    // =========================================================================
    // Random Number Generation (LCG - Linear Congruential Generator)
    // =========================================================================

    [[nodiscard]] float nextRandomValue() noexcept {
        // LCG parameters (same as MINSTD)
        randomState_ = randomState_ * 48271u % 2147483647u;
        // Convert to [-1, 1] range
        return static_cast<float>(randomState_) / 1073741823.5f - 1.0f;
    }

    // =========================================================================
    // Tempo Sync Calculations
    // =========================================================================

    void updateTempoSyncFrequency() noexcept {
        // Base beats per note value
        static constexpr float kBeatsPerNote[] = {
            4.0f,    // Whole
            2.0f,    // Half
            1.0f,    // Quarter
            0.5f,    // Eighth
            0.25f,   // Sixteenth
            0.125f   // ThirtySecond
        };

        float beatsPerNote = kBeatsPerNote[static_cast<size_t>(noteValue_)];

        // Apply modifier
        switch (noteModifier_) {
            case NoteModifier::None:
                break;
            case NoteModifier::Dotted:
                beatsPerNote *= 1.5f;
                break;
            case NoteModifier::Triplet:
                beatsPerNote *= (2.0f / 3.0f);
                break;
        }

        // Calculate frequency: BPM / (60 * beatsPerNote)
        // = beatsPerSecond / beatsPerNote
        float beatsPerSecond = bpm_ / 60.0f;
        tempoSyncFrequency_ = beatsPerSecond / beatsPerNote;

        // Clamp to valid range
        tempoSyncFrequency_ = std::clamp(tempoSyncFrequency_, kMinFrequency, kMaxFrequency);
    }

    void updatePhaseIncrement() noexcept {
        float freq = tempoSync_ ? tempoSyncFrequency_ : frequency_;
        phaseIncrement_ = static_cast<double>(freq) / sampleRate_;
    }

    // =========================================================================
    // State Variables
    // =========================================================================

    // Sample rate
    double sampleRate_ = 44100.0;

    // Phase state
    double phase_ = 0.0;           // Current phase [0, 1)
    double phaseIncrement_ = 0.0;  // Phase advance per sample
    double phaseOffsetNorm_ = 0.0; // Phase offset [0, 1)
    float phaseOffsetDeg_ = 0.0f;  // Phase offset in degrees (for query)

    // Frequency
    float frequency_ = 1.0f;       // Free-running frequency (Hz)
    float tempoSyncFrequency_ = 2.0f; // Tempo-synced frequency (Hz)
    float bpm_ = 120.0f;           // Tempo (BPM)

    // Waveform
    Waveform waveform_ = Waveform::Sine;

    // Tempo sync
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;
    bool tempoSync_ = false;

    // Retrigger
    bool retriggerEnabled_ = true;

    // Random state (for S&H and SmoothRandom)
    uint32_t randomState_ = 12345u;
    float currentRandom_ = 0.0f;   // Current S&H value
    float previousRandom_ = 0.0f;  // SmoothRandom: previous target
    float targetRandom_ = 0.0f;    // SmoothRandom: current target

    // Wavetables (Sine, Triangle, Sawtooth, Square)
    std::array<std::vector<float>, 4> wavetables_;
};

} // namespace DSP
} // namespace Iterum
