# Quickstart: Modulation Matrix

**Feature**: 020-modulation-matrix
**Date**: 2025-12-25

## Basic Usage

### 1. Setup and Initialization

```cpp
#include "dsp/systems/modulation_matrix.h"
#include "dsp/primitives/lfo.h"
#include "dsp/processors/envelope_follower.h"

using namespace Iterum::DSP;

// Create components
ModulationMatrix matrix;
LFO lfo;
EnvelopeFollower envFollower;

// Prepare all components
constexpr double sampleRate = 44100.0;
constexpr size_t blockSize = 512;

lfo.prepare(sampleRate);
envFollower.prepare(sampleRate, blockSize);
matrix.prepare(sampleRate, blockSize, 32);  // 32 max routes
```

### 2. Register Sources and Destinations

```cpp
// Wrap LFO as ModulationSource (if adapter needed)
class LFOSource : public ModulationSource {
    LFO& lfo_;
public:
    explicit LFOSource(LFO& lfo) : lfo_(lfo) {}
    float getCurrentValue() const noexcept override {
        return lfo_.getCurrentValue();  // Assuming LFO has this method
    }
    std::pair<float, float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};
    }
};

LFOSource lfoSource(lfo);

// Register sources
matrix.registerSource(0, &lfoSource);        // Source 0: LFO
// matrix.registerSource(1, &envFollower);   // Source 1: EnvFollower (if it implements interface)

// Register destinations
matrix.registerDestination(0, 0.0f, 100.0f, "Delay Time (ms)");
matrix.registerDestination(1, 200.0f, 8000.0f, "Filter Cutoff (Hz)");
matrix.registerDestination(2, 0.0f, 1.0f, "Feedback Amount");
```

### 3. Create Modulation Routes

```cpp
// LFO → Delay Time, 50% depth, bipolar
int route1 = matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);

// LFO → Filter Cutoff, 30% depth, unipolar (only positive modulation)
int route2 = matrix.createRoute(0, 1, 0.3f, ModulationMode::Unipolar);

// EnvFollower → Feedback, 80% depth, unipolar
// int route3 = matrix.createRoute(1, 2, 0.8f, ModulationMode::Unipolar);
```

### 4. Process Audio

```cpp
void processBlock(float* leftIn, float* rightIn,
                  float* leftOut, float* rightOut,
                  size_t numSamples) {
    // 1. Process LFO to update its internal state
    float lfoValue = lfo.process();  // Or use processBlock for per-sample values

    // 2. Process envelope follower on input
    envFollower.process(leftIn, nullptr, numSamples);

    // 3. Process modulation matrix
    matrix.process(numSamples);

    // 4. Get modulated parameter values
    float baseDelayTime = 50.0f;  // Base delay time in ms
    float baseFilterCutoff = 2000.0f;  // Base filter cutoff in Hz
    float baseFeedback = 0.5f;

    float delayTimeMs = matrix.getModulatedValue(0, baseDelayTime);
    float filterCutoffHz = matrix.getModulatedValue(1, baseFilterCutoff);
    float feedbackAmount = matrix.getModulatedValue(2, baseFeedback);

    // 5. Use modulated values in DSP processing
    delayEngine.setDelayTimeMs(delayTimeMs);
    filter.setCutoff(filterCutoffHz);
    feedbackNetwork.setFeedbackAmount(feedbackAmount);

    // ... rest of audio processing
}
```

## Common Patterns

### Real-Time Depth Changes

```cpp
// Depth can be changed at any time - smoothing prevents clicks
void onMidiCC(int cc, float value) {
    if (cc == 1) {  // Mod wheel
        // Map 0-127 to 0.0-1.0
        float depth = value / 127.0f;
        matrix.setRouteDepth(route1, depth);
    }
}
```

### A/B Comparison

```cpp
// Toggle route on/off for comparison
void onButtonPress(int button) {
    if (button == kCompareButton) {
        bool currentState = matrix.isRouteEnabled(route1);
        matrix.setRouteEnabled(route1, !currentState);
    }
}
```

### UI Feedback

```cpp
// Display current modulation amount in UI
void updateUI() {
    float delayModulation = matrix.getCurrentModulation(0);

    // Display as percentage of range
    float range = 100.0f - 0.0f;  // Delay destination range
    float percent = (delayModulation / range) * 100.0f;

    displayLabel.setText(fmt::format("Delay Mod: {:.1f}%", percent));
}
```

### Multiple Routes to Same Destination

```cpp
// Both LFO and EnvFollower modulate filter cutoff
int lfoToFilter = matrix.createRoute(0, 1, 0.3f, ModulationMode::Bipolar);
int envToFilter = matrix.createRoute(1, 1, 0.5f, ModulationMode::Unipolar);

// Result: filter cutoff = base + (LFO contribution) + (EnvFollower contribution)
// Clamped to [200, 8000] Hz
```

## Bipolar vs Unipolar Mode

### Bipolar Mode (Default)

Source range [-1, +1] maps directly. Use for:
- Delay time wobble (chorus/vibrato)
- Filter sweep (both directions)
- Pitch modulation

```cpp
// LFO at +1.0 with depth=0.5:
// modulation = +1.0 * 0.5 = +0.5 (50% of range positive)
// LFO at -1.0 with depth=0.5:
// modulation = -1.0 * 0.5 = -0.5 (50% of range negative)
matrix.createRoute(sourceId, destId, 0.5f, ModulationMode::Bipolar);
```

### Unipolar Mode

Source range [-1, +1] maps to [0, 1]. Use for:
- Tremolo (gain only goes down, not negative)
- Filter opening (only adds brightness)
- Mix increase

```cpp
// LFO at +1.0 with depth=0.5 and Unipolar:
// First: +1.0 → (1.0 + 1.0) * 0.5 = 1.0
// Then: modulation = 1.0 * 0.5 = +0.5

// LFO at -1.0 with depth=0.5 and Unipolar:
// First: -1.0 → (-1.0 + 1.0) * 0.5 = 0.0
// Then: modulation = 0.0 * 0.5 = 0.0
matrix.createRoute(sourceId, destId, 0.5f, ModulationMode::Unipolar);
```

## Thread Safety Notes

1. **Route Configuration**: Call `registerSource()`, `registerDestination()`, and `createRoute()` during `prepare()` phase only - NOT during audio processing.

2. **Safe During Processing**:
   - `setRouteDepth()` - changes are smoothed
   - `setRouteEnabled()` - toggles with smoothing
   - `getModulatedValue()` - read-only
   - `getCurrentModulation()` - read-only

3. **Not Thread-Safe**: Adding/removing routes or sources during `process()` is undefined behavior.
