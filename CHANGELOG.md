# Changelog

All notable changes to Iterum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.1] - 2025-12-22

### Added

- **Layer 0 Core Utilities: dB/Linear Conversion** (`src/dsp/core/db_utils.h`)
  - `Iterum::DSP::dbToGain(float dB)` - Convert decibels to linear gain
  - `Iterum::DSP::gainToDb(float gain)` - Convert linear gain to decibels
  - `Iterum::DSP::kSilenceFloorDb` - Silence floor constant (-144 dB)
  - Full C++20 `constexpr` support for compile-time evaluation
  - Real-time safe: no allocation, no exceptions, no I/O
  - NaN handling: `dbToGain(NaN)` returns 0.0f, `gainToDb(NaN)` returns -144 dB

- **Custom constexpr math implementations** (MSVC compatibility)
  - Taylor series `constexprExp()` and `constexprLn()` functions
  - Required because MSVC lacks constexpr `std::pow`/`std::log10`

- **Comprehensive test suite** (146 assertions across 24 test cases)
  - Unit tests for all dB conversion functions
  - Constexpr compile-time evaluation tests
  - Edge case coverage (NaN, infinity, silence)

- **Project infrastructure**
  - Layered DSP architecture (Layer 0-4 hierarchy)
  - Test-first development workflow (Constitution Principle XII)
  - Catch2 testing framework integration

### Technical Details

- **Silence floor**: -144 dB (24-bit dynamic range: 6.02 dB/bit * 24 = ~144 dB)
- **Formulas**:
  - `dbToGain`: gain = 10^(dB/20)
  - `gainToDb`: dB = 20 * log10(gain), clamped to -144 dB floor
- **Namespace**: `Iterum::DSP` (Layer 0 core utilities)

### Usage

```cpp
#include "dsp/core/db_utils.h"

// Runtime conversion
float gain = Iterum::DSP::dbToGain(-6.0f);    // ~0.5
float dB   = Iterum::DSP::gainToDb(0.5f);     // ~-6 dB

// Compile-time lookup tables
constexpr std::array<float, 3> gains = {
    Iterum::DSP::dbToGain(-20.0f),  // 0.1
    Iterum::DSP::dbToGain(0.0f),    // 1.0
    Iterum::DSP::dbToGain(20.0f)    // 10.0
};
```
