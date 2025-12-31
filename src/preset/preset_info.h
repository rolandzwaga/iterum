#pragma once

// ==============================================================================
// PresetInfo - Preset Metadata Structure
// ==============================================================================
// Spec 042: Preset Browser
// Contains metadata for a single preset file.
// ==============================================================================

#include <string>
#include <filesystem>
#include "../delay_mode.h"  // DelayMode enum (SDK-independent)

namespace Iterum {

struct PresetInfo {
    std::string name;                    // Display name (from filename or metadata)
    std::string category;                // Category label (e.g., "Ambient", "Rhythmic")
    DelayMode mode = DelayMode::Digital; // Target delay mode
    std::filesystem::path path;          // Full path to .vstpreset file
    bool isFactory = false;              // True if factory preset (read-only)
    std::string description;             // Optional description text
    std::string author;                  // Optional author name

    /// Check if preset info is valid (has name and path)
    [[nodiscard]] bool isValid() const {
        return !name.empty() && !path.empty();
    }

    /// Compare presets by name (case-insensitive)
    bool operator<(const PresetInfo& other) const {
        return name < other.name;
    }
};

} // namespace Iterum
