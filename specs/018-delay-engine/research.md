# Research: DelayEngine

**Feature**: 018-delay-engine
**Date**: 2025-12-25
**Status**: Complete

## Overview

This document captures research findings for implementing the DelayEngine Layer 3 system component.

## Existing Components Analysis

### DelayLine (Layer 1)

**Location**: `src/dsp/primitives/delay_line.h`

**Interface**:
```cpp
void prepare(double sampleRate, float maxDelaySeconds) noexcept;
void reset() noexcept;
void write(float sample) noexcept;
float read(size_t delaySamples) const noexcept;        // Integer delay
float readLinear(float delaySamples) const noexcept;   // FR-012: Linear interpolation
float readAllpass(float delaySamples) noexcept;        // For fixed delays only
size_t maxDelaySamples() const noexcept;
double sampleRate() const noexcept;
```

**Decision**: Use `readLinear()` for FR-012 (sub-sample accuracy). Do NOT use `readAllpass()` as DelayEngine may have modulated delay times.

**Rationale**: Per constitution Principle X (DSP Constraints), allpass interpolation is only for fixed delays in feedback loops. Since DelayEngine supports smooth delay time changes (FR-004), we must use linear interpolation.

### OnePoleSmoother (Layer 1)

**Location**: `src/dsp/primitives/smoother.h`

**Interface**:
```cpp
void configure(float smoothTimeMs, float sampleRate) noexcept;
void setTarget(float target) noexcept;
float process() noexcept;
bool isComplete() const noexcept;
void snapTo(float value) noexcept;
void reset() noexcept;
```

**Decision**: Use OnePoleSmoother for both delay time (FR-004) and mix parameter transitions.

**Rationale**: Exponential smoothing provides natural response without the pitch effects of linear ramping. Default 5ms smoothing time is appropriate for parameter changes.

### BlockContext (Layer 0)

**Location**: `src/dsp/core/block_context.h`

**Key Method**:
```cpp
size_t tempoToSamples(NoteValue note, NoteModifier modifier = NoteModifier::None) const noexcept;
```

**Decision**: Use BlockContext::tempoToSamples() directly for synced mode calculations.

**Rationale**: Already handles tempo clamping (20-300 BPM), sample rate conversion, and note modifiers. No need to duplicate this logic.

### NoteValue/NoteModifier (Layer 0)

**Location**: `src/dsp/core/note_value.h`

**Enums**:
- NoteValue: Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond
- NoteModifier: None, Dotted, Triplet

**Decision**: Store NoteValue and NoteModifier in DelayEngine for synced mode configuration.

## Design Decisions

### D1: Time Mode Implementation

**Decision**: Simple enum with conditional logic in process()

**Alternatives Considered**:
1. Strategy pattern with virtual function - Rejected: overhead for simple branching
2. Template parameter - Rejected: need runtime switching

**Rationale**: TimeMode only has two values. An if/else in process() is cleaner and has no virtual call overhead.

### D2: Delay Time Smoothing Strategy

**Decision**: OnePoleSmoother with 20ms smoothing time for delay parameter changes

**Alternatives Considered**:
1. LinearRamp - Rejected: causes pitch shifting during transition (tape effect)
2. No smoothing - Rejected: causes clicks on parameter changes
3. SlewLimiter - Rejected: rate-limited but doesn't have the natural exponential decay

**Rationale**: OnePoleSmoother provides click-free transitions without pitch artifacts. 20ms is fast enough to feel responsive but long enough to prevent zipper noise.

### D3: Mix Calculation

**Decision**: Linear crossfade: `output = dry * (1 - mix) + wet * mix`

**Alternatives Considered**:
1. Equal power crossfade: `sqrt(1-mix)` and `sqrt(mix)` - Rejected: not needed for this use case
2. Separate dry/wet gains - Rejected: more complex API

**Rationale**: Linear crossfade is standard for delay plugins. Simple and CPU-efficient.

### D4: Kill-Dry Mode

**Decision**: Boolean flag that forces dry coefficient to 0

**Implementation**:
```cpp
const float dryCoeff = killDry_ ? 0.0f : (1.0f - mix);
const float wetCoeff = mix;
```

**Rationale**: Kill-dry is for parallel processing (aux send/return). When enabled, only wet signal passes regardless of mix setting.

### D5: Delay Time Storage

**Decision**: Store delay time in samples (float), not milliseconds

**Rationale**:
- DelayLine works in samples
- Avoids repeated ms-to-samples conversion per block
- Smoother operates on the sample value directly

Conversion happens once when setDelayTimeMs() or tempo changes.

### D6: Stereo Processing

**Decision**: Provide both mono and stereo process() overloads

**Implementation**:
```cpp
void process(float* buffer, size_t numSamples, const BlockContext& ctx) noexcept;
void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept;
```

**Rationale**: Stereo delays share configuration but need separate delay lines. The stereo overload processes both channels identically (no ping-pong, that's 024-stereo-field).

### D7: Edge Case Handling

| Edge Case | Decision |
|-----------|----------|
| Delay time 0ms | Output immediate signal (dry + immediate wet) |
| Delay time negative | Clamp to 0ms (FR-010) |
| Delay time > max | Clamp to maxDelayMs (FR-010) |
| NaN delay time | Reject, keep previous value (FR-011) |
| Infinity delay time | Clamp to maxDelayMs (FR-011) |
| BlockContext tempo 0 | BlockContext clamps to 20 BPM minimum |
| Mix < 0 or > 1 | Clamp to [0, 1] range |

## Dependencies Summary

| Component | Layer | Include Path |
|-----------|-------|--------------|
| DelayLine | 1 | `dsp/primitives/delay_line.h` |
| OnePoleSmoother | 1 | `dsp/primitives/smoother.h` |
| BlockContext | 0 | `dsp/core/block_context.h` |
| NoteValue | 0 | `dsp/core/note_value.h` |

## Risk Assessment

**Low Risk**: This is a composition pattern wrapping well-tested primitives. The main risks are:

1. **Delay time calculation errors in synced mode** - Mitigated by using existing BlockContext::tempoToSamples()
2. **Click artifacts during parameter changes** - Mitigated by OnePoleSmoother
3. **ODR violations** - Mitigated by thorough codebase search (no existing DelayEngine/TimeMode)

## Conclusion

All research questions resolved. No NEEDS CLARIFICATION items remain. Ready for Phase 1 (data-model.md, contracts/).
