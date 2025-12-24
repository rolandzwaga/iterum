# Quickstart: Layer 0 Utilities

**Feature**: 017-layer0-utilities
**Date**: 2025-12-24

This guide shows how to integrate the Layer 0 utilities into DSP components.

## BlockContext - Tempo Sync Integration

### Basic Usage

```cpp
#include "dsp/core/block_context.h"

using namespace Iterum::DSP;

// Create context (typically from VST3 ProcessContext)
BlockContext ctx;
ctx.sampleRate = 48000.0;
ctx.tempoBPM = 140.0;
ctx.isPlaying = true;

// Calculate tempo-synced delay time
size_t quarterNoteSamples = ctx.tempoToSamples(NoteValue::Quarter);
// At 140 BPM, 48kHz: ~20571 samples

size_t dottedEighthSamples = ctx.tempoToSamples(NoteValue::Eighth, NoteModifier::Dotted);
// 3/4 of quarter note: ~15428 samples

size_t tripletSamples = ctx.tempoToSamples(NoteValue::Quarter, NoteModifier::Triplet);
// 2/3 of quarter note: ~13714 samples
```

### VST3 Integration (in Processor::process)

```cpp
#include "dsp/core/block_context.h"

tresult PLUGIN_API Processor::process(ProcessData& data) {
    // Populate BlockContext from host
    BlockContext ctx;
    ctx.sampleRate = processSetup.sampleRate;
    ctx.blockSize = data.numSamples;

    if (data.processContext) {
        ctx.tempoBPM = data.processContext->tempo;
        ctx.isPlaying = (data.processContext->state & ProcessContext::kPlaying) != 0;
        ctx.transportPositionSamples = data.processContext->projectTimeSamples;

        if (data.processContext->state & ProcessContext::kTimeSigValid) {
            ctx.timeSignatureNumerator = static_cast<uint8_t>(data.processContext->timeSigNumerator);
            ctx.timeSignatureDenominator = static_cast<uint8_t>(data.processContext->timeSigDenominator);
        }
    }

    // Use context for tempo-synced delay
    if (tempoSyncEnabled_) {
        delayLine_.setDelayTime(ctx.tempoToSamples(noteValue_, noteModifier_));
    }

    // Process audio...
}
```

### Layer 3 Delay Engine Integration

```cpp
// In delay_engine.h (Layer 3)
#include "dsp/core/block_context.h"

class DelayEngine {
public:
    void prepare(const BlockContext& ctx) {
        sampleRate_ = ctx.sampleRate;
        updateDelayTime(ctx);
    }

    void process(float* buffer, size_t numSamples, const BlockContext& ctx) {
        // Update tempo-synced delay if tempo changed
        if (tempoSync_ && std::abs(ctx.tempoBPM - lastTempo_) > 0.01) {
            updateDelayTime(ctx);
            lastTempo_ = ctx.tempoBPM;
        }
        // Process...
    }

private:
    void updateDelayTime(const BlockContext& ctx) {
        if (tempoSync_) {
            delaySamples_ = ctx.tempoToSamples(noteValue_, noteModifier_);
        }
    }
};
```

---

## FastMath - CPU-Critical Path Optimization

### Feedback Saturation

```cpp
#include "dsp/core/fast_math.h"

using namespace Iterum::DSP::FastMath;

// In feedback loop (called per sample)
void processWithSaturation(float* buffer, size_t numSamples, float drive) {
    for (size_t i = 0; i < numSamples; ++i) {
        // Use fastTanh for soft clipping - 2x faster than std::tanh
        buffer[i] = fastTanh(buffer[i] * drive);
    }
}
```

### LFO-Like Modulation

```cpp
#include "dsp/core/fast_math.h"

using namespace Iterum::DSP::FastMath;

// Generate sine modulation without wavetable
float generateSineModulation(double& phase, double phaseIncrement) {
    float output = fastSin(static_cast<float>(phase * kTwoPi));
    phase += phaseIncrement;
    if (phase >= 1.0) phase -= 1.0;
    return output;
}
```

### Exponential Envelope

```cpp
#include "dsp/core/fast_math.h"

using namespace Iterum::DSP::FastMath;

// Exponential decay envelope
class ExpEnvelope {
public:
    float process() noexcept {
        float output = fastExp(-decayRate_ * time_);
        time_ += timeIncrement_;
        return output;
    }

private:
    float decayRate_ = 5.0f;
    float time_ = 0.0f;
    float timeIncrement_ = 0.0001f;
};
```

### Compile-Time Lookup Tables

```cpp
#include "dsp/core/fast_math.h"

using namespace Iterum::DSP::FastMath;

// Pre-compute sine table at compile time
constexpr std::array<float, 256> generateSineTable() {
    std::array<float, 256> table{};
    for (size_t i = 0; i < 256; ++i) {
        float phase = static_cast<float>(i) / 256.0f * kTwoPi;
        table[i] = fastSin(phase);
    }
    return table;
}

inline constexpr auto kSineTable = generateSineTable();
```

---

## Interpolation - Sample Domain Operations

### Delay Line Reading

```cpp
#include "dsp/core/interpolation.h"

using namespace Iterum::DSP::Interpolation;

class DelayLine {
public:
    // Linear interpolation for modulated delays (chorus, flanger)
    float readLinear(float delaySamples) const noexcept {
        size_t index0 = static_cast<size_t>(delaySamples);
        float frac = delaySamples - static_cast<float>(index0);

        float y0 = buffer_[(writeIndex_ - index0) & mask_];
        float y1 = buffer_[(writeIndex_ - index0 - 1) & mask_];

        return linearInterpolate(y0, y1, frac);
    }

    // Cubic interpolation for pitch shifting
    float readCubic(float delaySamples) const noexcept {
        size_t index0 = static_cast<size_t>(delaySamples);
        float frac = delaySamples - static_cast<float>(index0);

        float ym1 = buffer_[(writeIndex_ - index0 + 1) & mask_];
        float y0 = buffer_[(writeIndex_ - index0) & mask_];
        float y1 = buffer_[(writeIndex_ - index0 - 1) & mask_];
        float y2 = buffer_[(writeIndex_ - index0 - 2) & mask_];

        return cubicHermiteInterpolate(ym1, y0, y1, y2, frac);
    }
};
```

### Pitch Shifter Integration

```cpp
#include "dsp/core/interpolation.h"

using namespace Iterum::DSP::Interpolation;

class GranularPitchShifter {
public:
    float readGrain(float position) const noexcept {
        size_t index = static_cast<size_t>(position);
        float frac = position - static_cast<float>(index);

        // Use Lagrange for high-quality pitch shifting
        float ym1 = grainBuffer_[std::max<size_t>(0, index - 1)];
        float y0 = grainBuffer_[index];
        float y1 = grainBuffer_[std::min(grainSize_ - 1, index + 1)];
        float y2 = grainBuffer_[std::min(grainSize_ - 1, index + 2)];

        return lagrangeInterpolate(ym1, y0, y1, y2, frac);
    }
};
```

### Constexpr Interpolation

```cpp
#include "dsp/core/interpolation.h"

using namespace Iterum::DSP::Interpolation;

// Pre-compute interpolated values at compile time
constexpr std::array<float, 10> precomputeRamp() {
    std::array<float, 10> result{};
    for (size_t i = 0; i < 10; ++i) {
        float t = static_cast<float>(i) / 9.0f;
        result[i] = linearInterpolate(0.0f, 1.0f, t);
    }
    return result;
}

inline constexpr auto kPrecomputedRamp = precomputeRamp();
```

---

## Combined Example: Tempo-Synced Delay with Saturation

```cpp
#include "dsp/core/block_context.h"
#include "dsp/core/fast_math.h"
#include "dsp/core/interpolation.h"

using namespace Iterum::DSP;
using namespace Iterum::DSP::FastMath;
using namespace Iterum::DSP::Interpolation;

class TempoSyncDelay {
public:
    void prepare(const BlockContext& ctx) {
        sampleRate_ = ctx.sampleRate;
        updateDelayTime(ctx);
    }

    void process(float* buffer, size_t numSamples, const BlockContext& ctx) {
        // Update delay time if tempo changed
        if (std::abs(ctx.tempoBPM - lastTempo_) > 0.01) {
            updateDelayTime(ctx);
            lastTempo_ = ctx.tempoBPM;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            // Read from delay with modulation (linear interp for LFO mod)
            float modDelay = static_cast<float>(delaySamples_) + modDepth_ * lfo_.process();
            float delayed = readDelayLinear(modDelay);

            // Apply saturation to feedback
            float feedback = fastTanh(delayed * feedbackAmount_ * drive_);

            // Write to delay
            writeDelay(buffer[i] + feedback);

            // Mix output
            buffer[i] = buffer[i] * (1.0f - mix_) + delayed * mix_;
        }
    }

private:
    void updateDelayTime(const BlockContext& ctx) {
        delaySamples_ = ctx.tempoToSamples(noteValue_, noteModifier_);
    }

    float readDelayLinear(float samples) const noexcept {
        size_t i0 = static_cast<size_t>(samples);
        float frac = samples - static_cast<float>(i0);
        return linearInterpolate(
            buffer_[(writeIdx_ - i0) & mask_],
            buffer_[(writeIdx_ - i0 - 1) & mask_],
            frac
        );
    }

    void writeDelay(float sample) noexcept {
        buffer_[writeIdx_] = sample;
        writeIdx_ = (writeIdx_ + 1) & mask_;
    }

    // Parameters
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::Dotted;
    float feedbackAmount_ = 0.5f;
    float drive_ = 1.5f;
    float mix_ = 0.5f;
    float modDepth_ = 10.0f;

    // State
    double sampleRate_ = 44100.0;
    double lastTempo_ = 120.0;
    size_t delaySamples_ = 0;
    size_t writeIdx_ = 0;
    size_t mask_ = 0;
    std::vector<float> buffer_;
    LFO lfo_;  // From primitives layer
};
```

---

## Migration: Updating LFO to Use Layer 0 NoteValue

After implementing this feature, update `src/dsp/primitives/lfo.h`:

```cpp
// BEFORE (in lfo.h)
enum class NoteValue : uint8_t { ... };
enum class NoteModifier : uint8_t { ... };

// AFTER (in lfo.h)
#include "dsp/core/note_value.h"  // Add this include
// Remove the enum definitions - now using Layer 0 versions
```

This ensures consistent enum definitions across the codebase and proper layer dependencies.
