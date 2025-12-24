// Layer 2: DSP Processor - NoiseGenerator API Contract
// Feature: 013-noise-generator
// This is a contract file defining the public API. Implementation may vary.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Iterum::DSP {

/// Noise generation algorithm types
enum class NoiseType : uint8_t {
    White = 0,      ///< Flat spectrum white noise
    Pink,           ///< -3dB/octave pink noise
    TapeHiss,       ///< Signal-dependent tape hiss
    VinylCrackle,   ///< Impulsive clicks and surface noise
    Asperity        ///< Tape head contact noise
};

/// Number of noise types available
constexpr size_t kNumNoiseTypes = 5;

/**
 * @brief Layer 2 noise generator processor
 *
 * Generates various noise types for analog character and lo-fi effects.
 * Supports independent level control per noise type, signal-dependent
 * modulation for tape hiss and asperity, and real-time safe processing.
 *
 * @par Layer Dependencies
 * - Layer 0: db_utils (dbToGain, gainToDb)
 * - Layer 1: Biquad (tape hiss shaping), OnePoleSmoother (level smoothing)
 * - Layer 2: EnvelopeFollower (signal-dependent modulation)
 *
 * @par Real-Time Safety
 * - No memory allocation in process()
 * - All buffers pre-allocated in prepare()
 * - Lock-free parameter updates via smoothing
 *
 * @par Usage Example
 * @code
 * NoiseGenerator noise;
 * noise.prepare(44100.0f, 512);
 * noise.setNoiseEnabled(NoiseType::TapeHiss, true);
 * noise.setNoiseLevel(NoiseType::TapeHiss, -30.0f);
 * noise.setTapeHissParams(-60.0f, 1.0f);
 * noise.processMix(input, output, 512);
 * @endcode
 */
class NoiseGenerator {
public:
    NoiseGenerator() noexcept;
    ~NoiseGenerator() = default;

    // Non-copyable, movable
    NoiseGenerator(const NoiseGenerator&) = delete;
    NoiseGenerator& operator=(const NoiseGenerator&) = delete;
    NoiseGenerator(NoiseGenerator&&) noexcept = default;
    NoiseGenerator& operator=(NoiseGenerator&&) noexcept = default;

    //-------------------------------------------------------------------------
    // Lifecycle
    //-------------------------------------------------------------------------

    /**
     * @brief Initialize for a given sample rate and block size
     * @param sampleRate Sample rate in Hz (44100-192000)
     * @param maxBlockSize Maximum samples per process() call
     * @pre sampleRate > 0
     * @pre maxBlockSize > 0 && maxBlockSize <= 8192
     */
    void prepare(float sampleRate, size_t maxBlockSize) noexcept;

    /**
     * @brief Clear all internal state and reseed random generator
     * @post All noise channels produce fresh sequences
     */
    void reset() noexcept;

    //-------------------------------------------------------------------------
    // Configuration - Level Control
    //-------------------------------------------------------------------------

    /**
     * @brief Set output level for a specific noise type
     * @param type Noise type to configure
     * @param dB Level in decibels [-96, +12]
     */
    void setNoiseLevel(NoiseType type, float dB) noexcept;

    /**
     * @brief Get current level for a noise type
     * @param type Noise type to query
     * @return Level in decibels
     */
    float getNoiseLevel(NoiseType type) const noexcept;

    /**
     * @brief Enable or disable a specific noise type
     * @param type Noise type to configure
     * @param enabled True to enable, false to disable
     */
    void setNoiseEnabled(NoiseType type, bool enabled) noexcept;

    /**
     * @brief Check if a noise type is enabled
     * @param type Noise type to query
     * @return True if enabled
     */
    bool isNoiseEnabled(NoiseType type) const noexcept;

    /**
     * @brief Set master output level
     * @param dB Master level in decibels [-96, +12]
     */
    void setMasterLevel(float dB) noexcept;

    /**
     * @brief Get master output level
     * @return Master level in decibels
     */
    float getMasterLevel() const noexcept;

    //-------------------------------------------------------------------------
    // Configuration - Type-Specific Parameters
    //-------------------------------------------------------------------------

    /**
     * @brief Configure tape hiss parameters
     * @param floorDb Minimum noise floor in dB [-96, 0] (noise when signal silent)
     * @param sensitivity Modulation sensitivity [0, 2] (1.0 = normal)
     */
    void setTapeHissParams(float floorDb, float sensitivity) noexcept;

    /**
     * @brief Configure asperity noise parameters
     * @param floorDb Minimum noise floor in dB [-96, 0]
     * @param sensitivity Modulation sensitivity [0, 2]
     */
    void setAsperityParams(float floorDb, float sensitivity) noexcept;

    /**
     * @brief Configure vinyl crackle parameters
     * @param density Clicks per second [0.1, 20]
     * @param surfaceNoiseDb Continuous surface noise level [-96, 0]
     */
    void setCrackleParams(float density, float surfaceNoiseDb) noexcept;

    //-------------------------------------------------------------------------
    // Processing
    //-------------------------------------------------------------------------

    /**
     * @brief Generate noise without sidechain input
     *
     * For noise types that don't require signal input (White, Pink, VinylCrackle).
     * Signal-dependent types (TapeHiss, Asperity) use floor level.
     *
     * @param output Output buffer to fill with noise
     * @param numSamples Number of samples to generate
     * @pre output != nullptr
     * @pre numSamples <= maxBlockSize from prepare()
     */
    void process(float* output, size_t numSamples) noexcept;

    /**
     * @brief Generate noise with sidechain input for signal-dependent types
     *
     * Input signal is used for envelope following (TapeHiss, Asperity modulation).
     * Input is NOT passed through; output contains only generated noise.
     *
     * @param input Sidechain input buffer (for envelope following)
     * @param output Output buffer to fill with noise
     * @param numSamples Number of samples to process
     * @pre input != nullptr
     * @pre output != nullptr
     * @pre numSamples <= maxBlockSize from prepare()
     */
    void process(const float* input, float* output, size_t numSamples) noexcept;

    /**
     * @brief Add generated noise to existing signal (in-place)
     *
     * Input signal is passed through with noise added. Input is also used
     * as sidechain for signal-dependent noise types.
     *
     * @param input Input buffer (also used as sidechain)
     * @param output Output buffer (input + noise mixed)
     * @param numSamples Number of samples to process
     * @pre input != nullptr
     * @pre output != nullptr (may alias input for in-place processing)
     * @pre numSamples <= maxBlockSize from prepare()
     */
    void processMix(const float* input, float* output, size_t numSamples) noexcept;

    //-------------------------------------------------------------------------
    // Queries
    //-------------------------------------------------------------------------

    /**
     * @brief Check if any noise type is enabled
     * @return True if at least one noise type is enabled
     */
    bool isAnyEnabled() const noexcept;

private:
    // Implementation details hidden
    struct Impl;
    // Note: Actual implementation uses inline members for real-time safety
};

} // namespace Iterum::DSP
