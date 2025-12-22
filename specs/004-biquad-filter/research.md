# Research: Biquad Filter Implementation

**Feature**: 004-biquad-filter
**Date**: 2025-12-22
**Purpose**: Document coefficient formulas, topology selection, and implementation best practices

---

## 1. Filter Topology Selection

### Decision: Transposed Direct Form II (TDF2)

**Rationale**: Constitution Principle X explicitly recommends TDF2 for floating-point biquads.

### Topology Comparison

| Topology | Pros | Cons | Recommendation |
|----------|------|------|----------------|
| Direct Form I | Stable with fixed-point, good for high Q | More state variables (4) | Not for floating-point |
| Direct Form II | Fewer state (2) | Numerical issues at low freq | Avoid |
| **Transposed Direct Form II** | Best for floating-point, 2 state vars | None significant | **Use this** |
| State Variable Filter | Multi-output, good modulation | Complex, needs oversampling | Future primitive |

### TDF2 Difference Equations

```
y[n] = b0*x[n] + z1[n-1]
z1[n] = b1*x[n] - a1*y[n] + z2[n-1]
z2[n] = b2*x[n] - a2*y[n]
```

Where:
- `x[n]` = input sample
- `y[n]` = output sample
- `z1, z2` = state variables
- `b0, b1, b2, a1, a2` = normalized coefficients (a0 = 1)

---

## 2. Coefficient Formulas

### Source: Robert Bristow-Johnson's Audio EQ Cookbook

This is the industry-standard reference for biquad coefficient calculation. All major DAWs and audio software use these formulas.

### Common Intermediate Variables

```
omega = 2 * pi * frequency / sampleRate
sin_omega = sin(omega)
cos_omega = cos(omega)
alpha = sin_omega / (2 * Q)
A = 10^(dBgain / 40)  // for peaking and shelving EQ
```

### Filter Type Coefficients

#### Lowpass (LPF)

```
b0 = (1 - cos_omega) / 2
b1 = 1 - cos_omega
b2 = (1 - cos_omega) / 2
a0 = 1 + alpha
a1 = -2 * cos_omega
a2 = 1 - alpha
```

#### Highpass (HPF)

```
b0 = (1 + cos_omega) / 2
b1 = -(1 + cos_omega)
b2 = (1 + cos_omega) / 2
a0 = 1 + alpha
a1 = -2 * cos_omega
a2 = 1 - alpha
```

#### Bandpass (BPF) - Constant 0 dB peak gain

```
b0 = alpha
b1 = 0
b2 = -alpha
a0 = 1 + alpha
a1 = -2 * cos_omega
a2 = 1 - alpha
```

#### Notch (Band-reject)

```
b0 = 1
b1 = -2 * cos_omega
b2 = 1
a0 = 1 + alpha
a1 = -2 * cos_omega
a2 = 1 - alpha
```

#### Allpass

```
b0 = 1 - alpha
b1 = -2 * cos_omega
b2 = 1 + alpha
a0 = 1 + alpha
a1 = -2 * cos_omega
a2 = 1 - alpha
```

#### Low Shelf

```
A = sqrt(10^(dBgain/20))
beta = sqrt(A) / Q  // or sqrt(2*A) for constant-Q

b0 = A * ((A+1) - (A-1)*cos_omega + beta*sin_omega)
b1 = 2*A * ((A-1) - (A+1)*cos_omega)
b2 = A * ((A+1) - (A-1)*cos_omega - beta*sin_omega)
a0 = (A+1) + (A-1)*cos_omega + beta*sin_omega
a1 = -2 * ((A-1) + (A+1)*cos_omega)
a2 = (A+1) + (A-1)*cos_omega - beta*sin_omega
```

#### High Shelf

```
A = sqrt(10^(dBgain/20))
beta = sqrt(A) / Q

b0 = A * ((A+1) + (A-1)*cos_omega + beta*sin_omega)
b1 = -2*A * ((A-1) + (A+1)*cos_omega)
b2 = A * ((A+1) + (A-1)*cos_omega - beta*sin_omega)
a0 = (A+1) - (A-1)*cos_omega + beta*sin_omega
a1 = 2 * ((A-1) - (A+1)*cos_omega)
a2 = (A+1) - (A-1)*cos_omega - beta*sin_omega
```

#### Peak / Parametric EQ

```
A = sqrt(10^(dBgain/20))

b0 = 1 + alpha*A
b1 = -2 * cos_omega
b2 = 1 - alpha*A
a0 = 1 + alpha/A
a1 = -2 * cos_omega
a2 = 1 - alpha/A
```

### Coefficient Normalization

All coefficients must be divided by `a0` before use:

```
b0 /= a0; b1 /= a0; b2 /= a0;
a1 /= a0; a2 /= a0;
// a0 = 1 after normalization (not stored)
```

---

## 3. Edge Case Handling

### Near-Nyquist Frequency

**Problem**: When `frequency >= sampleRate/2`, `omega >= pi`, causing instability.

**Solution**: Clamp frequency to 0.99 * Nyquist:
```cpp
float maxFreq = sampleRate * 0.495f;  // Leave margin
float clampedFreq = std::min(frequency, maxFreq);
```

### Very Low Frequency

**Problem**: When `frequency < 5 Hz`, numerical precision issues may occur.

**Solution**: Use double precision for coefficient calculation, then cast to float:
```cpp
double omega_d = 2.0 * pi_d * frequency / sampleRate;
// ... calculate with double ...
b0_ = static_cast<float>(b0_d / a0_d);
```

### Extreme Q Values

**Problem**: Q < 0.1 causes very wide bandwidth; Q > 30 causes self-oscillation.

**Solution**: Clamp Q to practical range:
```cpp
constexpr float kMinQ = 0.1f;
constexpr float kMaxQ = 30.0f;
float clampedQ = std::clamp(Q, kMinQ, kMaxQ);
```

### Denormalized Numbers

**Problem**: State variables can decay to denormals (1e-38 to 1e-45), causing 100x CPU slowdown.

**Solution**: Flush to zero when below threshold:
```cpp
constexpr float kDenormalThreshold = 1e-15f;

inline float flushDenormal(float x) noexcept {
    return (std::abs(x) < kDenormalThreshold) ? 0.0f : x;
}

// In process():
z1_ = flushDenormal(z1_);
z2_ = flushDenormal(z2_);
```

### NaN Input

**Problem**: NaN input corrupts filter state permanently.

**Solution**: Check input and reset state if NaN detected:
```cpp
if (!std::isfinite(input)) {
    reset();
    return 0.0f;
}
```

Note: Due to -ffast-math, use bit-level check (see constitution):
```cpp
inline bool isFinite(float x) noexcept {
    const auto bits = std::bit_cast<uint32_t>(x);
    return (bits & 0x7F800000u) != 0x7F800000u;
}
```

---

## 4. Coefficient Smoothing Strategy

### Decision: Per-Coefficient One-Pole Smoothing

**Rationale**: Reuse existing `OnePoleSmoother` from dsp_utils.h.

### Approach

```cpp
class SmoothedBiquad {
    Biquad filter_;

    // Target coefficients (updated immediately)
    BiquadCoefficients target_;

    // Smoothed coefficients (approach target)
    OnePoleSmoother smoothB0_, smoothB1_, smoothB2_;
    OnePoleSmoother smoothA1_, smoothA2_;

    void setSmoothingTime(float ms, float sampleRate) {
        float seconds = ms * 0.001f;
        smoothB0_.setTime(seconds, sampleRate);
        // ... same for all 5 smoothers
    }

    float process(float input) {
        // Update filter with smoothed coefficients
        filter_.setCoefficients(
            smoothB0_.process(target_.b0),
            smoothB1_.process(target_.b1),
            smoothB2_.process(target_.b2),
            smoothA1_.process(target_.a1),
            smoothA2_.process(target_.a2)
        );
        return filter_.process(input);
    }
};
```

### Smoothing Time

- Default: 10ms (good balance between responsiveness and click-free)
- Range: 1ms (fast modulation) to 50ms (very smooth)
- For LFO modulation at 5Hz: 5-10ms is appropriate

---

## 5. Constexpr Coefficient Calculation

### Problem

MSVC doesn't support constexpr `std::sin`, `std::cos`, `std::pow` even in C++20.

### Solution: Taylor Series Approximations

```cpp
// Already have constexpr exp/log in db_utils.h
// Add constexpr sin/cos for coefficient calculation

namespace detail {

constexpr float constexprSin(float x) noexcept {
    // Normalize to [-pi, pi]
    while (x > kPi) x -= kTwoPi;
    while (x < -kPi) x += kTwoPi;

    // Taylor series: sin(x) = x - x^3/3! + x^5/5! - x^7/7! + ...
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x5 = x3 * x2;
    const float x7 = x5 * x2;
    const float x9 = x7 * x2;

    return x - x3/6.0f + x5/120.0f - x7/5040.0f + x9/362880.0f;
}

constexpr float constexprCos(float x) noexcept {
    // cos(x) = sin(x + pi/2)
    return constexprSin(x + kPi * 0.5f);
}

} // namespace detail
```

### Alternative: Use std::is_constant_evaluated()

```cpp
constexpr float calcSin(float x) noexcept {
    if (std::is_constant_evaluated()) {
        return detail::constexprSin(x);
    } else {
        return std::sin(x);
    }
}
```

---

## 6. Cascading for Steeper Slopes

### Decision: BiquadCascade Template

```cpp
template<size_t N>
class BiquadCascade {
    std::array<Biquad, N> stages_;

public:
    // Process through all stages in series
    float process(float input) noexcept {
        float x = input;
        for (auto& stage : stages_) {
            x = stage.process(x);
        }
        return x;
    }

    // Set all stages to same frequency with Butterworth Q alignment
    void setButterworth(FilterType type, float freq, float sampleRate);
};
```

### Butterworth Q Values for Flat Passband

For N cascaded 2-pole stages (total 2N poles):

| Stages | Poles | Q Values |
|--------|-------|----------|
| 1 | 2 | 0.7071 |
| 2 | 4 | 0.5412, 1.3066 |
| 3 | 6 | 0.5176, 0.7071, 1.9319 |
| 4 | 8 | 0.5098, 0.6013, 0.9000, 2.5628 |

Formula: `Q[k] = 1 / (2 * cos(pi * (2*k + N - 1) / (2*N)))` for k = 0..N-1

---

## 7. Performance Considerations

### Alternatives Considered

| Approach | CPU Cost | Quality | Decision |
|----------|----------|---------|----------|
| Naive TDF2 | Baseline | Good | **Use** |
| SIMD (SSE) | 0.25x | Good | Future optimization |
| State Variable | 1.2x | Multi-output | Separate primitive |
| ZDF (Zero-Delay Feedback) | 1.5x | Excellent modulation | Future |

### Memory Layout

```cpp
struct Biquad {
    // Coefficients (20 bytes)
    float b0_, b1_, b2_;
    float a1_, a2_;

    // State (8 bytes)
    float z1_, z2_;

    // Padding for cache alignment
    // Total: 28 bytes, pad to 32 for alignment
};
```

---

## 8. References

1. **Robert Bristow-Johnson's Audio EQ Cookbook**
   - https://www.w3.org/2011/audio/audio-eq-cookbook.html
   - Definitive reference for all coefficient formulas

2. **Julius O. Smith III - Introduction to Digital Filters**
   - CCRMA Stanford
   - TDF2 numerical analysis

3. **Udo Zölzer - DAFX: Digital Audio Effects**
   - Academic reference for filter design

4. **Cytomic Technical Papers (Andrew Simper)**
   - Modern SVF and ZDF filter designs
   - Reference for future improvements

---

## 9. Summary of Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Topology | Transposed Direct Form II | Constitution requirement, best for float |
| Formulas | Audio EQ Cookbook | Industry standard |
| Smoothing | Per-coefficient OnePoleSmoother | Reuse existing code |
| Denormals | Flush to zero at 1e-15 | Prevent CPU spikes |
| Constexpr | Taylor series for sin/cos | MSVC compatibility |
| Cascading | Template with Butterworth Q | Flat passband |
| Edge cases | Clamp freq/Q, check NaN | Robustness |
