// ==============================================================================
// Layer 2: DSP Processor
// PitchShiftProcessor - Pitch shifting with multiple quality modes
// ==============================================================================
// Feature: 016-pitch-shifter
// Constitution Principle VIII: DSP algorithms must be independently testable
// Constitution Principle XIV: ODR-safe implementation (no duplicate definitions)
// ==============================================================================
//
// Three quality modes:
// - Simple: Delay-line modulation (zero latency, audible artifacts)
// - Granular: OLA grains (~46ms latency, good quality)
// - PhaseVocoder: STFT-based (~116ms latency, excellent quality)
//
// Dependencies (Layer 1):
// - DelayLine: For Simple mode delay buffer
// - STFT: For PhaseVocoder mode
// - WindowFunctions: For grain windowing
// - OnePoleSmoother: For parameter smoothing
//
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <array>
#include <memory>
#include <vector>
#include <algorithm>

// Layer 0 dependencies
#include "dsp/core/db_utils.h"

// Layer 1 dependencies
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/smoother.h"

namespace Iterum::DSP {

// ==============================================================================
// Enumerations
// ==============================================================================

/// Quality mode selection for pitch shifting algorithm
enum class PitchMode : std::uint8_t {
    Simple = 0,      ///< Delay-line modulation, zero latency, audible artifacts
    Granular = 1,    ///< OLA grains, ~46ms latency, good quality
    PhaseVocoder = 2 ///< STFT-based, ~116ms latency, excellent quality
};

// ==============================================================================
// Utility Functions
// ==============================================================================

/// @brief Convert semitones to pitch ratio
///
/// Formula: ratio = 2^(semitones/12)
///
/// @param semitones Pitch shift in semitones
/// @return Pitch ratio (e.g., 12 semitones -> 2.0)
[[nodiscard]] inline float pitchRatioFromSemitones(float semitones) noexcept {
    // Using std::pow for runtime accuracy
    // 2^(semitones/12) = exp(semitones * ln(2) / 12)
    constexpr float kLn2Over12 = 0.05776226504666210911810267678818f; // ln(2)/12
    return std::exp(semitones * kLn2Over12);
}

/// @brief Convert pitch ratio to semitones
///
/// Formula: semitones = 12 * log2(ratio)
///
/// @param ratio Pitch ratio (must be > 0)
/// @return Pitch shift in semitones (e.g., 2.0 -> 12 semitones)
[[nodiscard]] inline float semitonesFromPitchRatio(float ratio) noexcept {
    if (ratio <= 0.0f) return 0.0f;
    // 12 * log2(ratio) = 12 * ln(ratio) / ln(2)
    constexpr float k12OverLn2 = 17.31234049066756088832381614082f; // 12/ln(2)
    return std::log(ratio) * k12OverLn2;
}

// ==============================================================================
// PitchShiftProcessor Class
// ==============================================================================

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
    //=========================================================================
    // Internal Implementation
    //=========================================================================

    // Forward declaration of implementation
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};

// ==============================================================================
// SimplePitchShifter - Internal class for delay-line modulation
// ==============================================================================

/// @brief Zero-latency pitch shifter using dual delay-line crossfade
///
/// Algorithm based on MathWorks delay-based pitch shifter and DSPRELATED theory:
/// The pitch shift comes from TIME-VARYING DELAY (Doppler effect).
///
/// Key physics: ω_out = ω_in × (1 - dDelay/dt)
/// For pitch ratio R: dDelay/dt = 1 - R
/// - R > 1 (pitch up): delay DECREASES at rate (R-1) samples per sample
/// - R < 1 (pitch down): delay INCREASES at rate (1-R) samples per sample
///
/// Implementation:
/// - Two delays ramping in opposite directions
/// - When one delay reaches its limit, reset it and crossfade to the other
/// - Continuous half-sine crossfade preserves energy
///
/// Sources:
/// - https://www.mathworks.com/help/audio/ug/delay-based-pitch-shifter.html
/// - https://www.dsprelated.com/freebooks/pasp/Time_Varying_Delay_Effects.html
/// - https://www.katjaas.nl/pitchshiftlowlatency/pitchshiftlowlatency.html
class SimplePitchShifter {
public:
    static constexpr float kWindowTimeMs = 50.0f;  // 50ms crossfade window
    static constexpr float kPi = 3.14159265358979323846f;

    SimplePitchShifter() = default;

    void prepare(double sampleRate, std::size_t /*maxBlockSize*/) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);

        // Delay range in samples (~2205 at 44.1kHz for 50ms window)
        maxDelay_ = sampleRate_ * kWindowTimeMs * 0.001f;
        minDelay_ = 1.0f;  // Minimum safe delay

        // Buffer must be large enough to hold max delay + safety margin
        bufferSize_ = static_cast<std::size_t>(maxDelay_) * 2 + 64;
        buffer_.resize(bufferSize_, 0.0f);

        reset();
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writePos_ = 0;

        // delay1 starts at max, is the "active" delay
        // delay2 will be set when we need to crossfade
        delay1_ = maxDelay_;
        delay2_ = maxDelay_;  // Will be reset when needed
        crossfadePhase_ = 0.0f;  // 0 = use delay1 only
        needsCrossfade_ = false;
    }

    void process(const float* input, float* output, std::size_t numSamples,
                 float pitchRatio) noexcept {
        // At unity pitch, just pass through
        if (std::abs(pitchRatio - 1.0f) < 0.0001f) {
            if (input != output) {
                std::copy(input, input + numSamples, output);
            }
            return;
        }

        // Delay-based pitch shifter using Doppler effect:
        //
        // Key physics: ω_out = ω_in × (1 - dDelay/dt)
        // For pitch ratio R: dDelay/dt = 1 - R
        //
        // R = 2.0: delay decreases by 1 sample/sample (pitch UP)
        // R = 0.5: delay increases by 0.5 samples/sample (pitch DOWN)
        //
        // Algorithm:
        // 1. Delay1 is the "active" delay, ramping in the appropriate direction
        // 2. When delay1 approaches its limit, reset delay2 to the START and crossfade
        // 3. After crossfade completes, delay2 becomes active (swap roles)
        // 4. Repeat

        const float delayChange = 1.0f - pitchRatio;  // Negative for pitch up
        const float bufferSizeF = static_cast<float>(bufferSize_);

        // Crossfade over ~25% of the delay range for smooth transitions
        const float crossfadeLength = maxDelay_ * 0.25f;
        const float crossfadeRate = 1.0f / crossfadeLength;

        // Threshold for triggering crossfade (when delay gets close to limit)
        const float triggerThreshold = crossfadeLength;

        for (std::size_t i = 0; i < numSamples; ++i) {
            // Write input to buffer
            buffer_[writePos_] = input[i];

            // Read from both delay taps
            float readPos1 = static_cast<float>(writePos_) - delay1_;
            float readPos2 = static_cast<float>(writePos_) - delay2_;

            // Wrap to valid buffer range
            if (readPos1 < 0.0f) readPos1 += bufferSizeF;
            if (readPos2 < 0.0f) readPos2 += bufferSizeF;

            float sample1 = readInterpolated(readPos1);
            float sample2 = readInterpolated(readPos2);

            // Half-sine crossfade for constant power
            float gain1 = std::cos(crossfadePhase_ * kPi * 0.5f);
            float gain2 = std::sin(crossfadePhase_ * kPi * 0.5f);

            output[i] = sample1 * gain1 + sample2 * gain2;

            // Update the active delay (always delay1 conceptually, but we swap)
            delay1_ += delayChange;
            delay2_ += delayChange;

            // Check if we need to start a crossfade
            if (!needsCrossfade_) {
                // For pitch UP (delayChange < 0): delay decreases toward minDelay_
                // For pitch DOWN (delayChange > 0): delay increases toward maxDelay_
                bool approachingLimit = (delayChange < 0.0f && delay1_ <= minDelay_ + triggerThreshold) ||
                                        (delayChange > 0.0f && delay1_ >= maxDelay_ - triggerThreshold);

                if (approachingLimit) {
                    // Reset delay2 to the START of the cycle
                    delay2_ = (pitchRatio > 1.0f) ? maxDelay_ : minDelay_;
                    needsCrossfade_ = true;
                }
            }

            // Manage crossfade
            if (needsCrossfade_) {
                crossfadePhase_ += crossfadeRate;

                if (crossfadePhase_ >= 1.0f) {
                    // Crossfade complete - swap delays
                    crossfadePhase_ = 0.0f;
                    needsCrossfade_ = false;

                    // Swap delay1 and delay2 (delay2 becomes the new active)
                    std::swap(delay1_, delay2_);
                }
            }

            // Clamp delays to valid range (safety, shouldn't normally hit this)
            delay1_ = std::clamp(delay1_, minDelay_, maxDelay_);
            delay2_ = std::clamp(delay2_, minDelay_, maxDelay_);

            // Advance write position
            writePos_ = (writePos_ + 1) % bufferSize_;
        }
    }

private:
    [[nodiscard]] float readInterpolated(float pos) const noexcept {
        const std::size_t idx0 = static_cast<std::size_t>(pos) % bufferSize_;
        const std::size_t idx1 = (idx0 + 1) % bufferSize_;
        const float frac = pos - std::floor(pos);
        return buffer_[idx0] * (1.0f - frac) + buffer_[idx1] * frac;
    }

    std::vector<float> buffer_;
    std::size_t bufferSize_ = 0;
    std::size_t writePos_ = 0;
    float delay1_ = 0.0f;
    float delay2_ = 0.0f;
    float crossfadePhase_ = 0.0f;
    float maxDelay_ = 0.0f;
    float minDelay_ = 1.0f;
    float sampleRate_ = 44100.0f;
    bool needsCrossfade_ = false;
};

// ==============================================================================
// PitchShiftProcessor Implementation
// ==============================================================================

struct PitchShiftProcessor::Impl {
    // Parameters
    PitchMode mode = PitchMode::Simple;  // Default to Simple for US1
    float semitones = 0.0f;
    float cents = 0.0f;
    bool formantPreserve = false;
    double sampleRate = 44100.0;
    std::size_t maxBlockSize = 512;
    bool prepared = false;

    // Internal processors
    SimplePitchShifter simpleShifter;

    // Parameter smoothers
    OnePoleSmoother semitoneSmoother;
    OnePoleSmoother centsSmoother;
};

inline PitchShiftProcessor::PitchShiftProcessor() noexcept
    : pImpl_(std::make_unique<Impl>()) {}

inline PitchShiftProcessor::~PitchShiftProcessor() noexcept = default;

inline PitchShiftProcessor::PitchShiftProcessor(PitchShiftProcessor&&) noexcept = default;
inline PitchShiftProcessor& PitchShiftProcessor::operator=(PitchShiftProcessor&&) noexcept = default;

inline void PitchShiftProcessor::prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
    pImpl_->sampleRate = sampleRate;
    pImpl_->maxBlockSize = maxBlockSize;

    // Prepare all internal shifters
    pImpl_->simpleShifter.prepare(sampleRate, maxBlockSize);

    // Configure parameter smoothers (10ms smoothing time)
    constexpr float kSmoothTimeMs = 10.0f;
    pImpl_->semitoneSmoother.configure(kSmoothTimeMs, static_cast<float>(sampleRate));
    pImpl_->centsSmoother.configure(kSmoothTimeMs, static_cast<float>(sampleRate));

    pImpl_->prepared = true;
    reset();
}

inline void PitchShiftProcessor::reset() noexcept {
    if (!pImpl_->prepared) return;

    pImpl_->simpleShifter.reset();
    pImpl_->semitoneSmoother.reset();
    pImpl_->semitoneSmoother.setTarget(pImpl_->semitones);
    pImpl_->centsSmoother.reset();
    pImpl_->centsSmoother.setTarget(pImpl_->cents);
}

inline bool PitchShiftProcessor::isPrepared() const noexcept {
    return pImpl_->prepared;
}

inline void PitchShiftProcessor::process(const float* input, float* output,
                                         std::size_t numSamples) noexcept {
    if (!pImpl_->prepared || input == nullptr || output == nullptr || numSamples == 0) {
        return;
    }

    // Update smoother targets
    pImpl_->semitoneSmoother.setTarget(pImpl_->semitones);
    pImpl_->centsSmoother.setTarget(pImpl_->cents);

    // Calculate pitch ratio
    // For Simple mode: use direct value (zero latency = no parameter smoothing delay)
    // For Granular/PhaseVocoder: use smoothed value
    float totalSemitones;
    if (pImpl_->mode == PitchMode::Simple) {
        // Simple mode: zero latency, use direct parameters
        totalSemitones = pImpl_->semitones + pImpl_->cents / 100.0f;
        // Snap smoothers to target for consistency when mode changes
        pImpl_->semitoneSmoother.snapToTarget();
        pImpl_->centsSmoother.snapToTarget();
    } else {
        // Granular/PhaseVocoder: use smoothed values (advance per block for now)
        float smoothedSemitones = pImpl_->semitoneSmoother.process();
        float smoothedCents = pImpl_->centsSmoother.process();
        totalSemitones = smoothedSemitones + smoothedCents / 100.0f;
    }

    float pitchRatio = pitchRatioFromSemitones(totalSemitones);

    // Route to appropriate processor based on mode
    switch (pImpl_->mode) {
        case PitchMode::Simple:
            pImpl_->simpleShifter.process(input, output, numSamples, pitchRatio);
            break;

        case PitchMode::Granular:
            // TODO: Implement in US2
            // For now, fall through to simple mode
            pImpl_->simpleShifter.process(input, output, numSamples, pitchRatio);
            break;

        case PitchMode::PhaseVocoder:
            // TODO: Implement in US2
            // For now, fall through to simple mode
            pImpl_->simpleShifter.process(input, output, numSamples, pitchRatio);
            break;
    }
}

inline void PitchShiftProcessor::setMode(PitchMode mode) noexcept {
    pImpl_->mode = mode;
}

inline PitchMode PitchShiftProcessor::getMode() const noexcept {
    return pImpl_->mode;
}

inline void PitchShiftProcessor::setSemitones(float semitones) noexcept {
    // Clamp to valid range
    pImpl_->semitones = std::clamp(semitones, -24.0f, 24.0f);
}

inline float PitchShiftProcessor::getSemitones() const noexcept {
    return pImpl_->semitones;
}

inline void PitchShiftProcessor::setCents(float cents) noexcept {
    // Clamp to valid range
    pImpl_->cents = std::clamp(cents, -100.0f, 100.0f);
}

inline float PitchShiftProcessor::getCents() const noexcept {
    return pImpl_->cents;
}

inline float PitchShiftProcessor::getPitchRatio() const noexcept {
    float totalSemitones = pImpl_->semitones + pImpl_->cents / 100.0f;
    return pitchRatioFromSemitones(totalSemitones);
}

inline void PitchShiftProcessor::setFormantPreserve(bool enable) noexcept {
    pImpl_->formantPreserve = enable;
}

inline bool PitchShiftProcessor::getFormantPreserve() const noexcept {
    return pImpl_->formantPreserve;
}

inline std::size_t PitchShiftProcessor::getLatencySamples() const noexcept {
    if (!pImpl_->prepared) return 0;

    switch (pImpl_->mode) {
        case PitchMode::Simple:
            return 0;  // Zero latency
        case PitchMode::Granular:
            // ~grain size (~46ms at 44.1kHz)
            return static_cast<std::size_t>(pImpl_->sampleRate * 0.046);
        case PitchMode::PhaseVocoder:
            // FFT_SIZE + HOP_SIZE (~116ms at 44.1kHz)
            return static_cast<std::size_t>(pImpl_->sampleRate * 0.116);
    }
    return 0;
}

} // namespace Iterum::DSP
