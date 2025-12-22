# Quickstart: LFO DSP Primitive

**Feature**: 003-lfo
**Location**: `src/dsp/primitives/lfo.h`
**Namespace**: `Iterum::DSP`

## Installation

Include the header in your source file:

```cpp
#include "dsp/primitives/lfo.h"
```

## Basic Usage

### Simple Sine LFO

```cpp
#include "dsp/primitives/lfo.h"

using namespace Iterum::DSP;

// Create and prepare
LFO lfo;
lfo.prepare(44100.0);  // Sample rate

// Configure
lfo.setWaveform(Waveform::Sine);
lfo.setFrequency(2.0f);  // 2 Hz

// In audio callback - generate modulation
float modulation = lfo.process();  // Returns [-1, +1]
```

### Process Block

```cpp
// More efficient for processing multiple samples
std::array<float, 512> modBuffer;
lfo.processBlock(modBuffer.data(), modBuffer.size());
```

## Waveforms

### Available Waveforms

```cpp
lfo.setWaveform(Waveform::Sine);        // Smooth sinusoid
lfo.setWaveform(Waveform::Triangle);    // Linear ramp up/down
lfo.setWaveform(Waveform::Sawtooth);    // Linear ramp, instant reset
lfo.setWaveform(Waveform::Square);      // Binary +1/-1
lfo.setWaveform(Waveform::SampleHold);  // Random step
lfo.setWaveform(Waveform::SmoothRandom); // Interpolated random
```

### Waveform Characteristics

| Waveform | Character | Best For |
|----------|-----------|----------|
| Sine | Smooth, natural | Chorus, vibrato, tremolo |
| Triangle | Linear, symmetric | Filter sweeps, subtle modulation |
| Sawtooth | Rising sweep | Synth-style filter movement |
| Square | Abrupt transitions | Rhythmic gating, trance effects |
| SampleHold | Random steps | Glitchy, experimental |
| SmoothRandom | Organic variation | Natural movement, subtle drift |

## Tempo Sync

### Basic Tempo Sync

```cpp
// Enable tempo sync
lfo.setTempoSync(true);

// Set tempo from host
lfo.setTempo(120.0f);  // BPM

// Set note value
lfo.setNoteValue(NoteValue::Quarter);  // 1 cycle per beat
```

### Note Values with Modifiers

```cpp
// Dotted notes (1.5x duration)
lfo.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);

// Triplet notes (2/3x duration)
lfo.setNoteValue(NoteValue::Quarter, NoteModifier::Triplet);

// Available note values
NoteValue::Whole        // 1/1 - 4 beats
NoteValue::Half         // 1/2 - 2 beats
NoteValue::Quarter      // 1/4 - 1 beat
NoteValue::Eighth       // 1/8 - 0.5 beats
NoteValue::Sixteenth    // 1/16 - 0.25 beats
NoteValue::ThirtySecond // 1/32 - 0.125 beats
```

### Tempo Sync Timing Examples (at 120 BPM)

| Note Value | Modifier | Cycle Duration |
|------------|----------|----------------|
| 1/4 | None | 500 ms |
| 1/4 | Dotted | 750 ms |
| 1/4 | Triplet | 333 ms |
| 1/8 | None | 250 ms |
| 1/8 | Dotted | 375 ms |
| 1/2 | Triplet | 667 ms |

## Phase Control

### Phase Offset

```cpp
// Create stereo width with phase-shifted LFOs
LFO lfoLeft;
LFO lfoRight;

lfoLeft.prepare(44100.0);
lfoRight.prepare(44100.0);

lfoLeft.setFrequency(1.0f);
lfoRight.setFrequency(1.0f);

lfoLeft.setPhaseOffset(0.0f);    // Start at 0 degrees
lfoRight.setPhaseOffset(90.0f);  // Start at 90 degrees (quadrature)

// Now lfoLeft and lfoRight are 90 degrees out of phase
```

### Retrigger

```cpp
// Enable retrigger (default)
lfo.setRetriggerEnabled(true);

// On note-on event, reset phase
void onNoteOn() {
    lfo.retrigger();  // Phase resets to phaseOffset
}

// For free-running (no reset on note-on)
lfo.setRetriggerEnabled(false);
```

## Common Use Cases

### Chorus Effect (with DelayLine)

```cpp
#include "dsp/primitives/delay_line.h"
#include "dsp/primitives/lfo.h"

DelayLine delay;
LFO lfo;

void prepare(double sampleRate) {
    delay.prepare(sampleRate, 0.05f);  // 50ms max delay
    lfo.prepare(sampleRate);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(0.5f);  // 0.5 Hz modulation rate
}

float processChorus(float input) {
    // Modulation depth: 5ms center, +/- 2ms modulation
    float modulation = lfo.process();
    float delayMs = 5.0f + 2.0f * modulation;  // 3-7ms
    float delaySamples = delayMs * 0.001f * sampleRate;

    delay.write(input);
    float delayed = delay.readLinear(delaySamples);

    // Mix dry + wet
    return 0.5f * input + 0.5f * delayed;
}
```

### Tremolo Effect

```cpp
LFO lfo;

void prepare(double sampleRate) {
    lfo.prepare(sampleRate);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(4.0f);  // 4 Hz tremolo rate
}

float processTremolo(float input, float depth) {
    // Convert bipolar [-1, +1] to unipolar [0, 1]
    float modulation = (lfo.process() + 1.0f) * 0.5f;

    // Apply depth: 1.0 = full tremolo, 0.0 = no effect
    float gain = 1.0f - depth * (1.0f - modulation);

    return input * gain;
}
```

### Auto-Pan Effect

```cpp
LFO lfo;

void prepare(double sampleRate) {
    lfo.prepare(sampleRate);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(0.25f);  // Slow pan
}

void processAutoPan(float input, float& left, float& right) {
    float pan = lfo.process();  // -1 = left, +1 = right

    // Equal power panning
    float angle = (pan + 1.0f) * 0.25f * 3.14159f;  // 0 to PI/2
    left = input * std::cos(angle);
    right = input * std::sin(angle);
}
```

### Tempo-Synced Tremolo

```cpp
LFO lfo;

void prepare(double sampleRate) {
    lfo.prepare(sampleRate);
    lfo.setWaveform(Waveform::Square);  // Hard gating
    lfo.setTempoSync(true);
    lfo.setNoteValue(NoteValue::Eighth);  // Gate on 8th notes
}

void onTempoChange(float bpm) {
    lfo.setTempo(bpm);
}

float processGate(float input) {
    float gate = (lfo.process() + 1.0f) * 0.5f;  // 0 or 1
    return input * gate;
}
```

## Performance Notes

- **Real-time safe**: All process methods are noexcept with no allocations
- **O(1) per sample**: Constant time regardless of settings
- **Wavetable size**: 2048 samples per waveform (pre-allocated in prepare())
- **Double precision phase**: Prevents drift over long sessions

## API Reference

See [contracts/lfo.h](contracts/lfo.h) for the complete API contract.

## See Also

- [spec.md](spec.md) - Full feature specification
- [research.md](research.md) - Algorithm details and design decisions
- [data-model.md](data-model.md) - Class structure and state variables
- [DelayLine](../../002-delay-line/quickstart.md) - Circular buffer delay (used with LFO for chorus/flanger)
