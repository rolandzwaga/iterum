// ==============================================================================
// Layer 3: System Component - CharacterProcessor
// ==============================================================================
// Applies analog character/coloration to audio signal with multiple modes:
// - Tape: Saturation, wow/flutter, hiss, high-frequency rolloff
// - BBD: Bandwidth limiting, clock noise, soft saturation
// - DigitalVintage: Bit depth and sample rate reduction
// - Clean: Unity gain passthrough
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 3 (composes Layer 0-2 components)
// - Principle X: DSP Constraints (crossfade transitions, parameter smoothing)
// - Principle XII: Test-First Development
//
// Reference: specs/021-character-processor/spec.md
// ==============================================================================

#pragma once

#include "dsp/primitives/lfo.h"
#include "dsp/primitives/smoother.h"
#include "dsp/primitives/bit_crusher.h"
#include "dsp/primitives/sample_rate_reducer.h"
#include "dsp/processors/saturation_processor.h"
#include "dsp/processors/noise_generator.h"
#include "dsp/processors/multimode_filter.h"
#include "dsp/core/db_utils.h"
#include "dsp/core/crossfade_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// CharacterMode Enumeration
// =============================================================================

/// @brief Character mode selection
///
/// Each mode provides distinct analog character:
/// - Clean: Transparent passthrough (bypass)
/// - Tape: Warm saturation, wow/flutter, hiss, HF rolloff
/// - BBD: Bucket-brigade delay character with bandwidth limiting
/// - DigitalVintage: Lo-fi bit reduction and sample rate reduction
enum class CharacterMode : uint8_t {
    Clean = 0,        ///< Unity gain passthrough
    Tape = 1,         ///< Tape machine emulation
    BBD = 2,          ///< Bucket-brigade device emulation
    DigitalVintage = 3 ///< Early digital/sampler character
};

// =============================================================================
// CharacterProcessor Class
// =============================================================================

/// @brief Layer 3 System Component - Analog character processor
///
/// Composes Layer 1-2 DSP components to provide four distinct character modes.
/// Features 50ms equal-power crossfade between modes for click-free transitions.
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, RAII)
/// - Principle IX: Layer 3 (composes Layer 0-2 components)
/// - Principle X: DSP Constraints (crossfade, smoothing)
///
/// @par Usage
/// @code
/// CharacterProcessor character;
/// character.prepare(44100.0, 512);
/// character.setMode(CharacterMode::Tape);
/// character.setTapeSaturation(0.5f);
///
/// // In process callback
/// character.process(buffer, numSamples);
/// @endcode
class CharacterProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kCrossfadeTimeMs = 50.0f;     ///< Mode crossfade time
    static constexpr float kSmoothingTimeMs = 20.0f;     ///< Parameter smoothing time
    static constexpr float kDefaultTapeSaturation = 0.3f;
    static constexpr float kDefaultTapeHissLevel = -60.0f;
    static constexpr float kDefaultTapeRolloff = 12000.0f;
    static constexpr float kDefaultWowRate = 0.5f;
    static constexpr float kDefaultWowDepth = 0.0f;
    static constexpr float kDefaultFlutterRate = 5.0f;
    static constexpr float kDefaultFlutterDepth = 0.0f;
    static constexpr float kDefaultBBDBandwidth = 10000.0f;
    static constexpr float kDefaultBBDSaturation = 0.2f;
    static constexpr float kDefaultBBDClockNoise = -70.0f;
    static constexpr float kDefaultDigitalBitDepth = 16.0f;
    static constexpr float kDefaultDigitalSampleRateReduction = 1.0f;
    static constexpr float kDefaultDigitalDither = 0.5f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    CharacterProcessor() noexcept = default;

    /// @brief Prepare for processing
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process call
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Calculate crossfade increment (samples to reach 1.0)
        crossfadeSamples_ = static_cast<size_t>(sampleRate_ * kCrossfadeTimeMs / 1000.0);
        if (crossfadeSamples_ < 1) crossfadeSamples_ = 1;
        crossfadeIncrement_ = 1.0f / static_cast<float>(crossfadeSamples_);

        // Prepare sub-components
        // Tape mode components
        tapeSaturation_.prepare(sampleRate, maxBlockSize);
        tapeSaturation_.setType(SaturationType::Tape);
        tapeSaturation_.setMix(1.0f);

        tapeHiss_.prepare(static_cast<float>(sampleRate), maxBlockSize);
        tapeHiss_.setNoiseEnabled(NoiseType::TapeHiss, true);
        tapeHiss_.setNoiseLevel(NoiseType::TapeHiss, kDefaultTapeHissLevel);

        tapeRolloff_.prepare(sampleRate, maxBlockSize);
        tapeRolloff_.setType(FilterType::Lowpass);
        tapeRolloff_.setCutoff(kDefaultTapeRolloff);
        tapeRolloff_.setResonance(0.707f);

        wowLfo_.prepare(sampleRate);
        wowLfo_.setWaveform(Waveform::Sine);
        wowLfo_.setFrequency(kDefaultWowRate);

        flutterLfo_.prepare(sampleRate);
        flutterLfo_.setWaveform(Waveform::Sine);
        flutterLfo_.setFrequency(kDefaultFlutterRate);

        // BBD mode components
        bbdSaturation_.prepare(sampleRate, maxBlockSize);
        bbdSaturation_.setType(SaturationType::Tape);
        bbdSaturation_.setMix(1.0f);

        bbdBandwidth_.prepare(sampleRate, maxBlockSize);
        bbdBandwidth_.setType(FilterType::Lowpass);
        bbdBandwidth_.setCutoff(kDefaultBBDBandwidth);
        bbdBandwidth_.setResonance(0.707f);
        bbdBandwidth_.setSlope(FilterSlope::Slope24dB); // Steeper rolloff

        bbdClockNoise_.prepare(static_cast<float>(sampleRate), maxBlockSize);
        bbdClockNoise_.setNoiseEnabled(NoiseType::RadioStatic, true); // High-frequency noise
        bbdClockNoise_.setNoiseLevel(NoiseType::RadioStatic, kDefaultBBDClockNoise);

        // Digital vintage components
        bitCrusher_.prepare(sampleRate);
        bitCrusher_.setBitDepth(kDefaultDigitalBitDepth);
        bitCrusher_.setDither(kDefaultDigitalDither);

        sampleRateReducer_.prepare(sampleRate);
        sampleRateReducer_.setReductionFactor(kDefaultDigitalSampleRateReduction);

        // Parameter smoothers
        tapeSaturationSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        tapeSaturationSmoother_.setTarget(kDefaultTapeSaturation);
        tapeSaturationSmoother_.snapTo(kDefaultTapeSaturation);

        bbdSaturationSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
        bbdSaturationSmoother_.setTarget(kDefaultBBDSaturation);
        bbdSaturationSmoother_.snapTo(kDefaultBBDSaturation);

        // Allocate work buffers
        workBufferL_.resize(maxBlockSize);
        workBufferR_.resize(maxBlockSize);
        previousModeBufferL_.resize(maxBlockSize);
        previousModeBufferR_.resize(maxBlockSize);
        noiseBuffer_.resize(maxBlockSize);

        reset();
    }

    /// @brief Reset internal state
    void reset() noexcept {
        crossfadePosition_ = 1.0f; // Not crossfading
        previousMode_ = currentMode_;

        tapeSaturation_.reset();
        tapeHiss_.reset();
        tapeRolloff_.reset();
        wowLfo_.reset();
        flutterLfo_.reset();

        bbdSaturation_.reset();
        bbdBandwidth_.reset();
        bbdClockNoise_.reset();

        bitCrusher_.reset();
        sampleRateReducer_.reset();

        std::fill(workBufferL_.begin(), workBufferL_.end(), 0.0f);
        std::fill(workBufferR_.begin(), workBufferR_.end(), 0.0f);
        std::fill(previousModeBufferL_.begin(), previousModeBufferL_.end(), 0.0f);
        std::fill(previousModeBufferR_.begin(), previousModeBufferR_.end(), 0.0f);
        std::fill(noiseBuffer_.begin(), noiseBuffer_.end(), 0.0f);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process mono audio in-place
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void process(float* buffer, size_t numSamples) noexcept {
        if (numSamples == 0) return;

        // Process in chunks of maxBlockSize_ for safety
        size_t offset = 0;
        while (offset < numSamples) {
            size_t chunkSize = std::min(numSamples - offset, maxBlockSize_);
            processChunk(buffer + offset, chunkSize);
            offset += chunkSize;
        }
    }

private:
    /// @brief Process a single chunk (must be <= maxBlockSize_)
    void processChunk(float* buffer, size_t numSamples) noexcept {
        // Handle NaN inputs
        for (size_t i = 0; i < numSamples; ++i) {
            if (!std::isfinite(buffer[i])) {
                buffer[i] = 0.0f;
            }
        }

        // If crossfading, process both modes and blend
        if (isCrossfading()) {
            // Copy input for previous mode processing
            std::copy(buffer, buffer + numSamples, previousModeBufferL_.data());

            // Process current mode
            processMode(buffer, numSamples, currentMode_);

            // Process previous mode
            processMode(previousModeBufferL_.data(), numSamples, previousMode_);

            // Crossfade between modes (equal power) using Layer 0 utility
            for (size_t i = 0; i < numSamples; ++i) {
                float fadeOut, fadeIn;
                equalPowerGains(crossfadePosition_, fadeOut, fadeIn);

                buffer[i] = previousModeBufferL_[i] * fadeOut + buffer[i] * fadeIn;

                crossfadePosition_ += crossfadeIncrement_;
                if (crossfadePosition_ >= 1.0f) {
                    crossfadePosition_ = 1.0f;
                    break;
                }
            }
        } else {
            processMode(buffer, numSamples, currentMode_);
        }
    }

public:

    /// @brief Process stereo audio in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    void processStereo(float* left, float* right, size_t numSamples) noexcept {
        // Process each channel independently for now
        // (A more sophisticated implementation could share modulation sources)
        process(left, numSamples);
        process(right, numSamples);
    }

    // =========================================================================
    // Mode Selection
    // =========================================================================

    /// @brief Set the character mode
    /// @param mode New mode to switch to
    void setMode(CharacterMode mode) noexcept {
        if (mode != currentMode_) {
            previousMode_ = currentMode_;
            currentMode_ = mode;
            crossfadePosition_ = 0.0f; // Start crossfade
        }
    }

    /// @brief Get current mode
    [[nodiscard]] CharacterMode getMode() const noexcept {
        return currentMode_;
    }

    /// @brief Check if currently crossfading between modes
    [[nodiscard]] bool isCrossfading() const noexcept {
        return crossfadePosition_ < 1.0f;
    }

    /// @brief Get sample rate
    [[nodiscard]] double getSampleRate() const noexcept {
        return sampleRate_;
    }

    // =========================================================================
    // Tape Mode Parameters
    // =========================================================================

    /// @brief Set tape saturation amount [0, 1]
    void setTapeSaturation(float amount) noexcept {
        amount = std::clamp(amount, 0.0f, 1.0f);
        tapeSaturationSmoother_.setTarget(amount);
    }

    /// @brief Set tape hiss level in dB
    void setTapeHissLevel(float levelDb) noexcept {
        tapeHiss_.setNoiseLevel(NoiseType::TapeHiss, levelDb);
    }

    /// @brief Set tape high-frequency rolloff frequency
    void setTapeRolloffFreq(float freqHz) noexcept {
        tapeRolloff_.setCutoff(freqHz);
    }

    /// @brief Set wow modulation rate (Hz)
    void setTapeWowRate(float rateHz) noexcept {
        wowLfo_.setFrequency(rateHz);
    }

    /// @brief Set wow modulation depth [0, 1]
    void setTapeWowDepth(float depth) noexcept {
        wowDepth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    /// @brief Set flutter modulation rate (Hz)
    void setTapeFlutterRate(float rateHz) noexcept {
        flutterLfo_.setFrequency(rateHz);
    }

    /// @brief Set flutter modulation depth [0, 1]
    void setTapeFlutterDepth(float depth) noexcept {
        flutterDepth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    // =========================================================================
    // BBD Mode Parameters
    // =========================================================================

    /// @brief Set BBD bandwidth limit (Hz)
    void setBBDBandwidth(float freqHz) noexcept {
        bbdBandwidth_.setCutoff(freqHz);
    }

    /// @brief Set BBD saturation amount [0, 1]
    void setBBDSaturation(float amount) noexcept {
        amount = std::clamp(amount, 0.0f, 1.0f);
        bbdSaturationSmoother_.setTarget(amount);
    }

    /// @brief Set BBD clock noise level in dB
    void setBBDClockNoiseLevel(float levelDb) noexcept {
        bbdClockNoise_.setNoiseLevel(NoiseType::RadioStatic, levelDb);
    }

    // =========================================================================
    // Digital Vintage Mode Parameters
    // =========================================================================

    /// @brief Set bit depth [4, 16]
    void setDigitalBitDepth(float bits) noexcept {
        bitCrusher_.setBitDepth(bits);
    }

    /// @brief Set sample rate reduction factor [1, 8]
    void setDigitalSampleRateReduction(float factor) noexcept {
        sampleRateReducer_.setReductionFactor(factor);
    }

    /// @brief Set dither amount [0, 1]
    void setDigitalDitherAmount(float amount) noexcept {
        bitCrusher_.setDither(amount);
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Process audio through a specific mode
    void processMode(float* buffer, size_t numSamples, CharacterMode mode) noexcept {
        switch (mode) {
            case CharacterMode::Clean:
                processClean(buffer, numSamples);
                break;
            case CharacterMode::Tape:
                processTape(buffer, numSamples);
                break;
            case CharacterMode::BBD:
                processBBD(buffer, numSamples);
                break;
            case CharacterMode::DigitalVintage:
                processDigitalVintage(buffer, numSamples);
                break;
        }
    }

    /// @brief Process Clean mode (passthrough)
    void processClean(float* buffer, size_t numSamples) noexcept {
        // Unity gain passthrough - no processing
        (void)buffer;
        (void)numSamples;
    }

    /// @brief Process Tape mode
    void processTape(float* buffer, size_t numSamples) noexcept {
        // Apply wow/flutter modulation to amplitude (simplified)
        for (size_t i = 0; i < numSamples; ++i) {
            float wow = wowLfo_.process() * wowDepth_ * 0.02f;      // Max 2% variation
            float flutter = flutterLfo_.process() * flutterDepth_ * 0.01f; // Max 1% variation
            float modulation = 1.0f + wow + flutter;
            buffer[i] *= modulation;
        }

        // Update saturation drive from smoother
        float satAmount = tapeSaturationSmoother_.process();

        // Map saturation amount [0, 1] to drive for THD range ~0.1% to ~5%
        //
        // Empirically calibrated drive range (iteratively tuned):
        // - At 0% saturation: -17dB drive → THD ~0.1%
        // - At 100% saturation: +24dB drive → THD ~5%
        //
        // THD measured at 0.5 amplitude test signal through tanh saturation.
        // Note: THD growth slows at high drive due to tanh compression and
        // the saturation processor's DC blocker attenuating harmonics.
        float driveDb = -17.0f + satAmount * 41.0f;  // -17dB to +24dB (41dB span)
        tapeSaturation_.setInputGain(driveDb);

        // Apply makeup gain to maintain roughly unity output level
        // At low saturation: need full compensation for attenuation
        // At high saturation: tanh compresses heavily, need less makeup
        float makeupDb = -driveDb * (1.0f - satAmount * 0.75f);
        makeupDb = std::clamp(makeupDb, -10.0f, 18.0f);
        tapeSaturation_.setOutputGain(makeupDb);

        // Apply saturation
        tapeSaturation_.process(buffer, numSamples);

        // Apply high-frequency rolloff
        tapeRolloff_.process(buffer, numSamples);

        // Generate and add hiss
        tapeHiss_.process(noiseBuffer_.data(), numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] += noiseBuffer_[i];
        }
    }

    /// @brief Process BBD mode
    void processBBD(float* buffer, size_t numSamples) noexcept {
        // Apply bandwidth limiting first
        bbdBandwidth_.process(buffer, numSamples);

        // Update saturation from smoother
        float satAmount = bbdSaturationSmoother_.process();
        float driveDb = satAmount * 12.0f; // 0-12dB drive (softer than tape)
        bbdSaturation_.setInputGain(driveDb);

        // Apply soft saturation
        bbdSaturation_.process(buffer, numSamples);

        // Add clock noise
        bbdClockNoise_.process(noiseBuffer_.data(), numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] += noiseBuffer_[i];
        }
    }

    /// @brief Process Digital Vintage mode
    void processDigitalVintage(float* buffer, size_t numSamples) noexcept {
        // Apply sample rate reduction first (creates aliasing)
        sampleRateReducer_.process(buffer, numSamples);

        // Apply bit crushing
        bitCrusher_.process(buffer, numSamples);
    }

    // =========================================================================
    // Private Data
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;

    // Mode state
    CharacterMode currentMode_ = CharacterMode::Clean;
    CharacterMode previousMode_ = CharacterMode::Clean;

    // Crossfade state
    float crossfadePosition_ = 1.0f; // 1.0 = not crossfading
    float crossfadeIncrement_ = 0.0f;
    size_t crossfadeSamples_ = 2205; // 50ms at 44.1kHz

    // Tape mode components
    SaturationProcessor tapeSaturation_;
    NoiseGenerator tapeHiss_;
    MultimodeFilter tapeRolloff_;
    LFO wowLfo_;
    LFO flutterLfo_;
    float wowDepth_ = kDefaultWowDepth;
    float flutterDepth_ = kDefaultFlutterDepth;

    // BBD mode components
    SaturationProcessor bbdSaturation_;
    MultimodeFilter bbdBandwidth_;
    NoiseGenerator bbdClockNoise_;

    // Digital vintage components
    BitCrusher bitCrusher_;
    SampleRateReducer sampleRateReducer_;

    // Parameter smoothers
    OnePoleSmoother tapeSaturationSmoother_;
    OnePoleSmoother bbdSaturationSmoother_;

    // Work buffers
    std::vector<float> workBufferL_;
    std::vector<float> workBufferR_;
    std::vector<float> previousModeBufferL_;
    std::vector<float> previousModeBufferR_;
    std::vector<float> noiseBuffer_;
};

} // namespace DSP
} // namespace Iterum
