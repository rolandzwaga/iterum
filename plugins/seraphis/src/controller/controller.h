#pragma once

// ==============================================================================
// Seraphis - Edit Controller (UI thread)
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation.
// This header NEVER includes anything under processor/.
//
// NO INoteExpressionController (FR-019 - a knowing Phase 9 decision; see
// plugins/seraphis/CLAUDE.md). NO createCustomView / verifyView overrides
// (FR-018, FR-056 - there are no custom views until Phase 11).
// ==============================================================================

#include "preset/preset_manager.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <memory>

namespace Seraphis {

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate {
public:
    Controller() = default;
    ~Controller() override = default;

    static Steinberg::FUnknown* createInstance(void* /*context*/) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

private:
    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
};

}  // namespace Seraphis
