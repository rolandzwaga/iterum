# Technical Research: Layer 0 Utilities

**Feature**: 017-layer0-utilities
**Date**: 2025-12-24
**Status**: Complete

## Research Questions

### RQ1: FastMath Algorithm Selection

**Question**: Which algorithms provide the best balance of speed and accuracy for sin, cos, tanh, and exp approximations?

**Research Findings**:

1. **fastSin/fastCos**: Polynomial approximation using Chebyshev or minimax polynomials
   - 5th-order polynomial achieves <0.1% error for [-pi, pi] range
   - Range reduction using modulo 2π (via math_constants.h kTwoPi)
   - fastCos can be derived as fastSin(x + π/2) or separate polynomial

2. **fastTanh**: Rational polynomial approximation (Padé approximant)
   - Padé (3,3) approximation: `x * (27 + x²) / (27 + 9*x²)` achieves ~0.3% max error for |x| < 3
   - For |x| >= 3, tanh saturates to ±1, so can use sign(x) with small correction
   - Alternative: piecewise polynomial with special handling at saturation

3. **fastExp**: Schraudolph's bit manipulation or polynomial approximation
   - Schraudolph method uses IEEE 754 representation for ~2% accuracy
   - For 0.5% accuracy, use Taylor series with range reduction (as in db_utils.h)
   - Existing `detail::constexprExp()` in db_utils.h provides reference implementation

**Decision**: Use polynomial approximations optimized for the specific error bounds in spec:
- Sin/Cos: 5th-order minimax polynomial (0.1% max error)
- Tanh: Padé approximant with saturation handling (0.5% for |x|<3, 1% otherwise)
- Exp: Range-reduced Taylor series (0.5% max error for [-10, 10])

### RQ2: Constexpr Feasibility

**Question**: Can all functions be constexpr, or do some require runtime-only execution?

**Research Findings**:

1. **constexpr math functions**: C++20 does not make std::sin, std::cos, std::tanh, std::exp constexpr
   - MSVC specifically does not support constexpr transcendentals
   - db_utils.h already solved this with custom Taylor series implementations

2. **Polynomial evaluation**: Pure arithmetic is constexpr-compatible
   - Horner's method for polynomial evaluation: `((c4*x + c3)*x + c2)*x + c1)*x + c0`
   - No std:: function calls needed for polynomial approximations

3. **Range reduction**: Requires care for constexpr
   - Modulo operations with float are constexpr
   - std::floor/std::fmod are NOT constexpr in MSVC

**Decision**: All FastMath functions will be constexpr using:
- Custom range reduction without std::floor (use integer truncation)
- Polynomial approximations with Horner's method
- Pattern from db_utils.h for Taylor series

### RQ3: Interpolation Quality vs. Performance

**Question**: What are the trade-offs between interpolation methods for audio?

**Research Findings**:

1. **Linear interpolation**:
   - Formula: `y0 + t * (y1 - y0)` - 1 multiply, 2 adds
   - Quality: First-order accuracy, audible in pitch shifting but fine for many uses
   - Use case: LFO-modulated delay (chorus, flanger)

2. **Cubic Hermite interpolation**:
   - Formula uses 4 points (y[-1], y[0], y[1], y[2]) and position t
   - Quality: Third-order accuracy, smooth derivatives
   - Use case: Pitch shifting, high-quality resampling
   - Standard formula: Catmull-Rom spline (tension = 0.5)

3. **Lagrange interpolation (4-point)**:
   - Formula: Polynomial through 4 points
   - Quality: Third-order accuracy, no smoothness guarantee
   - Use case: Oversampling, filter design
   - More computationally expensive than Hermite

**Decision**: Implement all three methods as specified:
- `linearInterpolate(y0, y1, t)` - 2 input samples
- `cubicHermiteInterpolate(ym1, y0, y1, y2, t)` - 4 input samples, Catmull-Rom
- `lagrangeInterpolate(ym1, y0, y1, y2, t)` - 4 input samples

### RQ4: BlockContext Design

**Question**: What tempo-to-samples conversion approach should BlockContext use?

**Research Findings**:

1. **Existing pattern in lfo.h** (lines 418-450):
   ```cpp
   float beatsPerSecond = bpm_ / 60.0f;
   float frequency = beatsPerSecond / beatsPerNote;
   ```
   This calculates frequency, not sample count.

2. **For delay applications**, we need sample count:
   ```cpp
   double beatsToSeconds = 60.0 / bpm;
   double noteSeconds = beatsToSeconds * beatsPerNote;  // e.g., quarter = 1 beat
   size_t samples = static_cast<size_t>(noteSeconds * sampleRate);
   ```

3. **Note value definitions** (from lfo.h lines 55-69):
   - Whole = 4 beats, Half = 2 beats, Quarter = 1 beat, etc.
   - Dotted = 1.5x, Triplet = 2/3x

**Decision**: BlockContext::tempoToSamples will:
- Accept NoteValue enum and optional NoteModifier
- Return size_t (sample count)
- Use double precision for intermediate calculations to avoid rounding errors
- Handle edge cases: tempo 0 (clamp to minimum), sample rate 0 (return 0)

### RQ5: NoteValue Enum Location

**Question**: Should we reuse lfo.h enums or define new ones in Layer 0?

**Research Findings**:

1. **Current location**: lfo.h (Layer 1) defines NoteValue and NoteModifier
2. **Layer violation**: BlockContext (Layer 0) cannot include lfo.h (Layer 1)
3. **ODR risk**: Defining duplicate enums in different files violates ODR

**Decision**:
1. Create `src/dsp/core/note_value.h` with canonical enum definitions
2. Update `src/dsp/primitives/lfo.h` to include and use Layer 0 definitions
3. BlockContext includes `note_value.h` for tempo sync functionality

This migration requires:
- Moving enum definitions from lfo.h to note_value.h
- Adding `#include "dsp/core/note_value.h"` to lfo.h
- Removing original enum definitions from lfo.h

## Technical Decisions Summary

| Decision | Choice | Rationale |
|----------|--------|-----------|
| fastSin/fastCos algorithm | 5th-order minimax polynomial | Meets 0.1% accuracy, fully constexpr |
| fastTanh algorithm | Padé (3,3) approximant | Meets 0.5% accuracy, handles saturation gracefully |
| fastExp algorithm | Range-reduced Taylor series | Matches db_utils.h pattern, 0.5% accuracy |
| Interpolation methods | Linear, Catmull-Rom Hermite, Lagrange | Standard DSP choices for different quality needs |
| BlockContext.tempoToSamples | Double precision calculation | Prevents rounding errors in sample counts |
| NoteValue enum location | New note_value.h (Layer 0) | Proper layer compliance, single definition |
| Range reduction (sin/cos) | Integer truncation | Avoids non-constexpr std::floor |

## Risk Mitigations

| Risk | Mitigation |
|------|------------|
| Platform-specific float behavior | Use -fno-fast-math for test files (per CLAUDE.md) |
| ODR violation with NoteValue | Create single Layer 0 definition, update lfo.h to use it |
| Accuracy drift at extreme values | Add explicit edge case tests for boundaries |
| Performance regression | Include benchmark tests comparing to std:: equivalents |
