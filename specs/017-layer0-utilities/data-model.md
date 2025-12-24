# Data Model: Layer 0 Utilities

**Feature**: 017-layer0-utilities
**Date**: 2025-12-24

## Entities

### NoteValue (Enum)

Musical note duration values for tempo synchronization.

```cpp
namespace Iterum::DSP {

/// @brief Musical note divisions for tempo sync.
enum class NoteValue : uint8_t {
    Whole = 0,       ///< 1/1 note (4 beats at 4/4)
    Half,            ///< 1/2 note (2 beats)
    Quarter,         ///< 1/4 note (1 beat) - default
    Eighth,          ///< 1/8 note (0.5 beats)
    Sixteenth,       ///< 1/16 note (0.25 beats)
    ThirtySecond     ///< 1/32 note (0.125 beats)
};

/// @brief Timing modifiers for note values.
enum class NoteModifier : uint8_t {
    None = 0,        ///< Normal duration (default)
    Dotted,          ///< 1.5x duration
    Triplet          ///< 2/3x duration
};

} // namespace Iterum::DSP
```

**Beats per note value** (at 4/4 time signature):
| NoteValue | Beats |
|-----------|-------|
| Whole | 4.0 |
| Half | 2.0 |
| Quarter | 1.0 |
| Eighth | 0.5 |
| Sixteenth | 0.25 |
| ThirtySecond | 0.125 |

**Modifier multipliers**:
| NoteModifier | Multiplier |
|--------------|------------|
| None | 1.0 |
| Dotted | 1.5 |
| Triplet | 2/3 ≈ 0.6667 |

---

### BlockContext (Struct)

Per-block processing context carrying sample rate, block size, tempo, transport state, and time signature.

```cpp
namespace Iterum::DSP {

/// @brief Per-block processing context for DSP components.
///
/// Carries host-provided information about the current processing block.
/// Used by tempo-synced components (delays, LFOs) and transport-aware features.
///
/// @note All member access is noexcept. No dynamic allocation.
/// @note Default values represent a typical standalone scenario.
struct BlockContext {
    // =========================================================================
    // Audio Context
    // =========================================================================

    double sampleRate = 44100.0;      ///< Sample rate in Hz (FR-001)
    size_t blockSize = 512;           ///< Block size in samples (FR-002)

    // =========================================================================
    // Tempo Context
    // =========================================================================

    double tempoBPM = 120.0;          ///< Tempo in beats per minute (FR-003)
    uint8_t timeSignatureNumerator = 4;    ///< Time signature numerator (FR-004)
    uint8_t timeSignatureDenominator = 4;  ///< Time signature denominator (FR-004)

    // =========================================================================
    // Transport Context
    // =========================================================================

    bool isPlaying = false;           ///< Transport playing state (FR-005)
    int64_t transportPositionSamples = 0;  ///< Position from song start (FR-006)

    // =========================================================================
    // Methods
    // =========================================================================

    /// @brief Convert note value to sample count at current tempo/sample rate.
    /// @param note The note value (quarter, eighth, etc.)
    /// @param modifier Optional timing modifier (dotted, triplet)
    /// @return Sample count for the note duration
    [[nodiscard]] constexpr size_t tempoToSamples(
        NoteValue note,
        NoteModifier modifier = NoteModifier::None
    ) const noexcept;

    /// @brief Get the duration of one beat in samples at current tempo.
    /// @return Samples per beat
    [[nodiscard]] constexpr size_t samplesPerBeat() const noexcept;

    /// @brief Get the duration of one bar/measure in samples.
    /// @return Samples per bar
    [[nodiscard]] constexpr size_t samplesPerBar() const noexcept;
};

} // namespace Iterum::DSP
```

**Default values rationale**:
- `sampleRate = 44100.0` - Standard CD quality
- `blockSize = 512` - Common DAW default
- `tempoBPM = 120.0` - Standard default tempo
- `timeSignature = 4/4` - Most common time signature
- `isPlaying = false` - Safe default (stopped)
- `transportPositionSamples = 0` - Start of song

---

### FastMath (Namespace)

Optimized approximations of transcendental functions.

```cpp
namespace Iterum::DSP::FastMath {

/// @brief Fast sine approximation using 5th-order polynomial.
/// @param x Angle in radians
/// @return Approximate sin(x), max error 0.1% for [-2π, 2π]
[[nodiscard]] constexpr float fastSin(float x) noexcept;

/// @brief Fast cosine approximation using 5th-order polynomial.
/// @param x Angle in radians
/// @return Approximate cos(x), max error 0.1% for [-2π, 2π]
[[nodiscard]] constexpr float fastCos(float x) noexcept;

/// @brief Fast hyperbolic tangent approximation using Padé approximant.
/// @param x Input value
/// @return Approximate tanh(x), max error 0.5% for |x|<3, 1% for larger
[[nodiscard]] constexpr float fastTanh(float x) noexcept;

/// @brief Fast exponential approximation using range-reduced Taylor series.
/// @param x Exponent
/// @return Approximate exp(x), max error 0.5% for x in [-10, 10]
[[nodiscard]] constexpr float fastExp(float x) noexcept;

} // namespace Iterum::DSP::FastMath
```

**Accuracy requirements** (from spec):
| Function | Range | Max Error |
|----------|-------|-----------|
| fastSin | [-2π, 2π] | 0.1% |
| fastCos | [-2π, 2π] | 0.1% |
| fastTanh | \|x\| < 3 | 0.5% |
| fastTanh | \|x\| >= 3 | 1.0% |
| fastExp | [-10, 10] | 0.5% |

---

### Interpolation (Namespace)

Standalone interpolation utilities for sample-domain operations.

```cpp
namespace Iterum::DSP::Interpolation {

/// @brief Linear interpolation between two samples.
/// @param y0 Sample at position 0
/// @param y1 Sample at position 1
/// @param t Fractional position [0, 1]
/// @return Interpolated value
[[nodiscard]] constexpr float linearInterpolate(
    float y0, float y1, float t
) noexcept;

/// @brief Cubic Hermite (Catmull-Rom) interpolation using 4 samples.
/// @param ym1 Sample at position -1
/// @param y0 Sample at position 0
/// @param y1 Sample at position 1
/// @param y2 Sample at position 2
/// @param t Fractional position [0, 1] between y0 and y1
/// @return Interpolated value
[[nodiscard]] constexpr float cubicHermiteInterpolate(
    float ym1, float y0, float y1, float y2, float t
) noexcept;

/// @brief 4-point Lagrange interpolation.
/// @param ym1 Sample at position -1
/// @param y0 Sample at position 0
/// @param y1 Sample at position 1
/// @param y2 Sample at position 2
/// @param t Fractional position [0, 1] between y0 and y1
/// @return Interpolated value
[[nodiscard]] constexpr float lagrangeInterpolate(
    float ym1, float y0, float y1, float y2, float t
) noexcept;

} // namespace Iterum::DSP::Interpolation
```

**Interpolation formulas**:

1. **Linear**: `y = y0 + t * (y1 - y0)`

2. **Cubic Hermite (Catmull-Rom)**:
   ```
   c0 = y0
   c1 = 0.5 * (y1 - ym1)
   c2 = ym1 - 2.5*y0 + 2*y1 - 0.5*y2
   c3 = 0.5*(y2 - ym1) + 1.5*(y0 - y1)
   y = ((c3*t + c2)*t + c1)*t + c0
   ```

3. **Lagrange (4-point)**:
   ```
   L0 = -t*(t-1)*(t-2)/6
   L1 = (t+1)*(t-1)*(t-2)/2
   L2 = -(t+1)*t*(t-2)/2
   L3 = (t+1)*t*(t-1)/6
   y = L0*ym1 + L1*y0 + L2*y1 + L3*y2
   ```

---

## Relationships

```
┌─────────────────────────────────────────────────────────────┐
│                     note_value.h (Layer 0)                  │
│   NoteValue enum, NoteModifier enum                         │
└─────────────────────────────────────────────────────────────┘
                              │
            ┌─────────────────┼─────────────────┐
            ▼                 ▼                 ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│ block_context.h │  │   fast_math.h   │  │ interpolation.h │
│  BlockContext   │  │  fastSin/Cos    │  │ linearInterp... │
│ (uses NoteValue)│  │  fastTanh/Exp   │  │ cubicHermite... │
└─────────────────┘  └─────────────────┘  │  lagrange...    │
                                          └─────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│               lfo.h (Layer 1 - updated)                     │
│   #include "dsp/core/note_value.h"                          │
│   Uses NoteValue and NoteModifier from Layer 0              │
└─────────────────────────────────────────────────────────────┘
```

## Constants

### note_value.h

```cpp
/// @brief Beats per note value (at 4/4 time signature).
/// Array indexed by NoteValue enum.
inline constexpr float kBeatsPerNote[] = {
    4.0f,    // Whole
    2.0f,    // Half
    1.0f,    // Quarter
    0.5f,    // Eighth
    0.25f,   // Sixteenth
    0.125f   // ThirtySecond
};

/// @brief Modifier multipliers.
/// Array indexed by NoteModifier enum.
inline constexpr float kModifierMultiplier[] = {
    1.0f,         // None
    1.5f,         // Dotted
    2.0f / 3.0f   // Triplet
};
```

### fast_math.h

```cpp
/// @brief Polynomial coefficients for fastSin (5th-order minimax).
namespace detail {
    inline constexpr float kSinC1 = 0.99997f;
    inline constexpr float kSinC3 = -0.16605f;
    inline constexpr float kSinC5 = 0.00761f;
}
```

### block_context.h

```cpp
/// @brief Minimum tempo in BPM (prevents division issues).
inline constexpr double kMinTempoBPM = 20.0;

/// @brief Maximum tempo in BPM (reasonable musical limit).
inline constexpr double kMaxTempoBPM = 300.0;
```
