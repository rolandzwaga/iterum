// ==============================================================================
// The audition monitor must be polyphonic
// ==============================================================================
// AuditionVoice was a single voice and its noteOff() took no pitch: it released
// whatever was sounding. The processor called it on EVERY emitted NoteOff, so in
// Chord mode the first note-off of a chord silenced the whole monitor even
// though two more notes were still sounding at the MIDI output. The monitor and
// the host therefore disagreed about what was playing -- the same divergence
// class as GMF-011.
//
// Note-to-voice routing now lives in Krate::DSP::VoiceAllocator (Layer 3,
// allocation-free) and the processor renders a pool of 8 AuditionVoices, which
// accumulate into the same buffer.
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

    // Peak of the audio actually rendered in the most recent block.
    float lastBlockPeak = 0.0f;
    float runPeak = 0.0f;

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

        lastBlockPeak = 0.0f;
        for (int32 s = 0; s < kBlockSize; ++s) {
            lastBlockPeak = std::max(lastBlockPeak, std::fabs(outL[static_cast<size_t>(s)]));
            lastBlockPeak = std::max(lastBlockPeak, std::fabs(outR[static_cast<size_t>(s)]));
        }
        runPeak = std::max(runPeak, lastBlockPeak);
        ctx.projectTimeSamples += kBlockSize;
    }

    /// Run n blocks, returning the largest per-block peak seen.
    float runBlocks(int n)
    {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) {
            runBlock();
            peak = std::max(peak, lastBlockPeak);
        }
        return peak;
    }

    [[nodiscard]] bool reportsSilence() const { return outputBus.silenceFlags == 0x3; }
};

/// Live mode, audition on, slow rate and a long decay so a struck chord is still
/// sounding many blocks later.
void auditionSetup(Krate::Test::ParameterChanges& p)
{
    using namespace Gradus;
    p.add(kArpSourceModeId, 0.0);          // Live
    p.add(kArpTempoSyncId, 1.0);
    p.add(kArpNoteValueId, 13.0 / 29.0);   // 1/4
    p.add(kAuditionEnabledId, 1.0);
    p.add(kAuditionVolumeId, 1.0);
    p.add(kAuditionDecayId, 1.0);       // longest decay
}

}  // namespace

TEST_CASE("Audition: the monitor is never silent while the host has notes down",
          "[gradus][processor][audition][polyphony]")
{
    // Host/monitor parity -- the GMF-011 divergence class. Whenever the emitted
    // MIDI stream has notes outstanding, the monitor must be making sound.
    //
    // The monophonic monitor broke this wherever note-offs are STAGGERED:
    // AuditionVoice::noteOff() took no pitch and released whatever was sounding,
    // and the processor called it on every emitted NoteOff. Up mode with a gate
    // over 100% overlaps consecutive notes, so note N's off arrives while note
    // N+1 is still down -- the mono voice released there and the monitor went
    // quiet with a note still sounding at the MIDI output. (A Chord-mode chord
    // would NOT show this: its notes all end together, so releasing on the first
    // off is correct.)
    //
    // Measured against the emitted stream, not the wall clock: at a gate below
    // 100% the arp genuinely goes quiet between steps and the monitor is right
    // to follow it down.
    using namespace Gradus;
    Driver d;
    Krate::Test::ParameterChanges setupParams;
    auditionSetup(setupParams);
    setupParams.add(kArpModeId, 0.0);          // Up
    setupParams.add(kArpGateLengthId, 150.0 / 200.0);  // overlap consecutive notes

    // Track outstanding notes from the very first block. The setup block already
    // fires the pattern's first step, so skipping it would miss those note-ons
    // and drive the counter permanently negative.
    int outstanding = 0;
    int violations = 0;
    int blocksFullyHeld = 0;
    float loudest = 0.0f;

    auto tallyEmitted = [&d, &outstanding]() {
        const int32 count = d.outEvents.getEventCount();
        for (int32 i = 0; i < count; ++i) {
            Event e{};
            if (d.outEvents.getEvent(i, e) != kResultTrue) continue;
            if (e.type == Event::kNoteOnEvent) ++outstanding;
            else if (e.type == Event::kNoteOffEvent) --outstanding;
        }
    };

    d.inEvents.addNoteOn(60, 0.8f);
    d.inEvents.addNoteOn(64, 0.8f);
    d.inEvents.addNoteOn(67, 0.8f);
    d.runBlock(&setupParams);
    tallyEmitted();

    for (int b = 0; b < 320; ++b) {
        const int before = outstanding;
        d.runBlock();
        loudest = std::max(loudest, d.lastBlockPeak);
        tallyEmitted();

        // Only judge blocks where notes were down for the WHOLE block: a block
        // that starts or ends a note legitimately contains silence.
        if (before > 0 && outstanding > 0) {
            ++blocksFullyHeld;
            if (d.lastBlockPeak <= 0.0f) {
                ++violations;
                INFO("block " << b << " had " << before
                     << " notes down throughout but rendered silence");
            }
        }
    }

    INFO("loudest block peak over the run: " << loudest);
    REQUIRE(loudest > 0.01f);
    INFO("blocks with notes down throughout: " << blocksFullyHeld
         << ", of which silent: " << violations);
    REQUIRE(blocksFullyHeld > 20);
    CHECK(violations == 0);
}

TEST_CASE("Audition: a held chord drives more than one voice",
          "[gradus][processor][audition][polyphony]")
{
    // A monophonic monitor plays only the last note of a chord, so its output
    // stays a single sine. Level-based comparisons cannot show the difference --
    // any normalization deliberately holds level roughly constant -- so measure
    // WAVEFORM SHAPE instead, via crest factor (peak / RMS). A pure sine sits at
    // sqrt(2) ~= 1.414 whatever its amplitude; summing several detuned sines
    // raises the crest factor well above that. This distinguishes "three voices
    // sounding" from "one voice at some level" independently of the gain law.
    using namespace Gradus;

    auto crestFactorOver = [](int numNotes) {
        Driver d;
        Krate::Test::ParameterChanges setupParams;
        auditionSetup(setupParams);
        setupParams.add(kArpModeId, 9.0 / 11.0);  // Chord

        const int16 pitches[3] = {60, 64, 67};
        for (int i = 0; i < numNotes; ++i)
            d.inEvents.addNoteOn(pitches[i], 0.8f);
        d.runBlock(&setupParams);

        double sumSq = 0.0;
        double peak = 0.0;
        size_t n = 0;
        for (int b = 0; b < 60; ++b) {
            d.runBlock();
            for (int32 s = 0; s < kBlockSize; ++s) {
                const double v = d.outL[static_cast<size_t>(s)];
                sumSq += v * v;
                peak = std::max(peak, std::fabs(v));
                ++n;
            }
        }
        const double rms = std::sqrt(sumSq / static_cast<double>(n));
        return (rms > 0.0) ? peak / rms : 0.0;
    };

    const double crest1 = crestFactorOver(1);
    const double crest3 = crestFactorOver(3);
    INFO("crest factor with 1 note: " << crest1
         << ", with a 3-note chord: " << crest3);
    REQUIRE(crest1 > 0.0);
    // A single sine is ~1.414 (the sustained portion dominates the window).
    CHECK(crest1 < 1.8);
    // Three simultaneous pitches beat against each other, pushing the peak well
    // above the RMS.
    CHECK(crest3 > crest1 * 1.2);
}

TEST_CASE("Audition: a full chord does not clip the output",
          "[gradus][processor][audition][polyphony]")
{
    // Voices accumulate into one buffer, so without headroom scaling eight of
    // them at full volume would sum far past full scale.
    using namespace Gradus;
    Driver d;
    Krate::Test::ParameterChanges setupParams;
    auditionSetup(setupParams);
    setupParams.add(kArpModeId, 9.0 / 11.0);  // Chord

    for (int16 pitch = 48; pitch < 56; ++pitch)  // 8 notes = the whole pool
        d.inEvents.addNoteOn(pitch, 1.0f);
    d.runBlock(&setupParams);
    const float peak = d.runBlocks(120);

    INFO("peak with an 8-note chord at full velocity and volume: " << peak);
    REQUIRE(peak > 0.01f);
    CHECK(peak <= 1.0f);
}

TEST_CASE("Audition: a single note keeps its monophonic level",
          "[gradus][processor][audition][polyphony]")
{
    // The headroom scale is 1/sqrt(N) and must be exactly 1 for a single voice,
    // so adding polyphony cannot have made the common case quieter.
    using namespace Gradus;
    Driver d;
    Krate::Test::ParameterChanges setupParams;
    auditionSetup(setupParams);

    d.inEvents.addNoteOn(60, 1.0f);
    d.runBlock(&setupParams);
    const float peak = d.runBlocks(40);

    // Voice gain is velocity(1.0) * volume(1.0) * envelope, and the envelope
    // peaks at 1.0 at the end of the attack, so a lone sine should approach
    // full scale rather than sitting at some scaled-down fraction.
    INFO("peak for a single note at full velocity and volume: " << peak);
    CHECK(peak > 0.5f);
    CHECK(peak <= 1.0f);
}

TEST_CASE("Audition: voices are returned to the pool after release",
          "[gradus][processor][audition][polyphony]")
{
    // If finished voices were never handed back with voiceFinished(), the
    // allocator would run out of slots and later notes would go unheard.
    using namespace Gradus;
    Driver d;
    Krate::Test::ParameterChanges setupParams;
    auditionSetup(setupParams);
    // Short decay so each note frees its slot quickly.
    setupParams.add(kAuditionDecayId, 0.0);
    d.runBlock(&setupParams);

    // Cycle far more notes through the pool than it has slots.
    for (int i = 0; i < 40; ++i) {
        const auto pitch = static_cast<int16>(48 + (i % 12));
        d.inEvents.addNoteOn(pitch, 0.9f);
        d.runBlocks(3);
        d.inEvents.addNoteOff(pitch);
        d.runBlocks(6);
    }

    // After all that churn a fresh note must still be audible -- proof the pool
    // did not silently fill up.
    d.inEvents.addNoteOn(72, 1.0f);
    const float peak = d.runBlocks(20);
    INFO("peak for a note struck after 40 note cycles: " << peak);
    CHECK(peak > 0.01f);

    // And once everything is released the monitor must go properly silent.
    d.inEvents.addNoteOff(72);
    d.runBlocks(200);
    INFO("silenceFlags after everything is released: " << d.outputBus.silenceFlags);
    CHECK(d.reportsSilence());
}
