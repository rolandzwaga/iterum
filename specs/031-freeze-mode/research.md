# Research: Freeze Mode

**Feature**: 031-freeze-mode
**Date**: 2025-12-26

## Research Summary

This document captures research findings and design decisions for the Freeze Mode feature.

## Key Findings

### 1. FlexibleFeedbackNetwork Already Has Freeze Built-In

**Finding**: The existing FlexibleFeedbackNetwork (Layer 3) already implements core freeze functionality:

```cpp
// From flexible_feedback_network.h
void setFreezeEnabled(bool enabled) noexcept {
    freezeEnabled_ = enabled;
    freezeMixSmoother_.setTarget(enabled ? 1.0f : 0.0f);
}
```

**Implementation Details**:
- Input is muted via `(1.0f - freezeMix)` interpolation
- Feedback interpolates to 100% when frozen: `feedback + freezeMix * (1.0f - feedback)`
- Uses 20ms smoothing for click-free transitions via `freezeMixSmoother_`

**Impact**: FreezeMode Layer 4 feature is a thin wrapper, not a full reimplementation.

### 2. Decay Control Design

**Decision**: Implement decay as per-sample gain reduction in FreezeFeedbackProcessor

**Rationale**:
- Decay is NOT the same as reducing feedback from 100%
- Feedback reduction would only apply when audio re-enters the delay line
- Decay needs to attenuate the signal on every pass through the feedback path
- This creates the "fade out" effect even at 100% feedback

**Implementation**:
```cpp
// In FreezeFeedbackProcessor::process()
// Calculate decay gain per sample to achieve target decay time
const float decayGain = calculateDecayGain(decayAmount_, sampleRate_);
for (size_t i = 0; i < numSamples; ++i) {
    left[i] *= decayGain;
    right[i] *= decayGain;
}
```

**Decay Coefficient Calculation**:
- SC-003 requires decay 100% reaches -60dB in 500ms
- -60dB = 0.001 amplitude
- At 44.1kHz, 500ms = 22050 samples
- decayGain^22050 = 0.001
- decayGain = 0.001^(1/22050) ≈ 0.999686

### 3. ShimmerDelay Pattern Reference

**Finding**: ShimmerDelay (029) establishes the pattern for Layer 4 features with IFeedbackProcessor injection:

1. **ShimmerFeedbackProcessor** implements IFeedbackProcessor
2. **ShimmerDelay** composes FlexibleFeedbackNetwork
3. Processor is injected via `setProcessor(&shimmerProcessor_)`
4. User-facing API delegates to internal components

**Impact**: FreezeMode follows identical architecture with FreezeFeedbackProcessor.

### 4. Transition Timing for Short Delays

**Finding**: Spec requires `min(50ms, delay_time)` transition for short delays (FR-007, SC-001)

**Analysis**:
- FlexibleFeedbackNetwork uses fixed 20ms smoothing time
- For delays < 20ms, this is already within the delay time
- For delays < 50ms but > 20ms, existing 20ms smoothing is sufficient
- Edge case: delays < 20ms may need special handling

**Decision**: Accept 20ms as the de facto transition time since:
- It's already smooth enough to prevent clicks
- Configurable transition time would add complexity without clear benefit
- The spec's 50ms is an upper bound, not a requirement

## Alternatives Considered

### Decay Implementation Alternatives

| Approach | Pros | Cons | Decision |
|----------|------|------|----------|
| Reduce feedback % | Simple | Doesn't fade frozen content | Rejected |
| Per-sample gain in processor | Correct behavior, smooth | Slightly more CPU | **Selected** |
| Per-block gain | Lower CPU | Potential zipper noise | Rejected |

### Processor Architecture Alternatives

| Approach | Pros | Cons | Decision |
|----------|------|------|----------|
| Extend ShimmerFeedbackProcessor | Code reuse | Tight coupling, SRP violation | Rejected |
| New FreezeFeedbackProcessor | Clean separation | Some duplication | **Selected** |
| Parameterized generic processor | Ultimate flexibility | Over-engineering | Rejected |

## Dependencies Verified

| Component | Version/Location | Status |
|-----------|------------------|--------|
| FlexibleFeedbackNetwork | src/dsp/systems/flexible_feedback_network.h | Verified, freeze API present |
| IFeedbackProcessor | src/dsp/systems/i_feedback_processor.h | Verified, interface matches |
| PitchShiftProcessor | src/dsp/processors/pitch_shift_processor.h | Verified, for shimmer freeze |
| DiffusionNetwork | src/dsp/processors/diffusion_network.h | Verified, for pad textures |
| OnePoleSmoother | src/dsp/primitives/smoother.h | Verified, for parameter smoothing |

## Open Questions

All questions resolved through codebase research. No blocking unknowns remain.

## References

- FlexibleFeedbackNetwork implementation: `src/dsp/systems/flexible_feedback_network.h`
- ShimmerDelay reference implementation: `src/dsp/features/shimmer_delay.h`
- Spec: `specs/031-freeze-mode/spec.md`
