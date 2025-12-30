# Quickstart: Mode Switch Click-Free Transitions

**Feature Branch**: `041-mode-switch-clicks`
**Date**: 2025-12-30

## Overview

This guide provides step-by-step implementation instructions for adding click-free mode transitions to the Iterum delay plugin. The solution uses a 50ms equal-power crossfade between the old and new mode outputs when switching.

**Key Design Decision**: Extract crossfade utilities to Layer 0 for reuse, then refactor existing duplicates.

## Prerequisites

Before starting implementation:

1. **Read these files** (Constitution Principle XII):
   - `specs/TESTING-GUIDE.md` - Test patterns
   - `specs/VST-GUIDE.md` - Framework pitfalls

2. **Verify branch**:
   ```bash
   git checkout 041-mode-switch-clicks
   ```

## Implementation Steps

### Step 1: Create Layer 0 Crossfade Utility

**File**: `src/dsp/core/crossfade_utils.h` (NEW)

Create the shared crossfade utility that will be used by all components:

```cpp
// ==============================================================================
// Layer 0: Core Utility - Crossfade Utilities
// ==============================================================================
// Shared crossfade math for smooth audio transitions.
//
// Constitution Compliance:
// - Principle IX: Layer 0 (no dependencies except standard library)
// - Principle XIV: ODR Prevention (single definition for all consumers)
// ==============================================================================

#pragma once

#include <cmath>
#include <utility>

namespace Iterum {
namespace DSP {

/// Pi/2 constant for crossfade calculations
constexpr float kHalfPi = 1.5707963267948966f;

/// Calculate equal-power crossfade gains (constant power: fadeOut² + fadeIn² ≈ 1)
/// @param position Crossfade position [0.0 = start, 1.0 = complete]
/// @param fadeOut Output gain for outgoing signal (1.0 → 0.0)
/// @param fadeIn Output gain for incoming signal (0.0 → 1.0)
inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept {
    fadeOut = std::cos(position * kHalfPi);
    fadeIn = std::sin(position * kHalfPi);
}

/// Single-call version returning both gains as a pair
/// @param position Crossfade position [0.0 = start, 1.0 = complete]
/// @return {fadeOut, fadeIn} gains
[[nodiscard]] inline std::pair<float, float> equalPowerGains(float position) noexcept {
    return {std::cos(position * kHalfPi), std::sin(position * kHalfPi)};
}

/// Calculate crossfade increment for given duration and sample rate
/// @param durationMs Crossfade duration in milliseconds
/// @param sampleRate Sample rate in Hz
/// @return Per-sample increment value
[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept {
    const float samples = durationMs * 0.001f * static_cast<float>(sampleRate);
    return (samples > 0.0f) ? (1.0f / samples) : 1.0f;
}

} // namespace DSP
} // namespace Iterum
```

### Step 2: Write Layer 0 Unit Tests

**File**: `tests/unit/core/crossfade_utils_tests.cpp` (NEW)

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/core/crossfade_utils.h"

using namespace Iterum::DSP;
using Catch::Approx;

TEST_CASE("equalPowerGains at boundaries", "[dsp][core][crossfade]") {
    float fadeOut, fadeIn;

    SECTION("position 0.0 - start of crossfade") {
        equalPowerGains(0.0f, fadeOut, fadeIn);
        REQUIRE(fadeOut == Approx(1.0f));
        REQUIRE(fadeIn == Approx(0.0f));
    }

    SECTION("position 1.0 - end of crossfade") {
        equalPowerGains(1.0f, fadeOut, fadeIn);
        REQUIRE(fadeOut == Approx(0.0f).margin(1e-6f));
        REQUIRE(fadeIn == Approx(1.0f));
    }

    SECTION("position 0.5 - midpoint") {
        equalPowerGains(0.5f, fadeOut, fadeIn);
        // At midpoint, both should be ~0.707 (sqrt(2)/2)
        REQUIRE(fadeOut == Approx(0.7071f).margin(0.001f));
        REQUIRE(fadeIn == Approx(0.7071f).margin(0.001f));
    }
}

TEST_CASE("equalPowerGains maintains constant power", "[dsp][core][crossfade]") {
    // fadeOut² + fadeIn² should ≈ 1.0 at all positions
    for (float pos = 0.0f; pos <= 1.0f; pos += 0.1f) {
        float fadeOut, fadeIn;
        equalPowerGains(pos, fadeOut, fadeIn);
        float totalPower = fadeOut * fadeOut + fadeIn * fadeIn;
        REQUIRE(totalPower == Approx(1.0f).margin(0.001f));
    }
}

TEST_CASE("crossfadeIncrement calculates correctly", "[dsp][core][crossfade]") {
    // 50ms at 44100 Hz = 2205 samples
    // increment = 1/2205 ≈ 0.000453
    float inc = crossfadeIncrement(50.0f, 44100.0);
    REQUIRE(inc == Approx(1.0f / 2205.0f).margin(1e-6f));
}
```

### Step 3: Refactor CharacterProcessor to Use Shared Utility

**File**: `src/dsp/systems/character_processor.h`

Add include and replace inline crossfade math:

```cpp
// Add include at top
#include "dsp/core/crossfade_utils.h"

// In processChunk() around line 256, replace:
//   float fadeOut = std::cos(crossfadePosition_ * 1.5707963f);
//   float fadeIn = std::sin(crossfadePosition_ * 1.5707963f);
// With:
    float fadeOut, fadeIn;
    equalPowerGains(crossfadePosition_, fadeOut, fadeIn);
```

### Step 4: Add Crossfade State to Processor Header

**File**: `src/processor/processor.h`

Add include and crossfade state:

```cpp
// Add include
#include "dsp/core/crossfade_utils.h"

private:
    // Existing
    std::atomic<int> mode_{5};  // DelayMode enum value (5 = Digital)

    // NEW: Mode crossfade state
    int currentProcessingMode_ = 5;     // Current mode being processed
    int previousMode_ = 5;              // Previous mode for crossfade
    float crossfadePosition_ = 1.0f;    // 1.0 = not crossfading, 0.0 = start
    float crossfadeIncrement_ = 0.0f;   // Per-sample increment

    // NEW: Crossfade work buffers (pre-allocated)
    std::vector<float> crossfadeBufferL_;
    std::vector<float> crossfadeBufferR_;

    // NEW: Crossfade duration constant
    static constexpr float kCrossfadeTimeMs = 50.0f;
```

Add helper method declaration:

```cpp
private:
    // NEW: Process a specific mode
    void processMode(int mode, const float* inputL, const float* inputR,
                     float* outputL, float* outputR, size_t numSamples,
                     const DSP::BlockContext& ctx) noexcept;
```

### Step 5: Allocate Buffers in setupProcessing

**File**: `src/processor/processor.cpp`

In `setupProcessing()`, after existing buffer allocations:

```cpp
Steinberg::tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;

    // ... existing buffer allocations ...

    // NEW: Allocate crossfade work buffers
    crossfadeBufferL_.resize(static_cast<size_t>(maxBlockSize_));
    crossfadeBufferR_.resize(static_cast<size_t>(maxBlockSize_));
    std::fill(crossfadeBufferL_.begin(), crossfadeBufferL_.end(), 0.0f);
    std::fill(crossfadeBufferR_.begin(), crossfadeBufferR_.end(), 0.0f);

    // NEW: Calculate crossfade increment using Layer 0 utility
    crossfadeIncrement_ = DSP::crossfadeIncrement(kCrossfadeTimeMs, sampleRate_);

    // Initialize crossfade state
    currentProcessingMode_ = mode_.load(std::memory_order_relaxed);
    previousMode_ = currentProcessingMode_;
    crossfadePosition_ = 1.0f;

    return AudioEffect::setupProcessing(setup);
}
```

### Step 6: Extract processMode Helper

**File**: `src/processor/processor.cpp`

Create a helper method that processes a specific mode. This refactors the existing switch statement:

```cpp
void Processor::processMode(int mode, const float* inputL, const float* inputR,
                            float* outputL, float* outputR, size_t numSamples,
                            const DSP::BlockContext& ctx) noexcept {
    switch (static_cast<DelayMode>(mode)) {
        case DelayMode::Granular:
            // Update Granular parameters (existing code)
            granularDelay_.setGrainSize(granularParams_.grainSize.load(std::memory_order_relaxed));
            // ... all other parameter updates ...
            granularDelay_.process(inputL, inputR, outputL, outputR, numSamples, ctx);
            break;

        case DelayMode::Spectral:
            // ... existing Spectral code ...
            // Note: Copy input to output first for in-place processing
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            spectralDelay_.process(outputL, outputR, numSamples, ctx);
            break;

        // ... all other modes (Shimmer, Tape, BBD, Digital, PingPong, Reverse, MultiTap, Freeze, Ducking) ...
    }
}
```

### Step 7: Add Crossfade Logic to process()

**File**: `src/processor/processor.cpp`

Replace the existing mode switch logic with crossfade-aware processing:

```cpp
// In process(), after reading transport and creating ctx:

// ==========================================================================
// Mode Crossfade Logic
// ==========================================================================

const int newMode = mode_.load(std::memory_order_relaxed);
const size_t numSamples = static_cast<size_t>(data.numSamples);

// Detect mode change - initiate crossfade
if (newMode != currentProcessingMode_) {
    previousMode_ = currentProcessingMode_;
    currentProcessingMode_ = newMode;
    crossfadePosition_ = 0.0f;  // Start/restart crossfade
}

// Process with crossfade if transitioning
if (crossfadePosition_ < 1.0f) {
    // Process NEW mode into output buffers
    processMode(currentProcessingMode_, inputL, inputR, outputL, outputR, numSamples, ctx);

    // Process OLD mode into crossfade work buffers
    processMode(previousMode_, inputL, inputR,
                crossfadeBufferL_.data(), crossfadeBufferR_.data(), numSamples, ctx);

    // Equal-power crossfade blend using Layer 0 utility
    for (size_t i = 0; i < numSamples; ++i) {
        float fadeOut, fadeIn;
        DSP::equalPowerGains(crossfadePosition_, fadeOut, fadeIn);

        outputL[i] = crossfadeBufferL_[i] * fadeOut + outputL[i] * fadeIn;
        outputR[i] = crossfadeBufferR_[i] * fadeOut + outputR[i] * fadeIn;

        crossfadePosition_ += crossfadeIncrement_;
        if (crossfadePosition_ >= 1.0f) {
            crossfadePosition_ = 1.0f;
            break;
        }
    }
} else {
    // Normal single-mode processing
    processMode(currentProcessingMode_, inputL, inputR, outputL, outputR, numSamples, ctx);
}
```

### Step 8 (Optional): Upgrade CrossfadingDelayLine

**File**: `src/dsp/primitives/crossfading_delay_line.h`

Optionally replace linear crossfade with equal-power for smoother delay time changes:

```cpp
// Add include
#include "dsp/core/crossfade_utils.h"

// In read() method, replace linear gain changes with equal-power:
// Instead of:
//   tapAGain_ -= crossfadeIncrement_;
//   tapBGain_ += crossfadeIncrement_;
// Use position-based equal-power gains
```

## Testing

### Unit Tests for Mode Crossfade

Create `tests/unit/processor/mode_crossfade_tests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

TEST_CASE("Mode crossfade produces no discontinuity", "[processor][crossfade]") {
    // Test that switching modes during audio produces smooth transition
    // Verify no sample-to-sample jump > threshold (e.g., 0.1)
}

TEST_CASE("Crossfade completes in expected time", "[processor][crossfade]") {
    // Verify crossfade duration is ~50ms
    // At 44100 Hz, should be ~2205 samples
}

TEST_CASE("Rapid mode switching is stable", "[processor][crossfade]") {
    // Switch modes 10 times per second
    // Verify no cumulative artifacts or crashes
}
```

### Manual Verification

1. Load plugin in DAW
2. Play continuous audio through plugin
3. Switch between modes while audio plays
4. Listen for clicks/pops (should be none)
5. Test all mode combinations

## Build Verification

```bash
# Build
cmake --build build --config Release --target Iterum dsp_tests

# Run tests
ctest --test-dir build -C Release --output-on-failure

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"
```

## Implementation Order Summary

1. **Layer 0**: Create `crossfade_utils.h` with `equalPowerGains()` and `crossfadeIncrement()`
2. **Tests**: Write unit tests for Layer 0 utility
3. **Refactor**: Update CharacterProcessor to use shared utility
4. **Processor**: Add crossfade state and buffers
5. **Processor**: Implement crossfade logic using shared utility
6. **Tests**: Write mode transition tests
7. **Optional**: Upgrade CrossfadingDelayLine to equal-power

## Key Benefits of Layer 0 Extraction

| Benefit | Description |
|---------|-------------|
| **DRY** | Single implementation instead of 4 duplicates |
| **Tested** | Unit tests cover the math once |
| **Maintainable** | Fix bugs in one place |
| **Consistent** | All crossfades behave identically |
| **Documented** | API docs in one location |

## Success Criteria Verification

| Criterion | How to Verify |
|-----------|---------------|
| SC-001: Zero audible clicks | Listen test with all mode combinations |
| SC-002: <50ms transition | Count samples: ~2205 at 44.1kHz |
| SC-003: RMS <3dB spike | Measure peak level during transition |
| SC-004: All 110 combinations | Systematic test matrix |
| SC-005: Rapid switching stable | Automate 10 switches/sec |
