// ==============================================================================
// Ruinae Factory Preset Audibility Test
// ==============================================================================
// Every factory preset must make a sound when a note is played. A preset that
// renders silence is indistinguishable from a broken plugin to the user, and the
// existing factory-preset coverage (byte-identical MIDI, state round-trip) only
// looks at arp presets and at state bytes -- never at the audio.
//
// Each preset is loaded into a real processor, sent a single held note, and
// rendered long enough to clear the slowest factory amp-envelope attack. The
// rendered peak must be audible.
// ==============================================================================

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int32 kBlockSize = 512;

// The slowest factory amp attack is 2000 ms and some pads layer a slow filter
// sweep on top, so render well past onset before judging a preset silent.
constexpr double kRenderSeconds = 6.0;

// -60 dBFS. Comfortably above denormal/noise-floor dither, far below any level
// a preset could use intentionally.
constexpr float kAudibleThreshold = 0.001f;

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

struct PresetLevels {
    float peak{0.0f};          ///< Peak absolute sample over the whole render.
    float sustainedRms{0.0f};  ///< RMS once the slowest attacks have opened.
    bool loaded{false};
};

/// @brief Load a preset, hold one note, and measure its output levels.
PresetLevels renderPresetLevels(const std::filesystem::path& presetPath) {
    auto proc = std::make_unique<Ruinae::Processor>();
    proc->initialize(nullptr);

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.sampleRate = kSampleRate;
    setup.maxSamplesPerBlock = kBlockSize;
    proc->setupProcessing(setup);

    PresetLevels levels{};

    // Ruinae defers preset application through RTTransferT; activate first.
    proc->setActive(true);
    if (!loadPresetFileIntoProcessor(presetPath, *proc)) return levels;
    levels.loaded = true;

    // Drain the deferred preset snapshot.
    {
        ProcessData drainData{};
        drainData.numSamples = 0;
        proc->process(drainData);
    }

    std::vector<float> outL(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR(static_cast<size_t>(kBlockSize), 0.0f);
    float* outChannels[2] = { outL.data(), outR.data() };
    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannels;

    TestEventList inEvents;
    TestEventList outEvents;
    EmptyParamChanges emptyParams;

    ProcessContext processContext{};
    processContext.state = ProcessContext::kPlaying
                         | ProcessContext::kTempoValid
                         | ProcessContext::kProjectTimeMusicValid;
    processContext.sampleRate = kSampleRate;
    processContext.tempo = 120.0;

    // C4, moderately hard, so velocity-sensitive presets open up.
    inEvents.addNoteOn(60, 0.8f, 0);

    const int numBlocks =
        static_cast<int>((kRenderSeconds * kSampleRate) / kBlockSize);

    // Measure sustained level only after the slowest factory attack (2000 ms)
    // plus filter-sweep settling, so a slow swell is not mistaken for a quiet
    // preset.
    const int sustainStartBlock =
        static_cast<int>((3.5 * kSampleRate) / kBlockSize);

    double sumSquares = 0.0;
    size_t sustainSamples = 0;

    float peak = 0.0f;
    for (int block = 0; block < numBlocks; ++block) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numInputs = 0;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputEvents = &inEvents;
        data.outputEvents = &outEvents;
        data.inputParameterChanges = &emptyParams;
        data.processContext = &processContext;

        proc->process(data);

        for (int32 i = 0; i < kBlockSize; ++i) {
            const float l = outL[static_cast<size_t>(i)];
            const float r = outR[static_cast<size_t>(i)];
            peak = std::max(peak, std::abs(l));
            peak = std::max(peak, std::abs(r));

            if (block >= sustainStartBlock) {
                sumSquares += static_cast<double>(l) * l
                            + static_cast<double>(r) * r;
                sustainSamples += 2;
            }
        }

        // The note-on is only delivered in the first block; hold it after that.
        inEvents.clear();
        outEvents.clear();

        processContext.projectTimeSamples += kBlockSize;
        processContext.projectTimeMusic +=
            static_cast<double>(kBlockSize) / kSampleRate
            * (processContext.tempo / 60.0);
    }

    proc->setActive(false);
    proc->terminate();

    levels.peak = peak;
    levels.sustainedRms = (sustainSamples > 0)
        ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(sustainSamples)))
        : 0.0f;
    return levels;
}

float toDbfs(float linear) {
    return (linear > 0.0f) ? 20.0f * std::log10(linear) : -200.0f;
}

std::vector<std::filesystem::path> enumerateAllPresets() {
    const std::filesystem::path presetRoot{
        std::filesystem::path(__FILE__).parent_path().parent_path()
            .parent_path() / "resources" / "presets"};
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(presetRoot)) return result;

    for (const auto& subdir : std::filesystem::directory_iterator(presetRoot)) {
        if (!subdir.is_directory()) continue;
        for (const auto& file :
             std::filesystem::directory_iterator(subdir.path()))
        {
            if (!file.is_regular_file()) continue;
            if (file.path().extension() == ".vstpreset") {
                result.push_back(file.path());
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace

TEST_CASE("Every Ruinae factory preset produces audible output for a held note",
          "[processor][presets][audio][regression]")
{
    auto presets = enumerateAllPresets();
    REQUIRE_FALSE(presets.empty());

    for (const auto& p : presets) {
        DYNAMIC_SECTION("preset " << p.stem().string()) {
            const auto levels = renderPresetLevels(p);
            INFO("preset: " << p.stem().string());
            INFO("peak: " << levels.peak << " (" << toDbfs(levels.peak) << " dBFS)");
            REQUIRE(levels.loaded);
            REQUIRE(levels.peak > kAudibleThreshold);
        }
    }
}

TEST_CASE("No Ruinae factory preset is drastically quieter than the bank",
          "[processor][presets][audio][loudness][long]")
{
    // Passing the audibility test is not enough: a preset that renders far below
    // its neighbours reads as broken when a user auditions the bank, which is
    // exactly how the Xenoform pad was reported.
    //
    // This compares peak, not sustained RMS. Sustained RMS punishes presets that
    // are percussive by design -- a bell or a ping has decayed to near nothing by
    // the time it is measured, which says nothing about whether it is too quiet.
    // Peak level is the property that actually distinguishes a broken-sounding
    // preset from a short one.
    auto presets = enumerateAllPresets();
    REQUIRE_FALSE(presets.empty());

    struct Measured {
        std::string name;
        float peak;
    };
    std::vector<Measured> measured;
    measured.reserve(presets.size());

    for (const auto& p : presets) {
        const auto levels = renderPresetLevels(p);
        REQUIRE(levels.loaded);
        measured.push_back({p.stem().string(), levels.peak});
    }

    std::vector<float> sorted;
    sorted.reserve(measured.size());
    for (const auto& m : measured) sorted.push_back(m.peak);
    std::sort(sorted.begin(), sorted.end());
    const float medianPeak = sorted[sorted.size() / 2];
    const float medianDb = toDbfs(medianPeak);

    // Factory presets legitimately span a wide dynamic range -- an airy texture
    // is not a bass patch -- so this is a floor on outliers, not a normaliser.
    constexpr float kMaxBelowMedianDb = 20.0f;

    for (const auto& m : measured) {
        const float db = toDbfs(m.peak);
        INFO("preset: " << m.name);
        INFO("peak: " << db << " dBFS, bank median peak: " << medianDb << " dBFS");
        // CHECK, not REQUIRE: report every outlier in one run rather than
        // aborting on the first.
        CHECK(db > medianDb - kMaxBelowMedianDb);
    }
}

// =============================================================================
// Filter resonance is a Q value, not a normalized 0-1 fraction
// =============================================================================
// kFilterResonanceId denormalizes as 0.1 + value * 29.9, and the preset stream
// stores the resulting Q directly (filter_params.h: "0.1-30.0"). A preset
// author writing 0.3 for "30% resonance" therefore gets Q = 0.3 -- below
// Butterworth, so the filter has no resonant peak at all. Nothing caught this:
// the format compatibility test byte-compares the layout without ever checking
// what the numbers mean.

TEST_CASE("Ruinae factory presets use real Q values for filter resonance",
          "[processor][presets][resonance]")
{
    auto presets = enumerateAllPresets();
    REQUIRE_FALSE(presets.empty());

    // Butterworth. At or below this a filter is critically damped or
    // overdamped: no resonant peak, which is never a deliberate choice across
    // an entire factory bank.
    constexpr double kMinUsefulQ = 0.7;

    for (const auto& p : presets) {
        auto proc = std::make_unique<Ruinae::Processor>();
        proc->initialize(nullptr);

        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc->setupProcessing(setup);
        proc->setActive(true);
        REQUIRE(loadPresetFileIntoProcessor(p, *proc));

        {
            ProcessData drainData{};
            drainData.numSamples = 0;
            proc->process(drainData);
        }

        MemoryStream saved;
        REQUIRE(proc->getState(&saved) == kResultTrue);
        saved.seek(0, IBStream::kIBSeekSet, nullptr);

        Ruinae::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);
        REQUIRE(controller.setComponentState(&saved) == kResultTrue);

        const double normalized =
            controller.getParamNormalized(Ruinae::kFilterResonanceId);
        const double q = 0.1 + normalized * 29.9;

        INFO("preset: " << p.stem().string());
        INFO("resonance Q: " << q);
        CHECK(q >= kMinUsefulQ);

        proc->setActive(false);
        proc->terminate();
        controller.terminate();
    }
}

TEST_CASE("Ruinae filter resonance defaults to Butterworth",
          "[processor][presets][resonance][defaults]")
{
    // The default sat at Q 0.1 -- the minimum of the [0.1, 30] range -- so a
    // fresh instance, or any preset that simply omitted the field, had a filter
    // with no resonant peak whatsoever. GlobalFilterState already defaulted to
    // 0.707; this brings the per-voice filter in line.
    constexpr double kButterworthQ = 0.707;

    SECTION("processor default state") {
        auto proc = std::make_unique<Ruinae::Processor>();
        proc->initialize(nullptr);

        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc->setupProcessing(setup);

        MemoryStream saved;
        REQUIRE(proc->getState(&saved) == kResultTrue);
        saved.seek(0, IBStream::kIBSeekSet, nullptr);

        Ruinae::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);
        REQUIRE(controller.setComponentState(&saved) == kResultTrue);

        const double q = 0.1 + controller.getParamNormalized(Ruinae::kFilterResonanceId) * 29.9;
        INFO("default resonance Q: " << q);
        CHECK(q == Approx(kButterworthQ).margin(0.01));

        proc->terminate();
        controller.terminate();
    }

    SECTION("controller's registered parameter default") {
        // What a host restores on "reset to default"; it must agree with the
        // processor's own default rather than snapping back to the range floor.
        Ruinae::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        const double normalized =
            controller.getParamNormalized(Ruinae::kFilterResonanceId);
        const double q = 0.1 + normalized * 29.9;
        INFO("registered default Q: " << q);
        CHECK(q == Approx(kButterworthQ).margin(0.01));

        controller.terminate();
    }
}
