#pragma once

// ==============================================================================
// Seraphis Preset Configuration (FR-050, FR-051)
// ==============================================================================
// Provides the PresetManagerConfig for Seraphis.
//
// `Textures` is a SEED, not a placeholder. Phase 12 EXTENDS this list and MUST
// NOT rename a shipped category - a rename orphans every preset saved against
// it (the Membrum lesson). The name is carried in TWO places that must always
// agree: this list, and the filesystem subdirectory
// `resources/presets/Textures/`.
// ==============================================================================

#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"

#include <string>
#include <vector>

namespace Seraphis {

// Field order is load-bearing (plugins/shared/src/preset/preset_manager_config.h:16-18).
inline Krate::Plugins::PresetManagerConfig makeSeraphisPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID =*/ kProcessorUID,
        /*.pluginName =*/ "Seraphis",
        /*.pluginCategoryDesc =*/ "Synth",
        /*.subcategoryNames =*/ {"Textures"}
    };
}

}  // namespace Seraphis
