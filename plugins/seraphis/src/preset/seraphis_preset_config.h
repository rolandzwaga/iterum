#pragma once

// ==============================================================================
// Seraphis Preset Configuration (FR-050, FR-051)
// ==============================================================================
// Provides the PresetManagerConfig for Seraphis.
//
// `Textures` was a SEED, not a placeholder. Phase 12 has EXTENDED this list to
// the seven shipped categories; `Textures` keeps its byte-exact spelling and its
// existing directory.
//
// This list is ADDITIVE-ONLY. A shipped category MUST NOT be renamed, reordered
// out of existence, or removed - a rename orphans every preset saved against it
// (the Membrum lesson): `PresetManager::parsePresetFile` matches the parent
// directory name against `subcategoryNames` by exact `==` and leaves
// `subcategory` EMPTY on a miss
// (plugins/shared/src/preset/preset_manager.cpp:95-103).
//
// Every name is carried in TWO places that must always agree: this list, and the
// filesystem subdirectory `resources/presets/<Name>/`.
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
        /*.subcategoryNames =*/ {"Textures", "Pads", "Drones", "Bells",
                                 "Choirs", "Motion", "Cinematic"}
    };
}

/// The preset browser's tab labels: "All" first, then the config's own
/// subcategory list in order.
///
/// THIS IS THE CONTROLLER'S OWN CONSTRUCTION, not a description of it.
/// `Controller::togglePresetBrowser()` calls this function and hands the result
/// straight to `PresetBrowserView`. It lives here rather than inline in the
/// controller because `togglePresetBrowser()` needs a live `CFrame` and an open
/// editor, so a test cannot invoke it - and a test that rebuilt the same three
/// lines would keep passing after the controller's copy drifted, which is the
/// exact defect SC-008 exists to catch.
[[nodiscard]] inline std::vector<std::string> makeSeraphisPresetTabLabels() {
    const Krate::Plugins::PresetManagerConfig config = makeSeraphisPresetConfig();
    std::vector<std::string> tabLabels;
    tabLabels.reserve(config.subcategoryNames.size() + 1u);
    tabLabels.emplace_back("All");
    tabLabels.insert(tabLabels.end(), config.subcategoryNames.begin(),
                     config.subcategoryNames.end());
    return tabLabels;
}

}  // namespace Seraphis
