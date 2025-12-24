// ==============================================================================
// Layer 2: DSP Processor - Noise Generator
// ==============================================================================
// Generates various noise types for analog character and lo-fi effects:
// - White noise (flat spectrum)
// - Pink noise (-3dB/octave)
// - Tape hiss (signal-dependent)
// - Vinyl crackle (impulsive)
// - Asperity noise (tape head contact)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (sample-accurate processing)
// - Principle XII: Test-First Development
//
// Reference: specs/013-noise-generator/spec.md
// ==============================================================================

#pragma once

#include "dsp/core/db_utils.h"
#include "dsp/core/random.h"
#include "dsp/primitives/biquad.h"
#include "dsp/primitives/smoother.h"
#include "dsp/processors/envelope_follower.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// NoiseType Enumeration
// =============================================================================

/// @brief Noise generation algorithm types
enum class NoiseType : uint8_t {
    White = 0,      ///< Flat spectrum white noise
    Pink,           ///< -3dB/octave pink noise (Paul Kellet filter)
    TapeHiss,       ///< Signal-dependent tape hiss with high-frequency emphasis
    VinylCrackle,   ///< Impulsive clicks/pops with optional surface noise
    Asperity,       ///< Tape head contact noise varying with signal level
    Brown,          ///< -6dB/octave brown/red noise (integrated white noise)
    Blue,           ///< +3dB/octave blue noise (differentiated pink noise)
    Violet,         ///< +6dB/octave violet noise (differentiated white noise)
    Grey,           ///< Inverse A-weighting for perceptually flat loudness
    Velvet,         ///< Sparse random impulses for smooth noise character
    VinylRumble,    ///< Low-frequency motor/platter noise below 100Hz
    ModulationNoise,///< Signal-correlated noise (scales with input level, no floor)
    RadioStatic     ///< Band-limited atmospheric/interference noise (~5kHz cutoff)
};

/// @brief Number of noise types available
constexpr size_t kNumNoiseTypes = 13;

// =============================================================================
// PinkNoiseFilter (Internal)
// =============================================================================

/// @brief Paul Kellet's pink noise filter
///
/// Converts white noise to pink noise (-3dB/octave spectral rolloff).
/// Uses a 7-state recursive filter for excellent accuracy with minimal CPU.
///
/// @par Algorithm
/// Filter coefficients from Paul Kellet's "pink noise generation" article.
/// Accuracy: -3dB/octave ±0.5dB across audible range.
///
/// @par Reference
/// https://www.firstpr.com.au/dsp/pink-noise/
class PinkNoiseFilter {
public:
    /// @brief Process one white noise sample through the filter
    /// @param white Input white noise sample (typically [-1, 1])
    /// @return Pink noise sample
    [[nodiscard]] float process(float white) noexcept {
        // Paul Kellet's filter coefficients
        b0_ = 0.99886f * b0_ + white * 0.0555179f;
        b1_ = 0.99332f * b1_ + white * 0.0750759f;
        b2_ = 0.96900f * b2_ + white * 0.1538520f;
        b3_ = 0.86650f * b3_ + white * 0.3104856f;
        b4_ = 0.55000f * b4_ + white * 0.5329522f;
        b5_ = -0.7616f * b5_ - white * 0.0168980f;

        float pink = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f;
        b6_ = white * 0.115926f;

        // Normalize output to stay within [-1, 1] range
        // The filter has peak gain of approximately 5.0, so we use a conservative factor
        // and clamp to ensure we never exceed the range
        float normalized = pink * 0.2f;
        return (normalized > 1.0f) ? 1.0f : ((normalized < -1.0f) ? -1.0f : normalized);
    }

    /// @brief Reset filter state to zero
    void reset() noexcept {
        b0_ = b1_ = b2_ = b3_ = b4_ = b5_ = b6_ = 0.0f;
    }

private:
    float b0_ = 0.0f;
    float b1_ = 0.0f;
    float b2_ = 0.0f;
    float b3_ = 0.0f;
    float b4_ = 0.0f;
    float b5_ = 0.0f;
    float b6_ = 0.0f;
};

// =============================================================================
// NoiseGenerator Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Multi-type noise generator
///
/// Generates various noise types for analog character and lo-fi effects.
/// Supports independent level control per noise type, signal-dependent
/// modulation for tape hiss and asperity, and real-time safe processing.
///
/// @par Layer Dependencies
/// - Layer 0: db_utils (dbToGain, gainToDb), random (Xorshift32)
/// - Layer 1: Biquad (tape hiss shaping), OnePoleSmoother (level smoothing)
///
/// @par Real-Time Safety
/// - No memory allocation in process()
/// - All buffers pre-allocated in prepare()
/// - Lock-free parameter updates via smoothing
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends only on Layer 0/1)
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// NoiseGenerator noise;
/// noise.prepare(44100.0f, 512);
/// noise.setNoiseEnabled(NoiseType::White, true);
/// noise.setNoiseLevel(NoiseType::White, -20.0f);
/// noise.process(outputBuffer, numSamples);
/// @endcode
class NoiseGenerator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinLevelDb = -96.0f;
    static constexpr float kMaxLevelDb = 12.0f;
    static constexpr float kDefaultLevelDb = -20.0f;
    static constexpr float kMinCrackleDensity = 0.1f;
    static constexpr float kMaxCrackleDensity = 20.0f;
    static constexpr float kDefaultCrackleDensity = 3.0f;
    static constexpr float kMinSensitivity = 0.0f;
    static constexpr float kMaxSensitivity = 2.0f;
    static constexpr float kDefaultSensitivity = 1.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    NoiseGenerator() noexcept = default;

    /// @brief Destructor
    ~NoiseGenerator() = default;

    // Non-copyable, movable
    NoiseGenerator(const NoiseGenerator&) = delete;
    NoiseGenerator& operator=(const NoiseGenerator&) = delete;
    NoiseGenerator(NoiseGenerator&&) noexcept = default;
    NoiseGenerator& operator=(NoiseGenerator&&) noexcept = default;

    /// @brief Prepare processor for given sample rate and block size
    /// @param sampleRate Audio sample rate in Hz (44100-192000)
    /// @param maxBlockSize Maximum samples per process() call (up to 8192)
    /// @pre sampleRate >= 44100.0f
    /// @pre maxBlockSize > 0 && maxBlockSize <= 8192
    void prepare(float sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Initialize smoothers for all noise levels
        const float smoothTimeMs = 5.0f; // 5ms smoothing for click-free level changes
        for (size_t i = 0; i < kNumNoiseTypes; ++i) {
            levelSmoothers_[i].configure(smoothTimeMs, sampleRate);
            levelSmoothers_[i].setTarget(0.0f); // All disabled by default
        }

        // Master level smoother
        masterSmoother_.configure(smoothTimeMs, sampleRate);
        masterSmoother_.setTarget(1.0f); // 0 dB default

        // Configure tape hiss high-shelf filter (+3dB at 5kHz for tape character)
        tapeHissFilter_.configure(FilterType::HighShelf, 5000.0f, 0.707f, 3.0f, sampleRate);

        // Configure grey noise low-shelf filter (inverse A-weighting approximation)
        // A-weighting cuts ~19dB at 100Hz, we boost lows to compensate
        greyLowShelf_.configure(FilterType::LowShelf, 200.0f, 0.707f, 12.0f, sampleRate);

        // Configure vinyl rumble low-pass filter (sub-bass only, ~80Hz cutoff)
        // Uses 2-pole low-pass for steep rolloff above 100Hz
        rumbleLowPass_.configure(FilterType::Lowpass, 80.0f, 0.707f, 0.0f, sampleRate);

        // Configure envelope followers for signal-dependent noise
        tapeHissEnvelope_.prepare(static_cast<double>(sampleRate), maxBlockSize);
        tapeHissEnvelope_.setMode(DetectionMode::RMS);
        tapeHissEnvelope_.setAttackTime(10.0f);  // Fast attack
        tapeHissEnvelope_.setReleaseTime(100.0f); // Medium release

        asperityEnvelope_.prepare(static_cast<double>(sampleRate), maxBlockSize);
        asperityEnvelope_.setMode(DetectionMode::Amplitude);
        asperityEnvelope_.setAttackTime(5.0f);   // Very fast attack
        asperityEnvelope_.setReleaseTime(50.0f); // Fast release

        // Configure modulation noise envelope follower (fast response, no floor)
        modulationEnvelope_.prepare(static_cast<double>(sampleRate), maxBlockSize);
        modulationEnvelope_.setMode(DetectionMode::RMS);
        modulationEnvelope_.setAttackTime(2.0f);   // Very fast attack
        modulationEnvelope_.setReleaseTime(30.0f); // Fast release

        // Configure radio static low-pass filter (~5kHz cutoff for AM radio bandwidth)
        radioLowPass_.configure(FilterType::Lowpass, 5000.0f, 0.707f, 0.0f, sampleRate);

        reset();
    }

    /// @brief Clear all internal state and reseed random generator
    /// @post All noise channels produce fresh sequences
    void reset() noexcept {
        // Reseed RNG with new seed based on current state
        // This ensures different instances have uncorrelated sequences
        rng_.seed(rng_.next() ^ 0xDEADBEEF);

        // Reset pink noise filter
        pinkFilter_.reset();

        // Reset crackle state
        crackleCounter_ = 0.0f;
        crackleAmplitude_ = 0.0f;
        crackleDecay_ = 0.0f;

        // Reset biquad filters
        tapeHissFilter_.reset();
        greyLowShelf_.reset();
        rumbleLowPass_.reset();

        // Reset envelope followers
        tapeHissEnvelope_.reset();
        asperityEnvelope_.reset();
        modulationEnvelope_.reset();

        // Reset cached envelope value
        lastEnvelopeValue_ = 0.0f;

        // Reset brown noise integrator
        brownPrevious_ = 0.0f;

        // Reset blue noise differentiator
        bluePrevious_ = 0.0f;

        // Reset violet noise differentiator
        violetPrevious_ = 0.0f;

        // Reset vinyl rumble integrator
        rumblePrevious_ = 0.0f;

        // Reset radio static low-pass filter
        radioLowPass_.reset();
    }

    // =========================================================================
    // Configuration - Level Control
    // =========================================================================

    /// @brief Set output level for a specific noise type
    /// @param type Noise type to configure
    /// @param dB Level in decibels [-96, +12]
    void setNoiseLevel(NoiseType type, float dB) noexcept {
        const size_t idx = static_cast<size_t>(type);
        if (idx < kNumNoiseTypes) {
            dB = std::clamp(dB, kMinLevelDb, kMaxLevelDb);
            noiseLevels_[idx] = dB;
            updateLevelTarget(type);
        }
    }

    /// @brief Get current level for a noise type
    /// @param type Noise type to query
    /// @return Level in decibels
    [[nodiscard]] float getNoiseLevel(NoiseType type) const noexcept {
        const size_t idx = static_cast<size_t>(type);
        return (idx < kNumNoiseTypes) ? noiseLevels_[idx] : kMinLevelDb;
    }

    /// @brief Enable or disable a specific noise type
    /// @param type Noise type to configure
    /// @param enabled True to enable, false to disable
    void setNoiseEnabled(NoiseType type, bool enabled) noexcept {
        const size_t idx = static_cast<size_t>(type);
        if (idx < kNumNoiseTypes) {
            noiseEnabled_[idx] = enabled;
            updateLevelTarget(type);
        }
    }

    /// @brief Check if a noise type is enabled
    /// @param type Noise type to query
    /// @return True if enabled
    [[nodiscard]] bool isNoiseEnabled(NoiseType type) const noexcept {
        const size_t idx = static_cast<size_t>(type);
        return (idx < kNumNoiseTypes) ? noiseEnabled_[idx] : false;
    }

    /// @brief Set master output level
    /// @param dB Master level in decibels [-96, +12]
    void setMasterLevel(float dB) noexcept {
        dB = std::clamp(dB, kMinLevelDb, kMaxLevelDb);
        masterLevelDb_ = dB;
        masterSmoother_.setTarget(dbToGain(dB));
    }

    /// @brief Get master output level
    /// @return Master level in decibels
    [[nodiscard]] float getMasterLevel() const noexcept {
        return masterLevelDb_;
    }

    // =========================================================================
    // Configuration - Type-Specific Parameters
    // =========================================================================

    /// @brief Configure tape hiss parameters
    /// @param floorDb Minimum noise floor in dB [-96, 0]
    /// @param sensitivity Modulation sensitivity [0, 2]
    void setTapeHissParams(float floorDb, float sensitivity) noexcept {
        tapeHissFloorDb_ = std::clamp(floorDb, kMinLevelDb, 0.0f);
        tapeHissSensitivity_ = std::clamp(sensitivity, kMinSensitivity, kMaxSensitivity);
    }

    /// @brief Configure asperity noise parameters
    /// @param floorDb Minimum noise floor in dB [-96, 0]
    /// @param sensitivity Modulation sensitivity [0, 2]
    void setAsperityParams(float floorDb, float sensitivity) noexcept {
        asperityFloorDb_ = std::clamp(floorDb, kMinLevelDb, 0.0f);
        asperitySensitivity_ = std::clamp(sensitivity, kMinSensitivity, kMaxSensitivity);
    }

    /// @brief Configure vinyl crackle parameters
    /// @param density Clicks per second [0.1, 20]
    /// @param surfaceNoiseDb Continuous surface noise level [-96, 0]
    void setCrackleParams(float density, float surfaceNoiseDb) noexcept {
        crackleDensity_ = std::clamp(density, kMinCrackleDensity, kMaxCrackleDensity);
        surfaceNoiseDb_ = std::clamp(surfaceNoiseDb, kMinLevelDb, 0.0f);
    }

    /// @brief Configure velvet noise density (impulses per second)
    /// @param density Impulses per second [100, 20000] (FR-025)
    void setVelvetDensity(float density) noexcept {
        velvetDensity_ = std::clamp(density, 100.0f, 20000.0f);
    }

    /// @brief Get current velvet noise density
    /// @return Impulses per second
    [[nodiscard]] float getVelvetDensity() const noexcept {
        return velvetDensity_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Generate noise without sidechain input
    /// @param output Output buffer to fill with noise
    /// @param numSamples Number of samples to generate
    void process(float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = generateNoiseSample(0.0f);
        }
    }

    /// @brief Generate noise with sidechain input
    /// @param input Sidechain input buffer (for envelope following)
    /// @param output Output buffer to fill with noise
    /// @param numSamples Number of samples to process
    void process(const float* input, float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = generateNoiseSample(input[i]);
        }
    }

    /// @brief Add generated noise to existing signal
    /// @param input Input buffer (also used as sidechain)
    /// @param output Output buffer (input + noise mixed)
    /// @param numSamples Number of samples to process
    void processMix(const float* input, float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            float noise = generateNoiseSample(input[i]);
            output[i] = input[i] + noise;
        }
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Check if any noise type is enabled
    /// @return True if at least one noise type is enabled
    [[nodiscard]] bool isAnyEnabled() const noexcept {
        for (size_t i = 0; i < kNumNoiseTypes; ++i) {
            if (noiseEnabled_[i]) return true;
        }
        return false;
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Generate a single sample of mixed noise from all enabled types
    /// @param sidechainInput Input sample for signal-dependent modulation
    /// @return Combined noise sample with level and master gain applied
    [[nodiscard]] float generateNoiseSample(float sidechainInput) noexcept {
        float sample = 0.0f;

        // Generate base white noise sample (used by white, pink, tape hiss, asperity)
        float whiteNoise = rng_.nextFloat();

        // White noise (US1)
        float whiteGain = levelSmoothers_[static_cast<size_t>(NoiseType::White)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::White)]) {
            sample += whiteNoise * whiteGain;
        }

        // Pink noise (US2)
        float pinkNoise = pinkFilter_.process(whiteNoise);
        float pinkGain = levelSmoothers_[static_cast<size_t>(NoiseType::Pink)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::Pink)]) {
            sample += pinkNoise * pinkGain;
        }

        // Tape hiss (US3) - pink noise + high shelf + signal-dependent modulation
        float tapeHissGain = levelSmoothers_[static_cast<size_t>(NoiseType::TapeHiss)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::TapeHiss)]) {
            // Apply high-shelf to pink noise for tape character
            float shapedNoise = tapeHissFilter_.process(pinkNoise);

            // Calculate signal-dependent modulation
            float envelope = tapeHissEnvelope_.processSample(sidechainInput);
            float floorGain = dbToGain(tapeHissFloorDb_);

            // Modulation: floor + (1 - floor) * envelope * sensitivity
            float modulation = floorGain + (1.0f - floorGain) * envelope * tapeHissSensitivity_;
            modulation = std::clamp(modulation, 0.0f, 1.0f);

            sample += shapedNoise * tapeHissGain * modulation;
        }

        // Vinyl crackle (US4) - impulsive clicks + surface noise
        float crackleGain = levelSmoothers_[static_cast<size_t>(NoiseType::VinylCrackle)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::VinylCrackle)]) {
            float crackleSample = 0.0f;

            // Generate click using Poisson process
            float clickProb = crackleDensity_ / sampleRate_;
            float rand = rng_.nextUnipolar();
            if (rand < clickProb) {
                // Generate click with exponential amplitude distribution
                float randAmp = rng_.nextUnipolar();
                // Avoid log(0) by clamping
                randAmp = std::max(randAmp, 0.001f);
                crackleAmplitude_ = -std::log(randAmp) * 0.3f;
                crackleAmplitude_ = std::min(crackleAmplitude_, 1.0f);
                // Set decay rate (click lasts about 0.5-2ms)
                crackleDecay_ = 0.995f - (rng_.nextUnipolar() * 0.005f);
            }

            // Add click with decay
            if (crackleAmplitude_ > 0.001f) {
                crackleSample += crackleAmplitude_ * (rng_.nextFloat() > 0.0f ? 1.0f : -1.0f);
                crackleAmplitude_ *= crackleDecay_;
            }

            // Add continuous surface noise (filtered white noise)
            float surfaceGain = dbToGain(surfaceNoiseDb_);
            crackleSample += whiteNoise * surfaceGain;

            sample += crackleSample * crackleGain;
        }

        // Asperity noise (US5) - signal-dependent white noise
        float asperityGain = levelSmoothers_[static_cast<size_t>(NoiseType::Asperity)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::Asperity)]) {
            // Calculate signal-dependent modulation
            float envelope = asperityEnvelope_.processSample(sidechainInput);
            float floorGain = dbToGain(asperityFloorDb_);

            // Modulation: floor + (1 - floor) * envelope * sensitivity
            float modulation = floorGain + (1.0f - floorGain) * envelope * asperitySensitivity_;
            modulation = std::clamp(modulation, 0.0f, 1.0f);

            sample += whiteNoise * asperityGain * modulation;
        }

        // Brown noise (US7) - integrated white noise with leaky integrator (-6dB/octave)
        float brownGain = levelSmoothers_[static_cast<size_t>(NoiseType::Brown)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::Brown)]) {
            // Leaky integrator: brown[n] = leak * brown[n-1] + (1-leak) * white[n]
            // Leak coefficient ~0.98-0.99 for -6dB/octave slope
            constexpr float kBrownLeak = 0.98f;
            brownPrevious_ = kBrownLeak * brownPrevious_ + (1.0f - kBrownLeak) * whiteNoise;

            // Normalize and clamp output to [-1, 1] range
            // The integrator has lower variance, so boost slightly for reasonable level
            float brownNoise = brownPrevious_ * 5.0f;
            brownNoise = std::clamp(brownNoise, -1.0f, 1.0f);

            sample += brownNoise * brownGain;
        }

        // Blue noise (US8) - differentiated pink noise (+3dB/octave)
        // Pink has -3dB/octave, differentiation adds +6dB/octave, net = +3dB/octave
        float blueGain = levelSmoothers_[static_cast<size_t>(NoiseType::Blue)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::Blue)]) {
            // Differentiate pink noise for +3dB/octave
            float blueNoise = pinkNoise - bluePrevious_;
            bluePrevious_ = pinkNoise;

            // Scale and clamp to [-1, 1] range
            // Differentiator increases high frequencies, so apply normalization
            blueNoise *= 0.7f;
            blueNoise = std::clamp(blueNoise, -1.0f, 1.0f);

            sample += blueNoise * blueGain;
        }

        // Violet noise (US9) - differentiated white noise (+6dB/octave)
        float violetGain = levelSmoothers_[static_cast<size_t>(NoiseType::Violet)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::Violet)]) {
            // Differentiate white noise for +6dB/octave
            float violetNoise = whiteNoise - violetPrevious_;
            violetPrevious_ = whiteNoise;

            // Scale and clamp to [-1, 1] range
            // Differentiation emphasizes high frequencies, normalize to maintain levels
            violetNoise *= 0.5f;
            violetNoise = std::clamp(violetNoise, -1.0f, 1.0f);

            sample += violetNoise * violetGain;
        }

        // Grey noise (US10) - inverse A-weighting for perceptually flat loudness
        // A-weighting attenuates lows, so we boost them to make it sound "flat" to human ears
        float greyGain = levelSmoothers_[static_cast<size_t>(NoiseType::Grey)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::Grey)]) {
            // Apply low-shelf filter to white noise for inverse A-weighting approximation
            float greyNoise = greyLowShelf_.process(whiteNoise);
            greyNoise = std::clamp(greyNoise, -1.0f, 1.0f);
            sample += greyNoise * greyGain;
        }

        // Velvet noise (US11) - sparse random impulses for smooth noise character
        // Velvet noise is perceptually smoother than white noise due to sparse impulses
        float velvetGain = levelSmoothers_[static_cast<size_t>(NoiseType::Velvet)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::Velvet)]) {
            // Probability of an impulse per sample = density / sampleRate
            float impulseProb = velvetDensity_ / sampleRate_;
            float randVal = rng_.nextUnipolar();

            if (randVal < impulseProb) {
                // Generate impulse with random polarity (+1 or -1)
                float impulse = (rng_.nextUnipolar() < 0.5f) ? 1.0f : -1.0f;
                sample += impulse * velvetGain;
            }
            // Otherwise output is 0 (sparse)
        }

        // Vinyl rumble (US12) - low-frequency motor/platter noise
        // Uses brown noise (leaky integrator) filtered through steep low-pass to concentrate energy below 100Hz
        float rumbleGain = levelSmoothers_[static_cast<size_t>(NoiseType::VinylRumble)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::VinylRumble)]) {
            // Independent leaky integrator (same algorithm as brown noise)
            constexpr float kRumbleLeak = 0.98f;
            rumblePrevious_ = kRumbleLeak * rumblePrevious_ + (1.0f - kRumbleLeak) * whiteNoise;

            // Low-pass filter to concentrate energy below 100Hz
            float rumbleSample = rumbleLowPass_.process(rumblePrevious_ * 5.0f);
            rumbleSample = std::clamp(rumbleSample, -1.0f, 1.0f);
            sample += rumbleSample * rumbleGain;
        }

        // Modulation noise (US14) - signal-correlated noise, NO floor
        // Unlike tape hiss, modulation noise is purely proportional to signal level
        float modulationGain = levelSmoothers_[static_cast<size_t>(NoiseType::ModulationNoise)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::ModulationNoise)]) {
            // Track input signal envelope (no floor - zero when silent)
            float envelope = modulationEnvelope_.processSample(sidechainInput);

            // Scale noise by envelope directly (no floor)
            // Use white noise as base, modulated purely by signal level
            float modulatedNoise = whiteNoise * envelope;
            modulatedNoise = std::clamp(modulatedNoise, -1.0f, 1.0f);
            sample += modulatedNoise * modulationGain;
        }

        // Radio static (US15) - band-limited atmospheric/interference noise
        // White noise through ~5kHz low-pass filter for AM radio bandwidth character
        float radioGain = levelSmoothers_[static_cast<size_t>(NoiseType::RadioStatic)].process();
        if (noiseEnabled_[static_cast<size_t>(NoiseType::RadioStatic)]) {
            // Apply low-pass filter to white noise for band-limited character
            float radioNoise = radioLowPass_.process(whiteNoise);
            radioNoise = std::clamp(radioNoise, -1.0f, 1.0f);
            sample += radioNoise * radioGain;
        }

        // Apply master level
        float masterGain = masterSmoother_.process();
        return sample * masterGain;
    }

    void updateLevelTarget(NoiseType type) noexcept {
        const size_t idx = static_cast<size_t>(type);
        if (idx < kNumNoiseTypes) {
            float targetGain = noiseEnabled_[idx] ? dbToGain(noiseLevels_[idx]) : 0.0f;
            levelSmoothers_[idx].setTarget(targetGain);
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    // Core state
    float sampleRate_ = 44100.0f;
    size_t maxBlockSize_ = 512;
    Xorshift32 rng_{12345};

    // Per-noise-type configuration
    std::array<float, kNumNoiseTypes> noiseLevels_ = {
        kDefaultLevelDb, kDefaultLevelDb, kDefaultLevelDb, kDefaultLevelDb,
        kDefaultLevelDb, kDefaultLevelDb, kDefaultLevelDb, kDefaultLevelDb,
        kDefaultLevelDb, kDefaultLevelDb, kDefaultLevelDb, kDefaultLevelDb,
        kDefaultLevelDb
    };
    std::array<bool, kNumNoiseTypes> noiseEnabled_ = {
        false, false, false, false, false, false, false, false, false, false, false, false, false
    };
    std::array<OnePoleSmoother, kNumNoiseTypes> levelSmoothers_;

    // Master level
    float masterLevelDb_ = 0.0f;
    OnePoleSmoother masterSmoother_;

    // Pink noise filter (Paul Kellet's algorithm)
    PinkNoiseFilter pinkFilter_;

    // Tape hiss parameters and components
    float tapeHissFloorDb_ = -60.0f;
    float tapeHissSensitivity_ = 1.0f;
    Biquad tapeHissFilter_;
    EnvelopeFollower tapeHissEnvelope_;

    // Asperity parameters and components
    float asperityFloorDb_ = -72.0f;
    float asperitySensitivity_ = 1.0f;
    EnvelopeFollower asperityEnvelope_;

    // Vinyl crackle parameters and state
    float crackleDensity_ = kDefaultCrackleDensity;
    float surfaceNoiseDb_ = -42.0f;
    float crackleCounter_ = 0.0f;
    float crackleAmplitude_ = 0.0f;
    float crackleDecay_ = 0.0f;

    // Cached envelope value for signal-dependent noise
    float lastEnvelopeValue_ = 0.0f;

    // Brown noise state (leaky integrator)
    float brownPrevious_ = 0.0f;

    // Blue noise state (differentiator)
    float bluePrevious_ = 0.0f;

    // Violet noise state (differentiator)
    float violetPrevious_ = 0.0f;

    // Grey noise filter (inverse A-weighting approximation)
    Biquad greyLowShelf_;

    // Velvet noise density (impulses per second)
    float velvetDensity_ = 1000.0f;

    // Vinyl rumble state (independent integrator for when brown is disabled)
    float rumblePrevious_ = 0.0f;
    Biquad rumbleLowPass_;

    // Modulation noise envelope follower (no floor, scales purely with input)
    EnvelopeFollower modulationEnvelope_;

    // Radio static low-pass filter (~5kHz bandwidth for AM radio character)
    Biquad radioLowPass_;
};

} // namespace DSP
} // namespace Iterum
