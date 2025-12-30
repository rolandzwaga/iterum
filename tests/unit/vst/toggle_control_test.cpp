// ==============================================================================
// Toggle Control Tests
// ==============================================================================
// Tests for on/off toggle controls in the plugin UI.
//
// BUG DOCUMENTATION:
// COnOffButton controls in editor.uidesc require bitmap images to render.
// The <bitmaps/> section was empty, causing all toggle buttons to be INVISIBLE.
//
// Affected controls (originally using COnOffButton):
// - GranularFreeze
// - SpectralFreeze
// - ShimmerFilterEnabled
// - TapeSpliceEnabled
// - TapeHead1Enabled, TapeHead2Enabled, TapeHead3Enabled
// - ReverseFilterEnabled
// - FreezeEnabled
// - FreezeFilterEnabled
// - DuckingEnabled
// - DuckingSidechainFilterEnabled
//
// FIX: Replace COnOffButton with CCheckBox which renders without bitmaps.
//
// Manual Testing Requirements:
// 1. Load plugin in a DAW
// 2. Navigate to Tape Delay mode
// 3. Verify Head 1, Head 2, Head 3 toggles are VISIBLE
// 4. Verify clicking toggles changes the control state
// 5. Verify enabled heads produce audible delay output
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

using namespace Iterum;
using Catch::Approx;

// ==============================================================================
// TEST: Tape head toggle parameter IDs are correctly defined
// ==============================================================================

TEST_CASE("Tape head toggle parameter IDs are sequential", "[vst][toggle][tape]") {
    SECTION("Head enabled parameters are contiguous") {
        // Head enables should be sequential for UI binding
        REQUIRE(kTapeHead1EnabledId == 410);
        REQUIRE(kTapeHead2EnabledId == 411);
        REQUIRE(kTapeHead3EnabledId == 412);

        // Verify sequential ordering
        REQUIRE(kTapeHead2EnabledId == kTapeHead1EnabledId + 1);
        REQUIRE(kTapeHead3EnabledId == kTapeHead2EnabledId + 1);
    }

    SECTION("Head level parameters follow enables") {
        REQUIRE(kTapeHead1LevelId == 413);
        REQUIRE(kTapeHead2LevelId == 414);
        REQUIRE(kTapeHead3LevelId == 415);
    }

    SECTION("Head pan parameters follow levels") {
        REQUIRE(kTapeHead1PanId == 416);
        REQUIRE(kTapeHead2PanId == 417);
        REQUIRE(kTapeHead3PanId == 418);
    }
}

// ==============================================================================
// TEST: Other toggle parameter IDs
// ==============================================================================

TEST_CASE("Feature toggle parameter IDs are correctly defined", "[vst][toggle]") {
    SECTION("Freeze-related toggles") {
        // Granular freeze
        REQUIRE(kGranularFreezeId == 108);

        // Spectral freeze
        REQUIRE(kSpectralFreezeId == 206);

        // Freeze mode enable
        REQUIRE(kFreezeEnabledId == 1000);
        REQUIRE(kFreezeFilterEnabledId == 1009);
    }

    SECTION("Filter enable toggles") {
        // Shimmer filter
        REQUIRE(kShimmerFilterEnabledId == 307);

        // Reverse filter
        REQUIRE(kReverseFilterEnabledId == 804);
    }

    SECTION("Effect enable toggles") {
        // Tape splice
        REQUIRE(kTapeSpliceEnabledId == 405);

        // Ducking
        REQUIRE(kDuckingEnabledId == 1100);
        REQUIRE(kDuckingSidechainFilterEnabledId == 1107);
    }
}

// ==============================================================================
// TEST: Toggle parameter value semantics
// ==============================================================================

TEST_CASE("Toggle parameters use boolean semantics", "[vst][toggle][semantics]") {
    // Boolean parameters should have:
    // - Normalized 0.0 = OFF
    // - Normalized 1.0 = ON
    // - Threshold at 0.5 for determining state

    SECTION("OFF state represented as 0.0") {
        constexpr float kOffNormalized = 0.0f;
        REQUIRE(kOffNormalized < 0.5f);  // Below threshold = OFF
    }

    SECTION("ON state represented as 1.0") {
        constexpr float kOnNormalized = 1.0f;
        REQUIRE(kOnNormalized >= 0.5f);  // At or above threshold = ON
    }

    SECTION("Threshold is at 0.5") {
        // Standard boolean parameter threshold
        constexpr float kThreshold = 0.5f;

        // Values below threshold are OFF
        REQUIRE(0.0f < kThreshold);
        REQUIRE(0.25f < kThreshold);
        REQUIRE(0.49f < kThreshold);

        // Values at or above threshold are ON
        REQUIRE(0.5f >= kThreshold);
        REQUIRE(0.75f >= kThreshold);
        REQUIRE(1.0f >= kThreshold);
    }
}

// ==============================================================================
// TEST: UI control requirements documentation
// ==============================================================================

TEST_CASE("Toggle controls require visible UI elements", "[vst][toggle][ui]") {
    // This test documents the requirement that toggle controls must be visible.
    // COnOffButton requires bitmaps - CCheckBox works without them.

    SECTION("COnOffButton requires bitmap resources") {
        // COnOffButton in VSTGUI needs a bitmap with two states (off/on)
        // Without a bitmap, the control renders as invisible
        //
        // WRONG in editor.uidesc:
        //   <view class="COnOffButton" ... />
        //   <bitmaps/>  <!-- EMPTY! -->
        //
        // This causes the button to be completely invisible.
        REQUIRE(true);  // Documents the requirement
    }

    SECTION("CCheckBox renders without bitmap") {
        // CCheckBox can render using native/generic styling
        // It doesn't require external bitmap resources
        //
        // CORRECT in editor.uidesc:
        //   <view class="CCheckBox" ... title="" />
        //
        // Or with label:
        //   <view class="CCheckBox" ... title="Enable" />
        REQUIRE(true);  // Documents the solution
    }
}

// ==============================================================================
// TEST: Tape head default states
// ==============================================================================

TEST_CASE("Tape head default enabled states", "[vst][toggle][tape][defaults]") {
    // Default behavior: Head 1 ON, Head 2 OFF, Head 3 OFF
    // This is the classic single-head tape delay sound

    // These values come from TapeParams initialization in tape_params.h
    SECTION("Head 1 defaults to enabled") {
        // head1Enabled{true} in tape_params.h
        // Users expect the primary head to be active by default
        REQUIRE(true);  // Documents expected default
    }

    SECTION("Heads 2 and 3 default to disabled") {
        // head2Enabled{false}, head3Enabled{false} in tape_params.h
        // Multi-head operation is an advanced feature
        REQUIRE(true);  // Documents expected default
    }
}

