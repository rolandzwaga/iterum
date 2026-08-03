// ==============================================================================
// Seraphis - Editor open/close lifecycle tests (T024 -> SC-012)
// ==============================================================================
// Three things are asserted here, and only the first is what the shared harness
// gives us for free:
//
//   1. SC-012 clause 2 - three headless open/close cycles complete with no crash
//      (and, under ASan/valgrind, no use-after-free in the VST3EditorDelegate
//      teardown path). The harness itself CHECKs attached() == kResultTrue and
//      REQUIREs getFrame()->getNbViews() > 0
//      (tests/test_helpers/editor_lifecycle_harness.h:120-128).
//
//   2. SC-012 clause 2b (FR-054) - the .uidesc really carries the eight bound
//      controls. The harness alone proves nothing about the file's CONTENTS: a
//      template holding a single CTextLabel satisfies all three of its
//      assertions, and VST3Editor binds a mismatched control class to a
//      parameter with NO error path. The view-tree walk below is what makes a
//      wrong control type (FR-048 freezes those types for the life of the
//      plugin) fail now rather than in Phase 11.
//
//   3. SC-012 clause 2c (FR-050, FR-051) - the preset config is LIVE.
//      Instantiating a PresetManager scans nothing: its constructor only stores
//      config_/processor_/controller_ and the two path overrides
//      (plugins/shared/src/preset/preset_manager.h:55-61, :134-143); all
//      enumeration lives in scanPresets() (preset_manager.cpp:37-56), which
//      nothing in Phase 8 calls. Without this section FR-050/FR-051 have no
//      detector at all.
//
// THE [lifecycle] TAG IS MANDATORY. .github/workflows/valgrind-nightly.yml:283-290
// invokes each binary as `"$BINDIR/$bin" '[lifecycle]'`; without the tag the
// nightly lane selects ZERO Seraphis tests and the valgrind clause is satisfied
// vacuously (or the job fails on no-tests-matched).
//
// NEVER name a kPlatformType* constant here - always
// Krate::TestSupport::nativePlatformType() (editor_lifecycle_harness.h:87).
// tools/lint-platform-type-literals.js enforces that.
// ==============================================================================

#include <editor_lifecycle_harness.h>

#include "controller/controller.h"
#include "plugin_ids.h"
#include "preset/preset_manager.h"
#include "preset/seraphis_preset_config.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace {

// Local recursive helper - deliberately NOT shared infrastructure; only this
// test needs it.
//
// The `getTag() >= 0` filter is load-bearing, and NOT a convenience. CTextLabel
// IS-A CControl in VSTGUI (ctextlabel.h:19 -> cparamdisplay.h:23 -> CControl),
// so an unfiltered dynamic_cast walk would also collect the eight decorative
// labels in resources/editor.uidesc and report 16. Untagged views keep the
// tag CParamDisplay's constructor gives them, -1 (cparamdisplay.cpp:23-24), and
// a view whose `control-tag` names nothing in <control-tags> is likewise forced
// to -1 (uidescription/viewcreator/controlcreator.cpp:80, :103). So "tag >= 0"
// means exactly "bound to a parameter", which is what FR-054 is about.
void collectControls(VSTGUI::CViewContainer* container,
                     std::vector<VSTGUI::CControl*>& out) {
    if (container == nullptr) {
        return;
    }
    const std::uint32_t count = container->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        VSTGUI::CView* view = container->getView(i);
        if (view == nullptr) {
            continue;
        }
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
            if (control->getTag() >= 0) {
                out.push_back(control);
            }
        }
        if (auto* child = dynamic_cast<VSTGUI::CViewContainer*>(view)) {
            collectControls(child, out);
        }
    }
}

VSTGUI::CControl* controlWithTag(const std::vector<VSTGUI::CControl*>& controls,
                                 std::int32_t tag) {
    for (auto* control : controls) {
        if (control != nullptr && control->getTag() == tag) {
            return control;
        }
    }
    return nullptr;
}

// Platform::getFactoryPresetDirectory lowercases the leaf on Linux and keeps it
// cased on Windows/macOS (plugins/shared/src/platform/preset_paths.cpp:36-50).
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}  // namespace

TEST_CASE("Seraphis_EditorLifecycle", "[seraphis][controller][ui][lifecycle]") {
    const std::string uidescPath = std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc";

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // SC-012 clause 2: 3 open/close cycles, no crash.
    Krate::TestSupport::exerciseEditorLifecycle(controller, "editor", uidescPath,
                                               /*cycles=*/3);

    SECTION("Seraphis_EditorBindsTheEightShippedParameters") {
        // The harness owns and destroys its own editors, so build one directly
        // to inspect the view tree it produced.
        Krate::TestSupport::ensureVstguiInitialized();

        auto* editor = new VSTGUI::VST3Editor(&controller, "editor", uidescPath.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==
                Steinberg::kResultTrue);
        REQUIRE(editor->getFrame() != nullptr);

        std::vector<VSTGUI::CControl*> controls;
        collectControls(editor->getFrame(), controls);

        std::set<std::int32_t> tags;
        for (auto* control : controls) {
            tags.insert(control->getTag());
        }

        // FR-054: exactly eight bound controls, one per shipped parameter ID.
        REQUIRE(controls.size() == 8u);

        // Explicit casts, not brace-narrowing: ParameterIDs' underlying type is
        // Steinberg::Vst::ParamID (uint32), and Clang errors on narrowing in
        // brace initialization.
        const std::set<std::int32_t> kExpectedTags{
            static_cast<std::int32_t>(Seraphis::kMasterGainId),
            static_cast<std::int32_t>(Seraphis::kPolyphonyId),
            static_cast<std::int32_t>(Seraphis::kSoftLimitId),
            static_cast<std::int32_t>(Seraphis::kMacroDreamId),
            static_cast<std::int32_t>(Seraphis::kMacroBloomId),
            static_cast<std::int32_t>(Seraphis::kMacroDissolveId),
            static_cast<std::int32_t>(Seraphis::kMacroGravityId),
            static_cast<std::int32_t>(Seraphis::kMacroEntropyId)};
        REQUIRE(tags == kExpectedTags);

        // FR-048 freezes the registered parameter types; the control classes
        // must agree with them. Polyphony is a StringListParameter, soft limit
        // is a stepped toggle.
        REQUIRE(dynamic_cast<VSTGUI::COptionMenu*>(controlWithTag(
                    controls, static_cast<std::int32_t>(Seraphis::kPolyphonyId))) != nullptr);
        REQUIRE(dynamic_cast<VSTGUI::CCheckBox*>(controlWithTag(
                    controls, static_cast<std::int32_t>(Seraphis::kSoftLimitId))) != nullptr);

        view->removed();
        view->release();
    }

    SECTION("Seraphis_PresetConfigIsLive") {
        const auto cfg = Seraphis::makeSeraphisPresetConfig();
        REQUIRE(cfg.pluginName == "Seraphis");

        const std::vector<std::string> kExpectedSubcategories{"Textures"};
        REQUIRE(cfg.subcategoryNames == kExpectedSubcategories);

        const bool processorUidMatches = (cfg.processorUID == Seraphis::kProcessorUID);
        REQUIRE(processorUidMatches);

        // FR-051: the filesystem half and the XML-metadata half agree.
        const auto presetsDir = std::filesystem::path(SERAPHIS_RESOURCES_DIR) / "presets";
        REQUIRE(std::filesystem::is_directory(presetsDir / "Textures"));

        // Both path overrides stay inside the repo, so nothing touches the
        // machine's real ProgramData/Library preset trees.
        Krate::Plugins::PresetManager pm(cfg, nullptr, nullptr, presetsDir, presetsDir);
        REQUIRE(pm.scanPresets().empty());  // Phase 8 ships no .vstpreset
        REQUIRE(pm.getConfig().pluginName == "Seraphis");

        // Default (un-overridden) factory directory is per-plugin named.
        Krate::Plugins::PresetManager def(cfg, nullptr, nullptr);
        REQUIRE(toLower(def.getFactoryPresetDirectory().filename().string()) == "seraphis");
    }

    controller.terminate();
}

// ==============================================================================
// SC-016 (Phase 9) - the lifecycle stays clean at the ENLARGED surface
// ==============================================================================
// Phase 9 takes the registered surface from 8 parameters to 91 and adds a
// <control-tags> entry for every one of them, while deliberately leaving the
// eight-control placeholder template as it stands (spec "Scope" clause 8 and the
// Phase 11 non-goal row). Those two facts are exactly what makes this a separate
// criterion from the Phase 8 case above:
//
//   - VST3Editor::open() parses the WHOLE <control-tags> block, not just the
//     tags a view happens to reference, so 83 unreferenced tags are 83 new
//     UIDescription entries created and destroyed on every open/close cycle.
//     The Phase 8 case cannot detect a defect there because it was written
//     against a file with eight tags in it.
//   - EditControllerEx1 now holds 91 Parameter objects whose lifetime spans the
//     cycles; a dangling IDependent registration on any of them surfaces as a
//     use-after-free in teardown, which is what the ASan/valgrind clauses of
//     SC-016 are for.
//
// SC-016's clause (a) is the valgrind-nightly editor-lifecycle job and clause
// (b) is a local -DENABLE_ASAN=ON Debug run. Both select on the [lifecycle] tag
// (.github/workflows/valgrind-nightly.yml invokes each binary as
// `"$BINDIR/$bin" '[lifecycle]'`), so the tag below is load-bearing for clause
// (a) in exactly the way the banner at the top of this file records.
//
// THE PARAMETER-COUNT ASSERTION IS NOT A DUPLICATE of parameter_surface_test's.
// There it is the subject; here it is the PRECONDITION - without it, a
// regression that dropped the Phase 9 registrations would leave this case
// exercising the Phase 8 surface and reporting a clean lifecycle for a surface
// it never opened.
TEST_CASE("Seraphis_EditorLifecycle_SurvivesFullSurface",
          "[seraphis][controller][ui][lifecycle]") {
    const std::string uidescPath = std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc";

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // The precondition: the enlarged surface really is registered.
    REQUIRE(controller.getParameterCount() == 107);

    // SC-016: TEN headless open/close cycles, zero reports. The count is the
    // criterion's own number, not a round one - SC-016 reads "the editor-lifecycle
    // harness still passes 10 open/close cycles" - and it is load-bearing rather
    // than decorative: a reference-count leak in VST3Editor's parameter
    // attach/detach shows up as an arithmetic drift per cycle, so a 3-cycle run
    // sees a third of the excursion a 10-cycle run does. The harness CHECKs
    // attached() == kResultTrue and REQUIREs getFrame()->getNbViews() > 0 on
    // every cycle (tests/test_helpers/editor_lifecycle_harness.h:102-105).
    Krate::TestSupport::exerciseEditorLifecycle(controller, "editor", uidescPath,
                                               /*cycles=*/10);

    // The surface must survive the cycles intact: VST3Editor takes and releases
    // references on the controller's parameters, and a teardown that released
    // one too many would show up here as a shrunken count rather than as a
    // crash on some later host action.
    REQUIRE(controller.getParameterCount() == 107);

    controller.terminate();
}
