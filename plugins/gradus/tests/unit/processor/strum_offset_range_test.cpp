// ==============================================================================
// Emitted event sample offsets must stay inside the block (GMF-007)
// ==============================================================================
// fireStep clamps humanizedSampleOffset to [0, blockSize-1], but then ADDS the
// per-note strum offset without re-clamping. strumTimeMs runs to 100 ms, which
// is 4800 samples at 48 kHz -- thousands of samples past the end of a 64-sample
// host block. The processor copies evt.sampleOffset verbatim into
// data.outputEvents and MidiNoteDelay only sorts, never clamps, so the
// out-of-range NoteOn reaches the host (which drops it, or rejects the whole
// list) while its gate NoteOff, scheduled in range, still fires.
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

// 48 kHz with a small block is the worst case: a 50 ms strum is 2400 samples
// against a 64-sample window.
constexpr double kSampleRate = 48000.0;
constexpr int32  kBlockSize  = 64;

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

    int totalEvents = 0;
    int outOfRange = 0;
    int32 worstOffset = 0;

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

    void runBlock(IParameterChanges* params = nullptr)
    {
        outEvents.clear();
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = params ? params : &noParams;
        proc->process(data);
        inEvents.clear();

        const int32 count = outEvents.getEventCount();
        for (int32 i = 0; i < count; ++i) {
            Event e{};
            if (outEvents.getEvent(i, e) != kResultTrue) continue;
            ++totalEvents;
            if (e.sampleOffset < 0 || e.sampleOffset >= kBlockSize) {
                ++outOfRange;
                worstOffset = std::max(worstOffset, e.sampleOffset);
            }
        }
        ctx.projectTimeSamples += kBlockSize;
    }

    void runBlocks(int n) { for (int i = 0; i < n; ++i) runBlock(); }
};

}  // namespace

TEST_CASE("GMF-007: strummed chord NoteOn offsets stay within [0, numSamples)",
          "[gradus][processor][strum][offset]")
{
    using namespace Gradus;
    Driver d;

    Krate::Test::ParameterChanges setupParams;
    setupParams.add(kArpSourceModeId, 0.0);        // Live
    setupParams.add(kArpModeId, 9.0 / 11.0);       // Chord
    setupParams.add(kArpTempoSyncId, 1.0);
    setupParams.add(kArpNoteValueId, 7.0 / 29.0);  // 1/16
    setupParams.add(kArpStrumTimeId, 0.5);         // 50 ms == 2400 samples

    d.inEvents.addNoteOn(60, 0.8f);
    d.inEvents.addNoteOn(64, 0.8f);
    d.inEvents.addNoteOn(67, 0.8f);
    d.runBlock(&setupParams);
    d.runBlocks(200);

    INFO("events captured: " << d.totalEvents);
    REQUIRE(d.totalEvents > 0);

    INFO("events outside [0, " << kBlockSize << "): " << d.outOfRange
         << "; largest offset seen: " << d.worstOffset);
    CHECK(d.outOfRange == 0);
}
