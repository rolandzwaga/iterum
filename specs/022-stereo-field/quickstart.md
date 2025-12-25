# Quickstart: Stereo Field

**Feature**: 022-stereo-field
**Date**: 2025-12-25

## Basic Usage

```cpp
#include "dsp/systems/stereo_field.h"

using namespace Iterum::DSP;

// Create and prepare
StereoField stereo;
stereo.prepare(44100.0, 512, 10000.0f);  // 10 second max delay

// Select stereo mode
stereo.setMode(StereoMode::Stereo);

// Configure parameters
stereo.setWidth(150.0f);      // 150% stereo width
stereo.setPan(0.0f);          // Center
stereo.setLROffset(5.0f);     // 5ms offset (R delayed)
stereo.setLRRatio(1.0f);      // 1:1 ratio (equal L/R times)

// Set delay times
stereo.setDelayTimeMs(250.0f);  // 250ms base delay

// Process audio (stereo in/out)
stereo.process(leftIn, rightIn, leftOut, rightOut, numSamples);
```

## Stereo Modes

### Mono Mode
```cpp
stereo.setMode(StereoMode::Mono);
// L+R summed, identical output on both channels
// Width, L/R Offset, and L/R Ratio are ignored
```

### Stereo Mode
```cpp
stereo.setMode(StereoMode::Stereo);
stereo.setLRRatio(0.75f);  // L delay = 75% of R delay
// Independent L/R delays for polyrhythmic patterns
```

### Ping-Pong Mode
```cpp
stereo.setMode(StereoMode::PingPong);
stereo.setFeedback(50.0f);  // 50% feedback for repeating ping-pong
// Delays alternate between L and R channels
```

### Dual Mono Mode
```cpp
stereo.setMode(StereoMode::DualMono);
stereo.setPan(-30.0f);  // Pan 30% to the left
// Same delay time for both, but can pan the output
```

### Mid/Side Mode
```cpp
stereo.setMode(StereoMode::MidSide);
stereo.setWidth(180.0f);  // Enhanced stereo width
// Mid and Side components delayed independently
```

## Width Control

```cpp
// Mono (width = 0%)
stereo.setWidth(0.0f);
// L and R outputs are identical

// Normal stereo (width = 100%)
stereo.setWidth(100.0f);
// Original stereo image preserved

// Enhanced stereo (width = 200%)
stereo.setWidth(200.0f);
// Side component doubled for wider image
```

## Panning

```cpp
// Full left
stereo.setPan(-100.0f);

// Center (default)
stereo.setPan(0.0f);

// Full right
stereo.setPan(100.0f);

// Uses constant-power pan law for consistent perceived volume
```

## L/R Offset (Haas Effect)

```cpp
// No offset
stereo.setLROffset(0.0f);

// R delayed by 10ms (creates width perception)
stereo.setLROffset(10.0f);

// L delayed by 10ms
stereo.setLROffset(-10.0f);

// Range: ±50ms
```

## L/R Ratio (Polyrhythmic)

```cpp
// Equal times (1:1)
stereo.setLRRatio(1.0f);
// At 400ms base: L = 400ms, R = 400ms

// 3:4 ratio
stereo.setLRRatio(0.75f);
// At 400ms base: L = 300ms, R = 400ms

// 2:3 ratio
stereo.setLRRatio(0.667f);
// At 400ms base: L = 267ms, R = 400ms

// Range: 0.1 to 10.0
```

## Smooth Transitions

```cpp
// Mode transitions are automatically crossfaded (50ms)
stereo.setMode(StereoMode::PingPong);  // No click

// Parameter changes are smoothed (20ms)
stereo.setWidth(0.0f);    // Smooth fade to mono
stereo.setWidth(200.0f);  // Smooth fade to wide
```

## Reset

```cpp
// Clear all internal delay buffers
stereo.reset();
// Use after transport stop or when starting fresh playback
```

## Integration with Delay Plugin

```cpp
// In your plugin processor:
class DelayProcessor {
    DelayEngine mainDelay_;
    FeedbackNetwork feedback_;
    StereoField stereoField_;

    void processBlock(float** inputs, float** outputs, size_t numSamples) {
        // Main delay processing
        mainDelay_.process(...);

        // Feedback processing
        feedback_.process(...);

        // Stereo field processing (width, pan, ping-pong, etc.)
        stereoField_.process(tempL, tempR, outputs[0], outputs[1], numSamples);
    }
};
```
