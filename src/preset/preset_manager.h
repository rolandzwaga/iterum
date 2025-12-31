#pragma once

// ==============================================================================
// PresetManager - Preset File Operations
// ==============================================================================
// Spec 042: Preset Browser
// Handles all preset file operations including scanning, loading, saving,
// importing, and deleting presets.
//
// Thread Safety: All methods must be called from UI thread only.
//
// Constitution Compliance:
// - Principle II: No audio thread involvement
// - Principle VI: Cross-platform via std::filesystem
// ==============================================================================

#include "preset_info.h"
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

namespace Steinberg::Vst {
    class IComponent;
    class IEditController;
}

namespace Iterum {

class PresetManager {
public:
    using PresetList = std::vector<PresetInfo>;

    /// Constructor
    /// @param processor VST3 processor component for state access
    /// @param controller VST3 edit controller for state sync
    explicit PresetManager(
        Steinberg::Vst::IComponent* processor,
        Steinberg::Vst::IEditController* controller
    );

    ~PresetManager();

    // ==========================================================================
    // Scanning
    // ==========================================================================

    /// Scan all preset directories and return combined list
    /// Scans both user and factory directories
    PresetList scanPresets();

    /// Get presets filtered by mode
    /// Must call scanPresets() first
    PresetList getPresetsForMode(DelayMode mode) const;

    /// Search presets by name (case-insensitive)
    PresetList searchPresets(std::string_view query) const;

    // ==========================================================================
    // Load/Save
    // ==========================================================================

    /// Load a preset, restoring all parameters
    /// @return true on success
    bool loadPreset(const PresetInfo& preset);

    /// Save current state as new preset
    /// @return true on success
    bool savePreset(
        const std::string& name,
        const std::string& category,
        DelayMode mode,
        const std::string& description = ""
    );

    /// Delete a user preset
    /// Factory presets cannot be deleted
    /// @return true on success, false if factory or not found
    bool deletePreset(const PresetInfo& preset);

    /// Import a preset from external location
    /// Copies file to user preset directory
    /// @return true on success
    bool importPreset(const std::filesystem::path& sourcePath);

    // ==========================================================================
    // Directory Access
    // ==========================================================================

    /// Get user preset directory path (creates if needed)
    std::filesystem::path getUserPresetDirectory() const;

    /// Get factory preset directory path
    std::filesystem::path getFactoryPresetDirectory() const;

    // ==========================================================================
    // Validation
    // ==========================================================================

    /// Validate preset name for filesystem compatibility
    static bool isValidPresetName(const std::string& name);

    /// Get last error message
    std::string getLastError() const { return lastError_; }

private:
    Steinberg::Vst::IComponent* processor_ = nullptr;
    Steinberg::Vst::IEditController* controller_ = nullptr;
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
