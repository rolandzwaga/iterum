// ==============================================================================
// Seraphis - Processor lifecycle tests
// ==============================================================================
// TEST_CASE("Seraphis_ProcessorLifecycle") starts here at T015 (fixture smoke)
// and is extended by T017 (latency invariance), T018 (degenerate shapes,
// silence flags, setActive) and T025 (zero allocation in process() and on
// setActive(true)).
//
// TAG RULE: this case carries [lifecycle-proc], NOT [lifecycle] -- the valgrind
// nightly invokes every binary as `"$BINDIR/$bin" '[lifecycle]'` and a 4 s
// render behind a 771 968 B engine must not be dragged into that job. Only
// Seraphis_EditorLifecycle (T024) carries [lifecycle].
// ==============================================================================

#include "plugin_ids.h"
#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstevents.h>

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>
#include <vst_event_list.h>

#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <memory>

namespace {

/// FR-033 / plan §1.3 C-1. The LITERAL expected value, deliberately NOT
/// `reverb_->getLatencySamples()`: an equality against the component's own
/// report is also satisfied by a configuration that switched spectral diffusion
/// off, which reports 0 (aether_reverb.h:2612). The number is what
/// `makeSeraphisReverbConfig`'s pinned `diffusionFftSize = 1024` buys, and it is
/// what this matrix defends.
constexpr Steinberg::uint32 kExpectedLatencySamples = 1024u;

/// The eight parameters Phase 8 registers (plugin_ids.h:57-69).
constexpr Steinberg::Vst::ParamID kAllPhase8Params[] = {
    Seraphis::kMasterGainId,   Seraphis::kPolyphonyId,    Seraphis::kSoftLimitId,
    Seraphis::kMacroDreamId,   Seraphis::kMacroBloomId,   Seraphis::kMacroDissolveId,
    Seraphis::kMacroGravityId, Seraphis::kMacroEntropyId,
};

/// The block size every T018 SECTION renders at. One constant so the seed loop,
/// the peak loop and the ProcessData all agree; a mismatch between them would
/// read past the fixture's seeded region and turn a real failure into a
/// guard-word trip instead of a silence failure.
constexpr Steinberg::int32 kTestBlockSamples = 512;

/// The canary the output buffers are pre-seeded with before every "produces
/// silence" / "leaves the buffer alone" assertion. MANDATORY (plan 4.2): the
/// fixture zeroes its own vectors, so without a non-zero pre-seed an
/// implementation that returns WITHOUT writing -- handing the host back the
/// previous plug-in's buffer content, which VST3 does not zero -- passes.
constexpr float kBufferSeed = 0.5f;

/// SC-026 clause 1: absolute peak after setActive(false) -> setActive(true).
constexpr float kSilenceEpsilon = 1.0e-6f;

/// A ProcessSetup filled the way a host fills it. A free function rather than a
/// brace-init at each call site: `setupProcessing()` takes a NON-const
/// reference, so every call needs a named lvalue anyway.
Steinberg::Vst::ProcessSetup makeSetup(double sampleRate, Steinberg::int32 blockSize) {
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    return setup;
}

/// The 36-byte state stream of format VERSION 1 (plan §3.4), which the Phase 9
/// v2 format keeps as a strict prefix (plan §5.1), carrying a NON-DEFAULT value
/// in every one of the eight fields. Written by hand rather than through
/// `getState()`: the claim under test is that the reported latency survives a
/// `setState()`, so the stream must not depend on the processor under test
/// having produced it. Labelled `kStateVersion1` so it is an HONEST version-1
/// stream and additionally exercises FR-093's v1 -> v2 migration. Leaves the
/// stream rewound, ready to hand to `setState()`.
void writeNonDefaultState(Steinberg::MemoryStream& stream) {
    {
        Steinberg::IBStreamer streamer(&stream, kLittleEndian);
        streamer.writeInt32(Seraphis::kStateVersion1);
        streamer.writeFloat(1.5f);   // masterGain -- default 1.0
        streamer.writeInt32(4);      // polyphony  -- default 8
        streamer.writeInt32(0);      // softLimit  -- default on
        streamer.writeFloat(0.75f);  // dream      -- default 0.0
        streamer.writeFloat(0.25f);  // bloom      -- default 0.0
        streamer.writeFloat(0.9f);   // dissolve   -- default 0.0
        streamer.writeFloat(0.1f);   // gravity    -- default 0.5
        streamer.writeFloat(0.6f);   // entropy    -- default 0.0
    }
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
}

/// Everything one degenerate-shape probe observes. Returned (rather than
/// REQUIREd inside the shared runner) so every shape asserts on its OWN source
/// lines: a runner that REQUIREs internally reports the same line number for all
/// five shapes and tells you nothing about which one broke.
struct ShapeProbe {
    Steinberg::tresult result = Steinberg::kResultFalse;
    bool canariesIntact = false;
    std::size_t activeVoices = 0;
    bool buffersUntouched = false;
    bool silenceFlagsUntouched = false;
};

// -----------------------------------------------------------------------------
// T025 / SC-007 -- the per-block work the no-allocation render is measured over.
//
// EVERY block does IDENTICAL work: two events (a note-on at offset 0 and the
// matching note-off at offset 384, so the slice loop really sub-divides) and
// eight three-point parameter queues. That uniformity is what makes the warm-up
// sound: the fixture's containers grow on demand and are then reused
// (seraphis_test_fixture.h:15-19), so if block 0 needed a container that block 1
// does not, a four-block warm-up would leave a growth step inside the measured
// scope and the case would report a HARNESS allocation as a processor defect.
// -----------------------------------------------------------------------------
constexpr std::size_t kAllocRenderBlocks = 200;
constexpr double kAllocSweepDenominator = 15.0;

void allocStressScript(std::size_t block, Krate::Test::EventList& events,
                       SeraphisTest::ParameterChanges& params) {
    // Note traffic walks a 12-semitone span so voices are genuinely allocated,
    // released and stolen rather than one slot being retriggered forever.
    const auto pitch = static_cast<Steinberg::int16>(48 + static_cast<int>(block % 12u));
    events.addNoteOn(pitch, 0.8f, 0);
    events.addNoteOff(pitch, 384);

    // Parameter sweep: all eight shipped IDs, multi-point, values moving every
    // block, so the polyphony/soft-limit "on change only" pushes (FR-024a
    // clauses 1-2) and the master-gain smoother retarget (clause 3) are all
    // inside the measured scope.
    const double phase = static_cast<double>(block % 16u) / kAllocSweepDenominator;
    const double inverse = 1.0 - phase;
    for (const Steinberg::Vst::ParamID id : kAllPhase8Params) {
        SeraphisTest::MultiPointParamValueQueue& queue = params.addQueue(id);
        queue.addTestPoint(0, phase);
        queue.addTestPoint(128, inverse);
        queue.addTestPoint(256, phase);
    }
}

}  // namespace

TEST_CASE("Seraphis_ProcessorLifecycle", "[seraphis][processor][lifecycle-proc]") {
    SeraphisTest::ProcessorFixture fixture;

    SECTION("fixture constructs and prepares") {
        REQUIRE(fixture.proc != nullptr);
        REQUIRE(fixture.prepare(48000.0, 512) == Steinberg::kResultOk);
    }

    // -------------------------------------------------------------------------
    // SC-013 clause 4, restated per plan §1.3 C-1/C-2 as an INVARIANCE matrix.
    //
    // Phase 8 has no code path that can change the reported latency: the reverb
    // reports `spectralEnabled_ ? diffusionFftSize_ : 0` (aether_reverb.h:2607-
    // 2613) and both are pinned by makeSeraphisReverbConfig, so the value is
    // 1024 from construction onwards. There is therefore nothing to announce --
    // and no route to announce it on, since a Steinberg::Vst::AudioEffect never
    // receives an IComponentHandler (that goes to the edit controller,
    // vsteditcontroller.h:59, 97, 108). What IS observable, and is asserted
    // here, is that no other value is ever reported.
    // -------------------------------------------------------------------------
    SECTION("Seraphis_LatencyIsReported") {
        // (1) Before ANY setupProcessing().
        REQUIRE(fixture.proc->initialize(nullptr) == Steinberg::kResultOk);
        REQUIRE(fixture.proc->getLatencySamples() == kExpectedLatencySamples);

        // (2) Four consecutive prepares, in ascending-rate order. A sample
        //     COUNT does not scale with the rate; running them in one sequence
        //     (rather than four fresh processors) is what catches an
        //     implementation that latched the first rate it saw.
        for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
            Steinberg::Vst::ProcessSetup setup = makeSetup(rate, 512);
            REQUIRE(fixture.proc->setupProcessing(setup) == Steinberg::kResultOk);
            REQUIRE(fixture.proc->getLatencySamples() == kExpectedLatencySamples);
        }

        // (3) Activation toggles.
        REQUIRE(fixture.proc->setActive(true) == Steinberg::kResultOk);
        REQUIRE(fixture.proc->getLatencySamples() == kExpectedLatencySamples);
        REQUIRE(fixture.proc->setActive(false) == Steinberg::kResultOk);
        REQUIRE(fixture.proc->getLatencySamples() == kExpectedLatencySamples);

        // (4) A setState carrying non-default values in all eight fields.
        Steinberg::MemoryStream stateStream;
        writeNonDefaultState(stateStream);
        REQUIRE(fixture.proc->setState(&stateStream) == Steinberg::kResultOk);
        REQUIRE(fixture.proc->getLatencySamples() == kExpectedLatencySamples);

        // (5) Every registered parameter, at both ends of its range. The pushes
        //     are delivered through process(), so re-activate first.
        REQUIRE(fixture.proc->setActive(true) == Steinberg::kResultOk);
        for (const Steinberg::Vst::ParamID id : kAllPhase8Params) {
            for (const double normalized : {0.0, 1.0}) {
                fixture.setParam(id, normalized);
                REQUIRE(fixture.processBlock(64) == Steinberg::kResultOk);
                REQUIRE(fixture.proc->getLatencySamples() == kExpectedLatencySamples);
            }
        }

        // (6) 100 process() calls.
        for (int block = 0; block < 100; ++block) {
            REQUIRE(fixture.processBlock(512) == Steinberg::kResultOk);
        }
        REQUIRE(fixture.proc->getLatencySamples() == kExpectedLatencySamples);

        // A processor that has JUST been initialized, with no call in between,
        // must survive setupProcessing() -- the adjacent case to the null guard
        // of plan §2.5.4 (whose out-of-order half T018 owns).
        auto fresh = std::make_unique<Seraphis::Processor>();
        REQUIRE_NOTHROW(fresh->initialize(nullptr));
        Steinberg::Vst::ProcessSetup freshSetup = makeSetup(48000.0, 512);
        REQUIRE_NOTHROW(fresh->setupProcessing(freshSetup));
        REQUIRE(fresh->getLatencySamples() == kExpectedLatencySamples);
    }

    // -------------------------------------------------------------------------
    // SC-021 -- degenerate process() shapes are safe (FR-030).
    //
    // Every probe below runs with a NOTE-ON QUEUED on the event list. That is
    // what gives the getActiveVoiceCount() assertion teeth: an implementation
    // that dispatched host events before its shape guards would allocate a voice
    // and the count would move. Without the queued event the assertion compares
    // 0 against 0 and cannot fail.
    //
    // The buffers are pre-seeded to kBufferSeed for the same reason the silence
    // clauses are: an early-out must leave the host's buffer ALONE, and a
    // zero-filled fixture buffer cannot distinguish "left alone" from "written".
    // -------------------------------------------------------------------------
    SECTION("Seraphis_DegenerateProcessShapes") {
        REQUIRE(fixture.prepare(48000.0, kTestBlockSamples) == Steinberg::kResultOk);

        // One warm render. It is what gives the fixture its block size and its
        // buffer capacity, both of which withOutputChannels() reuses below.
        REQUIRE(fixture.processBlock(kTestBlockSamples) == Steinberg::kResultOk);
        REQUIRE(fixture.checkCanaries());

        Krate::DSP::SeraphisEngine* engine = fixture.proc->engineForTest();
        REQUIRE(engine != nullptr);
        const std::size_t baselineVoices = engine->getActiveVoiceCount();
        const std::size_t sampleCount = static_cast<std::size_t>(kTestBlockSamples);

        auto probeShape = [&](int outputChannels, auto&& mutate) {
            fixture.events.clear();
            fixture.params.clear();
            fixture.pushEvent(
                static_cast<Steinberg::uint16>(Steinberg::Vst::Event::kNoteOnEvent), 60, 0.8f);
            fixture.seedOutputBuffers(kBufferSeed);

            Steinberg::Vst::ProcessData& data = fixture.withOutputChannels(outputChannels);
            data.numSamples = kTestBlockSamples;
            data.outputs[0].silenceFlags = 3;  // an early-out must not touch this
            mutate(data);

            ShapeProbe probe;
            probe.result = fixture.proc->process(data);
            probe.canariesIntact = fixture.checkCanaries();
            probe.activeVoices = engine->getActiveVoiceCount();
            probe.buffersUntouched = true;
            for (std::size_t i = 0; i < sampleCount; ++i) {
                if (fixture.audioL()[i] != kBufferSeed || fixture.audioR()[i] != kBufferSeed) {
                    probe.buffersUntouched = false;
                }
            }
            // Only readable where the bus itself is; when numOutputs == 0 or
            // outputs == nullptr, reading data.outputs[0] is exactly what SC-021
            // forbids the processor from doing, so the test must not do it either.
            probe.silenceFlagsUntouched =
                (data.numOutputs > 0 && data.outputs != nullptr)
                    ? (data.outputs[0].silenceFlags == 3u)
                    : true;

            fixture.events.clear();
            fixture.params.clear();
            return probe;
        };

        // (a) numOutputs == 0, with a valid outputs pointer.
        {
            const ShapeProbe p = probeShape(2, [](Steinberg::Vst::ProcessData& d) {
                d.numInputs = 0;
                d.inputs = nullptr;
                d.numOutputs = 0;
            });
            REQUIRE(p.result == Steinberg::kResultOk);
            REQUIRE(p.canariesIntact);
            REQUIRE(p.activeVoices == baselineVoices);
            REQUIRE(p.buffersUntouched);
        }

        // (b) outputs == nullptr with numOutputs still claiming a bus -- the
        //     other half of the same guard.
        {
            const ShapeProbe p = probeShape(2, [](Steinberg::Vst::ProcessData& d) {
                d.numOutputs = 1;
                d.outputs = nullptr;
            });
            REQUIRE(p.result == Steinberg::kResultOk);
            REQUIRE(p.canariesIntact);
            REQUIRE(p.activeVoices == baselineVoices);
            REQUIRE(p.buffersUntouched);
        }

        // (c) channelBuffers32 == nullptr.
        {
            const ShapeProbe p = probeShape(2, [](Steinberg::Vst::ProcessData& d) {
                d.outputs[0].channelBuffers32 = nullptr;
            });
            REQUIRE(p.result == Steinberg::kResultOk);
            REQUIRE(p.canariesIntact);
            REQUIRE(p.activeVoices == baselineVoices);
            REQUIRE(p.buffersUntouched);
            REQUIRE(p.silenceFlagsUntouched);
        }

        // (d) numSamples == 0. The buffer here is valid and writable, so
        //     "buffers untouched" is a real assertion, not a tautology.
        {
            const ShapeProbe p =
                probeShape(2, [](Steinberg::Vst::ProcessData& d) { d.numSamples = 0; });
            REQUIRE(p.result == Steinberg::kResultOk);
            REQUIRE(p.canariesIntact);
            REQUIRE(p.activeVoices == baselineVoices);
            REQUIRE(p.buffersUntouched);
            REQUIRE(p.silenceFlagsUntouched);
        }

        // (e) MONO-OUTPUT CLAUSE. withOutputChannels(1) builds a ONE-ELEMENT
        //     channelBuffers32 array, so an implementation that reads [1]
        //     performs a genuine out-of-bounds heap read -- a hard failure under
        //     the ASan lane (T033), and silent-but-wrong in Release. A host may
        //     ignore setBusArrangements' kResultFalse and present mono anyway,
        //     which is why the guard is in process() and not only in the
        //     arrangement negotiation. Model:
        //     plugins/ruinae/src/processor/processor.cpp:430 -- Membrum has NO
        //     numChannels check anywhere, so copying Membrum copies the gap.
        {
            const ShapeProbe p = probeShape(1, [](Steinberg::Vst::ProcessData&) {});
            REQUIRE(p.result == Steinberg::kResultOk);
            REQUIRE(p.canariesIntact);
            REQUIRE(p.activeVoices == baselineVoices);
            REQUIRE(p.buffersUntouched);
            REQUIRE(p.silenceFlagsUntouched);
        }

        // (f) numInputs == 0 is NOT degenerate for an instrument -- it is the
        //     shipped shape (FR-020: no addAudioInput()). It must render, not
        //     early-out, so it carries no queued note and no "untouched" claim.
        {
            fixture.events.clear();
            fixture.params.clear();
            Steinberg::Vst::ProcessData& data = fixture.withOutputChannels(2);
            data.numSamples = kTestBlockSamples;
            data.numInputs = 0;
            data.inputs = nullptr;
            REQUIRE(fixture.proc->process(data) == Steinberg::kResultOk);
            REQUIRE(fixture.checkCanaries());
            REQUIRE(engine->getActiveVoiceCount() == baselineVoices);
        }
    }

    // -------------------------------------------------------------------------
    // SC-021 -- process() BEFORE setupProcessing() produces silence, with teeth.
    //
    // The pre-seed is the whole point: VST3 does not guarantee zeroed output
    // buffers, so an implementation that returns without writing hands the host
    // back the previous plug-in's content. Against a fixture that zeroed its own
    // vectors that bug is invisible.
    // -------------------------------------------------------------------------
    SECTION("Seraphis_ProcessBeforePrepareIsSilent") {
        REQUIRE(fixture.proc->initialize(nullptr) == Steinberg::kResultOk);

        // Warm the fixture buffers. This call is itself a not-ready render.
        REQUIRE(fixture.processBlock(kTestBlockSamples) == Steinberg::kResultOk);
        fixture.seedOutputBuffers(kBufferSeed);

        Steinberg::Vst::ProcessData& data = fixture.withOutputChannels(2);
        data.numSamples = kTestBlockSamples;
        REQUIRE(fixture.proc->process(data) == Steinberg::kResultOk);

        bool allZero = true;
        for (std::size_t i = 0; i < static_cast<std::size_t>(kTestBlockSamples); ++i) {
            if (fixture.audioL()[i] != 0.0f || fixture.audioR()[i] != 0.0f) {
                allZero = false;
            }
        }
        REQUIRE(allZero);
        REQUIRE(fixture.checkCanaries());
    }

    // -------------------------------------------------------------------------
    // FR-024 silence-flag clause / SC-021 -- BOTH directions, each from the
    // opposite pre-seed, so neither can pass because the host happened to leave
    // the field alone.
    // -------------------------------------------------------------------------
    SECTION("Seraphis_SilenceFlagsAreWritten") {
        // Direction 1: a normal render must CLEAR the flags. Pre-seeded to 3.
        REQUIRE(fixture.prepare(48000.0, kTestBlockSamples) == Steinberg::kResultOk);
        REQUIRE(fixture.processBlock(kTestBlockSamples) == Steinberg::kResultOk);  // warm

        fixture.pushEvent(static_cast<Steinberg::uint16>(Steinberg::Vst::Event::kNoteOnEvent), 60,
                          0.8f);
        Steinberg::Vst::ProcessData& data = fixture.withOutputChannels(2);
        data.numSamples = kTestBlockSamples;
        data.outputs[0].silenceFlags = 3;
        REQUIRE(fixture.proc->process(data) == Steinberg::kResultOk);
        REQUIRE(data.outputs[0].silenceFlags == 0u);
        fixture.events.clear();

        // Direction 2: the not-ready path must ASSERT silence. Pre-seeded to 0.
        // A second fixture rather than a re-used one: "not ready" is a state the
        // first fixture has already left, and there is no way back to it.
        SeraphisTest::ProcessorFixture notReady;
        REQUIRE(notReady.processBlock(kTestBlockSamples) == Steinberg::kResultOk);  // warm
        Steinberg::Vst::ProcessData& notReadyData = notReady.withOutputChannels(2);
        notReadyData.numSamples = kTestBlockSamples;
        notReadyData.outputs[0].silenceFlags = 0;
        REQUIRE(notReady.proc->process(notReadyData) == Steinberg::kResultOk);
        REQUIRE(notReadyData.outputs[0].silenceFlags == 3u);
    }

    // -------------------------------------------------------------------------
    // SC-026 clause 1 (FR-032) -- setActive(false) leaves no tail.
    //
    // The peak is read out of the SAME buffers the ringing render wrote, so an
    // implementation whose post-reactivation process() writes nothing fails:
    // the tail is still sitting there.
    // -------------------------------------------------------------------------
    SECTION("Seraphis_SetActiveLeavesNoTail") {
        REQUIRE(fixture.prepare(48000.0, kTestBlockSamples) == Steinberg::kResultOk);

        // Hold a note long enough for the reverb to be ringing, then release it.
        fixture.pushEvent(static_cast<Steinberg::uint16>(Steinberg::Vst::Event::kNoteOnEvent), 60,
                          0.8f);
        for (int block = 0; block < 8; ++block) {
            REQUIRE(fixture.processBlock(kTestBlockSamples) == Steinberg::kResultOk);
        }
        fixture.pushEvent(static_cast<Steinberg::uint16>(Steinberg::Vst::Event::kNoteOffEvent), 60,
                          0.0f);
        REQUIRE(fixture.processBlock(kTestBlockSamples) == Steinberg::kResultOk);

        REQUIRE(fixture.proc->setActive(false) == Steinberg::kResultOk);
        REQUIRE(fixture.proc->setActive(true) == Steinberg::kResultOk);
        REQUIRE(fixture.processBlock(kTestBlockSamples) == Steinberg::kResultOk);

        float peak = 0.0f;
        for (std::size_t i = 0; i < static_cast<std::size_t>(kTestBlockSamples); ++i) {
            const float l = std::fabs(fixture.audioL()[i]);
            const float r = std::fabs(fixture.audioR()[i]);
            if (l > peak) {
                peak = l;
            }
            if (r > peak) {
                peak = r;
            }
        }
        REQUIRE(peak < kSilenceEpsilon);
        REQUIRE(fixture.checkCanaries());
    }

    // -------------------------------------------------------------------------
    // SC-021 out-of-order clause -- the surface pluginval strictness 5 probes.
    // setupProcessing() on a processor whose initialize() never ran must not
    // crash, must leave getLatencySamples() at 0 (there is no reverb_ to report
    // from), and a subsequent process() must still be silent rather than
    // undefined.
    // -------------------------------------------------------------------------
    SECTION("Seraphis_SetupProcessingWithoutInitialize") {
        auto orphan = std::make_unique<Seraphis::Processor>();
        Steinberg::Vst::ProcessSetup setup = makeSetup(48000.0, kTestBlockSamples);
        REQUIRE_NOTHROW(orphan->setupProcessing(setup));
        REQUIRE(orphan->getLatencySamples() == 0u);

        // Warm the fixture's buffers (its own processor is likewise uninitialized,
        // so this render takes the not-ready path too), then hand the orphan a
        // fully valid, pre-seeded ProcessData.
        REQUIRE(fixture.processBlock(kTestBlockSamples) == Steinberg::kResultOk);
        fixture.seedOutputBuffers(kBufferSeed);

        Steinberg::Vst::ProcessData& data = fixture.withOutputChannels(2);
        data.numSamples = kTestBlockSamples;
        REQUIRE(orphan->process(data) == Steinberg::kResultOk);

        bool allZero = true;
        for (std::size_t i = 0; i < static_cast<std::size_t>(kTestBlockSamples); ++i) {
            if (fixture.audioL()[i] != 0.0f || fixture.audioR()[i] != 0.0f) {
                allZero = false;
            }
        }
        REQUIRE(allZero);
        REQUIRE(fixture.checkCanaries());
    }

    // -------------------------------------------------------------------------
    // SC-007 (FR-029) -- process() allocates NOTHING.
    //
    // THE READING FORM BELOW IS NORMATIVE, and it is not the obvious one.
    // `AllocationScope::getAllocationCount()` returns the member `count_`
    // (allocation_detector.h:85-87), and `count_` is assigned ONLY in
    // `~AllocationScope()` (:81-83) -- so `REQUIRE(scope.getAllocationCount()
    // == 0)` written INSIDE the scope reads a value-initialized 0 and passes
    // unconditionally, whatever the processor did. (That is exactly the form of
    // the in-repo model plugins/membrum/tests/unit/test_allocation_matrix.cpp:
    // 129-135, which is why this file does not copy it.) The count is therefore
    // taken from the LIVE atomic on the detector singleton
    // (AllocationDetector::getAllocationCount(), :48-50) while tracking is still
    // on, stored in a local, and asserted after the scope has closed -- Catch2's
    // REQUIRE is itself an allocating expression and must never run inside.
    // -------------------------------------------------------------------------
    SECTION("Seraphis_ProcessorNoAllocInProcess") {
        REQUIRE(fixture.prepare(48000.0, kTestBlockSamples) == Steinberg::kResultOk);

        const std::size_t blockSamples = static_cast<std::size_t>(kTestBlockSamples);

        // Every fixture-side container is grown BEFORE the scope opens: the
        // capture vectors to their final size, and the event / parameter-queue
        // storage by rendering four blocks of the very script that is about to
        // be measured. Without this the case measures the harness warming up.
        fixture.reserveCapture(kAllocRenderBlocks * blockSamples);
        fixture.renderBlocks(4, blockSamples, allocStressScript);
        fixture.capturedL.clear();  // clear() keeps capacity; resize() would not
        fixture.capturedR.clear();

        std::size_t allocations = 0;
        {
            TestHelpers::AllocationScope scope;
            fixture.renderBlocks(kAllocRenderBlocks, blockSamples, allocStressScript);
            allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        REQUIRE(allocations == 0u);
        REQUIRE(fixture.checkCanaries());

        // LIVENESS PROBE -- a SEPARATE, never nested scope. Nesting would be
        // silently wrong in both directions: the inner ctor's startTracking()
        // RESETS the outer count to 0 (:31-34), and the inner dtor's
        // stopTracking() switches tracking off for the outer scope too (:37-40).
        // The probe can only pass because unit/test_main.cpp includes
        // <allocation_operator_overrides.h> (FR-066a) -- recordAllocation()
        // (:53-57) fires from those global replacements and nowhere else, so
        // without them the assertion above would be vacuous.
        std::size_t probe = 0;
        {
            TestHelpers::AllocationScope scope;
            // `volatile` is load-bearing, not decoration: [expr.new]/10 permits
            // a compiler to ELIDE an otherwise-unobserved new/delete pair even
            // when the global allocation functions are replaced, which would
            // make this probe read 0 for a perfectly live detector and turn the
            // guard into the very thing it exists to catch. Storing the pointer
            // into a volatile object is an observable side effect, so the
            // allocation must actually happen.
            int* volatile deliberate = new int(7);
            probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
            delete deliberate;
        }
        REQUIRE(probe >= 1u);
    }

    // -------------------------------------------------------------------------
    // SC-026 clause 2 (FR-032) -- setActive(true) performs EXACTLY 0
    // allocations, in the identical normative form.
    //
    // The measured call is a RE-activation: prepare() above has already done the
    // first setActive(true), so any once-only allocation has already happened
    // and cannot hide behind "it only allocates the first time". The preceding
    // setActive(false) is the branch that clears the engine and resets the
    // reverb; if that branch ever grew an allocation it would land in the next
    // scope's window, not this one, so the deactivate is deliberately outside.
    // -------------------------------------------------------------------------
    SECTION("Seraphis_SetActiveDoesNotAllocate") {
        REQUIRE(fixture.prepare(48000.0, kTestBlockSamples) == Steinberg::kResultOk);
        REQUIRE(fixture.processBlock(kTestBlockSamples) == Steinberg::kResultOk);
        REQUIRE(fixture.proc->setActive(false) == Steinberg::kResultOk);

        Steinberg::tresult activateResult = Steinberg::kResultFalse;
        std::size_t allocations = 0;
        {
            TestHelpers::AllocationScope scope;
            activateResult = fixture.proc->setActive(true);
            allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        REQUIRE(activateResult == Steinberg::kResultOk);
        REQUIRE(allocations == 0u);

        // Its own liveness probe -- an empty measured window is exactly the
        // shape a broken detector produces, so this SECTION must prove the
        // detector was live for ITS scope, not rely on the other SECTION's.
        std::size_t probe = 0;
        {
            TestHelpers::AllocationScope scope;
            // `volatile` is load-bearing, not decoration: [expr.new]/10 permits
            // a compiler to ELIDE an otherwise-unobserved new/delete pair even
            // when the global allocation functions are replaced, which would
            // make this probe read 0 for a perfectly live detector and turn the
            // guard into the very thing it exists to catch. Storing the pointer
            // into a volatile object is an observable side effect, so the
            // allocation must actually happen.
            int* volatile deliberate = new int(7);
            probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
            delete deliberate;
        }
        REQUIRE(probe >= 1u);
    }
}
