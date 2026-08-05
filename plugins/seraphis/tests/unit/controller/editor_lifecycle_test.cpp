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
//   2. SC-012 clause 2b (FR-054) - the .uidesc really carries its bound
//      controls. The harness alone proves nothing about the file's CONTENTS: a
//      template holding a single CTextLabel satisfies all three of its
//      assertions, and VST3Editor binds a mismatched control class to a
//      parameter with NO error path. The view-tree walk below is what makes a
//      wrong control type (FR-048 freezes those types for the life of the
//      plugin) fail now.
//
//      PHASE 11 RE-ANCHORED THE COUNT, DELIBERATELY. Phase 8 shipped a
//      placeholder template with EIGHT bound views and this section pinned
//      `controls.size() == 8u` / `tags == {the eight shipped IDs}`. Phase 11's
//      spec retires that template by name - "the placeholder template
//      (editor.uidesc:140-184) and its banner (:3-5) MUST be gone"
//      (specs/seraphis-phase11-ui/spec.md:973) - and replaces it with the full
//      organism-first surface whose binding budget is EXACTLY 110 bound views
//      over the 107 registered IDs (spec.md:557, :980, C-3, SC-002). The old
//      `== 8u` therefore contradicts the current spec and could only be kept by
//      keeping the placeholder. It is re-anchored here, not relaxed: the walk
//      now pins the exact 110, pins that the eight originally-shipped IDs are
//      all still bound, and keeps both class assertions. This is the assertion
//      on the BUILT VIEW TREE; parameter_surface_test's SC-002 is the assertion
//      on the XML, and neither substitutes for the other (a creator that failed
//      to link yields a tree that disagrees with the document).
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
#include "ui/cloud_view.h"
#include "ui/edit_sub_controller.h"

// Shared toggle the 2026-08-04 consistency pass drew SoftLimit as.
#include <ui/toggle_button.h>

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
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

    SECTION("Seraphis_EditorBindsTheFullParameterSurface") {
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

        // A tag >= kSessionTagBase is a SESSION tag the sub-controller stamped
        // onto a control that carries NO control-tag (edit_sub_controller.h:62,
        // :108). Those are not parameter bindings and must never be counted as
        // such - splitting them out here is what keeps the 110 comparable to
        // SC-002's XML-side count instead of drifting with the toolbar.
        std::vector<VSTGUI::CControl*> paramBound;
        std::set<std::int32_t> tags;
        for (auto* control : controls) {
            if (control->getTag() >= Seraphis::UI::kSessionTagBase) {
                continue;
            }
            paramBound.push_back(control);
            tags.insert(control->getTag());
        }

        // C-3 / SC-002: exactly 110 bound views over the 107 registered IDs -
        // the three second bindings are the header freeze cluster.
        REQUIRE(paramBound.size() == 110u);
        REQUIRE(tags.size() == 107u);

        // FR-054 continuity: every one of the eight IDs Phase 8 shipped is
        // still bound somewhere in the tree. Explicit casts, not
        // brace-narrowing: ParameterIDs' underlying type is
        // Steinberg::Vst::ParamID (uint32), and Clang errors on narrowing in
        // brace initialization.
        const std::set<std::int32_t> kPhase8Tags{
            static_cast<std::int32_t>(Seraphis::kMasterGainId),
            static_cast<std::int32_t>(Seraphis::kPolyphonyId),
            static_cast<std::int32_t>(Seraphis::kSoftLimitId),
            static_cast<std::int32_t>(Seraphis::kMacroDreamId),
            static_cast<std::int32_t>(Seraphis::kMacroBloomId),
            static_cast<std::int32_t>(Seraphis::kMacroDissolveId),
            static_cast<std::int32_t>(Seraphis::kMacroGravityId),
            static_cast<std::int32_t>(Seraphis::kMacroEntropyId)};
        REQUIRE(std::includes(tags.begin(), tags.end(), kPhase8Tags.begin(),
                              kPhase8Tags.end()));

        // FR-048 freezes the registered parameter types; the control classes
        // must agree with them. Polyphony is a StringListParameter, soft limit
        // is a stepped toggle.
        REQUIRE(dynamic_cast<VSTGUI::COptionMenu*>(controlWithTag(
                    controls, static_cast<std::int32_t>(Seraphis::kPolyphonyId))) != nullptr);
        REQUIRE(dynamic_cast<Krate::Plugins::ToggleButton*>(controlWithTag(
                    controls, static_cast<std::int32_t>(Seraphis::kSoftLimitId))) != nullptr);

        view->removed();
        view->release();
    }

    SECTION("Seraphis_PresetConfigIsLive") {
        const auto cfg = Seraphis::makeSeraphisPresetConfig();
        REQUIRE(cfg.pluginName == "Seraphis");

        // Phase 12 (FR-001 / C-1) EXTENDED the seed list to the seven shipped
        // categories, in this order (specs/seraphis-phase12-presets-release/spec.md:188).
        // This literal is a SECOND, independent copy of the list that
        // tests/unit/preset/factory_preset_test.cpp:47-48 owns as the FR-001
        // gate - both are literals so both can fail, and both MUST be moved
        // together whenever a category is added.
        const std::vector<std::string> kExpectedSubcategories{
            "Textures", "Pads", "Drones", "Bells", "Choirs", "Motion", "Cinematic"};
        REQUIRE(cfg.subcategoryNames == kExpectedSubcategories);

        const bool processorUidMatches = (cfg.processorUID == Seraphis::kProcessorUID);
        REQUIRE(processorUidMatches);

        // FR-051: the filesystem half and the XML-metadata half agree.
        const auto presetsDir = std::filesystem::path(SERAPHIS_RESOURCES_DIR) / "presets";
        REQUIRE(std::filesystem::is_directory(presetsDir / "Textures"));

        // Both path overrides stay inside the repo, so nothing touches the
        // machine's real ProgramData/Library preset trees.
        Krate::Plugins::PresetManager pm(cfg, nullptr, nullptr, presetsDir, presetsDir);

        // Phase 8's premise here was "ships no .vstpreset", so this asserted an
        // EMPTY scan. Phase 12 retires that premise by construction: the
        // `generate_seraphis_presets` target writes real factory presets into
        // this very tree (specs/seraphis-phase12-presets-release/tasks.md:416-421)
        // and they ship as tracked resources, exactly as Iterum's and Membrum's
        // do. What this section is actually for - the name is
        // Seraphis_PresetConfigIsLive - is that the config drives the scanner,
        // so the replacement asserts the property that would break if it did
        // not: every scanned preset is filed under one of the CONFIGURED
        // subcategories. A directory that is not in subcategoryNames leaves
        // `subcategory` empty (preset_manager.cpp:95-103) - the exact Membrum
        // failure mode spec.md:595-596 calls out - and fails the loop below.
        //
        // The 42-entry count is deliberately NOT asserted here. It belongs to
        // tests/unit/preset/factory_preset_test.cpp, and it would be wrong in
        // this fixture anyway: both directory overrides point at the SAME
        // directory, so scanPresets() enumerates the tree twice - once as user,
        // once as factory (preset_manager.cpp:41-49).
        const auto scanned = pm.scanPresets();
        REQUIRE_FALSE(scanned.empty());
        for (const auto& preset : scanned) {
            INFO("preset: " << preset.path.string());
            REQUIRE(std::find(cfg.subcategoryNames.begin(), cfg.subcategoryNames.end(),
                              preset.subcategory) != cfg.subcategoryNames.end());
        }

        REQUIRE(pm.getConfig().pluginName == "Seraphis");

        // Default (un-overridden) factory directory is per-plugin named.
        Krate::Plugins::PresetManager def(cfg, nullptr, nullptr);
        REQUIRE(toLower(def.getFactoryPresetDirectory().filename().string()) == "seraphis");
    }

    controller.terminate();
}

// ==============================================================================
// SC-005 (Phase 11) - 10 cycles with the FULL LAYOUT, and FR-041's teardown
// ==============================================================================
// This case began as Phase 9's SC-016: ten cycles over an enlarged registered
// surface whose .uidesc still held the eight-control PLACEHOLDER template. Phase
// 11 keeps the number - SC-005 reads "exerciseEditorLifecycle(..., cycles=10)
// passes, with getParameterCount() == 107 before and after"
// (specs/seraphis-phase11-ui/spec.md:1308-1311) - and changes what those ten
// cycles actually open. T019 retired the placeholder for the organism-first
// surface, so every cycle now builds and tears down the REAL tree: one
// CloudView, one DrawerContainer, five MacroRingKnobs and the
// SeraphisEditSubController bound on the template root. The old banner's premise
// ("deliberately leaving the eight-control placeholder as it stands") went with
// the template it described, so it is rewritten here rather than left to mislead
// the next reader.
//
// Why ten, and why this is not covered by the 3-cycle case above:
//
//   - VST3Editor::open() parses the WHOLE <control-tags> block, not just the
//     tags a view happens to reference, and EditControllerEx1 holds 107
//     Parameter objects whose lifetime spans the cycles. A dangling IDependent
//     registration on any of them surfaces as a use-after-free in teardown,
//     which is what SC-005's ASan/valgrind clauses are for.
//   - A reference-count leak in VST3Editor's parameter attach/detach shows up as
//     an arithmetic drift PER CYCLE, so a 3-cycle run sees a third of the
//     excursion a 10-cycle run does. The count is the criterion's own number,
//     not a round one.
//   - The Phase 11 tree owns resources the placeholder had none of: a
//     CVSTGUITimer created in CloudView::attached() and cancelled in removed()
//     (src/ui/cloud_view.cpp:296-300), and three raw view caches on the
//     controller that willClose() must zero (src/controller/controller.cpp:
//     388-405). Those are FR-041's clauses, and they are asserted after the loop
//     below - a leaked timer or a surviving pointer is a dangling reference into
//     a frame that has already destroyed its views.
//
// THE PARAMETER-COUNT ASSERTION IS NOT A DUPLICATE of parameter_surface_test's.
// There it is the subject; here it is the PRECONDITION - without it, a
// regression that dropped the registrations would leave this case exercising a
// smaller surface and reporting a clean lifecycle for a surface it never opened.
//
// SC-005's ASan clause is a local -DENABLE_ASAN=ON Debug build and its valgrind
// clause is the nightly editor-lifecycle job. Both select on the [lifecycle] tag
// (.github/workflows/valgrind-nightly.yml invokes each binary as
// `"$BINDIR/$bin" '[lifecycle]'`), so the tag below is load-bearing for the
// valgrind clause in exactly the way the banner at the top of this file records.
TEST_CASE("Seraphis_EditorLifecycle_SurvivesFullSurface",
          "[seraphis][controller][ui][lifecycle][phase11]") {
    const std::string uidescPath = std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc";

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // The precondition: the enlarged surface really is registered.
    REQUIRE(controller.getParameterCount() == 107);

    // SC-005: TEN headless open/close cycles, zero reports. The harness CHECKs
    // attached() == kResultTrue and REQUIREs getFrame()->getNbViews() > 0 on
    // every cycle (tests/test_helpers/editor_lifecycle_harness.h:121-128).
    Krate::TestSupport::exerciseEditorLifecycle(controller, "editor", uidescPath,
                                               /*cycles=*/10);

    // The surface must survive the cycles intact: VST3Editor takes and releases
    // references on the controller's parameters, and a teardown that released
    // one too many would show up here as a shrunken count rather than as a
    // crash on some later host action.
    REQUIRE(controller.getParameterCount() == 107);

    // FR-041, read off the state the LAST cycle left behind. The harness's final
    // act is `view->removed()` -> close() -> willClose()
    // (editor_lifecycle_harness.h:130-131), so by here every raw view pointer the
    // controller cached during that cycle must be null and the PER-OPEN
    // sub-controller counter must be back at 0 (controller.cpp:388-405). None of
    // these is redundant with custom_view_test's per-cycle check: there the
    // editor is driven by hand, here it is driven by the shared harness, and a
    // teardown path that only the harness reaches would otherwise go unasserted.
    //
    // The editor-open refcount FR-047 adds is deliberately NOT checked here - it
    // is reset in terminate(), not willClose() (FR-041's own last sentence), so a
    // zero-expectation at this point would be asserting the opposite of the spec.
    CHECK(controller.cloudView() == nullptr);
    CHECK(controller.drawer() == nullptr);
    for (auto* ring : controller.macroRingsForTest()) {
        CHECK(ring == nullptr);
    }
    CHECK(controller.subControllerInstanceCountForTest() == 0);

    controller.terminate();
}

// ==============================================================================
// SC-023 (Phase 11) - an editor that NEVER receives a frame (FR-019)
// ==============================================================================
// FR-019: "with no frame ever received - no host DataExchange support, or the
// editor opened before the processor connected - the cloud view MUST render an
// empty constellation and MUST NOT be blank-broken, crash, or spin"
// (spec.md:1032-1034). SC-023 spells that out as ten cycles with the C-2 clause
// 6 gate never opened, draw() entered at least once per cycle through a test
// seam, zero points drawn, and getParameterCount() == 107 before and after
// (spec.md:1605-1610).
//
// WHY THIS CASE DRIVES THE CYCLE ITSELF INSTEAD OF CALLING THE HARNESS.
// exerciseEditorLifecycle cannot satisfy SC-023 as worded: it calls only
// IPlugView::attached(nullptr, ...) and removed()
// (editor_lifecycle_harness.h:113-132) and its own banner records that the
// platform attach is a no-op (:12-13). No platform window means no paint cycle,
// no CDrawContext, and therefore draw() is NEVER entered - a harness-only version
// of this case would assert nothing about FR-019 at all. Because the helper owns
// its cycle loop and destroys its own editors, the same three calls are made
// here directly so that a `renderForTest()` can sit between attached() and
// removed(). The spec sentence is re-pointed at that seam in T026 (D-9 row 9g).
//
// THE GATE IS NEVER OPENED, BY CONSTRUCTION - this is not a setting the test
// forgets to flip. Controller::didOpen() does try to send the kind-0 EditorGate
// message, but sendEditMessage() bails at `allocateMessage()` returning null when
// there is no host context (controller.h:249-256), which is exactly the headless
// case. Nothing ever calls onDataExchangeBlocksReceived either, so
// cachedCloudFrame() stays the default-constructed CloudFrame for the whole run -
// partialCount 0, sequence 0 - and that is asserted per cycle below rather than
// assumed, because an assertion of "zero points drawn" against a frame that was
// never checked to be empty is vacuous.
//
// WHICH LEG OF renderForTest() RUNS HERE, AND WHY IT IS DETERMINISTIC.
// CView::getFrame() returns pImpl->parentFrame (cview.cpp:1137-1140), which is
// assigned only in CView::attached() (:452). Headless, CFrame::open(nullptr)
// returns false on its first line without ever attaching, so no view in this tree
// is attached and getFrame() is null for the CloudView. renderForTest() therefore
// takes its documented fallback leg, draw(nullptr), whose emitting half is a
// defined no-op (cloud_view.cpp:227, :265-285). buildPoints() - the half
// pointsDrawnForTest() reads - runs identically on both legs, so the criterion is
// about the real body either way.
//
// Run under -DENABLE_ASAN=ON Debug so a null-frame dereference inside draw() is a
// report rather than luck; the [lifecycle] tag also puts it in the
// valgrind-nightly selection.
TEST_CASE("Seraphis_Editor_WorksWithNoFrameEverReceived",
          "[seraphis][controller][ui][lifecycle][phase11]") {
    const std::string uidescPath = std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc";

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(controller.getParameterCount() == 107);

    Krate::TestSupport::ensureVstguiInitialized();

    constexpr int kCycles = 10;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        INFO("editor open/close cycle " << cycle);

        auto* editor = new VSTGUI::VST3Editor(&controller, "editor", uidescPath.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==
                Steinberg::kResultTrue);
        REQUIRE(editor->getFrame() != nullptr);
        REQUIRE(editor->getFrame()->getNbViews() > 0);

        // Captured BETWEEN attached() and removed(): willClose() zeros this
        // cache (FR-041), so reading it after the close would only ever be null.
        Seraphis::UI::CloudView* cloud = controller.cloudView();
        REQUIRE(cloud != nullptr);

        // FR-019's precondition, re-asserted every cycle rather than once: the
        // controller's cache is still the default-constructed frame, so the
        // render below really is "no frame ever received" and not a leftover
        // from an earlier cycle.
        // partialCount is a std::uint8_t, so it is widened explicitly: Catch2
        // would otherwise stringify a failure as a control character rather than
        // as a number.
        REQUIRE(controller.cachedCloudFrame().sequence == 0u);
        REQUIRE(static_cast<unsigned>(controller.cachedCloudFrame().partialCount) == 0u);

        const std::size_t drawsBefore = cloud->drawCountForTest();
        cloud->renderForTest();

        // SC-023: draw() was entered, and it painted an EMPTY constellation.
        // Both halves matter - a view that never drew would trivially report
        // zero points, and a view that drew something would mean partialCount
        // was not zero after all.
        CHECK(cloud->drawCountForTest() >= 1u);
        CHECK(cloud->drawCountForTest() == drawsBefore + 1u);
        CHECK(cloud->pointsDrawnForTest() == 0u);

        view->removed();  // -> close() -> willClose()
        view->release();  // FUnknown-refcounted; created with refcount 1

        // The cached pointer is dropped with the frame, so the next cycle starts
        // from nullptr rather than from a view the frame has already destroyed.
        CHECK(controller.cloudView() == nullptr);
    }

    REQUIRE(controller.getParameterCount() == 107);

    controller.terminate();
}
