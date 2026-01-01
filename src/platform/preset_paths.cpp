#include "preset_paths.h"
#include <cstdlib>

namespace Iterum::Platform {

std::filesystem::path getUserPresetDirectory() {
    namespace fs = std::filesystem;

#if defined(_WIN32)
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return fs::path(userProfile) / "Documents" / "Krate Audio" / "Iterum";
    }
    return fs::path();
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) {
        return fs::path(home) / "Documents" / "Krate Audio" / "Iterum";
    }
    return fs::path();
#else
    // Linux
    const char* home = std::getenv("HOME");
    if (home) {
        return fs::path(home) / "Documents" / "Krate Audio" / "Iterum";
    }
    return fs::path();
#endif
}

std::filesystem::path getFactoryPresetDirectory() {
    namespace fs = std::filesystem;

#if defined(_WIN32)
    const char* programData = std::getenv("PROGRAMDATA");
    if (programData) {
        return fs::path(programData) / "Krate Audio" / "Iterum";
    }
    return fs::path();
#elif defined(__APPLE__)
    return fs::path("/Library/Application Support/Krate Audio/Iterum");
#else
    // Linux
    return fs::path("/usr/share/krate-audio/iterum");
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
