// ============================================================================
// API CONTRACT: db_utils.h
// ============================================================================
// Feature Branch: 001-db-conversion
// Layer: 0 (Core Utilities)
// Date: 2025-12-22
// Type: Refactor & Upgrade
// Status: DRAFT - Implementation must match this contract
// ============================================================================
//
// This file defines the public API contract for the refactored dB/linear
// conversion utilities. The actual implementation will be placed in:
//   src/dsp/core/db_utils.h
//
// Migration from:
//   src/dsp/dsp_utils.h (VSTWork::DSP::dBToLinear, VSTWork::DSP::linearToDb)
//
// ============================================================================

#pragma once

#include <cmath>

namespace Iterum {
namespace DSP {

// ============================================================================
// Constants
// ============================================================================

/// Floor value for silence/zero gain in decibels.
/// Represents approximately 24-bit dynamic range (6.02 dB/bit * 24 = ~144 dB).
/// Used as the return value when gain is zero, negative, or NaN.
///
/// MIGRATION NOTE: Replaces VSTWork::DSP::kSilenceThreshold (1e-8f linear)
///                 Previous floor was -80 dB, now -144 dB for 24-bit support
constexpr float kSilenceFloorDb = -144.0f;

// ============================================================================
// Functions
// ============================================================================

/// Convert decibels to linear gain.
///
/// @param dB  Decibel value (any finite float)
/// @return    Linear gain multiplier (>= 0)
///
/// @formula   gain = 10^(dB/20)
///
/// @note      Real-time safe: no allocation, no exceptions
/// @note      Constexpr: usable at compile time (C++20)
/// @note      NaN input returns 0.0f (NEW behavior)
///
/// MIGRATION NOTE: Replaces VSTWork::DSP::dBToLinear
///                 - Now constexpr (was inline only)
///                 - Now handles NaN (was undefined)
///                 - Renamed from dBToLinear to dbToGain
///
/// @example   dbToGain(0.0f)    -> 1.0f     (unity gain)
/// @example   dbToGain(-6.02f)  -> ~0.5f    (half amplitude)
/// @example   dbToGain(-20.0f)  -> 0.1f     (-20 dB)
/// @example   dbToGain(+20.0f)  -> 10.0f    (+20 dB)
///
[[nodiscard]] constexpr float dbToGain(float dB) noexcept;

/// Convert linear gain to decibels.
///
/// @param gain  Linear gain value
/// @return      Decibel value (clamped to kSilenceFloorDb minimum)
///
/// @formula     dB = 20 * log10(gain), clamped to floor for invalid inputs
///
/// @note        Real-time safe: no allocation, no exceptions
/// @note        Constexpr: usable at compile time (C++20)
/// @note        Zero/negative/NaN input returns kSilenceFloorDb (-144 dB)
///
/// MIGRATION NOTE: Replaces VSTWork::DSP::linearToDb
///                 - Now constexpr (was inline only)
///                 - Floor changed from -80 dB to -144 dB (BREAKING CHANGE)
///                 - Now handles NaN (was undefined)
///                 - Renamed from linearToDb to gainToDb
///
/// @example     gainToDb(1.0f)   -> 0.0f      (unity = 0 dB)
/// @example     gainToDb(0.5f)   -> ~-6.02f   (half amplitude)
/// @example     gainToDb(0.0f)   -> -144.0f   (silence floor, was -80.0f)
/// @example     gainToDb(-1.0f)  -> -144.0f   (invalid -> floor)
///
[[nodiscard]] constexpr float gainToDb(float gain) noexcept;

} // namespace DSP
} // namespace Iterum

// ============================================================================
// Implementation Notes (for implementer reference)
// ============================================================================
//
// 1. Use std::pow and std::log10 from <cmath> (constexpr in C++20)
//
// 2. dbToGain implementation:
//    constexpr float dbToGain(float dB) noexcept {
//        // NaN check: NaN != NaN
//        if (dB != dB) return 0.0f;
//        return std::pow(10.0f, dB / 20.0f);
//    }
//
// 3. gainToDb implementation:
//    constexpr float gainToDb(float gain) noexcept {
//        // NaN or non-positive check
//        if (gain != gain || gain <= 0.0f) return kSilenceFloorDb;
//        float result = 20.0f * std::log10(gain);
//        return (result < kSilenceFloorDb) ? kSilenceFloorDb : result;
//    }
//
// 4. The NaN check (value != value) works in constexpr context unlike std::isnan
//
// ============================================================================
// Migration Checklist
// ============================================================================
//
// After implementing this contract:
//
// [ ] Create src/dsp/core/ directory
// [ ] Create src/dsp/core/db_utils.h with implementation
// [ ] Update src/dsp/dsp_utils.h to #include "core/db_utils.h"
// [ ] Remove old dBToLinear, linearToDb, kSilenceThreshold from dsp_utils.h
// [ ] Search for any VSTWork::DSP::dBToLinear usages and update
// [ ] Search for any VSTWork::DSP::linearToDb usages and update
// [ ] Create tests/unit/core/db_utils_test.cpp
// [ ] Verify build on all platforms
//
// ============================================================================
