// ==============================================================================
// Velocity-0 NoteOn is a NoteOff (Gradus audit GMF-004)
// ==============================================================================
// MIDI 1.0 running status expresses a key release as a NoteOn with velocity 0,
// and VST3 hosts forward that verbatim on the legacy Event path. The Gradus
// input drain routed every kNoteOnEvent to arpCore_.noteOn(pitch, vel*127), so a
// vel-0 release incremented physicalKeysHeld_ and stored the pitch in
// heldNotes_. A source that sends the vel-0 NoteOn *as* the release never sends
// a kNoteOffEvent, so the pitch arped forever and the physical-key count stayed
// permanently over-counted (corrupting latch-Hold's "all keys released"
// detection and the empty-held short-circuit).
// ==============================================================================

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#include "vst_param_changes.h"
#include "vst_event_list.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int32  kBlockSize  = 512;

struct Driver {
    std::unique_ptr<Gradus::Processor> proc = std::make_unique<Gradus::Processor>();
    std::vector<float> outL = std::vector<float>(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR = std::vector<float>(static_cast<size_t>(kBlockSize), 0.0f);
    float* channels[2] = { outL.data(), outR.data() };
    AudioBusBuffers outputBus{};

    Krate::Test::EventList inEvents;
    Krate::Test::EventList outEvents;
    Krate::Test::ParameterChanges noParams;

    ProcessContext ctx{};
    ProcessData data{};

    std::array<int, 128> onCount{};
    std::array<int, 128> offCount{};

    Driver()
    {
        proc->initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc->setupProcessing(setup);
        proc->setActive(true);

        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channels;

        ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid;
        ctx.tempo = 120.0;
        ctx.sampleRate = kSampleRate;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlockSize;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &inEvents;
        data.outputEvents = &outEvents;
        data.processContext = &ctx;
    }

    ~Driver()
    {
        proc->setActive(false);
        proc->terminate();
    }

    /// Run one block. Returns the number of NoteOns emitted for `watchPitch`.
    int runBlock(IParameterChanges* params, int watchPitch)
    {
        outEvents.clear();
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = params ? params : &noParams;
        proc->process(data);
        inEvents.clear();

        int watched = 0;
        const int32 count = outEvents.getEventCount();
        for (int32 i = 0; i < count; ++i) {
            Event e{};
            if (outEvents.getEvent(i, e) != kResultTrue) continue;
            if (e.type == Event::kNoteOnEvent) {
                ++onCount[static_cast<size_t>(e.noteOn.pitch)];
                if (e.noteOn.pitch == watchPitch) ++watched;
            } else if (e.type == Event::kNoteOffEvent) {
                ++offCount[static_cast<size_t>(e.noteOff.pitch)];
            }
        }
        return watched;
    }
};

}  // namespace

TEST_CASE("GMF-004: velocity-0 NoteOn is treated as a NoteOff and does not strand a held note",
          "[gradus][processor][velocity]")
{
    using namespace Gradus;
    Driver d;

    // Live mode, tempo-synced 1/16 so the arp fires several times per second.
    Krate::Test::ParameterChanges setupParams;
    setupParams.add(kArpSourceModeId, 0.0);
    setupParams.add(kArpTempoSyncId, 1.0);
    setupParams.add(kArpNoteValueId, 7.0 / 29.0);

    d.inEvents.addNoteOn(60, 100.0f / 127.0f);
    d.runBlock(&setupParams, 60);

    int onsWhileHeld = 0;
    for (int b = 0; b < 30; ++b) onsWhileHeld += d.runBlock(nullptr, 60);
    INFO("pitch-60 note-ons while held: " << onsWhileHeld);
    REQUIRE(onsWhileHeld > 0);

    // The release: a raw velocity-0 NoteOn, with NO kNoteOffEvent following.
    d.inEvents.addNoteOn(60, 0.0f);
    d.runBlock(nullptr, 60);

    // Give the engine a few blocks to finish the step that was already in
    // flight, then require silence: nothing may retrigger pitch 60.
    for (int b = 0; b < 4; ++b) d.runBlock(nullptr, 60);

    int onsAfterRelease = 0;
    for (int b = 0; b < 40; ++b) onsAfterRelease += d.runBlock(nullptr, 60);
    INFO("pitch-60 note-ons after the velocity-0 release: " << onsAfterRelease);
    CHECK(onsAfterRelease == 0);

    for (int p = 0; p < 128; ++p) {
        INFO("pitch " << p << ": on=" << d.onCount[static_cast<size_t>(p)]
                      << " off=" << d.offCount[static_cast<size_t>(p)]);
        CHECK(d.offCount[static_cast<size_t>(p)] >= d.onCount[static_cast<size_t>(p)]);
    }
}
