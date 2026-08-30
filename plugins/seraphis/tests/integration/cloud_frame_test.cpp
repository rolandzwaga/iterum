// ==============================================================================
// Seraphis - the cloud-frame producer (Phase 11)
// ==============================================================================
// Reference: specs/seraphis-phase11-ui/spec.md   (C-2, FR-011 - FR-015)
//            specs/seraphis-phase11-ui/plan.md   (section 5.1 - 5.3; tasks.md T008)
//
// CRITERIA OWNED BY THIS TU:
//   SC-006 (frame contents, arms (e)/(f)/(g)/(i)), SC-007 (publish cadence),
//   FR-011 (DataExchange lifecycle), FR-012 (once per process() call),
//   FR-014 (the three-clause focus rule).
//   T020 adds SC-008 (frame-sequence determinism on one build) as
//   TEST_CASE("Seraphis_CloudFrame_IsDeterministic").
//
// EVERY FRAME READ GOES THROUGH lastPublishedFrameForTest(), BETWEEN process()
// CALLS, NEVER THROUGH THE DataExchange QUEUE. plugins/seraphis/tests/
// seraphis_test_fixture.h's ProcessorFixture does
// initialize(nullptr) -> setupProcessing -> setActive(true) (:179-213) and NEVER
// calls connect(), so in every plugin-side test the handler is null and no block
// is ever handed out. That is exactly why publishCloudFrame() fills its member
// frame BEFORE consulting the transport - see its body-order banner.
// The one exception is the lifecycle case below, which builds its OWN peer
// IConnectionPoint local to this TU; it is deliberately NOT added to
// ProcessorFixture, because a second SDK object in every Seraphis test's boot
// path would land the DataExchange fallback's onActivate allocation inside
// lifecycle_test.cpp's Seraphis_SetActiveDoesNotAllocate measurement window.
//
// NO std::isnan / std::isinf / std::numeric_limits<>::infinity() ANYWHERE: the
// macOS leg builds with -ffast-math. This TU injects no non-finite payloads, so
// it is deliberately NOT listed under -fno-fast-math in
// plugins/seraphis/tests/CMakeLists.txt (plan section 10.3: only
// partial_edit_test.cpp and ui_negative_control_test.cpp get that flag).
//
// NO CHECKED-IN FLOAT GOLDEN. Every number asserted here is either a comparison
// of two reads of the SAME live DSP state made microseconds apart in this same
// process, an integer count, or (T020) a comparison of two frame sequences this
// same binary produced in this same process. SC-008 is SAME-BUILD determinism
// only and makes NO cross-toolchain claim; render_fingerprint.h's constants are
// deliberately not used for it - see that case's own banner.
// ==============================================================================

#include "processor/cloud_frame.h"
#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include "plugin_ids.h"

#include <pluginterfaces/vst/ivstmessage.h>

#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/seraphis_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// =============================================================================
// T008 - the cloud-frame probe.
//
// processor.h DECLARES `Seraphis::detail::SeraphisCloudFrameProbe` and friends
// it; this TU is the ONLY place in the repository that DEFINES it, so a shipping
// build has no way to call it.
//
// SOLE capability: write the two override bitmasks publishCloudFrame() mirrors.
// SC-006 arm (e) asserts that mirroring, but the SHIPPING writers of those masks
// are the C-5 edit channel's kinds 2 and 3 (Processor::notify ->
// applyEditMessage), which land in T010 - two groups after this one. Without the
// probe, arm (e) could only be written against the default all-zero state, which
// asserts nothing about the mirroring at all.
// =============================================================================
namespace Seraphis::detail {

struct SeraphisCloudFrameProbe {
    static void setOverrideBits(Seraphis::Processor& processor, std::uint64_t maskBits,
                                std::uint64_t panBits) noexcept {
        processor.partialMaskBits_.store(maskBits, std::memory_order_relaxed);
        processor.partialPanOverrideBits_.store(panBits, std::memory_order_relaxed);
    }
};

}  // namespace Seraphis::detail

namespace {

using Catch::Approx;
using SeraphisTest::ProcessorFixture;

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 512;

/// FR-043's mapping: clamp(int(v * 15 + 1.5), 1, 16), so voices n -> (n-1)/15.
constexpr double polyphonyNorm(int voices) {
    return static_cast<double>(voices - 1) / 15.0;
}

constexpr float kHostVelocity = 0.75f;

/// Both sides of every mirroring assertion are reads of the SAME live DSP state
/// taken microseconds apart, so they are equal up to whatever the compiler did
/// with the multiply. The epsilon is the criterion's stated 1e-6 relative; the
/// margin keeps a legitimately-zero entry from failing an epsilon test.
Approx mirrored(float expected) {
    return Approx(static_cast<double>(expected)).epsilon(1.0e-6).margin(1.0e-9);
}

/// A minimal peer connection point. LOCAL TO THIS TU by design - see the banner.
/// Processor::connect only stores the pointer and hands it to
/// DataExchangeHandler::onConnect, so nothing here has to do any real work.
class PeerConnectionPoint final : public Steinberg::Vst::IConnectionPoint {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* /*other*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API disconnect(
        Steinberg::Vst::IConnectionPoint* /*other*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* /*message*/) override {
        ++notifyCount;
        return Steinberg::kResultOk;
    }

    std::size_t notifyCount = 0;
};

}  // namespace

// =============================================================================
// SC-006, FR-013 - the frame mirrors the cloud accessors it claims to mirror.
// =============================================================================
TEST_CASE("Seraphis_CloudFrame_MirrorsCloudAccessors", "[cloud_frame][phase11]") {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->engineForTest() != nullptr);

    // The gate is C-2 clause 6's ONLY short-circuit. Opened here, before any
    // render, through the same atomic the C-5 kind-0 message writes.
    fx.proc->setCloudFrameGateForTest(true);

    fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(8));
    fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, 60, kHostVelocity, 0);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    // Past the attack, so the cloud is genuinely populated rather than ramping
    // from silence with every amplitude still at 0.
    for (int b = 0; b < 40; ++b) {
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    }

    const Seraphis::CloudFrame& frame = fx.proc->lastPublishedFrameForTest();
    INFO("focusVoice = " << static_cast<int>(frame.focusVoice)
                         << ", partialCount = " << static_cast<int>(frame.partialCount));

    // The gate is open, so the producer ran on every one of those calls.
    REQUIRE(fx.proc->cloudFramePublishAttemptCountForTest() > 0u);
    REQUIRE(frame.sequence == fx.proc->cloudFrameSequenceForTest());

    const Krate::DSP::SeraphisEngine& engine = *fx.proc->engineForTest();
    const Krate::DSP::HarmonicCloud& cloud =
        engine.getVoice(static_cast<std::size_t>(frame.focusVoice)).cloud();

    SECTION("every published partial mirrors its accessor pair") {
        REQUIRE(static_cast<std::size_t>(frame.partialCount) == cloud.getActivePartialCount());
        REQUIRE(frame.partialCount > 0);

        for (std::size_t i = 0; i < static_cast<std::size_t>(frame.partialCount); ++i) {
            INFO("partial " << i);
            // DRIFT-INCLUSIVE, C-2 clause 3.
            CHECK(frame.frequencyHz[i]
                  == mirrored(cloud.getPartialFrequencyHz(i) * cloud.getPartialDriftDetune(i)));
            CHECK(frame.amplitude[i]
                  == mirrored(cloud.getPartialCurrentAmplitude(i)
                              * cloud.getPartialAntiAliasGain(i)));
            CHECK(frame.position[i] == mirrored(cloud.getPartialPosition(i)));
        }
    }

    SECTION("entries above the active count are EXACTLY zero, never stale") {
        for (auto i = static_cast<std::size_t>(frame.partialCount);
             i < Krate::DSP::HarmonicCloud::kMaxPartials; ++i) {
            INFO("slot " << i);
            CHECK(frame.frequencyHz[i] == 0.0f);
            CHECK(frame.amplitude[i] == 0.0f);
            CHECK(frame.position[i] == 0.0f);
        }
    }

    SECTION("fundamentalHz is the UNDETUNED f0, not frequencyHz[0]") {
        REQUIRE(frame.activeVoices > 0);
        CHECK(frame.fundamentalHz == mirrored(cloud.getFundamentalHz()));
        CHECK(frame.voiceLevel
              == mirrored(engine.getVoiceLevel(static_cast<std::size_t>(frame.focusVoice))));
        CHECK(static_cast<std::size_t>(frame.activeVoices) == engine.getActiveVoiceCount());
    }

    // --- (e) --------------------------------------------------------------
    SECTION("(e) maskBits and overriddenBits mirror the override table") {
        constexpr std::uint64_t kMask = (1ull << 3) | (1ull << 17);
        constexpr std::uint64_t kPan = (1ull << 9);
        Seraphis::detail::SeraphisCloudFrameProbe::setOverrideBits(*fx.proc, kMask, kPan);

        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        const Seraphis::CloudFrame& after = fx.proc->lastPublishedFrameForTest();

        CHECK(after.maskBits == kMask);
        // "pan AND/OR mask" - bits 3, 9 and 17, and nothing else.
        CHECK(after.overriddenBits == (kMask | kPan));
    }

    // --- (f) --------------------------------------------------------------
    SECTION("(f) morphTravelPosition tracks the morph engine") {
        for (double normalized : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            fx.setParam(Seraphis::kMorphPositionId, normalized);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

            const Seraphis::CloudFrame& after = fx.proc->lastPublishedFrameForTest();
            const float expected =
                fx.proc->engineForTest()
                    ->getVoice(static_cast<std::size_t>(after.focusVoice))
                    .morph()
                    .getTravelPosition();
            INFO("kMorphPositionId = " << normalized);
            CHECK(after.morphTravelPosition == mirrored(expected));
        }
    }
}

// =============================================================================
// SC-006 arm (g), FR-014 - the three-clause focus rule, all three clauses.
//
// No criterion named FR-014 before this one: a focus rule that always returned
// slot 0 passed everything else in the phase.
// =============================================================================
TEST_CASE("Seraphis_CloudFrame_FocusVoiceFollowsAllocationSerial", "[cloud_frame][phase11]") {
    SECTION("clauses (a), (b) and (c) at polyphony 8") {
        ProcessorFixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        REQUIRE(fx.proc->engineForTest() != nullptr);
        fx.proc->setCloudFrameGateForTest(true);

        fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(8));
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        // --- (a) three overlapping notes -----------------------------------
        constexpr std::array<Steinberg::int16, 3> kNotes{55, 62, 69};
        std::array<std::size_t, 3> focusPerNote{0, 0, 0};
        std::array<std::uint64_t, 3> serialPerNote{0, 0, 0};

        for (std::size_t k = 0; k < 3; ++k) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNotes[k], kHostVelocity, 0);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

            const Krate::DSP::SeraphisEngine& engine = *fx.proc->engineForTest();
            const auto focus =
                static_cast<std::size_t>(fx.proc->lastPublishedFrameForTest().focusVoice);

            // The focus slot must carry the GREATEST allocation serial among the
            // slots the allocator does not report Idle.
            std::uint64_t best = 0;
            bool anyNonIdle = false;
            for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
                if (engine.getVoiceState(v) == Krate::DSP::VoiceState::Idle) {
                    continue;
                }
                anyNonIdle = true;
                best = (engine.getVoiceAllocationSerial(v) > best)
                           ? engine.getVoiceAllocationSerial(v)
                           : best;
            }
            INFO("note index " << k << ", focus slot " << focus);
            REQUIRE(anyNonIdle);
            CHECK(engine.getVoiceAllocationSerial(focus) == best);

            focusPerNote[k] = focus;
            serialPerNote[k] = best;
        }

        // A build that always answered slot 0 satisfies the argmax check above
        // (slot 0 IS the argmax while it is the only allocated slot), so the
        // teeth are here: the serial at the focus slot must STRICTLY increase
        // across the three note-ons, which only a real rule delivers.
        CHECK(serialPerNote[1] > serialPerNote[0]);
        CHECK(serialPerNote[2] > serialPerNote[1]);
        CHECK(focusPerNote[1] != focusPerNote[0]);
        CHECK(focusPerNote[2] != focusPerNote[1]);

        // --- (b) release the NEWEST note only ------------------------------
        const std::size_t released = focusPerNote[2];
        fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, kNotes[2], 0.0f, 0);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        {
            const Krate::DSP::SeraphisEngine& engine = *fx.proc->engineForTest();
            INFO("released slot " << released << ", level "
                                  << engine.getVoiceLevel(released));
            // The release still animates: the focus stays on the RELEASED slot,
            // not on the next-highest serial.
            CHECK(static_cast<std::size_t>(fx.proc->lastPublishedFrameForTest().focusVoice)
                  == released);
            CHECK(engine.getVoiceLevel(released) > Seraphis::kCloudFrameSilenceLevel);
            CHECK(released != focusPerNote[1]);
        }

        // --- (c) every voice silent ---------------------------------------
        // The shipped release default is 8 s (kEnvReleaseMsDefault,
        // parameters/life_mod_params.h:68). Pushed to its minimum here so this
        // arm settles in seconds rather than a minute; the focus rule under test
        // does not depend on the release time, only on retirement happening.
        fx.setParam(Seraphis::kEnvReleaseMsId, 0.0);
        fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, kNotes[0], 0.0f, 0);
        fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, kNotes[1], 0.0f, 0);

        // Bounded, and the bound is asserted: a run that never retires every
        // voice is a failure, not a silent pass.
        constexpr int kMaxSettleBlocks = 4000;  // ~42 s at 512 / 48 kHz
        int settleBlocks = 0;
        while (settleBlocks < kMaxSettleBlocks) {
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            ++settleBlocks;
            if (fx.proc->engineForTest()->getActiveVoiceCount() == 0u) {
                break;
            }
        }
        INFO("settle blocks = " << settleBlocks);
        REQUIRE(settleBlocks < kMaxSettleBlocks);

        // A couple more blocks so the retention clause's level test is also
        // below kCloudFrameSilenceLevel (kTailSilenceThreshold is 1e-5,
        // seraphis_voice.h:147, i.e. an order below the display threshold).
        for (int b = 0; b < 4; ++b) {
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        }
        CHECK(fx.proc->lastPublishedFrameForTest().focusVoice == 0);
    }

    SECTION("polyphony 1 degenerates to slot 0 on every arm") {
        ProcessorFixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        fx.proc->setCloudFrameGateForTest(true);

        fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(1));
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        constexpr std::array<Steinberg::int16, 3> kNotes{55, 62, 69};
        for (std::size_t k = 0; k < 3; ++k) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kNotes[k], kHostVelocity, 0);
            for (int b = 0; b < 8; ++b) {
                REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
                CHECK(fx.proc->lastPublishedFrameForTest().focusVoice == 0);
            }
        }
        for (std::size_t k = 0; k < 3; ++k) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, kNotes[k], 0.0f, 0);
        }
        for (int b = 0; b < 32; ++b) {
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            CHECK(fx.proc->lastPublishedFrameForTest().focusVoice == 0);
        }
    }
}

// =============================================================================
// SC-006 arm (i), FR-011 - the handler follows the connection and the activation.
// =============================================================================
TEST_CASE("Seraphis_DataExchangeHandler_FollowsTheConnectionAndActivation",
          "[cloud_frame][phase11]") {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    fx.proc->setCloudFrameGateForTest(true);

    // The fixture never connects, so the handler starts absent - which is the
    // configuration every other case in this TU runs in.
    REQUIRE_FALSE(fx.proc->dataExchangeHandlerLiveForTest());

    PeerConnectionPoint peer;
    REQUIRE(fx.proc->connect(&peer) == Steinberg::kResultTrue);
    CHECK(fx.proc->dataExchangeHandlerLiveForTest());

    // Re-activation does not tear the handler down, and re-activating twice does
    // not double-open: the queue's open/close is setActive()'s business and the
    // handler outlives all of it.
    REQUIRE(fx.proc->setActive(true) == Steinberg::kResultOk);
    CHECK(fx.proc->dataExchangeHandlerLiveForTest());
    REQUIRE(fx.proc->setActive(false) == Steinberg::kResultOk);
    CHECK(fx.proc->dataExchangeHandlerLiveForTest());
    REQUIRE(fx.proc->setActive(true) == Steinberg::kResultOk);
    CHECK(fx.proc->dataExchangeHandlerLiveForTest());

    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    const std::size_t attemptsWhileConnected = fx.proc->cloudFramePublishAttemptCountForTest();
    REQUIRE(attemptsWhileConnected > 0u);

    // Disconnect RELEASES the transport rather than merely idling it.
    REQUIRE(fx.proc->disconnect(&peer) == Steinberg::kResultTrue);
    CHECK_FALSE(fx.proc->dataExchangeHandlerLiveForTest());

    // ...and with no transport at all, every gated publish is accounted as a
    // skipped block, which is what proves the producer still RAN.
    // Reported, not asserted: with a null host context the SDK's fallback queue
    // never opens (dataexchange.cpp:77-79), so the peer sees no QueueOpened
    // message. That is the harness's shape, not a property under test.
    INFO("peer notify count = " << peer.notifyCount);

    const std::size_t skippedBefore = fx.proc->cloudFrameSkippedBlockCountForTest();
    const std::size_t attemptsBefore = fx.proc->cloudFramePublishAttemptCountForTest();
    constexpr int kBlocks = 6;
    for (int b = 0; b < kBlocks; ++b) {
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    }
    CHECK(fx.proc->cloudFramePublishAttemptCountForTest()
          == attemptsBefore + static_cast<std::size_t>(kBlocks));
    CHECK(fx.proc->cloudFrameSkippedBlockCountForTest()
          == skippedBefore + static_cast<std::size_t>(kBlocks));
}

// =============================================================================
// SC-007, FR-012 - EXACTLY one publish attempt per process() call that reached
// the slice loop, and strictly fewer attempts than slices.
//
// THE DIVISOR IS NOT THE HOST CALL COUNT. process() has six pre-slice-loop early
// returns and publishCloudFrame() sits after the loop, so an equality against
// the raw host call count is false about a CORRECT build under pluginval-5 and
// real hosts. effectsStageProcessCalls_ already carries exactly the meaning
// wanted - "process() calls that reached the slice loop" - so it is reused
// rather than a seventh counter being added.
//
// cloudFrameSkippedBlockCountForTest() is reported via INFO and asserted about
// NOT AT ALL: in this harness it rises on every publish (no queue exists) and
// C-2 clause 7 says it is recorded, never gating.
// =============================================================================
TEST_CASE("Seraphis_CloudFrame_PublishesOncePerProcessCall", "[cloud_frame][phase11]") {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    // Opened BEFORE the first render, so the lifetime counters can be compared
    // directly rather than as deltas.
    fx.proc->setCloudFrameGateForTest(true);

    constexpr double kSeconds = 60.0;
    const auto kBlocks =
        static_cast<std::size_t>(kSeconds * kSampleRate / static_cast<double>(kBlock));

    // processBlock() rather than renderBlocks(): this case asserts only on
    // counters, so capturing 2 x 2.88 M samples of audio would cost ~23 MB and
    // a push_back per sample for nothing.
    for (std::size_t b = 0; b < kBlocks; ++b) {
        if (b == 0) {
            fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(8));
        }
        // MIDI on NON-BLOCK boundaries, so the slice loop genuinely subdivides.
        if (b % 7 == 1) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent,
                         static_cast<Steinberg::int16>(48 + (b % 24)), kHostVelocity, 137);
        }
        if (b % 7 == 4) {
            fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent,
                         static_cast<Steinberg::int16>(48 + ((b - 3) % 24)), 0.0f, 291);
        }
        // Automation on a class-(b) smoother forces the absolute 64-sample
        // control-chunk subdivision on top of the MIDI slicing.
        if (b % 11 == 0) {
            fx.setParam(Seraphis::kBodyDampingId, (b % 22 == 0) ? 0.1 : 0.9);
        }
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    }

    const std::size_t attempts = fx.proc->cloudFramePublishAttemptCountForTest();
    const std::size_t calls = fx.proc->effectsStageProcessCallsForTest();
    const std::size_t slices = fx.proc->renderSliceCountForTest();
    INFO("attempts = " << attempts << ", process calls = " << calls << ", slices = " << slices
                       << ", skipped = " << fx.proc->cloudFrameSkippedBlockCountForTest());

    // ONE attempt per process() call that reached the slice loop - never one per
    // slice, which would issue up to 8x the frames and exhaust the queue.
    CHECK(attempts == calls);
    CHECK(attempts == kBlocks);
    // ...and the render really did subdivide, or the equality above would be
    // vacuous.
    CHECK(slices > attempts);
    // The sequence number advances exactly once per attempt.
    CHECK(static_cast<std::size_t>(fx.proc->cloudFrameSequenceForTest()) == attempts);
}

// =============================================================================
// T020 / SC-008 - THE FRAME SEQUENCE IS REPRODUCIBLE
// =============================================================================
// Two runs of the same SEEDED script, in the SAME PROCESS on the SAME BUILD,
// produce frame sequences that agree on four aggregates, each compared
// RELATIVELY at 1e-5, plus `sequence` strictly increasing and `partialCount`
// equal frame-for-frame.
//
// TWO FRESH FIXTURES, NOT ONE RE-PREPARED INSTANCE. A re-prepare returns a
// processor to a REPRODUCIBLE state but not to a VIRGIN one - ContinuousBody is
// path dependent between its first render and every later one, measured at the
// layer that owns it (dsp/tests/unit/systems/seraphis_voice_test.cpp:816-827) and
// again through the whole processor (integration/effects_chain_test.cpp:382-393),
// where two FRESH Processor instances are recorded as agreeing exactly while a
// virgin-vs-re-prepared pair diverges by ~6.5e-3. SC-008 is a claim about a fresh
// instance loading the same state, so two fresh fixtures is the shape that
// matches the claim; it also removes the warm-up discipline the negative control
// needs, because neither run is a "second render".
//
// THE SEQUENCE IS READ AT lastPublishedFrameForTest(), ONCE AFTER EVERY process()
// CALL - the producer's own frame, never the DataExchange queue. ProcessorFixture
// never calls connect(), so a headless run lands ZERO blocks and a queue-sourced
// sequence would be empty for a perfectly correct build (see this file's banner).
//
// render_fingerprint.h's CONSTANTS ARE DELIBERATELY NOT USED. kSampleTolerance =
// 1.0e-4f (:49) is an ABSOLUTE per-sample bound on AUDIO, calibrated against a
// signal peak of 2.17 (:25-29). Pointed at frequencyHz - absolute Hz - it would
// be 2.5e-8 relative on a 4 kHz partial, below float epsilon (~1.2e-7), i.e. a
// bound no correct implementation can meet. The four relative bounds below are
// the substitute. If a pilot run measures a spread above 1e-5 on any of them, the
// MEASURED number is recorded in the spec (T026) and the criterion re-stated with
// it - the bound is derived, never relaxed to fit a failing run. Every measured
// spread is reported via INFO so that number is readable without a re-run.
//
// SAME-BUILD DETERMINISM ONLY. No cross-toolchain agreement is claimed, asserted
// or implied, and nothing here is checked in: both sides are produced by this
// binary, in this process, seconds apart.
// =============================================================================
namespace {

/// 938 x 512 = 480 256 samples = 10.005 s at 48 kHz - the same script geometry
/// the phase's other 10 s renders use.
constexpr std::size_t kDetBlocks = 938;

/// kSeedId (3) is a 16-entry StringListParameter whose denormalization is
/// clamp(int(v * 15 + 0.5), 0, 15) (parameters/global_params.h:106-114), so
/// 5/15 selects index 5. SC-008 says "the same SEEDED script", so the seed is
/// written EXPLICITLY rather than inherited: a determinism criterion that never
/// touches the seed parameter is not testing the seeded path.
constexpr double kDetSeedNormalized = 5.0 / 15.0;

constexpr std::array<Steinberg::int16, 4> kDetPitches{48, 55, 62, 69};
/// Staggered, so the render carries voice allocation, sustain and release rather
/// than one block-aligned chord.
constexpr std::array<std::size_t, 4> kDetNoteOnBlocks{4, 60, 120, 180};
constexpr std::array<std::size_t, 4> kDetNoteOffBlocks{500, 560, 620, 680};
/// NON-BLOCK-BOUNDARY sample offsets (all < 512), so the slice loop genuinely
/// subdivides and the determinism claim covers the subdivided path too.
constexpr std::array<Steinberg::int32, 4> kDetNoteOffsets{137, 291, 53, 401};

/// SC-008's bound, relative, on all four aggregates.
constexpr double kDetRelativeBound = 1.0e-5;

/// One frame, reduced to the per-frame quantities SC-008's four aggregates are
/// built from. The whole 808-byte frame is deliberately NOT retained: the
/// criterion is about the aggregates, and a stored frame stream would invite a
/// per-field exact comparison the spec does not ask for.
struct FrameRecord {
    std::uint32_t sequence = 0;
    std::uint8_t partialCount = 0;
    /// False when the frame carries no partials, no amplitude at all, or a
    /// non-positive frequency - i.e. when the amplitude-weighted mean pitch is
    /// undefined rather than zero. Such frames are EXCLUDED from the pitch
    /// aggregates instead of being folded in as 0, which would make the metric a
    /// function of the silence length rather than of the cloud.
    bool pitchValid = false;
    double meanPitch = 0.0;      // sum(a_i * log2(f_i)) / sum(a_i)
    double meanAmplitude = 0.0;  // sum(a_i) / partialCount
    double meanPosition = 0.0;   // sum(position_i) / partialCount
    /// NOT one of SC-008's four. A non-vacuity witness only: mean position can
    /// legitimately sit at ~0 for a symmetric spread, which would make its
    /// relative comparison a comparison of two zeros. This says the position
    /// array was populated at all.
    double meanAbsPosition = 0.0;
};

[[nodiscard]] FrameRecord recordFrame(const Seraphis::CloudFrame& frame) {
    FrameRecord record;
    record.sequence = frame.sequence;
    record.partialCount = frame.partialCount;

    const auto n = static_cast<std::size_t>(frame.partialCount);
    if (n == 0) {
        return record;
    }

    double sumAmplitude = 0.0;
    double sumWeightedPitch = 0.0;
    double sumPosition = 0.0;
    double sumAbsPosition = 0.0;
    bool everyFrequencyPositive = true;

    for (std::size_t i = 0; i < n; ++i) {
        const double amplitude = static_cast<double>(frame.amplitude[i]);
        const double hz = static_cast<double>(frame.frequencyHz[i]);
        const double position = static_cast<double>(frame.position[i]);

        sumAmplitude += amplitude;
        sumPosition += position;
        sumAbsPosition += std::fabs(position);
        if (hz > 0.0) {
            sumWeightedPitch += amplitude * std::log2(hz);
        } else {
            everyFrequencyPositive = false;
        }
    }

    const auto count = static_cast<double>(n);
    record.meanAmplitude = sumAmplitude / count;
    record.meanPosition = sumPosition / count;
    record.meanAbsPosition = sumAbsPosition / count;
    if (everyFrequencyPositive && sumAmplitude > 0.0) {
        record.pitchValid = true;
        record.meanPitch = sumWeightedPitch / sumAmplitude;
    }
    return record;
}

/// SC-008's four aggregates over a whole sequence, plus the two frame counts the
/// non-vacuity checks need.
struct SequenceAggregates {
    double meanPitch = 0.0;
    double pitchTotalVariation = 0.0;
    double meanAmplitude = 0.0;
    double meanPosition = 0.0;
    double meanAbsPosition = 0.0;  // witness only, see FrameRecord
    std::size_t populatedFrames = 0;
    std::size_t pitchFrames = 0;
};

[[nodiscard]] SequenceAggregates aggregateSequence(const std::vector<FrameRecord>& frames) {
    SequenceAggregates out;

    double pitchSum = 0.0;
    double amplitudeSum = 0.0;
    double positionSum = 0.0;
    double absPositionSum = 0.0;
    double previousPitch = 0.0;
    bool havePrevious = false;

    for (const FrameRecord& record : frames) {
        if (record.partialCount == 0) {
            continue;
        }
        ++out.populatedFrames;
        amplitudeSum += record.meanAmplitude;
        positionSum += record.meanPosition;
        absPositionSum += record.meanAbsPosition;

        if (!record.pitchValid) {
            continue;
        }
        ++out.pitchFrames;
        pitchSum += record.meanPitch;
        if (havePrevious) {
            out.pitchTotalVariation += std::fabs(record.meanPitch - previousPitch);
        }
        previousPitch = record.meanPitch;
        havePrevious = true;
    }

    if (out.populatedFrames > 0) {
        const auto n = static_cast<double>(out.populatedFrames);
        out.meanAmplitude = amplitudeSum / n;
        out.meanPosition = positionSum / n;
        out.meanAbsPosition = absPositionSum / n;
    }
    if (out.pitchFrames > 0) {
        out.meanPitch = pitchSum / static_cast<double>(out.pitchFrames);
    }
    return out;
}

/// |a - b| / max(|a|, |b|), and 0 when both are exactly 0 - so a pair that is
/// legitimately zero on both sides passes rather than dividing by zero. The
/// non-vacuity checks in the case are what stop that branch from being how the
/// criterion is satisfied.
[[nodiscard]] double relativeSpread(double a, double b) noexcept {
    const double scale = std::max(std::fabs(a), std::fabs(b));
    return (scale > 0.0) ? (std::fabs(a - b) / scale) : 0.0;
}

/// One full run of SC-008's script on a FRESH fixture, returning the frame
/// sequence the producer generated. The frame is read ONCE after every process()
/// call, between calls - never concurrently, which is what
/// lastPublishedFrameForTest()'s contract requires.
[[nodiscard]] std::vector<FrameRecord> runDeterminismScript() {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->engineForTest() != nullptr);
    fx.proc->setCloudFrameGateForTest(true);

    std::vector<FrameRecord> frames;
    frames.reserve(kDetBlocks);

    for (std::size_t b = 0; b < kDetBlocks; ++b) {
        if (b == 0) {
            fx.setParam(Seraphis::kSeedId, kDetSeedNormalized);
            fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(8));
        }
        for (std::size_t k = 0; k < kDetPitches.size(); ++k) {
            if (b == kDetNoteOnBlocks[k]) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kDetPitches[k], kHostVelocity,
                             kDetNoteOffsets[k]);
            }
            if (b == kDetNoteOffBlocks[k]) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, kDetPitches[k], 0.0f,
                             kDetNoteOffsets[k]);
            }
        }
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        frames.push_back(recordFrame(fx.proc->lastPublishedFrameForTest()));
    }
    REQUIRE(fx.checkCanaries());
    return frames;
}

}  // namespace

TEST_CASE("Seraphis_CloudFrame_IsDeterministic", "[cloud_frame][phase11]") {
    const std::vector<FrameRecord> runA = runDeterminismScript();
    const std::vector<FrameRecord> runB = runDeterminismScript();

    REQUIRE(runA.size() == kDetBlocks);
    REQUIRE(runB.size() == runA.size());

    // --- `sequence` strictly increasing, in BOTH runs ------------------------
    // Counted rather than asserted per frame so a failure reports HOW MANY
    // frames broke the rule and WHERE the first one was, instead of 938
    // near-identical assertions.
    std::size_t nonIncreasingA = 0;
    std::size_t nonIncreasingB = 0;
    std::size_t firstNonIncreasing = runA.size();
    for (std::size_t i = 1; i < runA.size(); ++i) {
        const bool badA = !(runA[i].sequence > runA[i - 1].sequence);
        const bool badB = !(runB[i].sequence > runB[i - 1].sequence);
        nonIncreasingA += badA ? 1u : 0u;
        nonIncreasingB += badB ? 1u : 0u;
        if ((badA || badB) && firstNonIncreasing == runA.size()) {
            firstNonIncreasing = i;
        }
    }
    INFO("first non-increasing sequence index = " << firstNonIncreasing);
    CHECK(nonIncreasingA == 0u);
    CHECK(nonIncreasingB == 0u);
    // The gate was open for every block, so the producer numbered every frame:
    // this is what stops the "strictly increasing" arm passing on a run that
    // published nothing at all.
    CHECK(runA.back().sequence == static_cast<std::uint32_t>(kDetBlocks));
    CHECK(runB.back().sequence == runA.back().sequence);

    // --- `partialCount` equal frame-for-frame --------------------------------
    std::size_t countMismatches = 0;
    std::size_t firstCountMismatch = runA.size();
    for (std::size_t i = 0; i < runA.size(); ++i) {
        if (runA[i].partialCount != runB[i].partialCount) {
            ++countMismatches;
            if (firstCountMismatch == runA.size()) {
                firstCountMismatch = i;
            }
        }
    }
    INFO("first partialCount mismatch index = " << firstCountMismatch);
    CHECK(countMismatches == 0u);

    // --- SC-008's four aggregates --------------------------------------------
    const SequenceAggregates aggA = aggregateSequence(runA);
    const SequenceAggregates aggB = aggregateSequence(runB);

    // NON-VACUITY, before any comparison. Two empty sequences agree on all four
    // metrics trivially, and so do two all-zero ones.
    REQUIRE(aggA.populatedFrames > 0u);
    REQUIRE(aggA.pitchFrames > 0u);
    CHECK(aggA.populatedFrames == aggB.populatedFrames);
    CHECK(aggA.pitchFrames == aggB.pitchFrames);
    REQUIRE(aggA.meanPitch > 0.0);          // log2(Hz) of an audible partial
    REQUIRE(aggA.meanAmplitude > 0.0);
    REQUIRE(aggA.pitchTotalVariation > 0.0);  // the cloud actually moved
    REQUIRE(aggA.meanAbsPosition > 0.0);      // the position array was populated

    const double pitchSpread = relativeSpread(aggA.meanPitch, aggB.meanPitch);
    const double variationSpread =
        relativeSpread(aggA.pitchTotalVariation, aggB.pitchTotalVariation);
    const double amplitudeSpread = relativeSpread(aggA.meanAmplitude, aggB.meanAmplitude);
    const double positionSpread = relativeSpread(aggA.meanPosition, aggB.meanPosition);

    // REPORTED, so the pilot numbers T026 may have to write back into the spec
    // are readable without re-running the suite.
    INFO("mean pitch      A = " << aggA.meanPitch << ", B = " << aggB.meanPitch
                                << ", relative spread = " << pitchSpread);
    INFO("pitch total var A = " << aggA.pitchTotalVariation
                                << ", B = " << aggB.pitchTotalVariation
                                << ", relative spread = " << variationSpread);
    INFO("mean amplitude  A = " << aggA.meanAmplitude << ", B = " << aggB.meanAmplitude
                                << ", relative spread = " << amplitudeSpread);
    INFO("mean position   A = " << aggA.meanPosition << ", B = " << aggB.meanPosition
                                << ", relative spread = " << positionSpread
                                << " (mean |position| = " << aggA.meanAbsPosition << ")");

    CHECK(pitchSpread <= kDetRelativeBound);
    CHECK(variationSpread <= kDetRelativeBound);
    CHECK(amplitudeSpread <= kDetRelativeBound);
    CHECK(positionSpread <= kDetRelativeBound);
}

// =============================================================================
// T025 / SC-017, FR-021 - A MACRO RING PERTURBS THE CONSTELLATION, AND THE
// PERTURBATION IS THE REAL MATRIX RESPONSE READ BACK OUT OF THE SNAPSHOT.
// =============================================================================
// Headless, ON THE PRODUCER: every number below is read from
// lastPublishedFrameForTest() after a process() call. "Published CloudFrames"
// means the producer's own frame, never a queue delivery - ProcessorFixture
// never calls connect() (this TU's banner).
//
// THE METRIC IS
//
//     P = sum_i ( a_i * log2(f_i / f0) ) / sum_i a_i        over i < partialCount
//
// with f0 = frame.fundamentalHz - the amplitude-weighted pitch centroid of the
// constellation, in OCTAVES ABOVE THE FUNDAMENTAL and therefore dimensionless.
// f0 is the frame's UNDETUNED fundamental (C-2 clause 3 forbids frequencyHz[0]),
// so P is invariant to which note is held and cannot be moved by transposing the
// script.
//
// WHY BLOOM. Its four Voice-owned rows all push the constellation's weight
// upward: CloudSpectralTiltDb 0 -> +9 dB/oct, CloudRichness 0.60 -> 1.0,
// MorphTargetPosition slot 0 -> slot 1 (the brighter Glass state), plus
// CloudStereoSpread/VoiceWidth which move position, not pitch
// (seraphis_macro_matrix.h:272-341). P is the observable those first three
// share.
//
// THE MORPH TRAVEL RATE IS PUSHED TO ITS MAXIMUM IN EVERY RUN, INCLUDING THE
// CONTROLS. kMorphTravelRateDefault IS kMinTravelRate = 1/600 journeys per
// second (morph_params.h:80, spectral_morph_engine.h:101), i.e. TEN MINUTES per
// journey, so at the shipped default Bloom's MorphTargetPosition row could not
// travel a measurable distance inside any render this suite can afford, and the
// arm would be measuring tilt and richness only. Raising it is a property of the
// SCRIPT, applied identically to the swept arm, the negative control and the
// drift reference - it is not a threshold, and it moves no criterion.
//
// NO SUPPRESSION SEAM IS INVENTED. SeraphisMacroMatrix's entire mutator surface
// is setMacro (:554), setMacros (:599) and setTargetBase (:708); adding a
// bypass would be a dsp/ addition outside the phase's enumerated set. So the
// negative control sweeps kMasterGainId instead - a control that is applied to
// the REVERB RETURN, after engine_->processStereoBlock() (processor.cpp:2345-
// 2349), and therefore cannot reach a cloud accessor at all.
//
// "NO MOVEMENT" IS NOT ASSERTED AND COULD NOT BE: per-partial Brownian drift
// runs unconditionally and frequencyHz is drift-inclusive (C-2 clause 3). The
// control is bounded against the swept response AND against the drift-only
// spread of the same metric on the same script, which is the honest floor.
//
// THE TWO TABLES ARE EMITTED WITH WARN, NOT INFO. This case carries T025's
// SC-017(a) pilot measurement, whose whole point is to be READ on a passing run;
// INFO prints only on failure and would make the measurement invisible exactly
// when it succeeded.
// =============================================================================
namespace {

/// SC-017's five sweep points, shared by the swept arm and the negative control
/// so "the same script" is literally the same script.
constexpr std::array<double, 5> kMacroSweepPoints{0.0, 0.25, 0.50, 0.75, 1.0};

/// MEASURED (T025 step 2 / T026, 2026-08-04). Not a placeholder, not a guess.
/// The swept arm below printed its five-point table on a PASSING run:
///
///     Bloom 0.00  P = 1.81624   (window spread 0.0869709)
///     Bloom 0.25  P = 2.78791   (window spread 0.0582665)
///     Bloom 0.50  P = 3.50097   (window spread 0.0438952)
///     Bloom 0.75  P = 4.03203   (window spread 0.0426494)
///     Bloom 1.00  P = 4.40121   (window spread 0.0404959)
///     P(1) - P(0) = 2.58497 oct
///
/// 2.58497 ROUNDED DOWN to two decimals = 2.58, written back here and into spec
/// SC-017(a) in the same change, per OQ-4's rule that the only permitted
/// resolution is to run the sweep, record the full table, and write back the
/// measured figure. The ruled pilot start was 0.35; the measured value is 7.4x
/// larger, so the criterion got STRONGER, never weaker.
///
/// It clears its own noise floor by ~30x: the drift-only reference arm below
/// measures a window spread of 0.0869 oct with nothing swept at all.
///
/// ONCE WRITTEN THIS IS FIXED, AND IT ONLY EVER MOVES UP. Lowering it to fit a
/// failing run would be tuning the test to the instrument; raising the matrix's
/// response to hit a pre-guessed figure would be the same sin in the other
/// direction.
constexpr double kBloomOctaveThreshold = 2.58;

/// The registered default, 100 % gain (global_params.h:129-131). Held here in
/// every run the control arm is not itself sweeping.
constexpr double kMasterGainNeutral = 0.5;

/// kCloudDriftDepthId maps normalized -> [0, kMaxDriftCents] linearly
/// (cloud_params.h:140-146), so 0.0 IS the drift precondition and 1.0 is the
/// component maximum used by the drift reference.
constexpr double kDriftOff = 0.0;
constexpr double kDriftFull = 1.0;

/// ~4.3 s of settle, then ~1.07 s of measurement, at 512 / 48 kHz. The settle
/// covers the cloud attack, the macro smoothers and a full morph journey at the
/// maximum travel rate (1 journey/s across the default two states).
constexpr std::size_t kCentroidSettleBlocks = 400;
constexpr std::size_t kCentroidWindowBlocks = 100;

struct PitchCentroid {
    bool valid = false;
    double octaves = 0.0;
};

/// P for one published frame. Invalid - never 0 - when the weighted mean is
/// undefined (no partials, no amplitude, or a non-positive frequency), so a
/// silent frame is EXCLUDED from the mean rather than folded in as zero, which
/// would make the metric a function of the settle length.
[[nodiscard]] PitchCentroid pitchCentroid(const Seraphis::CloudFrame& frame) {
    PitchCentroid out;
    const auto n = static_cast<std::size_t>(frame.partialCount);
    const auto f0 = static_cast<double>(frame.fundamentalHz);
    if (n == 0 || !(f0 > 0.0)) {
        return out;
    }

    double sumAmplitude = 0.0;
    double sumWeighted = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double amplitude = static_cast<double>(frame.amplitude[i]);
        const double hz = static_cast<double>(frame.frequencyHz[i]);
        if (!(hz > 0.0)) {
            return out;  // still invalid, still not zero
        }
        sumAmplitude += amplitude;
        sumWeighted += amplitude * std::log2(hz / f0);
    }
    if (!(sumAmplitude > 0.0)) {
        return out;
    }

    out.valid = true;
    out.octaves = sumWeighted / sumAmplitude;
    return out;
}

struct CentroidRun {
    double mean = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    std::size_t frames = 0;

    /// The metric's own wobble across the measurement window, on this script.
    [[nodiscard]] double spread() const noexcept { return maximum - minimum; }
};

/// One run of SC-017's script on a FRESH fixture, returning P over the
/// measurement window. Fresh rather than re-prepared for the reason recorded in
/// SC-008's banner above: a re-prepared processor is reproducible but not
/// virgin, and ContinuousBody is path-dependent between its first render and
/// every later one.
[[nodiscard]] CentroidRun runCentroidScript(double bloomNormalized,
                                            double masterGainNormalized,
                                            double driftDepthNormalized) {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->engineForTest() != nullptr);
    fx.proc->setCloudFrameGateForTest(true);

    // Every macro at its FR-060 neutral except the one under sweep. Gravity is
    // the one bipolar macro and is neutral at 0.5, the rest at 0
    // (macro_params.h:50-56, seraphis_macro_matrix.h:548-550).
    fx.setParam(Seraphis::kMacroDreamId, 0.0);
    fx.setParam(Seraphis::kMacroBloomId, bloomNormalized);
    fx.setParam(Seraphis::kMacroDissolveId, 0.0);
    fx.setParam(Seraphis::kMacroGravityId, 0.5);
    fx.setParam(Seraphis::kMacroEntropyId, 0.0);

    // SC-013's drift precondition in its plugin form.
    fx.setParam(Seraphis::kCloudDriftDepthId, driftDepthNormalized);
    fx.setParam(Seraphis::kMasterGainId, masterGainNormalized);
    fx.setParam(Seraphis::kMorphTravelRateId, 1.0);  // see the banner
    fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(8));

    fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, 60, kHostVelocity, 0);

    for (std::size_t b = 0; b < kCentroidSettleBlocks; ++b) {
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    }

    CentroidRun run;
    double sum = 0.0;
    for (std::size_t b = 0; b < kCentroidWindowBlocks; ++b) {
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        const PitchCentroid p = pitchCentroid(fx.proc->lastPublishedFrameForTest());
        if (!p.valid) {
            continue;
        }
        if (run.frames == 0) {
            run.minimum = p.octaves;
            run.maximum = p.octaves;
        } else {
            run.minimum = std::min(run.minimum, p.octaves);
            run.maximum = std::max(run.maximum, p.octaves);
        }
        ++run.frames;
        sum += p.octaves;
    }
    REQUIRE(fx.checkCanaries());

    if (run.frames > 0) {
        run.mean = sum / static_cast<double>(run.frames);
    }
    return run;
}

}  // namespace

TEST_CASE("Seraphis_MacroRing_PerturbsConstellation", "[cloud_frame][phase11]") {
    // --- (a) the swept arm -------------------------------------------------
    std::array<CentroidRun, kMacroSweepPoints.size()> bloom{};
    for (std::size_t k = 0; k < kMacroSweepPoints.size(); ++k) {
        bloom[k] = runCentroidScript(kMacroSweepPoints[k], kMasterGainNeutral, kDriftOff);
        // Non-vacuity, per point: a run whose window produced no valid frame
        // would let every comparison below succeed on empty data.
        INFO("bloom point " << kMacroSweepPoints[k]);
        REQUIRE(bloom[k].frames == kCentroidWindowBlocks);
    }

    const double sweptDelta = bloom.back().mean - bloom.front().mean;

    // T025 step 2's measured table, ALWAYS PRINTED - this is the pilot run.
    WARN("SC-017(a) Bloom sweep, P = amplitude-weighted octaves above f0:"
         << "\n  Bloom 0.00  P = " << bloom[0].mean << "  (window spread "
         << bloom[0].spread() << ")"
         << "\n  Bloom 0.25  P = " << bloom[1].mean << "  (window spread "
         << bloom[1].spread() << ")"
         << "\n  Bloom 0.50  P = " << bloom[2].mean << "  (window spread "
         << bloom[2].spread() << ")"
         << "\n  Bloom 0.75  P = " << bloom[3].mean << "  (window spread "
         << bloom[3].spread() << ")"
         << "\n  Bloom 1.00  P = " << bloom[4].mean << "  (window spread "
         << bloom[4].spread() << ")"
         << "\n  P(1) - P(0) = " << sweptDelta << " oct, against the MEASURED threshold T = "
         << kBloomOctaveThreshold
         << " oct (2.58497 rounded DOWN to two decimals, written back here and into spec"
            " SC-017(a) on 2026-08-04). T only ever moves UP.");

    for (std::size_t k = 1; k < kMacroSweepPoints.size(); ++k) {
        INFO("Bloom " << kMacroSweepPoints[k - 1] << " -> " << kMacroSweepPoints[k] << ": P "
                      << bloom[k - 1].mean << " -> " << bloom[k].mean);
        CHECK(bloom[k].mean > bloom[k - 1].mean);
    }
    CHECK(sweptDelta >= kBloomOctaveThreshold);

    // --- the drift-only reference -------------------------------------------
    // The SAME script with Bloom held at neutral and drift at the component
    // maximum. Its window spread is how far P moves when NOTHING is swept and
    // only Brownian drift runs - the honest floor for arm (b), and the reason
    // "no movement" is never asserted.
    const CentroidRun driftReference = runCentroidScript(0.0, kMasterGainNeutral, kDriftFull);
    REQUIRE(driftReference.frames == kCentroidWindowBlocks);
    const double driftOnlySpread = driftReference.spread();
    // Non-vacuity: a reference that never moved would make the bound below
    // vacuously strict rather than meaningful.
    REQUIRE(driftOnlySpread > 0.0);

    // --- (b) the negative control -------------------------------------------
    std::array<CentroidRun, kMacroSweepPoints.size()> control{};
    for (std::size_t k = 0; k < kMacroSweepPoints.size(); ++k) {
        control[k] = runCentroidScript(0.0, kMacroSweepPoints[k], kDriftOff);
        INFO("master-gain point " << kMacroSweepPoints[k]);
        REQUIRE(control[k].frames == kCentroidWindowBlocks);
    }

    double controlMin = control.front().mean;
    double controlMax = control.front().mean;
    for (const CentroidRun& run : control) {
        controlMin = std::min(controlMin, run.mean);
        controlMax = std::max(controlMax, run.mean);
    }
    const double controlDelta = controlMax - controlMin;

    WARN("SC-017(b) negative control, kMasterGainId swept over the same five points"
         " with every macro at neutral:"
         << "\n  gain 0.00  P = " << control[0].mean << "\n  gain 0.25  P = " << control[1].mean
         << "\n  gain 0.50  P = " << control[2].mean << "\n  gain 0.75  P = " << control[3].mean
         << "\n  gain 1.00  P = " << control[4].mean << "\n  |dP| control = " << controlDelta
         << " oct,  |dP| swept = " << sweptDelta
         << " oct,  drift-only window spread = " << driftOnlySpread << " oct.");

    CHECK(controlDelta <= 0.1 * sweptDelta);
    // Note the bound is `<=`, not `<`: master gain is applied to the reverb
    // return AFTER the engine renders (processor.cpp:2345-2349), so a CORRECT
    // build reproduces the identical cloud at all five gain points and
    // controlDelta is expected to be exactly 0. A strict `<` would be a claim
    // about floating-point noise, not about the constellation.
    CHECK(controlDelta <= driftOnlySpread);
}
