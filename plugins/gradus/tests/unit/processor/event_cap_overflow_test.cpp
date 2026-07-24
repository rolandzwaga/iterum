// ==============================================================================
// 128-event span cap must not produce phantom or stranded NoteOffs (GMF-006)
// ==============================================================================
// Gradus::Processor hands ArpeggiatorCore a fixed arpEvents_[128] span, so
// processBlock caps emission at 128 events. The NoteOn writes were guarded by
// `eventCount < maxEvents`, but the currentArpNotes_ tracking and the
// addPendingNoteOff scheduling that follow them were not:
//
//   * phantom off  -- a NoteOn dropped at the cap was still tracked as sounding
//                     and still scheduled a pending NoteOff, so a later block
//                     emitted a release for a note that never sounded (routed to
//                     the host AND to the mono audition voice).
//   * stranded note -- the chord/ratchet replace loops ARE cap-guarded but then
//                     zeroed currentArpNoteCount_ unconditionally. After a
//                     Tie/Slide step (which schedules no pending NoteOff) a
//                     truncated replace loop erased those notes from tracking
//                     with no output NoteOff and no pending one, leaving them
//                     unreleasable even by a panic flush.
//
// Driving the real Processor is required: a core-only test supplies its own span
// and cannot see the plugin's 128-event boundary.
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
// A large block gives one process() call enough samples for several arp steps,
// which is how eventCount crosses 128 mid-step. The chord size below is chosen
// so the cap lands INSIDE a chord: 20 notes emit 40 events per step, and 128 is
// not a multiple of 40, so the third step is truncated part-way. (A 32-note
// chord emits exactly 64 events per step and 128 lands cleanly on a step
// boundary, which never exercises the defect.)
constexpr int32 kBlockSize = 16384;

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
    // Per-pitch running balance in emission order. A NoteOff for a note that
    // never sounded drives it negative -- a per-pitch total would miss it,
    // because the same pitches are re-struck on every step.
    std::array<int, 128> balance{};
    int phantomOffs = 0;
    int maxEventsInOneBlock = 0;

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
        maxEventsInOneBlock = std::max(maxEventsInOneBlock, static_cast<int>(count));
        for (int32 i = 0; i < count; ++i) {
            Event e{};
            if (outEvents.getEvent(i, e) != kResultTrue) continue;
            if (e.type == Event::kNoteOnEvent) {
                const auto p = static_cast<size_t>(e.noteOn.pitch);
                ++onCount[p];
                ++balance[p];
            } else if (e.type == Event::kNoteOffEvent) {
                const auto p = static_cast<size_t>(e.noteOff.pitch);
                ++offCount[p];
                if (--balance[p] < 0) ++phantomOffs;
            }
        }
        ctx.projectTimeSamples += kBlockSize;
    }

    void runBlocks(int n) { for (int i = 0; i < n; ++i) runBlock(); }
};

// Chord mode over a 32-note held chord at a fast rate: each step emits 32
// NoteOffs + 32 NoteOns, so 128 events is crossed within two steps of a block.
void buildOverflowSetup(Krate::Test::ParameterChanges& p)
{
    using namespace Gradus;
    p.add(kArpSourceModeId, 0.0);        // Live
    p.add(kArpModeId, 9.0 / 11.0);       // Chord (ArpMode index 9 of 0-11)
    p.add(kArpTempoSyncId, 1.0);
    // 1/64 (dropdown index 1 of 30): ~1378 samples per step at 120 BPM, so a
    // 4096-sample block fires ~3 steps -- 3 x (32 NoteOff + 32 NoteOn) events,
    // well past the 128-entry arpEvents_ span.
    p.add(kArpNoteValueId, 1.0 / 29.0);
}

}  // namespace

TEST_CASE("GMF-006: 128-event cap never emits a phantom NoteOff",
          "[gradus][processor][overflow][stuck]")
{
    Driver d;
    Krate::Test::ParameterChanges setupParams;
    buildOverflowSetup(setupParams);

    for (int16 pitch = 40; pitch < 60; ++pitch)
        d.inEvents.addNoteOn(pitch, 0.8f);
    d.runBlock(&setupParams);
    d.runBlocks(40);

    // Guard against a vacuous pass: a block must actually saturate the span.
    INFO("most events emitted in a single block: " << d.maxEventsInOneBlock);
    REQUIRE(d.maxEventsInOneBlock >= 128);

    INFO("note-offs released for a note that never sounded: " << d.phantomOffs);
    CHECK(d.phantomOffs == 0);
}

TEST_CASE("GMF-006: 128-event cap never strands a tied note",
          "[gradus][processor][overflow][stuck]")
{
    using namespace Gradus;
    Driver d;
    Krate::Test::ParameterChanges setupParams;
    buildOverflowSetup(setupParams);

    // Modifier lane alternating Tie / normal: a Tie step schedules no pending
    // NoteOff, so its notes rely entirely on the next step's replace loop -- the
    // loop the cap can truncate.
    // Modifier flags are bits: kStepActive=0x01, kStepTie=0x02, stored 0-255.
    setupParams.add(kArpModifierLaneLengthId, 1.0 / 31.0);  // length 2
    setupParams.add(kArpModifierLaneStep0Id, 3.0 / 255.0);  // Active | Tie
    setupParams.add(kArpModifierLaneStep1Id, 1.0 / 255.0);  // Active

    for (int16 pitch = 40; pitch < 60; ++pitch)
        d.inEvents.addNoteOn(pitch, 0.8f);
    d.runBlock(&setupParams);
    d.runBlocks(40);

    // Release everything and let the engine flush, then require balance: a note
    // erased from tracking by a truncated replace loop can never be closed.
    for (int16 pitch = 40; pitch < 72; ++pitch)
        d.inEvents.addNoteOff(pitch);
    d.runBlock();
    d.runBlocks(20);

    INFO("most events emitted in a single block: " << d.maxEventsInOneBlock);
    REQUIRE(d.maxEventsInOneBlock >= 128);

    INFO("note-offs released for a note that never sounded: " << d.phantomOffs);
    CHECK(d.phantomOffs == 0);

    for (int p = 0; p < 128; ++p) {
        INFO("pitch " << p << ": on=" << d.onCount[static_cast<size_t>(p)]
                      << " off=" << d.offCount[static_cast<size_t>(p)]
                      << " outstanding=" << d.balance[static_cast<size_t>(p)]);
        CHECK(d.balance[static_cast<size_t>(p)] == 0);
    }
}
