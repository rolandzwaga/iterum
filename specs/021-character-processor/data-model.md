# Data Model: Character Processor

**Feature**: 021-character-processor
**Date**: 2025-12-25
**Purpose**: Define entities, relationships, and data structures

## Enumerations

### CharacterMode

```cpp
/// @brief Character processor mode selection
enum class CharacterMode : uint8_t {
    Tape = 0,           ///< Tape delay character (saturation, wow/flutter, hiss, rolloff)
    BBD = 1,            ///< Bucket-brigade device character (bandwidth, clock noise, soft sat)
    DigitalVintage = 2, ///< Lo-fi digital character (bit reduction, sample rate reduction)
    Clean = 3           ///< Bypass/clean mode (unity gain, no processing)
};
```

**Validation**: Mode value must be in range [0, 3].

---

## Layer 1 Primitives

### BitCrusher

```cpp
/// @brief Layer 1 DSP Primitive - Bit depth reduction
///
/// Quantizes audio to a reduced bit depth with optional dither.
/// Creates quantization noise characteristic of early digital audio.
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (no external dependencies)
///
/// @par Reference
/// specs/021-character-processor/spec.md (FR-014)
class BitCrusher {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare for processing
    /// @param sampleRate Audio sample rate in Hz
    void prepare(double sampleRate) noexcept;

    /// @brief Reset internal state
    void reset() noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample [-1, 1]
    /// @return Quantized output sample
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a buffer in-place
    /// @param buffer Audio buffer
    /// @param numSamples Number of samples
    void process(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set bit depth
    /// @param bits Bit depth [4, 16]
    void setBitDepth(float bits) noexcept;

    /// @brief Set dither amount
    /// @param amount Dither [0, 1] (0 = none, 1 = full TPDF)
    void setDither(float amount) noexcept;

    /// @brief Get current bit depth
    [[nodiscard]] float getBitDepth() const noexcept;

    /// @brief Get current dither amount
    [[nodiscard]] float getDither() const noexcept;

private:
    float bitDepth_ = 16.0f;
    float dither_ = 0.0f;
    // RNG state for dither
};
```

**Parameters**:

| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| Bit Depth | 4.0 - 16.0 | 16.0 | bits |
| Dither | 0.0 - 1.0 | 0.0 | ratio |

---

### SampleRateReducer

```cpp
/// @brief Layer 1 DSP Primitive - Sample rate reduction
///
/// Reduces effective sample rate using sample-and-hold.
/// Creates aliasing artifacts characteristic of early digital audio.
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (no external dependencies)
///
/// @par Reference
/// specs/021-character-processor/spec.md (FR-015)
class SampleRateReducer {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare for processing
    /// @param sampleRate Audio sample rate in Hz
    void prepare(double sampleRate) noexcept;

    /// @brief Reset internal state
    void reset() noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample
    /// @return Reduced sample rate output
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a buffer in-place
    /// @param buffer Audio buffer
    /// @param numSamples Number of samples
    void process(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set reduction factor
    /// @param factor Reduction [1, 8] (1 = no reduction, 8 = heavy aliasing)
    void setReductionFactor(float factor) noexcept;

    /// @brief Get current reduction factor
    [[nodiscard]] float getReductionFactor() const noexcept;

private:
    float reductionFactor_ = 1.0f;
    float holdValue_ = 0.0f;
    float holdCounter_ = 0.0f;
};
```

**Parameters**:

| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| Reduction Factor | 1.0 - 8.0 | 1.0 | factor |

---

## Primary Entity: CharacterProcessor

### Class Definition

```cpp
/// @brief Layer 3 System Component - Character/coloration processor
///
/// Applies analog-style character to audio using four distinct modes:
/// - Tape: Saturation, wow/flutter, hiss, high-frequency rolloff
/// - BBD: Bandwidth limiting, clock noise, soft saturation
/// - DigitalVintage: Bit depth and sample rate reduction
/// - Clean: Unity gain passthrough
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, RAII)
/// - Principle IX: Layer 3 (depends only on Layer 0-2)
/// - Principle X: DSP Constraints (oversampling via SaturationProcessor)
///
/// @par Reference
/// specs/021-character-processor/spec.md
class CharacterProcessor {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process mono audio buffer in-place
    void process(float* buffer, size_t numSamples) noexcept;

    /// @brief Process stereo audio buffers in-place
    void processStereo(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Mode Selection
    // =========================================================================

    /// @brief Set character mode (initiates crossfade)
    void setMode(CharacterMode mode) noexcept;

    /// @brief Get current character mode
    [[nodiscard]] CharacterMode getMode() const noexcept;

    // =========================================================================
    // Tape Mode Parameters
    // =========================================================================

    void setTapeSaturation(float amount) noexcept;      // [0, 1] -> 0-100%
    void setTapeWowRate(float hz) noexcept;             // [0.1, 10] Hz
    void setTapeWowDepth(float depth) noexcept;         // [0, 1] -> 0-100%
    void setTapeFlutterRate(float hz) noexcept;         // [0.1, 10] Hz
    void setTapeFlutterDepth(float depth) noexcept;     // [0, 1] -> 0-100%
    void setTapeHissLevel(float dB) noexcept;           // [-inf, -40] dB
    void setTapeRolloffFreq(float hz) noexcept;         // [2000, 20000] Hz

    // =========================================================================
    // BBD Mode Parameters
    // =========================================================================

    void setBBDBandwidth(float hz) noexcept;            // [2000, 15000] Hz
    void setBBDClockNoiseLevel(float dB) noexcept;      // [-inf, -50] dB
    void setBBDSaturation(float amount) noexcept;       // [0, 1] -> 0-100%

    // =========================================================================
    // Digital Vintage Mode Parameters
    // =========================================================================

    void setDigitalBitDepth(float bits) noexcept;       // [4, 16] bits
    void setDigitalSampleRateReduction(float factor) noexcept; // [1, 8] factor
    void setDigitalDitherAmount(float amount) noexcept; // [0, 1]

    // =========================================================================
    // Common Parameters
    // =========================================================================

    void setCrossfadeTime(float ms) noexcept;           // [10, 100] ms

    // =========================================================================
    // Query
    // =========================================================================

    [[nodiscard]] bool isCrossfading() const noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;

private:
    // Internal components and state (see contracts/character_processor.h)
};
```

---

## Parameter Specifications

### Tape Mode Parameters

| Parameter | Range | Default | Smoothing | Unit |
|-----------|-------|---------|-----------|------|
| Saturation Amount | 0.0 - 1.0 | 0.3 | 20ms | ratio |
| Wow Rate | 0.1 - 10.0 | 1.0 | 20ms | Hz |
| Wow Depth | 0.0 - 1.0 | 0.3 | 20ms | ratio |
| Flutter Rate | 0.1 - 10.0 | 5.0 | 20ms | Hz |
| Flutter Depth | 0.0 - 1.0 | 0.1 | 20ms | ratio |
| Hiss Level | -144.0 - -40.0 | -60.0 | 20ms | dB |
| Rolloff Frequency | 2000 - 20000 | 8000 | 20ms | Hz |

### BBD Mode Parameters

| Parameter | Range | Default | Smoothing | Unit |
|-----------|-------|---------|-----------|------|
| Bandwidth | 2000 - 15000 | 8000 | 20ms | Hz |
| Clock Noise Level | -144.0 - -50.0 | -70.0 | 20ms | dB |
| Saturation Amount | 0.0 - 1.0 | 0.2 | 20ms | ratio |

### Digital Vintage Mode Parameters

| Parameter | Range | Default | Smoothing | Unit |
|-----------|-------|---------|-----------|------|
| Bit Depth | 4.0 - 16.0 | 12.0 | 20ms | bits |
| Sample Rate Reduction | 1.0 - 8.0 | 2.0 | 20ms | factor |
| Dither Amount | 0.0 - 1.0 | 0.5 | 20ms | ratio |

### Global Parameters

| Parameter | Range | Default | Smoothing | Unit |
|-----------|-------|---------|-----------|------|
| Crossfade Time | 10.0 - 100.0 | 50.0 | instant | ms |

---

## Internal Helper Entities

### TapeCharacter (Internal)

Manages tape mode processing chain:
- SaturationProcessor (Tape curve)
- LFO x2 (wow + flutter)
- Short delay line for pitch modulation
- NoiseGenerator (TapeHiss)
- MultimodeFilter (HighShelf rolloff)

### BBDCharacter (Internal)

Manages BBD mode processing chain:
- SaturationProcessor (Tube/Diode curve)
- MultimodeFilter (Lowpass bandwidth limiting)
- NoiseGenerator (RadioStatic for clock noise)

### DigitalVintageCharacter (Internal)

Manages digital vintage mode processing:
- BitCrusher (Layer 1 primitive - quantization with dither)
- SampleRateReducer (Layer 1 primitive - sample-and-hold)

---

## State Transitions

```
                    setMode(Tape)
                   ┌─────────────────┐
                   │                 │
    ┌──────────────▼──────────────┐  │
    │         Tape Mode           │──┘
    │  (Saturation, Wow/Flutter,  │
    │   Hiss, Rolloff active)     │
    └──────────────┬──────────────┘
                   │ setMode(BBD)
                   ▼
    ┌──────────────────────────────┐
    │         BBD Mode             │
    │  (Bandwidth, Clock Noise,    │
    │   Saturation active)         │
    └──────────────┬───────────────┘
                   │ setMode(DigitalVintage)
                   ▼
    ┌──────────────────────────────┐
    │    Digital Vintage Mode      │
    │  (Bit Reduction, SR Reduce,  │
    │   Dither active)             │
    └──────────────┬───────────────┘
                   │ setMode(Clean)
                   ▼
    ┌──────────────────────────────┐
    │         Clean Mode           │
    │  (Unity gain passthrough)    │
    └──────────────────────────────┘
```

All mode transitions pass through a crossfade state:
- Duration: configurable (10-100ms, default 50ms)
- Method: Equal-power crossfade between old and new mode outputs
- Both modes process audio during crossfade

---

## Constants

```cpp
/// Minimum crossfade time in milliseconds
inline constexpr float kMinCrossfadeTimeMs = 10.0f;

/// Maximum crossfade time in milliseconds
inline constexpr float kMaxCrossfadeTimeMs = 100.0f;

/// Default crossfade time in milliseconds (FR-003)
inline constexpr float kDefaultCrossfadeTimeMs = 50.0f;

/// Parameter smoothing time in milliseconds (FR-018)
inline constexpr float kParameterSmoothingMs = 20.0f;

/// Silence threshold in dB (maps to linear 0.0)
inline constexpr float kSilenceThresholdDb = -144.0f;

/// Tape mode defaults
inline constexpr float kDefaultTapeSaturation = 0.3f;
inline constexpr float kDefaultTapeWowRate = 1.0f;
inline constexpr float kDefaultTapeWowDepth = 0.3f;
inline constexpr float kDefaultTapeFlutterRate = 5.0f;
inline constexpr float kDefaultTapeFlutterDepth = 0.1f;
inline constexpr float kDefaultTapeHissDb = -60.0f;
inline constexpr float kDefaultTapeRolloffHz = 8000.0f;

/// BBD mode defaults
inline constexpr float kDefaultBBDBandwidthHz = 8000.0f;
inline constexpr float kDefaultBBDClockNoiseDb = -70.0f;
inline constexpr float kDefaultBBDSaturation = 0.2f;

/// Digital Vintage mode defaults
inline constexpr float kDefaultDigitalBitDepth = 12.0f;
inline constexpr float kDefaultDigitalSRReduction = 2.0f;
inline constexpr float kDefaultDigitalDither = 0.5f;
```
