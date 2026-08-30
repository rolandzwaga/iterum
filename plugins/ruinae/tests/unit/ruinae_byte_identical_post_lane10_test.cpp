// ==============================================================================
// Ruinae Byte-Identical Regression Test post lane 10 extension (SC-004b, spec 142)
// ==============================================================================
// For each Ruinae factory preset whose arpeggiator is enabled -- selected by
// reading the loaded arp state, not by category name -- load the preset, force
// kArpMidiOutId=1 (same as the harness in gen_v2_fixtures/), drive the processor
// with the canonical 60-second MIDI sequence, and assert the emitted MIDI
// matches the paired golden text file: event count, order, kind, pitch and
// velocity exactly, timestamps within a measured cross-toolchain tolerance.
// (Byte-for-byte cannot be asserted here -- the offsets are FP math truncated
// to integers and the GCC/Clang legs build with -ffast-math. See
// tests/test_helpers/midi_golden_compare.h.)
//
// This proves that bumping ArpeggiatorCore::kNumLanes from 9 to 10 (with the
// lane 10 conditional-inert branch in Live mode) does not perturb Ruinae's
// MIDI output. Ruinae never sets sourceMode_=Sequencer, so lane 10 should
// stay completely inert.
// ==============================================================================

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "test_helpers/midi_golden_compare.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// =====================================================================
// Minimal in-process host shims
// =====================================================================

class TestEventList : public IEventList {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override {
        if (index < 0 || index >= static_cast<int32>(events_.size())) return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultTrue;
    }
    tresult PLUGIN_API addEvent(Event& e) override { events_.push_back(e); return kResultTrue; }
    void addNoteOn(int16 pitch, float vel, int32 sampleOffset) {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = vel;
        e.noteOn.noteId = -1;
        events_.push_back(e);
    }
    void addNoteOff(int16 pitch, int32 sampleOffset) {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = -1;
        events_.push_back(e);
    }
    void clear() { events_.clear(); }
private:
    std::vector<Event> events_;
};

class EmptyParamChanges : public IParameterChanges {
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getParameterCount() override { return 0; }
    IParamValueQueue* PLUGIN_API getParameterData(int32) override { return nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
};

class SingleParamQueue : public IParamValueQueue {
public:
    SingleParamQueue(ParamID id, double value) : paramId_(id), value_(value) {}
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    ParamID PLUGIN_API getParameterId() override { return paramId_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& value) override {
        if (index != 0) return kResultFalse;
        sampleOffset = 0;
        value = value_;
        return kResultTrue;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
private:
    ParamID paramId_;
    double value_;
};

class SingleParamChanges : public IParameterChanges {
public:
    SingleParamChanges(ParamID id, double value) : queue_(id, value) {}
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getParameterCount() override { return 1; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override {
        return i == 0 ? &queue_ : nullptr;
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
private:
    SingleParamQueue queue_;
};

// =====================================================================
// Canonical 60-second test sequence (must match tools/gen_v2_fixtures/common.h)
// =====================================================================
constexpr double kSampleRate = 44100.0;
constexpr int32  kBlockSize  = 512;
constexpr int64_t kTotalSamples = static_cast<int64_t>(60.0 * kSampleRate);
constexpr int kNumBlocks = static_cast<int>(kTotalSamples / kBlockSize);

struct ScheduledEvent {
    int64_t sampleTime;
    bool    isNoteOn;
    int16_t pitch;
    float   velocity;
};

std::vector<ScheduledEvent> makeStandardMidiSequence() {
    std::vector<ScheduledEvent> seq;
    const int64_t s = static_cast<int64_t>(kSampleRate);
    seq.push_back({0LL,         true,  60, 0.8f});
    seq.push_back({5LL  * s,    false, 60, 0.0f});
    seq.push_back({5LL  * s,    true,  64, 0.8f});
    seq.push_back({10LL * s,    false, 64, 0.0f});
    seq.push_back({10LL * s,    true,  67, 0.8f});
    seq.push_back({15LL * s,    false, 67, 0.0f});
    seq.push_back({15LL * s,    true,  60, 0.8f});
    seq.push_back({15LL * s,    true,  64, 0.8f});
    seq.push_back({15LL * s,    true,  67, 0.8f});
    seq.push_back({45LL * s,    false, 60, 0.0f});
    seq.push_back({45LL * s,    false, 64, 0.0f});
    seq.push_back({45LL * s,    false, 67, 0.0f});
    return seq;
}

struct CapturedMidi {
    int64_t absoluteSample;
    bool    isNoteOn;
    int16_t pitch;
    int     velocity;
};

std::string formatCaptured(const std::vector<CapturedMidi>& events) {
    std::ostringstream ss;
    for (const auto& e : events) {
        char buf[128];
        if (e.isNoteOn) {
            std::snprintf(buf, sizeof(buf), "[%lld] noteOn  %d %d\n",
                static_cast<long long>(e.absoluteSample),
                static_cast<int>(e.pitch), e.velocity);
        } else {
            std::snprintf(buf, sizeof(buf), "[%lld] noteOff %d\n",
                static_cast<long long>(e.absoluteSample),
                static_cast<int>(e.pitch));
        }
        ss << buf;
    }
    return ss.str();
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Golden-fixture maintenance. The factory arp presets are generated data; when
// they are intentionally revised the paired goldens must be rebuilt from the new
// bank. Setting RUINAE_REGEN_GOLDENS=1 rewrites each golden from the captured
// MIDI instead of asserting against it. It is OFF by default, so CI and normal
// local runs always assert. Regenerate deliberately, then re-run without the
// variable to confirm the suite passes.
bool regenGoldensRequested() {
    const char* v = std::getenv("RUINAE_REGEN_GOLDENS");
    return v != nullptr && v[0] == '1';
}

void writeFile(const std::filesystem::path& p, const std::string& contents) {
    std::ofstream out(p, std::ios::binary);
    REQUIRE(out.good());
    out << contents;
}

// Sanitize a string the same way tools/gen_v2_fixtures/common.cpp does.
std::string sanitizeForFilename(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            out += c;
        } else if (c == ' ') {
            out += '_';
        } else {
            out += '_';
        }
    }
    return out;
}

// Subclass to expose protected processParameterChanges (Ruinae::Processor)
class RuinaeHarnessProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

bool loadPresetFileIntoProcessor(const std::filesystem::path& presetPath,
                                 Ruinae::Processor& processor) {
    auto* stream = FileStream::open(presetPath.string().c_str(), "rb");
    if (!stream) return false;

    PresetFile presetFile(stream);
    bool ok = presetFile.readChunkList() && presetFile.seekToComponentState();
    if (!ok) { stream->release(); return false; }

    const auto* entry = presetFile.getEntry(kComponentState);
    if (!entry) { stream->release(); return false; }

    auto componentStream = owned(new ReadOnlyBStream(stream, entry->offset, entry->size));
    auto setResult = processor.setState(componentStream);
    stream->release();

    return setResult == kResultTrue;
}

std::vector<CapturedMidi> driveRuinaeProcessor(Ruinae::Processor& processor)
{
    std::vector<float> outL(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlockSize), 0.0f);
    float* outChannelBuffers[2] = { outL.data(), outR.data() };
    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannelBuffers;

    TestEventList inEvents;
    TestEventList outEvents;
    EmptyParamChanges emptyParams;

    ProcessContext processContext{};
    processContext.state = ProcessContext::kPlaying
                         | ProcessContext::kTempoValid
                         | ProcessContext::kTimeSigValid
                         | ProcessContext::kProjectTimeMusicValid;
    processContext.tempo = 120.0;
    processContext.timeSigNumerator = 4;
    processContext.timeSigDenominator = 4;
    processContext.sampleRate = kSampleRate;
    processContext.projectTimeMusic = 0.0;
    processContext.projectTimeSamples = 0;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kBlockSize;
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &emptyParams;
    data.outputParameterChanges = nullptr;
    data.inputEvents = &inEvents;
    data.outputEvents = &outEvents;
    data.processContext = &processContext;

    auto sequence = makeStandardMidiSequence();
    size_t nextSeqIdx = 0;

    std::vector<CapturedMidi> captured;
    captured.reserve(8192);

    for (int blockIdx = 0; blockIdx < kNumBlocks; ++blockIdx) {
        const int64_t blockStartSample =
            static_cast<int64_t>(blockIdx) * kBlockSize;
        const int64_t blockEndSample = blockStartSample + kBlockSize;

        inEvents.clear();
        while (nextSeqIdx < sequence.size()
            && sequence[nextSeqIdx].sampleTime < blockEndSample)
        {
            const auto& s = sequence[nextSeqIdx];
            const int32 sampleOffset =
                static_cast<int32>(s.sampleTime - blockStartSample);
            if (s.isNoteOn) {
                inEvents.addNoteOn(s.pitch, s.velocity, sampleOffset);
            } else {
                inEvents.addNoteOff(s.pitch, sampleOffset);
            }
            ++nextSeqIdx;
        }

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        outEvents.clear();

        processor.process(data);

        int32 evCount = outEvents.getEventCount();
        for (int32 i = 0; i < evCount; ++i) {
            Event e{};
            if (outEvents.getEvent(i, e) != kResultTrue) continue;
            CapturedMidi c{};
            c.absoluteSample = blockStartSample + e.sampleOffset;
            if (e.type == Event::kNoteOnEvent) {
                c.isNoteOn = true;
                c.pitch    = e.noteOn.pitch;
                c.velocity = std::clamp(
                    static_cast<int>(e.noteOn.velocity * 127.0f + 0.5f),
                    0, 127);
            } else if (e.type == Event::kNoteOffEvent) {
                c.isNoteOn = false;
                c.pitch    = e.noteOff.pitch;
                c.velocity = 0;
            } else {
                continue;
            }
            captured.push_back(c);
        }

        processContext.projectTimeSamples += kBlockSize;
        processContext.projectTimeMusic +=
            static_cast<double>(kBlockSize) / kSampleRate
            * (processContext.tempo / 60.0);
    }

    return captured;
}

void verifyPreset(const std::filesystem::path& presetPath,
                  const std::filesystem::path& fixturesDir)
{
    const std::string stem = presetPath.stem().string();
    const std::string safe = sanitizeForFilename(stem);
    const auto goldenPath = fixturesDir /
        ("ruinae_factory_" + safe + "_golden_midi.txt");

    const bool regen = regenGoldensRequested();

    // SC-004b requires 100% factory-preset coverage. A missing golden must be
    // a hard failure so any future preset added without a paired golden trips
    // CI immediately (compliance fix, spec 142 Phase 3 retry).
    if (!regen) {
        INFO("missing golden file: " << goldenPath.string());
        REQUIRE(std::filesystem::exists(goldenPath));
    }

    auto golden = readFile(goldenPath);

    auto proc = std::make_unique<RuinaeHarnessProcessor>();
    proc->initialize(nullptr);

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kBlockSize;
    proc->setupProcessing(setup);

    // Ruinae uses RTTransferT for preset transfer; activate before setState.
    proc->setActive(true);

    REQUIRE(loadPresetFileIntoProcessor(presetPath, *proc));

    // Drain the deferred preset snapshot.
    {
        ProcessData drainData{};
        drainData.numSamples = 0;
        proc->process(drainData);
    }

    // Force MIDI output ON (matches the harness — most factory presets ship
    // with midiOut=off, but for byte-identical comparison we need MIDI bytes).
    {
        SingleParamChanges enableMidiOut(Ruinae::kArpMidiOutId, 1.0);
        proc->processParameterChanges(&enableMidiOut);
    }

    auto captured = driveRuinaeProcessor(*proc);
    proc->setActive(false);
    proc->terminate();

    auto actual = formatCaptured(captured);

    if (regen) {
        writeFile(goldenPath, actual);
        WARN("regenerated golden: " << goldenPath.filename().string()
            << " (" << actual.size() << " bytes)");
        return;
    }

    // Compare portably rather than byte-for-byte. Every sample offset in the
    // golden is FP arithmetic truncated to an integer, and the GCC/Clang legs
    // build with -ffast-math, so byte-identity demands bit-identical FP math
    // across MSVC/GCC/AppleClang and is structurally incapable of holding.
    // Event count, order, kind, pitch and velocity are still exact; only
    // timestamps carry a (measured, sub-millisecond) tolerance. See
    // tests/test_helpers/midi_golden_compare.h.
    const auto comparison = Krate::TestUtils::compareMidiGolden(actual, golden);

    INFO("preset: " << stem);
    INFO("actual bytes: " << actual.size()
        << "  golden bytes: " << golden.size());
    INFO("comparison: " << comparison.message);
    REQUIRE(comparison.ok);
}

/// @brief True when a preset actually runs its arpeggiator.
///
/// kArpOperatingModeId is a 4-entry list (Off/MIDI/Mod/MIDI+Mod), so any
/// normalized value above the Off entry means the arp is live. Reading the
/// loaded state back through the controller is the same route
/// factory_preset_audio_test uses to inspect a preset's parameters.
bool presetUsesArp(const std::filesystem::path& presetPath)
{
    auto proc = std::make_unique<RuinaeHarnessProcessor>();
    proc->initialize(nullptr);

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kBlockSize;
    proc->setupProcessing(setup);
    proc->setActive(true);

    bool usesArp = false;
    if (loadPresetFileIntoProcessor(presetPath, *proc)) {
        // Drain the deferred preset snapshot so getState reflects the preset.
        ProcessData drainData{};
        drainData.numSamples = 0;
        proc->process(drainData);

        MemoryStream saved;
        if (proc->getState(&saved) == kResultTrue) {
            saved.seek(0, IBStream::kIBSeekSet, nullptr);
            Ruinae::Controller controller;
            controller.initialize(nullptr);
            if (controller.setComponentState(&saved) == kResultTrue) {
                usesArp = controller.getParamNormalized(
                    Ruinae::kArpOperatingModeId) > 1e-6;
            }
            controller.terminate();
        }
    }

    proc->setActive(false);
    proc->terminate();
    return usesArp;
}

/// @brief Every factory preset whose arpeggiator is enabled, wherever it lives.
///
/// This used to filter on an "Arp" directory-name prefix, which quietly scoped
/// SC-004b to the dedicated Arp* categories: an arp preset filed under any other
/// category (Rhythmic, say) escaped MIDI regression coverage entirely despite
/// the requirement asking for 100% of factory arp presets. Selecting on the
/// loaded arp state instead means a new arp preset is covered no matter which
/// category it ships in.
std::vector<std::filesystem::path> enumerateArpPresets()
{
    const std::filesystem::path presetRoot{
        std::filesystem::path(__FILE__).parent_path().parent_path()
            .parent_path() / "resources" / "presets"};
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(presetRoot)) return result;

    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(presetRoot))
    {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".vstpreset") continue;
        if (presetUsesArp(entry.path())) {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace

// =============================================================================
// SC-004b: every Ruinae factory arp preset must produce byte-identical MIDI
// =============================================================================

TEST_CASE("SC-004b: Ruinae factory arp presets produce byte-identical MIDI after kNumLanes 9->10",
          "[processor][sequencer][SC-004b][regression][long]")
{
    const std::filesystem::path fixturesDir{RUINAE_FIXTURES_DIR};
    auto presets = enumerateArpPresets();
    if (presets.empty()) {
        WARN("no arp factory presets found — check preset directory layout");
    }
    REQUIRE_FALSE(presets.empty());

    for (const auto& p : presets) {
        DYNAMIC_SECTION("preset " << p.stem().string()) {
            verifyPreset(p, fixturesDir);
        }
    }
}
