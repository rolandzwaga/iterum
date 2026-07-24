// ==============================================================================
// Ring playhead animation — the ring must repaint as the playhead advances
// ==============================================================================
// The Processor writes the 8 ring lane playheads (3294-3299, 3371, 3372) into
// data.outputParameterChanges every block, so the host calls setParamNormalized
// on the Controller once per step. When the ring UI was introduced the whole
// invalidation lived inline in setParamNormalized:
//
//     if (ringDisplay_) {
//         bool isArpParam = (tag >= 3001 && tag <= 3400);
//         if (isArpParam) renderer->invalid();
//     }
//
// Every ring playhead ID falls inside 3001-3400, so the ring repainted on each
// advance and the step highlight animated. Commit e76b0eb9 replaced that inline
// call with the deferred kDirtyRing bit, but the playhead arm of the
// classification else-if chain only sets kDirtyPlayheads. The ring data bridge
// kept receiving the new step while the view was never invalidated, so the
// highlight only moved when some *other* parameter (switching a lane tab, an
// edit) happened to raise kDirtyRing.
//
// This asserts the classification, which is where the defect lives. Asserting
// the repaint itself would mean observing CFrame::invalidRect, which bottoms out
// in the platform frame and is not reachable from a headless test.
// ==============================================================================

#include "controller/controller.h"
#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

using namespace Gradus;

TEST_CASE("Ring lane playhead advance marks the ring display dirty",
          "[gradus][controller][ui][ring][playhead]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    // The lanes the ring actually draws a playhead highlight for
    // (RingRenderer::drawPlayheadHighlights covers lanes 0-7). The MIDI-delay
    // playhead is deliberately absent: it drives its own grid editor, not
    // the ring.
    const Steinberg::Vst::ParamID ringPlayheads[] = {
        kArpVelocityPlayheadId,  kArpGatePlayheadId,
        kArpPitchPlayheadId,     kArpRatchetPlayheadId,
        kArpModifierPlayheadId,  kArpConditionPlayheadId,
        kArpChordPlayheadId,     kArpInversionPlayheadId,
    };

    for (const auto id : ringPlayheads) {
        // Drop whatever initialize() or the previous iteration left pending so
        // the assertion can only be satisfied by THIS parameter change.
        controller.takePendingViewDirtyFlags();

        constexpr double kStep3Normalized = 3.0 / 32.0;
        REQUIRE(controller.setParamNormalized(id, kStep3Normalized)
                == Steinberg::kResultOk);

        const uint32_t flags = controller.takePendingViewDirtyFlags();
        INFO("playhead ParamID " << id << " raised flags 0x" << std::hex << flags);
        CHECK((flags & Controller::kDirtyPlayheads) != 0u);
        CHECK((flags & Controller::kDirtyRing) != 0u);
    }

    controller.terminate();
}
