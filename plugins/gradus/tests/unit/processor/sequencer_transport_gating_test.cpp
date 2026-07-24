// ==============================================================================
// Sequencer Transport-Gating Tests (spec 142, FR-031 at the Processor boundary)
// ==============================================================================
// Bug repro: in Source = Sequencer mode the plugin ignores the host transport.
// Gradus::Processor::process() hard-codes BlockContext::isPlaying = true on every
// block, so when the user presses Stop in the DAW the sequencer keeps clocking and
// keeps emitting note-ons. Spec 142 requires Sequencer playback to follow the host
// transport (spec.md US1: "When the host transport starts ... Then the plugin emits
// the programmed notes ... at the configured tempo").
//
// The free-running behavior is CORRECT for Live (held-note) mode — a hardware-style
// arp clocks off held notes without a transport — so the fix must gate isPlaying on
// transport ONLY in Sequencer mode. A companion test below pins that Live mode keeps
// emitting while the transport is stopped (no regression).
//
// Reference: specs/142-gradus-piano-roll-sequencer/spec.md (US1, FR-031)
// ==============================================================================

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "vst_param_changes.h"
#include "vst_event_list.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

using ParamChanges = Krate::Test::ParameterChanges;
using TestEventList = Krate::Test::EventList;

struct CapturedMidi {
    int64_t absoluteSample;
    bool    isNoteOn;
    int16_t pitch;
};

// Shared driver: owns the processor, the buffers, and the ProcessContext, and
// runs one block at a time, draining emitted MIDI events into `dest`.
struct ProcDriver {
    static constexpr double kSampleRate = 44100.0;
    static constexpr int32  kBlockSize  = 512;

    std::unique_ptr<Gradus::Processor> proc = std::make_unique<Gradus::Processor>();
    std::vector<float> outL = std::vector<float>(static_cast<size_t>(kBlockSize), 0.0f);
    std::vector<float> outR = std::vector<float>(static_cast<size_t>(kBlockSize), 0.0f);
    float* outChannels[2] = { outL.data(), outR.data() };
    AudioBusBuffers outputBus{};
    TestEventList inEvents;
    TestEventList outEvents;
    ProcessContext ctx{};
    ProcessData data{};
    int64_t blockStart = 0;

    ProcDriver() {
        proc->initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;
        proc->setupProcessing(setup);
        proc->setActive(true);

        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outChannels;

        ctx.tempo = 120.0;
        ctx.timeSigNumerator = 4;
        ctx.timeSigDenominator = 4;
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

    ~ProcDriver() {
        proc->setActive(false);
        proc->terminate();
    }

    // transportPlaying drives ProcessContext::kPlaying for this block.
    void runBlock(IParameterChanges* params, bool transportPlaying,
                  std::vector<CapturedMidi>& dest) {
        inEvents.clear();
        outEvents.clear();
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        ctx.state = ProcessContext::kTempoValid
                  | (transportPlaying ? ProcessContext::kPlaying : 0);
        ctx.projectTimeSamples = blockStart;

        data.inputParameterChanges = params;
        proc->process(data);

        int32 n = outEvents.getEventCount();
        for (int32 i = 0; i < n; ++i) {
            Event e{};
            if (outEvents.getEvent(i, e) != kResultTrue) continue;
            if (e.type == Event::kNoteOnEvent) {
                dest.push_back({blockStart + e.sampleOffset, true, e.noteOn.pitch});
            } else if (e.type == Event::kNoteOffEvent) {
                dest.push_back({blockStart + e.sampleOffset, false, e.noteOff.pitch});
            }
        }
        blockStart += kBlockSize;
    }
};

// Program a 4-step Sequencer pattern (60/62/64/65, all play) at a 1/16 rate so
// step boundaries fall every ~11 blocks @ 120 BPM — fast enough that the buggy
// always-playing path fires several note-ons across a multi-block stop window.
void buildSequencerSetup(ParamChanges& p) {
    using namespace Gradus;
    p.add(kArpSourceModeId, 1.0);                       // Sequencer
    p.add(kArpTempoSyncId, 1.0);                        // tempo-synced
    p.add(kArpNoteValueId, 7.0 / 29.0);                 // "1/16" (dropdown index 7)
    p.add(kArpSequencerNoteLaneLengthId, 3.0 / 31.0);   // length 4
    const int pitches[4] = {60, 62, 64, 65};
    for (int i = 0; i < 4; ++i) {
        p.add(static_cast<ParamID>(kArpSequencerNoteLaneStep0Id + i),
              pitches[i] / 127.0);
        p.add(static_cast<ParamID>(kArpSequencerNoteLaneRestStep0Id + i), 0.0); // play
    }
}

int countNoteOns(const std::vector<CapturedMidi>& v) {
    return static_cast<int>(std::count_if(
        v.begin(), v.end(), [](const CapturedMidi& c) { return c.isNoteOn; }));
}

}  // namespace

TEST_CASE("Sequencer follows host transport: Stop halts note emission (FR-031)",
          "[gradus][processor][sequencer][transport][spec142]")
{
    ProcDriver d;
    ParamChanges setupParams;
    buildSequencerSetup(setupParams);
    ParamChanges none;

    // --- Phase A: transport PLAYING. Configure + clock a few blocks. ---
    std::vector<CapturedMidi> whilePlaying;
    d.runBlock(&setupParams, /*transportPlaying=*/true, whilePlaying);   // block 0
    for (int b = 0; b < 24; ++b)                                          // ~12 steps
        d.runBlock(&none, /*transportPlaying=*/true, whilePlaying);

    // Control: the sequencer must actually play while the transport rolls.
    INFO("note-ons while playing: " << countNoteOns(whilePlaying));
    REQUIRE(countNoteOns(whilePlaying) >= 2);

    // --- Phase B: transport STOPPED. No NEW note-ons may be emitted. ---
    std::vector<CapturedMidi> whileStopped;
    for (int b = 0; b < 40; ++b)                                          // ~3.6 steps
        d.runBlock(&none, /*transportPlaying=*/false, whileStopped);

    INFO("note-ons after stop: " << countNoteOns(whileStopped));
    CHECK(countNoteOns(whileStopped) == 0);

    // No stuck notes: every note-on across the whole run gets a note-off.
    std::vector<CapturedMidi> all = whilePlaying;
    all.insert(all.end(), whileStopped.begin(), whileStopped.end());
    int ons = 0, offs = 0;
    for (const auto& c : all) (c.isNoteOn ? ons : offs)++;
    INFO("total ons=" << ons << " offs=" << offs);
    CHECK(offs >= ons);
}

TEST_CASE("GMF-002: Sequencer stop-then-play flushes in-flight echoes without stranding NoteOffs",
          "[gradus][processor][sequencer][transport][echo]")
{
    // The rising transport-play edge called midiDelay_.reset(), which zeroes
    // pendingCount_ with no NoteOff emission. midiDelay_.process() runs every
    // block regardless of transport (only arp step generation is gated), so
    // echo NoteOns keep reaching the host while stopped with their NoteOffs
    // still pending -- reset() then discarded those obligations. setActive(false)
    // deliberately uses flushWithNoteOffs() for exactly this reason; the
    // transport edge is the same situation (the host is still listening).
    ProcDriver d;
    using namespace Gradus;

    ParamChanges setupParams;
    buildSequencerSetup(setupParams);
    // MIDI-delay lane: length 1 (step 0 only), active, 8 echoes, ~200 ms Free.
    // Gate scaling stays at 100% (it compounds geometrically, and 200% would
    // give the 8th echo a ~40 s gate that no drain phase could outlast); the
    // default 80%-of-delay gate already leaves echoes sounding with their
    // NoteOff pending across the transport edge, which is what this pins.
    setupParams.add(kArpMidiDelayLaneLengthId, 0.0);
    setupParams.add(kArpMidiDelayActiveStep0Id, 1.0);
    setupParams.add(kArpMidiDelayFeedbackStep0Id, 0.5);       // 8 echoes
    setupParams.add(kArpMidiDelayTimeModeStep0Id, 0.0);       // Free
    setupParams.add(kArpMidiDelayTimeStep0Id, 0.0955);        // ~200 ms
    setupParams.add(kArpMidiDelayGateScaleStep0Id, 90.0 / 190.0);  // 100%
    setupParams.add(kArpMidiDelayVelDecayStep0Id, 0.0);       // no velocity decay
    ParamChanges none;

    std::vector<CapturedMidi> all;
    d.runBlock(&setupParams, /*transportPlaying=*/true, all);
    for (int b = 0; b < 60; ++b)
        d.runBlock(&none, /*transportPlaying=*/true, all);

    // Guard against a vacuous pass: echoes must actually be in flight. The
    // sequencer emits one NoteOn per step; with 8 echoes per step the capture
    // holds far more NoteOns than steps if echoes are really being produced.
    const int onsBeforeStop = countNoteOns(all);
    INFO("note-ons before stop: " << onsBeforeStop);
    REQUIRE(onsBeforeStop > 8);

    // Stop for one block, then resume: the rising edge hits the reset.
    d.runBlock(&none, /*transportPlaying=*/false, all);
    d.runBlock(&none, /*transportPlaying=*/true, all);
    for (int b = 0; b < 60; ++b)
        d.runBlock(&none, /*transportPlaying=*/true, all);

    // Drain: stop the transport (no new steps) and run long enough for the whole
    // echo tail to play out -- 8 echoes x ~200 ms is ~1.6 s ~= 140 blocks. At
    // quiescence every NoteOn ever emitted must have been closed, so a leftover
    // imbalance is a genuinely stranded note rather than one still sounding.
    for (int b = 0; b < 260; ++b)
        d.runBlock(&none, /*transportPlaying=*/false, all);

    std::array<int, 128> ons{};
    std::array<int, 128> offs{};
    for (const auto& c : all) {
        if (c.pitch < 0 || c.pitch > 127) continue;
        (c.isNoteOn ? ons : offs)[static_cast<size_t>(c.pitch)]++;
    }
    for (int p = 0; p < 128; ++p) {
        INFO("pitch " << p << ": on=" << ons[static_cast<size_t>(p)]
                      << " off=" << offs[static_cast<size_t>(p)]);
        REQUIRE(offs[static_cast<size_t>(p)] >= ons[static_cast<size_t>(p)]);
    }
}

TEST_CASE("Live mode free-runs while transport is stopped (no regression)",
          "[gradus][processor][live][transport][spec142]")
{
    // Live (held-note) mode is a classic arp: it must keep clocking off held
    // notes even when the transport is stopped. The transport-gating fix must
    // NOT touch this path.
    ProcDriver d;
    using namespace Gradus;

    // Source = Live (default 0), tempo-synced, 1/16 rate.
    ParamChanges setupParams;
    setupParams.add(kArpSourceModeId, 0.0);
    setupParams.add(kArpTempoSyncId, 1.0);
    setupParams.add(kArpNoteValueId, 7.0 / 29.0);

    std::vector<CapturedMidi> captured;
    d.runBlock(&setupParams, /*transportPlaying=*/false, captured);

    // Hold a note so the Live arp has something to clock. runBlock() clears
    // inEvents at the top of the block, so stage the note-on first, then drive a
    // block with no param changes — the arp latches the held note internally and
    // keeps clocking it on subsequent (empty) blocks.
    d.inEvents.addNoteOn(60, 1.0f);
    d.proc->process(d.data);
    d.blockStart += ProcDriver::kBlockSize;

    ParamChanges none;
    for (int b = 0; b < 40; ++b)
        d.runBlock(&none, /*transportPlaying=*/false, captured);

    INFO("Live note-ons while stopped: " << countNoteOns(captured));
    CHECK(countNoteOns(captured) >= 2);
}
