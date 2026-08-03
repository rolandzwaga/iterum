// ==============================================================================
// Seraphis - Edit Controller implementation
// ==============================================================================

#include "controller/controller.h"

#include "parameters/aether_params.h"
#include "parameters/atmosphere_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/effects_params.h"
#include "parameters/global_params.h"
#include "parameters/life_mod_params.h"
#include "parameters/macro_params.h"
#include "parameters/morph_params.h"
#include "plugin_ids.h"
#include "preset/seraphis_preset_config.h"

#include "base/source/fstreamer.h"

#include <type_traits>

// FR-052: the update config ships COMPILED BUT UNUSED in Phase 8 (no
// UpdateChecker instance -- it spawns a thread and a network fetch). This
// static_assert is the ONLY thing that compiles the header; a CMake source-list
// entry would NOT (CMake sets HEADER_FILE_ONLY on .h entries).
#include "update/seraphis_update_config.h"
static_assert(std::is_same_v<decltype(Seraphis::makeSeraphisUpdateConfig()),
                             Krate::Plugins::UpdateCheckerConfig>);

namespace Seraphis {

using namespace Steinberg;

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    // FR-060 / SC-001. The whole Phase 9 surface, in BAND ORDER - the same order
    // processParameterChanges' range ladder walks and getState writes, so the
    // three parallel lists (register / format / setComponentState) never drift.
    // 4 + 5 + 11 + 13 + 10 + 13 + 17 + 18 = 91 (FR-048 freezes their types),
    // + Phase 10's 16 effects rows = 107 (spec C-6, SC-001).
    registerGlobalParams(parameters);      // 0, 1, 2, 3
    registerMacroParams(parameters);       // 100-104
    registerCloudParams(parameters);       // 200-210
    registerMorphParams(parameters);       // 400-412
    registerLifeModParams(parameters);     // 600-604, 700-704
    registerBodyParams(parameters);        // 800-812
    registerAtmosphereParams(parameters);  // 1000-1016
    registerAetherParams(parameters);      // 1200-1217
    registerEffectsParams(parameters);     // 1400-1443

    // FR-050. NO UpdateChecker (FR-052).
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        makeSeraphisPresetConfig(), nullptr, this);

    return kResultOk;
}

tresult PLUGIN_API Controller::terminate() {
    presetManager_.reset();
    return EditControllerEx1::terminate();
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }

    IBStreamer streamer(state, kLittleEndian);

    int32 version = 0;
    if (!streamer.readInt32(version)) {
        return kResultFalse;
    }
    if (version > kCurrentStateVersion) {
        return kResultFalse;
    }

    const auto setParam = [this](Vst::ParamID id, double value) {
        setParamNormalized(id, value);
    };

    // Order MUST match Processor::getState EXACTLY (plan 5.1's write order). The
    // seed is its own trio positioned AFTER [macro] (FR-091a, global_params.h:
    // 259-271), and loadMorphParamsToController consumes - and discards - the
    // four 541-byte payloads, without which the following [life]/[body]/[atmos]/
    // [aether] blocks (55 parameters) would be read 2164 bytes off (plan 2.3.0).
    // Every loader is EOF-safe, so a 36-byte version-1 stream stops after
    // [macro] and leaves the remaining 99 parameters at their registered
    // defaults (FR-093), and a version-2 stream stops before [effects] and
    // leaves Phase 10's 16 at theirs (spec C-8).
    loadGlobalParamsToController(streamer, setParam);      // 0, 1, 2
    loadMacroParamsToController(streamer, setParam);       // 100-104
    loadGlobalSeedToController(streamer, setParam);        // 3
    loadCloudParamsToController(streamer, setParam);       // 200-210
    loadMorphParamsToController(streamer, setParam);       // 400-412 + payloads
    loadLifeModParamsToController(streamer, setParam);     // 600-604, 700-704
    loadBodyParamsToController(streamer, setParam);        // 800-812
    loadAtmosphereParamsToController(streamer, setParam);  // 1000-1016
    loadAetherParamsToController(streamer, setParam);      // 1200-1217
    loadEffectsParamsToController(streamer, setParam);     // 1400-1443 (v3, LAST)

    return kResultOk;
}

tresult PLUGIN_API Controller::getParamStringByValue(
    Vst::ParamID id, Vst::ParamValue valueNormalized, Vst::String128 string) {

    // FR-061. Band order again, and NO formatter claims a dropdown ID: every
    // `L` parameter is a StringListParameter and formats itself through the
    // fall-through below, out of the SINGLE dropdown_mappings.h label table it
    // was registered from.
    if (formatGlobalParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatMacroParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatCloudParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatMorphParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatLifeModParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatBodyParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatAtmosphereParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatAetherParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    // The ONE pack that DOES claim its dropdowns (1413, 1419): it writes the
    // label straight out of the single table they were registered from
    // (effects_params.h:322-329), so the shown string cannot drift from the list.
    if (formatEffectsParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    // Falls through so every StringListParameter formats itself.
    return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (FIDStringsEqual(name, Vst::ViewType::kEditor)) {
        return new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");
    }
    return nullptr;
}

}  // namespace Seraphis
