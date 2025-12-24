// Layer 2: DSP Processor
// API Contract - Pitch Shift Processor
// Feature: 016-pitch-shifter
//
// This file defines the public API contract for PitchShiftProcessor.
// Implementation must match this interface exactly.
// Internal classes (SimplePitchShifter, etc.) are implementation details.

#pragma once

#include <cstddef>
#include <cstdint>

namespace Iterum::DSP {

/// Quality mode selection for pitch shifting algorithm
enum class PitchMode : std::uint8_t {
    Simple = 0,      ///< Delay-line modulation, zero latency, audible artifacts
    Granular = 1,    ///< OLA grains, ~46ms latency, good quality
    PhaseVocoder = 2 ///< STFT-based, ~116ms latency, excellent quality
};

/// @brief Layer 2 pitch shift processor with multiple quality modes
///
/// Shifts audio pitch by semitones without changing playback duration.
/// Supports three quality modes with different latency/quality trade-offs:
/// - Simple: Zero latency using delay-line modulation (audible artifacts)
/// - Granular: Low latency (~46ms) using overlap-add grains
/// - PhaseVocoder: High quality using STFT with phase locking (~116ms latency)
///
/// Formant preservation is available in Granular and PhaseVocoder modes
/// to prevent the "chipmunk" effect when shifting vocals.
///
/// Thread Safety:
/// - Parameter setters are thread-safe (atomic writes)
/// - process() must be called from a single thread
/// - Mode/formant changes are safe between process() calls
///
/// Real-Time Safety:
/// - No memory allocation in process()
/// - No blocking operations
/// - Pre-allocate all buffers in prepare()
///
/// Usage:
/// @code
/// PitchShiftProcessor shifter;
/// shifter.prepare(44100.0, 512);
/// shifter.setMode(PitchMode::Granular);
/// shifter.setSemitones(7.0f);  // Perfect fifth up
///
/// // In audio callback:
/// shifter.process(input, output, numSamples);
/// @endcode
class PitchShiftProcessor {
public:
    //=========================================================================
    // Construction
    //=========================================================================

    /// @brief Construct pitch shift processor with default settings
    ///
    /// Default state:
    /// - Mode: Granular
    /// - Semitones: 0
    /// - Cents: 0
    /// - Formant preservation: disabled
    ///
    /// Must call prepare() before process().
    PitchShiftProcessor() noexcept;

    /// @brief Destructor
    ~PitchShiftProcessor() noexcept;

    // Non-copyable (internal state is complex)
    PitchShiftProcessor(const PitchShiftProcessor&) = delete;
    PitchShiftProcessor& operator=(const PitchShiftProcessor&) = delete;

    // Movable
    PitchShiftProcessor(PitchShiftProcessor&&) noexcept;
    PitchShiftProcessor& operator=(PitchShiftProcessor&&) noexcept;

    //=========================================================================
    // Lifecycle
    //=========================================================================

    /// @brief Prepare processor for given sample rate and block size
    ///
    /// Allocates all internal buffers. Must be called before process().
    /// Can be called multiple times to change sample rate.
    /// Implicitly calls reset().
    ///
    /// @param sampleRate Sample rate in Hz [44100, 192000]
    /// @param maxBlockSize Maximum samples per process() call [1, 8192]
    ///
    /// @pre sampleRate >= 44100.0 && sampleRate <= 192000.0
    /// @pre maxBlockSize >= 1 && maxBlockSize <= 8192
    /// @post isPrepared() == true
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state to initial conditions
    ///
    /// Clears delay buffers, grain states, phase accumulators.
    /// Does not deallocate memory or change parameters.
    /// Safe to call from audio thread.
    ///
    /// @pre isPrepared() == true (otherwise no-op)
    void reset() noexcept;

    /// @brief Check if processor is ready for processing
    /// @return true if prepare() has been called successfully
    [[nodiscard]] bool isPrepared() const noexcept;

    //=========================================================================
    // Processing
    //=========================================================================

    /// @brief Process audio through pitch shifter
    ///
    /// Applies pitch shift to input samples and writes to output.
    /// Supports in-place processing (input == output).
    ///
    /// @param input Pointer to input samples
    /// @param output Pointer to output samples (can equal input)
    /// @param numSamples Number of samples to process [1, maxBlockSize]
    ///
    /// @pre isPrepared() == true
    /// @pre input != nullptr
    /// @pre output != nullptr
    /// @pre numSamples <= maxBlockSize passed to prepare()
    ///
    /// @note Real-time safe: no allocations, no blocking
    void process(const float* input, float* output, std::size_t numSamples) noexcept;

    //=========================================================================
    // Parameters - Mode
    //=========================================================================

    /// @brief Set quality mode
    ///
    /// Changing mode during playback causes a brief crossfade.
    /// Latency reporting changes immediately.
    ///
    /// @param mode Quality mode (Simple, Granular, or PhaseVocoder)
    void setMode(PitchMode mode) noexcept;

    /// @brief Get current quality mode
    /// @return Current PitchMode
    [[nodiscard]] PitchMode getMode() const noexcept;

    //=========================================================================
    // Parameters - Pitch
    //=========================================================================

    /// @brief Set pitch shift in semitones
    ///
    /// Positive values shift pitch up, negative values shift down.
    /// Combined with cents for total shift.
    /// Changes are smoothed to prevent clicks.
    ///
    /// @param semitones Pitch shift in semitones [-24, +24]
    ///
    /// @note Values outside range are clamped
    void setSemitones(float semitones) noexcept;

    /// @brief Get pitch shift in semitones
    /// @return Current semitone setting [-24, +24]
    [[nodiscard]] float getSemitones() const noexcept;

    /// @brief Set fine pitch adjustment in cents
    ///
    /// 100 cents = 1 semitone.
    /// Added to semitones for total pitch shift.
    /// Changes are smoothed to prevent clicks.
    ///
    /// @param cents Fine pitch adjustment [-100, +100]
    ///
    /// @note Values outside range are clamped
    void setCents(float cents) noexcept;

    /// @brief Get fine pitch adjustment in cents
    /// @return Current cents setting [-100, +100]
    [[nodiscard]] float getCents() const noexcept;

    /// @brief Get current pitch ratio
    ///
    /// Computed as: 2^((semitones + cents/100) / 12)
    ///
    /// @return Current pitch ratio (e.g., 2.0 for octave up, 0.5 for octave down)
    [[nodiscard]] float getPitchRatio() const noexcept;

    //=========================================================================
    // Parameters - Formant Preservation
    //=========================================================================

    /// @brief Enable or disable formant preservation
    ///
    /// When enabled, attempts to preserve vocal formant frequencies
    /// during pitch shifting to avoid "chipmunk" effect.
    ///
    /// Only effective in Granular and PhaseVocoder modes.
    /// Simple mode ignores this setting.
    ///
    /// @param enable true to enable, false to disable
    void setFormantPreserve(bool enable) noexcept;

    /// @brief Get formant preservation state
    /// @return true if formant preservation is enabled
    [[nodiscard]] bool getFormantPreserve() const noexcept;

    //=========================================================================
    // Latency
    //=========================================================================

    /// @brief Get processing latency in samples
    ///
    /// Returns the algorithmic latency for the current mode:
    /// - Simple: 0 samples
    /// - Granular: ~grain_size samples (~2048 at 44.1kHz)
    /// - PhaseVocoder: FFT_SIZE + HOP_SIZE samples (~5120 at 44.1kHz)
    ///
    /// @return Latency in samples for current mode
    ///
    /// @pre isPrepared() == true
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

private:
    // Implementation details hidden
    // See data-model.md for internal structure
    struct Impl;
    Impl* pImpl_;
};

//=============================================================================
// Utility Functions
//=============================================================================

/// @brief Convert semitones to pitch ratio
///
/// Formula: ratio = 2^(semitones/12)
///
/// @param semitones Pitch shift in semitones
/// @return Pitch ratio (e.g., 12 semitones -> 2.0)
[[nodiscard]] constexpr float pitchRatioFromSemitones(float semitones) noexcept {
    // Using constexpr-safe approximation
    // For exact: return std::pow(2.0f, semitones / 12.0f);
    // At compile time, use Taylor series or table
    return 1.0f; // Placeholder - see implementation for constexpr version
}

/// @brief Convert pitch ratio to semitones
///
/// Formula: semitones = 12 * log2(ratio)
///
/// @param ratio Pitch ratio (must be > 0)
/// @return Pitch shift in semitones (e.g., 2.0 -> 12 semitones)
[[nodiscard]] constexpr float semitonesFromPitchRatio(float ratio) noexcept {
    // Using constexpr-safe approximation
    // For exact: return 12.0f * std::log2(ratio);
    return 0.0f; // Placeholder - see implementation for constexpr version
}

} // namespace Iterum::DSP

//=============================================================================
// Test Requirements (from spec.md)
//=============================================================================
//
// Unit Tests Required:
//
// US1 - Basic Pitch Shifting:
// - T001: 440Hz sine + 12 semitones = 880Hz output
// - T002: 440Hz sine - 12 semitones = 220Hz output
// - T003: 0 semitones = unity pass-through
// - T004: Pitch change during playback is smooth (no clicks)
//
// US2 - Quality Mode Selection:
// - T005: Simple mode latency == 0 samples
// - T006: Granular mode latency < 2048 samples
// - T007: PhaseVocoder mode has highest pitch accuracy
// - T008: Mode change during processing is click-free
//
// US3 - Fine Pitch Control:
// - T009: 0 semitones + 50 cents = 452.9Hz from 440Hz
// - T010: +1 semitone - 50 cents = +0.5 semitones total
// - T011: Cents changes are smooth
//
// US4 - Formant Preservation:
// - T012: Formant peaks remain within 10% when shifting up
// - T013: Formant shift occurs when preservation disabled
// - T014: Formant toggle transition is smooth
//
// US5 - Feedback Path Integration:
// - T015: 80% feedback loop decays naturally
// - T016: Multiple iterations maintain pitch accuracy
// - T017: No DC offset after extended feedback
//
// US6 - Real-Time Parameter Automation:
// - T018: Sweep -24 to +24 is smooth
// - T019: Rapid parameter changes cause no clicks
// - T020: Parameter reaches target within 50ms
//
// Success Criteria Tests:
// - SC-001: Pitch accuracy (±10 cents Simple, ±5 cents others)
// - SC-002: Simple latency == 0
// - SC-003: Granular latency < 2048 samples
// - SC-004: PhaseVocoder latency < 8192 samples
// - SC-005: CPU usage per mode
// - SC-006: No clicks during parameter sweep
// - SC-007: Formant preservation 10% tolerance
// - SC-008: Stable after 1000 feedback iterations
//=============================================================================
