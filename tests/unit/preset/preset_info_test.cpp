// =============================================================================
// PresetInfo Tests
// =============================================================================
// Spec 042: Preset Browser
// Tests for PresetInfo struct metadata handling
// =============================================================================

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include "preset/preset_info.h"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("PresetInfo default construction", "[preset][info]") {
    Iterum::PresetInfo info;

    SECTION("has empty name by default") {
        REQUIRE(info.name.empty());
    }

    SECTION("has empty category by default") {
        REQUIRE(info.category.empty());
    }

    SECTION("defaults to Digital mode") {
        REQUIRE(info.mode == Iterum::DelayMode::Digital);
    }

    SECTION("has empty path by default") {
        REQUIRE(info.path.empty());
    }

    SECTION("is not factory preset by default") {
        REQUIRE_FALSE(info.isFactory);
    }

    SECTION("has empty description by default") {
        REQUIRE(info.description.empty());
    }

    SECTION("has empty author by default") {
        REQUIRE(info.author.empty());
    }
}

TEST_CASE("PresetInfo::isValid() checks name and path", "[preset][info]") {
    Iterum::PresetInfo info;

    SECTION("empty info is not valid") {
        REQUIRE_FALSE(info.isValid());
    }

    SECTION("name only is not valid") {
        info.name = "Test Preset";
        REQUIRE_FALSE(info.isValid());
    }

    SECTION("path only is not valid") {
        info.path = "/path/to/preset.vstpreset";
        REQUIRE_FALSE(info.isValid());
    }

    SECTION("name and path together is valid") {
        info.name = "Test Preset";
        info.path = "/path/to/preset.vstpreset";
        REQUIRE(info.isValid());
    }
}

TEST_CASE("PresetInfo comparison operator", "[preset][info]") {
    Iterum::PresetInfo a, b;

    SECTION("compares by name alphabetically") {
        a.name = "Alpha";
        b.name = "Beta";
        REQUIRE(a < b);
        REQUIRE_FALSE(b < a);
    }

    SECTION("equal names are not less than each other") {
        a.name = "Same";
        b.name = "Same";
        REQUIRE_FALSE(a < b);
        REQUIRE_FALSE(b < a);
    }

    SECTION("comparison is case-sensitive") {
        a.name = "alpha";
        b.name = "Beta";
        // lowercase 'a' > uppercase 'B' in ASCII
        REQUIRE_FALSE(a < b);
    }
}

TEST_CASE("PresetInfo can store all metadata fields", "[preset][info]") {
    Iterum::PresetInfo info;
    info.name = "Ambient Pad";
    info.category = "Ambient";
    info.mode = Iterum::DelayMode::Shimmer;
    info.path = fs::path("/presets/Shimmer/Ambient Pad.vstpreset");
    info.isFactory = true;
    info.description = "A lush ambient shimmer pad";
    info.author = "Krate Audio";

    REQUIRE(info.name == "Ambient Pad");
    REQUIRE(info.category == "Ambient");
    REQUIRE(info.mode == Iterum::DelayMode::Shimmer);
    REQUIRE(info.path.string().find("Ambient Pad.vstpreset") != std::string::npos);
    REQUIRE(info.isFactory == true);
    REQUIRE(info.description == "A lush ambient shimmer pad");
    REQUIRE(info.author == "Krate Audio");
    REQUIRE(info.isValid());
}

TEST_CASE("PresetInfo supports all delay modes", "[preset][info]") {
    using Iterum::DelayMode;

    std::vector<DelayMode> allModes = {
        DelayMode::Granular,
        DelayMode::Spectral,
        DelayMode::Shimmer,
        DelayMode::Tape,
        DelayMode::BBD,
        DelayMode::Digital,
        DelayMode::PingPong,
        DelayMode::Reverse,
        DelayMode::MultiTap,
        DelayMode::Freeze,
        DelayMode::Ducking
    };

    for (auto mode : allModes) {
        Iterum::PresetInfo info;
        info.name = "Test";
        info.path = "/test.vstpreset";
        info.mode = mode;

        REQUIRE(info.mode == mode);
        REQUIRE(info.isValid());
    }
}
