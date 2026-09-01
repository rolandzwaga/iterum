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

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/core/phase_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace Krate {
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
inline constexpr float kCrossfadeTimeMs = 10.0f;  // Waveform transition time (ms)

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

// NoteValue and NoteModifier enums are now in dsp/core/note_value.h (Layer 0)

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
        updatePhaseIncrement();
        updateCrossfadeIncrement();
        // Rate-derived like the increments above, and only otherwise recomputed
        // in setFadeInTime() -- so re-preparing at a new sample rate left the
        // fade-in running for the wrong duration.
        updateFadeInIncrement();
        reset();
    }

    /// @brief Reset the LFO to initial state.
    void reset() noexcept {
        phaseAcc_.reset();
        // Initialize random state with a deterministic seed
        randomState_ = 12345u;
        currentRandom_ = nextRandomValue();
        previousRandom_ = currentRandom_;
        targetRandom_ = nextRandomValue();
        // Reset crossfade state
        crossfadeProgress_ = 1.0f;  // Not crossfading
        hasProcessed_ = false;      // Allow immediate waveform changes after reset
        // Reset fade-in (start at full gain, retrigger will reset to 0)
        fadeInGain_ = 1.0f;
    }

    // =========================================================================
    // Processing (real-time safe)
    // =========================================================================

    /// @brief Generate one sample of LFO output.
    [[nodiscard]] float process() noexcept {
        hasProcessed_ = true;  // Mark that processing has started

        // Calculate effective phase including offset
        double effectivePhase = wrapPhase(phaseAcc_.phase + phaseOffsetNorm_);

        // Apply symmetry/skew to wavetable-based waveforms
        double lookupPhase = effectivePhase;
        if (symmetry_ != 0.5f &&
            waveform_ != Waveform::SampleHold &&
            waveform_ != Waveform::SmoothRandom) {
            lookupPhase = static_cast<double>(
                applySymmetry(static_cast<float>(effectivePhase), symmetry_));
        }

        float output = 0.0f;

        // Get current waveform value
        float newValue = getWaveformValue(waveform_, lookupPhase, false);

        // Handle crossfading between waveforms
        if (crossfadeProgress_ < 1.0f) {
            // Linear crossfade from captured value to new waveform
            output = crossfadeFromValue_ + crossfadeProgress_ * (newValue - crossfadeFromValue_);

            // Advance crossfade
            crossfadeProgress_ += crossfadeIncrement_;
            if (crossfadeProgress_ >= 1.0f) {
                crossfadeProgress_ = 1.0f;  // Crossfade complete
            }
        } else {
            output = newValue;
        }

        // Apply quantization
        if (quantizeSteps_ >= 2) {
            float steps = static_cast<float>(quantizeSteps_);
            output = std::round(output * steps) / steps;
        }

        // Apply fade-in envelope
        if (fadeInGain_ < 1.0f) {
            output *= fadeInGain_;
            fadeInGain_ = std::min(1.0f, fadeInGain_ + fadeInIncrement_);
        }

        // Advance phase
        bool wrapped = phaseAcc_.advance();

        // Check for phase wrap (cycle complete)
        if (wrapped) {
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
        if (waveform != waveform_) {
            // Only crossfade if audio processing has started
            // During initial setup, switch immediately
            if (hasProcessed_) {
                // Calculate current effective phase
                double effectivePhase = wrapPhase(phaseAcc_.phase + phaseOffsetNorm_);

                // Capture the current output value to crossfade from
                // This handles both normal transitions and mid-crossfade transitions
                if (crossfadeProgress_ < 1.0f) {
                    // Already crossfading - capture the current blended value
                    float targetValue = getWaveformValue(waveform_, effectivePhase);
                    crossfadeFromValue_ = crossfadeFromValue_ +
                        crossfadeProgress_ * (targetValue - crossfadeFromValue_);
                } else {
                    // Not crossfading - capture current waveform value
                    crossfadeFromValue_ = getWaveformValue(waveform_, effectivePhase);
                }

                // Start new crossfade
                previousWaveform_ = waveform_;  // For reference only
                crossfadeProgress_ = 0.0f;
            }
            waveform_ = waveform;
        }
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
    /// @brief Current fade-in multiplier, 0 at retrigger ramping to 1.
    [[nodiscard]] float getFadeInGain() const noexcept { return fadeInGain_; }

    void retrigger() noexcept {
        if (retriggerEnabled_) {
            phaseAcc_.reset();
            // Re-initialize random state for consistent retrigger behavior
            if (waveform_ == Waveform::SampleHold || waveform_ == Waveform::SmoothRandom) {
                currentRandom_ = nextRandomValue();
                previousRandom_ = currentRandom_;
                targetRandom_ = nextRandomValue();
            }
            // Reset fade-in envelope
            if (fadeInTimeMs_ > 0.0f) {
                fadeInGain_ = 0.0f;
            }
        }
    }

    /// @brief Enable or disable retrigger functionality.
    void setRetriggerEnabled(bool enabled) noexcept {
        retriggerEnabled_ = enabled;
    }

    /// @brief Set the fade-in time after retrigger.
    /// @param ms Time in milliseconds for LFO depth to ramp from 0 to full (0 = disabled)
    void setFadeInTime(float ms) noexcept {
        fadeInTimeMs_ = std::max(0.0f, ms);
        updateFadeInIncrement();
    }

    /// @brief Set the symmetry/skew of the LFO waveform.
    /// @param value 0.0-1.0 where 0.5 = no skew (centered). Affects Sine/Tri/Saw/Square.
    void setSymmetry(float value) noexcept {
        symmetry_ = std::clamp(value, 0.001f, 0.999f);
    }

    /// @brief Set the number of quantization steps for the LFO output.
    /// @param steps 0 = off (continuous), 2-16 = quantize to discrete levels
    void setQuantizeSteps(int steps) noexcept {
        quantizeSteps_ = (steps < 2) ? 0 : std::min(steps, 16);
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

    [[nodiscard]] float freeRunningFrequency() const noexcept {
        return frequency_;
    }

    [[nodiscard]] NoteValue noteValue() const noexcept {
        return noteValue_;
    }

    [[nodiscard]] NoteModifier noteModifier() const noexcept {
        return noteModifier_;
    }

    /// @brief Get the current fade-in time.
    [[nodiscard]] float fadeInTime() const noexcept { return fadeInTimeMs_; }

    /// @brief Get the current symmetry value.
    [[nodiscard]] float symmetry() const noexcept { return symmetry_; }

    /// @brief Get the current quantize steps (0 = off).
    [[nodiscard]] int quantizeSteps() const noexcept { return quantizeSteps_; }

private:
    // =========================================================================
    // Wavetable Generation
    // =========================================================================

    /// @brief The four wavetables, shared by every LFO instance.
    ///
    /// The tables are a pure function of kTableSize -- no sample rate, no
    /// per-instance parameter -- so every LFO used to hold a byte-identical
    /// private copy. Sharing one immutable set makes prepare() allocation-free
    /// (each instance previously heap-allocated four 2048-float vectors, i.e.
    /// 32 KiB and four allocations per LFO) without changing a single sample.
    using WavetableSet = std::array<std::array<float, kTableSize>, 4>;

    [[nodiscard]] static WavetableSet generateWavetables() noexcept {
        WavetableSet wavetables{};

        constexpr double twoPi = 2.0 * std::numbers::pi;

        // Generate Sine wavetable
        for (size_t i = 0; i < kTableSize; ++i) {
            double phase = static_cast<double>(i) / static_cast<double>(kTableSize);
            wavetables[static_cast<size_t>(Waveform::Sine)][i] =
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
            wavetables[static_cast<size_t>(Waveform::Triangle)][i] = value;
        }

        // Generate Sawtooth wavetable (-1 to +1)
        for (size_t i = 0; i < kTableSize; ++i) {
            double phase = static_cast<double>(i) / static_cast<double>(kTableSize);
            wavetables[static_cast<size_t>(Waveform::Sawtooth)][i] =
                static_cast<float>(2.0 * phase - 1.0);
        }

        // Generate Square wavetable (+1 for first half, -1 for second half)
        for (size_t i = 0; i < kTableSize; ++i) {
            double phase = static_cast<double>(i) / static_cast<double>(kTableSize);
            wavetables[static_cast<size_t>(Waveform::Square)][i] =
                (phase < 0.5) ? 1.0f : -1.0f;
        }

        return wavetables;
    }

    /// @brief One lazily-built, immutable table set for the whole process.
    /// Built on the first LFO construction (never on the audio thread: an LFO
    /// is a prepare-time object), then read-only forever after.
    [[nodiscard]] static const WavetableSet& sharedWavetables() noexcept {
        static const WavetableSet kTables = generateWavetables();
        return kTables;
    }

    // =========================================================================
    // Wavetable Reading with Linear Interpolation
    // =========================================================================

    [[nodiscard]] float readWavetable(size_t tableIndex, double phase) const noexcept {
        const auto& table = (*wavetables_)[tableIndex];

        // Scale phase to table index
        double scaledPhase = phase * static_cast<double>(kTableSize);
        size_t index0 = static_cast<size_t>(scaledPhase);
        size_t index1 = (index0 + 1) & kTableMask;  // Wrap using bitmask
        float frac = static_cast<float>(scaledPhase - static_cast<double>(index0));

        // Linear interpolation
        return table[index0] + frac * (table[index1] - table[index0]);
    }

    // =========================================================================
    // Waveform Value Helper
    // =========================================================================

    [[nodiscard]] float getWaveformValue(Waveform waveform, double effectivePhase,
                                          bool /*unused*/ = false) const noexcept {
        switch (waveform) {
            case Waveform::Sine:
            case Waveform::Triangle:
            case Waveform::Sawtooth:
            case Waveform::Square:
                return readWavetable(static_cast<size_t>(waveform), effectivePhase);

            case Waveform::SampleHold:
                return currentRandom_;

            case Waveform::SmoothRandom:
                return previousRandom_ +
                       static_cast<float>(effectivePhase) * (targetRandom_ - previousRandom_);
        }
        return 0.0f;  // Should never reach here
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
        // Use Layer 0 getBeatsForNote() helper function
        float beatsPerNote = getBeatsForNote(noteValue_, noteModifier_);

        // Calculate frequency: BPM / (60 * beatsPerNote)
        // = beatsPerSecond / beatsPerNote
        float beatsPerSecond = bpm_ / 60.0f;
        tempoSyncFrequency_ = beatsPerSecond / beatsPerNote;

        // Clamp to valid range
        tempoSyncFrequency_ = std::clamp(tempoSyncFrequency_, kMinFrequency, kMaxFrequency);
    }

    void updatePhaseIncrement() noexcept {
        float freq = tempoSync_ ? tempoSyncFrequency_ : frequency_;
        phaseAcc_.increment = calculatePhaseIncrement(freq, static_cast<float>(sampleRate_));
    }

    void updateCrossfadeIncrement() noexcept {
        // Calculate increment to complete crossfade in kCrossfadeTimeMs
        float crossfadeSamples = static_cast<float>(sampleRate_) * kCrossfadeTimeMs * 0.001f;
        crossfadeIncrement_ = 1.0f / crossfadeSamples;
    }

    /// @brief Update the fade-in increment based on current sample rate and time.
    void updateFadeInIncrement() noexcept {
        if (fadeInTimeMs_ <= 0.0f || sampleRate_ <= 0.0) {
            fadeInIncrement_ = 1.0f;  // instant (no fade)
        } else {
            float fadeInSamples = static_cast<float>(sampleRate_) * fadeInTimeMs_ / 1000.0f;
            fadeInIncrement_ = 1.0f / fadeInSamples;
        }
    }

    /// @brief Apply symmetry/skew warping to phase.
    /// @param phase Raw phase in [0, 1)
    /// @return Warped phase in [0, 1)
    [[nodiscard]] static float applySymmetry(float phase, float symmetry) noexcept {
        if (phase < symmetry) {
            return 0.5f * (phase / symmetry);
        } else {
            return 0.5f + 0.5f * ((phase - symmetry) / (1.0f - symmetry));
        }
    }

    // =========================================================================
    // State Variables
    // =========================================================================

    // Sample rate
    double sampleRate_ = 44100.0;

    // Phase state
    PhaseAccumulator phaseAcc_;        // Centralized phase management
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

    // Wavetables (Sine, Triangle, Sawtooth, Square) -- shared, never owned.
    // Cached as a pointer rather than fetched through sharedWavetables() in
    // readWavetable() so the per-sample read costs no magic-static guard check.
    // It points at a function-local static, so it can never dangle, and the
    // defaulted move operations copy it correctly.
    const WavetableSet* wavetables_ = &sharedWavetables();

    // Crossfade state (for smooth waveform transitions)
    Waveform previousWaveform_ = Waveform::Sine;  // Waveform we were crossfading from (for reference)
    float crossfadeProgress_ = 1.0f;    // 0.0 = old value, 1.0 = new waveform (complete)
    float crossfadeIncrement_ = 0.0f;   // Progress per sample
    float crossfadeFromValue_ = 0.0f;   // Captured output value at start of crossfade
    bool hasProcessed_ = false;         // True after first process() call; controls crossfade behavior

    // Fade-in state
    float fadeInTimeMs_ = 0.0f;       ///< Fade-in time in ms (0 = disabled)
    float fadeInGain_ = 1.0f;         ///< Current fade-in multiplier (0→1 ramp)
    float fadeInIncrement_ = 1.0f;    ///< Per-sample increment for fade-in ramp

    // Symmetry/skew
    float symmetry_ = 0.5f;          ///< Waveform symmetry (0.001-0.999, 0.5 = centered)

    // Quantize
    int quantizeSteps_ = 0;           ///< 0 = off, 2-16 = number of discrete steps
};

} // namespace DSP
} // namespace Krate
