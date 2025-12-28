// ==============================================================================
// Dropdown Mapping Unit Tests
// ==============================================================================
// Tests for the dropdown index to enum mapping functions.
// These mappings provide type-safe, explicit conversion from UI dropdown
// indices to DSP enum values, avoiding fragile direct casts.
//
// Constitution Compliance:
// - Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "parameters/dropdown_mappings.h"

using namespace Iterum::DSP;         // For enum types (BBDChipModel, LRRatio, etc.)
using namespace Iterum::Parameters;  // For mapping functions (getBBDEraFromDropdown, etc.)

// =============================================================================
// BBDChipModel Dropdown Mapping Tests
// =============================================================================

TEST_CASE("BBD Era dropdown mapping produces correct values", "[dsp][core][bbd]") {
    SECTION("Index 0 maps to MN3005") {
        REQUIRE(getBBDEraFromDropdown(0) == BBDChipModel::MN3005);
    }

    SECTION("Index 1 maps to MN3007") {
        REQUIRE(getBBDEraFromDropdown(1) == BBDChipModel::MN3007);
    }

    SECTION("Index 2 maps to MN3205") {
        REQUIRE(getBBDEraFromDropdown(2) == BBDChipModel::MN3205);
    }

    SECTION("Index 3 maps to SAD1024") {
        REQUIRE(getBBDEraFromDropdown(3) == BBDChipModel::SAD1024);
    }
}

TEST_CASE("BBD Era dropdown mapping handles out of range", "[dsp][core][bbd]") {
    SECTION("Negative index defaults to MN3005") {
        REQUIRE(getBBDEraFromDropdown(-1) == BBDChipModel::MN3005);
    }

    SECTION("Index > 3 defaults to MN3005") {
        REQUIRE(getBBDEraFromDropdown(4) == BBDChipModel::MN3005);
        REQUIRE(getBBDEraFromDropdown(100) == BBDChipModel::MN3005);
    }
}

TEST_CASE("BBD Era dropdown count is correct", "[dsp][core][bbd]") {
    REQUIRE(kBBDEraDropdownCount == 4);
}

// =============================================================================
// LRRatio Dropdown Mapping Tests
// =============================================================================

TEST_CASE("LR Ratio dropdown mapping produces correct values", "[dsp][core][pingpong]") {
    SECTION("Index 0 maps to OneToOne (1:1)") {
        REQUIRE(getLRRatioFromDropdown(0) == LRRatio::OneToOne);
    }

    SECTION("Index 1 maps to TwoToOne (2:1)") {
        REQUIRE(getLRRatioFromDropdown(1) == LRRatio::TwoToOne);
    }

    SECTION("Index 2 maps to ThreeToTwo (3:2)") {
        REQUIRE(getLRRatioFromDropdown(2) == LRRatio::ThreeToTwo);
    }

    SECTION("Index 3 maps to FourToThree (4:3)") {
        REQUIRE(getLRRatioFromDropdown(3) == LRRatio::FourToThree);
    }

    SECTION("Index 4 maps to OneToTwo (1:2)") {
        REQUIRE(getLRRatioFromDropdown(4) == LRRatio::OneToTwo);
    }

    SECTION("Index 5 maps to TwoToThree (2:3)") {
        REQUIRE(getLRRatioFromDropdown(5) == LRRatio::TwoToThree);
    }

    SECTION("Index 6 maps to ThreeToFour (3:4)") {
        REQUIRE(getLRRatioFromDropdown(6) == LRRatio::ThreeToFour);
    }
}

TEST_CASE("LR Ratio dropdown mapping handles out of range", "[dsp][core][pingpong]") {
    SECTION("Negative index defaults to OneToOne") {
        REQUIRE(getLRRatioFromDropdown(-1) == LRRatio::OneToOne);
    }

    SECTION("Index > 6 defaults to OneToOne") {
        REQUIRE(getLRRatioFromDropdown(7) == LRRatio::OneToOne);
        REQUIRE(getLRRatioFromDropdown(100) == LRRatio::OneToOne);
    }
}

TEST_CASE("LR Ratio dropdown count is correct", "[dsp][core][pingpong]") {
    REQUIRE(kLRRatioDropdownCount == 7);
}

// =============================================================================
// TimingPattern Dropdown Mapping Tests
// =============================================================================

TEST_CASE("Timing Pattern dropdown mapping produces correct values", "[dsp][core][multitap]") {
    // Rhythmic patterns - basic note values
    SECTION("Index 0 maps to WholeNote") {
        REQUIRE(getTimingPatternFromDropdown(0) == TimingPattern::WholeNote);
    }

    SECTION("Index 1 maps to HalfNote") {
        REQUIRE(getTimingPatternFromDropdown(1) == TimingPattern::HalfNote);
    }

    SECTION("Index 2 maps to QuarterNote") {
        REQUIRE(getTimingPatternFromDropdown(2) == TimingPattern::QuarterNote);
    }

    SECTION("Index 3 maps to EighthNote") {
        REQUIRE(getTimingPatternFromDropdown(3) == TimingPattern::EighthNote);
    }

    SECTION("Index 4 maps to SixteenthNote") {
        REQUIRE(getTimingPatternFromDropdown(4) == TimingPattern::SixteenthNote);
    }

    SECTION("Index 5 maps to ThirtySecondNote") {
        REQUIRE(getTimingPatternFromDropdown(5) == TimingPattern::ThirtySecondNote);
    }

    // Rhythmic patterns - dotted variants
    SECTION("Index 6 maps to DottedHalf") {
        REQUIRE(getTimingPatternFromDropdown(6) == TimingPattern::DottedHalf);
    }

    SECTION("Index 7 maps to DottedQuarter") {
        REQUIRE(getTimingPatternFromDropdown(7) == TimingPattern::DottedQuarter);
    }

    SECTION("Index 8 maps to DottedEighth") {
        REQUIRE(getTimingPatternFromDropdown(8) == TimingPattern::DottedEighth);
    }

    SECTION("Index 9 maps to DottedSixteenth") {
        REQUIRE(getTimingPatternFromDropdown(9) == TimingPattern::DottedSixteenth);
    }

    // Rhythmic patterns - triplet variants
    SECTION("Index 10 maps to TripletHalf") {
        REQUIRE(getTimingPatternFromDropdown(10) == TimingPattern::TripletHalf);
    }

    SECTION("Index 11 maps to TripletQuarter") {
        REQUIRE(getTimingPatternFromDropdown(11) == TimingPattern::TripletQuarter);
    }

    SECTION("Index 12 maps to TripletEighth") {
        REQUIRE(getTimingPatternFromDropdown(12) == TimingPattern::TripletEighth);
    }

    SECTION("Index 13 maps to TripletSixteenth") {
        REQUIRE(getTimingPatternFromDropdown(13) == TimingPattern::TripletSixteenth);
    }

    // Mathematical patterns
    SECTION("Index 14 maps to GoldenRatio") {
        REQUIRE(getTimingPatternFromDropdown(14) == TimingPattern::GoldenRatio);
    }

    SECTION("Index 15 maps to Fibonacci") {
        REQUIRE(getTimingPatternFromDropdown(15) == TimingPattern::Fibonacci);
    }

    SECTION("Index 16 maps to Exponential") {
        REQUIRE(getTimingPatternFromDropdown(16) == TimingPattern::Exponential);
    }

    SECTION("Index 17 maps to PrimeNumbers") {
        REQUIRE(getTimingPatternFromDropdown(17) == TimingPattern::PrimeNumbers);
    }

    SECTION("Index 18 maps to LinearSpread") {
        REQUIRE(getTimingPatternFromDropdown(18) == TimingPattern::LinearSpread);
    }

    // Custom pattern
    SECTION("Index 19 maps to Custom") {
        REQUIRE(getTimingPatternFromDropdown(19) == TimingPattern::Custom);
    }
}

TEST_CASE("Timing Pattern dropdown mapping handles out of range", "[dsp][core][multitap]") {
    SECTION("Negative index defaults to QuarterNote") {
        REQUIRE(getTimingPatternFromDropdown(-1) == TimingPattern::QuarterNote);
    }

    SECTION("Index > 19 defaults to QuarterNote") {
        REQUIRE(getTimingPatternFromDropdown(20) == TimingPattern::QuarterNote);
        REQUIRE(getTimingPatternFromDropdown(100) == TimingPattern::QuarterNote);
    }
}

TEST_CASE("Timing Pattern dropdown count is correct", "[dsp][core][multitap]") {
    REQUIRE(kTimingPatternDropdownCount == 20);
}

// =============================================================================
// SpatialPattern Dropdown Mapping Tests
// =============================================================================

TEST_CASE("Spatial Pattern dropdown mapping produces correct values", "[dsp][core][multitap]") {
    SECTION("Index 0 maps to Cascade") {
        REQUIRE(getSpatialPatternFromDropdown(0) == SpatialPattern::Cascade);
    }

    SECTION("Index 1 maps to Alternating") {
        REQUIRE(getSpatialPatternFromDropdown(1) == SpatialPattern::Alternating);
    }

    SECTION("Index 2 maps to Centered") {
        REQUIRE(getSpatialPatternFromDropdown(2) == SpatialPattern::Centered);
    }

    SECTION("Index 3 maps to WideningStereo") {
        REQUIRE(getSpatialPatternFromDropdown(3) == SpatialPattern::WideningStereo);
    }

    SECTION("Index 4 maps to DecayingLevel") {
        REQUIRE(getSpatialPatternFromDropdown(4) == SpatialPattern::DecayingLevel);
    }

    SECTION("Index 5 maps to FlatLevel") {
        REQUIRE(getSpatialPatternFromDropdown(5) == SpatialPattern::FlatLevel);
    }

    SECTION("Index 6 maps to Custom") {
        REQUIRE(getSpatialPatternFromDropdown(6) == SpatialPattern::Custom);
    }
}

TEST_CASE("Spatial Pattern dropdown mapping handles out of range", "[dsp][core][multitap]") {
    SECTION("Negative index defaults to Centered") {
        REQUIRE(getSpatialPatternFromDropdown(-1) == SpatialPattern::Centered);
    }

    SECTION("Index > 6 defaults to Centered") {
        REQUIRE(getSpatialPatternFromDropdown(7) == SpatialPattern::Centered);
        REQUIRE(getSpatialPatternFromDropdown(100) == SpatialPattern::Centered);
    }
}

TEST_CASE("Spatial Pattern dropdown count is correct", "[dsp][core][multitap]") {
    REQUIRE(kSpatialPatternDropdownCount == 7);
}

// =============================================================================
// Comprehensive Coverage Test
// =============================================================================

TEST_CASE("All dropdown mappings are exhaustive and consistent", "[dsp][core]") {
    SECTION("BBD Era: All valid indices return valid enum values") {
        for (int i = 0; i < kBBDEraDropdownCount; ++i) {
            auto era = getBBDEraFromDropdown(i);
            REQUIRE(static_cast<int>(era) >= 0);
            REQUIRE(static_cast<int>(era) <= 3);
        }
    }

    SECTION("LR Ratio: All valid indices return valid enum values") {
        for (int i = 0; i < kLRRatioDropdownCount; ++i) {
            auto ratio = getLRRatioFromDropdown(i);
            REQUIRE(static_cast<int>(ratio) >= 0);
            REQUIRE(static_cast<int>(ratio) <= 6);
        }
    }

    SECTION("Timing Pattern: All valid indices return valid enum values") {
        for (int i = 0; i < kTimingPatternDropdownCount; ++i) {
            auto pattern = getTimingPatternFromDropdown(i);
            REQUIRE(static_cast<int>(pattern) >= 0);
            REQUIRE(static_cast<int>(pattern) <= 19);
        }
    }

    SECTION("Spatial Pattern: All valid indices return valid enum values") {
        for (int i = 0; i < kSpatialPatternDropdownCount; ++i) {
            auto pattern = getSpatialPatternFromDropdown(i);
            REQUIRE(static_cast<int>(pattern) >= 0);
            REQUIRE(static_cast<int>(pattern) <= 6);
        }
    }
}
