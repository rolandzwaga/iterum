# Research: FeedbackNetwork

**Feature**: 019-feedback-network
**Date**: 2025-12-25
**Status**: Complete

## Research Questions

### 1. Feedback Path Signal Flow

**Question**: What is the optimal order for filter and saturator in the feedback path?

**Decision**: Filter → Saturator

**Rationale**:
- **Tape emulation**: Real tape machines filter before the tape head saturation. High frequencies are pre-emphasized, hit the tape, then de-emphasized on playback. This naturally gives HF decay.
- **Harmonic preservation**: Filtering after saturation would remove harmonics just added. Filter-first shapes what goes into saturation.
- **Frequency buildup prevention**: High frequencies can accumulate dangerously in feedback loops. Filtering first prevents harsh buildup before it hits the saturator.
- **Classic analog delay design**: Units like the Roland Space Echo filter before the playback head (inherent saturation).

**Alternatives Considered**:
- Saturator → Filter: Would add harmonics then remove them, less efficient
- Parallel processing: More complex, not typical for feedback path

**Implementation Note**: Signal flow will be:
```
Delay Output → MultimodeFilter → SaturationProcessor → Feedback Input
```

### 2. Freeze Mode Implementation

**Question**: How should freeze mode be implemented to capture and sustain audio indefinitely?

**Decision**: Set feedback to 100% and mute input while freeze is active

**Rationale**:
- **Simple state change**: Rather than copying buffer contents, simply change two parameters:
  1. Override feedback to 1.0 (100%)
  2. Set input gain to 0.0
- **Smooth transition**: Use parameter smoothers for both changes to avoid clicks
- **Memory of previous feedback**: Store pre-freeze feedback value to restore on unfreeze
- **Cross-feedback still applies**: In stereo mode, cross-feedback continues within the frozen loop

**Alternatives Considered**:
- Buffer snapshot: Copy delay buffer to freeze buffer - adds complexity, memory use
- Hold mode with decay: Would eventually decay - not true freeze

**Implementation Note**:
```cpp
void setFreeze(bool freeze) noexcept {
    frozen_ = freeze;
    if (freeze) {
        preFreezeAmount_ = feedbackAmount_;  // Remember
        feedbackSmoother_.setTarget(1.0f);   // 100% feedback
        inputMuteSmoother_.setTarget(0.0f);  // Mute input
    } else {
        feedbackSmoother_.setTarget(preFreezeAmount_);  // Restore
        inputMuteSmoother_.setTarget(1.0f);              // Unmute
    }
}
```

### 3. Cross-Feedback Matrix

**Question**: Does the stereoCrossBlend formula handle all edge cases correctly?

**Decision**: Yes, the formula is mathematically correct for all values

**Formula**:
```cpp
outL = inL * (1 - crossAmount) + inR * crossAmount
outR = inR * (1 - crossAmount) + inL * crossAmount
```

**Verification**:

| crossAmount | outL | outR | Behavior |
|-------------|------|------|----------|
| 0.0 | inL | inR | No cross (normal stereo) |
| 0.5 | (inL+inR)/2 | (inL+inR)/2 | Mono sum |
| 1.0 | inR | inL | Full swap (ping-pong) |

**Edge Cases Verified**:
- **crossAmount < 0**: Should be clamped in setter (invalid)
- **crossAmount > 1**: Should be clamped in setter (invalid)
- **NaN input**: Passes through as NaN (caller's responsibility)
- **Infinity input**: Passes through (caller's responsibility)
- **Energy preservation**: At crossAmount = 0.5, total energy is preserved: `(L+R)/2 + (L+R)/2 = L+R`

**Implementation Note**: Add clamping in the setter, not in stereoCrossBlend itself (keep utility pure and fast).

## Existing Component Analysis

### DelayEngine (018-delay-engine)

**Location**: `src/dsp/systems/delay_engine.h`

**Key APIs for FeedbackNetwork**:
- `prepare(sampleRate, maxBlockSize, maxDelayMs)` - Initialize
- `process(buffer, numSamples, ctx)` - Mono processing
- `process(left, right, numSamples, ctx)` - Stereo processing
- `setDelayTimeMs(ms)` - Set delay time
- `setMix(wetRatio)` - Dry/wet mix
- `setKillDry(bool)` - Useful for feedback path (wet-only)
- `reset()` - Clear state

**Integration Notes**:
- Will need to set `setKillDry(true)` for feedback-only operation
- The delay engine handles its own smoothing, so no double-smoothing needed
- Stereo processing uses two internal DelayLines (left/right)

### MultimodeFilter (008-multimode-filter)

**Location**: `src/dsp/processors/multimode_filter.h`

**Key APIs for FeedbackNetwork**:
- `prepare(sampleRate, maxBlockSize)` - Initialize
- `process(buffer, numSamples)` - Block processing
- `setType(FilterType)` - LP/HP/BP/etc.
- `setCutoff(hz)` - Filter cutoff
- `setResonance(q)` - Q factor
- `reset()` - Clear state

**Integration Notes**:
- Has built-in coefficient smoothing (5ms default)
- Supports bypass by setting cutoff to Nyquist (could add explicit bypass flag)
- For feedback path, typically use LP at 2-8kHz for tape-style warmth

### SaturationProcessor (009-saturation-processor)

**Location**: `src/dsp/processors/saturation_processor.h`

**Key APIs for FeedbackNetwork**:
- `prepare(sampleRate, maxBlockSize)` - Initialize
- `process(buffer, numSamples)` - Block processing
- `setType(SaturationType)` - Tape/Tube/etc.
- `setInputGain(dB)` - Drive amount
- `setMix(ratio)` - Dry/wet (0 = bypass)
- `reset()` - Clear state

**Integration Notes**:
- Has 2x oversampling and DC blocking built-in
- Set mix = 0.0 for bypass (efficient, skips processing)
- Adds latency from oversampling filters (~32 samples at 2x)
- For feedback path, Tape type with moderate drive is typical

## Architecture Decisions

### Component Composition

FeedbackNetwork (Layer 3) will contain:
- 1x DelayEngine (Layer 3) - Wrapped, not inherited
- 2x MultimodeFilter (Layer 2) - L/R channels
- 2x SaturationProcessor (Layer 2) - L/R channels
- 3x OnePoleSmoother (Layer 1) - Feedback, cross-feedback, input mute

### Stereo Processing Strategy

For stereo, maintain independent L/R processing chains with cross-feedback:

```
Input L ────┬───► DelayL ──► FilterL ──► SaturatorL ──┬──► FeedbackL
            │                                          │      ↑
            │     ┌────────── Cross Blend ─────────────┘      │
            │     │                                           │
Input R ────┴───► DelayR ──► FilterR ──► SaturatorR ──┴──► FeedbackR
```

The cross-blend happens AFTER filter/saturation, BEFORE feeding back into delay.

### Memory Allocation Strategy

All memory allocated in `prepare()`:
- DelayEngine manages its own buffers
- MultimodeFilter manages its own oversample buffers
- SaturationProcessor manages its own oversample buffers
- FeedbackNetwork only needs small scratch buffers for cross-blend

## Recommendations

1. **Keep stereoCrossBlend as Layer 0 utility** - Simple, reusable, constexpr
2. **Document in ARCHITECTURE.md** - Other specs (022, 023) will use it
3. **Bypass via parameter, not conditional logic** - Set filter cutoff high or saturator mix = 0
4. **Test cross-feedback with impulse response** - Verify L/R alternation pattern
5. **Test freeze mode duration** - Verify no degradation over 60+ seconds
