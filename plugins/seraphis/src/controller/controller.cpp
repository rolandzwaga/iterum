// ==============================================================================
// Seraphis - Edit Controller implementation
// ==============================================================================

#include "controller/controller.h"

#include "parameters/global_params.h"
#include "parameters/macro_params.h"
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

    // Exactly eight parameters (FR-048 freezes their types).
    registerGlobalParams(parameters);
    registerMacroParams(parameters);

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

    // Order MUST match Processor::getState.
    loadGlobalParamsToController(streamer, setParam);
    loadMacroParamsToController(streamer, setParam);

    return kResultOk;
}

tresult PLUGIN_API Controller::getParamStringByValue(
    Vst::ParamID id, Vst::ParamValue valueNormalized, Vst::String128 string) {

    if (formatGlobalParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    if (formatMacroParam(id, valueNormalized, string) == kResultOk) {
        return kResultOk;
    }
    // Falls through so the StringListParameter (Polyphony) formats itself.
    return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (FIDStringsEqual(name, Vst::ViewType::kEditor)) {
        return new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");
    }
    return nullptr;
}

}  // namespace Seraphis
