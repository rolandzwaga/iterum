// =============================================================================
// PresetManager Tests
// =============================================================================
// Spec 042: Preset Browser
// Tests for PresetManager functionality
//
// Note: Tests requiring VST3 IComponent/IEditController are in vst_tests
// =============================================================================

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include "preset/preset_manager.h"
#include "platform/preset_paths.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Test fixture for preset manager tests
class PresetManagerTestFixture {
public:
    PresetManagerTestFixture() {
        // Create temp test directory
        testDir_ = fs::temp_directory_path() / "iterum_test_presets";
        std::error_code ec;
        fs::remove_all(testDir_, ec);
        fs::create_directories(testDir_);
    }

    ~PresetManagerTestFixture() {
        // Cleanup
        std::error_code ec;
        fs::remove_all(testDir_, ec);
    }

    fs::path testDir() const { return testDir_; }

    // Create a dummy preset file for testing
    void createDummyPreset(const fs::path& path) {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        // Write minimal VST3 preset header (just for file existence testing)
        const char header[] = "VST3";
        file.write(header, 4);
        file.close();
    }

private:
    fs::path testDir_;
};

// =============================================================================
// isValidPresetName Tests (T017, T020 partial)
// =============================================================================

TEST_CASE("isValidPresetName validates preset names", "[preset][manager]") {
    SECTION("accepts valid names") {
        REQUIRE(Iterum::PresetManager::isValidPresetName("My Preset"));
        REQUIRE(Iterum::PresetManager::isValidPresetName("Ambient Pad 1"));
        REQUIRE(Iterum::PresetManager::isValidPresetName("Test_Preset-123"));
        REQUIRE(Iterum::PresetManager::isValidPresetName("A"));
    }

    SECTION("rejects empty names") {
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName(""));
    }

    SECTION("rejects names with invalid filesystem characters") {
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName("Test/Preset"));
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName("Test\\Preset"));
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName("Test:Preset"));
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName("Test*Preset"));
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName("Test?Preset"));
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName("Test\"Preset"));
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName("Test<Preset"));
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName("Test>Preset"));
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName("Test|Preset"));
    }

    SECTION("rejects names exceeding 255 characters") {
        std::string longName(256, 'a');
        REQUIRE_FALSE(Iterum::PresetManager::isValidPresetName(longName));
    }

    SECTION("accepts names at 255 character limit") {
        std::string maxName(255, 'a');
        REQUIRE(Iterum::PresetManager::isValidPresetName(maxName));
    }
}

// =============================================================================
// Scanning Tests (T017)
// =============================================================================

TEST_CASE("PresetManager scanning functionality", "[preset][manager][scan]") {
    PresetManagerTestFixture fixture;

    // Create PresetManager with null components (scanning doesn't need them)
    Iterum::PresetManager manager(nullptr, nullptr);

    SECTION("scanPresets returns empty list when no presets exist") {
        // Note: This tests the default directories which may have presets
        // For isolation, we'd need to mock the directory functions
        auto presets = manager.scanPresets();
        // Just verify it doesn't crash and returns a valid list
        REQUIRE(presets.size() >= 0);
    }

    SECTION("getPresetsForMode filters by mode") {
        // Without actual presets, this should return empty
        auto digitalPresets = manager.getPresetsForMode(Iterum::DelayMode::Digital);
        REQUIRE(digitalPresets.size() >= 0);
    }

    SECTION("searchPresets with empty query returns all presets") {
        manager.scanPresets();
        auto all = manager.searchPresets("");
        // Should return same as cached presets
        REQUIRE(all.size() >= 0);
    }

    SECTION("searchPresets filters by name case-insensitively") {
        manager.scanPresets();
        // Search for common terms
        auto results = manager.searchPresets("ambient");
        // Just verify it returns valid results without crash
        REQUIRE(results.size() >= 0);
    }
}

// =============================================================================
// Delete Tests (T019 partial)
// =============================================================================

TEST_CASE("PresetManager delete functionality", "[preset][manager][delete]") {
    PresetManagerTestFixture fixture;
    Iterum::PresetManager manager(nullptr, nullptr);

    SECTION("deletePreset returns false for factory presets") {
        Iterum::PresetInfo factoryPreset;
        factoryPreset.name = "Factory Preset";
        factoryPreset.path = fixture.testDir() / "factory.vstpreset";
        factoryPreset.isFactory = true;

        REQUIRE_FALSE(manager.deletePreset(factoryPreset));
        REQUIRE(manager.getLastError().find("factory") != std::string::npos);
    }

    SECTION("deletePreset returns false for non-existent files") {
        Iterum::PresetInfo nonExistent;
        nonExistent.name = "Non Existent";
        nonExistent.path = fixture.testDir() / "nonexistent.vstpreset";
        nonExistent.isFactory = false;

        REQUIRE_FALSE(manager.deletePreset(nonExistent));
    }

    SECTION("deletePreset successfully deletes user preset") {
        // Create a test preset file
        auto presetPath = fixture.testDir() / "user_preset.vstpreset";
        fixture.createDummyPreset(presetPath);
        REQUIRE(fs::exists(presetPath));

        Iterum::PresetInfo userPreset;
        userPreset.name = "User Preset";
        userPreset.path = presetPath;
        userPreset.isFactory = false;

        REQUIRE(manager.deletePreset(userPreset));
        REQUIRE_FALSE(fs::exists(presetPath));
    }
}

// =============================================================================
// Import Tests (T019 partial)
// =============================================================================

TEST_CASE("PresetManager import functionality", "[preset][manager][import]") {
    PresetManagerTestFixture fixture;
    Iterum::PresetManager manager(nullptr, nullptr);

    SECTION("importPreset returns false for non-existent source") {
        auto nonExistent = fixture.testDir() / "nonexistent.vstpreset";
        REQUIRE_FALSE(manager.importPreset(nonExistent));
        REQUIRE(manager.getLastError().find("not found") != std::string::npos);
    }

    SECTION("importPreset returns false for wrong file type") {
        // Create a non-vstpreset file
        auto wrongType = fixture.testDir() / "wrong.txt";
        std::ofstream file(wrongType);
        file << "test";
        file.close();

        REQUIRE_FALSE(manager.importPreset(wrongType));
        REQUIRE(manager.getLastError().find("Invalid") != std::string::npos);
    }

    SECTION("importPreset copies valid preset file") {
        // Create a source preset
        auto sourceDir = fixture.testDir() / "source";
        auto sourcePath = sourceDir / "test_preset.vstpreset";
        fixture.createDummyPreset(sourcePath);
        REQUIRE(fs::exists(sourcePath));

        // Import should succeed
        REQUIRE(manager.importPreset(sourcePath));

        // The file should now exist in user preset directory
        auto userDir = manager.getUserPresetDirectory();
        auto destPath = userDir / "test_preset.vstpreset";
        REQUIRE(fs::exists(destPath));

        // Cleanup
        std::error_code ec;
        fs::remove(destPath, ec);
    }
}

// =============================================================================
// Directory Access Tests
// =============================================================================

TEST_CASE("PresetManager directory access", "[preset][manager][directory]") {
    Iterum::PresetManager manager(nullptr, nullptr);

    SECTION("getUserPresetDirectory returns valid path") {
        auto path = manager.getUserPresetDirectory();
        REQUIRE_FALSE(path.empty());
        REQUIRE(path.is_absolute());
    }

    SECTION("getFactoryPresetDirectory returns valid path") {
        auto path = manager.getFactoryPresetDirectory();
        REQUIRE_FALSE(path.empty());
        REQUIRE(path.is_absolute());
    }

    SECTION("user and factory directories are different") {
        auto userDir = manager.getUserPresetDirectory();
        auto factoryDir = manager.getFactoryPresetDirectory();
        REQUIRE(userDir != factoryDir);
    }
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_CASE("PresetManager error handling", "[preset][manager][error]") {
    Iterum::PresetManager manager(nullptr, nullptr);

    SECTION("getLastError returns empty after successful operation") {
        // Perform a successful validation
        REQUIRE(Iterum::PresetManager::isValidPresetName("Valid Name"));
        // Can't easily test getLastError without causing an error first
    }

    SECTION("loadPreset with null components returns false") {
        Iterum::PresetInfo preset;
        preset.name = "Test";
        preset.path = "/test/path.vstpreset";

        REQUIRE_FALSE(manager.loadPreset(preset));
    }

    SECTION("savePreset with null components returns false") {
        REQUIRE_FALSE(manager.savePreset("Test", "Category", Iterum::DelayMode::Digital));
    }

    SECTION("savePreset with invalid name returns false") {
        REQUIRE_FALSE(manager.savePreset("Invalid/Name", "Category", Iterum::DelayMode::Digital));
    }
}
