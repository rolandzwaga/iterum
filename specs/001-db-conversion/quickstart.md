# Quickstart: dB/Linear Conversion Utilities (Refactor)

**Feature Branch**: `001-db-conversion`
**Date**: 2025-12-22
**Type**: Refactor & Upgrade

## Migration Guide

This document describes the migration from the old `VSTWork::DSP` functions to the new `Iterum::DSP` Layer 0 utilities.

### Before (Old API)

```cpp
#include "dsp/dsp_utils.h"

using namespace VSTWork::DSP;

float gain = dBToLinear(-6.0f);     // inline, not constexpr
float dB = linearToDb(0.5f);        // -80 dB floor
```

### After (New API)

```cpp
#include "dsp/core/db_utils.h"

using namespace Iterum::DSP;

float gain = dbToGain(-6.0f);       // constexpr!
float dB = gainToDb(0.5f);          // -144 dB floor
```

## Installation

Include the new header:

```cpp
#include "dsp/core/db_utils.h"
```

Or continue using `dsp_utils.h` which now includes the core utilities:

```cpp
#include "dsp/dsp_utils.h"  // Now includes core/db_utils.h
```

## Basic Usage

### Convert dB to Linear Gain

```cpp
#include "dsp/core/db_utils.h"

using namespace Iterum::DSP;

// Unity gain (0 dB = 1.0)
float gain1 = dbToGain(0.0f);      // Returns 1.0f

// -6 dB attenuation (approximately half amplitude)
float gain2 = dbToGain(-6.0f);     // Returns ~0.501f

// +12 dB boost
float gain3 = dbToGain(12.0f);     // Returns ~3.98f

// Silence
float gain4 = dbToGain(-144.0f);   // Returns ~6.3e-8f
```

### Convert Linear Gain to dB

```cpp
#include "dsp/core/db_utils.h"

using namespace Iterum::DSP;

// Unity gain
float dB1 = gainToDb(1.0f);        // Returns 0.0f

// Half amplitude
float dB2 = gainToDb(0.5f);        // Returns ~-6.02f

// Double amplitude
float dB3 = gainToDb(2.0f);        // Returns ~+6.02f

// Silence (zero gain)
float dB4 = gainToDb(0.0f);        // Returns -144.0f (was -80.0f in old API)
```

## Key Changes from Old API

### 1. Function Names

| Old | New |
|-----|-----|
| `dBToLinear(dB)` | `dbToGain(dB)` |
| `linearToDb(linear)` | `gainToDb(gain)` |

### 2. Namespace

| Old | New |
|-----|-----|
| `VSTWork::DSP` | `Iterum::DSP` |

### 3. Silence Floor (Breaking Change)

| Old | New |
|-----|-----|
| `-80.0f` dB | `-144.0f` dB |

This affects code that compares against the floor value or expects -80 dB for silence.

### 4. constexpr Support (New!)

```cpp
// Now works! (didn't work with old API)
constexpr float kDefaultGainDb = -6.0f;
constexpr float kDefaultGainLinear = dbToGain(kDefaultGainDb);

// Use in parameter initialization
std::atomic<float> gain_{kDefaultGainLinear};
```

### 5. NaN Handling (New!)

```cpp
// Old: NaN propagated through
// New: Safe fallback values
float nanValue = std::numeric_limits<float>::quiet_NaN();
float gain = dbToGain(nanValue);   // Returns 0.0f (safe)
float dB = gainToDb(nanValue);     // Returns -144.0f (safe)
```

## Common Use Cases

### Gain Parameter Processing

```cpp
void processBlock(float* buffer, size_t numSamples, float gainDb) {
    const float gainLinear = dbToGain(gainDb);

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] *= gainLinear;
    }
}
```

### Level Metering

```cpp
float calculatePeakDb(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return gainToDb(peak);  // Returns dB for UI, clamped to -144 minimum
}
```

### Compile-Time Constants

```cpp
// These are evaluated at compile time!
constexpr float kUnityGainDb = 0.0f;
constexpr float kUnityGainLinear = dbToGain(kUnityGainDb);  // 1.0f

constexpr float kHalfGainDb = -6.0206f;
constexpr float kHalfGainLinear = dbToGain(kHalfGainDb);    // ~0.5f

constexpr float kSilenceGainLinear = dbToGain(kSilenceFloorDb);  // ~6.3e-8f
```

## Search & Replace Guide

To migrate existing code:

```bash
# Find usages (from project root)
grep -r "dBToLinear\|linearToDb\|VSTWork::DSP" src/

# Manual replacements:
# VSTWork::DSP::dBToLinear  ->  Iterum::DSP::dbToGain
# VSTWork::DSP::linearToDb  ->  Iterum::DSP::gainToDb
# VSTWork::DSP  ->  Iterum::DSP
# kSilenceThreshold  ->  (remove, use kSilenceFloorDb if needed)
```

## Testing

Run the unit tests to verify correct behavior:

```bash
cmake --build build --config Debug --target dsp_tests
ctest --test-dir build/tests -C Debug --output-on-failure
```

## API Reference

See [contracts/db_utils.h](contracts/db_utils.h) for the complete API specification.

| Symbol | Type | Description |
|--------|------|-------------|
| `dbToGain(dB)` | `constexpr float` | Convert dB to linear gain |
| `gainToDb(gain)` | `constexpr float` | Convert linear gain to dB |
| `kSilenceFloorDb` | `constexpr float = -144.0f` | Silence floor constant |
