// =============================================================================
// Preset LOAD path - the browser's double-click seam (Phase 12 hotfix)
// =============================================================================
// Phase 12 shipped 42 factory presets and a harness that proves every one of
// them loads through Processor::setState() - and NOT ONE test drove the path a
// user actually clicks: PresetBrowserView double-click -> PresetManager::
// loadPreset() -> the controller's LOAD PROVIDER. That provider was never
// wired (Controller::initialize built the PresetManager with a null component
// and no callbacks), so loadPreset() failed its first guard, the browser
// stayed open, and no control moved. This TU is the missing seam coverage:
//
//   1. the provider is wired and a browser-shaped loadPreset() call really
//      applies the preset - to the HOST (begin/perform/endEdit per parameter)
//      and to the PROCESSOR (full state, via ONE kind-8 EditMessage that
//      carries the raw component-state stream and is applied by the
//      processor's own setState() on the message thread);
//   2. an AUTHORED spectral slot survives a save -> load round trip through a
//      real .vstpreset container (the full-fidelity ruling, 2026-08-05);
//   3. the [partials] override table is restored INCLUDING its override bits,
//      and a factory preset (all-zero block) CLEARS stale overrides - the
//      property the per-partial kinds 2/3 cannot express (kind 2 can only SET
//      a pan-override bit, never clear it);
//   4. the save half: createComponentStateStream() returns the processor's
//      own getState() bytes when the host exposes IComponent through the
//      component handler (the Innexus precedent), and null when it does not.
//
// The pair is REAL on both sides: a real Processor (ProcessorFixture) and a
// real Controller, connected controller->processor so sendMessage() lands in
// Processor::notify() exactly as a host's connection proxy would deliver it.
// =============================================================================

#include <catch2/catch_test_macros.hpp>

#include "../seraphis_test_fixture.h"
#include "../preset_test_support.h"

#include "controller/controller.h"
#include "plugin_ids.h"
#include "ui/edit_message.h"

#include <krate/dsp/processors/spectral_state.h>

#include <base/source/fstreamer.h>
#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/vstpresetfile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::SpectralState;
using SeraphisTest::ProcessorFixture;

// -----------------------------------------------------------------------------
// MEMBER-WISE state equality - partial_edit_test.cpp's rule: never memcmp a
// struct of floats (object representation is not value identity).
// -----------------------------------------------------------------------------
[[nodiscard]] bool statesEqual(const SpectralState& a, const SpectralState& b) noexcept {
    return a.ratios == b.ratios && a.amplitudes == b.amplitudes && a.name == b.name
           && a.tiltDbPerOct == b.tiltDbPerOct && a.inharmonicity == b.inharmonicity
           && a.numPartials == b.numPartials;
}

// -----------------------------------------------------------------------------
// Component handler stub: records begin/perform/endEdit, and (for the save
// case) answers IComponent by FORWARDING the query to a real Processor - a
// Processor IS an IComponent, so no method stubs are needed.
// -----------------------------------------------------------------------------
struct EditRecord {
    enum class Action : std::uint8_t { Begin, Perform, End };
    Action action;
    Steinberg::Vst::ParamID id;
    Steinberg::Vst::ParamValue value;  // meaningful for Perform only
};

class MockComponentHandler final : public Steinberg::Vst::IComponentHandler {
public:
    std::vector<EditRecord> records;
    Steinberg::Vst::IComponent* componentTarget = nullptr;  // save case only

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override {
        records.push_back({.action = EditRecord::Action::Begin, .id = id, .value = 0.0});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue value) override {
        records.push_back({.action = EditRecord::Action::Perform, .id = id, .value = value});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override {
        records.push_back({.action = EditRecord::Action::End, .id = id, .value = 0.0});
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 /*flags*/) override {
        return Steinberg::kResultOk;
    }

    [[nodiscard]] std::size_t performCount() const noexcept {
        std::size_t n = 0;
        for (const auto& r : records) {
            if (r.action == EditRecord::Action::Perform) {
                ++n;
            }
        }
        return n;
    }
    [[nodiscard]] bool sawPerform(Steinberg::Vst::ParamID id) const noexcept {
        return std::ranges::any_of(records, [id](const EditRecord& r) {
            return r.action == EditRecord::Action::Perform && r.id == id;
        });
    }

    // FUnknown - stack-owned in every test, so ref counts are inert.
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid)
            || Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        // The Innexus save precedent: the HOST'S handler is where the controller
        // looks for IComponent. Forward the query to the real processor.
        if (componentTarget != nullptr
            && Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponent::iid)) {
            return componentTarget->queryInterface(iid, obj);
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

// -----------------------------------------------------------------------------
// The connected pair: real Processor + real Controller, controller's
// sendMessage() delivered straight into Processor::notify().
// -----------------------------------------------------------------------------
struct ConnectedPair {
    ProcessorFixture fx;
    Steinberg::Vst::HostApplication hostApp;
    Seraphis::Controller ctrl;
    MockComponentHandler handler;

    [[nodiscard]] bool up(double sampleRate = 44100.0, Steinberg::int32 blockSize = 512) {
        if (fx.prepare(sampleRate, blockSize) != Steinberg::kResultOk) {
            return false;
        }
        if (ctrl.initialize(&hostApp) != Steinberg::kResultOk) {
            return false;
        }
        ctrl.setComponentHandler(&handler);
        // Controller -> processor, the direction the load provider sends on.
        return ctrl.connect(fx.proc.get()) == Steinberg::kResultOk;
    }

    void down() {
        ctrl.disconnect(fx.proc.get());
        ctrl.terminate();
    }
};

// -----------------------------------------------------------------------------
// Preset plumbing helpers
// -----------------------------------------------------------------------------
[[nodiscard]] Krate::Plugins::PresetInfo infoFor(const std::filesystem::path& path) {
    Krate::Plugins::PresetInfo info;
    info.name = path.stem().string();
    info.subcategory = path.parent_path().filename().string();
    info.path = path;
    info.isFactory = true;
    return info;
}

/// Serialize `proc`'s state into a REAL .vstpreset container on disk.
[[nodiscard]] std::filesystem::path writeTempPreset(Seraphis::Processor& proc,
                                                    const char* fileName) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "seraphis_preset_load_test";
    fs::create_directories(dir);
    const fs::path path = dir / fileName;

    auto comp = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(proc.getState(comp) == Steinberg::kResultOk);
    REQUIRE(comp->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr) == Steinberg::kResultOk);

    auto* file = Steinberg::Vst::FileStream::open(path.string().c_str(), "wb");
    REQUIRE(file != nullptr);
    const bool saved = Steinberg::Vst::PresetFile::savePreset(
        file, Steinberg::FUID::fromTUID(Seraphis::kProcessorUID), comp);
    file->release();
    REQUIRE(saved);
    return path;
}

/// One authoring edit, byte-for-byte the controller's wire shape - the
/// partial_edit_test.cpp pattern.
void sendEdit(Seraphis::Processor& processor, std::uint8_t kind, std::uint8_t slot,
              std::uint16_t index, float a, float b) {
    auto message = Steinberg::owned(new Steinberg::Vst::HostMessage());
    message->setMessageID(Seraphis::UI::kSeraphisEditMessageId);
    Steinberg::Vst::IAttributeList* attributes = message->getAttributes();
    REQUIRE(attributes != nullptr);
    Seraphis::UI::EditMessage m{};
    m.kind = kind;
    m.slot = slot;
    m.index = index;
    m.a = a;
    m.b = b;
    REQUIRE(attributes->setBinary(Seraphis::UI::kSeraphisEditAttributeId, &m,
                                  static_cast<Steinberg::uint32>(sizeof(m)))
            == Steinberg::kResultOk);
    REQUIRE(processor.notify(message) == Steinberg::kResultOk);
}

/// Dropdown normalized value -> label index, the registration's own rounding.
[[nodiscard]] int dropdownIndex(double normalized, int labelCount) noexcept {
    return static_cast<int>(std::lround(normalized * static_cast<double>(labelCount - 1)));
}

}  // namespace

// =============================================================================
// 1. THE REGRESSION - the browser's own call, end to end
// =============================================================================
TEST_CASE("Seraphis_PresetLoad_BrowserLoadAppliesPresetToHostAndProcessor",
          "[preset_load][phase12]") {
    const auto files = SeraphisTest::allPresetFiles();
    REQUIRE_FALSE(files.empty());
    // A Bells preset: bell states make slot payloads distinctive.
    std::filesystem::path pick = files.front();
    for (const auto& f : files) {
        if (f.parent_path().filename() == "Bells") {
            pick = f;
            break;
        }
    }

    ConnectedPair pair;
    REQUIRE(pair.up());

    // The EXACT call PresetBrowserView::onPresetDoubleClicked makes. Before the
    // fix this returned false ("No component or load provider available") and
    // the browser stayed open.
    Krate::Plugins::PresetManager* manager = pair.ctrl.presetManagerForTest();
    REQUIRE(manager != nullptr);
    const bool loaded = manager->loadPreset(infoFor(pick));
    INFO("loadPreset error: " << manager->getLastError());
    REQUIRE(loaded);

    // --- The HOST heard about every parameter (undo / dirty / automation) ----
    REQUIRE(pair.handler.performCount() >= 100);  // the surface is 107 params
    REQUIRE(pair.handler.sawPerform(Seraphis::kMasterGainId));
    REQUIRE(pair.handler.sawPerform(Seraphis::kPolyphonyId));
    REQUIRE(pair.handler.sawPerform(Seraphis::kMorphState0Id));

    // --- The CONTROLLER's params match the committed bytes -------------------
    const auto parsed = SeraphisTest::parseVstPreset(pick);
    INFO("parse error: " << parsed.parseError);
    REQUIRE(parsed.parseError.empty());
    SeraphisTest::DecodedPresetState decoded;
    std::string why;
    REQUIRE(SeraphisTest::decodePresetState(parsed.comp, decoded, why));
    SeraphisTest::decodeSpectralPayloads(parsed.comp, decoded.payloads, decoded.payloadDecoded);
    for (int s = 0; s < 4; ++s) {
        REQUIRE(decoded.payloadDecoded[static_cast<std::size_t>(s)]);
        const auto id = static_cast<Steinberg::Vst::ParamID>(Seraphis::kMorphState0Id + s);
        REQUIRE(dropdownIndex(pair.ctrl.getParamNormalized(id),
                              static_cast<int>(Krate::DSP::kSpectralStateCount))
                == decoded.morph.slot[static_cast<std::size_t>(s)].load());
    }

    // --- The PROCESSOR holds the preset's exact payloads (full fidelity) -----
    REQUIRE(pair.fx.processBlock(512) == Steinberg::kResultOk);
    for (int s = 0; s < 4; ++s) {
        INFO("slot " << s);
        REQUIRE(statesEqual(pair.fx.proc->spectralSlotForTest(s),
                            decoded.payloads[static_cast<std::size_t>(s)]));
    }

    pair.down();
}

// =============================================================================
// 2. FULL FIDELITY - an AUTHORED slot survives save -> container -> load
// =============================================================================
TEST_CASE("Seraphis_PresetLoad_AuthoredSlotStateSurvivesRoundTrip",
          "[preset_load][phase12]") {
    // --- Author on processor A ----------------------------------------------
    ProcessorFixture author;
    REQUIRE(author.prepare(44100.0, 512) == Steinberg::kResultOk);
    sendEdit(*author.proc, 1, /*slot*/ 1, /*partial*/ 5, /*ratio*/ 3.21f, /*amp*/ 0.5f);
    sendEdit(*author.proc, 1, /*slot*/ 1, /*partial*/ 9, /*ratio*/ 7.77f, /*amp*/ 0.25f);
    REQUIRE(author.processBlock(512) == Steinberg::kResultOk);
    const SpectralState authored = author.proc->spectralSlotForTest(1);
    // The edit really diverged from every factory state, or this test is vacuous.
    bool matchesAFactoryState = false;
    for (std::size_t i = 0; i < Krate::DSP::kSpectralStateCount; ++i) {
        matchesAFactoryState =
            matchesAFactoryState
            || statesEqual(authored, Krate::DSP::makeFactoryState(
                                         static_cast<Krate::DSP::SpectralStateId>(i)));
    }
    REQUIRE_FALSE(matchesAFactoryState);

    const auto path = writeTempPreset(*author.proc, "authored_roundtrip.vstpreset");

    // --- Load on a FRESH pair through the browser's call ---------------------
    ConnectedPair pair;
    REQUIRE(pair.up());
    Krate::Plugins::PresetManager* manager = pair.ctrl.presetManagerForTest();
    const bool loaded = manager->loadPreset(infoFor(path));
    INFO("loadPreset error: " << manager->getLastError());
    REQUIRE(loaded);

    REQUIRE(pair.fx.processBlock(512) == Steinberg::kResultOk);
    REQUIRE(statesEqual(pair.fx.proc->spectralSlotForTest(1), authored));

    pair.down();
    std::filesystem::remove(path);
}

// =============================================================================
// 3. OVERRIDE BITS - restored by an authored preset, CLEARED by a factory one
// =============================================================================
TEST_CASE("Seraphis_PresetLoad_OverrideBitsRestoredThenClearedByFactoryPreset",
          "[preset_load][phase12]") {
    // --- Author overrides on processor A ------------------------------------
    ProcessorFixture author;
    REQUIRE(author.prepare(44100.0, 512) == Steinberg::kResultOk);
    sendEdit(*author.proc, 2, 0, /*partial*/ 7, /*pan*/ 0.7f, 0.0f);   // pan override
    sendEdit(*author.proc, 3, 0, /*partial*/ 9, /*mask on*/ 1.0f, 0.0f);
    REQUIRE(author.processBlock(512) == Steinberg::kResultOk);
    const auto path = writeTempPreset(*author.proc, "authored_overrides.vstpreset");

    // --- Fresh pair: load the authored preset --------------------------------
    ConnectedPair pair;
    REQUIRE(pair.up());
    Krate::Plugins::PresetManager* manager = pair.ctrl.presetManagerForTest();
    REQUIRE(manager->loadPreset(infoFor(path)));

    pair.fx.proc->setCloudFrameGateForTest(true);
    REQUIRE(pair.fx.processBlock(512) == Steinberg::kResultOk);
    {
        const auto& frame = pair.fx.proc->lastPublishedFrameForTest();
        REQUIRE((frame.overriddenBits & (std::uint64_t{1} << 7)) != 0);
        REQUIRE((frame.maskBits & (std::uint64_t{1} << 9)) != 0);
    }

    // --- Same pair: a FACTORY preset must CLEAR the stale overrides ----------
    // Its [partials] block is all-zero (FR-006a branch 1, asserted by the Phase
    // 12 harness) - and "restored" must mean the BITS too, not just the values:
    // kind 2 can only set a pan-override bit, so a load path built from the
    // per-partial kinds would leave bit 7 stuck forever.
    const auto files = SeraphisTest::allPresetFiles();
    REQUIRE_FALSE(files.empty());
    REQUIRE(manager->loadPreset(infoFor(files.front())));
    REQUIRE(pair.fx.processBlock(512) == Steinberg::kResultOk);
    {
        const auto& frame = pair.fx.proc->lastPublishedFrameForTest();
        REQUIRE(frame.overriddenBits == 0);
        REQUIRE(frame.maskBits == 0);
    }

    pair.down();
    std::filesystem::remove(path);
}

// =============================================================================
// 4. THE SAVE HALF - createComponentStateStream() is the processor's own bytes
// =============================================================================
TEST_CASE("Seraphis_PresetSave_StateStreamIsProcessorBytesWhenHostExposesComponent",
          "[preset_load][phase12]") {
    ConnectedPair pair;
    REQUIRE(pair.up());

    // Without IComponent on the handler, save has no source and must say so.
    REQUIRE(pair.ctrl.createComponentStateStream() == nullptr);

    // With it (the Innexus precedent), the stream is the processor's own bytes.
    pair.handler.componentTarget = pair.fx.proc.get();
    auto stream = Steinberg::owned(pair.ctrl.createComponentStateStream());
    REQUIRE(stream != nullptr);

    auto direct = Steinberg::owned(new Steinberg::MemoryStream());
    REQUIRE(pair.fx.proc->getState(direct) == Steinberg::kResultOk);

    REQUIRE(stream->getSize() == direct->getSize());
    REQUIRE(std::cmp_equal(stream->getSize(), SeraphisTest::kSeraphisStateBytes));
    REQUIRE(std::memcmp(stream->getData(), direct->getData(),
                        static_cast<std::size_t>(direct->getSize()))
            == 0);

    pair.down();
}
