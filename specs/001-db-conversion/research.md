# Research: dB/Linear Conversion Utilities (Refactor)

**Feature Branch**: `001-db-conversion`
**Date**: 2025-12-22
**Type**: Refactor & Upgrade

## Existing Code Analysis

### Current Implementation

**File**: `src/dsp/dsp_utils.h`
**Namespace**: `VSTWork::DSP`

```cpp
// Current constants
constexpr float kSilenceThreshold = 1e-8f;

// Current functions
[[nodiscard]] inline float dBToLinear(float dB) noexcept {
    return std::pow(10.0f, dB / 20.0f);
}

[[nodiscard]] inline float linearToDb(float linear) noexcept {
    if (linear <= kSilenceThreshold) {
        return -80.0f;  // Silence floor
    }
    return 20.0f * std::log10(linear);
}
```

### Issues Identified

| Issue | Severity | Description |
|-------|----------|-------------|
| Not constexpr | Medium | Cannot be used for compile-time constant initialization |
| Low silence floor | Low | -80 dB is ~13-bit dynamic range, insufficient for 24-bit audio |
| No NaN handling | Medium | Could propagate invalid values through signal chain |
| Layer mixing | Low | dB utilities mixed with Layer 1 components (OnePoleSmoother) |
| Legacy namespace | Low | Uses `VSTWork` instead of project rename `Iterum` |

### Usage Search

Searched codebase for usages of existing functions:

```bash
grep -r "dBToLinear\|linearToDb" src/
```

**Files using these functions**:
- `src/dsp/dsp_utils.h` - Definition (to be refactored)
- No other usages found in current skeleton project

**Conclusion**: Migration impact is minimal - only the definition file needs changes.

## Research Tasks

### 1. VST3 SDK dB utilities check

**Decision**: Implement our own - VST3 SDK does not provide dedicated functions.

**Rationale**: Comprehensive search of VST3 SDK source confirmed:
- `LogScale` class exists but is for custom curves, not standard dB formula
- Steinberg's own samples use inline math for dB conversion
- No constexpr-compatible utilities available

### 2. C++20 constexpr math availability

**Decision**: Use C++20 `std::pow` and `std::log10` which are constexpr.

**Rationale**:
- C++20 made most `<cmath>` functions constexpr
- MSVC 2019 16.8+, GCC 10+, Clang 12+ all support this
- All target platforms meet requirements per constitution

**Verification needed**: Test that `constexpr` context compiles on all target platforms.

### 3. Silence floor value selection

**Decision**: Upgrade from -80 dB to -144 dB.

**Rationale**:
- -144 dB represents 24-bit dynamic range (6.02 dB/bit × 24 = 144.5 dB)
- Industry standard for professional audio
- Current -80 dB is only ~13-bit equivalent
- Linear gain equivalent: ~6.3e-8 (safely above float32 epsilon)

**Breaking change**: Code relying on -80 dB floor behavior will see different values for very quiet signals. This is acceptable as -144 dB is more accurate.

### 4. NaN handling strategy

**Decision**: Return safe fallback values for invalid inputs.

**Rationale**:
- Real-time audio thread cannot handle exceptions
- Silent failure with defined behavior is safer than NaN propagation
- Consistent with constitution Principle II (Real-Time Safety)

**Implementation**:
```cpp
// NaN check (works in constexpr context)
if (value != value) return fallback;  // NaN != NaN is true
```

### 5. Function naming

**Decision**: Rename to `dbToGain` / `gainToDb` (lowercase 'db').

**Rationale**:
- More consistent with common audio library conventions
- Clearer that output is "gain" (multiplier) not just "linear"
- Follows project naming convention (camelCase with lowercase acronyms)

**Alternatives considered**:
- Keep `dBToLinear` / `linearToDb`: Rejected - opportunity to improve naming
- `decibelToGain` / `gainToDecibel`: Rejected - overly verbose

## Migration Strategy

### Phase 1: Create new Layer 0 utilities

1. Create `src/dsp/core/` directory
2. Create `src/dsp/core/db_utils.h` with:
   - `Iterum::DSP::kSilenceFloorDb = -144.0f`
   - `Iterum::DSP::dbToGain(float dB)` - constexpr
   - `Iterum::DSP::gainToDb(float gain)` - constexpr

### Phase 2: Update existing dsp_utils.h

1. Add `#include "core/db_utils.h"`
2. Remove old `dBToLinear`, `linearToDb`, `kSilenceThreshold`
3. Option A: Add compatibility aliases in `VSTWork::DSP` namespace
4. Option B: Update namespace to `Iterum::DSP` throughout

**Recommendation**: Option B - clean break, project is being renamed anyway.

### Phase 3: Update any usages

1. Search for any remaining `VSTWork::DSP::dBToLinear` usages
2. Replace with `Iterum::DSP::dbToGain`
3. Search for any remaining `VSTWork::DSP::linearToDb` usages
4. Replace with `Iterum::DSP::gainToDb`

## Dependencies

No external dependencies required. Implementation uses only:
- `<cmath>` - `std::pow`, `std::log10` (constexpr in C++20)

## Open Questions

*None - all clarifications resolved.*
