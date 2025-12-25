// ==============================================================================
// Layer 3: System Component - Character Processor (API Contract)
// ==============================================================================
// This file defines the public API contract for CharacterProcessor.
// Implementation will be in src/dsp/systems/character_processor.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 3 (depends only on Layer 0-2)
// - Principle X: DSP Constraints (oversampling via SaturationProcessor)
// - Principle XII: Test-First Development
//
// Reference: specs/021-character-processor/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// CharacterMode Enumeration
// =============================================================================

/// @brief Character processor mode selection (FR-001)
enum class CharacterMode : uint8_t {
    Tape = 0,           ///< Tape delay character (FR-007 to FR-010)
    BBD = 1,            ///< Bucket-brigade device character (FR-011 to FR-013)
    DigitalVintage = 2, ///< Lo-fi digital character (FR-014 to FR-016)
    Clean = 3           ///< Bypass/clean mode (FR-017)
};

// =============================================================================
// Constants
// =============================================================================

/// Minimum crossfade time in milliseconds
inline constexpr float kMinCrossfadeTimeMs = 10.0f;

/// Maximum crossfade time in milliseconds
inline constexpr float kMaxCrossfadeTimeMs = 100.0f;

/// Default crossfade time in milliseconds (FR-003)
inline constexpr float kDefaultCrossfadeTimeMs = 50.0f;

/// Parameter smoothing time in milliseconds (FR-018)
inline constexpr float kParameterSmoothingMs = 20.0f;

// =============================================================================
// CharacterProcessor Class (API Contract)
// =============================================================================

/// @brief Layer 3 System Component - Character/coloration processor
///
/// Applies analog-style character to audio using four distinct modes:
/// - **Tape**: Saturation, wow/flutter, hiss, high-frequency rolloff
/// - **BBD**: Bandwidth limiting, clock noise, soft saturation
/// - **DigitalVintage**: Bit depth and sample rate reduction
/// - **Clean**: Unity gain passthrough
///
/// @par Key Features
/// - Four distinct character modes (FR-001)
/// - Smooth mode transitions via crossfading (FR-003)
/// - Per-mode parameter controls (FR-007 to FR-017)
/// - Real-time safe processing (FR-019)
/// - Configurable smoothing for all parameters (FR-018)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, RAII)
/// - Principle IX: Layer 3 (depends only on Layer 0-2)
/// - Principle X: DSP Constraints (oversampling via SaturationProcessor)
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
///
/// @see spec.md for full requirements
class CharacterProcessor {
public:
    // =========================================================================
    // Lifecycle (FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Default constructor
    CharacterProcessor() noexcept = default;

    /// @brief Destructor
    ~CharacterProcessor() = default;

    // Non-copyable (contains stateful processors)
    CharacterProcessor(const CharacterProcessor&) = delete;
    CharacterProcessor& operator=(const CharacterProcessor&) = delete;

    // Movable
    CharacterProcessor(CharacterProcessor&&) noexcept = default;
    CharacterProcessor& operator=(CharacterProcessor&&) noexcept = default;

    /// @brief Prepare for processing (FR-004)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state without reallocation (FR-006)
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-005, FR-019)
    // =========================================================================

    /// @brief Process mono audio buffer in-place
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe: noexcept, no allocations
    void process(float* buffer, size_t numSamples) noexcept;

    /// @brief Process stereo audio buffers in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @pre prepare() has been called
    /// @note Real-time safe: noexcept, no allocations
    void processStereo(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Mode Selection (FR-002, FR-003)
    // =========================================================================

    /// @brief Set character mode
    /// @param mode New character mode
    /// @note Initiates smooth crossfade if mode changes (FR-003)
    void setMode(CharacterMode mode) noexcept;

    /// @brief Get current character mode
    /// @return Current mode (destination mode if crossfading)
    [[nodiscard]] CharacterMode getMode() const noexcept;

    // =========================================================================
    // Tape Mode Parameters (FR-007 to FR-010)
    // =========================================================================

    /// @brief Set tape saturation amount
    /// @param amount Saturation [0, 1] (0% to 100%)
    void setTapeSaturation(float amount) noexcept;

    /// @brief Set tape wow rate
    /// @param hz Wow frequency [0.1, 10] Hz
    void setTapeWowRate(float hz) noexcept;

    /// @brief Set tape wow depth
    /// @param depth Modulation depth [0, 1] (0% to 100%)
    void setTapeWowDepth(float depth) noexcept;

    /// @brief Set tape flutter rate
    /// @param hz Flutter frequency [0.1, 10] Hz
    void setTapeFlutterRate(float hz) noexcept;

    /// @brief Set tape flutter depth
    /// @param depth Modulation depth [0, 1] (0% to 100%)
    void setTapeFlutterDepth(float depth) noexcept;

    /// @brief Set tape hiss noise level
    /// @param dB Hiss level [-inf, -40] dB
    void setTapeHissLevel(float dB) noexcept;

    /// @brief Set tape high-frequency rolloff
    /// @param hz Rolloff frequency [2000, 20000] Hz
    void setTapeRolloffFreq(float hz) noexcept;

    // =========================================================================
    // BBD Mode Parameters (FR-011 to FR-013)
    // =========================================================================

    /// @brief Set BBD bandwidth limiting cutoff
    /// @param hz Cutoff frequency [2000, 15000] Hz
    void setBBDBandwidth(float hz) noexcept;

    /// @brief Set BBD clock noise level
    /// @param dB Noise level [-inf, -50] dB
    void setBBDClockNoiseLevel(float dB) noexcept;

    /// @brief Set BBD input stage saturation
    /// @param amount Saturation [0, 1] (0% to 100%)
    void setBBDSaturation(float amount) noexcept;

    // =========================================================================
    // Digital Vintage Mode Parameters (FR-014 to FR-016)
    // =========================================================================

    /// @brief Set bit depth for quantization
    /// @param bits Bit depth [4, 16] bits
    void setDigitalBitDepth(float bits) noexcept;

    /// @brief Set sample rate reduction factor
    /// @param factor Reduction [1, 8] (1x = no reduction, 8x = heavy aliasing)
    void setDigitalSampleRateReduction(float factor) noexcept;

    /// @brief Set dither amount for quantization
    /// @param amount Dither [0, 1] (0% to 100%)
    void setDigitalDitherAmount(float amount) noexcept;

    // =========================================================================
    // Global Parameters
    // =========================================================================

    /// @brief Set mode crossfade time
    /// @param ms Crossfade duration [10, 100] ms
    void setCrossfadeTime(float ms) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if currently crossfading between modes
    /// @return true if crossfade in progress
    [[nodiscard]] bool isCrossfading() const noexcept;

    /// @brief Get processing latency in samples
    /// @return Latency from internal processing (primarily from wow/flutter delay)
    [[nodiscard]] size_t getLatency() const noexcept;

    /// @brief Get sample rate
    [[nodiscard]] double getSampleRate() const noexcept;

private:
    // Implementation details in src/dsp/systems/character_processor.h
    // - SaturationProcessor instances for Tape/BBD
    // - NoiseGenerator instances for hiss/clock noise
    // - MultimodeFilter instances for rolloff/bandwidth
    // - LFO instances for wow/flutter
    // - Crossfade state and smoothers
    // - Per-mode parameter storage
};

}  // namespace DSP
}  // namespace Iterum
