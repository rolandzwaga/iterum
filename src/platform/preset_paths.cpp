#include "preset_paths.h"
#include <cstdlib>

namespace Iterum::Platform {

std::filesystem::path getUserPresetDirectory() {
    namespace fs = std::filesystem;

#if defined(_WIN32)
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return fs::path(userProfile) / "Documents" / "VST3 Presets" / "Iterum" / "Iterum";
    }
    return fs::path();
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) {
        return fs::path(home) / "Library" / "Audio" / "Presets" / "Iterum" / "Iterum";
    }
    return fs::path();
#else
    // Linux
    const char* home = std::getenv("HOME");
    if (home) {
        return fs::path(home) / ".vst3" / "presets" / "Iterum" / "Iterum";
    }
    return fs::path();
#endif
}

std::filesystem::path getFactoryPresetDirectory() {
    namespace fs = std::filesystem;

#if defined(_WIN32)
    const char* programData = std::getenv("PROGRAMDATA");
    if (programData) {
        return fs::path(programData) / "VST3 Presets" / "Iterum" / "Iterum";
    }
    return fs::path();
#elif defined(__APPLE__)
    return fs::path("/Library/Audio/Presets/Iterum/Iterum");
#else
    // Linux
    return fs::path("/usr/share/vst3/presets/Iterum/Iterum");
#endif
}

bool ensureDirectoryExists(const std::filesystem::path& path) {
    namespace fs = std::filesystem;

    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    if (fs::exists(path, ec)) {
        return fs::is_directory(path, ec);
    }

    return fs::create_directories(path, ec);
}

} // namespace Iterum::Platform
