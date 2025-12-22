# Data Model: Biquad Filter

**Feature**: 004-biquad-filter
**Date**: 2025-12-22
**Purpose**: Define filter types, structures, and state management

---

## Entity Overview

```
┌─────────────────────────────────────────────────────────────┐
│                       FilterType                             │
│              (enum: LP, HP, BP, Notch, etc.)                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    BiquadCoefficients                        │
│        (b0, b1, b2, a1, a2 - normalized floats)             │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                         Biquad                               │
│         (coefficients + state z1, z2 + process)             │
└─────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
┌───────────────────────┐   ┌───────────────────────────────┐
│    SmoothedBiquad     │   │    BiquadCascade<N>           │
│  (with coeff smooth)  │   │   (N stages for steeper)      │
└───────────────────────┘   └───────────────────────────────┘
```

---

## 1. FilterType Enumeration

Defines the supported filter response types.

```cpp
enum class FilterType : uint8_t {
    Lowpass,      // 12 dB/oct lowpass, -3dB at cutoff
    Highpass,     // 12 dB/oct highpass, -3dB at cutoff
    Bandpass,     // Constant 0 dB peak gain
    Notch,        // Band-reject filter
    Allpass,      // Flat magnitude, phase shift
    LowShelf,     // Boost/cut below cutoff
    HighShelf,    // Boost/cut above cutoff
    Peak          // Parametric EQ bell curve
};
```

### Filter Type Characteristics

| Type | Magnitude at Cutoff | Parameters Used | Typical Use |
|------|---------------------|-----------------|-------------|
| Lowpass | -3 dB | freq, Q | Tone darkening, anti-alias |
| Highpass | -3 dB | freq, Q | DC blocking, rumble removal |
| Bandpass | 0 dB peak | freq, Q | Frequency isolation |
| Notch | -∞ dB | freq, Q | Hum removal, resonance kill |
| Allpass | 0 dB | freq, Q | Phase manipulation |
| LowShelf | gain dB | freq, Q, gain | Bass EQ |
| HighShelf | gain dB | freq, Q, gain | Treble EQ |
| Peak | gain dB | freq, Q, gain | Parametric EQ |

---

## 2. BiquadCoefficients Structure

Stores the 5 normalized filter coefficients.

```cpp
struct BiquadCoefficients {
    float b0 = 1.0f;   // Numerator coefficient (feedforward)
    float b1 = 0.0f;   // Numerator coefficient
    float b2 = 0.0f;   // Numerator coefficient
    float a1 = 0.0f;   // Denominator coefficient (feedback)
    float a2 = 0.0f;   // Denominator coefficient
    // Note: a0 is always 1.0 after normalization (not stored)
};
```

### Invariants

- All coefficients are real-valued floats
- Coefficients are pre-normalized (a0 = 1 implied)
- Default state represents unity gain (bypass)
- Valid coefficients produce stable filter (|a2| < 1)

### Factory Methods

```cpp
// Calculate coefficients for given filter type and parameters
static BiquadCoefficients calculate(
    FilterType type,
    float frequency,    // Hz (20 to Nyquist)
    float Q,            // Quality factor (0.1 to 30)
    float gainDb,       // dB gain for shelf/peak types
    float sampleRate    // Hz (44100 to 192000)
) noexcept;

// Constexpr version for compile-time calculation
static constexpr BiquadCoefficients calculateConstexpr(
    FilterType type,
    float frequency,
    float Q,
    float gainDb,
    float sampleRate
) noexcept;
```

---

## 3. Biquad Class

Core filter processor with TDF2 topology.

```cpp
class Biquad {
public:
    // === State ===
    float z1_ = 0.0f;    // First delay state
    float z2_ = 0.0f;    // Second delay state

    // === Coefficients ===
    BiquadCoefficients coeffs_;

    // === Lifecycle ===

    // Default: bypass (unity gain)
    Biquad() = default;

    // Initialize with coefficients
    explicit Biquad(const BiquadCoefficients& coeffs);

    // === Configuration ===

    // Set coefficients directly
    void setCoefficients(const BiquadCoefficients& coeffs) noexcept;

    // Configure for specific filter type (recalculates coefficients)
    void configure(
        FilterType type,
        float frequency,
        float Q,
        float gainDb,
        float sampleRate
    ) noexcept;

    // === Processing ===

    // Process single sample (TDF2)
    [[nodiscard]] float process(float input) noexcept;

    // Process buffer of samples
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // === State Management ===

    // Clear filter state (prevents clicks when restarting)
    void reset() noexcept;

    // Get current state for analysis
    [[nodiscard]] float getState1() const noexcept { return z1_; }
    [[nodiscard]] float getState2() const noexcept { return z2_; }
};
```

### State Transitions

```
                    ┌──────────────┐
                    │   Created    │
                    │ (bypass mode)│
                    └──────┬───────┘
                           │ configure() or setCoefficients()
                           ▼
                    ┌──────────────┐
        ┌───────────│  Configured  │───────────┐
        │           │  (active)    │           │
        │           └──────┬───────┘           │
        │                  │                   │
   configure()        process()            reset()
        │                  │                   │
        │                  ▼                   │
        │           ┌──────────────┐           │
        └──────────▶│  Processing  │───────────┘
                    │ (state != 0) │
                    └──────────────┘
```

---

## 4. SmoothedBiquad Class

Wrapper providing click-free parameter changes.

```cpp
class SmoothedBiquad {
public:
    // === Configuration ===

    // Set smoothing time for coefficient transitions
    void setSmoothingTime(float milliseconds, float sampleRate) noexcept;

    // Set target filter parameters (will smooth towards these)
    void setTarget(
        FilterType type,
        float frequency,
        float Q,
        float gainDb,
        float sampleRate
    ) noexcept;

    // Immediately jump to target (no smoothing)
    void snapToTarget() noexcept;

    // === Processing ===

    // Process with coefficient interpolation
    [[nodiscard]] float process(float input) noexcept;

    // Process block with coefficient interpolation
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // === State ===

    // Check if smoothing is complete
    [[nodiscard]] bool isSmoothing() const noexcept;

    // Clear filter and smoother state
    void reset() noexcept;

private:
    Biquad filter_;
    BiquadCoefficients target_;
    BiquadCoefficients current_;

    // 5 smoothers for 5 coefficients
    OnePoleSmoother smoothB0_, smoothB1_, smoothB2_;
    OnePoleSmoother smoothA1_, smoothA2_;

    float sampleRate_ = 44100.0f;
};
```

---

## 5. BiquadCascade Template

Multiple stages for steeper filter slopes.

```cpp
template<size_t NumStages>
class BiquadCascade {
public:
    static_assert(NumStages >= 1 && NumStages <= 8,
        "BiquadCascade supports 1-8 stages");

    // === Configuration ===

    // Set all stages with Butterworth alignment for flat passband
    void setButterworth(
        FilterType type,
        float frequency,
        float sampleRate
    ) noexcept;

    // Set individual stage
    void setStage(
        size_t index,
        const BiquadCoefficients& coeffs
    ) noexcept;

    // === Processing ===

    // Process through all stages
    [[nodiscard]] float process(float input) noexcept;

    // Process block through all stages
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // === State ===

    void reset() noexcept;

    // Access individual stage
    [[nodiscard]] Biquad& stage(size_t index) noexcept;
    [[nodiscard]] const Biquad& stage(size_t index) const noexcept;

private:
    std::array<Biquad, NumStages> stages_;
};

// Common type aliases
using Biquad2Stage = BiquadCascade<2>;  // 24 dB/oct (4-pole)
using Biquad4Stage = BiquadCascade<4>;  // 48 dB/oct (8-pole)
```

### Butterworth Q Values

For flat passband, each stage uses different Q:

| Stages | Total Slope | Q Values (per stage) |
|--------|-------------|----------------------|
| 1 | 12 dB/oct | 0.7071 |
| 2 | 24 dB/oct | 0.5412, 1.3066 |
| 3 | 36 dB/oct | 0.5176, 0.7071, 1.9319 |
| 4 | 48 dB/oct | 0.5098, 0.6013, 0.9000, 2.5628 |

---

## 6. Validation Rules

### Frequency

- Minimum: 1 Hz (practical low limit)
- Maximum: sampleRate * 0.495 (leave margin from Nyquist)
- Values outside range are clamped

### Q (Quality Factor)

- Minimum: 0.1 (very wide bandwidth)
- Maximum: 30.0 (very narrow, near self-oscillation)
- Default: 0.7071 (Butterworth, critically damped)

### Gain (for Shelf/Peak)

- Range: -24 dB to +24 dB (typical EQ range)
- 0 dB: No boost or cut (bypass-like for shelf/peak)
- Ignored for LP, HP, BP, Notch, Allpass types

### Sample Rate

- Minimum: 8000 Hz (telephone quality)
- Maximum: 384000 Hz (high-res audio)
- Common: 44100, 48000, 88200, 96000, 176400, 192000

---

## 7. Memory Layout

```
Biquad (32 bytes aligned):
┌─────────────────────────────────────────────────────────────┐
│ b0 (4) │ b1 (4) │ b2 (4) │ a1 (4) │ a2 (4) │ z1 (4) │ z2 (4)│ pad (4)
└─────────────────────────────────────────────────────────────┘
  0        4        8       12       16       20       24      28

SmoothedBiquad (~200 bytes):
┌─────────────────────────────────────────────────────────────┐
│ Biquad (32) │ target (20) │ current (20) │ smoothers (5x16) │ sr (4)
└─────────────────────────────────────────────────────────────┘

BiquadCascade<4> (128 bytes):
┌─────────────────────────────────────────────────────────────┐
│ stage[0] (32) │ stage[1] (32) │ stage[2] (32) │ stage[3] (32)│
└─────────────────────────────────────────────────────────────┘
```

---

## 8. Thread Safety

| Operation | Thread Safety | Notes |
|-----------|---------------|-------|
| process() | Not thread-safe | Audio thread only |
| configure() | Not thread-safe | Audio thread only |
| reset() | Not thread-safe | Audio thread only |
| setCoefficients() | Not thread-safe | Audio thread only |

**Design Note**: Filter instances should be per-voice or per-channel. No locking is used to maintain real-time safety. If coefficients need to be updated from a UI thread, use a lock-free queue to send new parameters to the audio thread.
