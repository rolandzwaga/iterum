# Research: Stereo Field

**Feature**: 022-stereo-field
**Date**: 2025-12-25

## Research Questions

### Q1: What panning law should be used for constant-power panning?

**Decision**: Sine/Cosine (Quarter-Sine) Pan Law

**Rationale**:
- Mathematical definition: `gainL = cos(θ)`, `gainR = sin(θ)` where θ = pan * π/2
- At center (θ = π/4): gainL = gainR = 1/√2 ≈ 0.707, power = 0.5 + 0.5 = 1.0
- At extremes (θ = 0 or π/2): one channel at 1.0, other at 0.0, power = 1.0
- This maintains constant power (sum of squared gains = 1) across the pan range

**Alternatives Considered**:
- Linear pan: Simple but causes 3dB dip at center
- Square root pan: Similar to sine/cos but less smooth near extremes
- -3dB center: Common but not truly constant-power

### Q2: How should L/R Offset be implemented?

**Decision**: Separate small DelayLine for offset only

**Rationale**:
- L/R Offset is independent of main delay time
- Maximum ±50ms at 192kHz = 9,600 samples (small buffer)
- Use DelayLine primitive with linear interpolation
- Only one offset delay is active at a time:
  - Positive offset: delay R channel
  - Negative offset: delay L channel

**Alternatives Considered**:
- Modifying main delay times: Would complicate delay engine composition
- Phase shifter: Different audible effect, not what spec requires

### Q3: How should PingPong mode implement cross-feedback?

**Decision**: Internal feedback loop with alternating L/R routing

**Rationale**:
- Input summed to mono → first delay → output L
- Feedback from output → second delay → output R
- Feedback from R → back to first delay → alternates
- Feedback amount controls overall decay
- This matches traditional ping-pong delay behavior

**Implementation**:
```
                    ┌─────────────┐
Input (L+R)/2 ─────►│   Delay L   ├───► Output L
                    └──────▲──────┘
                           │ feedback
                    ┌──────┴──────┐
Feedback from R ───►│   Delay R   ├───► Output R
                    └─────────────┘
```

### Q4: Should MidSideProcessor be used directly or composed?

**Decision**: Compose MidSideProcessor directly

**Rationale**:
- MidSideProcessor already provides M/S encode/decode and width control
- Reusing it avoids code duplication (Constitution Principle XIV)
- Width parameter on StereoField delegates to MidSideProcessor.setWidth()
- MidSide mode uses MidSideProcessor for full M/S processing chain

**Integration**:
- StereoField contains one MidSideProcessor instance
- Width parameter always applies (affects all modes except Mono)
- MidSide mode uses M/S encode → delay M and S → M/S decode path

### Q5: How many DelayEngine instances are needed?

**Decision**: Two DelayEngine instances (Left and Right channels)

**Rationale**:
- Stereo mode needs independent L/R delays
- PingPong mode uses both with cross-routing
- DualMono can use just one (but two for consistency)
- MidSide mode uses them for Mid and Side delays
- Mono mode uses just one (other is idle)

**Memory**: 2 × DelayEngine at max 10 seconds × 192kHz = 3.84M samples × 2 × 4 bytes = ~30MB max

## Dependencies Confirmed

| Component | Version/Commit | Status |
|-----------|----------------|--------|
| DelayEngine | spec-018 | ✅ Available |
| MidSideProcessor | spec-014 | ✅ Available |
| OnePoleSmoother | spec-005 | ✅ Available |
| db_utils.h (isNaN, dbToGain) | Layer 0 | ✅ Available |

## Edge Cases Resolved

| Edge Case | Resolution |
|-----------|------------|
| Width > 200% | Clamp to 200% (FR-012) |
| L/R Offset > delay time | Clamp to delay time (documented in edge cases) |
| L/R Ratio extreme values | Clamp to [0.1, 10.0] (FR-016) |
| NaN input | Treat as 0.0 (FR-019, use isNaN from db_utils.h) |
| Mode transition during processing | 50ms crossfade (FR-003) |
