// ==============================================================================
// Controller: View Sync (setParamNormalized + deferred UI timer)
// ==============================================================================
// setParamNormalized() can be called from ANY thread (automation, state
// loading, host-internal scheduling). All VSTGUI view manipulation is
// deferred to syncViewsFromParams(), which runs on the UI thread via a
// CVSTGUITimer started in didOpen().
// ==============================================================================

#include "controller.h"
#include "source_mode_disable_list.h"
#include "../plugin_ids.h"
#include "../parameters/arpeggiator_params.h"
#include "../parameters/dropdown_mappings.h"
#include "../ui/ring_display.h"
#include "../ui/pin_flag_strip.h"
#include "../ui/markov_matrix_editor.h"

#include "krate/dsp/processors/markov_matrices.h"
#include "krate/dsp/primitives/held_note_buffer.h"

#include "ui/arp_lane_editor.h"
#include "../ui/midi_delay_lane_editor.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Gradus {

// ==============================================================================
// setParamNormalized — classify parameter and set dirty flags
// ==============================================================================
// Only host/controller API calls here (beginEdit, performEdit, etc.).
// NO VSTGUI view manipulation.

tresult PLUGIN_API Controller::setParamNormalized(
    ParamID tag, ParamValue value)
{
    tresult result = EditControllerEx1::setParamNormalized(tag, value);

    // Spec 142: forward Sequencer Note lane param changes to the Processor
    // via IMessage. See controller.h sendSeqNoteLaneParam declaration for why
    // we can't rely on the host's parameter queue alone. Skip the read-only
    // playhead (3811) since the Processor writes it, not the Controller.
    if (tag >= kArpSourceModeId && tag < kArpSequencerNoteLanePlayheadId) {
        sendSeqNoteLaneParam(tag, value);
    }

    // --- Classify parameter into dirty categories ---

    // Lane step/length parameters (LengthId is always 1 below Step0Id)
    if ((tag >= kArpVelocityLaneStep0Id && tag < kArpVelocityLaneStep0Id + 32) || tag == kArpVelocityLaneLengthId)
        viewDirtyFlags_.fetch_or(kDirtyVelocityLane | kDirtyLaneLengths | kDirtyRing, std::memory_order_relaxed);
    else if ((tag >= kArpGateLaneStep0Id && tag < kArpGateLaneStep0Id + 32) || tag == kArpGateLaneLengthId)
        viewDirtyFlags_.fetch_or(kDirtyGateLane | kDirtyLaneLengths | kDirtyRing, std::memory_order_relaxed);
    else if ((tag >= kArpPitchLaneStep0Id && tag < kArpPitchLaneStep0Id + 32) || tag == kArpPitchLaneLengthId)
        viewDirtyFlags_.fetch_or(kDirtyPitchLane | kDirtyPinFlags | kDirtyLaneLengths | kDirtyRing, std::memory_order_relaxed);
    else if ((tag >= kArpRatchetLaneStep0Id && tag < kArpRatchetLaneStep0Id + 32) || tag == kArpRatchetLaneLengthId)
        viewDirtyFlags_.fetch_or(kDirtyRatchetLane | kDirtyLaneLengths | kDirtyRing, std::memory_order_relaxed);
    else if ((tag >= kArpModifierLaneStep0Id && tag < kArpModifierLaneStep0Id + 32) || tag == kArpModifierLaneLengthId)
        viewDirtyFlags_.fetch_or(kDirtyModifierLane | kDirtyLaneLengths | kDirtyRing, std::memory_order_relaxed);
    else if ((tag >= kArpConditionLaneStep0Id && tag < kArpConditionLaneStep0Id + 32) || tag == kArpConditionLaneLengthId)
        viewDirtyFlags_.fetch_or(kDirtyConditionLane | kDirtyLaneLengths | kDirtyRing, std::memory_order_relaxed);
    else if ((tag >= kArpChordLaneStep0Id && tag < kArpChordLaneStep0Id + 32) || tag == kArpChordLaneLengthId)
        viewDirtyFlags_.fetch_or(kDirtyChordLane | kDirtyLaneLengths | kDirtyRing, std::memory_order_relaxed);
    else if ((tag >= kArpInversionLaneStep0Id && tag < kArpInversionLaneStep0Id + 32) || tag == kArpInversionLaneLengthId)
        viewDirtyFlags_.fetch_or(kDirtyInversionLane | kDirtyLaneLengths | kDirtyRing, std::memory_order_relaxed);

    // Pin flags
    else if (tag >= kArpPinFlagStep0Id && tag <= kArpPinFlagStep31Id)
        viewDirtyFlags_.fetch_or(kDirtyPinFlags, std::memory_order_relaxed);

    // Arp mode (shows/hides Markov editor)
    else if (tag == kArpModeId)
        viewDirtyFlags_.fetch_or(kDirtyArpMode | kDirtyRing, std::memory_order_relaxed);

    // Markov cells and presets
    else if ((tag >= kArpMarkovCell00Id && tag <= kArpMarkovCell66Id) || tag == kArpMarkovPresetId)
        viewDirtyFlags_.fetch_or(kDirtyMarkov | kDirtyRing, std::memory_order_relaxed);

    // MIDI Delay lane (must be checked BEFORE playhead range — delay IDs
    // 3510-3739 overlap the naive playhead range 3294-3740)
    else if (tag == kArpMidiDelayLaneLengthId)
        viewDirtyFlags_.fetch_or(kDirtyMidiDelayLane | kDirtyLaneLengths | kDirtyRing, std::memory_order_relaxed);
    else if ((tag >= kArpMidiDelayActiveStep0Id && tag < kArpMidiDelayActiveStep0Id + 32) ||
             (tag >= kArpMidiDelayTimeModeStep0Id && tag < kArpMidiDelayTimeModeStep0Id + 32) ||
             (tag >= kArpMidiDelayTimeStep0Id && tag < kArpMidiDelayTimeStep0Id + 32) ||
             (tag >= kArpMidiDelayFeedbackStep0Id && tag < kArpMidiDelayFeedbackStep0Id + 32) ||
             (tag >= kArpMidiDelayVelDecayStep0Id && tag < kArpMidiDelayVelDecayStep0Id + 32) ||
             (tag >= kArpMidiDelayPitchShiftStep0Id && tag < kArpMidiDelayPitchShiftStep0Id + 32) ||
             (tag >= kArpMidiDelayGateScaleStep0Id && tag < kArpMidiDelayGateScaleStep0Id + 32))
        viewDirtyFlags_.fetch_or(kDirtyMidiDelayLane, std::memory_order_relaxed);

    // Playhead parameters (9 specific IDs, NOT a broad range).
    // kDirtyRing too: the ring draws its own step highlight from the data
    // bridge, so without an invalidation the highlight only moves when some
    // unrelated parameter happens to raise kDirtyRing.
    else if (tag == kArpVelocityPlayheadId || tag == kArpGatePlayheadId ||
             tag == kArpPitchPlayheadId || tag == kArpRatchetPlayheadId ||
             tag == kArpModifierPlayheadId || tag == kArpConditionPlayheadId ||
             tag == kArpChordPlayheadId || tag == kArpInversionPlayheadId ||
             tag == kArpMidiDelayPlayheadId)
        viewDirtyFlags_.fetch_or(kDirtyPlayheads | kDirtyRing,
                                 std::memory_order_relaxed);

    // Scale type
    else if (tag == kArpScaleTypeId)
        viewDirtyFlags_.fetch_or(kDirtyScaleType, std::memory_order_relaxed);

    // Per-lane speed multipliers
    else if (tag >= kArpVelocityLaneSpeedId && tag <= kArpInversionLaneSpeedId)
        viewDirtyFlags_.fetch_or(kDirtyLaneSpeeds, std::memory_order_relaxed);

    // Euclidean parameters
    else if (tag == kArpEuclideanEnabledId || tag == kArpEuclideanHitsId ||
             tag == kArpEuclideanStepsId || tag == kArpEuclideanRotationId)
        viewDirtyFlags_.fetch_or(kDirtyEuclidean, std::memory_order_relaxed);

    // Spec 142: Sequencer mode source toggle (FR-027 visibility,
    // FR-036 disabled controls). Set BOTH dirty bits so the source-mode
    // handler runs AND any sequencer-lane refresh logic also fires.
    else if (tag == kArpSourceModeId)
        viewDirtyFlags_.fetch_or(
            kDirtyArpSourceMode | kDirtySequencerNoteLane,
            std::memory_order_relaxed);

    // Spec 142: Sequencer Note lane params (pitches 3743-3774, rests 3775-3806,
    // length 3742, modulators 3807-3810, playhead 3811). Piano-roll view
    // picks these up via its own IDependent on each param, but flagging the
    // generic dirty bit lets future controller-side helpers (e.g., audition
    // ring overlays) hook in cheaply.
    else if (tag >= kArpSequencerNoteLaneLengthId && tag <= kArpSequencerNoteLaneEndId)
        viewDirtyFlags_.fetch_or(kDirtySequencerNoteLane, std::memory_order_relaxed);

    // Generic arp range → ring invalidation
    else if (tag >= 3001 && tag <= 3400)
        viewDirtyFlags_.fetch_or(kDirtyRing, std::memory_order_relaxed);

    // --- Controller logic (host API calls, not VSTGUI) ---

    // Markov preset → batch-load matrix cells
    if (tag == kArpMarkovPresetId) {
        const int presetIdx = std::clamp(
            static_cast<int>(std::round(value * 5.0)), 0, 5);

        if (!suppressMarkovPresetLoad_ && presetIdx >= 0 && presetIdx <= 4) {
            const auto& matrix = Krate::DSP::getMarkovPresetMatrix(
                static_cast<Krate::DSP::MarkovPreset>(presetIdx));
            suppressMarkovCellEcho_ = true;
            for (size_t i = 0; i < Krate::DSP::kMarkovMatrixSize; ++i) {
                const auto cellId =
                    static_cast<ParamID>(kArpMarkovCell00Id + static_cast<int>(i));
                beginEdit(cellId);
                setParamNormalized(cellId, static_cast<double>(matrix[i]));
                performEdit(cellId, static_cast<double>(matrix[i]));
                endEdit(cellId);
            }
            suppressMarkovCellEcho_ = false;
        }
    }

    // Cell edit → flip preset to Custom
    if (tag >= kArpMarkovCell00Id && tag <= kArpMarkovCell66Id &&
        !suppressMarkovCellEcho_ && !suppressMarkovPresetLoad_) {
        const double presetNorm = getParamNormalized(kArpMarkovPresetId);
        const int currentPreset = std::clamp(
            static_cast<int>(std::round(presetNorm * 5.0)), 0, 5);
        if (currentPreset != 5) {
            constexpr double kCustomNormalized = 1.0;
            suppressMarkovPresetLoad_ = true;
            beginEdit(kArpMarkovPresetId);
            setParamNormalized(kArpMarkovPresetId, kCustomNormalized);
            performEdit(kArpMarkovPresetId, kCustomNormalized);
            endEdit(kArpMarkovPresetId);
            suppressMarkovPresetLoad_ = false;
        }
    }

    return result;
}

// ==============================================================================
// syncViewsFromParams — UI thread timer callback
// ==============================================================================
// Reads parameter values from the EditController model and pushes them
// to VSTGUI views. Only runs when the editor is open.

void Controller::syncViewsFromParams()
{
    uint32_t flags = viewDirtyFlags_.exchange(0, std::memory_order_relaxed);
    if (flags == 0) return;

    // Helper to read a normalized parameter and convert to int steps
    auto paramToSteps = [this](ParamID id) -> int {
        auto* p = getParameterObject(id);
        return p ? std::clamp(
            static_cast<int>(1.0 + std::round(p->getNormalized() * 31.0)), 1, 32)
            : 8;
    };

    auto paramNorm = [this](ParamID id) -> double {
        auto* p = getParameterObject(id);
        return p ? p->getNormalized() : 0.0;
    };

    // --- Sync float-step lanes (Velocity, Gate, Pitch, Ratchet) ---
    auto syncFloatLane = [&](uint32_t flag, Krate::Plugins::IArpLane* lanePtr,
                             ParamID stepBase, ParamID lengthId) {
        if (!(flags & flag) || !lanePtr) return;
        if (auto* lane = dynamic_cast<Krate::Plugins::ArpLaneEditor*>(lanePtr)) {
            for (int i = 0; i < 32; ++i) {
                lane->setStepLevel(i, static_cast<float>(
                    paramNorm(static_cast<ParamID>(stepBase + i))));
            }
            lane->setNumSteps(paramToSteps(lengthId));
            lane->setDirty(true);
        }
    };
    syncFloatLane(kDirtyVelocityLane, velocityLane_,
        kArpVelocityLaneStep0Id, kArpVelocityLaneLengthId);
    syncFloatLane(kDirtyGateLane, gateLane_,
        kArpGateLaneStep0Id, kArpGateLaneLengthId);
    syncFloatLane(kDirtyPitchLane, pitchLane_,
        kArpPitchLaneStep0Id, kArpPitchLaneLengthId);
    syncFloatLane(kDirtyRatchetLane, ratchetLane_,
        kArpRatchetLaneStep0Id, kArpRatchetLaneLengthId);

    // --- Modifier lane (uint8_t flags) ---
    if ((flags & kDirtyModifierLane) && modifierLane_) {
        if (auto* lane = dynamic_cast<Krate::Plugins::ArpModifierLane*>(
                static_cast<Krate::Plugins::IArpLane*>(modifierLane_))) {
            for (int i = 0; i < 32; ++i) {
                double v = paramNorm(static_cast<ParamID>(kArpModifierLaneStep0Id + i));
                auto f = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(std::round(v * 255.0)), 0, 255));
                lane->setStepFlags(i, f);
            }
            lane->setNumSteps(paramToSteps(kArpModifierLaneLengthId));
            lane->setDirty(true);
        }
    }

    // --- Condition lane (uint8_t indices) ---
    if ((flags & kDirtyConditionLane) && conditionLane_) {
        if (auto* lane = dynamic_cast<Krate::Plugins::ArpConditionLane*>(
                static_cast<Krate::Plugins::IArpLane*>(conditionLane_))) {
            for (int i = 0; i < 32; ++i) {
                double v = paramNorm(static_cast<ParamID>(kArpConditionLaneStep0Id + i));
                auto idx = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(std::round(v * 17.0)), 0, 17));
                lane->setStepCondition(i, idx);
            }
            lane->setNumSteps(paramToSteps(kArpConditionLaneLengthId));
            lane->setDirty(true);
        }
    }

    // --- Chord lane ---
    if ((flags & kDirtyChordLane) && chordLane_) {
        if (auto* lane = dynamic_cast<Krate::Plugins::ArpChordLane*>(
                static_cast<Krate::Plugins::IArpLane*>(chordLane_))) {
            for (int i = 0; i < 32; ++i) {
                double v = paramNorm(static_cast<ParamID>(kArpChordLaneStep0Id + i));
                auto idx = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(std::round(v * 4.0)), 0, 4));
                lane->setStepValue(i, idx);
            }
            lane->setNumSteps(paramToSteps(kArpChordLaneLengthId));
            lane->setDirty(true);
        }
    }

    // --- Inversion lane ---
    if ((flags & kDirtyInversionLane) && inversionLane_) {
        if (auto* lane = dynamic_cast<Krate::Plugins::ArpInversionLane*>(
                static_cast<Krate::Plugins::IArpLane*>(inversionLane_))) {
            for (int i = 0; i < 32; ++i) {
                double v = paramNorm(static_cast<ParamID>(kArpInversionLaneStep0Id + i));
                auto idx = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(std::round(v * 3.0)), 0, 3));
                lane->setStepValue(i, idx); // NOLINT(readability-suspicious-call-argument)
            }
            lane->setNumSteps(paramToSteps(kArpInversionLaneLengthId));
            lane->setDirty(true);
        }
    }

    // --- Pin flag strip ---
    if ((flags & kDirtyPinFlags) && pinFlagStrip_) {
        for (int i = 0; i < 32; ++i) {
            pinFlagStrip_->setStepValue(i, static_cast<float>(
                paramNorm(static_cast<ParamID>(kArpPinFlagStep0Id + i))));
        }
        pinFlagStrip_->setNumSteps(paramToSteps(kArpPitchLaneLengthId));
    }

    // --- Arp mode → show/hide Markov editor ---
    if ((flags & kDirtyArpMode) && markovEditor_) {
        const int modeIdx = std::clamp(
            static_cast<int>(std::round(paramNorm(kArpModeId) * 11.0)), 0, 11);
        markovEditor_->setVisible(modeIdx == 11);
    }

    // --- Markov cells + preset ---
    if (flags & kDirtyMarkov) {
        if (markovEditor_) {
            // Sync all cell values
            for (int i = 0; std::cmp_less(i, Krate::DSP::kMarkovMatrixSize); ++i) {
                auto v = static_cast<float>(paramNorm(
                    static_cast<ParamID>(kArpMarkovCell00Id + i)));
                markovEditor_->setCellValueFlat(i, v);
            }
            // Sync preset dropdown
            const int presetIdx = std::clamp(
                static_cast<int>(std::round(paramNorm(kArpMarkovPresetId) * 5.0)), 0, 5);
            markovEditor_->setPresetValue(presetIdx);
        }
    }

    // --- Playhead parameters → lane views + ring display ---
    if (flags & kDirtyPlayheads) {
        struct PlayheadEntry {
            ParamID id;
            Krate::Plugins::IArpLane* lane;
            int laneIndex;
        };
        const PlayheadEntry entries[] = {
            {.id = kArpVelocityPlayheadId,  .lane = velocityLane_,  .laneIndex = 0},
            {.id = kArpGatePlayheadId,      .lane = gateLane_,      .laneIndex = 1},
            {.id = kArpPitchPlayheadId,     .lane = pitchLane_,     .laneIndex = 2},
            {.id = kArpModifierPlayheadId,  .lane = modifierLane_,  .laneIndex = 3},
            {.id = kArpConditionPlayheadId, .lane = conditionLane_, .laneIndex = 4},
            {.id = kArpRatchetPlayheadId,   .lane = ratchetLane_,   .laneIndex = 5},
            {.id = kArpChordPlayheadId,     .lane = chordLane_,     .laneIndex = 6},
            {.id = kArpInversionPlayheadId, .lane = inversionLane_, .laneIndex = 7},
            {.id = kArpMidiDelayPlayheadId, .lane = midiDelayLane_, .laneIndex = 8},
        };
        for (const auto& e : entries) {
            double val = paramNorm(e.id);
            int step = static_cast<int>(val * 32.0);
            if (e.lane) {
                e.lane->setPlayheadStep(step);
                auto* view = e.lane->getView();
                if (view && view->isVisible())
                    view->invalidRect(view->getViewSize());
            }
            ringDataBridge_.setPlayheadStep(e.laneIndex, step);
        }
    }

    // --- Scale type → pitch lane ---
    if (flags & kDirtyScaleType) {
        if (auto* lane = dynamic_cast<Krate::Plugins::ArpLaneEditor*>(
                static_cast<Krate::Plugins::IArpLane*>(pitchLane_))) {
            double v = paramNorm(kArpScaleTypeId);
            int uiIndex = std::clamp(
                static_cast<int>(std::round(v * (kArpScaleTypeCount - 1))),
                0, kArpScaleTypeCount - 1);
            int enumValue = kArpScaleDisplayOrder[static_cast<size_t>(uiIndex)];
            lane->setScaleType(enumValue);
        }
    }

    // --- Per-lane speed multipliers ---
    if (flags & kDirtyLaneSpeeds) {
        const std::pair<ParamID, Krate::Plugins::IArpLane*> speedEntries[] = {
            {kArpVelocityLaneSpeedId,  velocityLane_},
            {kArpGateLaneSpeedId,      gateLane_},
            {kArpPitchLaneSpeedId,     pitchLane_},
            {kArpModifierLaneSpeedId,  modifierLane_},
            {kArpRatchetLaneSpeedId,   ratchetLane_},
            {kArpConditionLaneSpeedId, conditionLane_},
            {kArpChordLaneSpeedId,     chordLane_},
            {kArpInversionLaneSpeedId, inversionLane_},
        };
        for (const auto& [id, lane] : speedEntries) {
            if (!lane) continue;
            double v = paramNorm(id);
            int idx = std::clamp(
                static_cast<int>(std::round(v * (kLaneSpeedCount - 1))),
                0, kLaneSpeedCount - 1);
            lane->setSpeedMultiplier(kLaneSpeedValues[idx]);
            auto* view = lane->getView();
            if (view) view->invalid();
        }
    }

    // --- Euclidean parameters → ring data bridge ---
    if (flags & kDirtyEuclidean) {
        auto getParamInt = [this](ParamID id, double scale, int offset = 0) -> int {
            auto* p = getParameterObject(id);
            return p ? offset + static_cast<int>(std::round(p->getNormalized() * scale))
                     : 0;
        };
        ringDataBridge_.setEuclideanState(
            getParamInt(kArpEuclideanEnabledId, 1.0) > 0,
            getParamInt(kArpEuclideanHitsId, 32.0),
            std::clamp(getParamInt(kArpEuclideanStepsId, 30.0, 2), 2, 32),
            getParamInt(kArpEuclideanRotationId, 31.0));
    }

    // --- Lane lengths → ring geometry ---
    if (flags & kDirtyLaneLengths) {
        const std::pair<ParamID, int> lengthEntries[] = {
            {kArpVelocityLaneLengthId, 0}, {kArpGateLaneLengthId, 1},
            {kArpPitchLaneLengthId, 2},    {kArpModifierLaneLengthId, 3},
            {kArpConditionLaneLengthId, 4},{kArpRatchetLaneLengthId, 5},
            {kArpChordLaneLengthId, 6},    {kArpInversionLaneLengthId, 7},
            {kArpMidiDelayLaneLengthId, 8},
        };
        for (const auto& [id, laneIndex] : lengthEntries) {
            if (ringDisplay_) {
                int steps = paramToSteps(id);
                ringDisplay_->getRenderer()->geometry().setLaneStepCount(
                    laneIndex, steps);
            }
        }
    }

    // --- MIDI Delay lane ---
    if (flags & kDirtyMidiDelayLane) {
        // Length
        if (midiDelayLane_) {
            int steps = paramToSteps(kArpMidiDelayLaneLengthId);
            midiDelayLane_->setLength(steps);
        }

        // Step values
        if (auto* delayEditor = dynamic_cast<MidiDelayLaneEditor*>(
                static_cast<Krate::Plugins::IArpLane*>(midiDelayLane_))) {
            struct DelayRow {
                ParamID baseId;
                MidiDelayLaneEditor::KnobRow row;
            };
            const DelayRow rows[] = {
                {.baseId = kArpMidiDelayActiveStep0Id,     .row = MidiDelayLaneEditor::KnobRow::kActive},
                {.baseId = kArpMidiDelayTimeModeStep0Id,   .row = MidiDelayLaneEditor::KnobRow::kTimeMode},
                {.baseId = kArpMidiDelayTimeStep0Id,       .row = MidiDelayLaneEditor::KnobRow::kDelayTime},
                {.baseId = kArpMidiDelayFeedbackStep0Id,   .row = MidiDelayLaneEditor::KnobRow::kFeedback},
                {.baseId = kArpMidiDelayVelDecayStep0Id,   .row = MidiDelayLaneEditor::KnobRow::kVelDecay},
                {.baseId = kArpMidiDelayPitchShiftStep0Id, .row = MidiDelayLaneEditor::KnobRow::kPitchShift},
                {.baseId = kArpMidiDelayGateScaleStep0Id,  .row = MidiDelayLaneEditor::KnobRow::kGateScale},
            };
            for (const auto& r : rows) {
                for (int i = 0; i < 32; ++i) {
                    delayEditor->setStepValue(i, r.row, static_cast<float>(
                        paramNorm(static_cast<ParamID>(r.baseId + i))));
                }
            }
        }
    }

    // --- Spec 142 FR-036: Source = Sequencer disables FR-022 controls ---
    if ((flags & kDirtyArpSourceMode) && activeEditor_) {
        auto* frame = activeEditor_->getFrame();
        if (frame) {
            const bool sequencerActive =
                paramNorm(kArpSourceModeId) >= 0.5;
            const float alpha = sequencerActive ? 0.4f : 1.0f;
            const bool  enabled = !sequencerActive;

            // Walk every CControl in the frame; toggle enable + alpha when
            // the control's tag is in the disable list. We walk the tree
            // (containers contain controls inside lane editors etc.).
            std::function<void(VSTGUI::CViewContainer*)> walk =
                [&](VSTGUI::CViewContainer* container) {
                    if (!container) return;
                    container->forEachChild([&](VSTGUI::CView* child) {
                        if (auto* sub = dynamic_cast<VSTGUI::CViewContainer*>(child)) {
                            walk(sub);
                        }
                        if (auto* ctrl = dynamic_cast<VSTGUI::CControl*>(child)) {
                            const auto tag = ctrl->getTag();
                            if (tag >= 0 &&
                                isSourceSequencerDisabledParam(
                                    static_cast<uint32_t>(tag))) {
                                ctrl->setMouseEnabled(enabled);
                                ctrl->setAlphaValue(alpha);
                                ctrl->invalid();
                            }
                        }
                    });
                };
            walk(frame);

            // Markov editor: hide entirely when Source = Sequencer (its 49
            // cell params are inert per FR-022 / FR-036; the editor itself
            // also uses ArpMode visibility, but explicit disable here is
            // belt-and-braces so an ArpMode=Markov + Source=Sequencer
            // combination doesn't leave the matrix interactive).
            if (markovEditor_) {
                if (sequencerActive) {
                    markovEditor_->setMouseEnabled(false);
                    markovEditor_->setAlphaValue(0.4f);
                } else {
                    markovEditor_->setMouseEnabled(true);
                    markovEditor_->setAlphaValue(1.0f);
                }
                markovEditor_->invalid();
            }

            // Pin flag strip: same treatment (its 32 cell params are in the
            // disable list, but the strip itself is a single CView container).
            if (pinFlagStrip_) {
                pinFlagStrip_->setMouseEnabled(enabled);
                pinFlagStrip_->setAlphaValue(alpha);
                pinFlagStrip_->invalid();
            }
        }
    }

    // --- Invalidate ring display for any arp parameter change ---
    if ((flags & kDirtyRing) && ringDisplay_) {
        auto* renderer = ringDisplay_->getRenderer();
        if (renderer) renderer->invalid();
    }
}

} // namespace Gradus
