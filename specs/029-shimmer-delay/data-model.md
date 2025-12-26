# Data Model: Shimmer Delay Mode

**Feature**: 029-shimmer-delay
**Date**: 2025-12-26

## Entities

### ShimmerDelay

Layer 4 user feature composing Layer 2-3 components for pitch-shifted feedback delay.

**Composed Components**:
| Component | Layer | Role |
|-----------|-------|------|
| DelayEngine | 3 | Primary delay with tempo sync |
| FeedbackNetwork | 3 | Feedback loop with filtering/limiting |
| PitchShiftProcessor (×2) | 2 | Stereo pitch shifting in feedback |
| DiffusionNetwork | 2 | Smearing for reverb-like texture |
| ModulationMatrix* | 3 | Optional parameter modulation |

*ModulationMatrix is optional - pointer-based connection.

**Parameters**:

| Parameter | Type | Range | Default | Notes |
|-----------|------|-------|---------|-------|
| delayTimeMs | float | 10-5000 | 500 | Free mode delay time |
| timeMode | TimeMode | Free/Synced | Free | Tempo sync mode |
| noteValue | NoteValue | — | Quarter | For synced mode |
| noteModifier | NoteModifier | — | None | Dotted/Triplet |
| pitchSemitones | float | -24 to +24 | +12 | Pitch shift amount |
| pitchCents | float | -100 to +100 | 0 | Fine pitch tuning |
| pitchMode | PitchMode | Simple/Granular/PhaseVocoder | Granular | Quality mode |
| shimmerMix | float | 0-100 | 100 | % of feedback that is pitch-shifted |
| feedbackAmount | float | 0-1.2 | 0.5 | Feedback intensity (120% max) |
| diffusionAmount | float | 0-100 | 50 | Diffusion/smear amount |
| diffusionSize | float | 0-100 | 50 | Diffusion time scaling |
| filterEnabled | bool | — | false | Enable feedback filter |
| filterCutoffHz | float | 20-20000 | 4000 | Filter cutoff frequency |
| dryWetMix | float | 0-100 | 50 | Dry/wet blend |
| outputGainDb | float | -12 to +12 | 0 | Output level adjustment |

**State**:

| State Variable | Type | Notes |
|----------------|------|-------|
| prepared_ | bool | Whether prepare() has been called |
| sampleRate_ | double | Current sample rate |
| maxBlockSize_ | size_t | Maximum block size |
| pitchLatencyL_ | size_t | Left channel pitch shifter latency |
| pitchLatencyR_ | size_t | Right channel pitch shifter latency |

**Methods**:

```cpp
// Lifecycle
void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
void reset() noexcept;
[[nodiscard]] bool isPrepared() const noexcept;

// Processing
void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;

// Delay configuration
void setDelayTimeMs(float ms) noexcept;
void setTimeMode(TimeMode mode) noexcept;
void setNoteValue(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;
void setTempo(float bpm) noexcept;

// Pitch configuration
void setPitchSemitones(float semitones) noexcept;
void setPitchCents(float cents) noexcept;
void setPitchMode(PitchMode mode) noexcept;

// Shimmer configuration
void setShimmerMix(float percent) noexcept;
void setFeedbackAmount(float amount) noexcept;
void setDiffusionAmount(float percent) noexcept;
void setDiffusionSize(float percent) noexcept;

// Filter configuration
void setFilterEnabled(bool enabled) noexcept;
void setFilterCutoff(float hz) noexcept;

// Output configuration
void setDryWetMix(float percent) noexcept;
void setOutputGainDb(float dB) noexcept;

// Modulation
void connectModulationMatrix(ModulationMatrix* matrix) noexcept;

// Queries
[[nodiscard]] float getCurrentDelayMs() const noexcept;
[[nodiscard]] size_t getLatencySamples() const noexcept;
[[nodiscard]] float getPitchRatio() const noexcept;

// Parameter snapping (for initialization)
void snapParameters() noexcept;
```

## Validation Rules

| Parameter | Validation | Error Handling |
|-----------|------------|----------------|
| delayTimeMs | Clamp to [10, maxDelayMs_] | Silent clamp |
| pitchSemitones | Clamp to [-24, +24] | Silent clamp |
| pitchCents | Clamp to [-100, +100] | Silent clamp |
| shimmerMix | Clamp to [0, 100] | Silent clamp |
| feedbackAmount | Clamp to [0, 1.2] | Silent clamp |
| diffusionAmount | Clamp to [0, 100] | Silent clamp |
| filterCutoffHz | Clamp to [20, 20000] | Silent clamp |
| dryWetMix | Clamp to [0, 100] | Silent clamp |
| outputGainDb | Clamp to [-12, +12] | Silent clamp |

## Thread Safety

- All setters are thread-safe (atomic or smoothed parameters)
- process() must be called from audio thread only
- prepare()/reset() must not be called during process()
- All methods are noexcept
