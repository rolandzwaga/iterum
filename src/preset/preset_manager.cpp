#include "preset_manager.h"
#include "../platform/preset_paths.h"

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include <algorithm>
#include <cctype>

namespace Iterum {

PresetManager::PresetManager(
    Steinberg::Vst::IComponent* processor,
    Steinberg::Vst::IEditController* controller
)
    : processor_(processor)
    , controller_(controller)
{
}

PresetManager::~PresetManager() = default;

// =============================================================================
// Scanning
// =============================================================================

PresetManager::PresetList PresetManager::scanPresets() {
    cachedPresets_.clear();

    // Scan user presets
    auto userDir = getUserPresetDirectory();
    if (!userDir.empty() && std::filesystem::exists(userDir)) {
        scanDirectory(userDir, false);
    }

    // Scan factory presets
    auto factoryDir = getFactoryPresetDirectory();
    if (!factoryDir.empty() && std::filesystem::exists(factoryDir)) {
        scanDirectory(factoryDir, true);
    }

    // Sort by name
    std::sort(cachedPresets_.begin(), cachedPresets_.end());

    return cachedPresets_;
}

void PresetManager::scanDirectory(const std::filesystem::path& dir, bool isFactory) {
    namespace fs = std::filesystem;
    std::error_code ec;

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".vstpreset") {
            auto info = parsePresetFile(entry.path(), isFactory);
            if (info.isValid()) {
                cachedPresets_.push_back(std::move(info));
            }
        }
    }
}

PresetInfo PresetManager::parsePresetFile(const std::filesystem::path& path, bool isFactory) {
    PresetInfo info;
    info.path = path;
    info.isFactory = isFactory;

    // Extract name from filename (without extension)
    info.name = path.stem().string();

    // Try to read metadata from preset file
    readMetadata(path, info);

    // If no category from metadata, try to get from parent directory
    if (info.category.empty()) {
        auto parent = path.parent_path();
        if (parent.has_filename()) {
            info.category = parent.filename().string();
        }
    }

    return info;
}

PresetManager::PresetList PresetManager::getPresetsForMode(DelayMode mode) const {
    PresetList filtered;
    for (const auto& preset : cachedPresets_) {
        if (preset.mode == mode) {
            filtered.push_back(preset);
        }
    }
    return filtered;
}

PresetManager::PresetList PresetManager::searchPresets(std::string_view query) const {
    if (query.empty()) {
        return cachedPresets_;
    }

    PresetList results;

    // Convert query to lowercase
    std::string lowerQuery(query);
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
        [](unsigned char c) { return std::tolower(c); });

    for (const auto& preset : cachedPresets_) {
        // Convert name to lowercase for comparison
        std::string lowerName = preset.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (lowerName.find(lowerQuery) != std::string::npos) {
            results.push_back(preset);
        }
    }

    return results;
}

// =============================================================================
// Load/Save
// =============================================================================

bool PresetManager::loadPreset(const PresetInfo& preset) {
    if (!preset.isValid() || !processor_ || !controller_) {
        lastError_ = "Invalid preset or components not available";
        return false;
    }

    // TODO: Implement using PresetFile::loadPreset()
    // This will be completed in Phase 3 (US1+2)

    lastError_.clear();
    return true;
}

bool PresetManager::savePreset(
    const std::string& name,
    const std::string& category,
    DelayMode mode,
    const std::string& description
) {
    if (!isValidPresetName(name)) {
        lastError_ = "Invalid preset name";
        return false;
    }

    if (!processor_ || !controller_) {
        lastError_ = "Components not available";
        return false;
    }

    // TODO: Implement using PresetFile::savePreset()
    // This will be completed in Phase 3 (US1+2)

    lastError_.clear();
    return true;
}

bool PresetManager::deletePreset(const PresetInfo& preset) {
    if (preset.isFactory) {
        lastError_ = "Cannot delete factory presets";
        return false;
    }

    if (preset.path.empty() || !std::filesystem::exists(preset.path)) {
        lastError_ = "Preset file not found";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::remove(preset.path, ec)) {
        lastError_ = "Failed to delete preset: " + ec.message();
        return false;
    }

    lastError_.clear();
    return true;
}

bool PresetManager::importPreset(const std::filesystem::path& sourcePath) {
    if (!std::filesystem::exists(sourcePath)) {
        lastError_ = "Source file not found";
        return false;
    }

    if (sourcePath.extension() != ".vstpreset") {
        lastError_ = "Invalid preset file type";
        return false;
    }

    // Parse to get mode for destination folder
    PresetInfo info = parsePresetFile(sourcePath, false);

    // Create destination path
    auto userDir = getUserPresetDirectory();
    Platform::ensureDirectoryExists(userDir);

    auto destPath = userDir / sourcePath.filename();

    std::error_code ec;
    if (!std::filesystem::copy_file(sourcePath, destPath,
            std::filesystem::copy_options::skip_existing, ec)) {
        lastError_ = "Failed to import preset: " + ec.message();
        return false;
    }

    lastError_.clear();
    return true;
}

// =============================================================================
// Directory Access
// =============================================================================

std::filesystem::path PresetManager::getUserPresetDirectory() const {
    auto path = Platform::getUserPresetDirectory();
    Platform::ensureDirectoryExists(path);
    return path;
}

std::filesystem::path PresetManager::getFactoryPresetDirectory() const {
    return Platform::getFactoryPresetDirectory();
}

// =============================================================================
// Validation
// =============================================================================

bool PresetManager::isValidPresetName(const std::string& name) {
    if (name.empty() || name.length() > 255) {
        return false;
    }

    // Check for invalid filesystem characters
    const std::string invalidChars = "/\\:*?\"<>|";
    for (char c : name) {
        if (invalidChars.find(c) != std::string::npos) {
            return false;
        }
    }

    return true;
}

// =============================================================================
// Metadata Helpers
// =============================================================================

bool PresetManager::writeMetadata(const std::filesystem::path& /*path*/, const PresetInfo& /*info*/) {
    // TODO: Implement XML metadata writing
    // This will be completed in Phase 3 (US1+2)
    return true;
}

bool PresetManager::readMetadata(const std::filesystem::path& /*path*/, PresetInfo& /*info*/) {
    // TODO: Implement XML metadata reading
    // This will be completed in Phase 3 (US1+2)
    return true;
}

} // namespace Iterum
