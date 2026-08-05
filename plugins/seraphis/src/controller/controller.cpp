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

// Phase 11 T017. The custom views createCustomView owns, the sub-controller,
// and the shared preset browser the header button opens. outline_button.h is
// the shared flat-outline family every plugin's Presets button uses
// (2026-08-04 consistency pass).
#include "ui/cloud_view.h"
#include "ui/drawer_container.h"
#include "ui/edit_sub_controller.h"
#include "ui/outline_button.h"
#include "ui/preset_browser_view.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/common/memorystream.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/uidescription/uiattributes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// FR-052: the update config ships COMPILED BUT UNUSED in Phase 8 (no
// UpdateChecker instance -- it spawns a thread and a network fetch). This
// static_assert is the ONLY thing that compiles the header; a CMake source-list
// entry would NOT (CMake sets HEADER_FILE_ONLY on .h entries).
#include "update/seraphis_update_config.h"
static_assert(std::is_same_v<decltype(Seraphis::makeSeraphisUpdateConfig()),
                             Krate::Plugins::UpdateCheckerConfig>);

namespace Seraphis {

using namespace Steinberg;

namespace {

// ==============================================================================
// Phase 11 FR-034a (plan 6.4, 7.3) - the [partials] block, CONTROLLER side
// ==============================================================================
// The mirror of Processor::savePartialOverrides(), which is the AUTHORITY for
// this layout; the two are kept in step by Processor::getState's write order,
// exactly as every other block in this chain is. 272 bytes:
//
//   | Offset | Size | Field                                     |
//   |      0 |  256 | 64 x float pan, index order               |
//   |    256 |    8 | uint64 panOverrideBits                    |
//   |    264 |    8 | uint64 maskBits                           |
//
// WHY THE CONTROLLER READS IT AT ALL. Once audio is running the cloud view's
// override table arrives on the CloudFrame queue, which is authoritative. But
// setComponentState() runs at project load, potentially long before the first
// frame is published (C-2 clause 6 keeps the producer gated until an editor
// opens), and the view would otherwise draw a reloaded patch with no overrides
// until the user opened the editor. Seeding the cached frame here closes that
// window, and the next published frame simply overwrites it.
//
// It also keeps this chain's cursor in step with getState()'s write order - the
// same reason loadMorphParamsToController consumes the four 541-byte payloads it
// has nowhere to put. [partials] is LAST today; the next appended block would
// read from the wrong offset without this.
//
// EOF-SAFE, like every loader above it: a version-1, version-2 or Phase-10
// version-3 stream runs out before the block and every override stays absent -
// no version-aware branch anywhere. Because the pan array is read BEFORE the two
// masks, a partially truncated block leaves both masks 0 and every pan value
// therefore unreferenced.
void loadPartialOverridesToController(IBStreamer& streamer, CloudFrame& frame) {
    std::array<float, 64> pan{};
    for (float& value : pan) {
        if (!streamer.readFloat(value)) {
            return;
        }
    }
    uint64 panOverrideBits = 0;
    if (!streamer.readInt64u(panOverrideBits)) {
        return;
    }
    uint64 maskBits = 0;
    if (!streamer.readInt64u(maskBits)) {
        return;
    }

    // Only the AUTHORED pans are seeded. An un-authored slot keeps whatever the
    // frame already held, because a stored 0.0f there means "no override", not
    // "centred" - the processor's own re-push reads panOverrideBits the same way.
    for (std::size_t i = 0; i < pan.size(); ++i) {
        if (((panOverrideBits >> i) & 1u) != 0u) {
            frame.position[i] = std::clamp(pan[i], -1.0f, 1.0f);
        }
    }
    frame.maskBits = static_cast<std::uint64_t>(maskBits);
    // CloudFrame::overriddenBits is "pan AND/OR mask" (cloud_frame.h:36).
    frame.overriddenBits = static_cast<std::uint64_t>(panOverrideBits) | frame.maskBits;
}

}  // namespace

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

    // Phase 11 T018 (C-11, FR-046). The display mirror starts on the SAME four
    // factory states the four dropdowns were just registered with - the
    // processor seeds its own authoring mirror identically at construction
    // (processor.cpp:517-521), from the same makeFactoryState(). Without this
    // the cloud view would author against value-initialised states until the
    // user happened to move a dropdown.
    for (std::size_t s = 0; s < slotMirror_.size(); ++s) {
        const int index = kMorphSlotDefaultIndices[s];
        lastSlotStateIndex_[s] = index;
        slotMirror_[s] =
            Krate::DSP::makeFactoryState(static_cast<Krate::DSP::SpectralStateId>(index));
    }

    // FR-050. NO UpdateChecker (FR-052).
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        makeSeraphisPresetConfig(), nullptr, this);
    // Phase 12 hotfix (2026-08-05): the two provider callbacks WITHOUT WHICH the
    // browser is inert - loadPreset()/savePreset() fail their first guard when
    // the manager has neither a component nor a provider, the browser stays
    // open on the failed load, and no control moves. Every other plugin in the
    // monorepo wires both; Seraphis shipped Phase 12 without them.
    presetManager_->setStateProvider(
        [this]() -> Steinberg::IBStream* { return createComponentStateStream(); });
    presetManager_->setLoadProvider(
        [this](Steinberg::IBStream* stream, const Krate::Plugins::PresetInfo& /*info*/) {
            return loadComponentStateWithNotify(stream);
        });

    return kResultOk;
}

tresult PLUGIN_API Controller::terminate() {
    // FR-047. The refcount is reset HERE and never in willClose(): willClose
    // fires once per closing view and the count has to survive it to do its
    // job, while terminate() fires once per plugin instance. A count left
    // non-zero here would make the next didOpen() a 1 -> 2 transition and the
    // producer's gate would never be re-opened.
    editorOpenCount_ = 0;

    presetManager_.reset();
    return EditControllerEx1::terminate();
}

// ==============================================================================
// Phase 11 T018 - FR-046 re-seed source 1: the 409-412 slot dropdowns
// ==============================================================================
tresult PLUGIN_API Controller::setParamNormalized(Vst::ParamID tag, Vst::ParamValue value) {
    const tresult result = EditControllerEx1::setParamNormalized(tag, value);

    if (tag >= kMorphState0Id && tag <= kMorphState3Id) {
        reseedSlotMirrorFromDropdown(static_cast<int>(tag - kMorphState0Id), value);
    }

    return result;
}

void Controller::reseedSlotMirrorFromDropdown(int slot, Vst::ParamValue value) noexcept {
    if (slot < 0 || std::cmp_greater_equal(slot, slotMirror_.size())) {
        return;
    }
    const auto s = static_cast<std::size_t>(slot);

    // The SAME denormalization the pack's own handler applies to these four IDs
    // (morph_params.h's kMorphState0Id..kMorphState3Id case), out of the SAME
    // label table they were registered from - so the index the controller
    // derives cannot drift from the one the processor derives.
    const int index = std::clamp(
        detail::morphDropdownIndex(value, static_cast<int>(kSpectralStateLabels.size())), 0,
        static_cast<int>(Krate::DSP::kSpectralStateCount) - 1);

    if (index == lastSlotStateIndex_[s]) {
        // FR-035 is about MOVING the dropdown. An unchanged value leaves any
        // authored mirror alone - the same rule the processor's
        // syncAuthoringMirrorFromDropdowns() states (processor.cpp:3627).
        return;
    }
    lastSlotStateIndex_[s] = index;
    slotMirror_[s] = Krate::DSP::makeFactoryState(static_cast<Krate::DSP::SpectralStateId>(index));
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

    applyComponentStreamBody(streamer,
                             [this](Vst::ParamID id, double value) {
                                 setParamNormalized(id, value);
                             });

    return kResultOk;
}

// Phase 12 hotfix (2026-08-05): setComponentState()'s decode chain, extracted
// VERBATIM so loadComponentStateWithNotify() cannot drift from it - the block
// order below is the third copy of getState()'s write order in the codebase
// (after the processor's own and the harness's), and it must never become four.
void Controller::applyComponentStreamBody(
    IBStreamer& streamer, const std::function<void(Vst::ParamID, double)>& setParam) {
    // Order MUST match Processor::getState EXACTLY (plan 5.1's write order). The
    // seed is its own trio positioned AFTER [macro] (FR-091a, global_params.h:
    // 259-271), and loadMorphParamsToController consumes the four 541-byte
    // payloads - decoding them into slotMirror_ since Phase 11 T018, and before
    // that discarding them - without which the following [life]/[body]/[atmos]/
    // [aether] blocks (55 parameters) would be read 2164 bytes off (plan 2.3.0).
    // Every loader is EOF-safe, so a 36-byte version-1 stream stops after
    // [macro] and leaves the remaining 99 parameters at their registered
    // defaults (FR-093), and a version-2 stream stops before [effects] and
    // leaves Phase 10's 16 at theirs (spec C-8).
    loadGlobalParamsToController(streamer, setParam);      // 0, 1, 2
    loadMacroParamsToController(streamer, setParam);       // 100-104
    loadGlobalSeedToController(streamer, setParam);        // 3
    loadCloudParamsToController(streamer, setParam);       // 200-210
    // Phase 11 T018, FR-046 re-seed source 2. The four 541-byte payloads are no
    // longer discarded: they land in slotMirror_, so a reloaded project shows
    // the user's EDITED states rather than the factory states the dropdowns
    // name. The ORDER inside that loader is load-bearing - it replays the four
    // dropdown values through setParam (and therefore through
    // setParamNormalized's factory re-seed) FIRST, then overwrites each entry
    // with the stream's own payload, so the stream always wins.
    loadMorphParamsToController(streamer, setParam, slotMirror_);  // 400-412 + payloads
    loadLifeModParamsToController(streamer, setParam);     // 600-604, 700-704
    loadBodyParamsToController(streamer, setParam);        // 800-812
    loadAtmosphereParamsToController(streamer, setParam);  // 1000-1016
    loadAetherParamsToController(streamer, setParam);      // 1200-1217
    loadEffectsParamsToController(streamer, setParam);     // 1400-1443 (v3)
    // Phase 11 FR-034a. The [partials] block, appended LAST by getState(). It
    // carries NO parameter - drawer/mode/selection and the per-partial override
    // table are session state, never a ParamID - so it takes the cached frame
    // rather than setParam.
    loadPartialOverridesToController(streamer, cachedCloudFrame_);  // [partials] (LAST)
}

// ==============================================================================
// Phase 12 hotfix (2026-08-05) - preset browser load/save providers
// ==============================================================================
// Shipped Phase 12 built the PresetManager with a null component and NO
// callbacks, so loadPreset()/savePreset() failed their first guard and the
// browser's double-click did nothing. These two are the missing halves; the
// wiring is in initialize().

bool Controller::loadComponentStateWithNotify(IBStream* state) {
    if (state == nullptr) {
        return false;
    }

    // Read the stream ONCE into a buffer: it is both the kind-8 wire payload
    // (the processor applies it with setState()) and the local decode source.
    // The cap mirrors the wire format's own (edit_message.h); a stream past it
    // is not a Seraphis state.
    std::vector<char> bytes;
    bytes.reserve(4096);
    char chunk[4096];
    Steinberg::int32 numRead = 0;
    while (state->read(chunk, static_cast<Steinberg::int32>(sizeof(chunk)), &numRead)
               == kResultOk
           && numRead > 0) {
        bytes.insert(bytes.end(), chunk, chunk + numRead);
        if (bytes.size() > UI::kMaxPresetStateBytes) {
            return false;
        }
    }
    if (bytes.size() < sizeof(int32)) {
        return false;
    }

    // The SAME version acceptance setState() applies: refuse only the future.
    Steinberg::MemoryStream wrap(bytes.data(), static_cast<Steinberg::TSize>(bytes.size()));
    IBStreamer streamer(&wrap, kLittleEndian);
    int32 version = 0;
    if (!streamer.readInt32(version)) {
        return false;
    }
    if (version < 1 || version > kCurrentStateVersion) {
        return false;
    }

    // (1) THE PROCESSOR, full fidelity, ONE message: setState() over there is
    // the project-load path, so spectral payloads, the [partials] override
    // BITS, the authoring-mirror trackers and the force-push all behave exactly
    // as on project load - things a per-parameter replay can never carry.
    sendPresetStateMessage(bytes.data(), static_cast<Steinberg::uint32>(bytes.size()));

    // (2) THE HOST AND THIS CONTROLLER: the shared decode chain, with every
    // parameter replayed through the edit trio so the host records the change
    // (undo, dirty flag, generic editors, automation touch) - the Ruinae/
    // Innexus loadComponentStateWithNotify contract. setParamNormalized is the
    // virtual, so the dropdown re-seed and the payload overwrite inside the
    // chain behave exactly as in setComponentState().
    applyComponentStreamBody(streamer, [this](Vst::ParamID id, double value) {
        const double clamped = std::clamp(value, 0.0, 1.0);
        setParamNormalized(id, clamped);
        beginEdit(id);
        performEdit(id, clamped);
        endEdit(id);
    });

    return true;
}

Steinberg::MemoryStream* Controller::createComponentStateStream() {
    // The Innexus precedent (controller_presets.cpp:98-111): the host's
    // component handler is where a single-component host exposes IComponent.
    // Hosts that do not are answered with null, which savePreset() reports as
    // its no-source error - honest failure, never a drifted third serializer.
    Steinberg::FUnknownPtr<Vst::IComponent> component(getComponentHandler());
    if (!component) {
        return nullptr;
    }

    auto* stream = new Steinberg::MemoryStream();
    if (component->getState(stream) != kResultOk) {
        stream->release();
        return nullptr;
    }
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    return stream;
}

void Controller::sendPresetStateMessage(const void* data, Steinberg::uint32 size) {
    UI::EditMessage m{};
    m.kind = UI::kEditKindPresetState;
    lastSentEditMessage_ = m;
    ++editMessagesSent_;

    Steinberg::Vst::IMessage* msg = allocateMessage();
    if (msg == nullptr) {
        return;  // no host context (headless): recording already happened
    }
    msg->setMessageID(UI::kSeraphisEditMessageId);
    if (auto* attributes = msg->getAttributes()) {
        attributes->setBinary(UI::kSeraphisEditAttributeId, &m,
                              static_cast<Steinberg::uint32>(sizeof(m)));
        attributes->setBinary(UI::kSeraphisStateAttributeId, data, size);
    }
    sendMessage(msg);
    msg->release();
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

// ==============================================================================
// Phase 11 T017 - VST3EditorDelegate: custom views, sub-controller, teardown
// ==============================================================================

VSTGUI::CView* Controller::createCustomView(VSTGUI::UTF8StringPtr name,
                                            const VSTGUI::UIAttributes& attributes,
                                            const VSTGUI::IUIDescription* /*description*/,
                                            VSTGUI::VST3Editor* /*editor*/) {
    if (name == nullptr) {
        return nullptr;
    }

    // The view factory does NOT decorate a createCustomView view with the node's
    // geometry, so the rect is built here from the uidesc attributes - Membrum's
    // pattern (plugins/membrum/src/controller/controller.cpp:1046-1052).
    VSTGUI::CPoint origin(0.0, 0.0);
    VSTGUI::CPoint size(100.0, 100.0);
    attributes.getPointAttribute("origin", origin);
    attributes.getPointAttribute("size", size);
    const VSTGUI::CRect viewRect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);

    // MacroRingKnob is deliberately absent: it is a ViewCreatorAdapter (C-7a),
    // because it must accept control-tag and the rest of the CControl attribute
    // set, which a createCustomView view never receives.
    if (std::strcmp(name, "CloudView") == 0) {
        cloudView_ = new UI::CloudView(viewRect, this);
        return cloudView_;
    }
    if (std::strcmp(name, "DrawerContainer") == 0) {
        drawer_ = new UI::DrawerContainer(viewRect);
        return drawer_;
    }
    // The header preset button: the SHARED flat-outline renderer every other
    // plugin's Presets button uses (Krate::Plugins::drawOutlineButton). It is a
    // CControl, so verifyView() gives it its session tag (9000) and this
    // listener from the session-tag attribute - one valueChanged per click.
    if (std::strcmp(name, "PresetButton") == 0) {
        return new Krate::Plugins::OutlineBrowserButton(viewRect, nullptr, -1, "PRESETS");
    }
    return nullptr;
}

VSTGUI::IController* Controller::createSubController(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* editor) {

    if (name == nullptr || std::strcmp(name, "SeraphisEdit") != 0) {
        return nullptr;
    }

    // The caller owns the result: UIDescription stores it on the view as
    // kCViewControllerAttribute and CView's destructor deletes it
    // (uidescription.cpp:741-755). `editor` is the IController this one
    // delegates everything it does not claim to; it is null only in a headless
    // test that builds the sub-controller directly.
    ++subControllerInstances_;
    return new UI::SeraphisEditSubController(this, editor);
}

void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    activeEditor_ = editor;

    // The seven drawer pages arrive from the uidesc AFTER DrawerContainer's
    // constructor runs, so "exactly one page visible" is established here, once
    // the tree exists (drawer_container.h:85-89).
    if (drawer_ != nullptr) {
        drawer_->setActiveTab(0);
    }

    // FR-047 / SC-026 (T018). REFCOUNTED, never per-view: with two editors open
    // on one instance, the first one to close must not shut the producer down
    // under the second. Only the 0 -> 1 transition opens the gate.
    //
    // The processor's own gate stays the plain std::atomic<bool> C-2 clause 6
    // describes; it never learns the view count.
    if (editorOpenCount_++ == 0) {
        UI::EditMessage message{};
        message.kind = static_cast<std::uint8_t>(0);
        message.a = 1.0f;
        sendEditMessage(message);
    }
}

void Controller::willClose(VSTGUI::VST3Editor* /*editor*/) {
    // The frame is about to destroy every view below, so the caches are dropped
    // FIRST - a cached raw pointer that outlives its frame is precisely the
    // editor-lifecycle use-after-free the ASan/valgrind lanes exist to catch
    // (C-7c, FR-041). Each view cancels its own timer in removed().
    if (presetBrowserView_ != nullptr) {
        // Unregisters the frame's keyboard hook while the frame is still alive.
        presetBrowserView_->close();
        presetBrowserView_ = nullptr;  // the frame owns and destroys it
    }
    cloudView_ = nullptr;
    drawer_ = nullptr;
    macroRings_.fill(nullptr);
    activeEditor_ = nullptr;

    // The documented trap: the sub-controller count is per OPEN EDITOR, so it is
    // reset here and NOT in terminate().
    subControllerInstances_ = 0;

    // A gesture cannot outlive the views it was being driven from. Abandoned
    // rather than flushed: the control it came from is already gone, so the
    // flush would be authoring from a dead view.
    editThrottle_ = EditThrottle{};
    mirrorBlendSnapshotValid_ = false;

    // FR-047 / SC-026 (T018), the closing half. Only the 1 -> 0 transition
    // closes the gate, and the count is FLOORED at 0: an unpaired willClose()
    // (a host that closes a view it never opened through didOpen) must not
    // drive it negative, or the next didOpen would be a -1 -> 0 transition and
    // the gate would stay shut for good.
    if (editorOpenCount_ > 0) {
        if (--editorOpenCount_ == 0) {
            UI::EditMessage message{};
            message.kind = static_cast<std::uint8_t>(0);
            message.a = 0.0f;
            sendEditMessage(message);
        }
    } else {
        editorOpenCount_ = 0;
    }
}

// ==============================================================================
// Phase 11 T017 - the session-state surface the sub-controller drives (C-7b)
// ==============================================================================

void Controller::togglePresetBrowser() {
    if (activeEditor_ == nullptr || presetManager_ == nullptr) {
        return;
    }
    VSTGUI::CFrame* frame = activeEditor_->getFrame();
    if (frame == nullptr) {
        return;
    }

    if (presetBrowserView_ != nullptr) {
        presetBrowserView_->close();
        // removeView's default withForget destroys it, so the browser is really
        // GONE from the frame rather than merely hidden.
        frame->removeView(presetBrowserView_);
        presetBrowserView_ = nullptr;
        return;
    }

    // "All" first, then the config's own subcategory list, so the browser's tabs
    // cannot drift from makeSeraphisPresetConfig()'s single source of truth.
    // The construction itself lives in seraphis_preset_config.h so that SC-008's
    // test asserts against THIS vector rather than a re-typed copy of it.
    std::vector<std::string> tabLabels = makeSeraphisPresetTabLabels();

    presetBrowserView_ = new Krate::Plugins::PresetBrowserView(
        frame->getViewSize(), presetManager_.get(), std::move(tabLabels));
    frame->addView(presetBrowserView_);
    presetBrowserView_->open();
}

void Controller::toggleDrawer() noexcept {
    if (drawer_ != nullptr) {
        drawer_->setOpen(!drawer_->isOpen());
    }
}

void Controller::setDrawerTab(int index) noexcept {
    if (drawer_ != nullptr) {
        drawer_->setActiveTab(index);
        // A tab click on a COLLAPSED drawer must open it: the pages live below
        // the 30 px strip, so switching visibility alone is invisible to the
        // user (the 2026-08-04 dead-tabs bug). setOpen(true) is idempotent, so
        // the kick-style double-fire is harmless here.
        drawer_->setOpen(true);
    }
}

void Controller::setCloudViewEditMode(bool edit) noexcept {
    if (cloudView_ != nullptr) {
        cloudView_->setMode(edit ? UI::CloudView::Mode::Edit : UI::CloudView::Mode::Observe);
    }
}

void Controller::setSelectedSlot(int slot) {
    if (slot < 0 || slot > 3) {
        return;
    }
    selectedSlot_ = slot;
    // T018: a slot change ENDS any blend gesture on the display mirror, exactly
    // as kind 6 does on the processor (processor.cpp's applyEditMessage case 6).
    // Without it the next kind 4 would blend from a snapshot of the OTHER slot.
    mirrorBlendSnapshotValid_ = false;
    if (cloudView_ != nullptr) {
        cloudView_->setSelectedSlot(slot);
    }

    UI::EditMessage message{};
    message.kind = static_cast<std::uint8_t>(6);
    message.slot = static_cast<std::uint8_t>(slot);
    sendEditMessage(message);
}

void Controller::registerMacroRing(UI::MacroRingKnob* ring) noexcept {
    if (ring == nullptr) {
        return;
    }
    for (UI::MacroRingKnob*& cached : macroRings_) {
        if (cached == ring) {
            return;
        }
        if (cached == nullptr) {
            cached = ring;
            return;
        }
    }
    // A sixth ring in the document is a uidesc defect, and SC-004 arm 1 (which
    // counts them by dynamic_cast in the built frame) is its detector. Dropping
    // the overflow here is what keeps this method from writing out of bounds.
}

// ==============================================================================
// Phase 11 T018 - the gesture throttle and its terminal flush (FR-048, SC-027)
// ==============================================================================
// UI thread only. One gesture at a time, because one pointer is in flight at a
// time; a beginEditGesture() while another gesture is live simply restarts the
// window, which is what an interrupted drag should do.

void Controller::beginEditGesture() noexcept {
    editThrottle_.active = true;
    editThrottle_.hasPending = false;
    editThrottle_.sawValue = false;
    editThrottle_.pending = UI::EditMessage{};
    // The CLOCK EPOCH, so the FIRST value of a gesture is never withheld: the
    // user must see the drag respond immediately, not 33 ms later.
    editThrottle_.lastSend = std::chrono::steady_clock::time_point{};
}

void Controller::onEditGestureValue(const UI::EditMessage& m) {
    editThrottle_.pending = m;
    editThrottle_.hasPending = true;
    editThrottle_.sawValue = true;

    // FR-046 clause 3 / FR-029: the local mirror write happens for EVERY staged
    // value, whether or not the throttle lets this one onto the wire, and it
    // never substitutes for a send.
    applyEditToMirror(m);

    const auto now = std::chrono::steady_clock::now();
    if ((now - editThrottle_.lastSend) >= kEditThrottleInterval) {
        sendEditMessage(editThrottle_.pending);
        editThrottle_.lastSend = now;
        editThrottle_.hasPending = false;
    }
}

void Controller::endEditGesture() {
    // UNCONDITIONAL for any gesture that staged a value - including the case
    // where the throttle has just sent an identical message. FR-048 accepts at
    // most one redundant duplicate; it does NOT accept a dropped final value,
    // which is what a `if (hasPending)` flush would produce whenever the user
    // released the drag inside an unexpired window.
    if (editThrottle_.sawValue) {
        sendEditMessage(editThrottle_.pending);
    }
    editThrottle_.active = false;
    editThrottle_.hasPending = false;
    editThrottle_.sawValue = false;
}

// ==============================================================================
// Phase 11 T018 - FR-046 clause 3: the ONE mirror mutation site
// ==============================================================================
// The same Layer 2 functions Processor::applyEditMessage applies, in the same
// order and with the same guards (processor.cpp:3706-3781), so the display and
// the audio move together without the mirror ever being read back from the
// processor (C-11 clause 4 - divergence is cosmetic by construction).
//
// Kinds 0 (editor gate), 2 (pan) and 3 (mask) carry no SpectralState change and
// are deliberately absent: pan and mask live on HarmonicCloud's per-partial
// override table, not in a morph slot.
void Controller::applyEditToMirror(const UI::EditMessage& m) noexcept {
    const auto slot = static_cast<std::size_t>(m.slot);
    if (slot >= slotMirror_.size()) {
        return;
    }

    switch (m.kind) {
        case 1:  // PartialRatioAmp
            Krate::DSP::setPartial(slotMirror_[slot], static_cast<std::size_t>(m.index), m.a,
                                   m.b);
            return;

        case 4: {  // BlendStates - ABSOLUTE, from the latched pristine A (Q2)
            if (!mirrorBlendSnapshotValid_) {
                return;  // no live kind-7 snapshot for this gesture -> DROPPED
            }
            // The range test happens IN FLOAT, before the cast, for the same
            // reason the processor's does: `b` carries slot B as a float and a
            // static_cast<int> of a float outside int's range is undefined.
            if (!(m.b >= 0.0f) || !(m.b < 4.0f)) {
                return;
            }
            const auto bIndex = static_cast<std::size_t>(m.b);
            slotMirror_[slot] =
                Krate::DSP::blendStates(mirrorBlendSnapshot_, slotMirror_[bIndex], m.a);
            return;
        }

        case 5:  // TiltState - `a` is an ABSOLUTE dB/oct (C-6), never a delta
            Krate::DSP::tiltState(slotMirror_[slot], m.a);
            return;

        case 6:  // SlotSelect - a slot change ENDS any gesture
            mirrorBlendSnapshotValid_ = false;
            return;

        case 7:  // BlendBegin - latches A and writes nothing
            mirrorBlendSnapshot_ = slotMirror_[slot];
            mirrorBlendSnapshotValid_ = true;
            return;

        default:
            return;
    }
}

// ==============================================================================
// Phase 11 - IDataExchangeReceiver (FR-016, SC-006 arm (h))
// ==============================================================================

void PLUGIN_API Controller::queueOpened(
    Vst::DataExchangeUserContextID /*userContextID*/,
    uint32 /*blockSize*/,
    TBool& dispatchOnBackgroundThread) {
    // UI thread. cachedCloudFrame_ is then written and read on one thread only,
    // so the cloud view's timer needs no mutex to read it.
    dispatchOnBackgroundThread = static_cast<TBool>(false);
}

void PLUGIN_API Controller::queueClosed(Vst::DataExchangeUserContextID /*userContextID*/) {
    // Nothing to release - cachedCloudFrame_ is a POD value, not a view into
    // the queue's memory.
}

void PLUGIN_API Controller::onDataExchangeBlocksReceived(
    Vst::DataExchangeUserContextID /*userContextID*/,
    uint32 numBlocks,
    Vst::DataExchangeBlock* blocks,
    TBool /*onBackgroundThread*/) {
    if (blocks == nullptr) {
        return;
    }

    // MOST RECENT WINS. Every valid block in the delivery overwrites the cache,
    // so the LAST one survives; older frames are discarded, never queued. A
    // cloud view redrawing at 30 Hz wants the newest constellation, not a
    // backlog (Membrum's rule, controller.cpp:1719-1726).
    for (uint32 i = 0; i < numBlocks; ++i) {
        if (blocks[i].data != nullptr && blocks[i].size >= sizeof(CloudFrame)) {
            std::memcpy(&cachedCloudFrame_, blocks[i].data, sizeof(CloudFrame));
        }
    }
}

// ==============================================================================
// IMessage fallback - hosts without the native DataExchange API deliver blocks
// as IMessage payloads; the SDK helper decodes them and calls
// onDataExchangeBlocksReceived() above. Returns true iff the message WAS such a
// payload, so anything else still reaches EditControllerEx1.
// ==============================================================================
tresult PLUGIN_API Controller::notify(Vst::IMessage* message) {
    if (message == nullptr) {
        return EditControllerEx1::notify(message);
    }

    if (dataExchangeReceiver_.onMessage(message)) {
        return kResultOk;
    }

    return EditControllerEx1::notify(message);
}

}  // namespace Seraphis
