# Data Model: FeedbackNetwork

**Feature**: 019-feedback-network
**Date**: 2025-12-25
**Layer**: 3 (System Component)

## Class Structure

### FeedbackNetwork

**Purpose**: Layer 3 system component managing feedback loops for delay effects with filtering, saturation, and cross-feedback routing.

**Location**: `src/dsp/systems/feedback_network.h`

```
FeedbackNetwork
├── Lifecycle
│   ├── prepare(sampleRate, maxBlockSize, maxDelayMs)
│   ├── reset()
│   └── isPrepared()
│
├── Processing
│   ├── process(buffer, numSamples, ctx)           // Mono
│   └── process(left, right, numSamples, ctx)      // Stereo
│
├── Feedback Parameters
│   ├── setFeedbackAmount(ratio)                   // 0.0 - 1.2
│   ├── getFeedbackAmount()
│   └── feedbackSmoother_                          // 20ms smoothing
│
├── Filter Parameters (Feedback Path)
│   ├── setFilterEnabled(bool)
│   ├── setFilterType(FilterType)
│   ├── setFilterCutoff(hz)
│   ├── setFilterResonance(q)
│   └── filter_ [L, R]                             // MultimodeFilter x2
│
├── Saturation Parameters (Feedback Path)
│   ├── setSaturationEnabled(bool)
│   ├── setSaturationType(SaturationType)
│   ├── setSaturationDrive(dB)
│   └── saturator_ [L, R]                          // SaturationProcessor x2
│
├── Freeze Mode
│   ├── setFreeze(bool)
│   ├── isFrozen()
│   ├── preFreezeAmount_                           // Stored before freeze
│   └── inputMuteSmoother_                         // Smooth mute transition
│
├── Cross-Feedback (Stereo)
│   ├── setCrossFeedbackAmount(ratio)              // 0.0 - 1.0
│   ├── getCrossFeedbackAmount()
│   └── crossFeedbackSmoother_                     // 20ms smoothing
│
└── Internal Components
    ├── delayEngine_                               // DelayEngine (wrapped)
    ├── feedbackBuffer_ [L, R]                     // Pre-allocated scratch
    └── prepared_                                  // Lifecycle flag
```

## Parameter Definitions

### Feedback Amount

| Property | Value |
|----------|-------|
| Name | feedbackAmount |
| Type | float |
| Range | [0.0, 1.2] |
| Default | 0.5 |
| Unit | Ratio (0.5 = 50% = -6dB per repeat) |
| Smoothing | 20ms (OnePoleSmoother) |

**Behavior**:
- 0.0: No feedback (single repeat)
- 0.5: Each repeat is half amplitude (-6dB)
- 1.0: Infinite sustain (no decay)
- 1.0-1.2: Self-oscillation (builds up, saturator limits)

### Filter Cutoff

| Property | Value |
|----------|-------|
| Name | filterCutoff |
| Type | float |
| Range | [20.0, Nyquist/2] Hz |
| Default | 8000.0 |
| Unit | Hz |
| Smoothing | 5ms (via MultimodeFilter) |

### Filter Resonance

| Property | Value |
|----------|-------|
| Name | filterResonance |
| Type | float |
| Range | [0.1, 10.0] |
| Default | 0.707 (Butterworth) |
| Unit | Q factor |

### Saturation Drive

| Property | Value |
|----------|-------|
| Name | saturationDrive |
| Type | float |
| Range | [0.0, 24.0] dB |
| Default | 0.0 |
| Unit | dB |
| Smoothing | 5ms (via SaturationProcessor) |

### Cross-Feedback Amount

| Property | Value |
|----------|-------|
| Name | crossFeedbackAmount |
| Type | float |
| Range | [0.0, 1.0] |
| Default | 0.0 |
| Unit | Ratio |
| Smoothing | 20ms (OnePoleSmoother) |

**Behavior**:
- 0.0: Normal stereo (L→L, R→R)
- 0.5: Mono blend (L→L+R, R→L+R)
- 1.0: Full swap / ping-pong (L→R, R→L)

## State Machine: Freeze Mode

```
┌─────────────┐                  ┌─────────────┐
│   NORMAL    │   setFreeze(T)   │   FROZEN    │
│             │ ───────────────► │             │
│ feedback=X  │                  │ feedback=1.0│
│ input=ON    │ ◄─────────────── │ input=MUTE  │
└─────────────┘   setFreeze(F)   └─────────────┘
```

**Transitions**:
- NORMAL → FROZEN: Store current feedback, set feedback=1.0, mute input
- FROZEN → NORMAL: Restore stored feedback, unmute input
- Both transitions are smoothed (20ms) to prevent clicks

## Memory Layout

### Pre-allocated in prepare()

| Buffer | Size | Purpose |
|--------|------|---------|
| feedbackBufferL_ | maxBlockSize | Scratch for L channel feedback |
| feedbackBufferR_ | maxBlockSize | Scratch for R channel feedback |

**Note**: DelayEngine, MultimodeFilter, and SaturationProcessor manage their own internal buffers.

### Estimated Memory Usage

For 10 seconds max delay at 192kHz stereo:
- DelayEngine internal: ~15 MB (2 channels × 1.92M samples × 4 bytes)
- MultimodeFilter: ~64 KB (oversample buffers)
- SaturationProcessor: ~64 KB (oversample buffers)
- FeedbackNetwork scratch: ~8 KB (2 × maxBlockSize × 4 bytes)
- **Total**: ~15.2 MB

## Validation Rules

### Parameter Validation

```cpp
// Feedback amount
if (amount < 0.0f) amount = 0.0f;
if (amount > 1.2f) amount = 1.2f;
if (std::isnan(amount)) return;  // Reject NaN, keep previous

// Cross-feedback amount
if (amount < 0.0f) amount = 0.0f;
if (amount > 1.0f) amount = 1.0f;
if (std::isnan(amount)) return;  // Reject NaN, keep previous

// Filter cutoff (delegated to MultimodeFilter)
// Saturation drive (delegated to SaturationProcessor)
```

## Relationships

```
┌─────────────────────────────────────────────────────────┐
│                    FeedbackNetwork                       │
│                      (Layer 3)                          │
│                                                         │
│  ┌──────────────┐  ┌────────────────┐  ┌─────────────┐ │
│  │ DelayEngine  │  │ MultimodeFilter│  │ Saturation  │ │
│  │   (Layer 3)  │  │    (Layer 2)   │  │  Processor  │ │
│  │              │  │    × 2 (L/R)   │  │  (Layer 2)  │ │
│  │              │  │                │  │   × 2 (L/R) │ │
│  └──────────────┘  └────────────────┘  └─────────────┘ │
│                                                         │
│  ┌───────────────────────────────────────────────────┐ │
│  │              OnePoleSmoother (Layer 1) × 3        │ │
│  │      (feedback, crossFeedback, inputMute)         │ │
│  └───────────────────────────────────────────────────┘ │
│                                                         │
│  ┌───────────────────────────────────────────────────┐ │
│  │         stereoCrossBlend (Layer 0 utility)        │ │
│  │              src/dsp/core/stereo_utils.h           │ │
│  └───────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

## Layer 0 Utility: stereoCrossBlend

**Location**: `src/dsp/core/stereo_utils.h`

**Purpose**: Reusable stereo cross-routing for FeedbackNetwork (019), StereoField (022), and TapManager (023).

```cpp
namespace Iterum::DSP {

/// @brief Apply stereo cross-blend routing
/// @param inL Left input sample
/// @param inR Right input sample
/// @param crossAmount Cross-blend amount (0.0 = no cross, 1.0 = full swap)
/// @param outL Output: blended left sample
/// @param outR Output: blended right sample
constexpr void stereoCrossBlend(
    float inL, float inR,
    float crossAmount,
    float& outL, float& outR
) noexcept {
    const float keep = 1.0f - crossAmount;
    outL = inL * keep + inR * crossAmount;
    outR = inR * keep + inL * crossAmount;
}

} // namespace Iterum::DSP
```

**Properties**:
- constexpr: Usable at compile-time
- noexcept: Real-time safe
- ~10 LOC: Trivial but prevents duplication
- Energy-preserving at crossAmount = 0.5
