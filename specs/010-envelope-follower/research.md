# Research: Envelope Follower

**Feature**: 010-envelope-follower
**Date**: 2025-12-23
**Status**: Complete (no unknowns required research)

## Summary

No external research was required for this feature. Envelope following is a well-understood DSP technique with established algorithms, and all necessary primitives exist in the codebase.

## Decisions Made

### D-001: Detection Algorithm Selection

**Decision**: Use asymmetric one-pole smoothing with different attack/release coefficients

**Rationale**:
- One-pole (first-order IIR) is the standard industry approach for envelope detection
- Asymmetric coefficients allow fast attack with slow release (typical dynamics behavior)
- Formula: `coeff = exp(-1 / (time_samples))` already implemented in `smoother.h`
- O(1) per sample, minimal CPU overhead

**Alternatives Considered**:
- Two-pole smoothing: More complex, not necessary for basic envelope tracking
- Moving average: Higher latency, more memory, less responsive to transients
- Hilbert transform: Overkill for amplitude tracking, expensive

### D-002: RMS Implementation

**Decision**: Square → Smooth → Square Root (running RMS)

**Rationale**:
- Standard approach for continuous RMS tracking
- Smooth the squared signal, then sqrt the result
- Attack/release applies to power domain, which is perceptually meaningful
- Matches behavior of analog VU meters and most compressor sidechains

**Alternatives Considered**:
- Block-based RMS (exists in dsp_utils.h): Not suitable for sample-by-sample tracking
- True RMS with windowed buffer: Higher latency and memory, overkill for envelope purposes

### D-003: Peak Mode Attack Time

**Decision**: Support both instant attack (0ms) and configurable attack

**Rationale**:
- Spec FR-003 requires Peak mode to "capture single-sample impulses"
- When attack time is minimal (< 1 sample period), use instant capture
- When attack time is > 0, use standard asymmetric smoothing
- Provides flexibility for both brick-wall limiting and softer peak detection

**Alternatives Considered**:
- Always instant attack: Less flexible, though common in limiters
- Lookahead peak detection: Adds latency, out of scope for this component

### D-004: OnePoleSmoother Composition vs Custom Implementation

**Decision**: Implement custom asymmetric smoothing inline rather than using OnePoleSmoother directly

**Rationale**:
- OnePoleSmoother uses symmetric coefficients (same attack/release)
- Envelope followers require asymmetric behavior (different attack vs release)
- Implementing inline avoids function call overhead in tight loop
- Reuse coefficient calculation formula from smoother.h

**Implementation**:
```cpp
// Asymmetric one-pole smoothing
const float input = std::abs(sample);  // or sample² for RMS
if (input > envelope_) {
    envelope_ = input + attackCoeff_ * (envelope_ - input);
} else {
    envelope_ = input + releaseCoeff_ * (envelope_ - input);
}
```

### D-005: Sidechain Filter Integration

**Decision**: Use Biquad as optional pre-filter before envelope detection

**Rationale**:
- Biquad with FilterType::Highpass already exists and is tested
- Common sidechain cutoffs (80-200Hz) are well within Biquad's range
- Filter can be bypassed when not needed (FR-010)
- Signal flow: input → [optional HP filter] → detection → output

**Alternatives Considered**:
- One-pole highpass: Simpler but 6dB/oct slope may be insufficient
- Higher-order filter: Biquad's 12dB/oct is sufficient for sidechain use

## Existing Components Verified

| Component | Verified Working | Notes |
|-----------|------------------|-------|
| OnePoleSmoother | Yes | Used in SaturationProcessor, MultimodeFilter |
| Biquad | Yes | FilterType::Highpass available |
| detail::flushDenormal | Yes | Available in db_utils.h |
| detail::isNaN | Yes | Available in db_utils.h |
| calculateOnePolCoefficient | Yes | Formula matches standard envelope follower math |

## Research Conclusion

**No external research required.** All algorithms are well-known DSP techniques already implemented in similar forms within the codebase. The envelope follower can be built by composing existing Layer 0/1 components with custom asymmetric smoothing logic.
