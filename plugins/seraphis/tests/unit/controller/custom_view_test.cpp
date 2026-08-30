// ==============================================================================
// Seraphis Phase 11 - controller-side custom-view / cloud-frame consumer tests
// ==============================================================================
// Spec:  specs/seraphis-phase11-ui/spec.md  (SC-006 arm (h), FR-016)
// Tasks: specs/seraphis-phase11-ui/tasks.md (T009; later tasks EXTEND this file)
//
// Everything here is a PURE FUNCTION CALL on a stack-allocated Controller: no
// processor, no host, no DataExchange transport. The SDK's queue machinery is
// deliberately out of scope - Controller::onDataExchangeBlocksReceived is the
// entry point the SDK calls, so calling it directly is the honest test of the
// caching rule, and it is the only way to observe the rule without a live host
// that implements IDataExchangeHandler.
// ==============================================================================

#include "controller/controller.h"
// T024 / SC-022(c): the macro ring is driven on its REAL ParamID
// (kMacroBloomId == 101), not on a made-up tag.
#include "plugin_ids.h"
#include "processor/cloud_frame.h"
// T018 / SC-026 ONLY. The refcount criterion is not satisfiable on the
// controller alone: it says in so many words that the PRODUCER's gate stays open
// across the first willClose(), so this one case drives a real Processor with the
// very messages the controller emitted. Nothing else in this TU touches it.
#include "processor/processor.h"
#include "seraphis_test_fixture.h"
// T024 arm 1 requires this TU to include EVERY header under src/ui/, so that a
// class the scan of arm 2 finds is also a class the compile-time assertion set
// can name. `edit_message.h` arrives transitively through cloud_view.h and
// controller.h; it is listed explicitly so the include list and the scanned
// directory can be diffed by eye.
#include "ui/cloud_view.h"
#include "ui/drawer_container.h"
#include "ui/edit_message.h"
#include "ui/edit_sub_controller.h"
#include "ui/macro_ring_knob.h"
#include "ui/preset_browser_view.h"

// Shared components the 2026-08-04 consistency pass put in the uidesc: the
// tab/slot segment bars and (via createCustomView) the outline preset button.
#include <ui/icon_segment_button.h>
#include <ui/outline_button.h>

#include <krate/dsp/processors/spectral_state.h>

#include "public.sdk/source/vst/hosting/hostclasses.h"

#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <pluginterfaces/base/smartpointer.h>  // Steinberg::owned

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ccontrol.h"
// T024 arm 1's own negative control: CTextLabel is the transitive-CView base the
// scan's allowlist deliberately does NOT carry.
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/uidescription/uiattributes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

// LAST on purpose: on Windows this pulls <windows.h> in, and every header above
// is better off being parsed without its macros. NEVER name a kPlatformType*
// constant directly - Krate::TestSupport::nativePlatformType() is the only
// sanctioned form (editor_lifecycle_harness.h:87, enforced by
// tools/lint-platform-type-literals.js).
#include <editor_lifecycle_harness.h>

// =============================================================================
// T014 - the compile-time half of SC-004 arm 1 (FR-020)
// =============================================================================
// The runtime instantiation criterion - exactly FIVE MacroRingKnobs in the built
// frame, counted by dynamic_cast, which is what catches the silent-fallback
// hazard the Phase 8 uidesc banner names (resources/editor.uidesc:3-5) - needs
// the Phase 11 uidesc and lands in T019/T024. Until then THIS pair is the red:
// it does not compile until the class exists on the ArcKnob branch of the CView
// hierarchy, which is exactly what FR-020 requires of it.
static_assert(std::is_base_of_v<VSTGUI::CView, Seraphis::UI::MacroRingKnob>,
              "FR-020: MacroRingKnob must be a CView");
static_assert(std::is_base_of_v<Krate::Plugins::ArcKnob, Seraphis::UI::MacroRingKnob>,
              "FR-020: MacroRingKnob must derive from Krate::Plugins::ArcKnob "
              "(plugins/shared/src/ui/arc_knob.h:49)");

TEST_CASE("Seraphis_Controller_CachesOnlyTheMostRecentBlock", "[controller][phase11]") {
    Seraphis::Controller controller;

    SECTION("one delivery of three blocks caches the LAST one") {
        // FR-016 / SC-006 arm (h): "most recent wins". Older blocks in the same
        // delivery are discarded, not queued (Membrum's documented rule,
        // plugins/membrum/src/controller/controller.cpp:1719-1726).
        std::array<Seraphis::CloudFrame, 3> frames{};
        for (std::uint32_t i = 0; i < 3u; ++i) {
            frames[i].sequence     = 100u + i;
            frames[i].partialCount = static_cast<std::uint8_t>(i + 1u);
        }

        std::array<Steinberg::Vst::DataExchangeBlock, 3> blocks{};
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            blocks[i].data = &frames[i];
            blocks[i].size = static_cast<Steinberg::uint32>(sizeof(Seraphis::CloudFrame));
        }

        controller.onDataExchangeBlocksReceived(
            static_cast<Steinberg::Vst::DataExchangeUserContextID>(
                Seraphis::kCloudFrameUserContextId),
            static_cast<Steinberg::uint32>(blocks.size()), blocks.data(),
            static_cast<Steinberg::TBool>(false));

        CHECK(controller.cachedCloudFrame().sequence == 102u);
        // Cast: Catch2 stringifies a std::uint8_t as a character on failure.
        CHECK(static_cast<unsigned>(controller.cachedCloudFrame().partialCount) == 3u);
    }

    SECTION("queueOpened asks for UI-thread dispatch") {
        // FR-016: dispatchOnBackgroundThread must come back FALSE so
        // cachedCloudFrame_ is only ever touched on the UI thread and needs no
        // mutex. The seed is deliberately `true` - a body that never writes the
        // out-parameter passes if the seed is already false.
        // Steinberg::TBool is a std::uint8 typedef (ftypes.h:83) - cast at every
        // assertion so Catch2 reports a number, not a control character.
        auto dispatchOnBackgroundThread = static_cast<Steinberg::TBool>(true);
        REQUIRE(static_cast<unsigned>(dispatchOnBackgroundThread) != 0u);

        controller.queueOpened(static_cast<Steinberg::Vst::DataExchangeUserContextID>(
                                   Seraphis::kCloudFrameUserContextId),
                               static_cast<Steinberg::uint32>(sizeof(Seraphis::CloudFrame)),
                               dispatchOnBackgroundThread);

        CHECK(static_cast<unsigned>(dispatchOnBackgroundThread) == 0u);
    }
}

// =============================================================================
// T015 - CloudView (SC-020 arms (f)/(g), SC-032, FR-017, FR-018, FR-028)
// =============================================================================
// Everything below is headless: a stack Controller, a CloudView that is never
// attached to a CFrame, and synthetic CloudFrames pushed straight through
// onDataExchangeBlocksReceived - the same entry point the SDK calls.
//
// NO TEST CALLS draw(nullptr). Painting goes through CloudView::renderForTest(),
// which is the only seam that can enter the real body headlessly: nothing in
// the harness ever paints (tests/test_helpers/editor_lifecycle_harness.h:12-13 -
// "The platform attach itself is a no-op here"), so no CDrawContext exists.

using Seraphis::UI::CloudView;

namespace {

constexpr VSTGUI::CCoord kTestViewWidth  = 400.0;
constexpr VSTGUI::CCoord kTestViewHeight = 300.0;

/// Eight partials an octave apart starting at 100 Hz (100 .. 12800), all inside
/// the fixed 20 Hz .. 20 kHz span, so every point is ~30 px from its neighbour
/// on a 300 px view and a hit test at an exact centre is unambiguous.
///
/// activeVoices == 0 and fundamentalHz == 0 on purpose: CloudView::referenceHz()
/// then returns kFallbackReferenceHz (C4), which is a compile-time constant the
/// assertions can name (Q6 - authoring must work with no note held).
[[nodiscard]] Seraphis::CloudFrame makeOctaveFrame(std::uint32_t sequence,
                                                   std::uint8_t partialCount) {
    Seraphis::CloudFrame f{};
    f.sequence      = sequence;
    f.partialCount  = partialCount;
    f.activeVoices  = 0;
    f.fundamentalHz = 0.0f;
    for (std::size_t i = 0; i < static_cast<std::size_t>(partialCount); ++i) {
        f.frequencyHz[i] = 100.0f * std::exp2(static_cast<float>(i));
        f.amplitude[i]   = 0.6f;
        f.position[i]    = 0.0f;
    }
    return f;
}

void deliver(Seraphis::Controller& controller, Seraphis::CloudFrame& frame) {
    Steinberg::Vst::DataExchangeBlock block{};
    block.data = &frame;
    block.size = static_cast<Steinberg::uint32>(sizeof(Seraphis::CloudFrame));
    controller.onDataExchangeBlocksReceived(
        static_cast<Steinberg::Vst::DataExchangeUserContextID>(
            Seraphis::kCloudFrameUserContextId),
        static_cast<Steinberg::uint32>(1), &block, static_cast<Steinberg::TBool>(false));
}

[[nodiscard]] VSTGUI::SharedPointer<CloudView> makeView(Seraphis::Controller& controller) {
    return VSTGUI::owned(new CloudView(
        VSTGUI::CRect(0.0, 0.0, kTestViewWidth, kTestViewHeight), &controller));
}

}  // namespace

TEST_CASE("Seraphis_CloudView_AxisMapIsMonotoneAndClamped", "[cloud_view][phase11]") {
    Seraphis::Controller controller;
    auto view = makeView(controller);

    // --- y: log2 frequency, INVERTED, over a FIXED 20 Hz .. 20 kHz span -------
    const VSTGUI::CCoord y1     = view->yFromHzForTest(1.0f);
    const VSTGUI::CCoord y1999  = view->yFromHzForTest(19.99f);
    const VSTGUI::CCoord y20    = view->yFromHzForTest(20.0f);
    const VSTGUI::CCoord y100   = view->yFromHzForTest(100.0f);
    const VSTGUI::CCoord y1000  = view->yFromHzForTest(1000.0f);
    const VSTGUI::CCoord y20k   = view->yFromHzForTest(20000.0f);
    const VSTGUI::CCoord y20k01 = view->yFromHzForTest(20000.01f);
    const VSTGUI::CCoord y44100 = view->yFromHzForTest(44100.0f);

    SECTION("(i) strictly monotone across the in-span interior") {
        CHECK(y20 > y100);
        CHECK(y100 > y1000);
        CHECK(y1000 > y20k);
    }

    SECTION("(ii) clamped at both edges, and demonstrably NOT wrapped") {
        CHECK(y1 == Catch::Approx(y20).margin(1e-9));
        CHECK(y1999 == Catch::Approx(y20).margin(1e-9));
        CHECK(y20k01 == Catch::Approx(y20k).margin(1e-9));
        CHECK(y44100 == Catch::Approx(y20k).margin(1e-9));
        // A WRAP would put 44 100 Hz back down near the 20 Hz end. It must sit
        // at the far end instead.
        CHECK(y44100 < y20);
        CHECK(std::abs(y44100 - y20k) < std::abs(y44100 - y20));
    }

    SECTION("(iii) y is inverted - a higher frequency draws at a SMALLER y") {
        CHECK(y1000 < y100);
        CHECK(y20 == Catch::Approx(kTestViewHeight).margin(1e-9));  // bottom
        CHECK(y20k == Catch::Approx(0.0).margin(1e-9));             // top
    }

    SECTION("(iv) x is linear in position and clamped at +/-1") {
        const VSTGUI::CCoord xm2   = view->xFromPositionForTest(-2.0f);
        const VSTGUI::CCoord xm1   = view->xFromPositionForTest(-1.0f);
        const VSTGUI::CCoord xmh   = view->xFromPositionForTest(-0.5f);
        const VSTGUI::CCoord x0    = view->xFromPositionForTest(0.0f);
        const VSTGUI::CCoord xph   = view->xFromPositionForTest(0.5f);
        const VSTGUI::CCoord xp1   = view->xFromPositionForTest(1.0f);
        const VSTGUI::CCoord xp2   = view->xFromPositionForTest(2.0f);

        CHECK(xm2 == Catch::Approx(xm1).margin(1e-9));
        CHECK(xp2 == Catch::Approx(xp1).margin(1e-9));
        CHECK(xm1 < xmh);
        CHECK(xmh < x0);
        CHECK(x0 < xph);
        CHECK(xph < xp1);
        CHECK(xm1 == Catch::Approx(0.0).margin(1e-9));
        CHECK(xp1 == Catch::Approx(kTestViewWidth).margin(1e-9));
    }
}

TEST_CASE("Seraphis_CloudView_MaskedPartialStaysAClickTarget", "[cloud_view][phase11]") {
    Seraphis::Controller controller;
    auto view = makeView(controller);

    Seraphis::CloudFrame frame = makeOctaveFrame(1u, static_cast<std::uint8_t>(8));
    frame.maskBits     = (1ull << 3);  // partial 3 masked ...
    frame.amplitude[3] = 0.0f;         // ... and its amplitude already smoothed to 0
    frame.amplitude[5] = 0.0f;         // partial 5: silent but NOT masked
    deliver(controller, frame);

    view->onTimerForTest();
    view->renderForTest();

    // FR-018: the timer only invalidates on a sequence change.
    CHECK(view->invalidCountForTest() == 1u);
    view->onTimerForTest();
    CHECK(view->invalidCountForTest() == 1u);

    REQUIRE(view->pointsDrawnForTest() == 8u);
    const std::vector<CloudView::DrawnPoint>& points = view->drawnPointsForTest();

    SECTION("(i)+(ii) the masked partial is present, hollow, at kMaskedRingRadius") {
        // Not culled: eight points drawn for eight partials, above.
        CHECK(points[3].hollow);
        CHECK(points[3].radius == Catch::Approx(Seraphis::UI::kMaskedRingRadius).margin(1e-12));
        CHECK(points[3].radius > 0.0);
        CHECK(points[3].radius > Seraphis::UI::kMinRadius);
    }

    SECTION("(iii) and it is still the nearest hit at its own centre") {
        const VSTGUI::CPoint at(points[3].x, points[3].y);
        CHECK(view->hitTestForTest(at) == 3);
    }

    SECTION("complement: an UNMASKED silent partial draws at kMinRadius") {
        CHECK_FALSE(points[5].hollow);
        CHECK(points[5].radius == Catch::Approx(Seraphis::UI::kMinRadius).margin(1e-12));
    }
}

TEST_CASE("Seraphis_CloudView_GesturesEmitTheRightEditMessage", "[cloud_view][phase11]") {
    Seraphis::Controller controller;

    // Seed the display mirror so "the field that is NOT moving is carried
    // through unchanged" is a real assertion and not a comparison of two zeros.
    const Krate::DSP::SpectralState mirror =
        Krate::DSP::makeFactoryState(Krate::DSP::SpectralStateId::Bell);
    controller.setSlotMirror(1, mirror);

    Seraphis::CloudFrame frame = makeOctaveFrame(7u, static_cast<std::uint8_t>(8));
    deliver(controller, frame);

    auto view = makeView(controller);
    view->setMode(CloudView::Mode::Edit);
    view->setSelectedSlot(1);
    view->onTimerForTest();
    view->renderForTest();

    REQUIRE(view->pointsDrawnForTest() == 8u);
    const CloudView::DrawnPoint target = view->drawnPointsForTest()[3];
    const VSTGUI::CPoint downAt(target.x, target.y);

    const VSTGUI::CButtonState left(VSTGUI::kLButton);
    const VSTGUI::CButtonState altLeft(VSTGUI::kLButton | VSTGUI::kAlt);

    // --- (1) plain vertical drag: ratio moves, amplitude carried through -----
    {
        VSTGUI::CPoint p = downAt;
        REQUIRE(view->onMouseDown(p, left) == VSTGUI::kMouseEventHandled);
        VSTGUI::CPoint moved(downAt.x, downAt.y - 40.0);
        view->onMouseMoved(moved, left);
        VSTGUI::CPoint up = moved;
        view->onMouseUp(up, left);

        const Seraphis::UI::EditMessage& m = controller.lastSentEditMessageForTest();
        CHECK(static_cast<unsigned>(m.kind) == 1u);
        CHECK(static_cast<unsigned>(m.slot) == 1u);
        CHECK(static_cast<unsigned>(m.index) == 3u);

        const float expected =
            view->hzFromYForTest(downAt.y - 40.0) / Seraphis::UI::kFallbackReferenceHz;
        CHECK(m.a == Catch::Approx(expected).epsilon(1e-5));
        CHECK(m.b == Catch::Approx(mirror.amplitudes[3]).margin(1e-7));
        // b is a LIVE field, not a coincidental zero.
        CHECK(m.b > 0.0f);
    }

    // --- (2) ALT + vertical drag: amplitude moves, ratio carried through -----
    {
        VSTGUI::CPoint p = downAt;
        REQUIRE(view->onMouseDown(p, altLeft) == VSTGUI::kMouseEventHandled);
        VSTGUI::CPoint moved(downAt.x, downAt.y + 55.0);
        view->onMouseMoved(moved, altLeft);
        VSTGUI::CPoint up = moved;
        view->onMouseUp(up, altLeft);

        const Seraphis::UI::EditMessage& m = controller.lastSentEditMessageForTest();
        CHECK(static_cast<unsigned>(m.kind) == 1u);  // NOT kind 2
        CHECK(static_cast<unsigned>(m.index) == 3u);
        CHECK(m.a == Catch::Approx(mirror.ratios[3]).margin(1e-6));
        CHECK(m.b == Catch::Approx(view->amplitudeFromYForTest(downAt.y + 55.0)).margin(1e-6));
    }

    // --- (3) horizontal drag: a pan edit in [-1, +1] -------------------------
    {
        VSTGUI::CPoint p = downAt;
        REQUIRE(view->onMouseDown(p, left) == VSTGUI::kMouseEventHandled);
        VSTGUI::CPoint moved(downAt.x + 60.0, downAt.y);
        view->onMouseMoved(moved, left);
        VSTGUI::CPoint up = moved;
        view->onMouseUp(up, left);

        const Seraphis::UI::EditMessage& m = controller.lastSentEditMessageForTest();
        CHECK(static_cast<unsigned>(m.kind) == 2u);
        CHECK(static_cast<unsigned>(m.index) == 3u);
        CHECK(m.a >= -1.0f);
        CHECK(m.a <= 1.0f);
        CHECK(m.a == Catch::Approx(view->positionFromXForTest(downAt.x + 60.0)).margin(1e-6));
    }

    // --- (4a) click, bit CLEAR -> mask it ------------------------------------
    {
        VSTGUI::CPoint p = downAt;
        REQUIRE(view->onMouseDown(p, left) == VSTGUI::kMouseEventHandled);
        VSTGUI::CPoint up(downAt.x + 1.0, downAt.y + 1.0);  // inside kClickSlopPx
        view->onMouseUp(up, left);

        const Seraphis::UI::EditMessage& m = controller.lastSentEditMessageForTest();
        CHECK(static_cast<unsigned>(m.kind) == 3u);
        CHECK(static_cast<unsigned>(m.index) == 3u);
        CHECK(m.a == Catch::Approx(1.0f).margin(1e-7));
    }

    // --- (4b) the SAME click against a frame whose bit 3 is SET -> unmask ----
    {
        Seraphis::CloudFrame masked = makeOctaveFrame(8u, static_cast<std::uint8_t>(8));
        masked.maskBits = (1ull << 3);
        deliver(controller, masked);
        view->onTimerForTest();
        view->renderForTest();

        REQUIRE(view->pointsDrawnForTest() == 8u);
        const CloudView::DrawnPoint maskedTarget = view->drawnPointsForTest()[3];
        VSTGUI::CPoint p(maskedTarget.x, maskedTarget.y);
        REQUIRE(view->onMouseDown(p, left) == VSTGUI::kMouseEventHandled);
        VSTGUI::CPoint up(maskedTarget.x, maskedTarget.y);
        view->onMouseUp(up, left);

        const Seraphis::UI::EditMessage& m = controller.lastSentEditMessageForTest();
        CHECK(static_cast<unsigned>(m.kind) == 3u);
        CHECK(static_cast<unsigned>(m.index) == 3u);
        CHECK(m.a == Catch::Approx(0.0f).margin(1e-7));  // the TOGGLE, not an
                                                         // unconditional mask
    }

    // Five gestures, five messages: a mouse-up that followed a drag must NOT
    // also emit a click.
    CHECK(controller.editMessageSendCountForTest() == 5u);
}

// =============================================================================
// Regression (user bug 2026-08-04): "when I switch to edit mode, I don't see
// any difference. I still have to play a note to see anything."
// Q6's drawing half: with NO live frame, Edit mode MUST draw the selected
// slot's authored partials against the C4 fallback reference so there is
// something to see and drag while silent. Only the inverse map's C4 fallback
// had been implemented; the slot rendering never was.
// =============================================================================
TEST_CASE("Seraphis_CloudView_EditModeDrawsTheSlotWhileSilent",
          "[cloud_view][phase11]") {
    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);  // seeds slotMirror_
    auto view = makeView(controller);

    // No frame was ever delivered: Observe mode has nothing to draw.
    view->renderForTest();
    REQUIRE(view->pointsDrawnForTest() == 0u);

    // Edit mode: the SELECTED SLOT's authored partials appear, silent or not.
    view->setMode(CloudView::Mode::Edit);
    view->renderForTest();

    const Krate::DSP::SpectralState& slot = controller.slotMirror(0);
    REQUIRE(slot.numPartials > 0);  // factory-seeded at initialize()
    REQUIRE(view->pointsDrawnForTest() == static_cast<std::size_t>(slot.numPartials));

    // The y axis is the authored ratio against the C4 fallback (Q6): drift-free
    // by construction, because there is no live frame to leak drift from.
    const std::vector<CloudView::DrawnPoint>& points = view->drawnPointsForTest();
    for (const std::size_t i : {std::size_t{0}, std::size_t{1}}) {
        const float hz = slot.ratios[i] * Seraphis::UI::kFallbackReferenceHz;
        CHECK(points[i].y == Catch::Approx(view->yFromHzForTest(hz)).margin(1e-9));
    }

    // The points are draggable: the hit test finds one where it was drawn.
    CHECK(view->hitTestForTest(VSTGUI::CPoint(points[0].x, points[0].y)) == 0);

    // Switching the edit slot redraws (a stale constellation was the bug's
    // second face: setSelectedSlot never invalidated).
    const std::size_t invalidsBefore = view->invalidCountForTest();
    view->setSelectedSlot(2);
    CHECK(view->invalidCountForTest() == invalidsBefore + 1u);

    controller.terminate();
}

// =============================================================================
// Regression (2026-08-04 decision-coverage audit): Q4's view half. The frame
// has carried morphTravelPosition since T008 and the spec ruled "the view marks
// the selected slot 'not currently sounding' when it does not contribute" -
// but no indicator was ever built (the clause lived only in the decisions
// log; SC-034 now enforces it). Contribution mirrors SpectralMorphEngine::
// slotContributes (spectral_morph_engine.h:565-569): slot == floor(pos) or
// floor(pos)+1.
// =============================================================================
TEST_CASE("Seraphis_CloudView_SelectedSlotContributionIndicator",
          "[cloud_view][phase11]") {
    Seraphis::Controller controller;
    auto view = makeView(controller);
    view->setMode(CloudView::Mode::Edit);

    // No live frame: the slot IS what is drawn, so it always "contributes".
    REQUIRE(view->pointsDrawnForTest() == 0u);
    CHECK(view->selectedSlotContributesForTest());

    // Live frame with the journey parked between slots 0 and 1.
    Seraphis::CloudFrame frame = makeOctaveFrame(1u, static_cast<std::uint8_t>(8));
    frame.morphTravelPosition = 0.2f;
    deliver(controller, frame);
    view->onTimerForTest();

    view->setSelectedSlot(0);
    CHECK(view->selectedSlotContributesForTest());   // k = 0
    view->setSelectedSlot(1);
    CHECK(view->selectedSlotContributesForTest());   // k + 1
    view->setSelectedSlot(2);
    CHECK_FALSE(view->selectedSlotContributesForTest());
    view->setSelectedSlot(3);
    CHECK_FALSE(view->selectedSlotContributesForTest());

    // Journey moves onto the 2/3 pair: the verdicts follow the frame.
    Seraphis::CloudFrame moved = makeOctaveFrame(2u, static_cast<std::uint8_t>(8));
    moved.morphTravelPosition = 2.9f;
    deliver(controller, moved);
    view->onTimerForTest();
    CHECK(view->selectedSlotContributesForTest());   // slot 3, k + 1
    view->setSelectedSlot(0);
    CHECK_FALSE(view->selectedSlotContributesForTest());
}

// =============================================================================
// T016 - DrawerContainer (SC-020, FR-018, FR-023, FR-024)
// =============================================================================
// The criterion is the FRAME -> REDRAW path, not a bare timer. A headless
// controller receives no frames on its own, so any observed *rate* would only be
// the rate this test chose; what is actually asserted is that one delivered
// frame with a new `sequence` produces exactly one invalidation and that an
// unchanged `sequence` produces none - with the drawer OPEN, i.e. while it
// overlaps the cloud view.
//
// The two views are assembled under a real 1000 x 700 root container, which is
// the whole point of D-4: getViewSize() is in PARENT coordinates, so both rects
// are only the absolute rects FR-023 / FR-024 name while the drawer and the
// cloud view are DIRECT CHILDREN of the template root. Nesting either one deeper
// would make these comparisons fail, which is exactly the failure the spec wants
// caught here rather than in a DAW.

using Seraphis::UI::DrawerContainer;

static_assert(std::is_base_of_v<VSTGUI::CViewContainer, DrawerContainer>,
              "FR-025: the drawer is a plain CViewContainer subclass - NEVER a "
              "UIViewSwitchContainer, which would realise only the active tab "
              "and leave six tabs' worth of ParamIDs unreachable (C-3)");
static_assert(DrawerContainer::kTabCount == 7,
              "FR-022: Cloud, Morph, Body, Atmos, Aether, FX, Life/Env");

namespace {

/// The Phase 11 template root (plan section 9).
constexpr VSTGUI::CRect kTemplateRootRect{0.0, 0.0, 1000.0, 700.0};

/// FR-024: the cloud view's rect, IDENTICAL in both drawer states. The drawer
/// grows upward OVER it; it is never removed, hidden, unmounted or resized.
constexpr VSTGUI::CRect kCloudViewRect{0.0, 32.0, 1000.0, 670.0};

/// The tab pages' local rect inside the drawer: below the 30 px tab strip,
/// tall enough to fill the OPEN drawer. Child rects are parent-relative.
constexpr VSTGUI::CRect kTabPageRect{0.0, 30.0, 1000.0, 280.0};

/// C-1's rect table for the drawer, TRANSCRIBED FROM THE SPEC and deliberately
/// NOT taken from DrawerContainer's own constants. SC-020(e) compares the live
/// drawer against `DrawerContainer::kCollapsedRect` / `kOpenRect`, which proves
/// the toggle is a two-state toggle but says NOTHING about whether those two
/// states are the specced geometry - a retuned constant would move the drawer on
/// screen and leave every one of those CHECKs green. These two literals are the
/// only place in this file where C-1's numbers are written down independently,
/// and the pin against them is in section (e).
constexpr VSTGUI::CRect kSpecDrawerCollapsedRect{0.0, 670.0, 1000.0, 700.0};
constexpr VSTGUI::CRect kSpecDrawerOpenRect{0.0, 420.0, 1000.0, 700.0};

struct DrawerHarness {
    VSTGUI::SharedPointer<VSTGUI::CViewContainer> root;
    CloudView* cloudView = nullptr;      // owned by `root`
    DrawerContainer* drawer = nullptr;   // owned by `root`
};

/// CViewContainer::addView() takes ownership of the caller's reference
/// (cviewcontainer.cpp:502-532 stores a remembering SharedPointer; removeAll()
/// forgets it), so the children are plain `new` and only the root is owned here.
[[nodiscard]] DrawerHarness makeDrawerHarness(Seraphis::Controller& controller) {
    DrawerHarness h;
    h.root = VSTGUI::owned(new VSTGUI::CViewContainer(kTemplateRootRect));

    // FR-006: the cloud view is child index 0, so everything else draws over it.
    h.cloudView = new CloudView(kCloudViewRect, &controller);
    h.root->addView(h.cloudView);

    // Declared at its COLLAPSED rect, exactly as the uidesc declares it.
    h.drawer = new DrawerContainer(DrawerContainer::kCollapsedRect);
    h.root->addView(h.drawer);

    // Seven page containers, ALL present - never a UIViewSwitchContainer.
    for (int i = 0; i < DrawerContainer::kTabCount; ++i) {
        h.drawer->addView(new VSTGUI::CViewContainer(kTabPageRect));
    }
    h.drawer->setActiveTab(0);
    return h;
}

/// One delivered frame per tick, each with a strictly increasing `sequence`.
void feedFrames(Seraphis::Controller& controller, CloudView& view,
                std::uint32_t firstSequence, std::uint32_t count) {
    for (std::uint32_t k = 0; k < count; ++k) {
        Seraphis::CloudFrame f =
            makeOctaveFrame(firstSequence + k, static_cast<std::uint8_t>(8));
        deliver(controller, f);
        view.onTimerForTest();
    }
}

constexpr std::uint32_t kFedFrames = 60u;
constexpr std::uint32_t kIdleTicks = 30u;

}  // namespace

TEST_CASE("Seraphis_Drawer_DoesNotStopCloudView", "[drawer][phase11]") {
    SECTION("(a)(b)(c) the drawer is OPEN and the cloud view keeps redrawing") {
        Seraphis::Controller controller;
        DrawerHarness h = makeDrawerHarness(controller);

        h.drawer->setOpen(true);
        REQUIRE(h.drawer->isOpen());
        REQUIRE(h.drawer->getViewSize() == DrawerContainer::kOpenRect);

        // (a) one new sequence => exactly one invalidation, 60 times over.
        feedFrames(controller, *h.cloudView, 1u, kFedFrames);
        CHECK(h.cloudView->invalidCountForTest() == static_cast<std::size_t>(kFedFrames));

        // (b) an idle transport costs timer callbacks and NO redraws.
        for (std::uint32_t k = 0; k < kIdleTicks; ++k) {
            h.cloudView->onTimerForTest();
        }
        CHECK(h.cloudView->invalidCountForTest() == static_cast<std::size_t>(kFedFrames));

        // (c) FR-024: opening the drawer did not resize the cloud view.
        CHECK(h.cloudView->getViewSize() == kCloudViewRect);
    }

    SECTION("(d) the same three with the drawer COLLAPSED") {
        Seraphis::Controller controller;
        DrawerHarness h = makeDrawerHarness(controller);

        REQUIRE_FALSE(h.drawer->isOpen());
        REQUIRE(h.drawer->getViewSize() == DrawerContainer::kCollapsedRect);

        feedFrames(controller, *h.cloudView, 1u, kFedFrames);
        CHECK(h.cloudView->invalidCountForTest() == static_cast<std::size_t>(kFedFrames));

        for (std::uint32_t k = 0; k < kIdleTicks; ++k) {
            h.cloudView->onTimerForTest();
        }
        CHECK(h.cloudView->invalidCountForTest() == static_cast<std::size_t>(kFedFrames));

        CHECK(h.cloudView->getViewSize() == kCloudViewRect);
    }

    SECTION("(e) TWO rects and no others, and the cloud view's never moves") {
        Seraphis::Controller controller;
        DrawerHarness h = makeDrawerHarness(controller);

        // FIRST, PIN THE CONSTANTS THEMSELVES TO C-1's TABLE. Everything below
        // compares the live drawer against the implementation's own two rects,
        // which is a statement about the TOGGLE and not about the GEOMETRY: with
        // kOpenRect retuned to any other pair of numbers every CHECK in this
        // section still passes and the drawer is in the wrong place. This is the
        // one comparison against independently transcribed spec literals.
        CHECK(DrawerContainer::kCollapsedRect == kSpecDrawerCollapsedRect);
        CHECK(DrawerContainer::kOpenRect == kSpecDrawerOpenRect);

        // Compared against the SAME constants the implementation toggles
        // between - never against re-typed literals in this file.
        CHECK(h.drawer->getViewSize() == DrawerContainer::kCollapsedRect);
        CHECK(h.cloudView->getViewSize() == kCloudViewRect);

        h.drawer->setOpen(true);
        CHECK(h.drawer->getViewSize() == DrawerContainer::kOpenRect);
        CHECK(h.cloudView->getViewSize() == kCloudViewRect);

        h.drawer->setOpen(false);
        CHECK(h.drawer->getViewSize() == DrawerContainer::kCollapsedRect);
        CHECK(h.cloudView->getViewSize() == kCloudViewRect);

        // Idempotent: a redundant setOpen() must not invent a third rect.
        h.drawer->setOpen(false);
        CHECK(h.drawer->getViewSize() == DrawerContainer::kCollapsedRect);
    }

    SECTION("(f) seven page children, all present, exactly one visible") {
        Seraphis::Controller controller;
        DrawerHarness h = makeDrawerHarness(controller);

        REQUIRE(h.drawer->tabPageCount() == DrawerContainer::kTabCount);

        for (int active = 0; active < DrawerContainer::kTabCount; ++active) {
            h.drawer->setActiveTab(active);
            CHECK(h.drawer->activeTab() == active);

            int visibleCount = 0;
            for (int i = 0; i < DrawerContainer::kTabCount; ++i) {
                VSTGUI::CViewContainer* page = h.drawer->tabPage(i);
                REQUIRE(page != nullptr);
                if (page->isVisible()) {
                    ++visibleCount;
                    CHECK(i == active);
                }
            }
            CHECK(visibleCount == 1);
        }

        // Out-of-range selections are rejected, not clamped onto a page.
        h.drawer->setActiveTab(DrawerContainer::kTabCount);
        CHECK(h.drawer->activeTab() == DrawerContainer::kTabCount - 1);
        h.drawer->setActiveTab(-1);
        CHECK(h.drawer->activeTab() == DrawerContainer::kTabCount - 1);
    }
}

// =============================================================================
// T017 - SeraphisEditSubController (SC-022(b), SC-022(d), FR-007, FR-045, C-7b)
// =============================================================================
// WHY THE SUB-CONTROLLER IS BUILT DIRECTLY HERE RATHER THAN BY THE .uidesc.
// The `sub-controller="SeraphisEdit"` attribute lands on the template root in
// T019, which replaces resources/editor.uidesc wholesale; T017 (this task) is
// its dependency and runs first, so at this point the shipped .uidesc is still
// the Phase 8 placeholder and carries neither that attribute nor a single
// `session-tag`. Driving Controller::createSubController and the returned
// object's verifyView() directly is therefore the honest red-then-green test of
// the MECHANISM - the assignment of a session tag and a listener to a control
// that has no `control-tag` - which is exactly what FR-045 is about.
//
// The end-to-end half (the shipped XML really carries the attribute, so the
// sub-controller really is instantiated during an editor open) is SC-004's
// dynamic_cast walk and lands in T019/T024 against the Phase 11 uidesc. Nothing
// below asserts about the placeholder document's contents.

using Seraphis::UI::SeraphisEditSubController;

// It is NOT a CView (plan section 8.4). This is T024 arm 1's compile-time half,
// stated here because this is the file where the class first exists.
static_assert(!std::is_base_of_v<VSTGUI::CView, SeraphisEditSubController>,
              "FR-045 / C-7a: the edit sub-controller is a DelegationController, "
              "never a view");
static_assert(std::is_base_of_v<VSTGUI::DelegationController, SeraphisEditSubController>,
              "C-7b: it must delegate everything it does not claim to the parent "
              "controller, or every TAGGED control in the sub-tree goes dead");

namespace {

/// C-7b's table, in full. `attribute` is the value the uidesc writes into the
/// custom `session-tag` attribute; `tag` is what verifyView must assign.
struct SessionControlRow {
    const char* attribute;
    std::int32_t tag;
    const char* drives;
};

const std::vector<SessionControlRow>& sessionControlRows() {
    static const std::vector<SessionControlRow> rows{
        {.attribute = "preset",
         .tag = Seraphis::UI::kPresetButtonTag,
         .drives = "header preset button (FR-007)"},
        {.attribute = "mode", .tag = Seraphis::UI::kModeToggleTag, .drives = "Obs/Edit toggle"},
        {.attribute = "drawerHandle",
         .tag = Seraphis::UI::kDrawerHandleTag,
         .drives = "drawer open/close"},
        {.attribute = "blend", .tag = Seraphis::UI::kBlendTag, .drives = "Blend A->B slider"},
        {.attribute = "tilt", .tag = Seraphis::UI::kTiltTag, .drives = "Tilt dB/oct"},
        // 2026-08-04 consistency pass: the seven tab buttons and four slot
        // buttons became TWO IconSegmentButton bars (the Ruinae MainTab idiom);
        // the selected index rides the control's normalized value.
        {.attribute = "tabs",
         .tag = Seraphis::UI::kTabBarTag,
         .drives = "seven-tab drawer bar (FR-022)"},
        {.attribute = "slots",
         .tag = Seraphis::UI::kSlotBarTag,
         .drives = "four-slot morph selector bar"},
    };
    return rows;
}

[[nodiscard]] VSTGUI::SharedPointer<VSTGUI::COnOffButton> makeTaglessButton() {
    // COnOffButton's destructor is protected, so it can only be released through
    // forget() - VSTGUI::owned() is what does that.
    return VSTGUI::owned(new VSTGUI::COnOffButton(VSTGUI::CRect(0.0, 0.0, 24.0, 24.0)));
}

/// UIAttributes is reference-counted, so it is filled in place rather than
/// returned by value.
void setSessionTagAttribute(VSTGUI::UIAttributes& attributes, const char* value) {
    attributes.setAttribute(std::string(Seraphis::UI::kSessionTagAttribute),
                            std::string(value));
}

/// Recursive count of PresetBrowserViews anywhere under `container`.
[[nodiscard]] int countPresetBrowsers(VSTGUI::CViewContainer* container) {
    if (container == nullptr) {
        return 0;
    }
    int found = 0;
    const std::uint32_t count = container->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        VSTGUI::CView* child = container->getView(i);
        if (child == nullptr) {
            continue;
        }
        if (dynamic_cast<Krate::Plugins::PresetBrowserView*>(child) != nullptr) {
            ++found;
        }
        if (auto* nested = dynamic_cast<VSTGUI::CViewContainer*>(child)) {
            found += countPresetBrowsers(nested);
        }
    }
    return found;
}

}  // namespace

TEST_CASE("Seraphis_Phase11_SubController_OwnsEveryTaglessControl",
          "[controller][phase11]") {
    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // The sub-controller is owned by the view that carries the attribute in
    // production (UIDescription stores it as kCViewControllerAttribute and
    // CView's destructor deletes it, uidescription.cpp:741-755). No view adopts
    // it here, so this test owns it.
    VSTGUI::IController* raw =
        controller.createSubController("SeraphisEdit", nullptr, nullptr);
    REQUIRE(raw != nullptr);
    const std::unique_ptr<VSTGUI::IController> ownedSubController(raw);

    auto* sub = dynamic_cast<SeraphisEditSubController*>(raw);
    REQUIRE(sub != nullptr);
    REQUIRE(controller.subControllerInstanceCountForTest() == 1);

    SECTION("it answers exactly ONE name") {
        CHECK(controller.createSubController("NotTheSeraphisOne", nullptr, nullptr) ==
              nullptr);
        CHECK(controller.createSubController(nullptr, nullptr, nullptr) == nullptr);
        // A rejected name must not move the counter either.
        CHECK(controller.subControllerInstanceCountForTest() == 1);
    }

    SECTION("every tag-less control in C-7b's table gets ITS tag and THIS listener") {
        for (const SessionControlRow& row : sessionControlRows()) {
            INFO("session-tag=\"" << row.attribute << "\" drives " << row.drives);

            VSTGUI::SharedPointer<VSTGUI::COnOffButton> button = makeTaglessButton();

            // The precondition FR-045 exists for: VSTGUI's control creator only
            // installs a listener when a `control-tag` attribute is present
            // (viewcreator/controlcreator.cpp:75-100), so a tag-less control
            // arrives here dead.
            REQUIRE(button->getTag() == -1);
            REQUIRE(button->getListener() == nullptr);

            VSTGUI::UIAttributes attributes;
            setSessionTagAttribute(attributes, row.attribute);
            VSTGUI::CView* verified = sub->verifyView(button.get(), attributes, nullptr);

            CHECK(verified == button.get());
            CHECK(button->getTag() == row.tag);
            // Outside the registered ID space, so it can never be mistaken for a
            // ParamID nor counted as a parameter binding (SC-002's 110).
            CHECK(button->getTag() >= Seraphis::UI::kSessionTagBase);
            CHECK(button->getListener() == static_cast<VSTGUI::IControlListener*>(sub));
        }
    }

    SECTION("a control with NO session-tag is left alone for the parent controller") {
        VSTGUI::SharedPointer<VSTGUI::COnOffButton> button = makeTaglessButton();
        const VSTGUI::UIAttributes empty;
        CHECK(sub->verifyView(button.get(), empty, nullptr) == button.get());
        CHECK(button->getTag() == -1);
        CHECK(button->getListener() == nullptr);
    }

    SECTION("the session-tag names are exactly the recognised set") {
        for (const SessionControlRow& row : sessionControlRows()) {
            CHECK(Seraphis::UI::sessionTagForName(row.attribute) == row.tag);
        }
        // A typo'd or superseded name is rejected, never clamped onto a
        // neighbouring control. The per-button "tabN"/"slotN" grammar died in
        // the 2026-08-04 segment-bar consolidation.
        CHECK(Seraphis::UI::sessionTagForName("tab0") ==
              Seraphis::UI::kInvalidSessionTag);
        CHECK(Seraphis::UI::sessionTagForName("tab7") ==
              Seraphis::UI::kInvalidSessionTag);
        CHECK(Seraphis::UI::sessionTagForName("slot0") ==
              Seraphis::UI::kInvalidSessionTag);
        CHECK(Seraphis::UI::sessionTagForName("Preset") ==
              Seraphis::UI::kInvalidSessionTag);
        CHECK(Seraphis::UI::sessionTagForName("") == Seraphis::UI::kInvalidSessionTag);
    }

    SECTION("willClose() puts the instance count back to 0 (the documented trap)") {
        REQUIRE(controller.subControllerInstanceCountForTest() == 1);
        controller.willClose(nullptr);
        CHECK(controller.subControllerInstanceCountForTest() == 0);
        // And every cached view pointer is dropped with it (C-7c).
        CHECK(controller.cloudView() == nullptr);
        CHECK(controller.drawer() == nullptr);
        for (Seraphis::UI::MacroRingKnob* ring : controller.macroRingsForTest()) {
            CHECK(ring == nullptr);
        }
    }

    controller.terminate();
}

// =============================================================================
// Regression (user bug 2026-08-04): "there are 7 buttons at the bottom of the
// plugin view, when I click on any of them, nothing happens."
// Root cause 1: Controller::setDrawerTab() switched the visibility of pages
// that are CLIPPED inside the 30 px collapsed strip and never opened the
// drawer, so a tab click had no visible effect at all.
// =============================================================================
TEST_CASE("Seraphis_DrawerTab_ClickOpensACollapsedDrawer", "[controller][phase11]") {
    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // The drawer must be the one the controller CACHES (createCustomView) - a
    // harness-local drawer would pass even while the cached pointer went unused.
    VSTGUI::UIAttributes drawerAttributes;
    drawerAttributes.setAttribute("origin", "0, 670");
    drawerAttributes.setAttribute("size", "1000, 30");
    VSTGUI::CView* rawDrawer =
        controller.createCustomView("DrawerContainer", drawerAttributes, nullptr, nullptr);
    REQUIRE(rawDrawer != nullptr);
    const VSTGUI::SharedPointer<VSTGUI::CView> ownedDrawer = VSTGUI::owned(rawDrawer);
    auto* drawer = dynamic_cast<DrawerContainer*>(rawDrawer);
    REQUIRE(drawer != nullptr);
    for (int i = 0; i < DrawerContainer::kTabCount; ++i) {
        drawer->addView(new VSTGUI::CViewContainer(kTabPageRect));
    }

    VSTGUI::IController* rawSub =
        controller.createSubController("SeraphisEdit", nullptr, nullptr);
    REQUIRE(rawSub != nullptr);
    const std::unique_ptr<VSTGUI::IController> ownedSub(rawSub);
    auto* sub = dynamic_cast<SeraphisEditSubController*>(rawSub);
    REQUIRE(sub != nullptr);

    REQUIRE_FALSE(drawer->isOpen());  // declared collapsed, as in the uidesc

    // The tab bar (2026-08-04): ONE segment control whose normalized value
    // carries the selected index over kSessionTabCount segments.
    VSTGUI::SharedPointer<VSTGUI::COnOffButton> tabBar = makeTaglessButton();
    VSTGUI::UIAttributes tabBarAttributes;
    setSessionTagAttribute(tabBarAttributes, "tabs");
    REQUIRE(sub->verifyView(tabBar.get(), tabBarAttributes, nullptr) == tabBar.get());

    tabBar->setValueNormalized(2.0f /
                               static_cast<float>(Seraphis::UI::kSessionTabCount - 1));
    sub->valueChanged(tabBar.get());
    // The handler must stay idempotent under a redundant second fire.
    sub->valueChanged(tabBar.get());

    CHECK(drawer->isOpen());
    CHECK(drawer->getViewSize() == DrawerContainer::kOpenRect);
    CHECK(drawer->activeTab() == 2);

    // A second tab while open: the page switches and the drawer STAYS open.
    tabBar->setValueNormalized(5.0f /
                               static_cast<float>(Seraphis::UI::kSessionTabCount - 1));
    sub->valueChanged(tabBar.get());
    CHECK(drawer->isOpen());
    CHECK(drawer->activeTab() == 5);

    // The handle keeps its unchanged toggle contract on top of a tab-opened
    // drawer: one toggle closes it.
    VSTGUI::SharedPointer<VSTGUI::COnOffButton> handle = makeTaglessButton();
    VSTGUI::UIAttributes handleAttributes;
    setSessionTagAttribute(handleAttributes, "drawerHandle");
    REQUIRE(sub->verifyView(handle.get(), handleAttributes, nullptr) == handle.get());
    sub->valueChanged(handle.get());
    CHECK_FALSE(drawer->isOpen());

    controller.terminate();
}

// =============================================================================
// Regression (user bug 2026-08-04), root cause 2: COnOffButton draws NOTHING
// without a bitmap (its draw() renders the background bitmap and nothing else),
// so a bitmap-less one is an invisible click target. The preset button, the
// mode toggle and the drawer handle all shipped as exactly that. Every
// COnOffButton in the uidesc must carry a bitmap; visible text controls belong
// to CTextButton (kick-style="false" for toggles - a kick-style one fires
// valueChanged twice per click and self-cancels any toggle handler).
// =============================================================================
TEST_CASE("Seraphis_Uidesc_HasNoInvisibleButtons", "[controller][phase11]") {
    const std::string uidescPath =
        std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc";
    std::ifstream in(uidescPath, std::ios::binary);
    REQUIRE(in.good());
    const std::string xml{std::istreambuf_iterator<char>(in),
                          std::istreambuf_iterator<char>()};
    REQUIRE_FALSE(xml.empty());

    std::size_t pos = 0;
    while ((pos = xml.find("<view", pos)) != std::string::npos) {
        const std::size_t end = xml.find('>', pos);
        REQUIRE(end != std::string::npos);
        const std::string element = xml.substr(pos, end - pos);
        if (element.find("\"COnOffButton\"") != std::string::npos) {
            INFO("bitmap-less COnOffButton is invisible: " << element);
            CHECK(element.find("bitmap=") != std::string::npos);
        }
        pos = end;
    }
}

// The [lifecycle] tag is load-bearing: .github/workflows/valgrind-nightly.yml
// invokes each binary as `"$BINDIR/$bin" '[lifecycle]'`, and T017 requires this
// case to run in that lane so a browser still open at willClose() is a REPORT
// rather than luck.
TEST_CASE("Seraphis_PresetButton_OpensTheBrowser",
          "[controller][phase11][lifecycle]") {
    const std::string uidescPath = std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc";

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    // The browser is only ever built over a live PresetManager - PresetBrowserView
    // exposes no accessor for the manager it was handed, so this precondition
    // plus togglePresetBrowser()'s own `presetManager_ == nullptr` early-out is
    // what "bound to presetManager_" is asserted through.
    REQUIRE(controller.presetManagerForTest() != nullptr);

    Krate::TestSupport::ensureVstguiInitialized();
    auto* editor = new VSTGUI::VST3Editor(&controller, "editor", uidescPath.c_str());
    Steinberg::IPlugView* view = editor;
    REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==
            Steinberg::kResultTrue);
    REQUIRE(editor->getFrame() != nullptr);
    // attached() -> open() -> didOpen(), which is what gave the controller the
    // frame togglePresetBrowser() needs.

    VSTGUI::IController* raw =
        controller.createSubController("SeraphisEdit", nullptr, nullptr);
    REQUIRE(raw != nullptr);
    const std::unique_ptr<VSTGUI::IController> ownedSubController(raw);
    auto* sub = dynamic_cast<SeraphisEditSubController*>(raw);
    REQUIRE(sub != nullptr);

    VSTGUI::SharedPointer<VSTGUI::COnOffButton> button = makeTaglessButton();
    VSTGUI::UIAttributes attributes;
    setSessionTagAttribute(attributes, "preset");
    REQUIRE(sub->verifyView(button.get(), attributes, nullptr) == button.get());
    REQUIRE(button->getTag() == Seraphis::UI::kPresetButtonTag);
    REQUIRE(button->getListener() == static_cast<VSTGUI::IControlListener*>(sub));

    REQUIRE(controller.presetBrowserForTest() == nullptr);
    REQUIRE(countPresetBrowsers(editor->getFrame()) == 0);

    // --- first activation: the browser appears IN THE FRAME and is open ------
    button->setValue(1.0f);
    button->getListener()->valueChanged(button.get());

    REQUIRE(controller.presetBrowserForTest() != nullptr);
    CHECK(controller.presetBrowserForTest()->isOpen());
    CHECK(countPresetBrowsers(editor->getFrame()) == 1);

    // --- second activation: it is GONE, not merely hidden --------------------
    button->setValue(0.0f);
    button->getListener()->valueChanged(button.get());

    CHECK(controller.presetBrowserForTest() == nullptr);
    CHECK(countPresetBrowsers(editor->getFrame()) == 0);

    view->removed();  // -> willClose()
    view->release();
    controller.terminate();
}

// =============================================================================
// T018 - the editor-open refcount and the gesture throttle (SC-026, SC-027)
// =============================================================================
// Both cases read the controller's SEND-RECORDING seams
// (lastSentEditMessageForTest / editMessageSendCountForTest), which record what
// sendEditMessage() was ASKED to send BEFORE its allocateMessage() connection
// test. That ordering is the only reason either criterion is observable at all
// here: a headless Controller has no host context, so allocateMessage() returns
// null and nothing is ever delivered (controller.h's sendEditMessage banner).

namespace {

/// Hand one EditMessage to a Processor exactly as the host would deliver it -
/// one binary attribute on the "SeraphisEdit" message ID. The same helper shape
/// integration/partial_edit_test.cpp and unit/state_v3_test.cpp use, repeated
/// here rather than shared because the three TUs are otherwise independent.
void forwardToProcessor(Seraphis::Processor& processor, const Seraphis::UI::EditMessage& m) {
    auto message = Steinberg::owned(new Steinberg::Vst::HostMessage());
    message->setMessageID(Seraphis::UI::kSeraphisEditMessageId);
    Steinberg::Vst::IAttributeList* attributes = message->getAttributes();
    REQUIRE(attributes != nullptr);
    REQUIRE(attributes->setBinary(Seraphis::UI::kSeraphisEditAttributeId, &m,
                                  static_cast<Steinberg::uint32>(sizeof(m)))
            == Steinberg::kResultOk);
    REQUIRE(processor.notify(message) == Steinberg::kResultOk);
}

}  // namespace

TEST_CASE("Seraphis_MultiEditor_RefcountGatesCorrectly", "[controller][phase11]") {
    constexpr double kSampleRate = 48000.0;
    constexpr Steinberg::int32 kBlock = 512;

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    // The producer starts CLOSED (C-2 clause 6): no editor has opened yet.
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->cloudFramePublishAttemptCountForTest() == 0u);

    const std::size_t base = controller.editMessageSendCountForTest();
    REQUIRE(controller.editorOpenCountForTest() == 0);

    // --- 0 -> 1: EXACTLY ONE open message -----------------------------------
    controller.didOpen(nullptr);
    REQUIRE(controller.editorOpenCountForTest() == 1);
    REQUIRE(controller.editMessageSendCountForTest() == base + 1u);
    {
        const Seraphis::UI::EditMessage& m = controller.lastSentEditMessageForTest();
        CHECK(static_cast<unsigned>(m.kind) == 0u);
        CHECK(m.a == 1.0f);
        forwardToProcessor(*fx.proc, m);
    }

    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    const std::size_t afterFirstOpen = fx.proc->cloudFramePublishAttemptCountForTest();
    REQUIRE(afterFirstOpen > 0u);

    // --- 1 -> 2: a SECOND editor sends NOTHING ------------------------------
    controller.didOpen(nullptr);
    CHECK(controller.editorOpenCountForTest() == 2);
    CHECK(controller.editMessageSendCountForTest() == base + 1u);

    // --- 2 -> 1: the first editor closes, and STILL sends nothing -----------
    controller.willClose(nullptr);
    CHECK(controller.editorOpenCountForTest() == 1);
    CHECK(controller.editMessageSendCountForTest() == base + 1u);

    // ...so the producer's gate is still open and frames keep publishing. This
    // is SC-026's whole point: a per-view close message would have shut the
    // second editor's feed off here.
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    CHECK(fx.proc->cloudFramePublishAttemptCountForTest() == afterFirstOpen + 2u);

    // --- 1 -> 0: EXACTLY ONE close message ----------------------------------
    controller.willClose(nullptr);
    CHECK(controller.editorOpenCountForTest() == 0);
    REQUIRE(controller.editMessageSendCountForTest() == base + 2u);
    {
        const Seraphis::UI::EditMessage& m = controller.lastSentEditMessageForTest();
        CHECK(static_cast<unsigned>(m.kind) == 0u);
        CHECK(m.a == 0.0f);
        forwardToProcessor(*fx.proc, m);
    }

    const std::size_t afterClose = fx.proc->cloudFramePublishAttemptCountForTest();
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    CHECK(fx.proc->cloudFramePublishAttemptCountForTest() == afterClose);

    SECTION("an UNPAIRED willClose() floors at 0 and sends nothing") {
        const std::size_t sends = controller.editMessageSendCountForTest();
        controller.willClose(nullptr);
        CHECK(controller.editorOpenCountForTest() == 0);
        CHECK(controller.editMessageSendCountForTest() == sends);
        // ...and the NEXT open is still a 0 -> 1 transition, i.e. the floor did
        // not leave the counter at -1 where the next didOpen would be silent.
        controller.didOpen(nullptr);
        CHECK(controller.editorOpenCountForTest() == 1);
        CHECK(controller.editMessageSendCountForTest() == sends + 1u);
        CHECK(controller.lastSentEditMessageForTest().a == 1.0f);
    }

    SECTION("terminate() resets the count REGARDLESS of its prior value") {
        controller.didOpen(nullptr);
        controller.didOpen(nullptr);
        REQUIRE(controller.editorOpenCountForTest() == 2);

        controller.terminate();
        CHECK(controller.editorOpenCountForTest() == 0);

        // Proof it really reset: the next didOpen is a 0 -> 1 transition and
        // therefore sends again. A terminate() that left the count at 2 would
        // leave this silent.
        const std::size_t sends = controller.editMessageSendCountForTest();
        controller.didOpen(nullptr);
        CHECK(controller.editMessageSendCountForTest() == sends + 1u);
        CHECK(controller.lastSentEditMessageForTest().a == 1.0f);
        return;  // already terminated
    }

    controller.terminate();
}

TEST_CASE("Seraphis_EditThrottle_FlushesFinalValue", "[controller][phase11]") {
    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // A ratio drag on slot 1, partial 3 - the shape CloudView's vertical drag
    // produces. Only the payload matters here; the criterion is about COUNTS.
    const auto moveAt = [](int i) {
        Seraphis::UI::EditMessage m{};
        m.kind = static_cast<std::uint8_t>(1);
        m.slot = static_cast<std::uint8_t>(1);
        m.index = static_cast<std::uint16_t>(3);
        m.a = 1.0f + 0.0005f * static_cast<float>(i);
        m.b = 0.5f;
        return m;
    };

    SECTION("200 moves in one window: at most one throttled send, plus the flush") {
        constexpr int kMoves = 200;

        const std::size_t base = controller.editMessageSendCountForTest();
        const auto start = std::chrono::steady_clock::now();

        controller.beginEditGesture();
        Seraphis::UI::EditMessage last{};
        for (int i = 0; i < kMoves; ++i) {
            last = moveAt(i);
            controller.onEditGestureValue(last);
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;

        const std::size_t throttled = controller.editMessageSendCountForTest() - base;

        // FR-048's budget is one message per 33 ms window. 200 trivial calls sit
        // inside a single window on any machine this suite runs on, so the
        // budget is 1 - but the elapsed term is carried anyway rather than
        // asserting a wall-clock assumption: it is what keeps the criterion
        // honest (never LOOSER than the rule) if the loop really did cross a
        // boundary, and it can never excuse a build that sends per move.
        const auto windows =
            static_cast<std::size_t>(elapsed / Seraphis::kEditThrottleInterval);
        INFO("throttled sends " << throttled << " over " << windows << " full window(s)");
        CHECK(throttled <= 1u + windows);
        // NON-VACUITY: a throttle that swallowed EVERY move would satisfy the
        // bound above and drop the first value. The first move is never withheld
        // (beginGesture resets lastSend to the clock epoch).
        CHECK(throttled >= 1u);
        CHECK(throttled < static_cast<std::size_t>(kMoves));

        // --- the terminal flush: EXACTLY ONE more, carrying the LAST value ---
        controller.endEditGesture();
        CHECK(controller.editMessageSendCountForTest() == base + throttled + 1u);

        const Seraphis::UI::EditMessage& flushed = controller.lastSentEditMessageForTest();
        CHECK(static_cast<unsigned>(flushed.kind) == static_cast<unsigned>(last.kind));
        CHECK(static_cast<unsigned>(flushed.slot) == static_cast<unsigned>(last.slot));
        CHECK(static_cast<unsigned>(flushed.index) == static_cast<unsigned>(last.index));
        // EXACT: the flush must carry the payload it was handed, not a rounded
        // or re-derived one.
        CHECK(flushed.a == last.a);
        CHECK(flushed.b == last.b);
        // ...and the value really is the LAST one, not the first (which is what
        // the single throttled send above already carried).
        CHECK(flushed.a != moveAt(0).a);
    }

    SECTION("the flush is unconditional - it fires even right after a throttled send") {
        controller.beginEditGesture();
        const Seraphis::UI::EditMessage only = moveAt(7);
        controller.onEditGestureValue(only);  // sends immediately (epoch lastSend)
        const std::size_t afterMove = controller.editMessageSendCountForTest();

        controller.endEditGesture();
        CHECK(controller.editMessageSendCountForTest() == afterMove + 1u);
        CHECK(controller.lastSentEditMessageForTest().a == only.a);
    }

    SECTION("a gesture that staged NO value flushes nothing") {
        // The degenerate case the plan's `hasPending || active` form would send
        // a DEFAULT-CONSTRUCTED EditMessage for - and a default EditMessage is
        // kind 0 with a == 0, i.e. the editor-CLOSE message, which would shut the
        // cloud-frame producer down on an empty click.
        const std::size_t base = controller.editMessageSendCountForTest();
        controller.beginEditGesture();
        controller.endEditGesture();
        CHECK(controller.editMessageSendCountForTest() == base);
    }

    controller.terminate();
}

// =============================================================================
// T019 - the SHIPPED editor.uidesc really builds the organism-first layout
// =============================================================================
// SC-004. Everything above this line drives the classes DIRECTLY; this case is
// the only one that asserts about the document, so it is the only one that can
// catch the hazard the Phase 8 uidesc banner named: a creator TU that failed to
// link, or a `custom-view-name` that no longer matches, yields STOCK views and
// every other criterion in this file still passes.
//
// It runs its OWN open/close loop rather than calling exerciseEditorLifecycle,
// because arms 1-3 must all read the LIVE frame while the editor is open, and
// FR-027's arm must be re-read on EVERY cycle (the failure mode is an Edit mode
// that survives a close and is inherited by the next editor).

namespace {

/// Recursive count of views of type `T` anywhere under `container`.
template <typename T>
[[nodiscard]] int countViewsOfType(VSTGUI::CViewContainer* container) {
    if (container == nullptr) {
        return 0;
    }
    int found = 0;
    const std::uint32_t count = container->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        VSTGUI::CView* child = container->getView(i);
        if (child == nullptr) {
            continue;
        }
        if (dynamic_cast<T*>(child) != nullptr) {
            ++found;
        }
        if (VSTGUI::CViewContainer* nested = child->asViewContainer()) {
            found += countViewsOfType<T>(nested);
        }
    }
    return found;
}

/// The drawer's tab names, IN SEGMENT ORDER, read back off the ONE
/// IconSegmentButton tab bar (2026-08-04 consistency pass) - identified by the
/// session tag verifyView assigned, never by position, so a re-ordered or
/// mis-titled bar shows up as a different LIST rather than as a silent pass.
[[nodiscard]] std::vector<std::string> drawerTabTitles(DrawerContainer* drawer) {
    std::vector<std::string> titles;
    if (drawer == nullptr) {
        return titles;
    }
    const std::uint32_t count = drawer->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        auto* bar = dynamic_cast<Krate::Plugins::IconSegmentButton*>(drawer->getView(i));
        if (bar == nullptr || bar->getTag() != Seraphis::UI::kTabBarTag) {
            continue;
        }
        const std::string joined = bar->getSegmentNames();
        std::size_t start = 0;
        while (start <= joined.size()) {
            const std::size_t comma = joined.find(',', start);
            if (comma == std::string::npos) {
                titles.push_back(joined.substr(start));
                break;
            }
            titles.push_back(joined.substr(start, comma - start));
            start = comma + 1;
        }
    }
    return titles;
}

}  // namespace

TEST_CASE("Seraphis_Phase11_CustomViews_AreInstantiated",
          "[controller][phase11][lifecycle]") {
    const std::string uidescPath = std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc";

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    Krate::TestSupport::ensureVstguiInitialized();

    const std::vector<std::string> kExpectedTabTitles{"Cloud", "Morph",    "Body",
                                                      "Atmos", "Aether",   "FX",
                                                      "Life/Env"};

    constexpr int kCycles = 3;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        INFO("editor open/close cycle " << cycle);

        auto* editor = new VSTGUI::VST3Editor(&controller, "editor", uidescPath.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==
                Steinberg::kResultTrue);
        VSTGUI::CFrame* frame = editor->getFrame();
        REQUIRE(frame != nullptr);
        REQUIRE(frame->getNbViews() > 0);

        // --- ARM 1: instantiation, counted by dynamic_cast --------------------
        // Not by view COUNT: a stock CView standing in for CloudView would leave
        // the total untouched and only this cast can tell the difference.
        CHECK(countViewsOfType<CloudView>(frame) == 1);
        CHECK(countViewsOfType<DrawerContainer>(frame) == 1);
        CHECK(countViewsOfType<Seraphis::UI::MacroRingKnob>(frame) == 5);

        // ...and the controller cached exactly the same three things, i.e. the
        // createCustomView / verifyView halves agree with the built tree.
        CloudView* cloud = controller.cloudView();
        DrawerContainer* drawer = controller.drawer();
        REQUIRE(cloud != nullptr);
        REQUIRE(drawer != nullptr);
        for (Seraphis::UI::MacroRingKnob* ring : controller.macroRingsForTest()) {
            CHECK(ring != nullptr);
        }

        // --- ARM 3 (i), FR-006: the cloud view is child INDEX 0 of the root ---
        // A build that put it last would hide every ring behind it and still pass
        // arm 1. The same walk proves D-4: the root that owns it is a DIRECT child
        // of the frame, which is what makes FR-023 / FR-024's rects absolute.
        //
        // WALKED DOWN FROM THE FRAME, NEVER UP VIA getParentView(). CView's
        // parent pointer is set in CView::attached() (cview.cpp:446-451), and
        // CViewContainer::addView() calls attached() only `if (isAttached ())`
        // (cviewcontainer.cpp:526-530). Headless, IPlugView::attached(nullptr,
        // ...) reaches CFrame::open(nullptr, ...), which returns false on its
        // first line (`if (!systemWin || isAttached ()) return false;`,
        // cframe.cpp:206-209) WITHOUT ever calling attached(this) - so the frame
        // is never attached and EVERY view in this tree has a null parent
        // pointer. An up-walk here asserts about the platform window, not about
        // the document. The down-walk pins exactly the same three facts.
        // THE ROOT IS IDENTIFIED, NOT ASSUMED TO BE getView(0) OF A ONE-CHILD
        // FRAME. An earlier revision asserted `frame->getNbViews() == 1u` and
        // took `getView(0)`; that is a property of the RELEASE build only.
        // `VSTGUI_LIVE_EDITING` is defined for $<CONFIG:Debug> (the ASan lane
        // SC-005 and SC-023 require is a Debug build), and with it VST3Editor
        // adds a second child to the frame - so the old form failed with
        // `2 == 1` in exactly the configuration the other two criteria are
        // measured in, while the FACT it was standing in for was untouched.
        //
        // The fact is: there is EXACTLY ONE frame-level subtree containing the
        // cloud view, the cloud view is that container's child INDEX 0 (FR-006 -
        // a build that put it last would hide every ring behind it and still
        // pass arm 1), and the drawer is a direct child of the same container
        // (D-4, which is what makes FR-023 / FR-024's rects absolute). All three
        // are asserted below, and the uniqueness REQUIRE is stricter than the
        // count it replaced: two roots both holding a cloud view now fail.
        VSTGUI::CViewContainer* root = nullptr;
        int rootsHoldingTheCloud = 0;
        for (std::uint32_t c = 0; c < frame->getNbViews(); ++c) {
            VSTGUI::CView* child = frame->getView(c);
            VSTGUI::CViewContainer* candidate =
                (child != nullptr) ? child->asViewContainer() : nullptr;
            if ((candidate != nullptr) && candidate->isChild(cloud)) {
                ++rootsHoldingTheCloud;
                root = candidate;
            }
        }
        REQUIRE(rootsHoldingTheCloud == 1);
        REQUIRE(root != nullptr);
        CHECK(dynamic_cast<CloudView*>(root->getView(0)) == cloud);  // FR-006
        CHECK(root->isChild(cloud));                                 // direct child
        CHECK(root->isChild(drawer));                                // D-4

        // --- ARM 3 (iv), FR-023 / FR-024: C-1's RECTS, ON THE BUILT TREE ------
        // SC-020(c)/(e) assert rects on views the TEST constructs
        // (makeDrawerHarness), so they are statements about DrawerContainer's
        // toggle and about a rect this file passed to the constructor - never
        // about what `editor.uidesc` actually declares. A uidesc that placed the
        // cloud view at any other origin, or nested the drawer one level deeper
        // (making its absolute rect an offset one), passed every one of those
        // arms. These four compare the UIDESC-BUILT tree against C-1's table.
        //
        // The open rect is checked THROUGH setOpen() on the built drawer rather
        // than by re-reading the constant, because that is the only place the
        // uidesc's declared collapsed rect and the class's open rect have to
        // agree on a common origin.
        CHECK(root->getViewSize() == kTemplateRootRect);       // (0, 0, 1000, 700)
        CHECK(cloud->getViewSize() == kCloudViewRect);         // (0, 32, 1000, 670)
        CHECK(drawer->getViewSize() == kSpecDrawerCollapsedRect);  // (0, 670, 1000, 700)
        drawer->setOpen(true);
        CHECK(drawer->getViewSize() == kSpecDrawerOpenRect);   // (0, 420, 1000, 700)
        CHECK(cloud->getViewSize() == kCloudViewRect);         // FR-024: NOT resized
        drawer->setOpen(false);
        REQUIRE_FALSE(drawer->isOpen());  // restored before the tab arms below

        // --- ARM 3 (iii), FR-027: a freshly opened editor is in Observe -------
        CHECK(cloud->mode() == CloudView::Mode::Observe);

        // --- ARM 2, FR-022: the seven tab titles, in order --------------------
        // kTabCount == 7 is a static_assert above and is NOT a substitute for
        // this: it cannot see a wrong, swapped or missing LABEL.
        CHECK(drawerTabTitles(drawer) == kExpectedTabTitles);

        // --- ARM 3 (ii), FR-025: exactly one page visible, and it is page i ---
        REQUIRE(drawer->tabPageCount() == DrawerContainer::kTabCount);
        for (int active = 0; active < DrawerContainer::kTabCount; ++active) {
            INFO("active tab " << active);
            drawer->setActiveTab(active);
            int visibleCount = 0;
            int visibleIndex = -1;
            for (int page = 0; page < DrawerContainer::kTabCount; ++page) {
                VSTGUI::CViewContainer* container = drawer->tabPage(page);
                REQUIRE(container != nullptr);
                if (container->isVisible()) {
                    ++visibleCount;
                    visibleIndex = page;
                }
            }
            CHECK(visibleCount == 1);
            CHECK(visibleIndex == active);
        }

        view->removed();  // -> willClose()
        view->release();

        // C-7c: every cached pointer is dropped with the frame, so the next cycle
        // starts from nullptr rather than from a dangling view.
        CHECK(controller.cloudView() == nullptr);
        CHECK(controller.drawer() == nullptr);
    }

    controller.terminate();
}

// =============================================================================
// T024 - the view-surface bound (SC-022(a), FR-026) and the view half of FR-021
// =============================================================================
// TWO CRITERIA, TWO INSTRUMENTS, AND THE ORDER MATTERS.
//
// SC-022(a) / FR-026 say the phase's custom-view surface is EXACTLY three CView
// classes - CloudView, MacroRingKnob, DrawerContainer - and that
// SeraphisEditSubController is NOT one of them. Neither of the criteria already
// in this file can enforce that:
//
//   * SC-004 (Seraphis_Phase11_CustomViews_AreInstantiated, above) counts
//     INSTANCES in the built frame. A fourth view class the uidesc never
//     references is invisible to it.
//   * A pure token scan cannot resolve TRANSITIVE bases. MacroRingKnob is a
//     CView only via Krate::Plugins::ArcKnob (arc_knob.h:49) -> CKnobBase ->
//     CControl -> CView, a chain that lives entirely outside src/ui/; and a
//     fourth class written `: public VSTGUI::CTextLabel` is just as much a CView
//     while naming none of those tokens.
//
// So arm 1 is a COMPILE-TIME assertion set over the named types - the only
// instrument that resolves a transitive base - and arm 2 is a source scan used
// as a TRIPWIRE: it fails on any base name that is not on an enumerated
// allowlist, so an unknown base is a RED TEST rather than a silent pass. Adding
// a fourth view class therefore forces either a visible spec amendment (a new
// allowlist entry AND a new static_assert) or a failing build.
//
// HOW ARM 1's RED WAS ESTABLISHED. The procedure the task prescribes is:
// temporarily add a fourth class under src/ui/ deriving from VSTGUI::CTextLabel,
// confirm the assertion set and the allowlist both go red, revert. That
// procedure is destructive and leaves nothing behind, so the PERMANENT residue
// of it is the pair below: the static_assert that VSTGUI::CTextLabel is itself a
// CView (which is what makes such a fourth class a genuine fourth view surface,
// and is_base_of transitive over it), and the scanner's own negative control,
// which parses a synthetic source containing exactly that rogue declaration and
// requires the allowlist check to reject it. Both run on every build.
// =============================================================================

// -----------------------------------------------------------------------------
// ARM 1 - compile-time. Exactly these three are CViews; the sub-controller is not.
// -----------------------------------------------------------------------------
// (The MacroRingKnob pair at the top of this file is T014's and is about FR-020's
// ArcKnob branch; these are FR-026's, and they are deliberately restated here so
// the three-plus-one bound reads as one block.)
static_assert(std::is_base_of_v<VSTGUI::CView, Seraphis::UI::CloudView>,
              "FR-026: CloudView is one of the three Phase 11 CView classes");
static_assert(std::is_base_of_v<VSTGUI::CView, Seraphis::UI::MacroRingKnob>,
              "FR-026: MacroRingKnob is one of the three Phase 11 CView classes");
static_assert(std::is_base_of_v<VSTGUI::CView, Seraphis::UI::DrawerContainer>,
              "FR-026: DrawerContainer is one of the three Phase 11 CView classes");
static_assert(!std::is_base_of_v<VSTGUI::CView, Seraphis::UI::SeraphisEditSubController>,
              "FR-026 / C-7a: the sub-controller is a DelegationController, NOT a "
              "view - it must not count against the three-view bound");

// The instrument's own validation: `std::is_base_of_v` really does see through a
// multi-level chain that names no CView token. A fourth src/ui/ class written
// `: public VSTGUI::CTextLabel` would be a CView by exactly this reasoning - and
// `VSTGUI::CTextLabel` is deliberately ABSENT from arm 2's allowlist.
static_assert(std::is_base_of_v<VSTGUI::CView, VSTGUI::CTextLabel>,
              "arm 1's instrument must resolve TRANSITIVE bases "
              "(CTextLabel -> CParamDisplay -> CControl -> CView)");

namespace {

// -----------------------------------------------------------------------------
// ARM 2's scanner. Same instrument integration/ui_perf_test.cpp:174-283 carries
// (itself Phase 10's, effects_perf_test.cpp:619-759), re-pointed at src/ui/*.h
// and extended with a base-clause parser. It is re-declared here rather than
// hoisted into a shared header for the same reason that one records: each lives
// in its TU's anonymous namespace, and the CORPUS is the criterion.
//
// Comments and string-literal CONTENTS are stripped first, so the gate cannot be
// tripped by prose - this very comment names VSTGUI::CTextLabel, and this TU is
// not in the corpus.
// -----------------------------------------------------------------------------

/// Source with `//` comments, `/* */` comments and string-literal CONTENTS
/// removed, newlines preserved. Character literals are deliberately NOT tracked
/// (effects_perf_test.cpp:619-622's reason): no scanned file holds a comment
/// opener in one, and treating `'` as a state change would misparse `1'000`.
[[nodiscard]] std::string strippedUiSource(const std::string& src) {
    enum class State : std::uint8_t { Code, LineComment, BlockComment, StringLiteral };
    State state = State::Code;
    std::string out;
    out.reserve(src.size());

    for (std::size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        const char next = (i + 1u < src.size()) ? src[i + 1u] : '\0';

        switch (state) {
        case State::Code:
            if ((c == '/') && (next == '/')) {
                state = State::LineComment;
                ++i;
            } else if ((c == '/') && (next == '*')) {
                state = State::BlockComment;
                ++i;
            } else if (c == '"') {
                state = State::StringLiteral;
            } else {
                out.push_back(c);
            }
            break;
        case State::LineComment:
            if (c == '\n') {
                state = State::Code;
                out.push_back(c);
            }
            break;
        case State::BlockComment:
            if ((c == '*') && (next == '/')) {
                state = State::Code;
                ++i;
            } else if (c == '\n') {
                out.push_back(c);
            }
            break;
        case State::StringLiteral:
            if (c == '\\') {
                ++i;  // skip the escaped character, whatever it is
            } else if (c == '"') {
                state = State::Code;
            }
            break;
        }
    }
    return out;
}

[[nodiscard]] std::size_t countUiOccurrences(const std::string& haystack, const char* needle) {
    std::size_t n = 0;
    const std::string pattern(needle);
    if (pattern.empty()) {
        return 0;
    }
    for (std::size_t at = haystack.find(pattern); at != std::string::npos;
         at = haystack.find(pattern, at + pattern.size())) {
        ++n;
    }
    return n;
}

[[nodiscard]] constexpr bool isWordChar(char c) noexcept {
    return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
           ((c >= '0') && (c <= '9')) || (c == '_');
}

/// Word characters plus `:`, so a QUALIFIED base name (`VSTGUI::CView`) is read
/// as one token. Deliberately not used for the derived name, where a `X: public`
/// with no space would otherwise swallow the colon.
[[nodiscard]] constexpr bool isQualifiedNameChar(char c) noexcept {
    return isWordChar(c) || (c == ':');
}

[[nodiscard]] constexpr bool isSpaceChar(char c) noexcept {
    return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r') || (c == '\f') || (c == '\v');
}

void skipSpaces(const std::string& s, std::size_t& i) noexcept {
    while ((i < s.size()) && isSpaceChar(s[i])) {
        ++i;
    }
}

[[nodiscard]] std::string readToken(const std::string& s, std::size_t& i, bool qualified) {
    const std::size_t start = i;
    while ((i < s.size()) && (qualified ? isQualifiedNameChar(s[i]) : isWordChar(s[i]))) {
        ++i;
    }
    return s.substr(start, i - start);
}

/// The word immediately preceding `at`, or "". Used to reject `enum class E : T`,
/// whose `: T` is a fixed underlying type and not a base clause.
[[nodiscard]] std::string precedingWord(const std::string& s, std::size_t at) {
    std::size_t i = at;
    while ((i > 0u) && isSpaceChar(s[i - 1u])) {
        --i;
    }
    const std::size_t end = i;
    while ((i > 0u) && isWordChar(s[i - 1u])) {
        --i;
    }
    return s.substr(i, end - i);
}

struct BaseClause {
    std::string file;
    std::string derived;
    std::string base;
};

/// Every `class X : ... B` / `struct X : ... B` in `code`, one entry per base.
/// Forward declarations (`class Controller;`), base-less PODs (`struct
/// EditMessage {`) and `enum class` all produce nothing.
void parseBaseClauses(const std::string& file, const std::string& code,
                      std::vector<BaseClause>& out) {
    const std::array<const char*, 2> keywords{"class", "struct"};

    for (std::size_t p = 0; p < code.size(); ++p) {
        std::size_t kwLen = 0;
        for (const char* kw : keywords) {
            const std::size_t n = std::char_traits<char>::length(kw);
            if (code.compare(p, n, kw) == 0) {
                kwLen = n;
                break;
            }
        }
        if (kwLen == 0u) {
            continue;
        }
        // Whole-word only: `CLASS_METHODS` and `subclass` must not match.
        if ((p > 0u) && isWordChar(code[p - 1u])) {
            continue;
        }
        if (((p + kwLen) < code.size()) && isWordChar(code[p + kwLen])) {
            continue;
        }
        if (precedingWord(code, p) == "enum") {
            continue;
        }

        std::size_t i = p + kwLen;
        skipSpaces(code, i);
        std::string derived = readToken(code, i, false);
        if (derived.empty()) {
            continue;
        }
        skipSpaces(code, i);
        // An export macro (`class VSTGUI_API CView`) sits between the keyword and
        // the name: two identifiers in a row means the first one was the macro.
        if ((i < code.size()) && isWordChar(code[i])) {
            derived = readToken(code, i, false);
            skipSpaces(code, i);
        }
        if ((i >= code.size()) || (code[i] != ':')) {
            continue;  // forward declaration, or a definition with no bases
        }
        ++i;

        while (i < code.size()) {
            skipSpaces(code, i);
            const std::string token = readToken(code, i, true);
            if (token.empty()) {
                break;
            }
            if ((token == "public") || (token == "protected") || (token == "private") ||
                (token == "virtual")) {
                continue;
            }
            out.push_back(BaseClause{.file = file, .derived = derived, .base = token});
            skipSpaces(code, i);
            if ((i < code.size()) && (code[i] == ',')) {
                ++i;  // a second base in the same clause
                continue;
            }
            break;
        }
    }
}

struct UiHeaderScan {
    std::size_t filesScanned = 0;
    std::size_t filesMissing = 0;
    /// Non-comment bytes actually examined. Asserted non-trivial so a build that
    /// resolved the paths but read nothing cannot report "no unknown bases"
    /// about an empty corpus.
    std::size_t codeBytes = 0;
    /// Occurrences of a token that MUST be present, i.e. the witness that the
    /// corpus really is src/ui/ and not some readable but wrong directory.
    std::size_t witnesses = 0;
    std::vector<BaseClause> clauses;
    std::string firstUnreadable;
};

[[nodiscard]] UiHeaderScan scanUiHeaders(const std::vector<std::string>& files,
                                         const char* witness) {
    UiHeaderScan scan{};
    for (const std::string& path : files) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            ++scan.filesMissing;
            if (scan.firstUnreadable.empty()) {
                scan.firstUnreadable = path;
            }
            continue;
        }
        const std::string raw((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
        const std::string code = strippedUiSource(raw);
        ++scan.filesScanned;
        scan.codeBytes += code.size();
        scan.witnesses += countUiOccurrences(code, witness);
        parseBaseClauses(path, code, scan.clauses);
    }
    return scan;
}

/// ENUMERATED FROM THE DIRECTORY, never hard-coded: FR-026 is a statement about
/// the whole of src/ui/, so a hard-coded list would silently stop covering a
/// header a later task adds. The `>= 5` floor at the call site is what keeps a
/// wrong or empty directory red rather than vacuously green.
///
/// KNOWN BLIND SPOT, stated rather than papered over: the corpus is `*.h` only,
/// as the task specifies, so a view class defined ENTIRELY inside one of the
/// three src/ui/ .cpp files would be invisible to this arm. It is not a hole
/// today - none of those three TUs declares a class or struct at all - and arm 1
/// would still catch such a class the moment anything named it from a header.
/// Widening the corpus to `.cpp` is the fix if that ever changes; allowlisting is
/// not.
[[nodiscard]] std::vector<std::string> uiHeaderPaths() {
    std::vector<std::string> files;
    std::error_code ec;
    const std::filesystem::path uiDir =
        std::filesystem::path(std::string(SERAPHIS_SRC_DIR)) / "ui";
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(uiDir, ec)) {
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc)) {
            continue;
        }
        if (entry.path().extension().string() == ".h") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());  // stable failure messages
    return files;
}

/// THE ALLOWLIST. Five entries and no more: the three view bases FR-026 sanctions,
/// the sub-controller's DelegationController, and the ViewCreatorAdapter the
/// ring's creator struct derives from (C-7a). Anything else - `VSTGUI::CTextLabel`,
/// `VSTGUI::CControl`, a second container type - is an UNKNOWN BASE and fails.
/// Extending this list is a spec amendment, not a test fix.
[[nodiscard]] std::vector<std::string> allowedBaseNames() {
    return {"VSTGUI::CView", "VSTGUI::CViewContainer", "Krate::Plugins::ArcKnob",
            "VSTGUI::DelegationController", "VSTGUI::ViewCreatorAdapter"};
}

/// The three bases that make a class a Phase 11 CUSTOM VIEW. Counted so "exactly
/// three" is asserted as a number rather than inferred from the allowlist.
[[nodiscard]] bool isViewBase(const std::string& base) {
    return (base == "VSTGUI::CView") || (base == "VSTGUI::CViewContainer") ||
           (base == "Krate::Plugins::ArcKnob");
}

[[nodiscard]] bool isAllowedBase(const std::string& base) {
    const std::vector<std::string> allowed = allowedBaseNames();
    return std::find(allowed.begin(), allowed.end(), base) != allowed.end();
}

/// The base recorded for `derived`, or "" when the scan never saw that class.
[[nodiscard]] std::string baseOf(const UiHeaderScan& scan, const char* derived) {
    for (const BaseClause& clause : scan.clauses) {
        if (clause.derived == derived) {
            return clause.base;
        }
    }
    return {};
}

}  // namespace

TEST_CASE("Seraphis_Phase11_ViewSurface_IsExactlyThreePlusSubController", "[ui][phase11]") {
    SECTION("arm 1 - the compile-time bound, restated so the runner shows it") {
        // These mirror the static_asserts above one for one. The static_asserts
        // are the gate (a violation is a BUILD failure); these exist so the
        // criterion is visible in the test report rather than only in a compiler
        // diagnostic. Bound to named `const bool`s first so no compiler sees a
        // constant expression as a branch condition.
        const bool cloudViewIsAView = std::is_base_of_v<VSTGUI::CView, CloudView>;
        const bool ringIsAView = std::is_base_of_v<VSTGUI::CView, Seraphis::UI::MacroRingKnob>;
        const bool drawerIsAView = std::is_base_of_v<VSTGUI::CView, DrawerContainer>;
        const bool subControllerIsAView =
            std::is_base_of_v<VSTGUI::CView, SeraphisEditSubController>;

        CHECK(cloudViewIsAView);
        CHECK(ringIsAView);
        CHECK(drawerIsAView);
        CHECK_FALSE(subControllerIsAView);

        // ... and the instrument really resolves a transitive chain, which is
        // what makes a `: public VSTGUI::CTextLabel` fourth class detectable.
        const bool textLabelIsAView = std::is_base_of_v<VSTGUI::CView, VSTGUI::CTextLabel>;
        const bool ringIsAnArcKnob =
            std::is_base_of_v<Krate::Plugins::ArcKnob, Seraphis::UI::MacroRingKnob>;
        CHECK(textLabelIsAView);
        CHECK(ringIsAnArcKnob);
    }

    SECTION("arm 2's parser - the scanner's OWN negative control, run FIRST") {
        // A parser that silently found nothing would let the allowlist clause
        // pass vacuously forever. This synthetic corpus contains one ROGUE
        // declaration (the exact fourth-class shape FR-026 exists to catch), two
        // legal ones, and four shapes that must produce NO clause at all.
        const std::string probe =
            "namespace Probe {\n"
            "// class Commented : public VSTGUI::CEvil {};\n"
            "/* class Blocked : public VSTGUI::CEvil {}; */\n"
            "const char* s = \"class Quoted : public VSTGUI::CEvil\";\n"
            "class Rogue : public VSTGUI::CTextLabel {};\n"
            "class Good : public VSTGUI::CView {};\n"
            "struct Creator : VSTGUI::ViewCreatorAdapter {};\n"
            "enum class Mode : std::uint8_t { A, B };\n"
            "class Fwd;\n"
            "struct Pod { int a; };\n"
            "}\n";

        std::vector<BaseClause> clauses;
        parseBaseClauses("probe", strippedUiSource(probe), clauses);

        // Exactly the three real declarations - the commented, quoted, enum,
        // forward-declared and base-less shapes all produce nothing.
        REQUIRE(clauses.size() == 3u);
        CHECK(clauses[0].derived == "Rogue");
        CHECK(clauses[0].base == "VSTGUI::CTextLabel");
        CHECK(clauses[1].derived == "Good");
        CHECK(clauses[1].base == "VSTGUI::CView");
        CHECK(clauses[2].derived == "Creator");
        CHECK(clauses[2].base == "VSTGUI::ViewCreatorAdapter");

        // And the allowlist REJECTS the rogue one while accepting the other two.
        CHECK_FALSE(isAllowedBase(clauses[0].base));
        CHECK(isAllowedBase(clauses[1].base));
        CHECK(isAllowedBase(clauses[2].base));
    }

    SECTION("arm 2 - every base in src/ui/*.h is on the enumerated allowlist") {
        const std::vector<std::string> headers = uiHeaderPaths();
        INFO("src/ui header count: " << headers.size());
        // cloud_view.h, drawer_container.h, edit_message.h, edit_sub_controller.h,
        // macro_ring_knob.h. A floor, not an equality: a later header is allowed,
        // a MISSING directory is not.
        REQUIRE(headers.size() >= 5u);

        const UiHeaderScan scan = scanUiHeaders(headers, "CloudView");
        INFO("first unreadable: " << scan.firstUnreadable);

        // The SC-011 anti-vacuity guards, asserted BEFORE the clause itself: a
        // clean result over nothing is not a finding.
        REQUIRE(scan.filesMissing == 0u);
        REQUIRE(scan.filesScanned == headers.size());
        REQUIRE(scan.codeBytes > 0u);
        REQUIRE(scan.witnesses > 0u);
        // Four classes plus the ring's creator struct.
        REQUIRE(scan.clauses.size() >= 5u);

        // THE TRIPWIRE. An unknown base name is RED, never a silent pass.
        for (const BaseClause& clause : scan.clauses) {
            INFO(clause.file << ": " << clause.derived << " : " << clause.base);
            CHECK(isAllowedBase(clause.base));
        }

        // THE BOUND. Exactly three classes derive from a view base.
        std::size_t viewClasses = 0;
        for (const BaseClause& clause : scan.clauses) {
            if (isViewBase(clause.base)) {
                ++viewClasses;
            }
        }
        CHECK(viewClasses == 3u);

        // ... and they are THESE three, on THESE bases - so a swap (a fourth
        // class replacing one of them while keeping the count at three) is red.
        CHECK(baseOf(scan, "CloudView") == "VSTGUI::CView");
        CHECK(baseOf(scan, "DrawerContainer") == "VSTGUI::CViewContainer");
        CHECK(baseOf(scan, "MacroRingKnob") == "Krate::Plugins::ArcKnob");
        // The sub-controller is found by the SAME scan and is NOT a view base -
        // the scan agrees with arm 1's negative assertion.
        CHECK(baseOf(scan, "SeraphisEditSubController") == "VSTGUI::DelegationController");
        CHECK_FALSE(isViewBase(baseOf(scan, "SeraphisEditSubController")));
    }
}

// -----------------------------------------------------------------------------
// SC-022(c) - FR-021's VIEW half
// -----------------------------------------------------------------------------
// FR-021's only other criterion, SC-017, is measured ENTIRELY on the producer
// (cloud_frame_test.cpp's P metric over published CloudFrames). A CloudView that
// faked the constellation's reaction to a macro ring - a local displacement, an
// interpolation toward a target the DSP is not producing - would leave P
// untouched and SC-017 would still pass. This is the arm that closes that hole:
// with the frame FROZEN, driving the ring must move NOTHING in the view.

namespace {

/// Counts the three CControl->listener callbacks. Its only job is anti-vacuity:
/// it proves the ring really was driven, so "nothing moved" is a finding rather
/// than the result of a knob that never changed value.
class CountingControlListener : public VSTGUI::IControlListener {
public:
    void valueChanged(VSTGUI::CControl* /*control*/) override { ++valueChangedCount; }
    void controlBeginEdit(VSTGUI::CControl* /*control*/) override { ++beginEditCount; }
    void controlEndEdit(VSTGUI::CControl* /*control*/) override { ++endEditCount; }

    std::size_t valueChangedCount = 0;
    std::size_t beginEditCount = 0;
    std::size_t endEditCount = 0;
};

/// The sequence the frozen frame carries. Delivered ONCE and never bumped, so
/// CloudView::onTimerForTest()'s sequence gate (cloud_view.cpp:315-323) reports
/// "idle transport" on every subsequent tick.
constexpr std::uint32_t kFrozenSequence = 4242u;

constexpr std::uint8_t kFrozenPartials = 8;

/// Full-range sweep resolution. 21 points means 0.00, 0.05 ... 1.00 exactly.
constexpr int kRingSweepSteps = 21;

}  // namespace

TEST_CASE("Seraphis_MacroRing_DoesNotAnimateTheCloudViewLocally", "[ui][phase11]") {
    Seraphis::Controller controller;

    // ---- ONE frame, delivered ONCE. Its sequence never moves again. ----------
    Seraphis::CloudFrame frame = makeOctaveFrame(kFrozenSequence, kFrozenPartials);
    deliver(controller, frame);

    auto view = makeView(controller);
    view->onTimerForTest();  // first frame ever seen -> one invalidate
    view->renderForTest();

    REQUIRE(view->pointsDrawnForTest() == static_cast<std::size_t>(kFrozenPartials));
    const std::size_t invalidBefore = view->invalidCountForTest();
    const std::size_t drawsBefore = view->drawCountForTest();
    REQUIRE(invalidBefore == 1u);
    REQUIRE(drawsBefore == 1u);

    // A COPY, not a reference: drawnPointsForTest() hands back the live vector,
    // and a reference would compare the "after" state with itself.
    const std::vector<CloudView::DrawnPoint> pointsBefore = view->drawnPointsForTest();

    // ---- drive the ring across its FULL range -------------------------------
    // The real ParamID, the real base-class gesture: beginEdit / value change +
    // valueChanged / endEdit are all inherited from ArcKnob -> CKnobBase ->
    // CControl (arc_knob.h:136, :174, :190), and MacroRingKnob overrides none of
    // them. The 33 ms timer keeps ticking through the gesture, exactly as it
    // would in a live editor.
    CountingControlListener listener;
    auto ring = VSTGUI::owned(new Seraphis::UI::MacroRingKnob(
        VSTGUI::CRect(0.0, 0.0, 96.0, 96.0), &listener,
        static_cast<std::int32_t>(Seraphis::kMacroBloomId)));

    ring->beginEdit();
    for (int step = 0; step < kRingSweepSteps; ++step) {
        const float normalized =
            static_cast<float>(step) / static_cast<float>(kRingSweepSteps - 1);
        ring->setValueNormalized(normalized);
        ring->valueChanged();
        view->onTimerForTest();
    }
    ring->endEdit();

    SECTION("the drive really happened (anti-vacuity, checked before the claim)") {
        CHECK(listener.valueChangedCount == static_cast<std::size_t>(kRingSweepSteps));
        CHECK(listener.beginEditCount == 1u);
        CHECK(listener.endEditCount == 1u);
        CHECK(ring->getValueNormalized() == Catch::Approx(1.0f).margin(1e-6));
        CHECK(ring->getTag() == static_cast<std::int32_t>(Seraphis::kMacroBloomId));
    }

    SECTION("SC-022(c): no redraw was requested and nothing was repainted") {
        // The view's ONLY input is the cached CloudFrame, whose sequence did not
        // move. If either counter has risen, some path exists from a macro value
        // to the view - which is exactly the view-local animation FR-021 forbids.
        CHECK(view->invalidCountForTest() == invalidBefore);
        CHECK(view->drawCountForTest() == drawsBefore);
    }

    SECTION("SC-022(c): and no point MOVED, even when forced to repaint") {
        // Stronger than the counters: re-run the real draw() body and compare
        // every point. A view that recomputed positions from a macro value
        // without invalidating would pass the counters and fail here.
        view->renderForTest();
        const std::vector<CloudView::DrawnPoint>& pointsAfter = view->drawnPointsForTest();

        REQUIRE(pointsAfter.size() == pointsBefore.size());
        for (std::size_t i = 0; i < pointsBefore.size(); ++i) {
            INFO("partial " << i);
            CHECK(pointsAfter[i].x == Catch::Approx(pointsBefore[i].x).margin(1e-12));
            CHECK(pointsAfter[i].y == Catch::Approx(pointsBefore[i].y).margin(1e-12));
            CHECK(pointsAfter[i].radius == Catch::Approx(pointsBefore[i].radius).margin(1e-12));
            CHECK(pointsAfter[i].hollow == pointsBefore[i].hollow);
        }
    }
}
