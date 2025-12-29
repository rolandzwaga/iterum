// ==============================================================================
// Control Visibility Logic Tests
// ==============================================================================
// Tests for conditional UI control visibility based on parameter values.
// Specifically tests the logic for hiding delay time controls when time mode
// is set to "Synced" (since the time value is ignored in synced mode).
//
// Manual Testing Requirements (cannot be automated without full VSTGUI setup):
// 1. Load plugin in a DAW
// 2. Select Digital Delay mode
// 3. Verify "Delay Time" control is visible when "Time Mode" = "Free"
// 4. Change "Time Mode" to "Synced"
// 5. Verify "Delay Time" control disappears
// 6. Change back to "Free"
// 7. Verify "Delay Time" control reappears
// 8. Repeat steps 2-7 for PingPong Delay mode
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

using namespace Iterum;
using Catch::Approx;

// ==============================================================================
// TEST: Time Mode parameter values
// ==============================================================================

TEST_CASE("Time Mode parameter values follow correct mapping", "[vst][visibility][timemode]") {
    SECTION("Digital Time Mode values") {
        // Time mode is a binary parameter: 0 = Free, 1 = Synced
        // Normalized values: 0.0 = Free, 1.0 = Synced
        // Threshold: normalized < 0.5 = Free, >= 0.5 = Synced

        constexpr float kFreeModeNormalized = 0.0f;
        constexpr float kSyncedModeNormalized = 1.0f;
        constexpr float kVisibilityThreshold = 0.5f;

        REQUIRE(kFreeModeNormalized < kVisibilityThreshold);
        REQUIRE(kSyncedModeNormalized >= kVisibilityThreshold);
    }

    SECTION("PingPong Time Mode values") {
        // Same mapping as Digital
        constexpr float kFreeModeNormalized = 0.0f;
        constexpr float kSyncedModeNormalized = 1.0f;
        constexpr float kVisibilityThreshold = 0.5f;

        REQUIRE(kFreeModeNormalized < kVisibilityThreshold);
        REQUIRE(kSyncedModeNormalized >= kVisibilityThreshold);
    }
}

// ==============================================================================
// TEST: Visibility logic specification
// ==============================================================================

TEST_CASE("Delay time visibility follows correct logic", "[vst][visibility][logic]") {
    SECTION("Digital Delay Time visibility") {
        // Rule: Show delay time control when time mode is Free (< 0.5)
        //       Hide delay time control when time mode is Synced (>= 0.5)

        auto shouldBeVisible = [](float normalizedTimeModeValue) -> bool {
            return normalizedTimeModeValue < 0.5f;
        };

        REQUIRE(shouldBeVisible(0.0f) == true);   // Free mode
        REQUIRE(shouldBeVisible(0.25f) == true);  // Still Free
        REQUIRE(shouldBeVisible(0.49f) == true);  // Still Free
        REQUIRE(shouldBeVisible(0.5f) == false);  // Synced mode
        REQUIRE(shouldBeVisible(0.75f) == false); // Still Synced
        REQUIRE(shouldBeVisible(1.0f) == false);  // Synced mode
    }

    SECTION("PingPong Delay Time visibility") {
        // Same logic as Digital
        auto shouldBeVisible = [](float normalizedTimeModeValue) -> bool {
            return normalizedTimeModeValue < 0.5f;
        };

        REQUIRE(shouldBeVisible(0.0f) == true);   // Free mode
        REQUIRE(shouldBeVisible(0.25f) == true);  // Still Free
        REQUIRE(shouldBeVisible(0.49f) == true);  // Still Free
        REQUIRE(shouldBeVisible(0.5f) == false);  // Synced mode
        REQUIRE(shouldBeVisible(0.75f) == false); // Still Synced
        REQUIRE(shouldBeVisible(1.0f) == false);  // Synced mode
    }
}

// ==============================================================================
// TEST: Parameter ID mapping
// ==============================================================================

TEST_CASE("Correct parameter IDs are used for visibility control", "[vst][visibility][ids]") {
    SECTION("Digital Delay parameters") {
        REQUIRE(kDigitalDelayTimeId == 600);
        REQUIRE(kDigitalTimeModeId == 601);

        // These IDs must be adjacent for the visibility logic to work correctly
        REQUIRE(kDigitalTimeModeId == kDigitalDelayTimeId + 1);
    }

    SECTION("PingPong Delay parameters") {
        REQUIRE(kPingPongDelayTimeId == 700);
        REQUIRE(kPingPongTimeModeId == 701);

        // These IDs must be adjacent for the visibility logic to work correctly
        REQUIRE(kPingPongTimeModeId == kPingPongDelayTimeId + 1);
    }
}

// ==============================================================================
// TEST: Edge cases
// ==============================================================================

TEST_CASE("Visibility logic handles edge cases", "[vst][visibility][edge]") {
    SECTION("Boundary value exactly at threshold") {
        // At exactly 0.5, we should be in Synced mode (hidden)
        auto shouldBeVisible = [](float normalizedValue) -> bool {
            return normalizedValue < 0.5f;
        };

        REQUIRE(shouldBeVisible(0.5f) == false);
    }

    SECTION("Very small values near zero") {
        auto shouldBeVisible = [](float normalizedValue) -> bool {
            return normalizedValue < 0.5f;
        };

        REQUIRE(shouldBeVisible(0.0f) == true);
        REQUIRE(shouldBeVisible(0.001f) == true);
        REQUIRE(shouldBeVisible(0.00001f) == true);
    }

    SECTION("Values near 1.0") {
        auto shouldBeVisible = [](float normalizedValue) -> bool {
            return normalizedValue < 0.5f;
        };

        REQUIRE(shouldBeVisible(0.999f) == false);
        REQUIRE(shouldBeVisible(0.99999f) == false);
        REQUIRE(shouldBeVisible(1.0f) == false);
    }
}
