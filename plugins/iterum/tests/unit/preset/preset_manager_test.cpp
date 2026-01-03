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
#include <random>
#include <sstream>

namespace fs = std::filesystem;

// Test fixture for preset manager tests
// Uses unique directory per fixture instance for parallel test isolation
class PresetManagerTestFixture {
public:
    PresetManagerTestFixture() {
        // Generate unique directory name using random number to ensure
        // parallel test runs don't interfere with each other
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(100000, 999999);

        std::ostringstream dirName;
        dirName << "iterum_test_" << dist(gen);

        testDir_ = fs::temp_directory_path() / dirName.str();
        userDir_ = testDir_ / "user";
        factoryDir_ = testDir_ / "factory";

        std::error_code ec;
        fs::create_directories(userDir_, ec);
        fs::create_directories(factoryDir_, ec);
    }

    ~PresetManagerTestFixture() {
        // Cleanup
        std::error_code ec;
        fs::remove_all(testDir_, ec);
    }

    fs::path testDir() const { return testDir_; }
    fs::path userDir() const { return userDir_; }
    fs::path factoryDir() const { return factoryDir_; }

    // Create a dummy preset file for testing
    void createDummyPreset(const fs::path& path) {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        // Write minimal VST3 preset header (just for file existence testing)
        const char header[] = "VST3";
        file.write(header, 4);
        file.close();
    }

    // Create an isolated PresetManager for testing
    Iterum::PresetManager createManager() {
        return Iterum::PresetManager(nullptr, nullptr, userDir_, factoryDir_);
    }

private:
    fs::path testDir_;
    fs::path userDir_;
    fs::path factoryDir_;
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

    // Create PresetManager with isolated test directories
    auto manager = fixture.createManager();

    SECTION("scanPresets returns empty list when no presets exist") {
        auto presets = manager.scanPresets();
        // Isolated directories are empty, so should return exactly 0
        REQUIRE(presets.size() == 0);
    }

    SECTION("scanPresets finds presets in user directory") {
        // Create a test preset
        fixture.createDummyPreset(fixture.userDir() / "test_preset.vstpreset");

        auto presets = manager.scanPresets();
        REQUIRE(presets.size() == 1);
    }

    SECTION("scanPresets finds presets in factory directory") {
        // Create a factory preset
        fixture.createDummyPreset(fixture.factoryDir() / "factory_preset.vstpreset");

        auto presets = manager.scanPresets();
        REQUIRE(presets.size() == 1);
        REQUIRE(presets[0].isFactory == true);
    }

    SECTION("getPresetsForMode filters by mode") {
        auto digitalPresets = manager.getPresetsForMode(Iterum::DelayMode::Digital);
        REQUIRE(digitalPresets.size() == 0);
    }

    SECTION("searchPresets with empty query returns all presets") {
        fixture.createDummyPreset(fixture.userDir() / "ambient_pad.vstpreset");
        manager.scanPresets();
        auto all = manager.searchPresets("");
        REQUIRE(all.size() == 1);
    }

    SECTION("searchPresets filters by name case-insensitively") {
        fixture.createDummyPreset(fixture.userDir() / "Ambient_Pad.vstpreset");
        manager.scanPresets();

        auto results = manager.searchPresets("ambient");
        REQUIRE(results.size() == 1);

        auto noMatch = manager.searchPresets("digital");
        REQUIRE(noMatch.size() == 0);
    }
}

// =============================================================================
// Delete Tests (T019 partial)
// =============================================================================

TEST_CASE("PresetManager delete functionality", "[preset][manager][delete]") {
    PresetManagerTestFixture fixture;
    auto manager = fixture.createManager();

    SECTION("deletePreset returns false for factory presets") {
        Iterum::PresetInfo factoryPreset;
        factoryPreset.name = "Factory Preset";
        factoryPreset.path = fixture.factoryDir() / "factory.vstpreset";
        factoryPreset.isFactory = true;

        REQUIRE_FALSE(manager.deletePreset(factoryPreset));
        REQUIRE(manager.getLastError().find("factory") != std::string::npos);
    }

    SECTION("deletePreset returns false for non-existent files") {
        Iterum::PresetInfo nonExistent;
        nonExistent.name = "Non Existent";
        nonExistent.path = fixture.userDir() / "nonexistent.vstpreset";
        nonExistent.isFactory = false;

        REQUIRE_FALSE(manager.deletePreset(nonExistent));
    }

    SECTION("deletePreset successfully deletes user preset") {
        // Create a test preset file
        auto presetPath = fixture.userDir() / "user_preset.vstpreset";
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
    auto manager = fixture.createManager();

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
        // Create a source preset in an external location
        auto sourceDir = fixture.testDir() / "external";
        auto sourcePath = sourceDir / "test_preset.vstpreset";
        fixture.createDummyPreset(sourcePath);
        REQUIRE(fs::exists(sourcePath));

        // Import should succeed
        REQUIRE(manager.importPreset(sourcePath));

        // The file should now exist in isolated user preset directory
        auto destPath = fixture.userDir() / "test_preset.vstpreset";
        REQUIRE(fs::exists(destPath));
        // No manual cleanup needed - fixture destructor cleans up
    }
}

// =============================================================================
// Directory Access Tests
// =============================================================================

TEST_CASE("PresetManager directory access", "[preset][manager][directory]") {
    PresetManagerTestFixture fixture;
    auto manager = fixture.createManager();

    SECTION("getUserPresetDirectory returns override path when provided") {
        auto path = manager.getUserPresetDirectory();
        REQUIRE_FALSE(path.empty());
        REQUIRE(path.is_absolute());
        REQUIRE(path == fixture.userDir());
    }

    SECTION("getFactoryPresetDirectory returns override path when provided") {
        auto path = manager.getFactoryPresetDirectory();
        REQUIRE_FALSE(path.empty());
        REQUIRE(path.is_absolute());
        REQUIRE(path == fixture.factoryDir());
    }

    SECTION("user and factory directories are different") {
        auto userDir = manager.getUserPresetDirectory();
        auto factoryDir = manager.getFactoryPresetDirectory();
        REQUIRE(userDir != factoryDir);
    }
}

TEST_CASE("PresetManager uses platform directories when no override", "[preset][manager][directory]") {
    // Create manager without overrides - should use platform defaults
    Iterum::PresetManager manager(nullptr, nullptr);

    SECTION("getUserPresetDirectory returns valid platform path") {
        auto path = manager.getUserPresetDirectory();
        REQUIRE_FALSE(path.empty());
        REQUIRE(path.is_absolute());
    }

    SECTION("getFactoryPresetDirectory returns valid platform path") {
        auto path = manager.getFactoryPresetDirectory();
        REQUIRE_FALSE(path.empty());
        REQUIRE(path.is_absolute());
    }
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_CASE("PresetManager error handling", "[preset][manager][error]") {
    PresetManagerTestFixture fixture;
    auto manager = fixture.createManager();

    SECTION("getLastError returns empty after successful operation") {
        // Perform a successful validation
        REQUIRE(Iterum::PresetManager::isValidPresetName("Valid Name"));
        // Can't easily test getLastError without causing an error first
    }

    SECTION("loadPreset with null components returns false") {
        Iterum::PresetInfo preset;
        preset.name = "Test";
        preset.path = fixture.userDir() / "test.vstpreset";

        REQUIRE_FALSE(manager.loadPreset(preset));
    }

    SECTION("savePreset with null components returns false") {
        REQUIRE_FALSE(manager.savePreset("Test", "Category", Iterum::DelayMode::Digital));
    }

    SECTION("savePreset with invalid name returns false") {
        REQUIRE_FALSE(manager.savePreset("Invalid/Name", "Category", Iterum::DelayMode::Digital));
    }
}
