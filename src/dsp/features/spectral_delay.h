// ==============================================================================
// Layer 4: User Feature - SpectralDelay
// ==============================================================================
// Applies delay to individual frequency bands using STFT analysis/resynthesis.
// Creates ethereal, frequency-dependent echo effects where different frequency
// bands can have different delay times.
//
// Composes:
// - STFT, OverlapAdd (Layer 1): Spectral analysis/resynthesis
// - SpectralBuffer (Layer 1): Spectrum storage
// - DelayLine (Layer 1): Per-bin delay lines
// - OnePoleSmoother (Layer 1): Parameter smoothing
//
// Feature: 033-spectral-delay
// Layer: 4 (User Feature)
// Reference: specs/033-spectral-delay/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 4 (composes only from Layer 0-3)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include "dsp/core/block_context.h"
#include "dsp/core/db_utils.h"
#include "dsp/core/math_constants.h"
#include "dsp/core/note_value.h"
#include "dsp/core/random.h"
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/smoother.h"
#include "dsp/primitives/spectral_buffer.h"
#include "dsp/primitives/stft.h"
#include "dsp/systems/delay_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// SpreadDirection - Delay time distribution modes
// =============================================================================

/// @brief Spread direction modes for delay time distribution across frequency bins
enum class SpreadDirection : std::uint8_t {
    LowToHigh,  ///< Higher bins get longer delays (rising effect)
    HighToLow,  ///< Lower bins get longer delays (falling effect)
    CenterOut   ///< Edge bins get longer delays, center is base delay
};

/// @brief Spread curve modes for delay time distribution scaling
/// Phase 3.1: Linear vs logarithmic frequency distribution
enum class SpreadCurve : std::uint8_t {
    Linear,      ///< Linear distribution (perceptually less even)
    Logarithmic  ///< Logarithmic distribution (perceptually more even)
};

// =============================================================================
// SpectralDelay - Layer 4 User Feature
// =============================================================================

/// @brief Spectral delay effect using per-bin delay lines
///
/// Applies independent delay times to each frequency bin, creating unique
/// frequency-dependent echo effects. Features include:
/// - Configurable FFT size (512-4096)
/// - Per-bin delay with spread control
/// - Spectral freeze mode
/// - Frequency-dependent feedback with tilt
/// - Spectral diffusion/blur
///
/// @note Latency equals FFT size samples (analysis window fill time)
class SpectralDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr std::size_t kMinFFTSize = 512;
    static constexpr std::size_t kMaxFFTSize = 4096;
    static constexpr std::size_t kDefaultFFTSize = 1024;

    static constexpr float kMinDelayMs = 0.0f;
    static constexpr float kMaxDelayMs = 2000.0f;
    static constexpr float kDefaultDelayMs = 250.0f;

    static constexpr float kMinSpreadMs = 0.0f;
    static constexpr float kMaxSpreadMs = 2000.0f;

    static constexpr float kMinFeedback = 0.0f;
    static constexpr float kMaxFeedback = 1.2f;  // Allow slight overdrive

    static constexpr float kMinTilt = -1.0f;
    static constexpr float kMaxTilt = 1.0f;

    static constexpr float kMinDiffusion = 0.0f;
    static constexpr float kMaxDiffusion = 1.0f;

    static constexpr float kMinDryWet = 0.0f;
    static constexpr float kMaxDryWet = 100.0f;
    static constexpr float kDefaultDryWet = 50.0f;

    // Phase 3.2: Stereo width/decorrelation
    static constexpr float kMinStereoWidth = 0.0f;
    static constexpr float kMaxStereoWidth = 1.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    SpectralDelay() noexcept = default;
    ~SpectralDelay() = default;

    // Non-copyable, movable
    SpectralDelay(const SpectralDelay&) = delete;
    SpectralDelay& operator=(const SpectralDelay&) = delete;
    SpectralDelay(SpectralDelay&&) noexcept = default;
    SpectralDelay& operator=(SpectralDelay&&) noexcept = default;

    /// @brief Prepare for processing at given sample rate
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        hopSize_ = fftSize_ / 2;  // 50% overlap

        // Prepare STFT analysis (stereo)
        stftL_.prepare(fftSize_, hopSize_, WindowType::Hann);
        stftR_.prepare(fftSize_, hopSize_, WindowType::Hann);

        // Prepare overlap-add synthesis (stereo)
        overlapAddL_.prepare(fftSize_, hopSize_, WindowType::Hann);
        overlapAddR_.prepare(fftSize_, hopSize_, WindowType::Hann);

        // Prepare spectral buffers
        const std::size_t numBins = fftSize_ / 2 + 1;
        inputSpectrumL_.prepare(fftSize_);
        inputSpectrumR_.prepare(fftSize_);
        outputSpectrumL_.prepare(fftSize_);
        outputSpectrumR_.prepare(fftSize_);
        frozenSpectrumL_.prepare(fftSize_);
        frozenSpectrumR_.prepare(fftSize_);

        // Calculate max delay in seconds for delay lines
        const float maxDelaySeconds = kMaxDelayMs / 1000.0f;

        // Prepare per-bin delay lines (stereo)
        // Delay lines work at spectral frame rate (sampleRate / hopSize)
        const double frameRate = sampleRate / static_cast<double>(hopSize_);

        // Prepare complex delay lines (real + imaginary parts)
        binRealDelaysL_.clear();
        binRealDelaysR_.clear();
        binImagDelaysL_.clear();
        binImagDelaysR_.clear();
        binRealDelaysL_.reserve(numBins);
        binRealDelaysR_.reserve(numBins);
        binImagDelaysL_.reserve(numBins);
        binImagDelaysR_.reserve(numBins);

        for (std::size_t i = 0; i < numBins; ++i) {
            // Real part delay lines
            binRealDelaysL_.emplace_back();
            binRealDelaysR_.emplace_back();
            binRealDelaysL_.back().prepare(frameRate, maxDelaySeconds);
            binRealDelaysR_.back().prepare(frameRate, maxDelaySeconds);

            // Imaginary part delay lines
            binImagDelaysL_.emplace_back();
            binImagDelaysR_.emplace_back();
            binImagDelaysL_.back().prepare(frameRate, maxDelaySeconds);
            binImagDelaysR_.back().prepare(frameRate, maxDelaySeconds);
        }

        // Configure parameter smoothers (10ms smoothing time)
        const float smoothTimeMs = 10.0f;
        const float sampleRateF = static_cast<float>(sampleRate);
        baseDelaySmoother_.configure(smoothTimeMs, sampleRateF);
        spreadSmoother_.configure(smoothTimeMs, sampleRateF);
        feedbackSmoother_.configure(smoothTimeMs, sampleRateF);
        tiltSmoother_.configure(smoothTimeMs, sampleRateF);
        diffusionSmoother_.configure(smoothTimeMs, sampleRateF);
        dryWetSmoother_.configure(smoothTimeMs, sampleRateF);

        // Initialize smoothers to current values
        baseDelaySmoother_.setTarget(baseDelayMs_);
        spreadSmoother_.setTarget(spreadMs_);
        feedbackSmoother_.setTarget(feedback_);
        tiltSmoother_.setTarget(feedbackTilt_);
        diffusionSmoother_.setTarget(diffusion_);
        dryWetSmoother_.setTarget(dryWetMix_ / 100.0f);

        // Snap smoothers to initial values
        snapParameters();

        // Calculate freeze crossfade increment (per frame, not per sample)
        // Freeze operates at spectral frame rate
        freezeCrossfadeIncrement_ = static_cast<float>(hopSize_) /
                                    (kFreezeCrossfadeTimeMs * 0.001f *
                                     static_cast<float>(sampleRate));

        // Allocate temp buffers
        tempBufferL_.resize(maxBlockSize, 0.0f);
        tempBufferR_.resize(maxBlockSize, 0.0f);
        dryBufferL_.resize(maxBlockSize, 0.0f);
        dryBufferR_.resize(maxBlockSize, 0.0f);
        blurredMag_.resize(numBins, 0.0f);

        // Seed RNG with unique value per instance (address + sample rate)
        // This ensures different instances produce different random sequences
        rng_.seed(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this) ^
                                        static_cast<uintptr_t>(sampleRate)));

        prepared_ = true;
    }

    /// @brief Reset all internal state (delay lines, STFT buffers)
    void reset() noexcept {
        // Reset STFT
        stftL_.reset();
        stftR_.reset();

        // Reset overlap-add
        overlapAddL_.reset();
        overlapAddR_.reset();

        // Reset spectral buffers
        inputSpectrumL_.reset();
        inputSpectrumR_.reset();
        outputSpectrumL_.reset();
        outputSpectrumR_.reset();
        frozenSpectrumL_.reset();
        frozenSpectrumR_.reset();

        // Reset all per-bin complex delay lines (real parts)
        for (auto& delay : binRealDelaysL_) {
            delay.reset();
        }
        for (auto& delay : binRealDelaysR_) {
            delay.reset();
        }

        // Reset all per-bin complex delay lines (imaginary parts)
        for (auto& delay : binImagDelaysL_) {
            delay.reset();
        }
        for (auto& delay : binImagDelaysR_) {
            delay.reset();
        }

        // Reset freeze state
        wasFrozen_ = false;
        freezeCrossfade_ = 0.0f;

        // Clear temp buffers
        std::fill(tempBufferL_.begin(), tempBufferL_.end(), 0.0f);
        std::fill(tempBufferR_.begin(), tempBufferR_.end(), 0.0f);
        std::fill(dryBufferL_.begin(), dryBufferL_.end(), 0.0f);
        std::fill(dryBufferR_.begin(), dryBufferR_.end(), 0.0f);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio block
    /// @param left Left channel buffer (in-place)
    /// @param right Right channel buffer (in-place)
    /// @param numSamples Number of samples to process
    /// @param ctx Block processing context
    void process(float* left, float* right, std::size_t numSamples,
                 const BlockContext& ctx) noexcept {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples == 0) {
            return;
        }

        // Update base delay from tempo if in synced mode (spec 041, FR-003)
        if (timeMode_ == TimeMode::Synced) {
            // Get tempo with fallback to 120 BPM if unavailable (FR-007)
            double tempo = ctx.tempoBPM;
            if (tempo <= 0.0) {
                tempo = 120.0;  // Fallback default
            }

            // Calculate base delay from note value and tempo
            float syncedMs = dropdownToDelayMs(noteValueIndex_, tempo);

            // Clamp to max delay buffer (FR-006)
            syncedMs = std::clamp(syncedMs, kMinDelayMs, kMaxDelayMs);

            // Update smoother target for smooth transitions
            baseDelaySmoother_.setTarget(syncedMs);
        }
        // In Free mode (FR-004), base delay is set via setBaseDelayMs() - no change needed

        // Store dry signal for mixing
        std::copy(left, left + numSamples, dryBufferL_.begin());
        std::copy(right, right + numSamples, dryBufferR_.begin());

        // Push samples into STFT analyzers
        stftL_.pushSamples(left, numSamples);
        stftR_.pushSamples(right, numSamples);

        // Process spectral frames
        while (stftL_.canAnalyze() && stftR_.canAnalyze()) {
            // Analyze
            stftL_.analyze(inputSpectrumL_);
            stftR_.analyze(inputSpectrumR_);

            // Process the spectral frame
            processSpectralFrame(inputSpectrumL_, inputSpectrumR_,
                                 outputSpectrumL_, outputSpectrumR_);

            // Synthesize
            overlapAddL_.synthesize(outputSpectrumL_);
            overlapAddR_.synthesize(outputSpectrumR_);
        }

        // Pull processed samples
        const std::size_t availableL = overlapAddL_.samplesAvailable();
        const std::size_t availableR = overlapAddR_.samplesAvailable();
        const std::size_t toPull = std::min({numSamples, availableL, availableR});

        if (toPull > 0) {
            overlapAddL_.pullSamples(tempBufferL_.data(), toPull);
            overlapAddR_.pullSamples(tempBufferR_.data(), toPull);

            // Get smoothed parameters for this block
            const float wetMix = dryWetSmoother_.process();
            const float dryMix = 1.0f - wetMix;

            // Apply dry/wet mix
            for (std::size_t i = 0; i < toPull; ++i) {
                left[i] = dryBufferL_[i] * dryMix + tempBufferL_[i] * wetMix;
                right[i] = dryBufferR_[i] * dryMix + tempBufferR_[i] * wetMix;
            }

            // Zero remaining samples if we didn't get enough
            for (std::size_t i = toPull; i < numSamples; ++i) {
                left[i] = dryBufferL_[i] * dryMix;
                right[i] = dryBufferR_[i] * dryMix;
            }
        } else {
            // No processed samples available yet (latency filling)
            const float wetMix = dryWetSmoother_.process();
            const float dryMix = 1.0f - wetMix;

            for (std::size_t i = 0; i < numSamples; ++i) {
                left[i] = dryBufferL_[i] * dryMix;
                right[i] = dryBufferR_[i] * dryMix;
            }
        }

        // Advance smoothers (they process per-sample but we only need one value per block)
        // The values above were already advanced by the process() calls
    }

    // =========================================================================
    // FFT Configuration
    // =========================================================================

    /// @brief Set FFT size (must call prepare() after changing)
    /// @param fftSize FFT size (512, 1024, 2048, or 4096)
    void setFFTSize(std::size_t fftSize) noexcept {
        fftSize_ = std::clamp(fftSize, kMinFFTSize, kMaxFFTSize);
        // Round to nearest power of 2
        std::size_t pow2 = 1;
        while (pow2 < fftSize_) pow2 <<= 1;
        fftSize_ = pow2;
        // Caller must call prepare() after this
    }

    /// @brief Get current FFT size
    [[nodiscard]] std::size_t getFFTSize() const noexcept { return fftSize_; }

    // =========================================================================
    // Delay Controls
    // =========================================================================

    /// @brief Set base delay time in milliseconds
    void setBaseDelayMs(float ms) noexcept {
        baseDelayMs_ = std::clamp(ms, kMinDelayMs, kMaxDelayMs);
        baseDelaySmoother_.setTarget(baseDelayMs_);
    }
    [[nodiscard]] float getBaseDelayMs() const noexcept { return baseDelayMs_; }

    /// @brief Set spread amount in milliseconds
    void setSpreadMs(float ms) noexcept {
        spreadMs_ = std::clamp(ms, kMinSpreadMs, kMaxSpreadMs);
        spreadSmoother_.setTarget(spreadMs_);
    }
    [[nodiscard]] float getSpreadMs() const noexcept { return spreadMs_; }

    /// @brief Set spread direction mode
    void setSpreadDirection(SpreadDirection dir) noexcept {
        spreadDirection_ = dir;
    }
    [[nodiscard]] SpreadDirection getSpreadDirection() const noexcept {
        return spreadDirection_;
    }

    /// @brief Set spread curve mode (Linear or Logarithmic)
    /// Phase 3.1: Logarithmic gives perceptually more even distribution
    void setSpreadCurve(SpreadCurve curve) noexcept {
        spreadCurve_ = curve;
    }
    [[nodiscard]] SpreadCurve getSpreadCurve() const noexcept {
        return spreadCurve_;
    }

    // =========================================================================
    // Feedback Controls
    // =========================================================================

    /// @brief Set global feedback amount (0.0 to 1.2)
    void setFeedback(float amount) noexcept {
        feedback_ = std::clamp(amount, kMinFeedback, kMaxFeedback);
        feedbackSmoother_.setTarget(feedback_);
    }
    [[nodiscard]] float getFeedback() const noexcept { return feedback_; }

    /// @brief Set feedback tilt (-1.0 to +1.0)
    /// Negative = more low-frequency feedback, Positive = more high-frequency
    void setFeedbackTilt(float tilt) noexcept {
        feedbackTilt_ = std::clamp(tilt, kMinTilt, kMaxTilt);
        tiltSmoother_.setTarget(feedbackTilt_);
    }
    [[nodiscard]] float getFeedbackTilt() const noexcept { return feedbackTilt_; }

    // =========================================================================
    // Freeze
    // =========================================================================

    /// @brief Enable/disable spectral freeze
    void setFreezeEnabled(bool enabled) noexcept {
        freezeEnabled_ = enabled;
    }
    [[nodiscard]] bool isFreezeEnabled() const noexcept { return freezeEnabled_; }

    // =========================================================================
    // Diffusion
    // =========================================================================

    /// @brief Set diffusion amount (0.0 to 1.0)
    void setDiffusion(float amount) noexcept {
        diffusion_ = std::clamp(amount, kMinDiffusion, kMaxDiffusion);
        diffusionSmoother_.setTarget(diffusion_);
    }
    [[nodiscard]] float getDiffusion() const noexcept { return diffusion_; }

    // =========================================================================
    // Output
    // =========================================================================

    /// @brief Set dry/wet mix (0 to 100 percent)
    void setDryWetMix(float percent) noexcept {
        dryWetMix_ = std::clamp(percent, kMinDryWet, kMaxDryWet);
        dryWetSmoother_.setTarget(dryWetMix_ / 100.0f);
    }
    [[nodiscard]] float getDryWetMix() const noexcept { return dryWetMix_; }

    // =========================================================================
    // Stereo Width (Phase 3.2)
    // =========================================================================

    /// @brief Set stereo width/decorrelation amount (0.0 to 1.0)
    /// 0.0 = mono-like processing, 1.0 = full stereo decorrelation
    void setStereoWidth(float amount) noexcept {
        stereoWidth_ = std::clamp(amount, kMinStereoWidth, kMaxStereoWidth);
    }
    [[nodiscard]] float getStereoWidth() const noexcept { return stereoWidth_; }

    // =========================================================================
    // Tempo Sync (spec 041)
    // =========================================================================

    /// @brief Set time mode: 0 = Free (ms), 1 = Synced (note value + tempo)
    /// @param mode 0 for Free, 1 for Synced
    void setTimeMode(int mode) noexcept {
        timeMode_ = (mode == 1) ? TimeMode::Synced : TimeMode::Free;
    }
    [[nodiscard]] TimeMode getTimeMode() const noexcept { return timeMode_; }

    /// @brief Set note value index for tempo sync (0-9)
    /// Maps to: 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4, 1/2T, 1/2, 1/1
    /// @param index Dropdown index (0-9), default 4 = 1/8 note
    void setNoteValue(int index) noexcept {
        noteValueIndex_ = std::clamp(index, 0, 9);
    }
    [[nodiscard]] int getNoteValue() const noexcept { return noteValueIndex_; }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get latency in samples (equals FFT size)
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return fftSize_;
    }

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief Snap all smoothers to target for instant parameter changes
    void snapParameters() noexcept {
        baseDelaySmoother_.snapToTarget();
        spreadSmoother_.snapToTarget();
        feedbackSmoother_.snapToTarget();
        tiltSmoother_.snapToTarget();
        diffusionSmoother_.snapToTarget();
        dryWetSmoother_.snapToTarget();
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Calculate delay time for a specific bin based on spread settings
    [[nodiscard]] float calculateBinDelayMs(std::size_t bin,
                                            std::size_t numBins,
                                            float baseDelay,
                                            float spread) const noexcept {
        if (numBins <= 1) return baseDelay;

        float normalizedBin = static_cast<float>(bin) /
                              static_cast<float>(numBins - 1);

        // Phase 3.1: Apply logarithmic curve if selected
        // Log scaling gives more perceptually even distribution across frequencies
        if (spreadCurve_ == SpreadCurve::Logarithmic) {
            // Map linear 0-1 to logarithmic 0-1
            // log2(1 + x) / log2(2) = log2(1 + x) where x ranges 0-1
            // This maps 0->0, 0.5->0.585, 1->1 with emphasis on lower values
            constexpr float kLogBase = 2.0f;
            normalizedBin = std::log2(1.0f + normalizedBin * (kLogBase - 1.0f));
        }

        float delayOffset = 0.0f;

        switch (spreadDirection_) {
            case SpreadDirection::LowToHigh:
                delayOffset = normalizedBin * spread;
                break;
            case SpreadDirection::HighToLow:
                delayOffset = (1.0f - normalizedBin) * spread;
                break;
            case SpreadDirection::CenterOut:
                delayOffset = std::abs(normalizedBin - 0.5f) * 2.0f * spread;
                break;
        }

        return baseDelay + delayOffset;
    }

    /// @brief Calculate tilted feedback gain for a specific bin
    [[nodiscard]] float calculateTiltedFeedback(std::size_t bin,
                                                std::size_t numBins,
                                                float globalFeedback,
                                                float tilt) const noexcept {
        if (numBins <= 1) return globalFeedback;

        const float normalizedBin = static_cast<float>(bin) /
                                    static_cast<float>(numBins - 1);
        // tilt: -1.0 = full low bias, 0.0 = uniform, +1.0 = full high bias
        const float tiltFactor = 1.0f + tilt * (normalizedBin - 0.5f) * 2.0f;
        return std::clamp(globalFeedback * tiltFactor, 0.0f, kMaxFeedback);
    }

    /// @brief Apply diffusion blur to magnitude spectrum
    void applyDiffusion(const SpectralBuffer& input,
                        float diffusionAmount) noexcept {
        const std::size_t numBins = input.numBins();
        if (numBins < 3 || diffusionAmount < 0.001f) return;

        // 3-tap blur kernel
        const float side = diffusionAmount * 0.25f;
        const float center = 1.0f - diffusionAmount * 0.5f;

        // Store original magnitudes
        for (std::size_t i = 0; i < numBins; ++i) {
            blurredMag_[i] = input.getMagnitude(i);
        }

        // Apply blur (skip edges)
        for (std::size_t i = 1; i < numBins - 1; ++i) {
            blurredMag_[i] = input.getMagnitude(i - 1) * side +
                             input.getMagnitude(i) * center +
                             input.getMagnitude(i + 1) * side;
        }
    }

    /// @brief Process one spectral frame
    void processSpectralFrame(SpectralBuffer& inputL, SpectralBuffer& inputR,
                              SpectralBuffer& outputL, SpectralBuffer& outputR) noexcept {
        const std::size_t numBins = inputL.numBins();
        if (numBins == 0) return;

        // Get smoothed parameters
        const float baseDelay = baseDelaySmoother_.process();
        const float spread = spreadSmoother_.process();
        const float feedback = feedbackSmoother_.process();
        const float tilt = tiltSmoother_.process();
        const float diffusion = diffusionSmoother_.process();

        // Handle freeze transition
        const bool freezing = freezeEnabled_;
        if (freezing && !wasFrozen_) {
            // Just entered freeze: capture current spectrum
            for (std::size_t i = 0; i < numBins; ++i) {
                frozenSpectrumL_.setMagnitude(i, inputL.getMagnitude(i));
                frozenSpectrumL_.setPhase(i, inputL.getPhase(i));
                frozenSpectrumR_.setMagnitude(i, inputR.getMagnitude(i));
                frozenSpectrumR_.setPhase(i, inputR.getPhase(i));
            }
            freezeCrossfade_ = 0.0f;
            freezePhaseDrift_ = 0.0f;  // Reset phase drift when entering freeze
        }
        wasFrozen_ = freezing;

        // Update freeze crossfade
        if (freezing && freezeCrossfade_ < 1.0f) {
            freezeCrossfade_ = std::min(1.0f, freezeCrossfade_ + freezeCrossfadeIncrement_);
        } else if (!freezing && freezeCrossfade_ > 0.0f) {
            freezeCrossfade_ = std::max(0.0f, freezeCrossfade_ - freezeCrossfadeIncrement_);
        }

        // Phase 2.2: Accumulate phase drift when fully frozen
        // This prevents static resonance by slowly evolving the frozen spectrum
        if (freezing && freezeCrossfade_ >= 0.99f) {
            freezePhaseDrift_ += kPhaseDriftRate;
            // Wrap to prevent float precision issues over long freezes
            if (freezePhaseDrift_ > kTwoPi) {
                freezePhaseDrift_ -= kTwoPi;
            }
        }

        // Process each bin
        for (std::size_t bin = 0; bin < numBins; ++bin) {
            // Calculate per-bin delay time in frames
            const float binDelayMs = calculateBinDelayMs(bin, numBins, baseDelay, spread);
            const float frameRate = static_cast<float>(sampleRate_) /
                                    static_cast<float>(hopSize_);
            const float delayFrames = (binDelayMs / 1000.0f) * frameRate;

            // Calculate tilted feedback for this bin
            const float binFeedback = calculateTiltedFeedback(bin, numBins,
                                                              feedback, tilt);

            // Get input magnitude and phase
            float inputMagL = inputL.getMagnitude(bin);
            float inputMagR = inputR.getMagnitude(bin);
            const float inputPhaseL = inputL.getPhase(bin);
            const float inputPhaseR = inputR.getPhase(bin);

            // Convert input to complex (real + imaginary) for delay line storage
            // This avoids phase wrapping issues during linear interpolation
            const float inputRealL = inputMagL * std::cos(inputPhaseL);
            const float inputImagL = inputMagL * std::sin(inputPhaseL);
            const float inputRealR = inputMagR * std::cos(inputPhaseR);
            const float inputImagR = inputMagR * std::sin(inputPhaseR);

            // Read delayed complex values from delay lines (linear interpolation is safe here)
            const float delayedRealL = binRealDelaysL_[bin].readLinear(delayFrames);
            const float delayedImagL = binImagDelaysL_[bin].readLinear(delayFrames);
            const float delayedRealR = binRealDelaysR_[bin].readLinear(delayFrames);
            const float delayedImagR = binImagDelaysR_[bin].readLinear(delayFrames);

            // Convert delayed complex back to magnitude and phase
            const float delayedMagL = std::sqrt(delayedRealL * delayedRealL +
                                                 delayedImagL * delayedImagL);
            const float delayedMagR = std::sqrt(delayedRealR * delayedRealR +
                                                 delayedImagR * delayedImagR);
            const float delayedPhaseL = std::atan2(delayedImagL, delayedRealL);
            const float delayedPhaseR = std::atan2(delayedImagR, delayedRealR);

            // Apply feedback: calculate feedback magnitude (for soft limiting)
            float feedbackMagL = delayedMagL * binFeedback;
            float feedbackMagR = delayedMagR * binFeedback;
            if (binFeedback > 1.0f) {
                feedbackMagL = std::tanh(feedbackMagL);
                feedbackMagR = std::tanh(feedbackMagR);
            }

            // Convert feedback to complex using delayed phase
            const float feedbackRealL = feedbackMagL * std::cos(delayedPhaseL);
            const float feedbackImagL = feedbackMagL * std::sin(delayedPhaseL);
            const float feedbackRealR = feedbackMagR * std::cos(delayedPhaseR);
            const float feedbackImagR = feedbackMagR * std::sin(delayedPhaseR);

            // Only write to delay lines when not frozen
            // This ensures freeze truly ignores new input
            if (!freezing) {
                // Write complex values (input + feedback) to delay lines
                binRealDelaysL_[bin].write(inputRealL + feedbackRealL);
                binImagDelaysL_[bin].write(inputImagL + feedbackImagL);
                binRealDelaysR_[bin].write(inputRealR + feedbackRealR);
                binImagDelaysR_[bin].write(inputImagR + feedbackImagR);
            }

            // Output is the delayed magnitude and phase
            float outMagL = delayedMagL;
            float outMagR = delayedMagR;
            float outPhaseL = delayedPhaseL;
            float outPhaseR = delayedPhaseR;

            // Apply freeze crossfade if active
            if (freezeCrossfade_ > 0.0f) {
                const float frozenMagL = frozenSpectrumL_.getMagnitude(bin);
                const float frozenMagR = frozenSpectrumR_.getMagnitude(bin);
                float frozenPhaseL = frozenSpectrumL_.getPhase(bin);
                float frozenPhaseR = frozenSpectrumR_.getPhase(bin);

                // Phase 2.2: Apply phase drift per-bin with slight frequency variation
                // Higher bins drift slightly faster for more natural evolution
                if (freezePhaseDrift_ > 0.0f) {
                    const float binFactor = 1.0f + 0.5f * static_cast<float>(bin) /
                                                   static_cast<float>(numBins);
                    frozenPhaseL += freezePhaseDrift_ * binFactor;
                    frozenPhaseR += freezePhaseDrift_ * binFactor;
                }

                outMagL = outMagL * (1.0f - freezeCrossfade_) +
                          frozenMagL * freezeCrossfade_;
                outMagR = outMagR * (1.0f - freezeCrossfade_) +
                          frozenMagR * freezeCrossfade_;

                // When fully frozen (crossfade >= 0.99), use frozen phase (with drift)
                // to ensure new input has no effect on output
                if (freezeCrossfade_ >= 0.99f) {
                    outPhaseL = frozenPhaseL;
                    outPhaseR = frozenPhaseR;
                }
            }

            // Phase 3.2: Apply stereo decorrelation
            // Add bin-dependent phase offset between L and R for stereo width
            if (stereoWidth_ > 0.001f) {
                // Create frequency-dependent phase difference between L and R
                // Use random offsets that differ between channels to break correlation
                const float randomOffsetL = rng_.nextFloat() * stereoWidth_ * kHalfPi;
                const float randomOffsetR = rng_.nextFloat() * stereoWidth_ * kHalfPi;
                outPhaseL += randomOffsetL;
                outPhaseR -= randomOffsetR;  // Opposite direction for maximum decorrelation
            }

            // Apply phase randomization when diffusion is enabled
            // Random phase offsets create spectral smearing/decorrelation
            // Phase 2.1: True diffusion requires phase randomization, not just magnitude blur
            if (diffusion > 0.001f) {
                // Max phase offset scales from 0 to ±π as diffusion goes 0→1
                const float maxPhaseOffset = diffusion * kPi;
                outPhaseL += rng_.nextFloat() * maxPhaseOffset;
                outPhaseR += rng_.nextFloat() * maxPhaseOffset;
            }

            // Set output spectrum
            outputL.setMagnitude(bin, outMagL);
            outputL.setPhase(bin, outPhaseL);
            outputR.setMagnitude(bin, outMagR);
            outputR.setPhase(bin, outPhaseR);
        }

        // Apply diffusion magnitude blur if enabled
        // This spreads energy across neighboring frequency bins
        if (diffusion > 0.001f) {
            applyDiffusion(outputL, diffusion);
            for (std::size_t i = 0; i < numBins; ++i) {
                outputL.setMagnitude(i, blurredMag_[i]);
            }
            applyDiffusion(outputR, diffusion);
            for (std::size_t i = 0; i < numBins; ++i) {
                outputR.setMagnitude(i, blurredMag_[i]);
            }
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // Random number generator for phase randomization (unique seed per instance)
    Xorshift32 rng_{1};

    // FFT Configuration
    std::size_t fftSize_ = kDefaultFFTSize;
    std::size_t hopSize_ = kDefaultFFTSize / 2;  // 50% overlap

    // STFT Analysis (stereo)
    STFT stftL_;
    STFT stftR_;

    // Overlap-Add Synthesis (stereo)
    OverlapAdd overlapAddL_;
    OverlapAdd overlapAddR_;

    // Spectral Buffers
    SpectralBuffer inputSpectrumL_;
    SpectralBuffer inputSpectrumR_;
    SpectralBuffer outputSpectrumL_;
    SpectralBuffer outputSpectrumR_;
    SpectralBuffer frozenSpectrumL_;
    SpectralBuffer frozenSpectrumR_;

    // Per-Bin Delay Lines for Complex Values (stereo)
    // We delay real and imaginary parts instead of magnitude and phase
    // to avoid phase wrapping issues during linear interpolation.
    // When phase wraps from +π to -π, linear interpolation produces incorrect
    // values (e.g., interp(3.1, -3.1, 0.5) = 0.0 instead of ~±π).
    // Complex interpolation handles this correctly.
    std::vector<DelayLine> binRealDelaysL_;
    std::vector<DelayLine> binRealDelaysR_;
    std::vector<DelayLine> binImagDelaysL_;
    std::vector<DelayLine> binImagDelaysR_;

    // Parameters
    float baseDelayMs_ = kDefaultDelayMs;
    float spreadMs_ = 0.0f;
    SpreadDirection spreadDirection_ = SpreadDirection::LowToHigh;
    SpreadCurve spreadCurve_ = SpreadCurve::Linear;  // Phase 3.1: Default to linear
    float feedback_ = 0.0f;
    float feedbackTilt_ = 0.0f;
    float diffusion_ = 0.0f;
    float dryWetMix_ = kDefaultDryWet;
    float stereoWidth_ = 0.0f;  // Phase 3.2: Stereo decorrelation amount
    bool freezeEnabled_ = false;

    // Tempo Sync State (spec 041)
    TimeMode timeMode_ = TimeMode::Free;  // Default to free (ms) mode
    int noteValueIndex_ = 4;              // Default to 1/8 note (index 4)

    // Parameter Smoothers
    OnePoleSmoother baseDelaySmoother_;
    OnePoleSmoother spreadSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother tiltSmoother_;
    OnePoleSmoother diffusionSmoother_;
    OnePoleSmoother dryWetSmoother_;

    // Freeze State
    bool wasFrozen_ = false;
    float freezeCrossfade_ = 0.0f;
    static constexpr float kFreezeCrossfadeTimeMs = 75.0f;  // 50-100ms per spec
    float freezeCrossfadeIncrement_ = 0.0f;

    // Phase 2.2: Phase drift during freeze to prevent static resonance
    // Accumulates slowly over time when frozen to add subtle variation
    float freezePhaseDrift_ = 0.0f;
    static constexpr float kPhaseDriftRate = 0.01f;  // Radians per frame (slow drift)

    // Internal Buffers
    std::vector<float> tempBufferL_;
    std::vector<float> tempBufferR_;
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;
    std::vector<float> blurredMag_;
};

}  // namespace DSP
}  // namespace Iterum
