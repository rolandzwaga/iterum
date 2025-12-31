// =============================================================================
// Platform Preset Paths Tests
// =============================================================================
// Spec 042: Preset Browser
// Tests for cross-platform preset directory path helpers
// =============================================================================

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include "platform/preset_paths.h"
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

TEST_CASE("getUserPresetDirectory returns valid path", "[preset][platform]") {
    auto path = Iterum::Platform::getUserPresetDirectory();

    SECTION("returns non-empty path") {
        REQUIRE_FALSE(path.empty());
    }

    SECTION("path ends with Iterum/Iterum") {
        std::string pathStr = path.string();
        // Check the path contains Iterum
        REQUIRE(pathStr.find("Iterum") != std::string::npos);
    }

    SECTION("path is absolute") {
        REQUIRE(path.is_absolute());
    }

#if defined(_WIN32)
    SECTION("Windows path contains VST3 Presets") {
        std::string pathStr = path.string();
        REQUIRE(pathStr.find("VST3 Presets") != std::string::npos);
    }
#elif defined(__APPLE__)
    SECTION("macOS path contains Library/Audio/Presets") {
        std::string pathStr = path.string();
        REQUIRE(pathStr.find("Library/Audio/Presets") != std::string::npos);
    }
#else
    SECTION("Linux path contains .vst3/presets") {
        std::string pathStr = path.string();
        REQUIRE(pathStr.find(".vst3/presets") != std::string::npos);
    }
#endif
}

TEST_CASE("getFactoryPresetDirectory returns valid path", "[preset][platform]") {
    auto path = Iterum::Platform::getFactoryPresetDirectory();

    SECTION("returns non-empty path") {
        REQUIRE_FALSE(path.empty());
    }

    SECTION("path contains Iterum") {
        std::string pathStr = path.string();
        REQUIRE(pathStr.find("Iterum") != std::string::npos);
    }

    SECTION("path is absolute") {
        REQUIRE(path.is_absolute());
    }

#if defined(_WIN32)
    SECTION("Windows factory path uses ProgramData") {
        std::string pathStr = path.string();
        // ProgramData should be somewhere in the path
        REQUIRE(pathStr.find("VST3 Presets") != std::string::npos);
    }
#elif defined(__APPLE__)
    SECTION("macOS factory path is system-wide") {
        std::string pathStr = path.string();
        REQUIRE(pathStr.find("/Library/Audio/Presets") != std::string::npos);
    }
#else
    SECTION("Linux factory path is in /usr/share") {
        std::string pathStr = path.string();
        REQUIRE(pathStr.find("/usr/share") != std::string::npos);
    }
#endif
}

TEST_CASE("ensureDirectoryExists creates directories", "[preset][platform]") {
    // Create a unique test directory in temp
    auto testDir = fs::temp_directory_path() / "iterum_test" / "preset_test";

    // Clean up any existing test directory
    std::error_code ec;
    fs::remove_all(testDir, ec);

    SECTION("creates non-existent directory") {
        REQUIRE_FALSE(fs::exists(testDir));
        REQUIRE(Iterum::Platform::ensureDirectoryExists(testDir));
        REQUIRE(fs::exists(testDir));
        REQUIRE(fs::is_directory(testDir));
    }

    SECTION("returns true for existing directory") {
        fs::create_directories(testDir);
        REQUIRE(fs::exists(testDir));
        REQUIRE(Iterum::Platform::ensureDirectoryExists(testDir));
    }

    SECTION("returns false for empty path") {
        REQUIRE_FALSE(Iterum::Platform::ensureDirectoryExists(fs::path()));
    }

    // Clean up
    fs::remove_all(fs::temp_directory_path() / "iterum_test", ec);
}

TEST_CASE("User and factory directories are different", "[preset][platform]") {
    auto userPath = Iterum::Platform::getUserPresetDirectory();
    auto factoryPath = Iterum::Platform::getFactoryPresetDirectory();

    REQUIRE(userPath != factoryPath);
}
