// ==============================================================================
// Seraphis - MIDI event dispatch + block-size invariance tests (T021)
// ==============================================================================
// TEST_CASE("Seraphis_MidiEventTranslation") covers FR-025, FR-026, FR-027,
// FR-031 and FR-034 through the two criteria they answer to:
//
//   SC-022 - MIDI translation, asserted on the ENGINE'S OWN observable surface
//            (getActiveVoiceCount(), dsp/.../seraphis_engine.h:668, and
//            getVoiceState(i), :693) rather than on a wrapper-side counter that
//            could agree with a broken dispatch.
//   SC-008 - block-size invariance, in the SECTION named
//            Seraphis_ProcessorBlockSizeInvariance.
//
// The case deliberately carries NO NaN/Inf injection, which is why this TU is
// absent from the -fno-fast-math list in ../CMakeLists.txt:63-70.
// ==============================================================================

#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include <render_fingerprint.h>

#include <pluginterfaces/vst/ivstevents.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Seraphis {

// -----------------------------------------------------------------------------
// Defined in src/processor/processor.cpp at NAMESPACE scope (deliberately not in
// an anonymous namespace), declared here because SC-022 clause 6's upper clamp
// has NO behavioural proxy: SeraphisEngine::dispatch computes
// `static_cast<float>(e.velocity) / 127.0f` (seraphis_engine.h:1137) and
// SeraphisVoice::noteOn then clamps that to [0, 1] (seraphis_voice.h:527), so a
// mapped 128 and a mapped 127 render BIT-IDENTICAL audio and produce identical
// voice states. The only way to assert "127, not 128" is to call the mapping.
// -----------------------------------------------------------------------------
[[nodiscard]] std::uint8_t mapNoteOnVelocity(float velocity) noexcept;

}  // namespace Seraphis

namespace {

using Steinberg::Vst::Event;

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 512;

/// A note-on velocity comfortably inside (0, 1] that no clamp can move.
constexpr float kNormalVelocity = 0.8f;

[[nodiscard]] Krate::DSP::SeraphisEngine& engineOf(SeraphisTest::ProcessorFixture& fx) {
    Krate::DSP::SeraphisEngine* engine = fx.proc->engineForTest();
    REQUIRE(engine != nullptr);
    return *engine;
}

/// Every slot's allocator state, so two dispatch paths can be compared as a
/// whole rather than through a single aggregate count that could coincide.
[[nodiscard]] std::vector<Krate::DSP::VoiceState> voiceStates(
    const Krate::DSP::SeraphisEngine& engine) {
    std::vector<Krate::DSP::VoiceState> states;
    states.reserve(Krate::DSP::SeraphisEngine::kMaxVoices);
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        states.push_back(engine.getVoiceState(v));
    }
    return states;
}

[[nodiscard]] float maxAbs(const std::vector<float>& v) {
    float peak = 0.0f;
    for (const float s : v) {
        peak = std::max(peak, std::abs(s));
    }
    return peak;
}

struct DiffResult {
    float worst = 0.0f;
    std::size_t index = 0;
};

[[nodiscard]] DiffResult maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    DiffResult out;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const float d = std::abs(a[i] - b[i]);
        if (d > out.worst) {
            out.worst = d;
            out.index = i;
        }
    }
    return out;
}

// -----------------------------------------------------------------------------
// SC-008 script
// -----------------------------------------------------------------------------

struct ScriptEvent {
    std::size_t position;  ///< ABSOLUTE sample index within the 4 s render
    bool noteOn;
    Steinberg::int16 pitch;
    float velocity;
};

/// 4 s at 48 kHz. Long enough for the atmosphere tap and the 1024-sample
/// spectral-diffusion latency to be fully inside the compared region.
constexpr std::size_t kTotalSamples = 192000;

/// The reference partition. It is NOT compared with itself - that comparison
/// carries no information.
constexpr std::size_t kReferenceBlock = 512;

/// {1, 7, 64, 65, 512, 2048, 4096}. 65 puts a partition boundary strictly
/// INSIDE a 64-sample control chunk (SeraphisEngine::kControlChunkSamples = 64,
/// seraphis_engine.h:132) and 4096 is the only partition above
/// kMaxBlockSamples = 2048 (:134), i.e. the only one that enters FR-026's
/// sub-division branch. Both facts are ASSERTED below, not assumed.
constexpr std::size_t kPartitions[] = {1, 7, 64, 65, 512, 2048, 4096};

/// Every position is a non-multiple of every partition above 1, so no event
/// ever lands on a block boundary in any partition (asserted below).
///
/// NOTE (SC-008, deliberate): the script carries NO parameter automation. VST3
/// delivers parameter queues per HOST BLOCK, so a re-partitioned automation lane
/// is a different lane by construction and any comparison across partitions
/// would fail a correct implementation.
constexpr ScriptEvent kScript[] = {
    {.position = 1000, .noteOn = true, .pitch = 60, .velocity = 0.80f},
    {.position = 13337, .noteOn = true, .pitch = 64, .velocity = 0.60f},
    {.position = 40001, .noteOn = true, .pitch = 67, .velocity = 0.90f},
    {.position = 71111, .noteOn = false, .pitch = 60, .velocity = 0.0f},
    {.position = 99999, .noteOn = true, .pitch = 72, .velocity = 0.50f},
    {.position = 133333, .noteOn = false, .pitch = 64, .velocity = 0.0f},
    {.position = 160007, .noteOn = false, .pitch = 67, .velocity = 0.0f},
};

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

/// One complete 4 s render through Processor::process() at `blockSize`.
///
/// The fixture (and with it the ~33 MB of per-voice capture rings the engine
/// allocates at prepare) is destroyed when this returns, so at most one engine
/// is alive at a time even though seven partitions are rendered.
[[nodiscard]] Render renderAtBlockSize(std::size_t blockSize) {
    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, static_cast<Steinberg::int32>(blockSize)) ==
            Steinberg::kResultOk);

    // ceil: a partition that does not divide kTotalSamples renders one extra
    // block, and the comparison then truncates to kTotalSamples. The render is
    // causal, so the trailing partial block cannot influence what is compared.
    const std::size_t numBlocks = (kTotalSamples + blockSize - 1u) / blockSize;

    fx.renderBlocks(numBlocks, blockSize,
                    [blockSize](std::size_t b, Krate::Test::EventList& events,
                                SeraphisTest::ParameterChanges& /*params*/) {
                        const std::size_t blockStart = b * blockSize;
                        for (const ScriptEvent& e : kScript) {
                            if (e.position < blockStart || e.position >= blockStart + blockSize) {
                                continue;
                            }
                            const auto offset =
                                static_cast<Steinberg::int32>(e.position - blockStart);
                            if (e.noteOn) {
                                events.addNoteOn(e.pitch, e.velocity, offset);
                            } else {
                                events.addNoteOff(e.pitch, offset);
                            }
                        }
                    });

    REQUIRE(fx.capturedL.size() >= kTotalSamples);
    REQUIRE(fx.capturedR.size() >= kTotalSamples);
    REQUIRE(fx.checkCanaries());

    Render out;
    out.left.assign(fx.capturedL.begin(),
                    fx.capturedL.begin() + static_cast<std::ptrdiff_t>(kTotalSamples));
    out.right.assign(fx.capturedR.begin(),
                     fx.capturedR.begin() + static_cast<std::ptrdiff_t>(kTotalSamples));
    return out;
}

}  // namespace

TEST_CASE("Seraphis_MidiEventTranslation", "[seraphis][midi]") {

    // -------------------------------------------------------------------------
    // SC-022 clause 1
    // -------------------------------------------------------------------------
    SECTION("clause 1: a NoteOn with velocity > 0 allocates EXACTLY one voice") {
        SeraphisTest::ProcessorFixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        Krate::DSP::SeraphisEngine& engine = engineOf(fx);
        REQUIRE(engine.getActiveVoiceCount() == std::size_t{0});

        fx.pushEvent(Event::kNoteOnEvent, 60, kNormalVelocity, 0);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});
        REQUIRE(fx.checkCanaries());
    }

    // -------------------------------------------------------------------------
    // SC-022 clause 2 - the WRAPPER's velocity-0 path, not the engine's own
    // guard: FR-031 writes the branch explicitly so this comparison tests
    // Processor::process()'s translation table.
    // -------------------------------------------------------------------------
    SECTION("clause 2: a velocity-0 NoteOn releases IDENTICALLY to a NoteOff") {
        const auto play = [](bool releaseViaVelocityZeroNoteOn) {
            SeraphisTest::ProcessorFixture fx;
            REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
            Krate::DSP::SeraphisEngine& engine = engineOf(fx);

            fx.pushEvent(Event::kNoteOnEvent, 60, kNormalVelocity, 0);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});

            if (releaseViaVelocityZeroNoteOn) {
                fx.pushEvent(Event::kNoteOnEvent, 60, 0.0f, 0);
            } else {
                fx.pushEvent(Event::kNoteOffEvent, 60, 0.0f, 0);
            }
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            return voiceStates(engine);
        };

        const std::vector<Krate::DSP::VoiceState> viaVelocityZero = play(true);
        const std::vector<Krate::DSP::VoiceState> viaNoteOff = play(false);

        // Non-vacuity: the release must actually have moved slot 0 off Active,
        // or "identical" would just be "both untouched".
        REQUIRE(viaNoteOff[0] == Krate::DSP::VoiceState::Releasing);
        REQUIRE(viaVelocityZero == viaNoteOff);
    }

    // -------------------------------------------------------------------------
    // SC-022 clause 3
    // -------------------------------------------------------------------------
    SECTION("clause 3: a NoteOff for a note never played is a no-op") {
        SeraphisTest::ProcessorFixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        Krate::DSP::SeraphisEngine& engine = engineOf(fx);

        const std::vector<Krate::DSP::VoiceState> before = voiceStates(engine);

        fx.pushEvent(Event::kNoteOffEvent, 42, 0.0f, 0);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);  // and does not crash

        const std::vector<Krate::DSP::VoiceState> after = voiceStates(engine);
        REQUIRE(after == before);
        REQUIRE(engine.getActiveVoiceCount() == std::size_t{0});
        REQUIRE(fx.checkCanaries());
    }

    // -------------------------------------------------------------------------
    // SC-022 clause 4 - clampOffset (plan 3.2). Both directions are legal
    // inputs from a malformed host and neither may produce a negative slice.
    // -------------------------------------------------------------------------
    SECTION("clause 4: out-of-range sampleOffsets clamp into [0, numSamples]") {
        SECTION("a negative offset clamps to 0 and the note is dispatched") {
            SeraphisTest::ProcessorFixture fx;
            REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
            Krate::DSP::SeraphisEngine& engine = engineOf(fx);

            fx.pushEvent(Event::kNoteOnEvent, 60, kNormalVelocity, -5);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

            REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});
            // A negative slice length would have wrapped std::size_t and
            // written far outside the buffer; the guard words prove it did not.
            REQUIRE(fx.checkCanaries());
        }

        SECTION("a past-the-end offset clamps to numSamples and renders cleanly") {
            SeraphisTest::ProcessorFixture fx;
            REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
            Krate::DSP::SeraphisEngine& engine = engineOf(fx);

            fx.pushEvent(Event::kNoteOnEvent, 60, kNormalVelocity, kBlock + 10);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            REQUIRE(fx.checkCanaries());

            // DOCUMENTED CONSEQUENCE, asserted rather than left ambiguous:
            // clamping into [0, numSamples] puts the event at `numSamples`,
            // which is one PAST the last renderable sample, so the block ends
            // before it comes due and it is never dispatched. VST3 requires
            // sampleOffset < numSamples, so no conforming host can reach this;
            // what the clause guarantees is that the malformed offset is
            // absorbed safely, not that it is honoured.
            REQUIRE(engine.getActiveVoiceCount() == std::size_t{0});

            // ...and the processor is not wedged by it: the next block works.
            fx.pushEvent(Event::kNoteOnEvent, 60, kNormalVelocity, 0);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});
        }
    }

    // -------------------------------------------------------------------------
    // SC-022 clause 5 - the dispatch loop is a WHILE, not an IF.
    //
    // With an `if`, the first pass over the loop breaks on `at > cursor`, the
    // slice ends at 100, and on the next pass only ONE of the two events at
    // offset 100 is dispatched: the second resolves sliceEnd back to the cursor
    // and is stranded, so exactly one voice is allocated. == 2 is the detector.
    // -------------------------------------------------------------------------
    SECTION("clause 5: two events at the SAME offset are both dispatched") {
        SeraphisTest::ProcessorFixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
        Krate::DSP::SeraphisEngine& engine = engineOf(fx);

        fx.pushEvent(Event::kNoteOnEvent, 60, kNormalVelocity, 100);
        fx.pushEvent(Event::kNoteOnEvent, 64, kNormalVelocity, 100);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        REQUIRE(engine.getActiveVoiceCount() == std::size_t{2});
        REQUIRE(fx.checkCanaries());
    }

    // -------------------------------------------------------------------------
    // SC-022 clause 6 - the velocity floor (plan 1.3 C-3).
    // -------------------------------------------------------------------------
    SECTION("clause 6: the velocity floor of 1 - a tiny NoteOn allocates, never releases") {
        SECTION("velocity strictly inside (0, 1/127) allocates a voice") {
            SeraphisTest::ProcessorFixture fx;
            REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
            Krate::DSP::SeraphisEngine& engine = engineOf(fx);

            // 0.003f * 127 = 0.381: a TRUNCATING uint8(velocity * 127) yields 0,
            // and SeraphisEngine::noteOn maps velocity 0 to noteOff
            // (seraphis_engine.h:374-377) - a note-on that RELEASES. The
            // rounding + floor of 1 is what this assertion detects.
            fx.pushEvent(Event::kNoteOnEvent, 60, 0.003f, 0);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

            REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});
        }

        SECTION("velocity 1/127 allocates a voice") {
            SeraphisTest::ProcessorFixture fx;
            REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
            Krate::DSP::SeraphisEngine& engine = engineOf(fx);

            fx.pushEvent(Event::kNoteOnEvent, 60, 1.0f / 127.0f, 0);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

            REQUIRE(engine.getActiveVoiceCount() == std::size_t{1});
        }

        SECTION("the mapping itself: 1/127 -> 1, 1.0 -> 127 (not 128), (0,1/127) -> 1") {
            REQUIRE(Seraphis::mapNoteOnVelocity(0.003f) == std::uint8_t{1});
            REQUIRE(Seraphis::mapNoteOnVelocity(1.0f / 127.0f) == std::uint8_t{1});
            // The UPPER clamp. Without it the mapping yields 128
            // (1.0 * 127 + 0.5 = 127.5), which is out of MIDI range.
            REQUIRE(Seraphis::mapNoteOnVelocity(1.0f) == std::uint8_t{127});
            // Sanity anchors either side of the midpoint.
            REQUIRE(Seraphis::mapNoteOnVelocity(100.0f / 127.0f) == std::uint8_t{100});
            REQUIRE(Seraphis::mapNoteOnVelocity(0.5f) == std::uint8_t{64});
        }
    }

    // -------------------------------------------------------------------------
    // SC-008 - block-size invariance.
    // -------------------------------------------------------------------------
    SECTION("Seraphis_ProcessorBlockSizeInvariance") {
        // REQUIRED COVERAGE, ASSERTED NOT ASSUMED.
        REQUIRE(65u % Krate::DSP::SeraphisEngine::kControlChunkSamples != 0u);
        REQUIRE(std::find(std::begin(kPartitions), std::end(kPartitions), std::size_t{65}) !=
                std::end(kPartitions));
        // 4096 is the ONLY partition above kMaxBlockSamples, i.e. the only one
        // that exercises FR-026's sub-division branch.
        REQUIRE(4096u > Krate::DSP::SeraphisEngine::kMaxBlockSamples);
        REQUIRE(std::find(std::begin(kPartitions), std::end(kPartitions), std::size_t{4096}) !=
                std::end(kPartitions));
        REQUIRE(std::find(std::begin(kPartitions), std::end(kPartitions), kReferenceBlock) !=
                std::end(kPartitions));

        // Events must never coincide with a block boundary in ANY partition, or
        // the sub-division path would go unexercised for that partition.
        for (const ScriptEvent& e : kScript) {
            for (const std::size_t block : kPartitions) {
                if (block == 1u) {
                    continue;  // every index is a multiple of 1
                }
                CAPTURE(e.position, block);
                REQUIRE(e.position % block != 0u);
            }
        }

        const Render reference = renderAtBlockSize(kReferenceBlock);
        REQUIRE(reference.left.size() == kTotalSamples);
        REQUIRE(reference.right.size() == kTotalSamples);

        // Non-vacuity: two silences are trivially invariant.
        const float referencePeak = std::max(maxAbs(reference.left), maxAbs(reference.right));
        CAPTURE(referencePeak);
        REQUIRE(referencePeak > 1.0e-4f);

        for (const std::size_t block : kPartitions) {
            if (block == kReferenceBlock) {
                continue;  // the reference is not compared with itself
            }
            CAPTURE(block);
            const Render got = renderAtBlockSize(block);
            REQUIRE(got.left.size() == kTotalSamples);

            // PRIMARY GATE: max absolute per-sample difference over ALL samples,
            // per channel. This is the shape Phase 7 shipped
            // (specs/seraphis-phase7-voice-engine/compliance.md:180).
            const DiffResult dl = maxAbsDiff(got.left, reference.left);
            const DiffResult dr = maxAbsDiff(got.right, reference.right);
            CAPTURE(dl.worst, dl.index, dr.worst, dr.index);
            REQUIRE(dl.worst <= 1.0e-5f);
            REQUIRE(dr.worst <= 1.0e-5f);

            // SECONDARY, WARN-ONLY - it MUST NOT gate. compareFingerprints
            // samples only 32 checkpoints (tests/test_helpers/
            // render_fingerprint.h:46) so it can miss a localised divergence,
            // and its kMetricTolerance = 1e-5 relative bound (:52) was measured
            // for the cross-toolchain spread of the SAME computation, not of a
            // re-partitioned one.
            const auto aggregate = Krate::DSP::TestUtils::compareFingerprints(
                Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(got.left)),
                Krate::DSP::TestUtils::fingerprintRender(std::span<const float>(reference.left)));
            if (!aggregate.withinTolerance()) {
                WARN("SC-008 secondary (non-gating) fingerprint drift at block "
                     << block << ": worstMetricRelativeError="
                     << aggregate.worstMetricRelativeError
                     << " worstSampleError=" << aggregate.worstSampleError << " "
                     << aggregate.detail);
            }
        }
    }
}
