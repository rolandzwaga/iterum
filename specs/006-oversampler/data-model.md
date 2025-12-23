# Data Model: Oversampler

**Feature**: 006-oversampler | **Date**: 2025-12-23 | **Spec**: [spec.md](spec.md)

## Entity Definitions

### OversamplingFactor

Enumeration of supported oversampling factors.

```cpp
enum class OversamplingFactor : uint8_t {
    x2 = 2,  // 2x oversampling (most common)
    x4 = 4   // 4x oversampling (for heavy distortion)
};
```

**Constraints**:
- Only 2x and 4x are supported (1x would be bypass, 8x is rarely needed in real-time)
- Value represents actual multiplication factor for buffer size calculation

### OversamplingQuality

Enumeration of filter quality presets.

```cpp
enum class OversamplingQuality : uint8_t {
    Economy,   // IIR 8-pole, ~48dB stopband, 0 latency
    Standard,  // FIR 31-tap, ~80dB stopband, 15 samples latency (2x)
    High       // FIR 63-tap, ~100dB stopband, 31 samples latency (2x)
};
```

**Constraints**:
- Economy always uses IIR (ZeroLatency mode implied)
- Standard and High use FIR (LinearPhase mode)

### OversamplingMode

Enumeration of latency/phase modes (derived from quality in most cases).

```cpp
enum class OversamplingMode : uint8_t {
    ZeroLatency,   // IIR filters, minimum-phase, no additional latency
    LinearPhase    // FIR filters, symmetric, adds latency
};
```

**Relationships**:
- Economy quality → ZeroLatency mode (fixed)
- Standard/High quality → LinearPhase mode (default) or ZeroLatency (explicit override)

### Oversampler

Main processing class. Template parameters control factor and channel count.

```cpp
template<OversamplingFactor Factor = OversamplingFactor::x2, size_t NumChannels = 2>
class Oversampler {
    // Configuration
    OversamplingQuality quality_;
    OversamplingMode mode_;
    float sampleRate_;
    size_t maxBlockSize_;

    // IIR filters (per channel, per stage)
    // For 4x: stage 0 = first 2x, stage 1 = second 2x
    std::array<BiquadCascade<4>, NumChannels * NumStages> upsampleFilters_;
    std::array<BiquadCascade<4>, NumChannels * NumStages> downsampleFilters_;

    // FIR filters (for LinearPhase mode)
    std::array<HalfbandFilter, NumChannels * NumStages> firUpsampleFilters_;
    std::array<HalfbandFilter, NumChannels * NumStages> firDownsampleFilters_;

    // Pre-allocated buffers
    std::vector<float> oversampledBuffer_; // Size: maxBlockSize * Factor * NumChannels

    // State
    size_t latencySamples_;
};
```

**Invariants**:
- `Factor` is compile-time constant (2 or 4)
- `NumChannels` is compile-time constant (1 or 2)
- All buffers allocated in `prepare()`, never during `process()`
- Filter state cleared on `reset()` or sample rate change

**Computed Values**:
- `NumStages` = log2(Factor) → 1 for 2x, 2 for 4x
- Buffer size = maxBlockSize * Factor * NumChannels

### HalfbandFilter

FIR filter optimized for 2x rate change with symmetric coefficients.

```cpp
class HalfbandFilter {
    // Coefficients (only non-zero taps stored)
    std::array<float, MaxTaps / 2> coefficients_;
    size_t numTaps_;          // Actual tap count (15, 31, or 63)

    // Delay line for filter state
    std::array<float, MaxTaps> delayLine_;
    size_t delayIndex_;

    static constexpr size_t MaxTaps = 64; // Maximum for High quality
};
```

**Invariants**:
- `numTaps_` is odd (symmetric FIR)
- Halfband: every other coefficient (except center) is zero
- Only store/compute non-zero coefficients for efficiency
- Center coefficient is always 0.5

## Relationships

```
┌─────────────────────────────────────────────────────────────────┐
│                          Oversampler                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │ Factor (2/4) │  │ Quality      │  │ Mode                 │   │
│  │ (template)   │  │ (runtime)    │  │ (derived from        │   │
│  └──────────────┘  └──────────────┘  │  quality or explicit)│   │
│                                       └──────────────────────┘   │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    Filter Arrays                            │  │
│  │  ┌─────────────────┐    ┌─────────────────┐                │  │
│  │  │ BiquadCascade<4>│    │ HalfbandFilter  │                │  │
│  │  │ (IIR mode)      │    │ (FIR mode)      │                │  │
│  │  │ × NumChannels   │    │ × NumChannels   │                │  │
│  │  │ × NumStages     │    │ × NumStages     │                │  │
│  │  └─────────────────┘    └─────────────────┘                │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │           oversampledBuffer_ (pre-allocated)               │  │
│  │           [maxBlockSize × Factor × NumChannels]            │  │
│  └────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## State Transitions

### Oversampler Lifecycle

```
                    ┌─────────────┐
                    │  Created    │
                    │(no buffers) │
                    └──────┬──────┘
                           │ prepare(sampleRate, maxBlockSize, quality)
                           ▼
                    ┌─────────────┐
                    │  Prepared   │◄────────────────┐
                    │(ready to    │                 │
                    │ process)    │                 │ prepare() with new params
                    └──────┬──────┘                 │
                           │ process()              │
                           ▼                        │
                    ┌─────────────┐                 │
                    │ Processing  │─────────────────┘
                    │(active)     │
                    └──────┬──────┘
                           │ reset()
                           ▼
                    ┌─────────────┐
                    │   Reset     │
                    │(filter      │──► returns to Prepared
                    │ state clear)│
                    └─────────────┘
```

### Quality → Mode Mapping

| Quality | Default Mode | Override Allowed |
|---------|-------------|------------------|
| Economy | ZeroLatency | No (IIR only) |
| Standard | LinearPhase | Yes (can force ZeroLatency) |
| High | LinearPhase | Yes (can force ZeroLatency) |

## Memory Layout

### Buffer Allocation (in `prepare()`)

For 2x stereo, maxBlockSize=512:
- `oversampledBuffer_`: 512 * 2 * 2 = 2048 floats = 8KB

For 4x stereo, maxBlockSize=512:
- `oversampledBuffer_`: 512 * 4 * 2 = 4096 floats = 16KB

For 4x stereo, maxBlockSize=8192 (maximum):
- `oversampledBuffer_`: 8192 * 4 * 2 = 65536 floats = 256KB

### Filter State (fixed size)

IIR Mode (per filter instance):
- 4 biquad stages × 2 state variables × 4 bytes = 32 bytes
- Total: 8 instances × 32 = 256 bytes

FIR Mode (per filter instance):
- 64 delay line samples × 4 bytes = 256 bytes
- 32 coefficients × 4 bytes = 128 bytes
- Total: 8 instances × 384 = 3072 bytes (~3KB)

## Processing Data Flow

### Upsample (2x Example)

```
Input[N] ──► Zero-stuff ──► [0,x0,0,x1,0,x2...] ──► Lowpass Filter ──► Output[2N]
            (insert 0s)      [2N samples]           (× 2 gain)
```

### Downsample (2x Example)

```
Input[2N] ──► Lowpass Filter ──► Decimate ──► Output[N]
              (anti-alias)       (take every 2nd sample)
```

### Complete Oversample-Process-Downsample

```
Input[N] ──► Upsample(2x) ──► [2N samples] ──► User Process ──► Downsample(2x) ──► Output[N]
                               (user provides               (oversampler
                                callback/lambda)             downsamples result)
```

## Thread Safety Considerations

| Operation | Thread | Notes |
|-----------|--------|-------|
| Constructor | Any | No state |
| prepare() | Setup thread | Allocates buffers, not real-time safe |
| process() | Audio thread | No allocations, noexcept |
| reset() | Any | Just clears filter state |
| setQuality() | Setup thread | Must call prepare() after |

**Critical**: `process()` must never allocate memory. All buffers sized for maximum block size.
