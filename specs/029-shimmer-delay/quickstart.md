# Quickstart: Shimmer Delay Mode

**Feature**: 029-shimmer-delay

## Basic Usage

```cpp
#include "dsp/features/shimmer_delay.h"

using namespace Iterum::DSP;

// Create shimmer delay
ShimmerDelay shimmer;

// Prepare for processing
shimmer.prepare(44100.0, 512, 5000.0f);  // 5 second max delay

// Configure classic shimmer sound
shimmer.setPitchSemitones(12.0f);       // Octave up
shimmer.setShimmerMix(100.0f);          // Full shimmer
shimmer.setFeedbackAmount(0.6f);        // 60% feedback
shimmer.setDiffusionAmount(70.0f);      // Lush diffusion
shimmer.setDryWetMix(50.0f);            // 50/50 mix

// Snap parameters (no smoothing on first call)
shimmer.snapParameters();

// In audio callback
void processAudio(float* left, float* right, size_t numSamples,
                  const BlockContext& ctx) {
    shimmer.process(left, right, numSamples, ctx);
}
```

## Tempo-Synced Shimmer

```cpp
// Enable tempo sync
shimmer.setTimeMode(TimeMode::Synced);
shimmer.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);

// Tempo is read from BlockContext in process()
```

## Subtle Shimmer (Blend with Clean Delay)

```cpp
// Only 30% of feedback is pitch-shifted
shimmer.setShimmerMix(30.0f);
shimmer.setPitchSemitones(12.0f);
shimmer.setFeedbackAmount(0.5f);
```

## Downward Shimmer

```cpp
// Octave down creates "descending" effect
shimmer.setPitchSemitones(-12.0f);
shimmer.setShimmerMix(100.0f);
```

## High-Quality Mode (Higher Latency)

```cpp
// Use PhaseVocoder for best quality
shimmer.setPitchMode(PitchMode::PhaseVocoder);

// Check latency for host compensation
size_t latency = shimmer.getLatencySamples();
```

## With Modulation

```cpp
ModulationMatrix matrix;
matrix.prepare(44100.0, 512, 32);

// Create LFO
LFO lfo;
lfo.prepare(44100.0, 512);
lfo.setRate(0.5f);  // 0.5 Hz

// Register and route
matrix.registerSource(0, &lfo);
matrix.registerDestination(0, -24.0f, 24.0f, "Pitch");
matrix.createRoute(0, 0, 0.1f, ModulationMode::Bipolar);  // 10% depth

// Connect to shimmer
shimmer.connectModulationMatrix(&matrix);
```

## Test Scenarios

### SC-001: Pitch Accuracy
```cpp
shimmer.setPitchSemitones(12.0f);
shimmer.setPitchCents(0.0f);
// Verify output is exactly 2× input frequency
```

### SC-003: Shimmer Mix at 0%
```cpp
shimmer.setShimmerMix(0.0f);
// Verify no pitch shifting occurs (standard delay)
```

### SC-005: Feedback Stability
```cpp
shimmer.setFeedbackAmount(1.2f);  // 120%
shimmer.setPitchSemitones(12.0f);
// Process for 10 seconds
// Verify output never exceeds +6 dBFS
```
