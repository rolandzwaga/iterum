// ==============================================================================
// Seraphis - the Phase 11 negative control: the producer changes no sample
// ==============================================================================
// Reference: specs/seraphis-phase11-ui/spec.md   (SC-001, FR-042, C-2 clause 6)
//            specs/seraphis-phase11-ui/plan.md   (section 10.2's SC-001 row;
//                                                 tasks.md T020)
//
// CRITERIA OWNED BY THIS TU: SC-001 only. It is the criterion the whole phase is
// built against - "the cloud-frame producer is the read-only observer C-2 claims
// it is" - and it is deliberately the ONLY case in this file, so a failure here
// is unambiguous about what regressed.
//
// ONE BUILD, ONE PROCESS, ONE Processor INSTANCE. Two renders of an identical
// 10 s MIDI script, every registered parameter at its shipped default, differing
// ONLY in the C-2 clause 6 gate: arm A has setCloudFrameGateForTest(true) - the
// producer runs, the 64-partial snapshot loop executes and frames are published -
// and arm B has it false.
//
// WHY EXACT EQUALITY IS LEGITIMATE HERE AND ONLY HERE, and why this is NOT a
// float golden. Both arms are the SAME COMPILED CODE PATH on the SAME INSTANCE,
// so codegen is identical by construction and the only question asked is whether
// publishCloudFrame() mutates anything the audio path reads. A CROSS-BUILD or
// cross-toolchain bit-exact comparison is FORBIDDEN (roadmap line 598): it would
// demand bit-identical floating point across MSVC / GCC / AppleClang, which
// tests/test_helpers/render_fingerprint.h:20-30 measures at 2.9e-5 per sample.
// Nothing here is checked in, hashed, or compared to a stored constant - the
// only value compared is a difference between two renders this same binary made
// in this same process, seconds apart. `node tools/lint-float-bit-goldens.js` is
// the gate for that distinction and this TU passes it: it reinterprets no float
// bits and accumulates no digest. The same reasoning is carried verbatim by
// Phase 10's SC-002 (integration/effects_chain_test.cpp:2081-2091).
//
// NO std::isnan / std::isinf / std::numeric_limits<>::infinity() ANYWHERE: the
// macOS leg builds with -ffast-math, under which the compiler may assume finite
// values and fold such a test away.
//
// COMPILE FLAGS: this TU IS listed under "-fno-fast-math -fno-finite-math-only"
// in plugins/seraphis/tests/CMakeLists.txt (T020, plan section 10.3) - SC-001's
// exact per-sample comparison must not be reshaped by fast-math contraction,
// which is free to fuse the two arms' arithmetic differently once anything
// around the call site changes.
// ==============================================================================

#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

using Fixture = SeraphisTest::ProcessorFixture;

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 512;
constexpr std::size_t kBlockSamples = 512;

/// 938 x 512 = 480 256 samples = 10.005 s at 48 kHz - SC-001's "10 s". Same
/// geometry Phase 10's SC-002 uses (effects_chain_test.cpp:360-363), so the two
/// negative controls measure the same script and a divergence between them is
/// attributable to the change under test rather than to the render.
constexpr std::size_t kRenderBlocks = 938;
/// Note-offs at 7.47 s, leaving ~2.5 s of release and tail inside the render, so
/// the sustained AND the released halves of the voice pool are compared.
constexpr std::size_t kNoteOffBlock = 700;

/// SC-001's "8-voice operating point". EIGHT notes, and NO kPolyphonyId queue:
/// the criterion says "every parameter at its shipped default" and the shipped
/// polyphony default IS 8 (global_params.h's kPolyphonyId registration), so
/// writing the parameter would be the one place this render stopped being a
/// defaults render.
constexpr Steinberg::int16 kEightNoteChord[] = {48, 52, 55, 59, 62, 64, 67, 71};
constexpr float kChordVelocity = 0.8f;

[[nodiscard]] float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    float worst = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::fabs(a[i] - b[i]));
    }
    return worst;
}

/// SC-001 is "ONE build, ONE process, ONE Processor instance", so the two renders
/// cannot be two fixtures - but they must start from identical DSP state, and the
/// first render leaves a live reverb tail, voice allocator and smoother set
/// behind. Re-preparing the SAME instance is what reconciles the two.
///
/// WHAT IT DOES *NOT* DO - AND WHY THE WARM-UP RENDER BELOW EXISTS. A re-prepare
/// returns the processor to a REPRODUCIBLE state, not to a VIRGIN one:
/// `ContinuousBody` is path dependent between its first render and every later
/// one, and a full prepare() does not close that gap either (measured and
/// documented at the layer that owns it, dsp/tests/unit/systems/
/// seraphis_voice_test.cpp:816-827, and measured again through the whole
/// processor at ~6.5e-3 on this exact script - effects_chain_test.cpp:382-393).
/// Warming once and then re-preparing before EACH measured render puts both arms
/// on the same side of that seam, so the only remaining difference between them
/// is the gate. This helper is a verbatim local copy of
/// effects_chain_test.cpp:394-405; it is NOT hoisted into the shared fixture,
/// because the warm-up discipline it belongs to is a property of the two
/// negative-control CASES that need it (Phase 10's SC-002 and this one), not of
/// every Seraphis test.
void reprepare(Fixture& fx) {
    REQUIRE(fx.proc->setActive(false) == Steinberg::kResultOk);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = kBlock;
    setup.sampleRate = kSampleRate;

    REQUIRE(fx.proc->setupProcessing(setup) == Steinberg::kResultOk);
    REQUIRE(fx.proc->setActive(true) == Steinberg::kResultOk);
}

}  // namespace

// =============================================================================
// SC-001, FR-042 - THE PRODUCER CHANGES NO SAMPLE
// =============================================================================
// THREE RENDERS RUN, TWO ARE MEASURED. The first is a warm-up whose output is
// discarded, for the reason reprepare()'s banner states and measures.
//
// ARM B RUNS FIRST, AND THAT ORDER IS LOAD-BEARING. cloudFramePublishAttempts_
// is a lifetime counter: its only writer is the `++` at processor.cpp:3988, and
// neither setActive() nor setupProcessing() resets it (setupProcessing's Phase 11
// block at :911-916 zeroes pendingFrame_ and cloudFrameFocusVoice_ and nothing
// else). So "== 0 in arm B" is an assertion about the GATE only while no earlier
// render in this case has run with the gate open - which is exactly what
// warm-up-closed -> B-closed -> A-open buys. Reversing the order would leave the
// arm B assertion unwritable without a counter reset seam this phase does not
// have and does not need.
//
// THIS CASE MUST STAY GREEN THROUGH EVERY LATER PHASE 11 TASK. If a task makes it
// fail, that task has made the producer write something the audio path reads, and
// the fix is to remove the mutation - NEVER to narrow the comparison.
// =============================================================================
TEST_CASE("Seraphis_Phase11_OpenGate_ChangesNoSample", "[ui][phase11]") {
    Fixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->engineForTest() != nullptr);

    // The script places note traffic through the fixture's own event list - the
    // same object renderBlocks() hands the callback, cleared before every block.
    // It carries NO parameter automation at all: SC-001 is a defaults render.
    const auto script = [&fx](std::size_t b, Krate::Test::EventList&,
                              SeraphisTest::ParameterChanges&) {
        if (b == 0) {
            for (const Steinberg::int16 pitch : kEightNoteChord) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kChordVelocity, 0);
            }
        } else if (b == kNoteOffBlock) {
            for (const Steinberg::int16 pitch : kEightNoteChord) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, pitch, 0.0f, 0);
            }
        }
    };

    // --- Warm-up: NOT a measured render, gate CLOSED -------------------------
    // The warm-up runs the IDENTICAL script, so every sub-component is left in
    // the same converged state the two measured renders will start from.
    fx.proc->setCloudFrameGateForTest(false);
    fx.renderBlocks(kRenderBlocks, kBlockSamples, script);
    fx.capturedL.clear();
    fx.capturedR.clear();
    REQUIRE(fx.checkCanaries());
    // The warm-up must not have published either, or arm B's `== 0` below would
    // be measuring the warm-up rather than the gate.
    REQUIRE(fx.proc->cloudFramePublishAttemptCountForTest() == 0u);

    // --- Render B: gate CLOSED, the producer never runs ----------------------
    reprepare(fx);
    fx.proc->setCloudFrameGateForTest(false);
    fx.renderBlocks(kRenderBlocks, kBlockSamples, script);

    const std::vector<float> closedL = fx.capturedL;
    const std::vector<float> closedR = fx.capturedR;
    fx.capturedL.clear();
    fx.capturedR.clear();
    REQUIRE(closedL.size() == kRenderBlocks * kBlockSamples);
    REQUIRE(closedR.size() == closedL.size());
    REQUIRE(fx.checkCanaries());

    // ARM B'S HALF OF "both arms must differ": with the gate closed the producer
    // is skipped entirely, so not one attempt is recorded over 938 blocks.
    CHECK(fx.proc->cloudFramePublishAttemptCountForTest() == 0u);
    CHECK(fx.proc->cloudFrameSequenceForTest() == 0u);

    // --- Render A: the SAME instance, re-prepared, gate OPEN -----------------
    reprepare(fx);
    fx.proc->setCloudFrameGateForTest(true);
    fx.renderBlocks(kRenderBlocks, kBlockSamples, script);

    REQUIRE(fx.capturedL.size() == closedL.size());
    REQUIRE(fx.capturedR.size() == closedR.size());
    REQUIRE(fx.checkCanaries());

    // ARM A'S HALF: the producer really did run, on every block. Without this the
    // whole case would pass vacuously on a build whose seam had been broken to a
    // no-op, and it would pass for exactly the wrong reason.
    const std::size_t attempts = fx.proc->cloudFramePublishAttemptCountForTest();
    INFO("publish attempts (arm A) = "
         << attempts
         << ", skipped = " << fx.proc->cloudFrameSkippedBlockCountForTest());
    CHECK(attempts > 0u);
    CHECK(attempts == kRenderBlocks);
    // ...and it published a real frame rather than running an empty loop.
    CHECK(fx.proc->lastPublishedFrameForTest().partialCount > 0);

    // Non-vacuity: two silences would satisfy exact equality trivially.
    float livePeak = 0.0f;
    for (const float v : fx.capturedL) {
        livePeak = std::max(livePeak, std::fabs(v));
    }
    REQUIRE(livePeak > 0.0f);

    // THE CRITERION. Exact, and legitimate for the reason the banner states:
    // same compiled code path, same instance, one bool apart. Not a golden, not
    // a cross-build comparison.
    CHECK(maxAbsDiff(closedL, fx.capturedL) == 0.0f);
    CHECK(maxAbsDiff(closedR, fx.capturedR) == 0.0f);
}
