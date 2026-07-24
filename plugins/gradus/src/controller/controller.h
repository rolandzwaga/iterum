#pragma once

// ==============================================================================
// Gradus Controller — UI Thread Component
// ==============================================================================

#include "ui/arp_lane.h"
#include "ui/ring_data_bridge.h"
#include "ui/speed_curve_data.h"
#include "preset/preset_manager.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>
#include <atomic>
#include <memory>

namespace Steinberg {
class MemoryStream;
class IBStreamer;
}

namespace Krate::Plugins {
class PresetBrowserView;
class SavePresetDialogView;
} // namespace Krate::Plugins

namespace Gradus {
class RingDisplay;
class DetailStrip;
class PinFlagStrip;
class MarkovMatrixEditor;
class SpeedCurveEditor;
class PianoRollView;
} // namespace Gradus

namespace Gradus {

static constexpr int kArpLaneCount = 9;

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate
{
public:
    Controller() = default;
    ~Controller() override = default;

    // --- IEditController ---
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(
        Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setParamNormalized(
        Steinberg::Vst::ParamID tag,
        Steinberg::Vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;

    // --- IMessage handler ---
    Steinberg::tresult PLUGIN_API notify(
        Steinberg::Vst::IMessage* message) override;

    // --- VST3EditorDelegate ---
    Steinberg::IPlugView* PLUGIN_API createView(
        Steinberg::FIDString name) override;
    VSTGUI::CView* createCustomView(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;
    VSTGUI::CView* verifyView(
        VSTGUI::CView* view,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;
    void didOpen(VSTGUI::VST3Editor* editor) override;
    void willClose(VSTGUI::VST3Editor* editor) override;

    // Arp lane management
    void constructArpLanes();
    Krate::Plugins::IArpLane* getArpLane(int index);
    uint32_t getArpLaneStepBaseParamId(int index);
    uint32_t getArpLaneLengthParamId(int index);
    /// Step-base ParamID for a lane addressed in RING (UI) order, which differs
    /// from getArpLane order at indices 3/4/5. See getArpLane() for both tables.
    static uint32_t getRingLaneStepBaseParamId(int ringIndex);
    void onLaneCopy(int laneIndex);
    void onLanePaste(int targetLaneIndex);
    void wireCopyPasteCallbacks();

    // Spec 142: Sequencer Note lane (lane index 9 inside ArpeggiatorCore).
    // The Sequencer Note lane is edited via PianoRollView; these accessors
    // expose its per-step pitch base ID + length param ID for any future
    // unified-lane walk that wants to include it without bumping kArpLaneCount.
    [[nodiscard]] static uint32_t getSequencerNoteLaneStepBaseParamId() noexcept;
    [[nodiscard]] static uint32_t getSequencerNoteLaneLengthParamId() noexcept;

    // Preset management
    void openPresetBrowser();
    void closePresetBrowser();
    void openSavePresetDialog();
    Krate::Plugins::PresetManager* getPresetManager() { return presetManager_.get(); }
    Steinberg::MemoryStream* createComponentStateStream();
    bool loadComponentStateWithNotify(Steinberg::IBStream* state);
    void editParamWithNotify(Steinberg::Vst::ParamID id,
                             Steinberg::Vst::ParamValue value);

    // Shared state loading (single path for both host recall and preset browser)
    template<typename SetParamFn>
    void loadFullState(Steinberg::IBStreamer& streamer, SetParamFn setParam);

    // --- Deferred UI sync (thread-safe setParamNormalized) ---
    // Dirty flags categorize which UI regions need syncing.
    // setParamNormalized sets flags; a timer flushes on the UI thread.
    enum DirtyFlags : uint32_t {
        kDirtyVelocityLane  = 1u << 0,
        kDirtyGateLane      = 1u << 1,
        kDirtyPitchLane     = 1u << 2,
        kDirtyRatchetLane   = 1u << 3,
        kDirtyModifierLane  = 1u << 4,
        kDirtyConditionLane = 1u << 5,
        kDirtyChordLane     = 1u << 6,
        kDirtyInversionLane = 1u << 7,
        kDirtyMidiDelayLane = 1u << 8,
        kDirtyPinFlags      = 1u << 9,
        kDirtyMarkov        = 1u << 10,
        kDirtyPlayheads     = 1u << 11,
        kDirtyRing          = 1u << 12,
        kDirtyScaleType     = 1u << 13,
        kDirtyLaneSpeeds    = 1u << 14,
        kDirtyEuclidean     = 1u << 15,
        kDirtyLaneLengths   = 1u << 16,
        kDirtyArpMode       = 1u << 17,
        kDirtyArpSourceMode = 1u << 18,
        kDirtySequencerNoteLane = 1u << 19,
    };

    /// Consume the pending dirty bits exactly as the UI-thread timer does.
    /// Test seam: lets a test assert setParamNormalized's classification
    /// without pumping a VSTGUI run loop.
    uint32_t takePendingViewDirtyFlags() noexcept
    {
        return viewDirtyFlags_.exchange(0, std::memory_order_relaxed);
    }

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

private:
    // Preset button factory (called from createCustomView)
    VSTGUI::CView* createPresetButton(const VSTGUI::CRect& rect, bool isBrowse);

    std::atomic<uint32_t> viewDirtyFlags_{0};
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> viewSyncTimer_;
    void syncViewsFromParams();  // Runs on UI thread via timer

    // Arp lane pointers (VSTGUI-owned, nulled in willClose)
    Krate::Plugins::IArpLane* velocityLane_ = nullptr;
    Krate::Plugins::IArpLane* gateLane_ = nullptr;
    Krate::Plugins::IArpLane* pitchLane_ = nullptr;
    Krate::Plugins::IArpLane* ratchetLane_ = nullptr;
    Krate::Plugins::IArpLane* modifierLane_ = nullptr;
    Krate::Plugins::IArpLane* conditionLane_ = nullptr;
    Krate::Plugins::IArpLane* chordLane_ = nullptr;
    Krate::Plugins::IArpLane* inversionLane_ = nullptr;
    Krate::Plugins::IArpLane* midiDelayLane_ = nullptr;

    // Circular ring display (VSTGUI-owned, nulled in willClose)
    RingDisplay* ringDisplay_ = nullptr;

    // Detail strip with lane tabs (VSTGUI-owned, nulled in willClose)
    DetailStrip* detailStrip_ = nullptr;

    // Ring data bridge (owned by controller, reads from IArpLane pointers)
    RingDataBridge ringDataBridge_;

    // v1.5: Per-lane swing knobs (captured in verifyView, toggled in selectLane)
    std::array<VSTGUI::CView*, 8> laneSwingKnobs_{};
    // v1.5 Part 2: Per-lane length jitter knobs
    std::array<VSTGUI::CView*, 8> laneJitterKnobs_{};
    // v1.5: Ratchet Decay knob + label (only visible when Ratchet lane selected)
    VSTGUI::CView* ratchetDecayKnob_ = nullptr;
    VSTGUI::CView* ratchetDecayLabel_ = nullptr;
    // Ratchet sub-step shuffle (Ratchet lane only)
    VSTGUI::CView* ratchetSubSwingKnob_ = nullptr;
    VSTGUI::CView* ratchetSubSwingLabel_ = nullptr;
    // v1.5: Strum controls + labels (only visible when Chord or Inversion lane selected)
    VSTGUI::CView* strumTimeKnob_ = nullptr;
    VSTGUI::CView* strumTimeLabel_ = nullptr;
    VSTGUI::CView* strumDirectionMenu_ = nullptr;
    VSTGUI::CView* strumDirectionLabel_ = nullptr;
    // v1.5 Part 2: Velocity Curve controls (only visible when Velocity lane selected)
    VSTGUI::CView* velCurveKnob_ = nullptr;
    VSTGUI::CView* velCurveLabel_ = nullptr;
    VSTGUI::CView* velCurveTypeMenu_ = nullptr;
    VSTGUI::CView* velCurveTypeLabel_ = nullptr;
    // v1.5 Part 3: Pitch-lane contextual controls
    // (Transpose + Pin Note + Note Range Mapping)
    VSTGUI::CView* transposeKnob_ = nullptr;
    VSTGUI::CView* transposeLabel_ = nullptr;
    VSTGUI::CView* pinNoteKnob_ = nullptr;
    VSTGUI::CView* pinNoteLabel_ = nullptr;
    VSTGUI::CView* rangeLowKnob_ = nullptr;
    VSTGUI::CView* rangeLowLabel_ = nullptr;
    VSTGUI::CView* rangeHighKnob_ = nullptr;
    VSTGUI::CView* rangeHighLabel_ = nullptr;
    VSTGUI::CView* rangeModeMenu_ = nullptr;
    // v1.6: Inline 32-cell pin toggle row (Pitch lane contextual)
    PinFlagStrip* pinFlagStrip_ = nullptr;

    // Per-lane speed curve editors (overlay on lane editor)
    std::array<SpeedCurveEditor*, 8> speedCurveEditors_{};
    std::array<VSTGUI::CView*, 8> speedCurveDepthKnobs_{};
    VSTGUI::CView* speedCurveContainer_ = nullptr;  // CViewContainer with bg
    VSTGUI::CView* speedCurveToggle_ = nullptr;     // inside container
    VSTGUI::CView* speedCurveDepthLabel_ = nullptr;  // inside container
    VSTGUI::CView* speedCurvePresetMenu_ = nullptr;  // inside container
    std::shared_ptr<VSTGUI::IControlListener> speedCurveToggleListener_;
    std::shared_ptr<VSTGUI::IControlListener> speedCurvePresetListener_;
    std::shared_ptr<VSTGUI::IControlListener> speedCurveDepthListener_;
    int selectedLaneIndex_ = 0;
    // Pending speed curve data from setComponentState (applied when editors are created)
    std::array<SpeedCurveData, 8> pendingSpeedCurves_{};
    bool hasPendingSpeedCurves_ = false;

    // Spec 142: Piano-roll view for the Sequencer Note lane (VSTGUI-owned,
    // nulled in willClose). The view is hosted inside a UIViewSwitchContainer
    // so the pointer is recaptured each time the editor opens; we do NOT
    // cache it across UIViewSwitchContainer swaps.
    PianoRollView* pianoRollView_ = nullptr;

    // Markov Chain mode editor (visible only when Markov arp mode active)
    MarkovMatrixEditor* markovEditor_ = nullptr;
    // Guard against recursive loads when state recall fires setParamNormalized
    // for kArpMarkovPresetId (otherwise it would overwrite cell values loaded
    // from the same state blob).
    bool suppressMarkovPresetLoad_ = false;
    // Guard against cell-edit → preset-to-Custom ping-pong.
    bool suppressMarkovCellEcho_ = false;

    // Speed curve IMessage helper
    void sendSpeedCurveTable(size_t laneIndex, const SpeedCurveData& data);
    void showSpeedCurveForLane(int laneIndex);

    // Spec 142: forward Sequencer Note lane parameter edits to the Processor
    // via IMessage. Some hosts don't relay programmatically-driven UI param
    // changes (PianoRollView clicks, COptionMenu selections) back through the
    // host's parameter queue to processParameterChanges, so Processor atomics
    // miss the update — getState then saves stale values. Same workaround as
    // sendSpeedCurveTable (see controller.cpp:458 for the rationale).
    void sendSeqNoteLaneParam(Steinberg::Vst::ParamID id,
                              Steinberg::Vst::ParamValue value);

    // Shared state loading helpers (used by setComponentState + preset loading)
    template<typename SetParamFn>
    void loadSpeedCurvesFromStream(Steinberg::IBStreamer& streamer, SetParamFn setParam);
    template<typename SetParamFn>
    void loadMidiDelayFromStream(Steinberg::IBStreamer& streamer, SetParamFn setParam);

    // Clipboard for copy/paste
    Krate::Plugins::LaneClipboard clipboard_{};

    // Preset manager
    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
    Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;
    Krate::Plugins::SavePresetDialogView* savePresetDialogView_ = nullptr;

    // Active editor
    VSTGUI::VST3Editor* activeEditor_ = nullptr;
};

} // namespace Gradus
