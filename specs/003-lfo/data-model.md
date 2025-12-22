# Data Model: LFO DSP Primitive

**Feature**: 003-lfo
**Date**: 2025-12-22
**Status**: Complete

## Overview

This document defines the class structure, state variables, and enumerations for the LFO (Low Frequency Oscillator) Layer 1 DSP primitive.

---

## Enumerations

### Waveform

Defines the available LFO waveform shapes.

```cpp
enum class Waveform : uint8_t {
    Sine = 0,        // Smooth sinusoidal wave (default)
    Triangle,        // Linear ramp up and down
    Sawtooth,        // Linear ramp from -1 to +1, instant reset
    Square,          // Binary alternation +1 / -1
    SampleHold,      // Random value held for each cycle
    SmoothRandom     // Interpolated random values

    // Count for iteration
    // kNumWaveforms = 6
};
```

### NoteValue

Defines musical note divisions for tempo sync.

```cpp
enum class NoteValue : uint8_t {
    Whole = 0,       // 1/1 - 4 beats
    Half,            // 1/2 - 2 beats
    Quarter,         // 1/4 - 1 beat (default)
    Eighth,          // 1/8 - 0.5 beats
    Sixteenth,       // 1/16 - 0.25 beats
    ThirtySecond     // 1/32 - 0.125 beats

    // kNumNoteValues = 6
};
```

### NoteModifier

Defines timing modifiers for note values.

```cpp
enum class NoteModifier : uint8_t {
    None = 0,        // Normal duration (default)
    Dotted,          // 1.5× duration
    Triplet          // 2/3× duration

    // kNumNoteModifiers = 3
};
```

---

## Class: LFO

### Public Interface

```cpp
class LFO {
public:
    // Lifecycle
    LFO() noexcept = default;
    ~LFO() = default;

    // Non-copyable, movable
    LFO(const LFO&) = delete;
    LFO& operator=(const LFO&) = delete;
    LFO(LFO&&) noexcept = default;
    LFO& operator=(LFO&&) noexcept = default;

    // Initialization (call before audio processing)
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Processing (real-time safe)
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // Parameter setters
    void setWaveform(Waveform waveform) noexcept;
    void setFrequency(float hz) noexcept;
    void setPhaseOffset(float degrees) noexcept;
    void setTempoSync(bool enabled) noexcept;
    void setTempo(float bpm) noexcept;
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;

    // Control
    void retrigger() noexcept;

    // Query
    [[nodiscard]] Waveform waveform() const noexcept;
    [[nodiscard]] float frequency() const noexcept;
    [[nodiscard]] float phaseOffset() const noexcept;
    [[nodiscard]] bool tempoSyncEnabled() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;
};
```

### Private State Variables

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `sampleRate_` | `double` | 0.0 | Current sample rate in Hz |
| `phase_` | `double` | 0.0 | Current phase position [0, 1) |
| `phaseIncrement_` | `double` | 0.0 | Phase advance per sample |
| `phaseOffset_` | `float` | 0.0f | Phase offset in normalized units [0, 1) |
| `frequency_` | `float` | 1.0f | LFO frequency in Hz |
| `bpm_` | `float` | 120.0f | Tempo in beats per minute |
| `waveform_` | `Waveform` | Sine | Current waveform type |
| `noteValue_` | `NoteValue` | Quarter | Tempo sync note value |
| `noteModifier_` | `NoteModifier` | None | Tempo sync note modifier |
| `tempoSync_` | `bool` | false | Tempo sync mode enabled |
| `retriggerEnabled_` | `bool` | true | Whether retrigger() resets phase |

### Private State Variables (Random Waveforms)

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `randomState_` | `uint32_t` | seed | PRNG state |
| `currentRandom_` | `float` | 0.0f | Current S&H value |
| `previousRandom_` | `float` | 0.0f | Smooth random: previous target |
| `targetRandom_` | `float` | 0.0f | Smooth random: current target |

### Private State Variables (Wavetables)

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `wavetables_` | `std::array<std::vector<float>, 4>` | empty | Pre-computed wavetables (Sine, Triangle, Saw, Square) |
| `tableSize_` | `size_t` | 2048 | Samples per wavetable |
| `tableMask_` | `size_t` | 2047 | Bitmask for wrap (tableSize - 1) |

---

## State Transitions

### Lifecycle States

```
┌─────────────────┐
│   Uninitialized │ ← Default constructor
└────────┬────────┘
         │ prepare(sampleRate)
         ▼
┌─────────────────┐
│     Ready       │ ← Wavetables generated, ready for processing
└────────┬────────┘
         │ process() / processBlock()
         ▼
┌─────────────────┐
│   Processing    │ ← Phase advancing, generating output
└────────┬────────┘
         │ reset()
         ▼
         └──────────► Ready (phase reset, state cleared)
```

### Phase Transitions

```
             ┌──────────────────────────────────────────┐
             ▼                                          │
    ┌─────────────────┐                                │
    │  phase_ = 0.0   │ ← reset() or retrigger()       │
    └────────┬────────┘                                │
             │ process()                               │
             ▼                                         │
    ┌─────────────────┐                                │
    │  phase_ += inc  │                                │
    └────────┬────────┘                                │
             │                                         │
             ▼                                         │
    ┌─────────────────────────────────────────┐        │
    │  phase_ >= 1.0?                         │        │
    │  Yes: phase_ -= 1.0, trigger S&H update │────────┘
    │  No: continue                           │
    └─────────────────────────────────────────┘
```

---

## Validation Rules

### Frequency Clamping
- Free-running mode: Clamp to [0.01, 20.0] Hz
- Zero frequency: Output DC at current phase value

### Phase Offset Clamping
- Input in degrees [0, 360)
- Internally normalized to [0, 1)
- Values >= 360 wrapped via modulo

### Tempo Sync
- BPM clamped to [1, 999] (practical DAW range)
- 0 BPM treated as free-running at minimum frequency

### Output Range
- All waveforms output [-1.0, +1.0]
- No additional clamping needed if implementation is correct

---

## Memory Layout

### Estimated Size

| Component | Size |
|-----------|------|
| Wavetables (4 × 2048 × float) | 32,768 bytes |
| State variables | ~64 bytes |
| **Total per instance** | ~33 KB |

### Alignment Considerations
- `std::vector` data is heap-allocated with default alignment
- No special SIMD alignment required for LFO (not processing audio buffers)
- Phase variables are `double` (8-byte aligned)

---

## Thread Safety

- **Single-threaded use**: LFO is designed for single-thread operation
- **Parameter changes**: Safe to call setters from same thread as process()
- **No internal locking**: Real-time safe by design
- **For multi-threaded use**: Caller must synchronize access

---

## Relationships

### Dependencies (Layer 0)
- `<cmath>` - std::sin for wavetable generation
- `<vector>` - wavetable storage
- `<array>` - wavetable container
- `<cstdint>` - fixed-width integers

### Used By (Layer 2+)
- Chorus (modulates DelayLine)
- Flanger (modulates DelayLine)
- Tremolo (modulates gain)
- Auto-pan (modulates stereo field)
- Modulation Matrix (general modulation source)
