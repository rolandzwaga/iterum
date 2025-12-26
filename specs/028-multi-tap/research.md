# Research: Multi-Tap Delay Mode

**Feature**: 028-multi-tap
**Date**: 2025-12-26
**Status**: Complete

## Research Questions

### Q1: How should MultiTapDelay compose Layer 3 components?

**Decision**: Composition over inheritance

**Rationale**:
- TapManager already provides 90% of required functionality (16 taps, per-tap controls, patterns)
- FeedbackNetwork provides stable master feedback with filtering
- ModulationMatrix provides external modulation routing
- Composition allows each component to maintain its own lifecycle
- Follows established Layer 4 pattern (see PingPongDelay)

**Alternatives Considered**:
1. **Extend TapManager**: Rejected - would require modifying tested Layer 3 code
2. **Single monolithic class**: Rejected - violates layered architecture, harder to test
3. **Multiple inheritance**: Rejected - C++ diamond problem, unnecessary complexity

### Q2: How should new pattern types be organized?

**Decision**: Two separate enums at Layer 4 level

**Rationale**:
- TimingPattern: Controls WHEN taps occur (time)
- SpatialPattern: Controls WHERE taps are positioned (pan/level)
- Separation allows independent selection (e.g., GoldenRatio timing + Cascade spatial)
- TapManager's existing TapPattern enum handles basic patterns; Layer 4 extends with new types

**Alternatives Considered**:
1. **Extend TapPattern enum**: Rejected - would modify Layer 3 code
2. **Single combined enum**: Rejected - loses flexibility of independent timing/spatial selection
3. **String-based pattern IDs**: Rejected - performance overhead, less type safety

### Q3: How should pattern morphing be implemented?

**Decision**: Per-tap parameter interpolation with OnePoleSmoother

**Rationale**:
- Each tap's time, level, pan values stored as "from" values
- Target values calculated from new pattern
- OnePoleSmoother interpolates each parameter independently
- Morph time configurable (50ms to 2000ms per FR-026)
- Smooth, click-free transitions

**Alternatives Considered**:
1. **Crossfade between two TapManagers**: Rejected - memory overhead, complexity
2. **Instant switching with soft mute**: Rejected - audible even with short mute
3. **DSP-level morphing**: Rejected - not possible with delay line reads

### Q4: How should modulation integration work?

**Decision**: External ModulationMatrix with registered destinations

**Rationale**:
- ModulationMatrix is a shared resource (one per plugin instance)
- MultiTapDelay registers per-tap destinations: time[0-15], level[16-31], pan[32-47], cutoff[48-63]
- Modulation applied additively to base parameter values
- Owner responsibility: Caller owns ModulationMatrix, passes pointer to MultiTapDelay

**Alternatives Considered**:
1. **Owned ModulationMatrix**: Rejected - prevents shared LFOs/envelopes
2. **Direct LFO integration**: Rejected - less flexible than matrix routing
3. **No modulation**: Rejected - per-tap modulation is key differentiator (US5)

### Q5: How should TapManager's existing patterns be reused?

**Decision**: Delegate to TapManager for basic patterns, extend at Layer 4

**Rationale**:
- TapManager provides: QuarterNote, DottedEighth, Triplet, GoldenRatio, Fibonacci
- TapManager provides loadNotePattern() for any NoteValue + modifier
- Layer 4 adds: Exponential, PrimeNumbers, LinearSpread (mathematical)
- Layer 4 adds: Cascade, Alternating, Centered, WideningStereo, DecayingLevel, FlatLevel (spatial)

**Pattern Delegation Strategy**:
```cpp
void loadTimingPattern(TimingPattern pattern, size_t tapCount) {
    switch (pattern) {
        // Delegate to TapManager for existing patterns
        case TimingPattern::QuarterNote:
            tapManager_.loadPattern(TapPattern::QuarterNote, tapCount);
            break;
        // ... other TapManager patterns

        // Handle new Layer 4 patterns locally
        case TimingPattern::Exponential:
            applyExponentialPattern(tapCount);
            break;
        // ... other Layer 4 patterns
    }
}
```

### Q6: What is the performance budget breakdown?

**Decision**: Total < 1% CPU at 44.1kHz stereo

**Component Budgets**:
| Component | Budget | Notes |
|-----------|--------|-------|
| TapManager | 0.5% | 16 taps, filtering, pan |
| FeedbackNetwork | 0.3% | Feedback loop, limiting |
| MultiTapDelay overhead | 0.2% | Pattern logic, morphing |
| **Total** | **< 1%** | Layer 4 budget per SC-007 |

**Rationale**:
- TapManager already tested at < 2% for 16 active taps (SC-007 from 023-tap-manager)
- FeedbackNetwork adds filtering but operates on summed signal, not per-tap
- Layer 4 overhead is minimal (pattern selection, morph interpolation)

## Technical Findings

### Existing TapManager Capabilities

From `src/dsp/systems/tap_manager.h`:

1. **Tap Management**: 16 fixed taps, indices 0-15
2. **Per-Tap Controls**:
   - Time: ms or NoteValue (tempo-synced)
   - Level: -96dB to +6dB
   - Pan: -100 to +100 (constant-power pan law)
   - Filter: Bypass/LP/HP with cutoff and Q
   - Feedback: Per-tap contribution to master feedback
3. **Preset Patterns**: QuarterNote, DottedEighth, Triplet, GoldenRatio, Fibonacci
4. **Extended Patterns**: loadNotePattern(NoteValue, NoteModifier, tapCount)
5. **Smoothing**: 20ms parameter smoothing (click-free)

### Existing FeedbackNetwork Capabilities

From `src/dsp/systems/feedback_network.h`:

1. **Feedback Range**: 0% to 120% (self-oscillation support)
2. **Path Processing**:
   - Filter (LP/HP/BP) in feedback path
   - Saturation for warmth and limiting
   - Soft limiter to prevent runaway
3. **Stereo Support**: Cross-feedback for ping-pong effects
4. **Freeze Mode**: Infinite sustain without input

### Existing ModulationMatrix Capabilities

From `src/dsp/systems/modulation_matrix.h`:

1. **Routing**: Up to 32 routes from 16 sources to 16 destinations
2. **Modes**: Bipolar (-1 to +1) or Unipolar (0 to 1)
3. **Depth Control**: Per-route depth with 20ms smoothing
4. **Additive Application**: Modulation added to base parameter values

## Implementation Notes

### New Pattern Implementations

**Exponential Pattern**:
```cpp
// tap[n] = baseTime × 2^(n-1) = 1×, 2×, 4×, 8×...
for (size_t i = 0; i < tapCount; ++i) {
    float multiplier = std::pow(2.0f, static_cast<float>(i));
    tapTimes_[i] = baseTimeMs * multiplier;
}
```

**PrimeNumbers Pattern**:
```cpp
// tap[n] = baseTime × prime[n] where prime = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53}
static constexpr std::array<uint8_t, 16> kPrimes = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53};
for (size_t i = 0; i < tapCount; ++i) {
    tapTimes_[i] = baseTimeMs * static_cast<float>(kPrimes[i]);
}
```

**LinearSpread Pattern**:
```cpp
// tap[n] = minTime + (maxTime - minTime) × n / (tapCount - 1)
for (size_t i = 0; i < tapCount; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(tapCount - 1);
    tapTimes_[i] = minTimeMs + (maxTimeMs - minTimeMs) * t;
}
```

### Spatial Pattern Implementations

**Cascade**:
```cpp
// pan[n] = -100 + 200 × n / (tapCount - 1) (L to R)
for (size_t i = 0; i < tapCount; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(tapCount - 1);
    tapPans_[i] = -100.0f + 200.0f * t;
}
```

**Alternating**:
```cpp
// pan[n] = n % 2 == 0 ? -100 : +100
for (size_t i = 0; i < tapCount; ++i) {
    tapPans_[i] = (i % 2 == 0) ? -100.0f : 100.0f;
}
```

**WideningStereo**:
```cpp
// pan[n] = alternating L/R with increasing spread
// n=0: 0, n=1: -25/+25, n=2: -50/+50, etc.
for (size_t i = 0; i < tapCount; ++i) {
    float spread = 100.0f * static_cast<float>(i + 1) / static_cast<float>(tapCount);
    tapPans_[i] = (i % 2 == 0) ? -spread : spread;
}
```

## Conclusion

The MultiTapDelay feature is well-suited for Layer 4 composition. All required infrastructure exists in Layer 3 (TapManager, FeedbackNetwork, ModulationMatrix). The feature adds:

1. Extended timing patterns (Exponential, PrimeNumbers, LinearSpread)
2. Spatial patterns (Cascade, Alternating, Centered, WideningStereo, DecayingLevel, FlatLevel)
3. Pattern morphing with smooth transitions
4. ModulationMatrix integration for per-tap modulation

No Layer 0-3 modifications required. Low ODR risk. Performance within budget.
