// ==============================================================================
// Mix/DryWet Scale Consistency Tests
// ==============================================================================
// This test verifies that ALL modes store their dryWet/mix parameters using
// the 0-1 scale internally, NOT 0-100.
//
// Rationale: The VST3 boundary uses normalized 0-1 values. Storing 0-1 internally
// makes the code simpler, reduces conversion errors, and ensures consistency.
//
// EXPECTED TO FAIL until modes are refactored to use 0-1 scale.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "parameters/spectral_params.h"
#include "parameters/shimmer_params.h"
#include "parameters/multitap_params.h"
#include "parameters/ducking_params.h"
#include "parameters/reverse_params.h"
#include "parameters/freeze_params.h"
#include "parameters/digital_params.h"
#include "parameters/granular_params.h"

using Catch::Approx;
using namespace Iterum;

// ==============================================================================
// PRINCIPLE: All mix/dryWet parameters should store 0-1 values internally
// When normalized value is 0.5, stored value should be 0.5 (not 50)
// ==============================================================================

TEST_CASE("All modes store dryWet as 0-1 scale", "[params][consistency][mix]") {

    SECTION("Digital delay stores mix as 0-1") {
        DigitalParams params;
        handleDigitalParamChange(params, kDigitalMixId, 0.5);
        // Expected: 0.5 (0-1 scale)
        REQUIRE(params.mix.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Granular delay stores dryWet as 0-1") {
        GranularParams params;
        handleGranularParamChange(params, kGranularMixId, 0.5);
        // Expected: 0.5 (0-1 scale)
        REQUIRE(params.dryWet.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Spectral delay stores dryWet as 0-1") {
        SpectralParams params;
        handleSpectralParamChange(params, kSpectralMixId, 0.5);
        // CURRENT: stores 50.0 (0-100 scale) - THIS SHOULD FAIL
        // EXPECTED: 0.5 (0-1 scale)
        REQUIRE(params.dryWet.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Shimmer delay stores dryWet as 0-1") {
        ShimmerParams params;
        handleShimmerParamChange(params, kShimmerMixId, 0.5);
        // CURRENT: stores 50.0 (0-100 scale) - THIS SHOULD FAIL
        // EXPECTED: 0.5 (0-1 scale)
        REQUIRE(params.dryWet.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Shimmer delay stores shimmerMix as 0-1") {
        ShimmerParams params;
        handleShimmerParamChange(params, kShimmerPitchBlendId, 0.5);
        // CURRENT: stores 50.0 (0-100 scale) - THIS SHOULD FAIL
        // EXPECTED: 0.5 (0-1 scale)
        REQUIRE(params.shimmerMix.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Shimmer delay stores diffusionAmount as 0-1") {
        ShimmerParams params;
        handleShimmerParamChange(params, kShimmerDiffusionAmountId, 0.5);
        // CURRENT: stores 50.0 (0-100 scale) - THIS SHOULD FAIL
        // EXPECTED: 0.5 (0-1 scale)
        REQUIRE(params.diffusionAmount.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("MultiTap delay stores dryWet as 0-1") {
        MultiTapParams params;
        handleMultiTapParamChange(params, kMultiTapMixId, 0.5);
        // CURRENT: stores 50.0 (0-100 scale) - THIS SHOULD FAIL
        // EXPECTED: 0.5 (0-1 scale)
        REQUIRE(params.dryWet.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Ducking delay stores dryWet as 0-1") {
        DuckingParams params;
        handleDuckingParamChange(params, kDuckingMixId, 0.5);
        // CURRENT: stores 50.0 (0-100 scale) - THIS SHOULD FAIL
        // EXPECTED: 0.5 (0-1 scale)
        REQUIRE(params.dryWet.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Ducking delay stores duckAmount as 0-1") {
        DuckingParams params;
        handleDuckingParamChange(params, kDuckingDuckAmountId, 0.5);
        // CURRENT: stores 50.0 (0-100 scale) - THIS SHOULD FAIL
        // EXPECTED: 0.5 (0-1 scale)
        REQUIRE(params.duckAmount.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Reverse delay stores dryWet as 0-1") {
        ReverseParams params;
        handleReverseParamChange(params, kReverseMixId, 0.5);
        // Reverse dryWet already uses 0-1 scale - should pass
        REQUIRE(params.dryWet.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Reverse delay stores crossfade as 0-1") {
        ReverseParams params;
        handleReverseParamChange(params, kReverseCrossfadeId, 0.5);
        // CURRENT: stores 50.0 (0-100 scale) - THIS SHOULD FAIL
        // EXPECTED: 0.5 (0-1 scale)
        REQUIRE(params.crossfade.load() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Freeze mode stores dryWet as 0-1") {
        FreezeParams params;
        handleFreezeParamChange(params, kFreezeMixId, 0.5);
        // Freeze dryWet already uses 0-1 scale - should pass
        REQUIRE(params.dryWet.load() == Approx(0.5f).margin(0.01f));
    }
}

// ==============================================================================
// Additional boundary value tests (0.0 and 1.0)
// ==============================================================================

TEST_CASE("Mix parameters handle boundary values correctly", "[params][consistency][mix]") {

    SECTION("Spectral: normalized 0.0 -> stored 0.0") {
        SpectralParams params;
        handleSpectralParamChange(params, kSpectralMixId, 0.0);
        REQUIRE(params.dryWet.load() == Approx(0.0f).margin(0.001f));
    }

    SECTION("Spectral: normalized 1.0 -> stored 1.0") {
        SpectralParams params;
        handleSpectralParamChange(params, kSpectralMixId, 1.0);
        REQUIRE(params.dryWet.load() == Approx(1.0f).margin(0.001f));
    }

    SECTION("Shimmer dryWet: normalized 0.0 -> stored 0.0") {
        ShimmerParams params;
        handleShimmerParamChange(params, kShimmerMixId, 0.0);
        REQUIRE(params.dryWet.load() == Approx(0.0f).margin(0.001f));
    }

    SECTION("Shimmer dryWet: normalized 1.0 -> stored 1.0") {
        ShimmerParams params;
        handleShimmerParamChange(params, kShimmerMixId, 1.0);
        REQUIRE(params.dryWet.load() == Approx(1.0f).margin(0.001f));
    }

    SECTION("MultiTap: normalized 0.0 -> stored 0.0") {
        MultiTapParams params;
        handleMultiTapParamChange(params, kMultiTapMixId, 0.0);
        REQUIRE(params.dryWet.load() == Approx(0.0f).margin(0.001f));
    }

    SECTION("MultiTap: normalized 1.0 -> stored 1.0") {
        MultiTapParams params;
        handleMultiTapParamChange(params, kMultiTapMixId, 1.0);
        REQUIRE(params.dryWet.load() == Approx(1.0f).margin(0.001f));
    }

    SECTION("Ducking dryWet: normalized 0.0 -> stored 0.0") {
        DuckingParams params;
        handleDuckingParamChange(params, kDuckingMixId, 0.0);
        REQUIRE(params.dryWet.load() == Approx(0.0f).margin(0.001f));
    }

    SECTION("Ducking dryWet: normalized 1.0 -> stored 1.0") {
        DuckingParams params;
        handleDuckingParamChange(params, kDuckingMixId, 1.0);
        REQUIRE(params.dryWet.load() == Approx(1.0f).margin(0.001f));
    }

    SECTION("Reverse crossfade: normalized 0.0 -> stored 0.0") {
        ReverseParams params;
        handleReverseParamChange(params, kReverseCrossfadeId, 0.0);
        REQUIRE(params.crossfade.load() == Approx(0.0f).margin(0.001f));
    }

    SECTION("Reverse crossfade: normalized 1.0 -> stored 1.0") {
        ReverseParams params;
        handleReverseParamChange(params, kReverseCrossfadeId, 1.0);
        REQUIRE(params.crossfade.load() == Approx(1.0f).margin(0.001f));
    }
}
