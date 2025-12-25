# Research: TapManager

**Feature**: 023-tap-manager
**Date**: 2025-12-25

## Summary

No major unknowns required resolution. All technical decisions are based on established patterns in the codebase and industry standards for multi-tap delay implementations.

---

## Decision Log

### 1. Tap Architecture

**Decision**: Use shared DelayLine with multiple read positions

**Rationale**:
- Memory efficient: 1 DelayLine vs 16 separate buffers
- All taps read from same input signal at different delay positions
- Matches industry standard (hardware and software delays)

**Alternatives Considered**:
- 16 independent DelayLines: More memory, no advantage for this use case
- Single DelayLine with tap structs: Chosen approach

---

### 2. Filter Implementation

**Decision**: Use Biquad directly (not MultimodeFilter)

**Rationale**:
- TapManager only needs LP/HP modes per tap
- Biquad is lighter weight than MultimodeFilter
- 16 Biquads is still performant (< 0.5% CPU each)

**Alternatives Considered**:
- MultimodeFilter: Overkill, includes unused modes (peak, shelf, notch)
- No filtering: Would limit creative options

---

### 3. Smoother Count Per Tap

**Decision**: 4 smoothers per tap (time, level, pan, filter cutoff)

**Rationale**:
- All user-controllable parameters need smoothing per FR-006, FR-011, FR-014, FR-018
- Filter resonance changes less frequently, can share cutoff smoother timing
- Total: 64 smoothers for 16 taps (acceptable overhead)

**Alternatives Considered**:
- Fewer smoothers: Would cause zipper noise on some parameters
- More smoothers: Filter Q rarely automated, not needed

---

### 4. Preset Pattern Implementation

**Decision**: Calculate patterns at load time, not per-sample

**Rationale**:
- Patterns are tempo-dependent, not sample-dependent
- Only recalculate when tempo or pattern changes
- Significantly reduces per-sample overhead

**Alternatives Considered**:
- Per-sample calculation: Wasteful for static patterns
- Cached + invalidated: Chosen approach

---

### 5. Feedback Routing

**Decision**: Each tap has independent feedback amount routed to master input

**Rationale**:
- Matches user expectation from spec (tap-to-master feedback)
- Simple implementation: sum enabled tap outputs × feedback amount
- Soft limiter on total feedback prevents runaway

**Alternatives Considered**:
- Tap-to-tap matrix: Too complex for initial implementation, could add in future
- No per-tap feedback: Limits creative options

---

## Existing Components Analysis

### Confirmed Reusable

| Component | Version | Compatibility |
|-----------|---------|---------------|
| DelayLine | Current | Full - will use readLinear() |
| Biquad | Current | Full - LP/HP modes |
| OnePoleSmoother | Current | Full - 20ms default works |
| kGoldenRatio | Current | Full - 1.618... |
| NoteValue | Current | Full - tempo sync |
| BlockContext | Current | Full - BPM access |
| dbToGain | Current | Full - level conversion |

### No Conflicts Found

All planned new types (Tap, TapManager, TapPattern) are unique to this feature.

---

## Performance Considerations

### Target: < 2% CPU for 16 taps

**Per-tap cost estimate**:
- DelayLine read (linear interp): ~0.02%
- Biquad process: ~0.03%
- 4 smoother updates: ~0.01%
- Pan + level: ~0.01%
- **Total per tap**: ~0.07%
- **16 taps**: ~1.1% (well under 2% budget)

### Memory estimate

- Shared DelayLine: maxDelayMs × sampleRate × sizeof(float) = ~1.8MB for 10s at 192kHz
- Per-tap data: ~200 bytes × 16 = ~3.2KB
- **Total**: ~1.8MB (dominated by delay buffer)
