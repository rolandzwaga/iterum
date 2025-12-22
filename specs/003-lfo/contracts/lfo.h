// ==============================================================================
// Layer 1: DSP Primitive - LFO (API Contract)
// ==============================================================================
// This file defines the PUBLIC API contract for the LFO class.
// Implementation details are intentionally omitted.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

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
///
/// Provides multiple waveforms (sine, triangle, saw, square, sample & hold,
/// smoothed random), tempo sync with musical note values, adjustable phase
/// offset, and retrigger capability.
///
/// @note All process methods are noexcept and allocation-free for real-time safety.
/// @note Memory is allocated only in prepare(), which must be called before processing.
///
/// @example Basic usage:
/// @code
/// LFO lfo;
/// lfo.prepare(44100.0);
/// lfo.setFrequency(2.0f);  // 2 Hz
/// lfo.setWaveform(Waveform::Sine);
///
/// // In audio callback:
/// float modulation = lfo.process();  // Returns [-1, +1]
/// @endcode
class LFO {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. Creates an uninitialized LFO.
    /// @note prepare() must be called before use.
    LFO() noexcept = default;

    /// @brief Destructor.
    ~LFO() = default;

    // Non-copyable, movable
    LFO(const LFO&) = delete;
    LFO& operator=(const LFO&) = delete;
    LFO(LFO&&) noexcept = default;
    LFO& operator=(LFO&&) noexcept = default;

    // =========================================================================
    // Initialization (call before audio processing)
    // =========================================================================

    /// @brief Prepare the LFO for processing.
    ///
    /// Generates wavetables and initializes internal state.
    ///
    /// @param sampleRate The sample rate in Hz (e.g., 44100.0, 48000.0, 96000.0)
    ///
    /// @note This method allocates memory and must be called before processing.
    /// @note Calling prepare() again reconfigures the LFO and resets phase.
    void prepare(double sampleRate) noexcept;

    /// @brief Reset the LFO to initial state.
    ///
    /// Resets phase to zero and clears random state. Does not deallocate memory.
    void reset() noexcept;

    // =========================================================================
    // Processing (real-time safe)
    // =========================================================================

    /// @brief Generate one sample of LFO output.
    ///
    /// @return The current LFO value in range [-1.0, +1.0].
    ///
    /// @note O(1) time complexity, no allocations.
    [[nodiscard]] float process() noexcept;

    /// @brief Generate a block of LFO output.
    ///
    /// @param output Pointer to output buffer (must have numSamples capacity).
    /// @param numSamples Number of samples to generate.
    ///
    /// @note O(n) time complexity where n = numSamples, no allocations.
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set the LFO waveform.
    /// @param waveform The waveform to use.
    void setWaveform(Waveform waveform) noexcept;

    /// @brief Set the LFO frequency in Hz.
    ///
    /// @param hz Frequency in Hz (clamped to [0.01, 20.0]).
    ///
    /// @note Ignored when tempo sync is enabled; use setTempo() and setNoteValue() instead.
    void setFrequency(float hz) noexcept;

    /// @brief Set the phase offset.
    ///
    /// @param degrees Phase offset in degrees [0, 360). Values outside this range are wrapped.
    void setPhaseOffset(float degrees) noexcept;

    /// @brief Enable or disable tempo sync mode.
    ///
    /// @param enabled True to sync to tempo, false for free-running Hz mode.
    void setTempoSync(bool enabled) noexcept;

    /// @brief Set the tempo for sync mode.
    ///
    /// @param bpm Tempo in beats per minute (clamped to [1, 999]).
    ///
    /// @note Only affects frequency when tempo sync is enabled.
    void setTempo(float bpm) noexcept;

    /// @brief Set the note value for tempo sync.
    ///
    /// @param value The note division (1/1 through 1/32).
    /// @param modifier Timing modifier (None, Dotted, or Triplet).
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;

    // =========================================================================
    // Control
    // =========================================================================

    /// @brief Retrigger the LFO phase.
    ///
    /// Resets phase to the configured phase offset. Use for note-on sync.
    ///
    /// @note Has no effect if retrigger is disabled (free-running mode).
    void retrigger() noexcept;

    /// @brief Enable or disable retrigger functionality.
    ///
    /// @param enabled True to allow retrigger(), false for free-running.
    void setRetriggerEnabled(bool enabled) noexcept;

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get the current waveform.
    [[nodiscard]] Waveform waveform() const noexcept;

    /// @brief Get the current frequency in Hz.
    ///
    /// @return The effective frequency (may differ from set value if tempo synced).
    [[nodiscard]] float frequency() const noexcept;

    /// @brief Get the current phase offset in degrees.
    [[nodiscard]] float phaseOffset() const noexcept;

    /// @brief Check if tempo sync is enabled.
    [[nodiscard]] bool tempoSyncEnabled() const noexcept;

    /// @brief Check if retrigger is enabled.
    [[nodiscard]] bool retriggerEnabled() const noexcept;

    /// @brief Get the current sample rate.
    [[nodiscard]] double sampleRate() const noexcept;

private:
    // Implementation details omitted from API contract
    // See data-model.md for internal structure
};

} // namespace DSP
} // namespace Iterum
