// ==============================================================================
// Layer 3: System Component - TimeVaryingCombBank
// ==============================================================================
// Bank of up to 8 comb filters with independently modulated delay times.
// Creates evolving metallic and resonant textures.
//
// Feature: 101-timevar-comb-bank
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 3 (composes Layer 0-1 components)
// - Principle X: DSP Constraints (linear interpolation for modulated delays)
// - Principle XI: Performance Budget (<1% CPU single core at 44.1kHz)
//
// Reference: specs/101-timevar-comb-bank/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/comb_filter.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace Krate {
namespace DSP {

// =============================================================================
// Tuning Mode Enumeration (FR-006)
// =============================================================================

/// @brief Tuning mode for automatic delay time calculation.
enum class Tuning : uint8_t {
    Harmonic,    ///< f[n] = fundamental * (n+1) - musical harmonic series
    Inharmonic,  ///< f[n] = fundamental * sqrt(1 + n*spread) - bell-like partials
    Custom       ///< Manual per-comb delay times via setCombDelay()
};

// =============================================================================
// TimeVaryingCombBank Class
// =============================================================================

/// @brief Bank of up to 8 comb filters with independently modulated delay times.
///
/// Creates evolving metallic and resonant textures by modulating each comb
/// filter's delay time with independent LFOs and optional random drift.
/// Supports automatic harmonic/inharmonic tuning from a fundamental frequency.
///
/// @par Architecture
/// Layer 3 System Component composing:
/// - FeedbackComb x8 (Layer 1) - Core comb filters with damping
/// - LFO x8 (Layer 1) - Per-comb modulation oscillators
/// - OnePoleSmoother x32 (Layer 1) - Parameter smoothing (4 per comb)
/// - Xorshift32 x8 (Layer 0) - Per-comb random drift generators
///
/// @par Signal Flow
/// ```
/// Input -> [Sum for each active comb]:
///            +-> Comb[n] with modulated delay -> gain -> pan -> L/R sum
/// Output <- [L/R stereo output]
/// ```
///
/// @par Constitution Compliance
/// - Principle II: Real-time safe (noexcept, no allocations in process)
/// - Principle IX: Layer 3 depends only on Layers 0-1
/// - Principle X: Linear interpolation for modulated delays (not allpass)
/// - Principle XI: <1% CPU at 44.1kHz with 8 combs
///
/// @see FeedbackComb for individual comb filter implementation
/// @see FilterFeedbackMatrix for related multi-filter system pattern
class TimeVaryingCombBank {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Maximum number of comb filters (compile-time limit)
    static constexpr size_t kMaxCombs = 8;

    /// Minimum fundamental frequency (corresponds to 50ms delay)
    static constexpr float kMinFundamental = 20.0f;

    /// Maximum fundamental frequency
    static constexpr float kMaxFundamental = 1000.0f;

    /// Minimum LFO modulation rate (FR-009)
    static constexpr float kMinModRate = 0.01f;

    /// Maximum LFO modulation rate (FR-009)
    static constexpr float kMaxModRate = 20.0f;

    /// Minimum modulation depth (FR-009)
    static constexpr float kMinModDepth = 0.0f;

    /// Maximum modulation depth as percentage (FR-009)
    static constexpr float kMaxModDepth = 100.0f;

    /// Smoothing time for delay parameter changes (FR-019)
    static constexpr float kDelaySmoothingMs = 20.0f;

    /// Smoothing time for feedback parameter changes (FR-019)
    static constexpr float kFeedbackSmoothingMs = 10.0f;

    /// Smoothing time for damping parameter changes (FR-019)
    static constexpr float kDampingSmoothingMs = 10.0f;

    /// Smoothing time for gain parameter changes (FR-019)
    static constexpr float kGainSmoothingMs = 5.0f;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    /// @brief Default constructor.
    /// Creates an unprepared comb bank. Call prepare() before processing.
    TimeVaryingCombBank() noexcept = default;

    /// @brief Destructor.
    ~TimeVaryingCombBank() = default;

    // Non-copyable (contains move-only DelayLines via FeedbackComb)
    TimeVaryingCombBank(const TimeVaryingCombBank&) = delete;
    TimeVaryingCombBank& operator=(const TimeVaryingCombBank&) = delete;

    // Movable
    TimeVaryingCombBank(TimeVaryingCombBank&&) noexcept = default;
    TimeVaryingCombBank& operator=(TimeVaryingCombBank&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-015, FR-016)
    // =========================================================================

    /// @brief Prepare for processing at the given sample rate.
    ///
    /// Allocates delay line buffers and configures all internal components.
    /// Must be called before process() or processStereo().
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000 typical)
    /// @param maxDelayMs Maximum delay time in milliseconds (default 50ms)
    ///
    /// @note This is the ONLY method that may allocate memory.
    /// @note Safe to call multiple times (reconfigures for new sample rate).
    /// @note FR-015: Allocation failures are handled gracefully.
    void prepare(double sampleRate, float maxDelayMs = 50.0f) noexcept;

    /// @brief Clear all internal state without changing parameters.
    ///
    /// Clears delay lines, LFOs, and random generators.
    /// Call when starting a new audio region to prevent artifacts.
    ///
    /// @note FR-016: Also resets random generators for reproducible behavior.
    void reset() noexcept;

    /// @brief Check if the comb bank has been prepared.
    /// @return true if prepare() has been called successfully
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Comb Configuration (FR-001, FR-002, FR-003, FR-004, FR-005)
    // =========================================================================

    /// @brief Set the number of active comb filters.
    ///
    /// @param count Number of combs to process (clamped to [1, kMaxCombs])
    ///
    /// @note FR-001: Runtime adjustable, 1-8 combs
    /// @note Inactive combs are not processed (CPU optimization)
    void setNumCombs(size_t count) noexcept;

    /// @brief Get the current number of active combs.
    [[nodiscard]] size_t getNumCombs() const noexcept;

    /// @brief Set delay time for a specific comb in milliseconds.
    ///
    /// @param index Comb index (0 to kMaxCombs-1)
    /// @param ms Delay time in milliseconds (clamped to [1, maxDelayMs])
    ///
    /// @note FR-002: Implicitly switches to Custom tuning mode
    void setCombDelay(size_t index, float ms) noexcept;

    /// @brief Set feedback amount for a specific comb.
    ///
    /// @param index Comb index (0 to kMaxCombs-1)
    /// @param amount Feedback amount (clamped to [-0.9999, 0.9999])
    ///
    /// @note FR-003: Positive = normal resonance, negative = inverted phase
    void setCombFeedback(size_t index, float amount) noexcept;

    /// @brief Set damping (lowpass in feedback) for a specific comb.
    ///
    /// @param index Comb index (0 to kMaxCombs-1)
    /// @param amount Damping amount (clamped to [0.0, 1.0])
    ///               0.0 = bright (no HF rolloff), 1.0 = dark (maximum HF rolloff)
    ///
    /// @note FR-004: One-pole lowpass in feedback path
    void setCombDamping(size_t index, float amount) noexcept;

    /// @brief Set output gain for a specific comb in decibels.
    ///
    /// @param index Comb index (0 to kMaxCombs-1)
    /// @param dB Gain in decibels (converted to linear internally)
    ///
    /// @note FR-005: No hard limit, converted via dbToGain()
    void setCombGain(size_t index, float dB) noexcept;

    // =========================================================================
    // Tuning Configuration (FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Set the tuning mode for automatic delay calculation.
    ///
    /// @param mode Tuning mode (Harmonic, Inharmonic, or Custom)
    ///
    /// @note FR-006: Changing to Harmonic/Inharmonic recalculates delays
    /// @note Changing to Custom preserves current delay values
    void setTuningMode(Tuning mode) noexcept;

    /// @brief Get the current tuning mode.
    [[nodiscard]] Tuning getTuningMode() const noexcept;

    /// @brief Set fundamental frequency for automatic tuning.
    ///
    /// @param hz Fundamental frequency in Hz (clamped to [20, 1000])
    ///
    /// @note FR-007: Only affects Harmonic and Inharmonic modes
    /// @note Harmonic: f[n] = fundamental * (n+1)
    /// @note Inharmonic: f[n] = fundamental * sqrt(1 + n*spread)
    void setFundamental(float hz) noexcept;

    /// @brief Get the current fundamental frequency.
    [[nodiscard]] float getFundamental() const noexcept;

    /// @brief Set the inharmonic spread factor.
    ///
    /// @param amount Spread amount (clamped to [0.0, 1.0])
    ///               0.0 = harmonic ratios, 1.0 = maximum inharmonicity
    ///
    /// @note FR-008: Only affects Inharmonic mode
    void setSpread(float amount) noexcept;

    /// @brief Get the current spread factor.
    [[nodiscard]] float getSpread() const noexcept;

    // =========================================================================
    // Modulation Configuration (FR-009, FR-010, FR-011)
    // =========================================================================

    /// @brief Set the global LFO modulation rate.
    ///
    /// @param hz Modulation rate in Hz (clamped to [0.01, 20.0])
    ///
    /// @note FR-009: Applied to all comb LFOs
    void setModRate(float hz) noexcept;

    /// @brief Get the current modulation rate.
    [[nodiscard]] float getModRate() const noexcept;

    /// @brief Set the modulation depth as a percentage.
    ///
    /// @param percent Modulation depth (clamped to [0.0, 100.0])
    ///                0% = no modulation, 100% = +/-100% of base delay
    ///
    /// @note FR-009: Delay varies by +/- (depth/100) * baseDelay
    void setModDepth(float percent) noexcept;

    /// @brief Get the current modulation depth (percentage).
    [[nodiscard]] float getModDepth() const noexcept;

    /// @brief Set the phase spread between adjacent comb LFOs.
    ///
    /// @param degrees Phase spread in degrees (wrapped to [0, 360))
    ///                Each comb gets: basePhase + index * phaseSpread
    ///
    /// @note FR-010: Creates stereo/spatial movement effects
    void setModPhaseSpread(float degrees) noexcept;

    /// @brief Get the current phase spread (degrees).
    [[nodiscard]] float getModPhaseSpread() const noexcept;

    /// @brief Set the random drift modulation amount.
    ///
    /// @param amount Random amount (clamped to [0.0, 1.0])
    ///               0.0 = no random drift, 1.0 = maximum drift
    ///
    /// @note FR-011: Adds organic variation using Xorshift32 PRNG
    void setRandomModulation(float amount) noexcept;

    /// @brief Get the current random modulation amount.
    [[nodiscard]] float getRandomModulation() const noexcept;

    // =========================================================================
    // Stereo Configuration (FR-012)
    // =========================================================================

    /// @brief Set the stereo spread amount.
    ///
    /// @param amount Spread amount (clamped to [0.0, 1.0])
    ///               0.0 = all combs centered, 1.0 = full L-R distribution
    ///
    /// @note FR-012: Combs are distributed L to R based on index
    void setStereoSpread(float amount) noexcept;

    /// @brief Get the current stereo spread.
    [[nodiscard]] float getStereoSpread() const noexcept;

    // =========================================================================
    // Processing Methods (FR-013, FR-014, FR-017, FR-020)
    // =========================================================================

    /// @brief Process a single mono sample.
    ///
    /// @param input Input sample
    /// @return Summed output from all active combs
    ///
    /// @note FR-013: Mono processing, combs summed equally
    /// @note FR-017: Real-time safe (noexcept, no allocations)
    /// @note FR-020: NaN/Inf in any comb resets that comb, returns 0 for it
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Block form of `process(float)`.
    ///
    /// Output is identical, sample for sample, to `numSamples` successive
    /// `process()` calls; when nothing is moving it takes a hoisted path that
    /// lifts the four per-comb smoother steps, the delay-time arithmetic and
    /// the three per-comb setter calls out of the inner loop. See the banner
    /// on the implementation for why that is bit-identical rather than an
    /// approximation, and for the conditions that send it back to the
    /// per-sample path.
    ///
    /// @param input  Source buffer, `numSamples` long
    /// @param output Destination buffer, `numSamples` long. It must NOT alias
    ///               `input`: the hoisted path reads the whole input buffer
    ///               once per comb.
    /// @param numSamples Sample count
    void processBlock(const float* input, float* output, size_t numSamples) noexcept;

    /// @brief Snap every per-comb parameter smoother to its target.
    ///
    /// The bank's own 20 ms delay/feedback/damping/gain smoothers exist for
    /// callers that push raw values from a UI thread. A caller that already
    /// smooths on its own control grid - `ContinuousBody` derives the comb
    /// fundamental from a 20 ms pitch smoother it advances once per 64-sample
    /// control chunk - puts a second lag in series with its own, and keeps the
    /// bank permanently unsettled so `processBlock()` can never take its
    /// hoisted path. Calling this after each parameter push makes the bank's
    /// coefficients piecewise-constant over the caller's control chunk;
    /// continuity is then the caller's own smoother, sampled on its grid.
    void snapSmoothers() noexcept {
        for (auto& ch : channels_) {
            ch.delaySmoother.snapToTarget();
            ch.feedbackSmoother.snapToTarget();
            ch.dampingSmoother.snapToTarget();
            ch.gainSmoother.snapToTarget();
        }
    }

    /// @brief Process stereo samples in-place.
    ///
    /// @param left Left channel sample (input and output)
    /// @param right Right channel sample (input and output)
    ///
    /// @note FR-014: Applies pan distribution per comb
    /// @note FR-017: Real-time safe
    /// @note FR-020: NaN/Inf handling per comb
    void processStereo(float& left, float& right) noexcept;

private:
    // =========================================================================
    // CombChannel Internal Structure
    // =========================================================================

    /// @brief Internal per-comb state (not part of public API).
    struct CombChannel {
        FeedbackComb comb;
        LFO lfo;
        Xorshift32 rng{12345u};

        OnePoleSmoother delaySmoother;
        OnePoleSmoother feedbackSmoother;
        OnePoleSmoother dampingSmoother;
        OnePoleSmoother gainSmoother;

        float baseDelayMs = 10.0f;
        float feedbackTarget = 0.5f;
        float dampingTarget = 0.0f;
        float gainDb = 0.0f;
        float gainLinear = 1.0f;

        float pan = 0.0f;
        float panLeftGain = 0.707107f;
        float panRightGain = 0.707107f;
        float lfoPhaseOffset = 0.0f;
    };

    // Per-comb state array
    std::array<CombChannel, kMaxCombs> channels_;

    // Global parameters
    size_t numCombs_ = 4;
    Tuning tuningMode_ = Tuning::Harmonic;
    float fundamental_ = 100.0f;
    float spread_ = 0.0f;
    float modRate_ = 1.0f;
    float modDepth_ = 0.0f;      // Stored as fraction [0, 1]
    float modPhaseSpread_ = 0.0f;
    float randomModAmount_ = 0.0f;
    float stereoSpread_ = 0.0f;

    // Runtime state
    double sampleRate_ = 44100.0;
    float maxDelayMs_ = 50.0f;
    bool prepared_ = false;

    // Internal helpers
    void recalculateTunedDelays() noexcept;
    void recalculatePanPositions() noexcept;
    void recalculateLfoPhases() noexcept;
    [[nodiscard]] float computeHarmonicDelay(size_t index) const noexcept;
    [[nodiscard]] float computeInharmonicDelay(size_t index) const noexcept;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void TimeVaryingCombBank::prepare(double sampleRate, float maxDelayMs) noexcept {
    sampleRate_ = std::max(1000.0, sampleRate);
    maxDelayMs_ = std::max(1.0f, maxDelayMs);

    const float maxDelaySeconds = maxDelayMs_ / 1000.0f;
    const float sampleRateF = static_cast<float>(sampleRate_);

    for (size_t i = 0; i < kMaxCombs; ++i) {
        auto& ch = channels_[i];

        // Prepare comb filter
        ch.comb.prepare(sampleRate_, maxDelaySeconds);

        // Prepare LFO
        ch.lfo.prepare(sampleRate_);
        ch.lfo.setWaveform(Waveform::Sine);

        // Configure smoothers with appropriate time constants (FR-019)
        ch.delaySmoother.configure(kDelaySmoothingMs, sampleRateF);
        ch.feedbackSmoother.configure(kFeedbackSmoothingMs, sampleRateF);
        ch.dampingSmoother.configure(kDampingSmoothingMs, sampleRateF);
        ch.gainSmoother.configure(kGainSmoothingMs, sampleRateF);

        // Initialize smoother targets
        ch.delaySmoother.snapTo(ch.baseDelayMs);
        ch.feedbackSmoother.snapTo(ch.feedbackTarget);
        ch.dampingSmoother.snapTo(ch.dampingTarget);
        ch.gainSmoother.snapTo(ch.gainLinear);

        // Initialize RNG with unique per-comb seed
        ch.rng.seed(12345u + static_cast<uint32_t>(i) * 7919u);
    }

    // Set initial LFO frequency
    for (size_t i = 0; i < kMaxCombs; ++i) {
        channels_[i].lfo.setFrequency(modRate_);
    }

    prepared_ = true;

    // Recalculate tuned delays and other dependent values
    recalculateTunedDelays();
    recalculatePanPositions();
    recalculateLfoPhases();
}

inline void TimeVaryingCombBank::reset() noexcept {
    for (size_t i = 0; i < kMaxCombs; ++i) {
        auto& ch = channels_[i];
        ch.comb.reset();
        ch.lfo.reset();
        ch.rng.seed(12345u + static_cast<uint32_t>(i) * 7919u);

        // Snap smoothers to current targets
        ch.delaySmoother.snapToTarget();
        ch.feedbackSmoother.snapToTarget();
        ch.dampingSmoother.snapToTarget();
        ch.gainSmoother.snapToTarget();
    }
}

inline bool TimeVaryingCombBank::isPrepared() const noexcept {
    return prepared_;
}

inline void TimeVaryingCombBank::setNumCombs(size_t count) noexcept {
    numCombs_ = std::clamp(count, size_t{1}, kMaxCombs);
    recalculatePanPositions();
    recalculateLfoPhases();
}

inline size_t TimeVaryingCombBank::getNumCombs() const noexcept {
    return numCombs_;
}

inline void TimeVaryingCombBank::setCombDelay(size_t index, float ms) noexcept {
    if (index >= kMaxCombs) return;

    // Implicitly switch to Custom mode (FR-006)
    tuningMode_ = Tuning::Custom;

    const float clampedMs = std::clamp(ms, 1.0f, maxDelayMs_);
    channels_[index].baseDelayMs = clampedMs;
    channels_[index].delaySmoother.setTarget(clampedMs);
}

inline void TimeVaryingCombBank::setCombFeedback(size_t index, float amount) noexcept {
    if (index >= kMaxCombs) return;

    const float clamped = std::clamp(amount, kMinCombCoeff, kMaxCombCoeff);
    channels_[index].feedbackTarget = clamped;
    channels_[index].feedbackSmoother.setTarget(clamped);
}

inline void TimeVaryingCombBank::setCombDamping(size_t index, float amount) noexcept {
    if (index >= kMaxCombs) return;

    const float clamped = std::clamp(amount, 0.0f, 1.0f);
    channels_[index].dampingTarget = clamped;
    channels_[index].dampingSmoother.setTarget(clamped);
}

inline void TimeVaryingCombBank::setCombGain(size_t index, float dB) noexcept {
    if (index >= kMaxCombs) return;

    channels_[index].gainDb = dB;
    channels_[index].gainLinear = dbToGain(dB);
    channels_[index].gainSmoother.setTarget(channels_[index].gainLinear);
}

inline void TimeVaryingCombBank::setTuningMode(Tuning mode) noexcept {
    tuningMode_ = mode;
    if (mode != Tuning::Custom) {
        recalculateTunedDelays();
    }
}

inline Tuning TimeVaryingCombBank::getTuningMode() const noexcept {
    return tuningMode_;
}

inline void TimeVaryingCombBank::setFundamental(float hz) noexcept {
    fundamental_ = std::clamp(hz, kMinFundamental, kMaxFundamental);
    if (tuningMode_ != Tuning::Custom) {
        recalculateTunedDelays();
    }
}

inline float TimeVaryingCombBank::getFundamental() const noexcept {
    return fundamental_;
}

inline void TimeVaryingCombBank::setSpread(float amount) noexcept {
    spread_ = std::clamp(amount, 0.0f, 1.0f);
    if (tuningMode_ == Tuning::Inharmonic) {
        recalculateTunedDelays();
    }
}

inline float TimeVaryingCombBank::getSpread() const noexcept {
    return spread_;
}

inline void TimeVaryingCombBank::setModRate(float hz) noexcept {
    modRate_ = std::clamp(hz, kMinModRate, kMaxModRate);
    for (size_t i = 0; i < kMaxCombs; ++i) {
        channels_[i].lfo.setFrequency(modRate_);
    }
}

inline float TimeVaryingCombBank::getModRate() const noexcept {
    return modRate_;
}

inline void TimeVaryingCombBank::setModDepth(float percent) noexcept {
    // Input is percentage [0, 100], store as fraction [0, 1]
    modDepth_ = std::clamp(percent, kMinModDepth, kMaxModDepth) / 100.0f;
}

inline float TimeVaryingCombBank::getModDepth() const noexcept {
    // Return as percentage
    return modDepth_ * 100.0f;
}

inline void TimeVaryingCombBank::setModPhaseSpread(float degrees) noexcept {
    // Wrap to [0, 360)
    modPhaseSpread_ = std::fmod(degrees, 360.0f);
    if (modPhaseSpread_ < 0.0f) {
        modPhaseSpread_ += 360.0f;
    }
    recalculateLfoPhases();
}

inline float TimeVaryingCombBank::getModPhaseSpread() const noexcept {
    return modPhaseSpread_;
}

inline void TimeVaryingCombBank::setRandomModulation(float amount) noexcept {
    randomModAmount_ = std::clamp(amount, 0.0f, 1.0f);
}

inline float TimeVaryingCombBank::getRandomModulation() const noexcept {
    return randomModAmount_;
}

inline void TimeVaryingCombBank::setStereoSpread(float amount) noexcept {
    stereoSpread_ = std::clamp(amount, 0.0f, 1.0f);
    recalculatePanPositions();
}

inline float TimeVaryingCombBank::getStereoSpread() const noexcept {
    return stereoSpread_;
}

inline float TimeVaryingCombBank::process(float input) noexcept {
    if (!prepared_) {
        return 0.0f;
    }

    // Check input for NaN/Inf
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    float output = 0.0f;

    // Zero-modulation fast path -- see the modActive banner on processStereo().
    const bool modActive = (modDepth_ > 0.0f);

    for (size_t i = 0; i < numCombs_; ++i) {
        auto& ch = channels_[i];

        // Get smoothed parameters
        const float smoothedDelay = ch.delaySmoother.process();
        const float smoothedFeedback = ch.feedbackSmoother.process();
        const float smoothedDamping = ch.dampingSmoother.process();
        const float smoothedGain = ch.gainSmoother.process();

        float modulatedDelayMs = smoothedDelay;
        if (modActive) {
            // Calculate modulated delay time
            const float lfoValue = ch.lfo.process();

            // Random drift (scaled by modDepth and randomModAmount)
            const float randomValue = ch.rng.nextFloat();  // [-1, 1]
            const float drift = randomValue * randomModAmount_ * modDepth_ * smoothedDelay;

            // Modulated delay = base * (1 + depth * lfo) + drift
            modulatedDelayMs = smoothedDelay * (1.0f + modDepth_ * lfoValue) + drift;
        }

        // Clamp modulated delay to valid range
        modulatedDelayMs = std::clamp(modulatedDelayMs, 1.0f, maxDelayMs_);

        // Convert to samples and set on comb
        const float delaySamples = modulatedDelayMs * 0.001f * static_cast<float>(sampleRate_);
        ch.comb.setDelaySamples(delaySamples);
        ch.comb.setFeedback(smoothedFeedback);
        ch.comb.setDamping(smoothedDamping);

        // Process through comb
        float combOutput = ch.comb.process(input);

        // Check for NaN/Inf in comb output (FR-020)
        if (detail::isNaN(combOutput) || detail::isInf(combOutput)) {
            ch.comb.reset();
            combOutput = 0.0f;
        }

        // Apply gain and accumulate
        output += combOutput * smoothedGain;
    }

    // Flush denormals
    output = detail::flushDenormal(output);

    return output;
}

// =============================================================================
// processBlock - hoisted equivalent of N x process()
// =============================================================================
// WHY: `process(float)` re-derives, for every sample and for every comb, four
// OnePoleSmoother steps, a clamp, a millisecond-to-samples conversion and three
// setter calls on the FeedbackComb - 24 smoother steps and 18 setter calls per
// sample at numCombs_ = 6. None of it moves once the smoothers have settled and
// modulation is off. Measured on this repo's MSVC Release build, 512 samples
// through a 6-comb bank cost 50,849 ns/block on the per-sample path.
//
// BIT-IDENTICAL, not an approximation, under the guard below:
//   * every smoother is required to report isComplete(). OnePoleSmoother::
//     process() snaps `current_ = target_` and returns `target_` from that
//     point on (smoother.h:197-211), so the ONE call made here returns exactly
//     what each of the numSamples calls would have returned;
//   * `modDepth_ == 0` makes the LFO and drift terms provably zero - the same
//     argument as the zero-modulation fast path in processStereo();
//   * the accumulation order per sample is unchanged. The loops are
//     interchanged (comb outer, sample inner) but `output[n]` still receives
//     comb 0's contribution first and comb numCombs_-1's last, starting from
//     0.0f, so the floating-point sum is formed in the identical order;
//   * a non-finite INPUT sample makes `process()` reset the whole bank and
//     return 0, which is a state change the hoisted path cannot express. The
//     block is scanned first and falls back to the per-sample path in one
//     piece if any sample is non-finite.
// =============================================================================
inline void TimeVaryingCombBank::processBlock(const float* input, float* output,
                                              size_t numSamples) noexcept {
    if (input == nullptr || output == nullptr || numSamples == 0) {
        return;
    }

    bool hoist = prepared_ && (modDepth_ <= 0.0f);
    for (size_t i = 0; hoist && i < numCombs_; ++i) {
        const auto& ch = channels_[i];
        hoist = ch.delaySmoother.isComplete() && ch.feedbackSmoother.isComplete()
                && ch.dampingSmoother.isComplete() && ch.gainSmoother.isComplete();
    }
    for (size_t n = 0; hoist && n < numSamples; ++n) {
        hoist = !detail::isNaN(input[n]) && !detail::isInf(input[n]);
    }

    if (!hoist) {
        for (size_t n = 0; n < numSamples; ++n) {
            output[n] = process(input[n]);
        }
        return;
    }

    std::array<float, kMaxCombs> combGain{};
    for (size_t i = 0; i < numCombs_; ++i) {
        auto& ch = channels_[i];

        const float smoothedDelay = ch.delaySmoother.process();
        const float smoothedFeedback = ch.feedbackSmoother.process();
        const float smoothedDamping = ch.dampingSmoother.process();
        combGain[i] = ch.gainSmoother.process();

        const float modulatedDelayMs = std::clamp(smoothedDelay, 1.0f, maxDelayMs_);
        const float delaySamples = modulatedDelayMs * 0.001f * static_cast<float>(sampleRate_);
        ch.comb.setDelaySamples(delaySamples);
        ch.comb.setFeedback(smoothedFeedback);
        ch.comb.setDamping(smoothedDamping);
    }

    for (size_t n = 0; n < numSamples; ++n) {
        output[n] = 0.0f;
    }

    for (size_t i = 0; i < numCombs_; ++i) {
        auto& ch = channels_[i];
        const float gain = combGain[i];
        // The delay is fixed for the whole block on this path, so the read
        // tap - a clamp, a std::floor, a float-to-index conversion and two
        // range clamps - is derived once instead of on every sample.
        const DelayLine::LinearTap tap = ch.comb.makeTap();
        for (size_t n = 0; n < numSamples; ++n) {
            float combOutput = ch.comb.process(input[n], tap);
            if (detail::isNaN(combOutput) || detail::isInf(combOutput)) {
                ch.comb.reset();
                combOutput = 0.0f;
            }
            output[n] += combOutput * gain;
        }
    }

    for (size_t n = 0; n < numSamples; ++n) {
        output[n] = detail::flushDenormal(output[n]);
    }
}

inline void TimeVaryingCombBank::processStereo(float& left, float& right) noexcept {
    if (!prepared_) {
        left = 0.0f;
        right = 0.0f;
        return;
    }

    // Sum input to mono for processing
    const float monoInput = (left + right) * 0.5f;

    // Check input for NaN/Inf
    if (detail::isNaN(monoInput) || detail::isInf(monoInput)) {
        reset();
        left = 0.0f;
        right = 0.0f;
        return;
    }

    float leftOut = 0.0f;
    float rightOut = 0.0f;

    // ====================== ZERO-MODULATION FAST PATH ========================
    // `LFO::process()` (primitives/lfo.h:116) is a general-purpose generator:
    // phase wrap in double, optional symmetry warp, wavetable lookup, waveform
    // crossfade, quantize and a fade-in envelope. At `numCombs_ = 6` it runs SIX
    // times per sample, plus six `Xorshift32::nextFloat()` calls -- and with
    // `modDepth_ == 0` (the DEFAULT, `modDepth_ = 0.0f` in the member block)
    // every one of those results is multiplied by zero.
    //
    // BIT-IDENTICAL, not a behaviour change, at modDepth_ == 0:
    //   drift             = randomValue * randomModAmount_ * 0.0f * smoothedDelay
    //                     = 0.0f for every finite randomValue;
    //   modulatedDelayMs  = smoothedDelay * (1.0f + 0.0f*lfoValue) + 0.0f
    //                     = smoothedDelay * 1.0f
    //                     = smoothedDelay,
    // exactly, for every finite lfoValue (`smoothedDelay` is a clamped-positive
    // delay, so the `+ 0.0f` cannot even flip a signed zero).
    //
    // WHAT DOES CHANGE: the per-comb LFO phase and RNG stream no longer advance
    // while modDepth_ is zero, so a later setModDepth(> 0) resumes on the phase
    // the bank held when modulation was switched off rather than on a
    // free-running one. That is deliberately different from DiffusionNetwork's
    // RA-4 guard, where the phase accumulator sits outside the guarded
    // expression and could be kept running for free; here the phase lives
    // inside the call being elided. Nothing observable depends on it: with the
    // depth at zero the phase is unobservable by construction.
    //
    // Measured (dsp_systems_tests, 6 combs, 512-sample block @ 48 kHz):
    // 74,580 ns/block before, see the SC-005 table in
    // specs/seraphis-phase4-continuous-body/spec.md.
    // =========================================================================
    const bool modActive = (modDepth_ > 0.0f);

    for (size_t i = 0; i < numCombs_; ++i) {
        auto& ch = channels_[i];

        // Get smoothed parameters
        const float smoothedDelay = ch.delaySmoother.process();
        const float smoothedFeedback = ch.feedbackSmoother.process();
        const float smoothedDamping = ch.dampingSmoother.process();
        const float smoothedGain = ch.gainSmoother.process();

        float modulatedDelayMs = smoothedDelay;
        if (modActive) {
            // Calculate modulated delay time
            const float lfoValue = ch.lfo.process();

            // Random drift (scaled by modDepth and randomModAmount)
            const float randomValue = ch.rng.nextFloat();  // [-1, 1]
            const float drift = randomValue * randomModAmount_ * modDepth_ * smoothedDelay;

            // Modulated delay = base * (1 + depth * lfo) + drift
            modulatedDelayMs = smoothedDelay * (1.0f + modDepth_ * lfoValue) + drift;
        }

        // Clamp modulated delay to valid range
        modulatedDelayMs = std::clamp(modulatedDelayMs, 1.0f, maxDelayMs_);

        // Convert to samples and set on comb
        const float delaySamples = modulatedDelayMs * 0.001f * static_cast<float>(sampleRate_);
        ch.comb.setDelaySamples(delaySamples);
        ch.comb.setFeedback(smoothedFeedback);
        ch.comb.setDamping(smoothedDamping);

        // Process through comb
        float combOutput = ch.comb.process(monoInput);

        // Check for NaN/Inf in comb output (FR-020)
        if (detail::isNaN(combOutput) || detail::isInf(combOutput)) {
            ch.comb.reset();
            combOutput = 0.0f;
        }

        // Apply gain
        const float gainedOutput = combOutput * smoothedGain;

        // Apply pan distribution (FR-014)
        leftOut += gainedOutput * ch.panLeftGain;
        rightOut += gainedOutput * ch.panRightGain;
    }

    // Flush denormals
    left = detail::flushDenormal(leftOut);
    right = detail::flushDenormal(rightOut);
}

inline void TimeVaryingCombBank::recalculateTunedDelays() noexcept {
    if (tuningMode_ == Tuning::Custom) {
        return;
    }

    for (size_t i = 0; i < kMaxCombs; ++i) {
        float delayMs;
        if (tuningMode_ == Tuning::Harmonic) {
            delayMs = computeHarmonicDelay(i);
        } else {  // Inharmonic
            delayMs = computeInharmonicDelay(i);
        }

        // Clamp to valid range
        delayMs = std::clamp(delayMs, 1.0f, maxDelayMs_);

        channels_[i].baseDelayMs = delayMs;
        channels_[i].delaySmoother.setTarget(delayMs);
    }
}

inline void TimeVaryingCombBank::recalculatePanPositions() noexcept {
    constexpr float kPi = std::numbers::pi_v<float>;

    for (size_t i = 0; i < kMaxCombs; ++i) {
        // Calculate pan position: distribute combs from left (-1) to right (+1)
        // based on stereo spread. With spread=0, all centered (pan=0)
        // With spread=1, fully distributed
        float normalizedIndex;
        if (numCombs_ > 1) {
            normalizedIndex = static_cast<float>(i) / static_cast<float>(numCombs_ - 1);
        } else {
            normalizedIndex = 0.5f;  // Center if only one comb
        }

        // Map [0, 1] to [-1, 1]
        const float fullPan = normalizedIndex * 2.0f - 1.0f;

        // Apply stereo spread
        const float pan = fullPan * stereoSpread_;
        channels_[i].pan = pan;

        // Calculate equal-power pan gains
        // pan in [-1, 1] -> angle in [0, pi/2]
        const float angle = (pan + 1.0f) * 0.25f * kPi;
        channels_[i].panLeftGain = std::cos(angle);
        channels_[i].panRightGain = std::sin(angle);
    }
}

inline void TimeVaryingCombBank::recalculateLfoPhases() noexcept {
    for (size_t i = 0; i < kMaxCombs; ++i) {
        const float phaseOffset = static_cast<float>(i) * modPhaseSpread_;
        channels_[i].lfoPhaseOffset = phaseOffset;
        channels_[i].lfo.setPhaseOffset(phaseOffset);
    }
}

inline float TimeVaryingCombBank::computeHarmonicDelay(size_t index) const noexcept {
    // f[n] = fundamental * (n + 1)
    // delay_ms = 1000 / f[n]
    const float frequency = fundamental_ * static_cast<float>(index + 1);
    return 1000.0f / frequency;
}

inline float TimeVaryingCombBank::computeInharmonicDelay(size_t index) const noexcept {
    // f[n] = fundamental * sqrt(1 + n * spread)
    // delay_ms = 1000 / f[n]
    const float frequency = fundamental_ * std::sqrt(1.0f + static_cast<float>(index) * spread_);
    return 1000.0f / frequency;
}

}  // namespace DSP
}  // namespace Krate
