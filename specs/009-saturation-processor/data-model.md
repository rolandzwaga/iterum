# Data Model: Saturation Processor

**Feature**: 009-saturation-processor
**Date**: 2025-12-23
**Layer**: 2 (DSP Processors)

## Class Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          SaturationProcessor                                │
├─────────────────────────────────────────────────────────────────────────────┤
│ Constants:                                                                  │
│   kMinGainDb: float = -24.0f                                                │
│   kMaxGainDb: float = +24.0f                                                │
│   kDefaultSmoothingMs: float = 5.0f                                         │
│   kDCBlockerCutoffHz: float = 10.0f                                         │
├─────────────────────────────────────────────────────────────────────────────┤
│ Public Methods:                                                             │
│   + prepare(sampleRate: double, maxBlockSize: size_t) noexcept -> void      │
│   + reset() noexcept -> void                                                │
│   + process(buffer: float*, numSamples: size_t) noexcept -> void            │
│   + processSample(input: float) noexcept -> float                           │
│   + setType(type: SaturationType) noexcept -> void                          │
│   + setInputGain(gainDb: float) noexcept -> void                            │
│   + setOutputGain(gainDb: float) noexcept -> void                           │
│   + setMix(mix: float) noexcept -> void                                     │
│   + getType() const noexcept -> SaturationType                              │
│   + getInputGain() const noexcept -> float                                  │
│   + getOutputGain() const noexcept -> float                                 │
│   + getMix() const noexcept -> float                                        │
│   + getLatency() const noexcept -> size_t                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ Private Members:                                                            │
│   - type_: SaturationType                                                   │
│   - inputGainDb_: float                                                     │
│   - outputGainDb_: float                                                    │
│   - mix_: float                                                             │
│   - inputGainSmoother_: OnePoleSmoother                                     │
│   - outputGainSmoother_: OnePoleSmoother                                    │
│   - mixSmoother_: OnePoleSmoother                                           │
│   - oversampler_: Oversampler<2, 1>                                         │
│   - dcBlocker_: Biquad                                                      │
│   - oversampledBuffer_: vector<float>                                       │
│   - sampleRate_: double                                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│ Private Methods:                                                            │
│   - saturateTape(x: float) noexcept -> float                                │
│   - saturateTube(x: float) noexcept -> float                                │
│   - saturateTransistor(x: float) noexcept -> float                          │
│   - saturateDigital(x: float) noexcept -> float                             │
│   - saturateDiode(x: float) noexcept -> float                               │
│   - applySaturation(x: float) noexcept -> float                             │
└─────────────────────────────────────────────────────────────────────────────┘
         │
         │ uses
         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            SaturationType                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ enum class SaturationType : uint8_t {                                       │
│     Tape = 0,       // tanh - symmetric, odd harmonics                      │
│     Tube = 1,       // asymmetric polynomial - even harmonics               │
│     Transistor = 2, // hard-knee soft clip - aggressive                     │
│     Digital = 3,    // hard clip - harsh                                    │
│     Diode = 4       // soft asymmetric - subtle warmth                      │
│ }                                                                           │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Composition Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                    SaturationProcessor (Layer 2)                │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ Input Gain Stage                                        │    │
│  │  ┌──────────────────┐                                   │    │
│  │  │ OnePoleSmoother  │ (Layer 1)                         │    │
│  │  └──────────────────┘                                   │    │
│  │  + dbToGain() (Layer 0)                                 │    │
│  └─────────────────────────────────────────────────────────┘    │
│                          │                                       │
│                          ▼                                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ Oversampling + Saturation                               │    │
│  │  ┌──────────────────┐                                   │    │
│  │  │ Oversampler<2,1> │ (Layer 1)                         │    │
│  │  └──────────────────┘                                   │    │
│  │       │                                                 │    │
│  │       ▼                                                 │    │
│  │  ┌──────────────────┐                                   │    │
│  │  │ Saturation       │ (internal waveshaping)            │    │
│  │  │ (Tape/Tube/etc.) │                                   │    │
│  │  └──────────────────┘                                   │    │
│  └─────────────────────────────────────────────────────────┘    │
│                          │                                       │
│                          ▼                                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ DC Blocking                                             │    │
│  │  ┌──────────────────┐                                   │    │
│  │  │ Biquad (HP 10Hz) │ (Layer 1)                         │    │
│  │  └──────────────────┘                                   │    │
│  └─────────────────────────────────────────────────────────┘    │
│                          │                                       │
│                          ▼                                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ Output Stage                                            │    │
│  │  ┌──────────────────┐  ┌──────────────────┐             │    │
│  │  │ OnePoleSmoother  │  │ OnePoleSmoother  │ (Layer 1)   │    │
│  │  │ (output gain)    │  │ (mix)            │             │    │
│  │  └──────────────────┘  └──────────────────┘             │    │
│  │                                                         │    │
│  │  output = dry * (1 - mix) + wet * mix                   │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Signal Flow

```
Input ──────────────────────────────────────────────────────────► Dry Path
   │                                                                  │
   ▼                                                                  │
┌──────────────┐                                                      │
│ Input Gain   │ (smoothed, dB to linear)                             │
│ [-24, +24]dB │                                                      │
└──────────────┘                                                      │
   │                                                                  │
   ▼                                                                  │
┌──────────────┐                                                      │
│ Upsample 2x  │                                                      │
└──────────────┘                                                      │
   │                                                                  │
   ▼                                                                  │
┌──────────────┐                                                      │
│ Saturation   │ (Tape/Tube/Transistor/Digital/Diode)                 │
└──────────────┘                                                      │
   │                                                                  │
   ▼                                                                  │
┌──────────────┐                                                      │
│ Downsample   │                                                      │
└──────────────┘                                                      │
   │                                                                  │
   ▼                                                                  │
┌──────────────┐                                                      │
│ DC Blocker   │ (HP @ 10Hz)                                          │
└──────────────┘                                                      │
   │                                                                  │
   ▼                                                                  │
┌──────────────┐                                                      │
│ Output Gain  │ (smoothed, dB to linear)                             │
│ [-24, +24]dB │                                                      │
└──────────────┘                                                      │
   │                                                                  │
   ▼                                                                  │
   Wet Path ◄──────────────────────────────────────────────────────────┘
              │
              ▼
         ┌─────────────────────────────────┐
         │ Mix: out = dry*(1-m) + wet*m    │ (smoothed)
         └─────────────────────────────────┘
                        │
                        ▼
                     Output
```

## API Contract

### Enumerations

```cpp
/// @brief Saturation algorithm type
enum class SaturationType : uint8_t {
    Tape = 0,       ///< tanh curve - warm, odd harmonics
    Tube = 1,       ///< Asymmetric polynomial - rich even harmonics
    Transistor = 2, ///< Hard-knee soft clip - aggressive
    Digital = 3,    ///< Hard clip - harsh, all harmonics
    Diode = 4       ///< Soft asymmetric - subtle warmth
};
```

### Class Interface

```cpp
/// @brief Layer 2 DSP Processor - Saturation with oversampling
class SaturationProcessor {
public:
    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Processing
    void process(float* buffer, size_t numSamples) noexcept;
    float processSample(float input) noexcept;

    // Parameter setters
    void setType(SaturationType type) noexcept;
    void setInputGain(float gainDb) noexcept;   // [-24, +24] dB
    void setOutputGain(float gainDb) noexcept;  // [-24, +24] dB
    void setMix(float mix) noexcept;            // [0, 1]

    // Parameter getters
    [[nodiscard]] SaturationType getType() const noexcept;
    [[nodiscard]] float getInputGain() const noexcept;
    [[nodiscard]] float getOutputGain() const noexcept;
    [[nodiscard]] float getMix() const noexcept;

    // Info
    [[nodiscard]] size_t getLatency() const noexcept;
};
```

### Parameter Specifications

| Parameter | Type | Range | Default | Smoothed |
|-----------|------|-------|---------|----------|
| type | SaturationType | enum | Tape | No |
| inputGain | float | [-24, +24] dB | 0 dB | Yes (5ms) |
| outputGain | float | [-24, +24] dB | 0 dB | Yes (5ms) |
| mix | float | [0.0, 1.0] | 1.0 | Yes (5ms) |

### Method Specifications

| Method | Allocation | Thread Safety | Notes |
|--------|------------|---------------|-------|
| prepare() | Yes (buffers) | Call from main thread only | Required before processing |
| reset() | No | Not thread-safe | Clear state, call when audio stops |
| process() | No | Single-threaded | Main processing entry |
| processSample() | No | Single-threaded | Per-sample variant |
| setType() | No | Thread-safe (atomic enum) | Instant change |
| setInputGain() | No | Thread-safe | Smoothed |
| setOutputGain() | No | Thread-safe | Smoothed |
| setMix() | No | Thread-safe | Smoothed |
| get*() | No | Thread-safe | Returns cached value |
| getLatency() | No | Thread-safe | Returns oversampler latency |

## Dependencies (Layer Compliance)

| Dependency | Layer | Header |
|------------|-------|--------|
| Oversampler<2,1> | 1 | dsp/primitives/oversampler.h |
| Biquad | 1 | dsp/primitives/biquad.h |
| OnePoleSmoother | 1 | dsp/primitives/smoother.h |
| dbToGain | 0 | dsp/core/db_utils.h |

SaturationProcessor is Layer 2 and depends ONLY on Layer 0 and Layer 1 components.

## Memory Layout

```cpp
class SaturationProcessor {
    // Parameters (32 bytes)
    SaturationType type_;          // 1 byte
    float inputGainDb_;            // 4 bytes
    float outputGainDb_;           // 4 bytes
    float mix_;                    // 4 bytes
    double sampleRate_;            // 8 bytes
    // padding                     // ~11 bytes

    // Smoothers (3 * ~32 bytes = 96 bytes)
    OnePoleSmoother inputGainSmoother_;
    OnePoleSmoother outputGainSmoother_;
    OnePoleSmoother mixSmoother_;

    // Components (~varies)
    Oversampler<2, 1> oversampler_;   // Polyphase filters + buffer
    Biquad dcBlocker_;                 // ~80 bytes

    // Pre-allocated buffer
    std::vector<float> oversampledBuffer_;  // maxBlockSize * 2
};
```

Total estimated size: ~512 bytes + oversampler + oversampledBuffer
