# Data Model: dB/Linear Conversion Utilities (Refactor)

**Feature Branch**: `001-db-conversion`
**Date**: 2025-12-22
**Type**: Refactor & Upgrade

## Overview

This refactor extracts and upgrades the existing dB/linear conversion functions from `src/dsp/dsp_utils.h` into proper Layer 0 core utilities at `src/dsp/core/db_utils.h`.

## Migration Mapping

| Current | New | Notes |
|---------|-----|-------|
| `VSTWork::DSP::dBToLinear` | `Iterum::DSP::dbToGain` | Renamed, now constexpr |
| `VSTWork::DSP::linearToDb` | `Iterum::DSP::gainToDb` | Renamed, now constexpr, -144dB floor |
| `VSTWork::DSP::kSilenceThreshold` | `Iterum::DSP::kSilenceFloorDb` | Changed from linear (1e-8) to dB (-144) |

## Constants

### `kSilenceFloorDb`

| Property | Current | New |
|----------|---------|-----|
| Name | `kSilenceThreshold` | `kSilenceFloorDb` |
| Type | `constexpr float` | `constexpr float` |
| Value | `1e-8f` (linear) | `-144.0f` (dB) |
| Purpose | Threshold for silence detection | Floor value returned for zero/negative gain |
| Rationale | - | Represents 24-bit dynamic range |

## Functions

### `dbToGain` (was `dBToLinear`)

Converts decibel values to linear gain multipliers.

| Property | Current | New |
|----------|---------|-----|
| Name | `dBToLinear` | `dbToGain` |
| Signature | `inline float dBToLinear(float dB) noexcept` | `constexpr float dbToGain(float dB) noexcept` |
| constexpr | No | Yes |
| NaN handling | None | Returns `0.0f` |
| Formula | `10^(dB/20)` | `10^(dB/20)` (unchanged) |

**Input/Output Examples** (unchanged behavior):

| Input (dB) | Output (gain) | Notes |
|------------|---------------|-------|
| `0.0f` | `1.0f` | Unity gain |
| `-6.0206f` | `~0.5f` | Half amplitude |
| `+6.0206f` | `~2.0f` | Double amplitude |
| `-20.0f` | `0.1f` | -20 dB attenuation |
| `+20.0f` | `10.0f` | +20 dB boost |
| `NaN` | `0.0f` | **NEW**: Safe fallback |

### `gainToDb` (was `linearToDb`)

Converts linear gain values to decibels.

| Property | Current | New |
|----------|---------|-----|
| Name | `linearToDb` | `gainToDb` |
| Signature | `inline float linearToDb(float linear) noexcept` | `constexpr float gainToDb(float gain) noexcept` |
| constexpr | No | Yes |
| Silence floor | `-80.0f` | `-144.0f` |
| NaN handling | None | Returns `-144.0f` |

**Input/Output Examples**:

| Input (gain) | Current Output | New Output | Change? |
|--------------|----------------|------------|---------|
| `1.0f` | `0.0f` | `0.0f` | No |
| `0.5f` | `~-6.02f` | `~-6.02f` | No |
| `2.0f` | `~+6.02f` | `~+6.02f` | No |
| `0.1f` | `-20.0f` | `-20.0f` | No |
| `0.0f` | `-80.0f` | `-144.0f` | **YES** |
| `1e-10f` | `-80.0f` | `-144.0f` | **YES** |
| `-0.5f` | `-80.0f` | `-144.0f` | No (same behavior) |
| `NaN` | `NaN` (propagated) | `-144.0f` | **YES** |

## Validation Rules

### dbToGain

| Rule | Current | New |
|------|---------|-----|
| NaN input | Propagates NaN | Returns `0.0f` |
| Extreme positive dB | Returns large value | Same |
| Extreme negative dB | Returns small value | Same |

### gainToDb

| Rule | Current | New |
|------|---------|-----|
| Zero input | Returns `-80.0f` | Returns `-144.0f` |
| Negative input | Returns `-80.0f` | Returns `-144.0f` |
| NaN input | Propagates NaN | Returns `-144.0f` |
| Very small positive | Returns `-80.0f` if < 1e-8 | Returns `-144.0f` if result < -144 |

## File Structure Changes

### Before

```text
src/dsp/
└── dsp_utils.h          # Contains dB functions + OnePoleSmoother + buffer ops
```

### After

```text
src/dsp/
├── dsp_utils.h          # Includes core/db_utils.h, keeps other utilities
└── core/                # NEW: Layer 0 directory
    └── db_utils.h       # NEW: Extracted constexpr dB utilities
```

## Namespace Changes

```cpp
// Before
namespace VSTWork {
namespace DSP {
    float dBToLinear(float dB);
    float linearToDb(float linear);
}}

// After
namespace Iterum {
namespace DSP {
    constexpr float dbToGain(float dB) noexcept;
    constexpr float gainToDb(float gain) noexcept;
}}
```

## Memory Footprint

| Item | Before | After |
|------|--------|-------|
| Static storage | 0 bytes | 0 bytes |
| Stack usage per call | ~8 bytes | ~8 bytes |
| Heap allocation | 0 bytes | 0 bytes |

No change in memory footprint - both are inline/constexpr functions.
