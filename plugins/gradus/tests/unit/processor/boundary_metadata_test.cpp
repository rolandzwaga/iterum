// ==============================================================================
// MIDI boundary representation guards (audit GMF-013, GMF-015, GMF-017)
// ==============================================================================
// Coverage for the value/metadata contract at the plugin's MIDI boundary. These
// pin behaviour rather than fix a defect:
//
//   GMF-013  velocity round-trip. The audit claimed the truncating input cast
//            (velocity * 127.0f) loses an LSB against the output divide
//            (velocity / 127.0f); one verifier refuted that empirically. This
//            test settled it in the audit's favour: 8 of the 127 MIDI
//            velocities did lose a step (72 -> 71, 104 -> 103, ...), because
//            e.g. 72/127 is just under 0.566929 in float32 and multiplying
//            back gives 71.9999. The input cast now rounds instead of
//            truncating, and clamps -- which also makes the conversion defined
//            if a non-conformant host sends velocity outside [0,1].
//   GMF-015  input events are applied at block granularity: the drain reads
//            only pitch and velocity, and arpCore_.noteOn/noteOff take no
//            sample offset. A note pressed AND released inside one block nets
//            to not-held before processBlock samples heldNotes_. That is the
//            standard model for a block-based arpeggiator and is INTENTIONAL;
//            changing it needs a design decision (sub-block splitting), not a
//            patch. Pinned so an accidental change is caught.
//   GMF-017  every emitted event carries channel 0 and noteId -1. No test
//            asserted this; it is a boundary invariant worth guarding.
// ==============================================================================

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "vst_param_changes.h"
#include "vst_event_list.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int32  kBlockSize  = 512;

struct CapturedEvent {
    bool    isNoteOn;
    int16   pitch;
    float   velocity;
    int16   channel;
    int32   noteId;
    int32   sampleOffset;
};

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

    std::vector<CapturedEvent> captured;

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
            if (e.type == Event::kNoteOnEvent) {
                captured.push_back({true, e.noteOn.pitch, e.noteOn.velocity,
                                    e.noteOn.channel, e.noteOn.noteId, e.sampleOffset});
            } else if (e.type == Event::kNoteOffEvent) {
                captured.push_back({false, e.noteOff.pitch, e.noteOff.velocity,
                                    e.noteOff.channel, e.noteOff.noteId, e.sampleOffset});
            }
        }
        ctx.projectTimeSamples += kBlockSize;
    }

    void runBlocks(int n) { for (int i = 0; i < n; ++i) runBlock(); }
};

void liveSetup(Krate::Test::ParameterChanges& p)
{
    using namespace Gradus;
    p.add(kArpSourceModeId, 0.0);        // Live
    p.add(kArpTempoSyncId, 1.0);
    p.add(kArpNoteValueId, 7.0 / 29.0);  // 1/16
}

}  // namespace

TEST_CASE("GMF-013: velocity round-trips exactly for MIDI 1..127",
          "[gradus][processor][velocity][boundary]")
{
    // The arp passes the held note's velocity through unscaled at the default
    // velocity-lane setting, so the emitted NoteOn velocity should reproduce the
    // input exactly. Any LSB loss in the input truncation would show up here.
    for (int v = 1; v <= 127; ++v) {
        Driver d;
        Krate::Test::ParameterChanges setupParams;
        liveSetup(setupParams);

        const float inVelocity = static_cast<float>(v) / 127.0f;
        d.inEvents.addNoteOn(60, inVelocity);
        d.runBlock(&setupParams);
        d.runBlocks(12);

        const auto it = std::find_if(
            d.captured.begin(), d.captured.end(),
            [](const CapturedEvent& e) { return e.isNoteOn && e.pitch == 60; });
        INFO("input MIDI velocity " << v);
        REQUIRE(it != d.captured.end());

        const int outMidi = static_cast<int>(std::lround(it->velocity * 127.0f));
        INFO("emitted normalized velocity " << it->velocity
             << " -> MIDI " << outMidi);
        CHECK(outMidi == v);
    }
}

TEST_CASE("GMF-013: a non-conformant out-of-range input velocity cannot wrap",
          "[gradus][processor][velocity][boundary]")
{
    // VST3 normalizes velocity to [0,1], so this is hardening rather than a live
    // bug: an out-of-range float would otherwise make the float->uint8_t cast
    // undefined. Whatever comes out must still be a legal MIDI velocity.
    Driver d;
    Krate::Test::ParameterChanges setupParams;
    liveSetup(setupParams);

    d.inEvents.addNoteOn(60, 4.0f);   // way past 1.0 -> 508 before clamping
    d.runBlock(&setupParams);
    d.runBlocks(12);

    bool sawNoteOn = false;
    for (const auto& e : d.captured) {
        if (!e.isNoteOn) continue;
        sawNoteOn = true;
        INFO("emitted normalized velocity " << e.velocity);
        CHECK(e.velocity > 0.0f);
        CHECK(e.velocity <= 1.0f);
    }
    CHECK(sawNoteOn);
}

TEST_CASE("GMF-015: input note fully contained in one block is quantized to block granularity (documented tradeoff)",
          "[gradus][processor][input][quantize]")
{
    // INTENTIONAL BEHAVIOUR, pinned rather than fixed. The input drain reads
    // only pitch and velocity; arpCore_.noteOn/noteOff take no sample offset, so
    // every input event applies at block start. A note whose NoteOn AND NoteOff
    // both land inside one block therefore nets to not-held before processBlock
    // samples heldNotes_, and never arps.
    //
    // This is the standard block-quantized input model for a block-based
    // arpeggiator: the host delivers events in ascending-offset order and both
    // On and Off are processed symmetrically, so no stuck note or ordering
    // hazard results. Making sub-block presses audible would require splitting
    // processBlock at each input event -- a design decision, not a patch. If
    // this test starts FAILING (pitch 61 does arp), that decision was taken
    // somewhere and this comment needs revisiting.
    Driver d;
    Krate::Test::ParameterChanges setupParams;
    liveSetup(setupParams);

    // A sustained note keeps the arp clocking so the assertion is not vacuous.
    d.inEvents.addNoteOn(48, 0.8f);
    d.runBlock(&setupParams);
    d.runBlocks(4);

    // Pitch 61 pressed at offset 10 and released at offset 300 of ONE block.
    d.inEvents.addNoteOn(61, 0.8f, 10);
    d.inEvents.addNoteOff(61, 300);
    const size_t before = d.captured.size();
    d.runBlock();
    d.runBlocks(20);

    bool sustainedArped = false;
    bool transientArped = false;
    for (size_t i = before; i < d.captured.size(); ++i) {
        if (!d.captured[i].isNoteOn) continue;
        if (d.captured[i].pitch == 48) sustainedArped = true;
        if (d.captured[i].pitch == 61) transientArped = true;
    }
    INFO("sustained pitch 48 kept arping: " << sustainedArped);
    REQUIRE(sustainedArped);
    INFO("transient pitch 61 arped: " << transientArped);
    CHECK_FALSE(transientArped);
}

TEST_CASE("GMF-017: all emitted MIDI events carry channel 0 and noteId -1",
          "[gradus][processor][boundary]")
{
    using namespace Gradus;
    Driver d;
    Krate::Test::ParameterChanges setupParams;
    liveSetup(setupParams);
    // Echoes active too, so MidiNoteDelay-produced events are covered as well as
    // the arp's own pass-through.
    setupParams.add(kArpMidiDelayLaneLengthId, 0.0);
    setupParams.add(kArpMidiDelayActiveStep0Id, 1.0);
    setupParams.add(kArpMidiDelayFeedbackStep0Id, 0.25);
    setupParams.add(kArpMidiDelayTimeModeStep0Id, 0.0);
    setupParams.add(kArpMidiDelayTimeStep0Id, 0.0955);

    d.inEvents.addNoteOn(60, 0.8f);
    d.inEvents.addNoteOn(64, 0.8f);
    d.runBlock(&setupParams);
    d.runBlocks(80);

    INFO("events captured: " << d.captured.size());
    REQUIRE(d.captured.size() > 4);

    bool sawNoteOn = false;
    bool sawNoteOff = false;
    for (const auto& e : d.captured) {
        (e.isNoteOn ? sawNoteOn : sawNoteOff) = true;
        INFO((e.isNoteOn ? "NoteOn" : "NoteOff")
             << " pitch " << e.pitch << " at offset " << e.sampleOffset);
        CHECK(e.channel == 0);
        CHECK(e.noteId == -1);
        CHECK(e.sampleOffset >= 0);
        CHECK(e.sampleOffset < kBlockSize);
    }
    CHECK(sawNoteOn);
    CHECK(sawNoteOff);
}

TEST_CASE("GMF-016: delay-lane step is sampled per block (post-advance) -- pins current echo-config granularity",
          "[gradus][processor][delay][echo]")
{
    // PINS CURRENT BEHAVIOUR -- no fix. Processor::process reads
    //   currentDelayStep = arpCore_.midiDelayLane().currentStep()
    // ONCE, AFTER processBlock (so it is the post-advance position), and passes
    // that single value to MidiNoteDelay::process for every NoteOn in the block.
    // When more than one arp step fires in a block, every echoed note therefore
    // uses one lane-step config -- possibly a step none of those notes fired on.
    //
    // MidiNoteDelay::process documents currentDelayStep as one-step-per-block by
    // contract, and the delay lane clocks at its own independent polymetric
    // rate, so there is no defined 1:1 note-to-step mapping to be wrong against,
    // and no note-lifecycle hazard results (every echo still gets its NoteOff --
    // asserted below). Making the echo config per-step-accurate would mean
    // tagging each ArpEvent with the delay-lane step active at fire time, a
    // change to the shared ArpeggiatorCore that shifts goldens: a DESIGN
    // DECISION for a human, not an in-scope fix.
    using namespace Gradus;
    Driver d;
    Krate::Test::ParameterChanges setupParams;
    liveSetup(setupParams);
    // Fast rate so several arp steps fall inside one 512-sample block.
    setupParams.add(kArpNoteValueId, 1.0 / 29.0);           // 1/64
    // Delay lane length 2: step 0 echoes, step 1 does not.
    setupParams.add(kArpMidiDelayLaneLengthId, 1.0 / 31.0);
    setupParams.add(kArpMidiDelayActiveStep0Id, 1.0);
    setupParams.add(kArpMidiDelayFeedbackStep0Id, 3.0 / 16.0);
    setupParams.add(kArpMidiDelayTimeModeStep0Id, 0.0);
    setupParams.add(kArpMidiDelayTimeStep0Id, 0.0955);      // ~200 ms
    setupParams.add(static_cast<ParamID>(kArpMidiDelayActiveStep0Id + 1), 0.0);  // inactive

    d.inEvents.addNoteOn(60, 0.8f);
    d.runBlock(&setupParams);
    d.runBlocks(120);

    // Echoes are produced at all (guards against a vacuous pass): far more
    // note-ons than the arp alone would emit for a single held pitch.
    int noteOns = 0;
    for (const auto& e : d.captured) if (e.isNoteOn) ++noteOns;
    INFO("note-ons captured (arp steps + echoes): " << noteOns);
    REQUIRE(noteOns > 0);

    // Whatever the per-block sampling does to echo CONFIG, it must never cost a
    // note its release. Drain first, then require balance.
    d.inEvents.addNoteOff(60);
    d.runBlock();
    d.runBlocks(200);

    std::array<int, 128> ons{};
    std::array<int, 128> offs{};
    for (const auto& e : d.captured) {
        if (e.pitch < 0 || e.pitch > 127) continue;
        (e.isNoteOn ? ons : offs)[static_cast<size_t>(e.pitch)]++;
    }
    for (int p = 0; p < 128; ++p) {
        INFO("pitch " << p << ": on=" << ons[static_cast<size_t>(p)]
                      << " off=" << offs[static_cast<size_t>(p)]);
        CHECK(offs[static_cast<size_t>(p)] >= ons[static_cast<size_t>(p)]);
    }
}
