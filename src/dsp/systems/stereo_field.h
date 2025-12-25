// ==============================================================================
// Layer 3: System Component - StereoField
// ==============================================================================
// High-level stereo processing modes for delay effects.
//
// Provides five stereo modes:
// - Mono: Sum L+R to identical outputs
// - Stereo: Independent L/R processing with L/R Ratio
// - PingPong: Alternating L/R delays with cross-feedback
// - DualMono: Same delay time, panned outputs
// - MidSide: M/S encoding with independent M/S delays
//
// Feature: 022-stereo-field
// Layer: 3 (System Component)
// Dependencies: Layer 0 (db_utils), Layer 1 (DelayLine, OnePoleSmoother),
//               Layer 2 (MidSideProcessor), Layer 3 (DelayEngine)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (enum class, nodiscard, noexcept)
// - Principle IX: Layered Architecture (Layer 3)
// - Principle XII: Test-First Development
//
// Reference: specs/022-stereo-field/spec.md
// ==============================================================================

#pragma once

#include "dsp/core/db_utils.h"
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/smoother.h"
#include "dsp/processors/midside_processor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// StereoMode Enumeration (FR-001)
// =============================================================================

/// @brief Available stereo processing modes.
enum class StereoMode : uint8_t {
    Mono,      ///< Sum L+R to both outputs (FR-007)
    Stereo,    ///< Independent L/R processing (FR-008)
    PingPong,  ///< Alternating L/R delays with cross-feedback (FR-009)
    DualMono,  ///< Same delay time, panned outputs (FR-010)
    MidSide    ///< M/S encode, delay, decode (FR-011)
};

// =============================================================================
// StereoField Class
// =============================================================================

/// @brief Layer 3 system component for stereo processing modes in delay effects.
///
/// Provides five stereo modes with smooth transitions:
/// - Mono: Sum L+R to identical mono output
/// - Stereo: Independent L/R delays with optional L/R ratio
/// - PingPong: Alternating delays with cross-feedback
/// - DualMono: Same delay, independent panning
/// - MidSide: M/S domain processing with width control
///
/// All parameters have smooth transitions (20ms) to prevent clicks.
/// Mode transitions use 50ms crossfade (SC-002).
///
/// @note All process() methods are noexcept and allocation-free.
/// @note Memory is allocated only in prepare() (Principle II).
class StereoField {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinWidth = 0.0f;          ///< Minimum width (mono)
    static constexpr float kMaxWidth = 200.0f;        ///< Maximum width
    static constexpr float kDefaultWidth = 100.0f;    ///< Unity width

    static constexpr float kMinPan = -100.0f;         ///< Full left
    static constexpr float kMaxPan = 100.0f;          ///< Full right
    static constexpr float kDefaultPan = 0.0f;        ///< Center

    static constexpr float kMinLROffset = -50.0f;     ///< Max L delayed
    static constexpr float kMaxLROffset = 50.0f;      ///< Max R delayed

    static constexpr float kMinLRRatio = 0.1f;        ///< Minimum ratio (FR-016)
    static constexpr float kMaxLRRatio = 10.0f;       ///< Maximum ratio (FR-016)
    static constexpr float kDefaultLRRatio = 1.0f;    ///< Equal L/R timing

    static constexpr float kSmoothingTimeMs = 20.0f;  ///< Parameter smoothing (FR-017)
    static constexpr float kTransitionTimeMs = 50.0f; ///< Mode transition (FR-003)

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    /// @brief Default constructor. Creates an uninitialized StereoField.
    StereoField() noexcept = default;

    /// @brief Destructor.
    ~StereoField() = default;

    // Non-copyable, movable
    StereoField(const StereoField&) = delete;
    StereoField& operator=(const StereoField&) = delete;
    StereoField(StereoField&&) noexcept = default;
    StereoField& operator=(StereoField&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-004, FR-006)
    // =========================================================================

    /// @brief Prepare for processing. (FR-004)
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process block
    /// @param maxDelayMs Maximum delay time in milliseconds
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;

    /// @brief Clear all internal state to silence. (FR-006)
    void reset() noexcept;

    // =========================================================================
    // Configuration Methods
    // =========================================================================

    /// @brief Set the stereo processing mode. (FR-002)
    void setMode(StereoMode mode) noexcept;

    /// @brief Get the current stereo mode.
    [[nodiscard]] StereoMode getMode() const noexcept { return currentMode_; }

    /// @brief Set the base delay time in milliseconds. (FR-021)
    void setDelayTimeMs(float ms) noexcept;

    /// @brief Get the current base delay time.
    [[nodiscard]] float getDelayTimeMs() const noexcept { return targetDelayMs_; }

    /// @brief Set stereo width (0-200%). (FR-012)
    void setWidth(float widthPercent) noexcept;

    /// @brief Get the current width setting.
    [[nodiscard]] float getWidth() const noexcept { return width_; }

    /// @brief Set output pan (-100 to +100). (FR-013)
    void setPan(float pan) noexcept;

    /// @brief Get the current pan setting.
    [[nodiscard]] float getPan() const noexcept { return pan_; }

    /// @brief Set L/R timing offset in milliseconds (±50ms). (FR-014)
    void setLROffset(float offsetMs) noexcept;

    /// @brief Get the current L/R offset.
    [[nodiscard]] float getLROffset() const noexcept { return lrOffset_; }

    /// @brief Set L/R delay ratio for polyrhythmic delays. (FR-015)
    void setLRRatio(float ratio) noexcept;

    /// @brief Get the current L/R ratio.
    [[nodiscard]] float getLRRatio() const noexcept { return lrRatio_; }

    // =========================================================================
    // Processing Methods (FR-005)
    // =========================================================================

    /// @brief Process stereo audio. (FR-005, FR-018)
    /// @param leftIn Input left channel
    /// @param rightIn Input right channel
    /// @param leftOut Output left channel
    /// @param rightOut Output right channel
    /// @param numSamples Number of samples to process
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept;

private:
    // =========================================================================
    // Internal Processing Methods
    // =========================================================================

    void processMono(const float* leftIn, const float* rightIn,
                     float* leftOut, float* rightOut, size_t numSamples) noexcept;

    void processStereo(const float* leftIn, const float* rightIn,
                       float* leftOut, float* rightOut, size_t numSamples) noexcept;

    void processPingPong(const float* leftIn, const float* rightIn,
                         float* leftOut, float* rightOut, size_t numSamples) noexcept;

    void processDualMono(const float* leftIn, const float* rightIn,
                         float* leftOut, float* rightOut, size_t numSamples) noexcept;

    void processMidSide(const float* leftIn, const float* rightIn,
                        float* leftOut, float* rightOut, size_t numSamples) noexcept;

    /// @brief Apply constant-power panning. (FR-020)
    void applyPan(float sample, float panAmount, float& left, float& right) noexcept;

    /// @brief Sanitize input (replace NaN with 0). (FR-019)
    [[nodiscard]] float sanitizeInput(float sample) const noexcept;

    /// @brief Convert milliseconds to samples.
    [[nodiscard]] float msToSamples(float ms) const noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Delay lines for main processing
    DelayLine delayL_;              ///< Left channel delay
    DelayLine delayR_;              ///< Right channel delay

    // Small delay lines for L/R offset (max 50ms at 192kHz ~ 9600 samples)
    DelayLine offsetDelayL_;        ///< L offset delay
    DelayLine offsetDelayR_;        ///< R offset delay

    // Mid/Side processor for MidSide mode
    MidSideProcessor msProcessor_;

    // Parameter smoothers (FR-017)
    OnePoleSmoother delaySmoother_;      ///< Base delay time
    OnePoleSmoother widthSmoother_;      ///< Width (as factor 0-2)
    OnePoleSmoother panSmoother_;        ///< Pan (-1 to +1)
    OnePoleSmoother lrOffsetSmoother_;   ///< L/R offset in samples
    OnePoleSmoother lrRatioSmoother_;    ///< L/R ratio
    LinearRamp transitionRamp_;          ///< Mode crossfade (50ms)

    // Target parameter values
    float targetDelayMs_ = 0.0f;
    float width_ = kDefaultWidth;
    float pan_ = kDefaultPan;
    float lrOffset_ = 0.0f;          ///< L/R offset in ms
    float lrRatio_ = kDefaultLRRatio;

    // Mode state
    StereoMode currentMode_ = StereoMode::Stereo;
    StereoMode targetMode_ = StereoMode::Stereo;
    bool transitioning_ = false;

    // Runtime state
    double sampleRate_ = 0.0;
    float maxDelayMs_ = 0.0f;
    size_t maxBlockSize_ = 0;
    bool prepared_ = false;

    // PingPong state
    bool pingPongPhase_ = false;     ///< true = R active, false = L active
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline void StereoField::prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    maxDelayMs_ = maxDelayMs;

    // Prepare main delay lines
    const float maxDelaySeconds = maxDelayMs / 1000.0f;
    delayL_.prepare(sampleRate, maxDelaySeconds);
    delayR_.prepare(sampleRate, maxDelaySeconds);

    // Prepare offset delay lines (50ms max)
    constexpr float kMaxOffsetSeconds = 0.05f;
    offsetDelayL_.prepare(sampleRate, kMaxOffsetSeconds);
    offsetDelayR_.prepare(sampleRate, kMaxOffsetSeconds);

    // Prepare M/S processor
    msProcessor_.prepare(static_cast<float>(sampleRate), maxBlockSize);

    // Configure parameter smoothers (20ms)
    const float sr = static_cast<float>(sampleRate);
    delaySmoother_.configure(kSmoothingTimeMs, sr);
    widthSmoother_.configure(kSmoothingTimeMs, sr);
    panSmoother_.configure(kSmoothingTimeMs, sr);
    lrOffsetSmoother_.configure(kSmoothingTimeMs, sr);
    lrRatioSmoother_.configure(kSmoothingTimeMs, sr);

    // Configure mode transition ramp (50ms)
    transitionRamp_.configure(kTransitionTimeMs, sr);

    // Initialize smoothers
    reset();

    prepared_ = true;
}

inline void StereoField::reset() noexcept {
    delayL_.reset();
    delayR_.reset();
    offsetDelayL_.reset();
    offsetDelayR_.reset();
    msProcessor_.reset();

    // Snap smoothers to their current target values
    delaySmoother_.snapTo(msToSamples(targetDelayMs_));
    widthSmoother_.snapTo(width_ / 100.0f);
    panSmoother_.snapTo(pan_ / 100.0f);
    lrOffsetSmoother_.snapTo(msToSamples(lrOffset_));
    lrRatioSmoother_.snapTo(lrRatio_);
    transitionRamp_.snapTo(1.0f);

    transitioning_ = false;
    pingPongPhase_ = false;
}

inline void StereoField::setMode(StereoMode mode) noexcept {
    if (mode == currentMode_) {
        return;
    }

    // For now, switch modes instantly (shared delay lines make crossfade complex)
    // Reset delay lines on mode switch to prevent artifacts
    delayL_.reset();
    delayR_.reset();
    offsetDelayL_.reset();
    offsetDelayR_.reset();

    currentMode_ = mode;
    targetMode_ = mode;
    transitioning_ = false;
}

inline void StereoField::setDelayTimeMs(float ms) noexcept {
    // Handle NaN (FR-019)
    if (detail::isNaN(ms)) {
        ms = 0.0f;
    }
    // Clamp to valid range
    ms = std::clamp(ms, 0.0f, maxDelayMs_);
    targetDelayMs_ = ms;
    // Store as samples (smoother works in samples, not ms)
    delaySmoother_.setTarget(msToSamples(ms));
}

inline void StereoField::setWidth(float widthPercent) noexcept {
    width_ = std::clamp(widthPercent, kMinWidth, kMaxWidth);
    widthSmoother_.setTarget(width_ / 100.0f);
    msProcessor_.setWidth(width_);
}

inline void StereoField::setPan(float pan) noexcept {
    pan_ = std::clamp(pan, kMinPan, kMaxPan);
    panSmoother_.setTarget(pan_ / 100.0f);
}

inline void StereoField::setLROffset(float offsetMs) noexcept {
    lrOffset_ = std::clamp(offsetMs, kMinLROffset, kMaxLROffset);
    lrOffsetSmoother_.setTarget(msToSamples(lrOffset_));
}

inline void StereoField::setLRRatio(float ratio) noexcept {
    lrRatio_ = std::clamp(ratio, kMinLRRatio, kMaxLRRatio);
    lrRatioSmoother_.setTarget(lrRatio_);
}

inline void StereoField::process(const float* leftIn, const float* rightIn,
                                  float* leftOut, float* rightOut,
                                  size_t numSamples) noexcept {
    if (!prepared_) return;

    // Handle mode transition
    if (transitioning_) {
        // Process with crossfade between modes
        for (size_t i = 0; i < numSamples; ++i) {
            const float blend = transitionRamp_.process();

            // Sanitize inputs (FR-019)
            const float inL = sanitizeInput(leftIn[i]);
            const float inR = sanitizeInput(rightIn[i]);

            // Process old mode
            float oldL = 0.0f, oldR = 0.0f;
            float newL = 0.0f, newR = 0.0f;

            // Single sample processing for crossfade
            const float* singleInL = &inL;
            const float* singleInR = &inR;

            switch (currentMode_) {
                case StereoMode::Mono:
                    processMono(singleInL, singleInR, &oldL, &oldR, 1);
                    break;
                case StereoMode::Stereo:
                    processStereo(singleInL, singleInR, &oldL, &oldR, 1);
                    break;
                case StereoMode::PingPong:
                    processPingPong(singleInL, singleInR, &oldL, &oldR, 1);
                    break;
                case StereoMode::DualMono:
                    processDualMono(singleInL, singleInR, &oldL, &oldR, 1);
                    break;
                case StereoMode::MidSide:
                    processMidSide(singleInL, singleInR, &oldL, &oldR, 1);
                    break;
            }

            switch (targetMode_) {
                case StereoMode::Mono:
                    processMono(singleInL, singleInR, &newL, &newR, 1);
                    break;
                case StereoMode::Stereo:
                    processStereo(singleInL, singleInR, &newL, &newR, 1);
                    break;
                case StereoMode::PingPong:
                    processPingPong(singleInL, singleInR, &newL, &newR, 1);
                    break;
                case StereoMode::DualMono:
                    processDualMono(singleInL, singleInR, &newL, &newR, 1);
                    break;
                case StereoMode::MidSide:
                    processMidSide(singleInL, singleInR, &newL, &newR, 1);
                    break;
            }

            // Crossfade
            leftOut[i] = oldL * (1.0f - blend) + newL * blend;
            rightOut[i] = oldR * (1.0f - blend) + newR * blend;

            if (transitionRamp_.isComplete()) {
                currentMode_ = targetMode_;
                transitioning_ = false;
            }
        }
        return;
    }

    // Normal processing (no transition)
    switch (currentMode_) {
        case StereoMode::Mono:
            processMono(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;
        case StereoMode::Stereo:
            processStereo(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;
        case StereoMode::PingPong:
            processPingPong(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;
        case StereoMode::DualMono:
            processDualMono(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;
        case StereoMode::MidSide:
            processMidSide(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;
    }
}

inline void StereoField::processMono(const float* leftIn, const float* rightIn,
                                      float* leftOut, float* rightOut,
                                      size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        const float inL = sanitizeInput(leftIn[i]);
        const float inR = sanitizeInput(rightIn[i]);

        // Sum to mono
        const float mono = (inL + inR) * 0.5f;

        // Get smoothed parameters
        // Note: delaySmoother_ already stores samples (not ms)
        const float delaySamples = delaySmoother_.process();
        const float panAmount = panSmoother_.process();

        // Advance unused smoothers (Width, L/R Offset, L/R Ratio ignored in Mono mode)
        (void)widthSmoother_.process();
        (void)lrOffsetSmoother_.process();
        (void)lrRatioSmoother_.process();

        // Write to delay
        delayL_.write(mono);

        // Read from delay
        const float delayed = delayL_.readLinear(delaySamples);

        // FR-007: Mono mode outputs mono signal (same content) to both channels
        // FR-013: Pan still applies to distribute the mono signal across L/R
        applyPan(delayed, panAmount, leftOut[i], rightOut[i]);
    }
}

inline void StereoField::processStereo(const float* leftIn, const float* rightIn,
                                        float* leftOut, float* rightOut,
                                        size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        const float inL = sanitizeInput(leftIn[i]);
        const float inR = sanitizeInput(rightIn[i]);

        // Get smoothed parameters
        // Note: delaySmoother_ already stores samples (not ms)
        const float baseDelaySamples = delaySmoother_.process();
        const float ratio = lrRatioSmoother_.process();
        const float offsetSamples = lrOffsetSmoother_.process();
        const float width = widthSmoother_.process();

        // Advance unused smoother
        (void)panSmoother_.process();

        // Calculate L and R delay times based on ratio
        // R is the base, L is scaled by ratio
        const float delaySamplesL = baseDelaySamples * ratio;
        const float delaySamplesR = baseDelaySamples;

        // Write to delays
        delayL_.write(inL);
        delayR_.write(inR);

        // Read with independent delay times
        float delayedL = delayL_.readLinear(delaySamplesL);
        float delayedR = delayR_.readLinear(delaySamplesR);

        // Always write to offset delay lines to keep them synchronized
        offsetDelayL_.write(delayedL);
        offsetDelayR_.write(delayedR);

        // Apply L/R offset
        if (offsetSamples > 0.0f) {
            // Positive offset: R delayed relative to L
            delayedR = offsetDelayR_.readLinear(offsetSamples);
        } else if (offsetSamples < 0.0f) {
            // Negative offset: L delayed relative to R
            delayedL = offsetDelayL_.readLinear(-offsetSamples);
        }
        // If offsetSamples == 0, both read at 0 (current samples)

        // Apply width using M/S conversion
        // M = (L + R) / 2, S = (L - R) / 2
        // Then scale S by width factor
        // L = M + S*width, R = M - S*width
        const float mid = (delayedL + delayedR) * 0.5f;
        const float side = (delayedL - delayedR) * 0.5f * width;

        leftOut[i] = mid + side;
        rightOut[i] = mid - side;
    }
}

inline void StereoField::processPingPong(const float* leftIn, const float* rightIn,
                                          float* leftOut, float* rightOut,
                                          size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        const float inL = sanitizeInput(leftIn[i]);
        const float inR = sanitizeInput(rightIn[i]);

        // Sum input to mono
        const float mono = (inL + inR) * 0.5f;

        // Get smoothed delay time
        // Note: delaySmoother_ already stores samples (not ms)
        const float delaySamples = delaySmoother_.process();

        // Advance unused smoothers
        (void)panSmoother_.process();
        (void)widthSmoother_.process();
        (void)lrOffsetSmoother_.process();
        (void)lrRatioSmoother_.process();

        // Read from delay lines first (before writing)
        float delayedL = delayL_.readLinear(delaySamples);
        float delayedR = delayR_.readLinear(delaySamples);

        // PingPong routing: input goes to L, L feeds R, R feeds L
        // Write current mono input to L delay
        delayL_.write(mono + delayedR * 0.5f);
        delayR_.write(delayedL * 0.5f);

        // Output
        leftOut[i] = delayedL;
        rightOut[i] = delayedR;
    }
}

inline void StereoField::processDualMono(const float* leftIn, const float* rightIn,
                                          float* leftOut, float* rightOut,
                                          size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        const float inL = sanitizeInput(leftIn[i]);
        const float inR = sanitizeInput(rightIn[i]);

        // Sum to mono
        const float mono = (inL + inR) * 0.5f;

        // Get smoothed parameters
        // Note: delaySmoother_ already stores samples (not ms)
        const float delaySamples = delaySmoother_.process();
        const float offsetSamples = lrOffsetSmoother_.process();
        const float panAmount = panSmoother_.process();

        // Advance unused smoothers
        (void)widthSmoother_.process();
        (void)lrRatioSmoother_.process();

        // Write to delay
        delayL_.write(mono);

        // Read with same delay time - both channels get same signal
        const float delayed = delayL_.readLinear(delaySamples);

        // FR-010: DualMono uses same delay time for both channels
        // L/R Offset adds timing difference (edge case: offset ignored if 0)
        float delayedL = delayed;
        float delayedR = delayed;

        // Always write to offset delay lines to keep them synchronized
        offsetDelayL_.write(delayed);
        offsetDelayR_.write(delayed);

        if (offsetSamples > 0.0f) {
            // Positive offset: R delayed relative to L
            delayedR = offsetDelayR_.readLinear(offsetSamples);
        } else if (offsetSamples < 0.0f) {
            // Negative offset: L delayed relative to R
            delayedL = offsetDelayL_.readLinear(-offsetSamples);
        }
        // If offsetSamples == 0, both stay as 'delayed' (read at 0 = current sample)

        // Apply constant-power pan separately to each channel's signal
        // DualMono outputs both signals independently with their respective timing
        float panL, panR;
        applyPan(delayedL, panAmount, panL, panR);
        leftOut[i] = panL;

        applyPan(delayedR, panAmount, panL, panR);
        rightOut[i] = panR;
    }
}

inline void StereoField::processMidSide(const float* leftIn, const float* rightIn,
                                         float* leftOut, float* rightOut,
                                         size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        const float inL = sanitizeInput(leftIn[i]);
        const float inR = sanitizeInput(rightIn[i]);

        // Encode to M/S
        const float mid = (inL + inR) * 0.5f;
        const float side = (inL - inR) * 0.5f;

        // Get smoothed parameters
        // Note: delaySmoother_ already stores samples (not ms)
        const float delaySamples = delaySmoother_.process();
        const float width = widthSmoother_.process();

        // Advance unused smoothers
        (void)panSmoother_.process();
        (void)lrOffsetSmoother_.process();
        (void)lrRatioSmoother_.process();

        // Delay Mid and Side independently
        delayL_.write(mid);
        delayR_.write(side);

        const float delayedMid = delayL_.readLinear(delaySamples);
        const float delayedSide = delayR_.readLinear(delaySamples) * width;

        // Decode back to L/R
        leftOut[i] = delayedMid + delayedSide;
        rightOut[i] = delayedMid - delayedSide;
    }
}

inline void StereoField::applyPan(float sample, float panAmount, float& left, float& right) noexcept {
    // Constant-power panning using sin/cos law (FR-020)
    // panAmount is -1 (full left) to +1 (full right)
    // Convert to 0-1 range for sin/cos
    const float panNorm = (panAmount + 1.0f) * 0.5f;

    // Pi/2 for quarter cycle
    constexpr float kHalfPi = 1.5707963267948966f;
    const float angle = panNorm * kHalfPi;

    // Constant-power: gainL = cos(angle), gainR = sin(angle)
    const float gainL = std::cos(angle);
    const float gainR = std::sin(angle);

    left = sample * gainL;
    right = sample * gainR;
}

inline float StereoField::sanitizeInput(float sample) const noexcept {
    // FR-019: NaN treated as 0.0
    if (detail::isNaN(sample)) {
        return 0.0f;
    }
    return sample;
}

inline float StereoField::msToSamples(float ms) const noexcept {
    return static_cast<float>(ms * sampleRate_ / 1000.0);
}

} // namespace DSP
} // namespace Iterum
