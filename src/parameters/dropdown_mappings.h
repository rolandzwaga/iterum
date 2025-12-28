// ==============================================================================
// Parameter Dropdown Mappings - Type-Safe UI to DSP Conversion
// ==============================================================================
// Type-safe mapping functions from UI dropdown indices to DSP enum values.
// These mappings provide explicit, auditable conversion instead of fragile
// direct casts that assume enum values match dropdown indices.
//
// Why explicit mappings matter:
// - Enum values may not start at 0 or be contiguous
// - UI dropdown order may differ from logical enum order
// - Direct casts are silent failures if enum/dropdown desync
// - Explicit mappings are testable and self-documenting
//
// Architecture:
// - Enums are defined in their respective DSP feature headers
// - Mapping functions live here in the parameters layer
// - This creates clean separation: DSP layer has no UI knowledge
//
// Constitution Compliance:
// - Principle III: Modern C++ (constexpr, type-safe)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

// Include DSP headers to get enum types
#include "dsp/features/bbd_delay.h"        // BBDChipModel
#include "dsp/features/ping_pong_delay.h"  // LRRatio
#include "dsp/features/multi_tap_delay.h"  // TimingPattern, SpatialPattern

namespace Iterum::Parameters {

// =============================================================================
// Dropdown Count Constants
// =============================================================================

/// @brief Number of BBD Era options in dropdown (MN3005, MN3007, MN3205, SAD1024)
inline constexpr int kBBDEraDropdownCount = 4;

/// @brief Number of L/R Ratio options in dropdown
inline constexpr int kLRRatioDropdownCount = 7;

/// @brief Number of Timing Pattern options in dropdown
inline constexpr int kTimingPatternDropdownCount = 20;

/// @brief Number of Spatial Pattern options in dropdown
inline constexpr int kSpatialPatternDropdownCount = 7;

// =============================================================================
// BBD Era Dropdown Mapping
// =============================================================================

/// @brief Convert dropdown index to BBDChipModel enum
/// @param index Dropdown index (0-3)
/// @return BBDChipModel enum value, defaults to MN3005 for out-of-range
/// @note Lookup table approach for O(1) access and explicit mapping
inline constexpr DSP::BBDChipModel getBBDEraFromDropdown(int index) noexcept {
    using DSP::BBDChipModel;

    // Explicit lookup table - order matches UI dropdown
    constexpr BBDChipModel kLookup[] = {
        BBDChipModel::MN3005,   // Index 0: Matsushita 4096-stage (classic)
        BBDChipModel::MN3007,   // Index 1: Matsushita 1024-stage (short)
        BBDChipModel::MN3205,   // Index 2: Matsushita 4096-stage (later)
        BBDChipModel::SAD1024   // Index 3: Reticon 1024-stage (different character)
    };

    if (index < 0 || index >= kBBDEraDropdownCount) {
        return BBDChipModel::MN3005;  // Safe default
    }

    return kLookup[index];
}

// =============================================================================
// L/R Ratio Dropdown Mapping (PingPong)
// =============================================================================

/// @brief Convert dropdown index to LRRatio enum
/// @param index Dropdown index (0-6)
/// @return LRRatio enum value, defaults to OneToOne for out-of-range
inline constexpr DSP::LRRatio getLRRatioFromDropdown(int index) noexcept {
    using DSP::LRRatio;

    // Explicit lookup table - order matches UI dropdown
    constexpr LRRatio kLookup[] = {
        LRRatio::OneToOne,      // Index 0: 1:1
        LRRatio::TwoToOne,      // Index 1: 2:1
        LRRatio::ThreeToTwo,    // Index 2: 3:2
        LRRatio::FourToThree,   // Index 3: 4:3
        LRRatio::OneToTwo,      // Index 4: 1:2
        LRRatio::TwoToThree,    // Index 5: 2:3
        LRRatio::ThreeToFour    // Index 6: 3:4
    };

    if (index < 0 || index >= kLRRatioDropdownCount) {
        return LRRatio::OneToOne;  // Safe default
    }

    return kLookup[index];
}

// =============================================================================
// Timing Pattern Dropdown Mapping (MultiTap)
// =============================================================================

/// @brief Convert dropdown index to TimingPattern enum
/// @param index Dropdown index (0-19)
/// @return TimingPattern enum value, defaults to QuarterNote for out-of-range
inline constexpr DSP::TimingPattern getTimingPatternFromDropdown(int index) noexcept {
    using DSP::TimingPattern;

    // Explicit lookup table - order matches UI dropdown
    constexpr TimingPattern kLookup[] = {
        // Basic note values (0-5)
        TimingPattern::WholeNote,       // Index 0
        TimingPattern::HalfNote,        // Index 1
        TimingPattern::QuarterNote,     // Index 2
        TimingPattern::EighthNote,      // Index 3
        TimingPattern::SixteenthNote,   // Index 4
        TimingPattern::ThirtySecondNote,// Index 5

        // Dotted variants (6-9)
        TimingPattern::DottedHalf,      // Index 6
        TimingPattern::DottedQuarter,   // Index 7
        TimingPattern::DottedEighth,    // Index 8
        TimingPattern::DottedSixteenth, // Index 9

        // Triplet variants (10-13)
        TimingPattern::TripletHalf,     // Index 10
        TimingPattern::TripletQuarter,  // Index 11
        TimingPattern::TripletEighth,   // Index 12
        TimingPattern::TripletSixteenth,// Index 13

        // Mathematical patterns (14-18)
        TimingPattern::GoldenRatio,     // Index 14
        TimingPattern::Fibonacci,       // Index 15
        TimingPattern::Exponential,     // Index 16
        TimingPattern::PrimeNumbers,    // Index 17
        TimingPattern::LinearSpread,    // Index 18

        // Custom (19)
        TimingPattern::Custom           // Index 19
    };

    if (index < 0 || index >= kTimingPatternDropdownCount) {
        return TimingPattern::QuarterNote;  // Safe default (most common)
    }

    return kLookup[index];
}

// =============================================================================
// Spatial Pattern Dropdown Mapping (MultiTap)
// =============================================================================

/// @brief Convert dropdown index to SpatialPattern enum
/// @param index Dropdown index (0-6)
/// @return SpatialPattern enum value, defaults to Centered for out-of-range
inline constexpr DSP::SpatialPattern getSpatialPatternFromDropdown(int index) noexcept {
    using DSP::SpatialPattern;

    // Explicit lookup table - order matches UI dropdown
    constexpr SpatialPattern kLookup[] = {
        SpatialPattern::Cascade,        // Index 0: L->R sweep
        SpatialPattern::Alternating,    // Index 1: L-R-L-R ping-pong
        SpatialPattern::Centered,       // Index 2: All taps centered
        SpatialPattern::WideningStereo, // Index 3: Narrow->Wide spread
        SpatialPattern::DecayingLevel,  // Index 4: Decreasing levels
        SpatialPattern::FlatLevel,      // Index 5: Equal levels
        SpatialPattern::Custom          // Index 6: User-defined
    };

    if (index < 0 || index >= kSpatialPatternDropdownCount) {
        return SpatialPattern::Centered;  // Safe default (neutral)
    }

    return kLookup[index];
}

} // namespace Iterum::Parameters
