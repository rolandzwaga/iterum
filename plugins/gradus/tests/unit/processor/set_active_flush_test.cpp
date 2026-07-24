// ==============================================================================
// Deactivation must not strand sounding notes (Gradus audit F4)
// ==============================================================================
// setActive() only did work for state == true (resetting arpCore_/midiDelay_);
// the state == false path fell straight through to AudioEffect::setActive with
// no note-off flush, and terminate()/the defaulted destructor do nothing either.
//
// The engine genuinely holds notes across blocks (pendingNoteOffs_ with per-note
// samplesRemaining), and Gradus emits its own arp and echo NoteOns to a MIDI
// *output* bus that the host cannot recall. Deactivating mid-note therefore
// stranded them downstream.
//
// VST3 does not call process() after setActive(false) returns, so the flush
// cannot emit MIDI from inside setActive(false) itself. Instead the obligation
// is registered on deactivate and discharged by the first process() after
// reactivation, which is the contract this test pins.
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
using namespace Gradus;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int32  kBlockSize  = 512;

struct Harness {
    std::unique_ptr<Gradus::Processor> proc;

    std::vector<float> outL{static_cast<size_t>(kBlockSize), 0.0f};
    std::vector<float> outR{static_cast<size_t>(kBlockSize), 0.0f};
    float* channels[2]{};
    AudioBusBuffers outputBus{};

    Krate::Test::EventList inEvents;
    Krate::Test::EventList outEvents;
    Krate::Test::ParameterChanges noParams;

    ProcessContext ctx{};
    ProcessData data{};

    std::array<int, 128> onCount{};
    std::array<int, 128> offCount{};

    Harness()
    {
        proc = std::make_unique<Gradus::Processor>();
        proc->initialize(nullptr);

        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc->setupProcessing(setup);

        outL.assign(static_cast<size_t>(kBlockSize), 0.0f);
        outR.assign(static_cast<size_t>(kBlockSize), 0.0f);
        channels[0] = outL.data();
        channels[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channels;

        ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid
                  | ProcessContext::kTimeSigValid;
        ctx.tempo = 120.0;
        ctx.timeSigNumerator = 4;
        ctx.timeSigDenominator = 4;
        ctx.sampleRate = kSampleRate;
        ctx.projectTimeSamples = 0;

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

    void noteOn(int16 pitch)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = 100.0f / 127.0f;
        e.noteOn.noteId = -1;
        inEvents.addEvent(e);
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
            if (e.type == Event::kNoteOnEvent)       ++onCount[e.noteOn.pitch];
            else if (e.type == Event::kNoteOffEvent) ++offCount[e.noteOff.pitch];
        }
        ctx.projectTimeSamples += kBlockSize;
    }

    void runBlocks(int n) { for (int i = 0; i < n; ++i) runBlock(); }

    /// Re-run setupProcessing with a different sample rate / block size, the way
    /// a host does when the audio device changes. Only legal while inactive.
    void reconfigure(double sampleRate, int32 maxBlockSize)
    {
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = sampleRate;
        setup.maxSamplesPerBlock = maxBlockSize;
        proc->setupProcessing(setup);
        ctx.sampleRate = sampleRate;
    }

    [[nodiscard]] int outstanding() const
    {
        int total = 0;
        for (int p = 0; p < 128; ++p) total += onCount[p] - offCount[p];
        return total;
    }
};

}  // namespace

TEST_CASE("setActive(false) does not strand sounding arp notes",
          "[processor][gradus][lifecycle][F4]")
{
    Harness h;
    h.proc->setActive(true);

    // Long note value so a note is still sounding when we deactivate: 1/2 note
    // at 120 BPM is ~58k samples, far longer than the run below.
    Krate::Test::ParameterChanges setupParams;
    setupParams.add(kArpTempoSyncId, 1.0);
    setupParams.add(kArpNoteValueId, 16.0 / 20.0);  // 1/2 note

    h.noteOn(60);
    h.runBlock(&setupParams);
    h.runBlocks(4);

    // A note must actually be sounding, otherwise the test proves nothing.
    const int soundingBeforeDeactivate = h.outstanding();
    INFO("outstanding note-ons at deactivate: " << soundingBeforeDeactivate);
    REQUIRE(soundingBeforeDeactivate > 0);

    // Host deactivates, then reactivates and resumes processing.
    h.proc->setActive(false);
    h.proc->setActive(true);
    h.runBlock();

    INFO("outstanding after reactivation + one block: " << h.outstanding());
    REQUIRE(h.outstanding() == 0);

    h.proc->setActive(false);
    h.proc->terminate();
}

TEST_CASE("GMF-001: setupProcessing between deactivate and reactivate does not strand arp notes",
          "[processor][gradus][lifecycle][flush]")
{
    // deactivate -> setupProcessing -> activate is the standard VST3 sample-rate
    // / block-size change sequence (setupProcessing is only legal while
    // inactive). setupProcessing calls arpCore_.prepare() -> reset(), which used
    // to wipe the panic obligation recorded by setActive(false) *and* the note
    // list it would have released, while pendingDeactivateFlush_ stayed true so
    // setActive(true) still skipped its own reset. midiDelay_ is untouched by
    // setupProcessing, so echo NoteOffs still fired -- the two flush paths were
    // asymmetric and the arp NoteOns were stranded downstream.
    Harness h;
    h.proc->setActive(true);

    Krate::Test::ParameterChanges setupParams;
    setupParams.add(kArpTempoSyncId, 1.0);
    setupParams.add(kArpNoteValueId, 16.0 / 20.0);  // 1/2 note

    h.noteOn(60);
    h.noteOn(64);
    h.noteOn(67);
    h.runBlock(&setupParams);
    h.runBlocks(4);

    const int soundingBeforeDeactivate = h.outstanding();
    INFO("outstanding note-ons at deactivate: " << soundingBeforeDeactivate);
    REQUIRE(soundingBeforeDeactivate > 0);

    h.proc->setActive(false);
    h.reconfigure(48000.0, 1024);
    h.proc->setActive(true);
    h.runBlock();

    for (int p = 0; p < 128; ++p) {
        INFO("pitch " << p << ": on=" << h.onCount[p] << " off=" << h.offCount[p]);
        REQUIRE(h.offCount[p] >= h.onCount[p]);
    }

    h.proc->setActive(false);
    h.proc->terminate();
}

TEST_CASE("setActive(false) does not strand pending MIDI-delay echoes",
          "[processor][gradus][lifecycle][midi-delay][F4]")
{
    Harness h;
    h.proc->setActive(true);

    // Echoes whose NoteOn has been emitted but whose NoteOff is still pending
    // are the other half of the obligation: midiDelay_.reset() used to discard
    // them silently.
    Krate::Test::ParameterChanges setupParams;
    setupParams.add(kArpTempoSyncId, 1.0);
    setupParams.add(kArpNoteValueId, 16.0 / 20.0);       // 1/2 note
    setupParams.add(kArpMidiDelayLaneLengthId, 0.0);      // length 1 -> step 0
    setupParams.add(kArpMidiDelayActiveStep0Id, 1.0);
    setupParams.add(kArpMidiDelayFeedbackStep0Id, 0.25);  // 4 echoes
    setupParams.add(kArpMidiDelayTimeModeStep0Id, 0.0);   // Free
    setupParams.add(kArpMidiDelayTimeStep0Id, 0.0955);    // ~200 ms

    h.noteOn(72);
    h.runBlock(&setupParams);
    // ~200 ms is ~17 blocks; run past the first couple of echoes so at least
    // one echo NoteOn has been emitted with its NoteOff still outstanding.
    h.runBlocks(40);

    const int soundingBeforeDeactivate = h.outstanding();
    INFO("outstanding note-ons at deactivate: " << soundingBeforeDeactivate);
    REQUIRE(soundingBeforeDeactivate > 0);

    h.proc->setActive(false);
    h.proc->setActive(true);
    h.runBlock();

    INFO("outstanding after reactivation + one block: " << h.outstanding());
    REQUIRE(h.outstanding() == 0);

    h.proc->setActive(false);
    h.proc->terminate();
}
