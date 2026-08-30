#pragma once

// ==============================================================================
// Seraphis Update-Check Configuration (FR-052)
// ==============================================================================
// SHIPPED COMPILED BUT UNUSED IN PHASE 8. No `UpdateChecker` instance exists
// anywhere in this plugin: it spawns a std::thread and a network fetch that
// would land inside the editor-lifecycle harness, the ASan lane and the
// valgrind nightly.
//
// This header is included by nothing else, and listing a `.h` in a CMake source
// list does NOT compile it (CMake sets HEADER_FILE_ONLY). The ONLY thing that
// compiles it is the static_assert touch point in controller.cpp.
// ==============================================================================

#include "update/update_checker_config.h"
#include "../version.h"

namespace Seraphis {

inline Krate::Plugins::UpdateCheckerConfig makeSeraphisUpdateConfig() {
    return Krate::Plugins::UpdateCheckerConfig{
        /*.pluginName =*/ stringPluginName,
        /*.currentVersion =*/ VERSION_STR,
        /*.endpointUrl =*/ "https://rolandzwaga.github.io/krate-audio/versions.json"
    };
}

}  // namespace Seraphis
