# Data Model: Spectral Delay

**Feature**: 033-spectral-delay
**Date**: 2025-12-26

## Overview

This document defines the component architecture and data structures for the Spectral Delay feature.

---

## Component Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                      SpectralDelay (Layer 4)                   │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────────────┐  │
│  │   STFT L    │   │   STFT R    │   │  Parameter Smoothers │  │
│  │  (Layer 1)  │   │  (Layer 1)  │   │    (Layer 1)         │  │
│  └──────┬──────┘   └──────┬──────┘   └─────────────────────┘  │
│         │                 │                                    │
│         ▼                 ▼                                    │
│  ┌──────────────────────────────────────────────┐             │
│  │           Per-Bin Delay Lines                │             │
│  │     DelayLine[numBins] x 2 (L/R)             │             │
│  │              (Layer 1)                        │             │
│  └──────────────────────────────────────────────┘             │
│         │                 │                                    │
│         ▼                 ▼                                    │
│  ┌──────────────────────────────────────────────┐             │
│  │          SpectralBuffers                      │             │
│  │   inputL/R, outputL/R, frozen                 │             │
│  │              (Layer 1)                        │             │
│  └──────────────────────────────────────────────┘             │
│         │                 │                                    │
│         ▼                 ▼                                    │
│  ┌─────────────┐   ┌─────────────┐                            │
│  │ OverlapAdd L│   │ OverlapAdd R│                            │
│  │  (Layer 1)  │   │  (Layer 1)  │                            │
│  └─────────────┘   └─────────────┘                            │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

---

## Enumerations

### SpreadDirection

```cpp
/// @brief Spread direction modes for delay time distribution
enum class SpreadDirection : uint8_t {
    LowToHigh,   ///< Higher bins get longer delays (rising effect)
    HighToLow,   ///< Lower bins get longer delays (falling effect)
    CenterOut    ///< Edge bins get longer delays, center is base delay
};
```

---

## Class: SpectralDelay

### Public Interface

```cpp
class SpectralDelay {
public:
    // Constants
    static constexpr size_t kMinFFTSize = 512;
    static constexpr size_t kMaxFFTSize = 4096;
    static constexpr size_t kDefaultFFTSize = 1024;
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

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Processing
    void process(float* left, float* right, size_t numSamples,
                 const BlockContext& ctx) noexcept;

    // FFT Configuration
    void setFFTSize(size_t fftSize) noexcept;
    [[nodiscard]] size_t getFFTSize() const noexcept;

    // Delay Controls
    void setBaseDelayMs(float ms) noexcept;
    [[nodiscard]] float getBaseDelayMs() const noexcept;

    void setSpreadMs(float ms) noexcept;
    [[nodiscard]] float getSpreadMs() const noexcept;

    void setSpreadDirection(SpreadDirection dir) noexcept;
    [[nodiscard]] SpreadDirection getSpreadDirection() const noexcept;

    // Feedback Controls
    void setFeedback(float amount) noexcept;  // 0.0 - 1.2
    [[nodiscard]] float getFeedback() const noexcept;

    void setFeedbackTilt(float tilt) noexcept;  // -1.0 to +1.0
    [[nodiscard]] float getFeedbackTilt() const noexcept;

    // Freeze
    void setFreezeEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isFreezeEnabled() const noexcept;

    // Diffusion
    void setDiffusion(float amount) noexcept;  // 0.0 - 1.0
    [[nodiscard]] float getDiffusion() const noexcept;

    // Output
    void setDryWetMix(float percent) noexcept;  // 0 - 100
    [[nodiscard]] float getDryWetMix() const noexcept;

    void setOutputGainDb(float dB) noexcept;  // -96 to +6
    [[nodiscard]] float getOutputGainDb() const noexcept;

    // Query
    [[nodiscard]] size_t getLatencySamples() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // Parameter snapshot for click-free changes
    void snapParameters() noexcept;
};
```

### Private Members

```cpp
private:
    // Sample rate
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;

    // FFT Configuration
    size_t fftSize_ = kDefaultFFTSize;
    size_t hopSize_ = kDefaultFFTSize / 2;  // 50% overlap

    // STFT Analysis (stereo)
    STFT stftL_;
    STFT stftR_;

    // Spectral Buffers
    SpectralBuffer inputSpectrumL_;
    SpectralBuffer inputSpectrumR_;
    SpectralBuffer outputSpectrumL_;
    SpectralBuffer outputSpectrumR_;
    SpectralBuffer frozenSpectrum_;

    // Per-Bin Delay Lines (stereo)
    std::vector<DelayLine> binDelaysL_;
    std::vector<DelayLine> binDelaysR_;

    // Overlap-Add Synthesis (stereo)
    OverlapAdd overlapAddL_;
    OverlapAdd overlapAddR_;

    // Parameters
    float baseDelayMs_ = kDefaultDelayMs;
    float spreadMs_ = 0.0f;
    SpreadDirection spreadDirection_ = SpreadDirection::LowToHigh;
    float feedback_ = 0.0f;
    float feedbackTilt_ = 0.0f;
    float diffusion_ = 0.0f;
    float dryWetMix_ = 50.0f;
    float outputGainDb_ = 0.0f;
    bool freezeEnabled_ = false;

    // Parameter Smoothers
    OnePoleSmoother baseDelaySmoother_;
    OnePoleSmoother spreadSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother tiltSmoother_;
    OnePoleSmoother diffusionSmoother_;
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother outputGainSmoother_;

    // Freeze State
    bool wasFreeze_ = false;
    float freezeCrossfade_ = 0.0f;

    // Internal buffers
    std::vector<float> tempBufferL_;
    std::vector<float> tempBufferR_;
    std::vector<float> blurredMag_;

    // State
    bool prepared_ = false;
```

---

## Processing Flow

### Per-Block Processing

```
Input Audio (L/R)
       │
       ▼
┌──────────────────────┐
│  Push to STFT L/R    │  ←── pushSamples()
└──────────────────────┘
       │
       ▼
┌──────────────────────┐
│  While canAnalyze()  │
│  ┌────────────────┐  │
│  │ Analyze Frame  │  │  ←── analyze() → SpectralBuffer
│  └────────────────┘  │
│         │            │
│         ▼            │
│  ┌────────────────┐  │
│  │ Per-Bin Delay  │  │  ←── Read/Write each bin's DelayLine
│  └────────────────┘  │
│         │            │
│         ▼            │
│  ┌────────────────┐  │
│  │   Diffusion    │  │  ←── Optional spectral blur
│  └────────────────┘  │
│         │            │
│         ▼            │
│  ┌────────────────┐  │
│  │   Synthesize   │  │  ←── synthesize() from output spectrum
│  └────────────────┘  │
└──────────────────────┘
       │
       ▼
┌──────────────────────┐
│  Pull from OverlapAdd│  ←── pullSamples()
└──────────────────────┘
       │
       ▼
┌──────────────────────┐
│   Apply Dry/Wet Mix  │
│   Apply Output Gain  │
└──────────────────────┘
       │
       ▼
   Output Audio (L/R)
```

### Per-Bin Delay Operation

For each frequency bin `k`:

1. **Read delayed magnitude**: `delayedMag = binDelays[k].read(delayFrames[k])`
2. **Apply feedback**: `feedbackMag = delayedMag * feedbackGain[k]`
3. **Write with feedback**: `binDelays[k].write(inputMag + feedbackMag)`
4. **Output**: `outputMag = delayedMag`

Phase handling: Phase can be passed through or delayed. Simple approach: pass through input phase (preserves transients better).

---

## Parameter Ranges

| Parameter | Min | Max | Default | Unit |
|-----------|-----|-----|---------|------|
| FFT Size | 512 | 4096 | 1024 | samples |
| Base Delay | 0 | 2000 | 250 | ms |
| Spread | 0 | 2000 | 0 | ms |
| Feedback | 0 | 120 | 0 | % |
| Feedback Tilt | -100 | +100 | 0 | % |
| Diffusion | 0 | 100 | 0 | % |
| Dry/Wet Mix | 0 | 100 | 50 | % |
| Output Gain | -96 | +6 | 0 | dB |

---

## State Transitions

### Freeze State Machine

```
         setFreezeEnabled(true)
              ┌─────┐
              │     │
              ▼     │
┌─────────┐       ┌──────────────┐
│ Normal  │ ◄──── │  Freezing    │ (crossfade 50-100ms)
│         │       │  (capture)   │
└─────────┘       └──────────────┘
     │                    │
     │                    ▼
     │            ┌──────────────┐
     └──────────► │   Frozen     │ (hold spectrum)
                  │              │
        setFreezeEnabled(false)
                  │
                  ▼
          ┌──────────────┐
          │  Unfreezing  │ (crossfade 50-100ms)
          │              │
          └──────────────┘
                  │
                  ▼
              (Normal)
```

---

## Memory Layout

For FFT size = 1024 (513 bins), 2000ms max delay at 44.1kHz:

| Component | Size (bytes) | Count | Total |
|-----------|--------------|-------|-------|
| STFT analyzer | ~16KB | 2 | 32KB |
| SpectralBuffer | ~4KB | 5 | 20KB |
| DelayLine (per bin) | ~360KB | 1026 | 369MB |
| OverlapAdd | ~8KB | 2 | 16KB |
| Smoothers | ~64B | 7 | 448B |
| Temp buffers | ~4KB | 3 | 12KB |

**Total (worst case)**: ~370MB for stereo 1024-FFT with 2s max delay

**Typical use (500ms max)**: ~92MB

Note: Memory is dominated by per-bin delay lines. Consider reducing max delay or implementing sparse allocation for unused delay ranges.
