#pragma once

// ==============================================================================
// Seraphis - Edit Controller (UI thread)
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation.
// This header includes NO processor TYPE - the single file it takes from
// src/processor/ is cloud_frame.h, the shared wire-format POD (see below).
//
// NO INoteExpressionController (FR-019 - a knowing Phase 9 decision; see
// plugins/seraphis/CLAUDE.md).
//
// Phase 11 (specs/seraphis-phase11-ui) opens the custom-view surface: this
// controller is now an IDataExchangeReceiver and caches the processor's
// CloudFrame for the cloud view (FR-016, SC-006 arm (h)). The Phase 8/9 banner
// that used to read "NO createCustomView / verifyView overrides - there are no
// custom views until Phase 11" is retired with this change; Phase 11 IS that
// phase, and a header that contradicts its own contents misleads the next
// reader (FR-052).
//
// cloud_frame.h lives under src/processor/ but is included here on purpose:
// it is a payload header with NO processor dependency (<cstdint> only), the
// same sanctioned shared-POD exception as Membrum's meters_block.h. The
// Constitution's rule forbids crossing the boundary with the processor's or
// controller's OWN headers, not with a shared wire-format POD.
// ==============================================================================

#include "preset/preset_manager.h"
#include "processor/cloud_frame.h"
#include "ui/edit_message.h"

#include <krate/dsp/processors/spectral_state.h>

#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>

namespace Steinberg {
class MemoryStream;
class IBStreamer;
}

namespace Krate::Plugins {
class PresetBrowserView;
}  // namespace Krate::Plugins

namespace Seraphis::UI {
// Forward-declared on purpose: controller.h names these three only as raw cache
// pointers, so none of the VSTGUI view headers has to be pulled in here.
class CloudView;
class DrawerContainer;
class MacroRingKnob;
}  // namespace Seraphis::UI

namespace Seraphis {

// ==============================================================================
// Phase 11 T018 - the gesture throttle (FR-048, SC-027, plan 7.4)
// ==============================================================================
// C-8's 30 Hz redraw rate, reused verbatim as the message rate so a drag can
// never send faster than the view can show. NOT A LEVER: T023's SC-031 measures
// the processor-side cost of an edit stream at exactly this rate.
inline constexpr std::chrono::milliseconds kEditThrottleInterval{33};

/// One in-flight drag/slider gesture. UI thread only; the controller owns
/// exactly one, because only one pointer gesture can be in flight at a time.
struct EditThrottle {
    std::chrono::steady_clock::time_point lastSend{};
    UI::EditMessage pending{};
    bool hasPending = false;
    bool active = false;

    /// TRUE once this gesture has staged at least one value.
    ///
    /// The plan's flush condition is written `hasPending || active`. Taken
    /// literally that flushes a DEFAULT-CONSTRUCTED EditMessage for a gesture
    /// that begins and ends without staging anything - and a default
    /// EditMessage is `kind = 0, a = 0`, i.e. the C-5 editor-CLOSE message,
    /// which would shut the cloud-frame producer down on an empty click. This
    /// flag narrows `active` to "active AND has something to flush" and changes
    /// nothing else: with one or more values staged, the flush is still
    /// unconditional, which is the half FR-048 exists for.
    bool sawValue = false;
};

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate,
                   public Steinberg::Vst::IDataExchangeReceiver {
public:
    Controller() = default;
    ~Controller() override = default;

    static Steinberg::FUnknown* createInstance(void* /*context*/) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;

    /// Phase 11 T018, FR-046 re-seed source 1. The ONLY reason this is
    /// overridden: IDs 409-412 re-seed slotMirror_ from makeFactoryState(), the
    /// same factory function the processor's own derivation path uses
    /// (processor.cpp:3631), so the display and the audio cannot disagree about
    /// what "Glass" means. Every other ID goes straight to the base.
    ///
    /// It is the right hook rather than the sub-controller's valueChanged
    /// because it catches HOST automation and setComponentState()'s replay as
    /// well as a UI move, and the slot dropdowns are ordinary tagged controls
    /// whose valueChanged the sub-controller deliberately does not claim.
    Steinberg::tresult PLUGIN_API setParamNormalized(
        Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) override;

    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    // ==========================================================================
    // Phase 11 - VST3EditorDelegate (T017; C-7a, C-7b, C-7c)
    // ==========================================================================
    // VST3EditorDelegate is ALREADY a base (see the class head above) - these
    // are overrides of it, not a second inheritance.
    //
    // createCustomView owns the TWO custom-view-name classes (CloudView,
    // DrawerContainer). MacroRingKnob is deliberately NOT here: it is a
    // ViewCreatorAdapter (C-7a, ui/macro_ring_knob.h:117-140) so that it accepts
    // control-tag and every other CControl attribute from the uidesc, which a
    // createCustomView view never gets.
    VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name,
                                    const VSTGUI::UIAttributes& attributes,
                                    const VSTGUI::IUIDescription* description,
                                    VSTGUI::VST3Editor* editor) override;

    /// Answers exactly ONE name, "SeraphisEdit" (the uidesc puts it on the
    /// TEMPLATE ROOT, D-4, so the whole document is inside its sub-tree).
    /// The returned object is owned by the view that carries the attribute -
    /// UIDescription stores it as kCViewControllerAttribute and CView's
    /// destructor deletes it (uidescription.cpp:741-755).
    VSTGUI::IController* createSubController(VSTGUI::UTF8StringPtr name,
                                             const VSTGUI::IUIDescription* description,
                                             VSTGUI::VST3Editor* editor) override;

    void didOpen(VSTGUI::VST3Editor* editor) override;
    void willClose(VSTGUI::VST3Editor* editor) override;

    // ==========================================================================
    // Phase 11 - session state the sub-controller drives (C-7b)
    // ==========================================================================
    // Session state, every one of them: no ParamID is involved, nothing here is
    // serialized, and nothing here reaches the audio thread except through
    // sendEditMessage(). UI thread only.

    void togglePresetBrowser();
    void toggleDrawer() noexcept;
    void setDrawerTab(int index) noexcept;
    void setCloudViewEditMode(bool edit) noexcept;

    /// Selects a morph slot and tells the processor (EditMessage kind 6).
    void setSelectedSlot(int slot);

    [[nodiscard]] int selectedSlot() const noexcept { return selectedSlot_; }

    /// The Blend A->B gesture's B endpoint. The selected slot is BOTH the
    /// pristine-A source and the destination (spec C-4's blend row), so B is the
    /// next slot cyclically - the phase's convention, chosen here once so the
    /// kind 7 snapshot and the kind 4 steps of one gesture can never disagree.
    [[nodiscard]] int blendSourceSlot() const noexcept {
        return (selectedSlot_ + 1) % 4;
    }

    /// Called from SeraphisEditSubController::verifyView for each of the five
    /// rings as the tree is built (they arrive through the ViewCreatorAdapter,
    /// so createCustomView never sees them).
    void registerMacroRing(UI::MacroRingKnob* ring) noexcept;

    [[nodiscard]] UI::CloudView* cloudView() const noexcept { return cloudView_; }
    [[nodiscard]] UI::DrawerContainer* drawer() const noexcept { return drawer_; }

    // --- T017 test seams ------------------------------------------------------
    [[nodiscard]] int subControllerInstanceCountForTest() const noexcept {
        return subControllerInstances_;
    }
    [[nodiscard]] Krate::Plugins::PresetBrowserView* presetBrowserForTest() const noexcept {
        return presetBrowserView_;
    }
    [[nodiscard]] Krate::Plugins::PresetManager* presetManagerForTest() const noexcept {
        return presetManager_.get();
    }
    [[nodiscard]] const std::array<UI::MacroRingKnob*, 5>& macroRingsForTest() const noexcept {
        return macroRings_;
    }

    // ==========================================================================
    // Phase 12 hotfix (2026-08-05) - the preset browser's load/save providers.
    // ==========================================================================
    // PresetManager is built with a null component (the processor lives across
    // the VST3 boundary), so WITHOUT these two callbacks loadPreset()/savePreset()
    // fail their first guard and the browser's double-click does nothing - the
    // shipped Phase 12 defect. Public because the preset tests drive them
    // directly (the Ruinae/Innexus precedent).

    /// The LOAD half. Applies a component-state stream (any version the EOF-safe
    /// chain accepts) to BOTH sides: the full stream is sent to the processor as
    /// ONE kind-8 EditMessage (applied there by setState() on the message
    /// thread - params, payloads, [partials] bits, force-push, exactly the
    /// project-load path), and every registered parameter is replayed through
    /// beginEdit/performEdit/endEdit so the HOST sees the change (undo, dirty
    /// flag, generic editors).
    bool loadComponentStateWithNotify(Steinberg::IBStream* state);

    /// The SAVE half. The processor's own getState() bytes, obtained through the
    /// host's component handler (FUnknownPtr<IComponent> - the Innexus
    /// precedent); null when the host does not expose IComponent there, which
    /// savePreset() reports as its no-source error. Caller owns the stream.
    [[nodiscard]] Steinberg::MemoryStream* createComponentStateStream();

    // ==========================================================================
    // Phase 11 - IMessage fallback for hosts without the native DataExchange API
    // ==========================================================================
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // ==========================================================================
    // Phase 11 - IDataExchangeReceiver (FR-016)
    // ==========================================================================
    // All THREE are pure virtual
    // (extern/vst3sdk/pluginterfaces/vst/ivstdataexchange.h:184, :192, :207).
    // Omit any one and Controller stays abstract, so createInstance's
    // `new Controller()` above does not compile.
    void PLUGIN_API queueOpened(Steinberg::Vst::DataExchangeUserContextID userContextID,
                                Steinberg::uint32 blockSize,
                                Steinberg::TBool& dispatchOnBackgroundThread) override;
    void PLUGIN_API queueClosed(
        Steinberg::Vst::DataExchangeUserContextID userContextID) override;
    void PLUGIN_API onDataExchangeBlocksReceived(
        Steinberg::Vst::DataExchangeUserContextID userContextID,
        Steinberg::uint32 numBlocks,
        Steinberg::Vst::DataExchangeBlock* blocks,
        Steinberg::TBool onBackgroundThread) override;

    OBJ_METHODS(Controller, EditControllerEx1)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(EditControllerEx1)
    DELEGATE_REFCOUNT(EditControllerEx1)

    /// The most recently received cloud frame. UI thread only - queueOpened
    /// answers `dispatchOnBackgroundThread = false`, which is what makes this
    /// mutex-free.
    [[nodiscard]] const CloudFrame& cachedCloudFrame() const noexcept {
        return cachedCloudFrame_;
    }

    // ==========================================================================
    // Phase 11 - the controller -> processor edit wire (T015 form, T018 EXTENDED)
    // ==========================================================================
    // T018 added the per-gesture throttle and the terminal flush BESIDE this
    // function (see beginEditGesture / onEditGestureValue / endEditGesture
    // below), not inside it: sendEditMessage stays the raw, unthrottled wire
    // call every immediate (non-gesture) send uses, and the throttle is the
    // gesture-shaped path layered on top. FR-048's rate limit applies to a
    // drag/slider gesture, which is exactly what that path is.
    //
    // It records what it was ASKED to send BEFORE the connection test, which is
    // what makes a gesture observable in a headless controller with no
    // processor attached; otherwise both SC-032 and SC-027 would be asserting
    // about a call that provably does nothing.
    //
    // UI thread only.
    void sendEditMessage(const UI::EditMessage& m) {
        lastSentEditMessage_ = m;
        ++editMessagesSent_;

        Steinberg::Vst::IMessage* msg = allocateMessage();
        if (msg == nullptr) {
            return;  // no host context (headless): recording already happened
        }
        msg->setMessageID(UI::kSeraphisEditMessageId);
        if (auto* attributes = msg->getAttributes()) {
            attributes->setBinary(UI::kSeraphisEditAttributeId, &m,
                                  static_cast<Steinberg::uint32>(sizeof(UI::EditMessage)));
        }
        sendMessage(msg);
        msg->release();
    }

    [[nodiscard]] const UI::EditMessage& lastSentEditMessageForTest() const noexcept {
        return lastSentEditMessage_;
    }

    /// Counts CALLS, not deliveries.
    [[nodiscard]] std::size_t editMessageSendCountForTest() const noexcept {
        return editMessagesSent_;
    }

    // ==========================================================================
    // Phase 11 T018 - the gesture path (FR-048, FR-046 clause 3, SC-027)
    // ==========================================================================
    // A drag or slider gesture is bracketed by beginEditGesture() /
    // endEditGesture() and streams its values through onEditGestureValue(). The
    // rate limit is kEditThrottleInterval; the gesture-end flush is
    // UNCONDITIONAL, so the value the user released the drag at can never be
    // swallowed by a window that had not expired.
    //
    // This is also FR-046 clause 3's mutation site: every value staged here is
    // applied to slotMirror_ with the SAME Layer 2 function the processor will
    // apply, IN ADDITION to being sent (FR-029 - the local write never
    // substitutes for the send). The two are never reconciled; C-11 clause 4
    // makes that divergence cosmetic by construction.

    void beginEditGesture() noexcept;
    void onEditGestureValue(const UI::EditMessage& m);
    void endEditGesture();

    [[nodiscard]] bool editGestureActiveForTest() const noexcept {
        return editThrottle_.active;
    }

    /// SC-026's observable. Counts OPEN EDITORS, and is reset in terminate() -
    /// never in willClose(), which fires once per closing view.
    [[nodiscard]] int editorOpenCountForTest() const noexcept { return editorOpenCount_; }

    /// The DISPLAY-ONLY SpectralState mirror (C-11, FR-046) - never serialized,
    /// never in process(), never read back from the processor. T018 added its
    /// two re-seed sources: the 409-412 dropdowns (setParamNormalized above,
    /// via makeFactoryState) and the state stream (setComponentState, via
    /// loadMorphParamsToController's payload decode). T015 only needs to read
    /// it, because a gesture carries the field it is NOT moving through
    /// unchanged.
    [[nodiscard]] const Krate::DSP::SpectralState& slotMirror(int slot) const noexcept {
        const std::size_t i =
            (slot < 0 || slot > 3) ? std::size_t{0} : static_cast<std::size_t>(slot);
        return slotMirror_[i];
    }

    void setSlotMirror(int slot, const Krate::DSP::SpectralState& s) noexcept {
        if (slot < 0 || slot > 3) {
            return;
        }
        slotMirror_[static_cast<std::size_t>(slot)] = s;
    }

private:
    /// FR-046 re-seed source 1's body. `slot` is 0..3; anything else is dropped.
    void reseedSlotMirrorFromDropdown(int slot, Steinberg::Vst::ParamValue value) noexcept;

    // --- Phase 12 hotfix (2026-08-05) -----------------------------------------
    /// The ONE decode chain (getState order), shared by setComponentState() and
    /// loadComponentStateWithNotify() so the two can never drift. The version
    /// int32 has already been read and accepted by the caller.
    void applyComponentStreamBody(
        Steinberg::IBStreamer& streamer,
        const std::function<void(Steinberg::Vst::ParamID, double)>& setParam);

    /// ONE kind-8 EditMessage carrying `size` bytes of component-state stream
    /// as the kSeraphisStateAttributeId attribute. Headless (no host context):
    /// recorded like every sendEditMessage() call, delivered nowhere.
    void sendPresetStateMessage(const void* data, Steinberg::uint32 size);

    /// FR-046 clause 3. Apply one authoring gesture value to slotMirror_, using
    /// the SAME Layer 2 mutator Processor::applyEditMessage will apply
    /// (processor.cpp:3706-3781). Kinds that carry no SpectralState change -
    /// the editor gate, pan and mask - are no-ops here by design.
    void applyEditToMirror(const UI::EditMessage& m) noexcept;

    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;

    // SDK helper that routes host DataExchange deliveries (and the IMessage
    // fallback in notify()) into this controller's IDataExchangeReceiver entry
    // points. WITHOUT THIS MEMBER the interface is advertised but never called.
    Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this};

    CloudFrame cachedCloudFrame_{};

    // Phase 11 (T015 minimal form; T018 extends). UI thread only.
    UI::EditMessage lastSentEditMessage_{};
    std::size_t editMessagesSent_ = 0;
    std::array<Krate::DSP::SpectralState, 4> slotMirror_{};

    /// T018. Which factory index each slot's mirror was last seeded from, so a
    /// dropdown that DID NOT MOVE does not discard an authored mirror. Mirrors
    /// the processor's own lastAuthoredSlotStateId_ (processor.h:1553) exactly:
    /// FR-035 is about MOVING the dropdown, and a host re-sending the value a
    /// parked control already has must not wipe the user's authoring.
    /// Seeded from kMorphSlotDefaultIndices in initialize(), alongside the
    /// mirror itself.
    std::array<int, 4> lastSlotStateIndex_{-1, -1, -1, -1};

    /// T018 (FR-048). The one in-flight gesture, and Q2's pristine-A snapshot
    /// for the mirror's local blend - the display-side counterpart of the
    /// processor's blendSnapshotA_ / blendSnapshotValid_ (processor.h:1558-1559).
    EditThrottle editThrottle_{};
    Krate::DSP::SpectralState mirrorBlendSnapshot_{};
    bool mirrorBlendSnapshotValid_ = false;

    /// T018 (FR-047). Editors currently open. Incremented in didOpen,
    /// decremented in willClose (floored at 0), RESET in terminate().
    int editorOpenCount_ = 0;

    // ==========================================================================
    // Phase 11 T017 - editor session state (C-7c, FR-041). UI thread only.
    // ==========================================================================
    // EVERY pointer below is a RAW, NON-OWNING cache of a view VSTGUI owns, and
    // every one of them is zeroed in willClose() - which is the whole point of
    // the teardown discipline: a cached pointer that outlives its frame is the
    // editor-lifecycle use-after-free the ASan/valgrind lanes exist to catch.
    VSTGUI::VST3Editor* activeEditor_ = nullptr;
    UI::CloudView* cloudView_ = nullptr;
    UI::DrawerContainer* drawer_ = nullptr;
    std::array<UI::MacroRingKnob*, 5> macroRings_{};
    Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;

    /// Counted up in createSubController and RESET TO 0 in willClose() - the
    /// documented trap: the count is per open editor, not per plugin instance.
    int subControllerInstances_ = 0;

    /// The morph slot every authoring gesture writes to. Session state: never a
    /// ParamID, never serialized.
    int selectedSlot_ = 0;
};

}  // namespace Seraphis
