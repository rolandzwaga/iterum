# Data Model: NoiseGenerator

**Feature**: 013-noise-generator | **Date**: 2025-12-23

## Entities

### NoiseType (Enumeration)

```cpp
enum class NoiseType : uint8_t {
    White = 0,      // Flat spectrum
    Pink,           // -3dB/octave
    TapeHiss,       // Signal-dependent filtered noise
    VinylCrackle,   // Impulsive clicks + surface noise
    Asperity        // Tape head contact noise
};
```

**Values**: 5 types (0-4)
**Usage**: Selects noise generation algorithm

---

### NoiseChannelConfig (Value Object)

Configuration for a single noise channel.

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| type | NoiseType | 0-4 | White | Noise algorithm to use |
| levelDb | float | -96 to +12 | -20 | Output level in decibels |
| enabled | bool | - | false | Whether channel is active |

**Type-Specific Parameters**:

| Field | Applies To | Type | Range | Default | Description |
|-------|------------|------|-------|---------|-------------|
| floorDb | TapeHiss, Asperity | float | -96 to 0 | -60 | Minimum noise floor when signal is silent |
| sensitivity | TapeHiss, Asperity | float | 0 to 2 | 1.0 | How strongly noise follows signal level |
| density | VinylCrackle | float | 0.1 to 20 | 2.0 | Clicks per second |
| surfaceNoiseLevel | VinylCrackle | float | -96 to 0 | -40 | Continuous surface noise level |

---

### NoiseGenerator (Main Processor)

Layer 2 DSP processor that generates and mixes multiple noise types.

**State Variables**:

| Field | Type | Description |
|-------|------|-------------|
| sampleRate_ | float | Current sample rate |
| maxBlockSize_ | size_t | Maximum samples per process call |
| rng_ | Xorshift32 | Random number generator |
| pinkFilter_ | PinkNoiseFilter | State for pink noise generation |
| hissShaper_ | Biquad | High-shelf for tape hiss spectrum |
| envelopeFollower_ | EnvelopeFollower | Signal level tracking |
| levelSmootherN_ | OnePoleSmoother | Per-channel level smoothing (×5) |
| masterSmoother_ | OnePoleSmoother | Master level smoothing |
| crackleState_ | CrackleState | Vinyl crackle envelope state |

**Configuration**:

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| channels | NoiseChannelConfig[5] | - | all disabled | Per-type configuration |
| masterLevelDb | float | -96 to +12 | 0 | Master output level |
| smoothTimeMs | float | 1 to 100 | 10 | Parameter smoothing time |

---

### PinkNoiseFilter (Internal)

State for Paul Kellet's pink noise filter.

| Field | Type | Description |
|-------|------|-------------|
| b0-b6 | float | Filter state variables (7 total) |

---

### CrackleState (Internal)

State for vinyl crackle impulse envelope.

| Field | Type | Description |
|-------|------|-------------|
| amplitude | float | Current click amplitude |
| decay | float | Decay coefficient |
| active | bool | Whether a click is in progress |

---

## Relationships

```
NoiseGenerator (1) ────────────> (5) NoiseChannelConfig
       │
       ├──────> (1) Xorshift32 (RNG)
       │
       ├──────> (1) PinkNoiseFilter
       │
       ├──────> (1) Biquad (hiss shaper)
       │
       ├──────> (1) EnvelopeFollower (signal tracking)
       │
       ├──────> (6) OnePoleSmoother (level smoothing)
       │
       └──────> (1) CrackleState
```

---

## API Methods

### Lifecycle

| Method | Signature | Description |
|--------|-----------|-------------|
| prepare | `void prepare(float sampleRate, size_t maxBlockSize)` | Initialize for given sample rate |
| reset | `void reset()` | Clear state, reseed RNG |

### Configuration

| Method | Signature | Description |
|--------|-----------|-------------|
| setNoiseLevel | `void setNoiseLevel(NoiseType type, float dB)` | Set level for specific noise type |
| setNoiseEnabled | `void setNoiseEnabled(NoiseType type, bool enabled)` | Enable/disable noise type |
| setMasterLevel | `void setMasterLevel(float dB)` | Set master output level |
| setTapeHissParams | `void setTapeHissParams(float floorDb, float sensitivity)` | Configure tape hiss |
| setAsperityParams | `void setAsperityParams(float floorDb, float sensitivity)` | Configure asperity |
| setCrackleParams | `void setCrackleParams(float density, float surfaceDb)` | Configure vinyl crackle |

### Processing

| Method | Signature | Description |
|--------|-----------|-------------|
| process | `void process(float* output, size_t numSamples)` | Generate noise (no sidechain) |
| process | `void process(const float* input, float* output, size_t numSamples)` | Generate with sidechain for signal-dependent types |
| processMix | `void processMix(const float* input, float* output, size_t numSamples)` | Add noise to existing signal |

### Queries

| Method | Signature | Description |
|--------|-----------|-------------|
| isAnyEnabled | `bool isAnyEnabled() const` | Check if any noise is active |
| getNoiseLevel | `float getNoiseLevel(NoiseType type) const` | Get current level for type |

---

## Processing Flow

```
Input Signal ──┐
               │
               ▼
        EnvelopeFollower
               │
               ▼ signalLevel
    ┌──────────┴──────────┐
    │                     │
    ▼                     ▼
TapeHiss              Asperity
(pink + shelf)    (broadband mod)
    │                     │
    ├──────────┬──────────┤
    │          │          │
    ▼          ▼          ▼
  White      Pink    VinylCrackle
  Noise     Noise   (impulse+surface)
    │          │          │
    └────┬─────┴────┬─────┘
         │          │
         ▼          ▼
    [Level Smoothing per channel]
         │
         ▼
    [Channel Mix]
         │
         ▼
    [Master Level]
         │
         ▼
      Output
```
