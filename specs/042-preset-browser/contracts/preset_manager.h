// Contract: PresetManager Interface
// This is a design contract, not actual implementation code.

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include "../preset_info.h"

namespace Steinberg::Vst {
    class IComponent;
    class IEditController;
}

namespace Iterum {

/**
 * PresetManager handles all preset file operations.
 *
 * Thread Safety: All methods must be called from UI thread only.
 *
 * Constitution Compliance:
 * - Principle II: No audio thread involvement
 * - Principle VI: Cross-platform via std::filesystem
 */
class PresetManager {
public:
    using PresetList = std::vector<PresetInfo>;

    /**
     * Constructor.
     * @param processor VST3 processor component for state access
     * @param controller VST3 edit controller for state sync
     */
    explicit PresetManager(
        Steinberg::Vst::IComponent* processor,
        Steinberg::Vst::IEditController* controller
    );

    ~PresetManager();

    // ========================================================================
    // Scanning
    // ========================================================================

    /**
     * Scan all preset directories and return combined list.
     * Scans both user and factory directories.
     * @return List of all discovered presets
     */
    PresetList scanPresets();

    /**
     * Get presets filtered by mode.
     * Must call scanPresets() first.
     * @param mode Target delay mode
     * @return Filtered preset list
     */
    PresetList getPresetsForMode(DelayMode mode) const;

    /**
     * Search presets by name (case-insensitive).
     * @param query Search string
     * @return Matching presets
     */
    PresetList searchPresets(std::string_view query) const;

    // ========================================================================
    // Load/Save
    // ========================================================================

    /**
     * Load a preset, restoring all parameters.
     * @param preset Preset to load
     * @return true on success
     */
    bool loadPreset(const PresetInfo& preset);

    /**
     * Save current state as new preset.
     * @param name Preset display name
     * @param category Category label
     * @param mode Target delay mode
     * @param description Optional description
     * @return true on success
     */
    bool savePreset(
        const std::string& name,
        const std::string& category,
        DelayMode mode,
        const std::string& description = ""
    );

    /**
     * Delete a user preset.
     * Factory presets cannot be deleted.
     * @param preset Preset to delete
     * @return true on success, false if factory or not found
     */
    bool deletePreset(const PresetInfo& preset);

    /**
     * Import a preset from external location.
     * Copies file to user preset directory.
     * @param sourcePath External preset file
     * @return true on success
     */
    bool importPreset(const std::filesystem::path& sourcePath);

    // ========================================================================
    // Directory Access
    // ========================================================================

    /**
     * Get user preset directory path.
     * Creates directory if it doesn't exist.
     */
    std::filesystem::path getUserPresetDirectory() const;

    /**
     * Get factory preset directory path.
     * May not exist if no factory presets installed.
     */
    std::filesystem::path getFactoryPresetDirectory() const;

    // ========================================================================
    // Validation
    // ========================================================================

    /**
     * Validate preset name for filesystem compatibility.
     * @param name Proposed preset name
     * @return true if valid
     */
    static bool isValidPresetName(const std::string& name);

    /**
     * Get last error message.
     * @return Error description or empty if no error
     */
    std::string getLastError() const;

private:
    Steinberg::Vst::IComponent* processor_;
    Steinberg::Vst::IEditController* controller_;
    PresetList cachedPresets_;
    std::string lastError_;

    // Scanning helpers
    void scanDirectory(const std::filesystem::path& dir, bool isFactory);
    PresetInfo parsePresetFile(const std::filesystem::path& path, bool isFactory);

    // Metadata helpers
    bool writeMetadata(const std::filesystem::path& path, const PresetInfo& info);
    bool readMetadata(const std::filesystem::path& path, PresetInfo& info);
};

} // namespace Iterum
