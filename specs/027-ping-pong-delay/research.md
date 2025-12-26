# Research: Ping-Pong Delay Mode

**Feature Branch**: `027-ping-pong-delay`
**Date**: 2025-12-26

## Research Questions Resolved

### Q1: Composition Architecture - DelayEngine vs Raw DelayLine?

**Decision**: Use 2x DelayLine directly (Layer 1), not DelayEngine (Layer 3)

**Rationale**:
- DelayEngine processes L/R with the same delay time (single delaySmoother_)
- PingPong needs independent L/R delay times with different ratios
- Using raw DelayLine gives us precise control over each channel's timing
- We'll handle tempo sync manually using BlockContext::tempoToSamples()

**Alternatives Considered**:
- 2x DelayEngine: Would work but adds unnecessary overhead (two smoothers processing identical values)
- StereoField's PingPong mode: Too basic, lacks ratio control and cross-feedback amount

### Q2: Cross-Feedback Implementation

**Decision**: Use stereoCrossBlend() from Layer 0 stereo_utils.h

**Rationale**:
- Already exists and tested in FeedbackNetwork
- Provides smooth crossfade between channels
- At crossFeedback=0: channels are independent (dual mono)
- At crossFeedback=1: full ping-pong alternation

**Implementation Pattern**:
```cpp
float crossedL, crossedR;
stereoCrossBlend(feedbackL, feedbackR, crossFeedback, crossedL, crossedR);
```

### Q3: L/R Ratio Representation

**Decision**: Create LRRatio enum with preset ratios

**Rationale**:
- Discrete ratios (1:1, 2:1, 3:2) are more musically useful than continuous
- Easier for users to set up polyrhythmic patterns
- StereoField uses continuous LRRatio (0.1 to 10.0) but that's harder to use

**Preset Ratios**:
| Enum Value | Ratio | Left Multiplier | Right Multiplier |
|------------|-------|-----------------|------------------|
| OneToOne | 1:1 | 1.0 | 1.0 |
| TwoToOne | 2:1 | 1.0 | 0.5 |
| ThreeToTwo | 3:2 | 1.0 | 0.667 |
| FourToThree | 4:3 | 1.0 | 0.75 |
| OneToTwo | 1:2 | 0.5 | 1.0 |
| TwoToThree | 2:3 | 0.667 | 1.0 |
| ThreeToFour | 3:4 | 0.75 | 1.0 |

### Q4: Width Control Implementation

**Decision**: Use M/S (Mid-Side) technique like StereoField

**Rationale**:
- Already proven in StereoField::processStereo()
- Width 0% = mono (correlation 1.0)
- Width 100% = natural stereo
- Width 200% = enhanced stereo (S boosted, correlation < 0.5)

**Implementation**:
```cpp
const float mid = (delayedL + delayedR) * 0.5f;
const float side = (delayedL - delayedR) * 0.5f * (width / 100.0f);
outL = mid + side;
outR = mid - side;
```

### Q5: Modulation Strategy

**Decision**: Use LFO from Layer 1 with independent phase offset for L/R

**Rationale**:
- LFO already supports all required waveforms (Sine, Triangle, etc.)
- 90° phase offset between L/R channels creates pleasing stereo modulation
- Apply modulation depth to delay time variation

**Implementation**:
```cpp
// In process loop:
const float modL = lfoL_.process() * modulationDepth_;  // 0 to ±maxModMs
const float modR = lfoR_.process() * modulationDepth_;  // 90° out of phase
leftDelaySamples += msToSamples(modL);
rightDelaySamples += msToSamples(modR);
```

### Q6: Limiter for Feedback > 100%

**Decision**: Reuse soft limiter pattern from DigitalDelay

**Rationale**:
- DigitalDelay uses DynamicsProcessor for this purpose
- Same limiting needed for ping-pong feedback stability
- Keeps architecture consistent across Layer 4 features

## Component Reuse Summary

| Component | Location | Reuse |
|-----------|----------|-------|
| DelayLine | Layer 1 primitives | 2 instances for L/R |
| LFO | Layer 1 primitives | 2 instances for L/R modulation |
| OnePoleSmoother | Layer 1 primitives | 8+ instances for all parameters |
| stereoCrossBlend | Layer 0 stereo_utils.h | Cross-feedback blending |
| DynamicsProcessor | Layer 2 processors | Feedback limiting |
| BlockContext | Layer 0 core | Tempo sync |
| NoteValue | Layer 0 core | Note value calculations |
| dbToGain | Layer 0 core | Level conversions |

## New Components Needed

| Component | Layer | Description |
|-----------|-------|-------------|
| PingPongDelay | 4 | Main user feature class |
| LRRatio (enum) | 4 | Preset ratio values |
