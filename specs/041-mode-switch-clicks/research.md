# Research: Mode Switch Click-Free Transitions

**Feature Branch**: `041-mode-switch-clicks`
**Date**: 2025-12-30
**Researcher**: Claude Code

## Executive Summary

The audible clicks during mode switching are caused by **instantaneous buffer discontinuity** - when the user switches from one delay mode to another, the output immediately jumps from one delay engine's buffer content to another's with no transition. The fix is to implement a 50ms equal-power crossfade between the old and new mode outputs during transitions.

## Root Cause Analysis

### Current Implementation

**Location**: [processor.cpp:215-218](src/processor/processor.cpp#L215-L218)

```cpp
const int currentMode = mode_.load(std::memory_order_relaxed);
const size_t numSamples = static_cast<size_t>(data.numSamples);

switch (static_cast<DelayMode>(currentMode)) {
    case DelayMode::Granular:
        // Process only current mode
        granularDelay_.process(...);
        break;
    // ... other modes
}
```

**Problem**: When `mode_` changes between process blocks, the output instantaneously switches from one delay engine to another. If the old engine had audio at +0.5 and the new engine has audio at -0.3, the output jumps from +0.5 to -0.3 in a single sample = **click**.

### Why This Causes Clicks

1. **Different Buffer Contents**: Each mode (Granular, Spectral, Tape, etc.) has its own delay buffer. These buffers contain different audio at any given moment.

2. **No Transition Logic**: The mode switch happens at the atomic `mode_` level with no fade between old and new outputs.

3. **Potentially Large Amplitude Jump**: If switching during a loud transient or with high feedback, the jump can be significant (several dB).

## Existing Crossfade Patterns in Codebase

Three existing implementations provide reference patterns:

### 1. CharacterProcessor (Recommended Pattern)

**Location**: [character_processor.h:244-271](src/dsp/systems/character_processor.h#L244-L271)

```cpp
static constexpr float kCrossfadeTimeMs = 50.0f;

void setMode(CharacterMode mode) noexcept {
    if (mode != currentMode_) {
        previousMode_ = currentMode_;
        currentMode_ = mode;
        crossfadePosition_ = 0.0f; // Start crossfade
    }
}

// During process (lines 244-271):
if (isCrossfading()) {
    // Copy input for previous mode processing
    std::copy(buffer, buffer + numSamples, previousModeBufferL_.data());

    // Process current mode
    processMode(buffer, numSamples, currentMode_);

    // Process previous mode
    processMode(previousModeBufferL_.data(), numSamples, previousMode_);

    // Crossfade between modes (equal power)
    for (size_t i = 0; i < numSamples; ++i) {
        float fadeOut = std::cos(crossfadePosition_ * 1.5707963f); // pi/2
        float fadeIn = std::sin(crossfadePosition_ * 1.5707963f);

        buffer[i] = previousModeBufferL_[i] * fadeOut + buffer[i] * fadeIn;

        crossfadePosition_ += crossfadeIncrement_;
        if (crossfadePosition_ >= 1.0f) {
            crossfadePosition_ = 1.0f;
            break;
        }
    }
}
```

**Key Features**:
- 50ms equal-power crossfade using sin/cos
- Stores `previousMode_` and `currentMode_`
- Pre-allocated `previousModeBufferL_` work buffer
- `crossfadePosition_` goes 0.0 → 1.0
- `crossfadeIncrement_` calculated from sample rate

### 2. StereoField

**Location**: [stereo_field.h:312-327](src/dsp/systems/stereo_field.h#L312-L327)

```cpp
static constexpr float kTransitionTimeMs = 50.0f;

LinearRamp transitionRamp_;          // Mode crossfade (50ms)
bool transitioning_ = false;

inline void setMode(StereoMode mode) noexcept {
    // Note: Currently resets delay lines instead of crossfading
    delayL_.reset();
    delayR_.reset();
    currentMode_ = mode;
}
```

**Key Features**:
- Has infrastructure for 50ms crossfade but currently resets buffers
- Uses `LinearRamp` instead of sin/cos
- Shows that even within our codebase, mode transitions need work

### 3. CrossfadingDelayLine

**Location**: [crossfading_delay_line.h](src/dsp/primitives/crossfading_delay_line.h)

```cpp
static constexpr float kDefaultCrossfadeTimeMs = 20.0f;

// Two-tap crossfade for delay time changes
float tapAGain_ = 1.0f;
float tapBGain_ = 0.0f;
bool crossfading_ = false;
```

**Key Features**:
- Different purpose (delay time changes, not mode changes)
- Linear crossfade (simpler than equal-power)
- 20ms default time (shorter than mode transitions)

## Proposed Solution

### Architecture

Add mode crossfade logic directly to `Processor` class:

```cpp
// processor.h additions:
std::atomic<int> mode_{5};
int previousMode_ = 5;              // NEW: Track previous mode for crossfade
float crossfadePosition_ = 1.0f;    // NEW: 1.0 = not crossfading
float crossfadeIncrement_ = 0.0f;   // NEW: Per-sample increment

// Work buffers for crossfade (pre-allocated in setupProcessing)
std::vector<float> crossfadeBufferL_;  // NEW
std::vector<float> crossfadeBufferR_;  // NEW
```

### Crossfade Logic (Equal Power)

```cpp
// In process():
const int newMode = mode_.load(std::memory_order_relaxed);

// Detect mode change
if (newMode != currentProcessingMode_) {
    previousMode_ = currentProcessingMode_;
    currentProcessingMode_ = newMode;
    crossfadePosition_ = 0.0f;  // Start crossfade
}

// If crossfading, process both modes
if (crossfadePosition_ < 1.0f) {
    // Copy input for previous mode
    std::copy_n(inputL, numSamples, crossfadeBufferL_.data());
    std::copy_n(inputR, numSamples, crossfadeBufferR_.data());

    // Process current mode into output
    processMode(currentProcessingMode_, inputL, inputR, outputL, outputR, numSamples, ctx);

    // Process previous mode into crossfade buffers
    processMode(previousMode_, inputL, inputR,
                crossfadeBufferL_.data(), crossfadeBufferR_.data(), numSamples, ctx);

    // Equal-power crossfade
    for (size_t i = 0; i < numSamples; ++i) {
        const float fadeOut = std::cos(crossfadePosition_ * kHalfPi);
        const float fadeIn = std::sin(crossfadePosition_ * kHalfPi);

        outputL[i] = crossfadeBufferL_[i] * fadeOut + outputL[i] * fadeIn;
        outputR[i] = crossfadeBufferR_[i] * fadeOut + outputR[i] * fadeIn;

        crossfadePosition_ += crossfadeIncrement_;
        if (crossfadePosition_ >= 1.0f) {
            crossfadePosition_ = 1.0f;
            break;
        }
    }
} else {
    // Normal processing - single mode
    processMode(currentProcessingMode_, inputL, inputR, outputL, outputR, numSamples, ctx);
}
```

### Crossfade Duration

Per spec FR-003: "The fade duration MUST be short enough to feel responsive (under 50ms perceived latency)"

**Recommendation**: 50ms (2205 samples at 44.1kHz)
- Matches CharacterProcessor and StereoField patterns
- Long enough to prevent clicks
- Short enough to feel instant to user

### Helper Function

Refactor mode processing into a helper to avoid code duplication:

```cpp
void Processor::processMode(int mode, const float* inL, const float* inR,
                            float* outL, float* outR, size_t numSamples,
                            const DSP::BlockContext& ctx) noexcept {
    switch (static_cast<DelayMode>(mode)) {
        case DelayMode::Granular:
            // Update params + process
            break;
        // ... other modes
    }
}
```

## Challenges and Mitigations

### Challenge 1: Double Processing Cost During Crossfade

**Issue**: During the 50ms crossfade, both modes must process simultaneously = 2x CPU.

**Mitigation**:
- 50ms is only ~2205 samples at 44.1kHz
- Most audio blocks are 128-512 samples
- Only 4-17 blocks affected per mode switch
- User doesn't switch modes rapidly (this isn't a tight modulation loop)

### Challenge 2: Different Processing Patterns

**Issue**: Some modes process in-place (Spectral, Tape), others use separate input/output.

**Mitigation**:
- The helper function can normalize this - always copy input to output first
- Each mode's processing already handles this internally

### Challenge 3: Rapid Mode Switching (FR-006)

**Issue**: What if user switches modes during an active crossfade?

**Mitigation**:
- When a new switch occurs during crossfade, just update `previousMode_` to current target and restart crossfade
- Previous mode's output will smoothly transition to new mode

## Components to Reuse

| Component | Location | Purpose |
|-----------|----------|---------|
| `OnePoleSmoother` | smoother.h | Could smooth crossfade position (optional) |
| `LinearRamp` | smoother.h | Alternative to manual crossfade math |
| Equal-power formula | character_processor.h:256-258 | sin/cos crossfade gains |

## Components to Create

| Component | Location | Purpose |
|-----------|----------|---------|
| `crossfadeBufferL_` | processor.h | Work buffer for previous mode L output |
| `crossfadeBufferR_` | processor.h | Work buffer for previous mode R output |
| `crossfadePosition_` | processor.h | Current position in crossfade [0,1] |
| `previousMode_` | processor.h | Track which mode we're fading from |
| `processMode()` | processor.cpp | Helper to process a specific mode |

## Test Strategy

### Unit Tests

1. **Mode switch produces no click**: Generate test signal, switch modes, verify no sample-to-sample discontinuity > threshold
2. **Crossfade completes in expected time**: Verify crossfade duration matches 50ms
3. **Rapid switching stability**: Switch 10 times per second, verify no artifacts accumulate

### Measurement Tests

1. **RMS level stability**: Verify level doesn't spike > 3dB during transition (SC-003)
2. **All mode combinations**: Test all 110 combinations (11 × 10) (SC-004)

### Manual Verification

1. **Listening test**: Switch modes during loud audio, verify no audible clicks
2. **DAW automation**: Automate mode parameter, verify smooth transitions

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Double CPU during crossfade | Low | Low | 50ms is brief, user doesn't switch rapidly |
| State leakage between modes | Medium | Medium | Each mode is independent, no shared state |
| Crossfade sounds unnatural | Low | Medium | Equal-power crossfade is proven technique |

## Conclusion

The fix is straightforward: add equal-power crossfade infrastructure to the Processor class, modeled after CharacterProcessor. The implementation requires:

1. Add crossfade state variables to processor.h
2. Pre-allocate work buffers in setupProcessing()
3. Detect mode changes and initiate crossfade in process()
4. Process both modes during crossfade, blend outputs with sin/cos gains
5. Test all 110 mode combinations for click-free transitions
