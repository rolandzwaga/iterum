// ==============================================================================
// Seraphis - Processor bus setup tests (T016, FR-020 / FR-021 -> SC-011)
// ==============================================================================
// Seraphis is an instrument: 1 event input, 1 stereo audio output, and NO audio
// input bus at all. The three rejections in setBusArrangements are each asserted
// here because each one guards a different failure:
//   (a) numIns  != 0 -- no audio input bus exists to arrange;
//   (b) numOuts != 1 -- exactly one output bus exists; accepting numOuts == 2
//       lets a host successfully negotiate a bus that does not exist;
//   (c) non-stereo   -- the render path reads channelBuffers32[0] and [1]
//       unconditionally, so a mono arrangement is an out-of-bounds audio-thread
//       read (model: plugins/membrum/src/processor/processor.cpp:1044-1069).
// ==============================================================================

#include "processor/processor.h"

#include "plugin_ids.h"
#include "seraphis_test_fixture.h"

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

using namespace Steinberg;
using namespace Steinberg::Vst;

TEST_CASE("Seraphis_ProcessorBusSetup", "[seraphis][processor][bus]") {
    auto proc = std::make_unique<Seraphis::Processor>();
    REQUIRE(proc->initialize(nullptr) == kResultOk);

    SECTION("bus counts are exactly instrument-shaped") {
        REQUIRE(proc->getBusCount(kAudio, kInput) == 0);
        REQUIRE(proc->getBusCount(kAudio, kOutput) == 1);
        REQUIRE(proc->getBusCount(kEvent, kInput) == 1);
    }

    SECTION("an audio input arrangement is rejected") {
        SpeakerArrangement in = SpeakerArr::kStereo;
        SpeakerArrangement out = SpeakerArr::kStereo;
        REQUIRE(proc->setBusArrangements(&in, 1, &out, 1) == kResultFalse);

        // Any input arrangement, not just stereo.
        SpeakerArrangement monoIn = SpeakerArr::kMono;
        REQUIRE(proc->setBusArrangements(&monoIn, 1, &out, 1) == kResultFalse);
    }

    SECTION("zero output buses is rejected") {
        REQUIRE(proc->setBusArrangements(nullptr, 0, nullptr, 0) == kResultFalse);
    }

    SECTION("two output buses is rejected (only one exists)") {
        std::array<SpeakerArrangement, 2> outs{SpeakerArr::kStereo, SpeakerArr::kStereo};
        REQUIRE(proc->setBusArrangements(nullptr, 0, outs.data(),
                                         static_cast<int32>(outs.size())) == kResultFalse);
    }

    SECTION("a mono output arrangement is rejected") {
        SpeakerArrangement mono = SpeakerArr::kMono;
        REQUIRE(proc->setBusArrangements(nullptr, 0, &mono, 1) == kResultFalse);
    }

    SECTION("one stereo output bus with no inputs is accepted") {
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        REQUIRE(proc->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue);
    }

    REQUIRE(proc->terminate() == kResultOk);
}

// ==============================================================================
// Phase 10 SC-004 / FR-005 - the effects section reports NO latency
// ==============================================================================
// Processor::getLatencySamples() returns reverb_->getLatencySamples() and
// nothing else (processor.cpp's FR-033 banner). Phase 10 adds two sources of
// delay to the bus that a naive implementation would be tempted to add to that
// report:
//
//   (a) SpectralDelay::getLatencySamples() == fftSize_ == 1024
//       (spectral_delay.h:542-544, :89), and
//   (b) the send accumulator's fixed ONE-CHUNK pipeline delay, 512 samples
//       (spec C-2 clause 5).
//
// Neither may be reported (FR-005): the send IS a delay, so both are absorbed
// into its delay time, and reporting them would add ~32 ms of latency to an
// instrument for users who never enable the effect. Making the report
// CONDITIONAL is worse still - it would be a runtime latency change on a
// parameter move.
//
// THE THIRD CLAUSE - "sweeping kFxDelayMixId 0 -> 1 -> 0 produces ZERO
// restartComponent(kLatencyChanged) calls" - is asserted here as the observable
// half of a source-level fact: a Steinberg::Vst::AudioEffect has NO route to an
// IComponentHandler (the handler is delivered only to the edit controller), so
// this class holds none and there is no call site to fire. processor.h's
// "NO announceLatencyIfChanged()" banner records that, and the constant-latency
// assertions below are what would FAIL if a later change introduced one and made
// the reported value move.
namespace {

/// Every Phase 10 ID at a NON-default normalized value, in C-6 table order. The
/// point of the sweep is that no combination of them can move the report - so
/// the boolean rows are ON, the feedback row is at its registered maximum (0.95
/// plain) and the two dropdowns are at their last entry.
void driveAllEffectsIdsNonDefault(SeraphisTest::ProcessorFixture& fx) {
    fx.setParam(Seraphis::kFxSaturationId, 1.0);            // 1400: 0.15 -> 1.0
    fx.setParam(Seraphis::kFxDelayMixId, 1.0);              // 1410: 0.0  -> 1.0
    fx.setParam(Seraphis::kFxDelayTimeId, 0.5);             // 1411: 250  -> 1000 ms
    fx.setParam(Seraphis::kFxDelaySpreadId, 0.5);           // 1412: 0    -> 1000 ms
    fx.setParam(Seraphis::kFxDelaySpreadDirectionId, 1.0);  // 1413: LowToHigh -> CenterOut
    fx.setParam(Seraphis::kFxDelayFeedbackId, 1.0);         // 1414: 0.35 -> 0.95 (the cap)
    fx.setParam(Seraphis::kFxDelayTiltId, 1.0);             // 1415: 0    -> +1
    fx.setParam(Seraphis::kFxDelayDiffusionId, 1.0);        // 1416: 0.30 -> 1.0
    fx.setParam(Seraphis::kFxDelayWidthId, 1.0);            // 1417: 0.50 -> 1.0
    fx.setParam(Seraphis::kFxDelaySyncId, 1.0);             // 1418: off  -> ON
    fx.setParam(Seraphis::kFxDelaySyncNoteId, 1.0);         // 1419: "1/16" -> last entry
    fx.setParam(Seraphis::kFxSpectralFreezeId, 1.0);        // 1430: off  -> ON
    fx.setParam(Seraphis::kFxWidthId, 1.0);                 // 1440: 100  -> 200 %
    fx.setParam(Seraphis::kFxWanderDepthId, 1.0);           // 1441: 0.0  -> 1.0
    fx.setParam(Seraphis::kFxWanderRateId, 1.0);            // 1442: 0.50 -> 1.0
    fx.setParam(Seraphis::kFxAzimuthDepthId, 1.0);          // 1443: 0.0  -> 1.0
}

}  // namespace

TEST_CASE("Phase 10 does not change reported latency", "[seraphis][processor][effects]") {
    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(48000.0, 512) == kResultOk);
    // A transport, so the synced rows (1418/1419) are live rather than falling
    // back and being trivially inert.
    fx.setTempo(120.0, 4, 4, true, true);

    Krate::DSP::AetherReverb* reverb = fx.proc->reverbForTest();
    REQUIRE(reverb != nullptr);

    const uint32 baseline = fx.proc->getLatencySamples();
    REQUIRE(baseline == static_cast<uint32>(reverb->getLatencySamples()));

    SECTION("every effects ID at a non-default value leaves the report identical") {
        // Render once at the defaults first, so `baseline` is the value a host
        // saw for a running instance and not merely a pre-roll constant.
        fx.pushEvent(Event::kNoteOnEvent, 60, 0.8f);
        REQUIRE(fx.processBlock(512) == kResultOk);
        REQUIRE(fx.proc->getLatencySamples() == baseline);

        driveAllEffectsIdsNonDefault(fx);
        REQUIRE(fx.processBlock(512) == kResultOk);

        // Neither the send's fftSize (1024) nor the accumulator's one-chunk
        // pipeline delay (512) has been added.
        REQUIRE(fx.proc->getLatencySamples() == baseline);
        REQUIRE(fx.proc->getLatencySamples()
                == static_cast<uint32>(reverb->getLatencySamples()));

        // Several more blocks with the effects section fully engaged: a report
        // that only moved once the send had actually run a chunk would slip past
        // a single-block check (512 samples is exactly one chunk).
        for (int b = 0; b < 8; ++b) {
            REQUIRE(fx.processBlock(512) == kResultOk);
            REQUIRE(fx.proc->getLatencySamples() == baseline);
            REQUIRE(fx.proc->getLatencySamples()
                    == static_cast<uint32>(reverb->getLatencySamples()));
        }
    }

    SECTION("sweeping the delay mix 0 -> 1 -> 0 never moves the report") {
        fx.pushEvent(Event::kNoteOnEvent, 60, 0.8f);
        REQUIRE(fx.processBlock(512) == kResultOk);

        // The mix is the one ID whose extremes decide whether the send runs at
        // all (spec C-3's bypass predicate), i.e. the ID a CONDITIONAL latency
        // report would key off. Each step is followed by a render, so a report
        // computed inside process() would have had its chance to move.
        for (const double mix : {1.0, 0.0, 1.0, 0.0}) {
            fx.setParam(Seraphis::kFxDelayMixId, mix);
            REQUIRE(fx.processBlock(512) == kResultOk);
            REQUIRE(fx.proc->getLatencySamples() == baseline);
            REQUIRE(fx.proc->getLatencySamples()
                    == static_cast<uint32>(reverb->getLatencySamples()));
        }
    }

    SECTION("a re-prepare with the effects section engaged reports the same value") {
        driveAllEffectsIdsNonDefault(fx);
        REQUIRE(fx.processBlock(512) == kResultOk);

        // setupProcessing() is where the send is prepared and seeded (FR-004,
        // FR-027). Re-preparing at a different rate must still report exactly
        // the reverb's value - the constant 1024 - and never fftSize + hop.
        REQUIRE(fx.proc->setActive(false) == kResultOk);  // host-legal order
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = 512;
        setup.sampleRate = 44100.0;
        REQUIRE(fx.proc->setupProcessing(setup) == kResultOk);
        REQUIRE(fx.proc->setActive(true) == kResultOk);

        REQUIRE(fx.proc->getLatencySamples() == baseline);
        REQUIRE(fx.proc->getLatencySamples()
                == static_cast<uint32>(reverb->getLatencySamples()));
    }
}
