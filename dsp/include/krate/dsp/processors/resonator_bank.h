// ==============================================================================
// Layer 2: DSP Processor - Resonator Bank
// ==============================================================================
// Bank of tuned resonant bandpass filters for physical modeling applications.
// Supports harmonic, inharmonic, and custom tuning modes with per-resonator
// control of frequency, decay, gain, and Q.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (constexpr, RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layers 0-1)
// - Principle X: DSP Constraints (sample-accurate processing)
// - Principle XII: Test-First Development
//
// Reference: specs/083-resonator-bank/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Maximum number of resonators in the bank
inline constexpr size_t kMaxResonators = 16;

/// Minimum resonator frequency in Hz
inline constexpr float kMinResonatorFrequency = 20.0f;

/// Maximum resonator frequency ratio (relative to sample rate)
inline constexpr float kMaxResonatorFrequencyRatio = 0.45f;

/// Minimum Q value for resonators
inline constexpr float kMinResonatorQ = 0.1f;

/// Maximum Q value for resonators (higher than Biquad default for physical modeling)
inline constexpr float kMaxResonatorQ = 100.0f;

/// Minimum decay time in seconds
inline constexpr float kMinDecayTime = 0.001f;

/// Maximum decay time in seconds
inline constexpr float kMaxDecayTime = 30.0f;

/// Default decay time in seconds
inline constexpr float kDefaultDecayTime = 1.0f;

/// Default Q value
inline constexpr float kDefaultResonatorQ = 10.0f;

/// Default gain in dB
inline constexpr float kDefaultGainDb = 0.0f;

/// Parameter smoothing time in milliseconds
inline constexpr float kResonatorSmoothingTimeMs = 20.0f;

/// Spectral tilt reference frequency in Hz
inline constexpr float kTiltReferenceFrequency = 1000.0f;

/// Minimum spectral tilt in dB/octave
inline constexpr float kMinSpectralTilt = -12.0f;

/// Maximum spectral tilt in dB/octave
inline constexpr float kMaxSpectralTilt = 12.0f;

/// Natural log of 1000 for RT60-to-Q conversion
inline constexpr float kLn1000 = 6.907755278982137f;

// =============================================================================
// Utility Functions
// =============================================================================

/// @brief Convert RT60 decay time to filter Q factor.
/// @param frequency Center frequency in Hz
/// @param rt60Seconds Decay time (time to decay by 60dB) in seconds
/// @return Q factor for resonant filter
/// @note Formula: Q = (pi * frequency * RT60) / ln(1000)
[[nodiscard]] inline constexpr float rt60ToQ(float frequency, float rt60Seconds) noexcept {
    if (rt60Seconds <= 0.0f || frequency <= 0.0f) {
        return kMinResonatorQ;
    }
    const float q = (kPi * frequency * rt60Seconds) / kLn1000;
    return std::clamp(q, kMinResonatorQ, kMaxResonatorQ);
}

/// @brief Calculate inharmonic partial frequency.
/// @param fundamental Fundamental frequency in Hz
/// @param partial Partial number (1 = fundamental, 2 = first overtone, etc.)
/// @param inharmonicity Inharmonicity coefficient B (0 = harmonic, higher = stretched)
/// @return Frequency of the partial in Hz
/// @note Formula: f_n = f_0 * n * sqrt(1 + B * n^2)
[[nodiscard]] inline float calculateInharmonicFrequency(
    float fundamental,
    int partial,
    float inharmonicity
) noexcept {
    const float n = static_cast<float>(partial);
    const float stretch = std::sqrt(1.0f + inharmonicity * n * n);
    return fundamental * n * stretch;
}

/// @brief Calculate spectral tilt gain for a given frequency.
/// @param frequency Resonator frequency in Hz
/// @param tiltDbPerOctave Tilt amount (positive = boost highs, negative = cut highs)
/// @return Linear gain multiplier
[[nodiscard]] inline float calculateTiltGain(float frequency, float tiltDbPerOctave) noexcept {
    if (tiltDbPerOctave == 0.0f || frequency <= 0.0f) {
        return 1.0f;
    }
    const float octaves = std::log2(frequency / kTiltReferenceFrequency);
    return dbToGain(tiltDbPerOctave * octaves);
}

// =============================================================================
// Tuning Mode Enumeration
// =============================================================================

/// @brief Tuning modes for the resonator bank.
enum class TuningMode : uint8_t {
    Harmonic,    ///< Integer multiples of fundamental: f, 2f, 3f, 4f...
    Inharmonic,  ///< Stretched partials: f_n = f * n * sqrt(1 + B*n^2)
    Custom       ///< User-specified frequencies
};

// =============================================================================
// ResonatorBank Class
// =============================================================================

/// @brief Bank of tuned resonant bandpass filters for physical modeling.
///
/// Provides 16 parallel bandpass resonators that can model marimba bars, bells,
/// strings, or arbitrary tunings. Each resonator has independent control of
/// frequency, decay time (RT60), gain, and Q factor.
///
/// @par Global Controls
/// - **Damping**: Scales all resonator decays (0 = full decay, 1 = instant silence)
/// - **Exciter Mix**: Blends dry input with resonant output (0 = wet only, 1 = dry only)
/// - **Spectral Tilt**: Per-resonator high frequency rolloff in dB/octave
///
/// @par Tuning Modes
/// - **Harmonic**: Integer multiples of fundamental (strings, flutes)
/// - **Inharmonic**: Stretched partials via stiff-string formula (bells, bars)
/// - **Custom**: User-specified frequencies for experimental tunings
///
/// @par Example Usage
/// @code
/// ResonatorBank bank;
/// bank.prepare(44100.0);
/// bank.setHarmonicSeries(440.0f, 8);  // A4 with 8 partials
/// bank.setDamping(0.2f);               // Light damping
///
/// // Process audio
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = bank.process(input[i]);
/// }
///
/// // Percussive trigger
/// bank.trigger(0.8f);  // Strike with 80% velocity
/// @endcode
class ResonatorBank {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    ResonatorBank() noexcept = default;

    /// @brief Initialize the resonator bank.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Configure smoothers with 20ms smoothing time
        const float sampleRateF = static_cast<float>(sampleRate);
        dampingSmoother_.configure(kResonatorSmoothingTimeMs, sampleRateF);
        exciterMixSmoother_.configure(kResonatorSmoothingTimeMs, sampleRateF);
        spectralTiltSmoother_.configure(kResonatorSmoothingTimeMs, sampleRateF);

        // Snap smoothers to initial values
        dampingSmoother_.snapTo(damping_);
        exciterMixSmoother_.snapTo(exciterMix_);
        spectralTiltSmoother_.snapTo(spectralTilt_);

        // Initialize all filters to default state
        for (size_t i = 0; i < kMaxResonators; ++i) {
            frequencies_[i] = 440.0f;
            decays_[i] = kDefaultDecayTime;
            gains_[i] = 1.0f;  // 0dB in linear
            qValues_[i] = kDefaultResonatorQ;
            enabled_[i] = false;
            filters_[i].reset();
        }

        prepared_ = true;
    }

    /// @brief Reset all filter states and parameters to defaults.
    /// @note User must reconfigure tuning after calling reset().
    void reset() noexcept {
        // 1. Clear all filter states
        for (auto& filter : filters_) {
            filter.reset();
        }

        // 2. Reset smoother states
        dampingSmoother_.reset();
        exciterMixSmoother_.reset();
        spectralTiltSmoother_.reset();

        // 3. Reset per-resonator parameters to defaults
        for (size_t i = 0; i < kMaxResonators; ++i) {
            frequencies_[i] = 440.0f;  // A4
            decays_[i] = kDefaultDecayTime;
            gains_[i] = 1.0f;
            qValues_[i] = kDefaultResonatorQ;
            enabled_[i] = false;
        }

        // 4. Reset global parameters to defaults
        damping_ = 0.0f;
        exciterMix_ = 0.0f;
        spectralTilt_ = 0.0f;

        // 5. Reset tuning state
        tuningMode_ = TuningMode::Custom;
        numActiveResonators_ = 0;

        // 6. Clear trigger state
        triggerPending_ = false;
        triggerVelocity_ = 0.0f;
    }

    // =========================================================================
    // Tuning Configuration
    // =========================================================================

    /// @brief Configure resonators as harmonic series.
    /// @param fundamentalHz Fundamental frequency in Hz
    /// @param numPartials Number of partials to create (1-16)
    /// @note Frequencies: f, 2f, 3f, 4f, ... up to numPartials
    void setHarmonicSeries(float fundamentalHz, int numPartials) noexcept {
        const int count = std::clamp(numPartials, 1, static_cast<int>(kMaxResonators));

        for (int i = 0; i < count; ++i) {
            const float freq = fundamentalHz * static_cast<float>(i + 1);
            frequencies_[static_cast<size_t>(i)] = clampFrequency(freq);
            enabled_[static_cast<size_t>(i)] = true;
            updateFilterCoefficients(static_cast<size_t>(i));
        }

        // Disable remaining resonators
        for (size_t i = static_cast<size_t>(count); i < kMaxResonators; ++i) {
            enabled_[i] = false;
        }

        tuningMode_ = TuningMode::Harmonic;
        updateActiveCount();
    }

    /// @brief Configure resonators as inharmonic series.
    /// @param baseHz Base frequency in Hz
    /// @param inharmonicity Inharmonicity coefficient B (0 = harmonic, higher = stretched)
    /// @note Formula: f_n = f * n * sqrt(1 + B * n^2)
    /// @note Uses all 16 resonators
    void setInharmonicSeries(float baseHz, float inharmonicity) noexcept {
        for (size_t i = 0; i < kMaxResonators; ++i) {
            const float freq = calculateInharmonicFrequency(
                baseHz, static_cast<int>(i + 1), inharmonicity);
            frequencies_[i] = clampFrequency(freq);
            enabled_[i] = true;
            updateFilterCoefficients(i);
        }

        tuningMode_ = TuningMode::Inharmonic;
        updateActiveCount();
    }

    /// @brief Configure resonators with custom frequencies.
    /// @param frequencies Array of frequencies in Hz
    /// @param count Number of frequencies (excess beyond 16 ignored)
    void setCustomFrequencies(const float* frequencies, size_t count) noexcept {
        const size_t usedCount = std::min(count, kMaxResonators);

        for (size_t i = 0; i < usedCount; ++i) {
            frequencies_[i] = clampFrequency(frequencies[i]);
            enabled_[i] = true;
            updateFilterCoefficients(i);
        }

        // Disable remaining resonators
        for (size_t i = usedCount; i < kMaxResonators; ++i) {
            enabled_[i] = false;
        }

        tuningMode_ = TuningMode::Custom;
        updateActiveCount();
    }

    /// @brief Get the current tuning mode.
    /// @return Current TuningMode
    [[nodiscard]] TuningMode getTuningMode() const noexcept { return tuningMode_; }

    /// @brief Get the number of active resonators.
    /// @return Number of enabled resonators (0-16)
    [[nodiscard]] size_t getNumActiveResonators() const noexcept { return numActiveResonators_; }

    // =========================================================================
    // Per-Resonator Control
    // =========================================================================

    /// @brief Set frequency for a specific resonator.
    /// @param index Resonator index (0-15)
    /// @param hz Frequency in Hz (clamped to valid range)
    void setFrequency(size_t index, float hz) noexcept {
        if (index >= kMaxResonators) return;
        frequencies_[index] = clampFrequency(hz);
        // Vorago Phase 2 (FR-099): match setDecay below so a drifting frequency no
        // longer silently changes the effective RT60 - Q is frequency-dependent.
        qValues_[index] = rt60ToQ(frequencies_[index], decays_[index]);
        updateFilterCoefficients(index);
    }

    /// @brief Get frequency of a specific resonator.
    /// @param index Resonator index (0-15)
    /// @return Frequency in Hz, or 0 if index invalid
    [[nodiscard]] float getFrequency(size_t index) const noexcept {
        if (index >= kMaxResonators) return 0.0f;
        return frequencies_[index];
    }

    /// @brief Set decay time (RT60) for a specific resonator.
    /// @param index Resonator index (0-15)
    /// @param seconds Decay time in seconds (clamped to [0.001, 30])
    void setDecay(size_t index, float seconds) noexcept {
        if (index >= kMaxResonators) return;
        decays_[index] = std::clamp(seconds, kMinDecayTime, kMaxDecayTime);
        // Decay affects Q - recalculate
        qValues_[index] = rt60ToQ(frequencies_[index], decays_[index]);
        updateFilterCoefficients(index);
    }

    /// @brief Get decay time of a specific resonator.
    /// @param index Resonator index (0-15)
    /// @return Decay time in seconds, or 0 if index invalid
    [[nodiscard]] float getDecay(size_t index) const noexcept {
        if (index >= kMaxResonators) return 0.0f;
        return decays_[index];
    }

    /// @brief Set gain for a specific resonator.
    /// @param index Resonator index (0-15)
    /// @param dB Gain in decibels
    void setGain(size_t index, float dB) noexcept {
        if (index >= kMaxResonators) return;
        gainsDb_[index] = dB;
        gains_[index] = dbToGain(dB);
    }

    /// @brief Get gain of a specific resonator in dB.
    /// @param index Resonator index (0-15)
    /// @return Gain in dB, or -144 if index invalid
    [[nodiscard]] float getGain(size_t index) const noexcept {
        if (index >= kMaxResonators) return -144.0f;
        return gainsDb_[index];
    }

    /// @brief Set Q factor for a specific resonator.
    /// @param index Resonator index (0-15)
    /// @param q Q factor (clamped to [0.1, 100])
    void setQ(size_t index, float q) noexcept {
        if (index >= kMaxResonators) return;
        qValues_[index] = std::clamp(q, kMinResonatorQ, kMaxResonatorQ);
        updateFilterCoefficients(index);
    }

    /// @brief Get Q factor of a specific resonator.
    /// @param index Resonator index (0-15)
    /// @return Q factor, or 0 if index invalid
    [[nodiscard]] float getQ(size_t index) const noexcept {
        if (index >= kMaxResonators) return 0.0f;
        return qValues_[index];
    }

    /// @brief Enable or disable a specific resonator.
    /// @param index Resonator index (0-15)
    /// @param enabled True to enable, false to disable
    void setEnabled(size_t index, bool enabled) noexcept {
        if (index >= kMaxResonators) return;
        enabled_[index] = enabled;
        updateActiveCount();
    }

    /// @brief Check if a specific resonator is enabled.
    /// @param index Resonator index (0-15)
    /// @return True if enabled, false if disabled or index invalid
    [[nodiscard]] bool isEnabled(size_t index) const noexcept {
        if (index >= kMaxResonators) return false;
        return enabled_[index];
    }

    // =========================================================================
    // Global Controls
    // =========================================================================

    /// @brief Set global damping.
    /// @param amount Damping amount (0 = full decay, 1 = instant silence)
    void setDamping(float amount) noexcept {
        damping_ = std::clamp(amount, 0.0f, 1.0f);
        dampingSmoother_.setTarget(damping_);
    }

    /// @brief Get current damping amount.
    /// @return Damping amount (0-1)
    [[nodiscard]] float getDamping() const noexcept { return damping_; }

    /// @brief Set exciter mix (dry/wet blend).
    /// @param amount Mix amount (0 = wet only, 1 = dry only)
    void setExciterMix(float amount) noexcept {
        exciterMix_ = std::clamp(amount, 0.0f, 1.0f);
        exciterMixSmoother_.setTarget(exciterMix_);
    }

    /// @brief Get current exciter mix.
    /// @return Mix amount (0-1)
    [[nodiscard]] float getExciterMix() const noexcept { return exciterMix_; }

    /// @brief Set spectral tilt.
    /// @param dBPerOctave Tilt in dB/octave (positive = boost highs, negative = cut highs)
    void setSpectralTilt(float dBPerOctave) noexcept {
        spectralTilt_ = std::clamp(dBPerOctave, kMinSpectralTilt, kMaxSpectralTilt);
        spectralTiltSmoother_.setTarget(spectralTilt_);
    }

    /// @brief Get current spectral tilt.
    /// @return Tilt in dB/octave
    [[nodiscard]] float getSpectralTilt() const noexcept { return spectralTilt_; }

    // =========================================================================
    // Trigger
    // =========================================================================

    /// @brief Trigger percussive excitation of all active resonators.
    /// @param velocity Excitation strength (0.0-1.0, default 1.0)
    void trigger(float velocity = 1.0f) noexcept {
        triggerPending_ = true;
        triggerVelocity_ = std::clamp(velocity, 0.0f, 1.0f);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Processed output sample
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) return input;

        // Get smoothed global parameters
        const float currentDamping = dampingSmoother_.process();
        const float currentMix = exciterMixSmoother_.process();
        const float currentTilt = spectralTiltSmoother_.process();

        // Handle trigger
        float excitation = input;
        if (triggerPending_) {
            excitation += triggerVelocity_;
            triggerPending_ = false;
        }

        // Process through all enabled resonators
        float wetSum = 0.0f;
        for (size_t i = 0; i < kMaxResonators; ++i) {
            if (!enabled_[i]) continue;

            // Calculate effective Q based on damping
            // Damping=1 means very low Q (instant silence), damping=0 means full Q
            const float dampingScale = 1.0f - currentDamping * 0.99f;  // Keep some Q even at max damping
            const float effectiveQ = qValues_[i] * dampingScale;

            // Update filter if Q changed significantly (via damping)
            // For real-time safety, we apply damping as a gain reduction instead
            // to avoid coefficient recalculation every sample
            float filterOutput = filters_[i].process(excitation);

            // Apply damping as output reduction (approximation for real-time safety)
            filterOutput *= dampingScale;

            // Apply per-resonator gain
            filterOutput *= gains_[i];

            // Apply spectral tilt
            const float tiltGain = calculateTiltGain(frequencies_[i], currentTilt);
            filterOutput *= tiltGain;

            wetSum += filterOutput;
        }

        // Apply exciter mix: output = dry * mix + wet * (1 - mix)
        const float output = input * currentMix + wetSum * (1.0f - currentMix);

        return output;
    }

    /// @brief Process a block of samples in-place.
    /// @param buffer Sample buffer (modified in place)
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // State Query
    // =========================================================================

    /// @brief Check if the resonator bank is prepared.
    /// @return True if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// Clamp frequency to valid range for current sample rate
    [[nodiscard]] float clampFrequency(float hz) const noexcept {
        const float maxFreq = static_cast<float>(sampleRate_) * kMaxResonatorFrequencyRatio;
        return std::clamp(hz, kMinResonatorFrequency, maxFreq);
    }

    /// Update filter coefficients for a specific resonator
    /// @note Coefficients are computed here instead of via Biquad::configure() because
    ///       BiquadCoefficients::calculate() clamps Q to biquad.h's kMaxQ = 30 (biquad.h:53,
    ///       applied by detail::clampQ at :673). This bank documents kMaxResonatorQ = 100
    ///       (:51, "higher than Biquad default for physical modeling") and derives Q from the
    ///       configured RT60 via rt60ToQ (:92), so that clamp silently capped every decay
    ///       longer than kLn1000 * kMaxQ / (pi * f) seconds - e.g. a resonator configured for
    ///       1 s at 200 Hz (Q = 90.96) actually rang for 0.33 s, and setQ() above 30 did
    ///       nothing at all. The math below is the same RBJ constant-0-dB-peak bandpass, in
    ///       the same operation order, as the FilterType::Bandpass case of
    ///       BiquadCoefficients::calculate (biquad.h:658-665, normalisation at :767-775);
    ///       the only difference is the Q range it admits, so results are unchanged for the
    ///       Q <= 30 configurations that were already realizable.
    void updateFilterCoefficients(size_t index) noexcept {
        if (index >= kMaxResonators) return;

        const float sampleRateF = static_cast<float>(sampleRate_);
        if (!(sampleRateF > 0.0f)) {
            // Match calculate()'s invalid-sample-rate behaviour: bypass coefficients.
            filters_[index].setCoefficients(BiquadCoefficients{});
            return;
        }

        // frequencies_[] is already clamped to [kMinResonatorFrequency, 0.45 * fs] by
        // clampFrequency(), which is strictly inside the Biquad frequency range.
        const float q = std::clamp(qValues_[index], kMinResonatorQ, kMaxResonatorQ);
        const float omega = kTwoPi * frequencies_[index] / sampleRateF;
        const float sinOmega = std::sin(omega);
        const float cosOmega = std::cos(omega);
        const float alpha = sinOmega / (2.0f * q);

        const float b0 = alpha;
        const float b2 = -alpha;
        const float a0 = 1.0f + alpha;
        const float a1 = -2.0f * cosOmega;
        const float a2 = 1.0f - alpha;

        BiquadCoefficients coeffs;
        const float invA0 = 1.0f / a0;
        coeffs.b0 = b0 * invA0;
        coeffs.b1 = 0.0f;  // Bandpass has no b1 term
        coeffs.b2 = b2 * invA0;
        coeffs.a1 = a1 * invA0;
        coeffs.a2 = a2 * invA0;
        filters_[index].setCoefficients(coeffs);
    }

    /// Recalculate active resonator count
    void updateActiveCount() noexcept {
        numActiveResonators_ = 0;
        for (size_t i = 0; i < kMaxResonators; ++i) {
            if (enabled_[i]) {
                ++numActiveResonators_;
            }
        }
    }

    // =========================================================================
    // Data Members
    // =========================================================================

    // Filter bank
    std::array<Biquad, kMaxResonators> filters_{};

    // Per-resonator parameters
    std::array<float, kMaxResonators> frequencies_{};
    std::array<float, kMaxResonators> decays_{};
    std::array<float, kMaxResonators> gains_{};
    std::array<float, kMaxResonators> gainsDb_{};  // Store dB values for getGain()
    std::array<float, kMaxResonators> qValues_{};
    std::array<bool, kMaxResonators> enabled_{};

    // Parameter smoothers
    OnePoleSmoother dampingSmoother_;
    OnePoleSmoother exciterMixSmoother_;
    OnePoleSmoother spectralTiltSmoother_;

    // Global parameters (targets)
    float damping_ = 0.0f;
    float exciterMix_ = 0.0f;
    float spectralTilt_ = 0.0f;

    // State
    double sampleRate_ = 44100.0;
    TuningMode tuningMode_ = TuningMode::Custom;
    size_t numActiveResonators_ = 0;
    bool prepared_ = false;
    bool triggerPending_ = false;
    float triggerVelocity_ = 0.0f;
};

} // namespace DSP
} // namespace Krate
