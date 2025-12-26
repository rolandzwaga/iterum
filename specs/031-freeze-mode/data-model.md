# Data Model: Freeze Mode

**Feature**: 031-freeze-mode
**Date**: 2025-12-26

## Components Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                      FreezeMode (Layer 4)                       │
│  User-facing API for freeze functionality                       │
├─────────────────────────────────────────────────────────────────┤
│  - setFreezeEnabled(bool)                                       │
│  - setDecay(float 0-100%)                                       │
│  - setPitchSemitones(float -24 to +24)                         │
│  - setShimmerMix(float 0-100%)                                 │
│  - setDiffusionAmount(float 0-100%)                            │
│  - setFilterEnabled(bool), setFilterCutoff(float)              │
│  - setDryWetMix(float 0-100%)                                  │
└──────────────────────────┬──────────────────────────────────────┘
                           │ composes
                           v
┌─────────────────────────────────────────────────────────────────┐
│             FlexibleFeedbackNetwork (Layer 3)                   │
│  Core delay + feedback loop with freeze support                 │
├─────────────────────────────────────────────────────────────────┤
│  - setFreezeEnabled(bool) - built-in freeze                    │
│  - setProcessor(IFeedbackProcessor*) - injection point         │
│  - setFeedbackAmount(float)                                    │
│  - setFilterEnabled/Cutoff/Type()                              │
└──────────────────────────┬──────────────────────────────────────┘
                           │ injects
                           v
┌─────────────────────────────────────────────────────────────────┐
│           FreezeFeedbackProcessor (implements IFeedbackProcessor)│
│  Processing chain in feedback path                              │
├─────────────────────────────────────────────────────────────────┤
│  - Pitch shifting (optional, via PitchShiftProcessor)          │
│  - Diffusion (optional, via DiffusionNetwork)                  │
│  - Decay gain reduction (per-sample)                           │
│  - Shimmer mix blending (pitched vs unpitched)                 │
└─────────────────────────────────────────────────────────────────┘
```

## Class Definitions

### FreezeFeedbackProcessor

**Purpose**: Implements IFeedbackProcessor to add pitch shifting, diffusion, and decay in the feedback path.

**Location**: `src/dsp/features/freeze_mode.h`

**Interface**:
```cpp
class FreezeFeedbackProcessor : public IFeedbackProcessor {
public:
    // IFeedbackProcessor interface
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept override;
    void process(float* left, float* right, std::size_t numSamples) noexcept override;
    void reset() noexcept override;
    [[nodiscard]] std::size_t getLatencySamples() const noexcept override;

    // Configuration
    void setPitchSemitones(float semitones) noexcept;  // -24 to +24
    void setPitchCents(float cents) noexcept;          // -100 to +100
    void setShimmerMix(float mix) noexcept;            // 0-1 (0 = unpitched)
    void setDiffusionAmount(float amount) noexcept;    // 0-1
    void setDiffusionSize(float size) noexcept;        // 0-100
    void setDecayAmount(float decay) noexcept;         // 0-1 (0 = infinite)

private:
    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;

    // Sub-processors
    PitchShiftProcessor pitchShifterL_;
    PitchShiftProcessor pitchShifterR_;
    DiffusionNetwork diffusion_;

    // Parameters
    float shimmerMix_ = 0.0f;      // 0-1
    float diffusionAmount_ = 0.0f; // 0-1
    float decayAmount_ = 0.0f;     // 0-1 (0 = infinite sustain)
    float decayGain_ = 1.0f;       // Pre-calculated per-sample gain

    // Buffers
    std::vector<float> unpitchedL_;
    std::vector<float> unpitchedR_;
    std::vector<float> diffusionOutL_;
    std::vector<float> diffusionOutR_;
};
```

**Processing Flow**:
1. Store unpitched signal for shimmer mix blending
2. Apply pitch shifting (if shimmerMix > 0)
3. Apply diffusion (if diffusionAmount > 0)
4. Blend pitched/unpitched based on shimmerMix
5. Apply decay gain reduction

### FreezeMode

**Purpose**: Layer 4 user-facing feature that composes FlexibleFeedbackNetwork with FreezeFeedbackProcessor.

**Location**: `src/dsp/features/freeze_mode.h`

**Interface**:
```cpp
class FreezeMode {
public:
    // Constants
    static constexpr float kMinDelayMs = 10.0f;
    static constexpr float kMaxDelayMs = 5000.0f;
    static constexpr float kDefaultDelayMs = 500.0f;

    static constexpr float kMinPitchSemitones = -24.0f;
    static constexpr float kMaxPitchSemitones = 24.0f;

    static constexpr float kMinDecay = 0.0f;     // Infinite sustain
    static constexpr float kMaxDecay = 100.0f;   // Fast fade

    // Lifecycle
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void snapParameters() noexcept;

    // Freeze control (FR-001 to FR-008)
    void setFreezeEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isFreezeEnabled() const noexcept;

    // Delay configuration
    void setDelayTimeMs(float ms) noexcept;
    void setTimeMode(TimeMode mode) noexcept;
    void setNoteValue(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;

    // Pitch shifting (FR-009 to FR-012)
    void setPitchSemitones(float semitones) noexcept;
    void setPitchCents(float cents) noexcept;
    void setShimmerMix(float percent) noexcept;  // 0-100%

    // Decay control (FR-013 to FR-016)
    void setDecay(float percent) noexcept;  // 0-100%

    // Diffusion (FR-017 to FR-019)
    void setDiffusionAmount(float percent) noexcept;
    void setDiffusionSize(float percent) noexcept;

    // Filter (FR-020 to FR-023)
    void setFilterEnabled(bool enabled) noexcept;
    void setFilterType(FilterType type) noexcept;
    void setFilterCutoff(float hz) noexcept;

    // Output (FR-024 to FR-026)
    void setDryWetMix(float percent) noexcept;
    void setOutputGainDb(float dB) noexcept;

    // Query
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

    // Processing
    void process(float* left, float* right, std::size_t numSamples,
                 const BlockContext& ctx) noexcept;

private:
    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // Core components
    FlexibleFeedbackNetwork feedbackNetwork_;
    FreezeFeedbackProcessor freezeProcessor_;

    // Parameter smoothers
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother outputGainSmoother_;
    OnePoleSmoother delaySmoother_;

    // Parameters
    float delayTimeMs_ = kDefaultDelayMs;
    float dryWetMix_ = 50.0f;
    float outputGainDb_ = 0.0f;
    float decayAmount_ = 0.0f;
    // ... other parameters

    // Scratch buffers
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;
};
```

## Parameter Mapping

| User Parameter | Range | Internal Representation | Notes |
|----------------|-------|-------------------------|-------|
| Freeze | on/off | bool | Delegates to FFN |
| Delay Time | 10-5000ms | float ms | Tempo sync optional |
| Pitch | -24 to +24 semitones | float semitones | Via processor |
| Fine Tune | -100 to +100 cents | float cents | Via processor |
| Shimmer Mix | 0-100% | float 0-1 | Pitched vs unpitched blend |
| Decay | 0-100% | float 0-1 | 0 = infinite, 100 = fast fade |
| Diffusion | 0-100% | float 0-1 | Amount of smearing |
| Diffusion Size | 0-100% | float 0-100 | Time scale |
| Filter On | on/off | bool | Via FFN |
| Filter Type | LP/HP/BP | FilterType enum | Via FFN |
| Filter Cutoff | 20-20000Hz | float Hz | Via FFN |
| Dry/Wet | 0-100% | float 0-1 | Output mix |
| Output Gain | -inf to +6dB | float dB | Via dbToGain() |

## State Transitions

```
                    setFreezeEnabled(true)
    ┌─────────────────────────────────────────┐
    │                                         v
┌───────────┐                           ┌───────────┐
│  Unfrozen │                           │   Frozen  │
│           │                           │           │
│ Input ON  │                           │ Input OFF │
│ Feedback  │                           │ Feedback  │
│ = user %  │                           │ = 100%    │
└───────────┘                           └───────────┘
    ^                                         │
    │                                         │
    └─────────────────────────────────────────┘
                    setFreezeEnabled(false)

Transitions are smoothed via freezeMixSmoother_ in FFN (20ms)
```

## Decay Calculation

SC-003 requires: Decay at 100% reaches -60dB within 500ms

```cpp
// -60dB = 10^(-60/20) = 0.001 amplitude
// At 44.1kHz, 500ms = 22050 samples
// decayGain^22050 = 0.001
// decayGain = 0.001^(1/22050) ≈ 0.999686

float calculateDecayGain(float decayAmount, double sampleRate) noexcept {
    if (decayAmount <= 0.0f) return 1.0f;  // Infinite sustain

    const float targetAmplitude = 0.001f;  // -60dB
    const float decayTimeMs = 500.0f / decayAmount;  // Scale by decay %
    const float decaySamples = static_cast<float>(decayTimeMs * sampleRate / 1000.0);

    return std::pow(targetAmplitude, 1.0f / decaySamples);
}
```

## Test Categories

1. **Basic Freeze**: Engage/disengage, input mute verification
2. **Pitch Shifting**: Shimmer evolution in frozen state
3. **Decay**: Verify -60dB timing at various decay settings
4. **Diffusion**: Stereo preservation, transient smearing
5. **Filter**: Cutoff effect in feedback path
6. **Transitions**: Click-free engage/disengage
7. **Edge Cases**: Empty buffer, short delays, parameter changes while frozen
