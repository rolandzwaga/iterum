#pragma once

// ==============================================================================
// Platform-Specific Preset Paths
// ==============================================================================
// Constitution Principle VI: Cross-Platform Compatibility
// - Uses std::filesystem for portable path handling
// - Platform detection via preprocessor macros
// ==============================================================================

#include <filesystem>

namespace Iterum::Platform {

/// Get user preset directory (writable)
/// Windows:  %USERPROFILE%\Documents\VST3 Presets\Iterum\Iterum\
/// macOS:    ~/Library/Audio/Presets/Iterum/Iterum/
/// Linux:    ~/.vst3/presets/Iterum/Iterum/
std::filesystem::path getUserPresetDirectory();

/// Get factory preset directory (read-only)
/// Windows:  %PROGRAMDATA%\VST3 Presets\Iterum\Iterum\
/// macOS:    /Library/Audio/Presets/Iterum/Iterum/
/// Linux:    /usr/share/vst3/presets/Iterum/Iterum/
std::filesystem::path getFactoryPresetDirectory();

/// Ensure directory exists, creating it if necessary
/// @return true if directory exists or was created
bool ensureDirectoryExists(const std::filesystem::path& path);

} // namespace Iterum::Platform
