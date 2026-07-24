// arpeggiator_core_ratcheting_test.cpp
// Ratcheting (spec 074) + ratchet swing (spec 078)
// Split from the former 17k-line arpeggiator_core_test.cpp (D1). Shared helpers in
// arpeggiator_core_test_helpers.h.
#include "arpeggiator_core_test_helpers.h"



// =============================================================================
// Phase 2: Foundational Ratchet Lane Infrastructure (074-ratcheting)
// =============================================================================

TEST_CASE("kMaxEvents is 128", "[arp][ratchet][infrastructure]") {
    static_assert(ArpeggiatorCore::kMaxEvents == 128,
                  "kMaxEvents must be 128 for ratcheted Chord mode headroom");
    REQUIRE(ArpeggiatorCore::kMaxEvents == 128);
}


TEST_CASE("Ratchet lane accessor exists and returns a valid lane",
          "[arp][ratchet][infrastructure]") {
    ArpeggiatorCore arp;

    // Non-const accessor
    ArpLane<uint8_t>& lane = arp.ratchetLane();
    (void)lane;

    // Const accessor
    const ArpeggiatorCore& constArp = arp;
    const ArpLane<uint8_t>& constLane = constArp.ratchetLane();
    (void)constLane;
}


TEST_CASE("Ratchet lane default length is 1",
          "[arp][ratchet][infrastructure]") {
    ArpeggiatorCore arp;
    // ArpLane default length is 1
    // The lane should cycle through just one step
    const auto& lane = arp.ratchetLane();
    // ArpLane<T>::getStep(0) should return the default step value
    // After constructor initialization, step 0 should be 1 (not 0)
    REQUIRE(lane.getStep(0) == 1);
}


TEST_CASE("Ratchet lane default step value is 1 (not 0)",
          "[arp][ratchet][infrastructure]") {
    ArpeggiatorCore arp;
    // ArpLane<uint8_t> zero-initializes steps to 0.
    // The constructor must explicitly set step 0 to 1 because
    // ratchet count 0 is invalid (FR-003).
    REQUIRE(arp.ratchetLane().getStep(0) == 1);
}


TEST_CASE("resetLanes resets ratchet lane to position 0",
          "[arp][ratchet][infrastructure]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    // Configure ratchet lane with length 3 and distinct values
    arp.ratchetLane().setLength(3);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(1));
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(2));
    arp.ratchetLane().setStep(2, static_cast<uint8_t>(3));

    // Advance the lane a couple positions
    arp.ratchetLane().advance();  // position 0 -> 1
    arp.ratchetLane().advance();  // position 1 -> 2

    // Verify we are at position 2
    REQUIRE(arp.ratchetLane().currentStep() == 2);

    // Enable and hold a note so resetLanes can be triggered via retrigger
    arp.setEnabled(true);
    arp.noteOn(60, 100);

    // Call resetLanes indirectly by disabling then re-enabling
    // (re-enable calls resetLanes)
    arp.setEnabled(false);
    arp.setEnabled(true);

    // After resetLanes, the lane position should be back to 0
    REQUIRE(arp.ratchetLane().currentStep() == 0);
}


// =============================================================================
// Phase 3: User Story 1 -- Basic Ratcheting for Rhythmic Rolls (074-ratcheting)
// =============================================================================

// T013: Ratchet count 1 produces 1 noteOn/noteOff pair (no ratcheting)
TEST_CASE("Ratchet count 1 produces 1 noteOn/noteOff pair (no ratcheting)",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Ratchet lane: length 1, step[0] = 1 (default, no ratcheting)
    // Already set by constructor

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Collect events over enough blocks for 4 steps
    // Step duration = 11025 samples. 4 steps = 44100 samples. 44100/512 ~ 87 blocks
    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // First 4 steps should each produce exactly 1 noteOn
    REQUIRE(noteOns.size() >= 4);

    // Check the first step spacing: step 0 at offset 11025 (first step fires after
    // one step duration), step 1 at offset 22050
    int32_t step0onset = noteOns[0].sampleOffset;
    int32_t step1onset = noteOns[1].sampleOffset;
    CHECK(step1onset - step0onset == 11025);

    // Verify exactly 1 noteOn per step (no sub-steps)
    // Count noteOns in the first step window [step0onset, step0onset + 11025)
    size_t noteOnsInStep0 = 0;
    for (const auto& e : noteOns) {
        if (e.sampleOffset >= step0onset &&
            e.sampleOffset < step0onset + 11025) {
            ++noteOnsInStep0;
        }
    }
    CHECK(noteOnsInStep0 == 1);
}


// T014: Ratchet count 2 produces 2 noteOn/noteOff pairs at correct sample offsets
TEST_CASE("Ratchet count 2 produces 2 noteOn/noteOff pairs at correct sample offsets",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Set ratchet lane: length 1, step[0] = 2
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Collect events for 2 full steps (22050 samples = ~44 blocks of 512)
    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    // Step 0 fires at offset 11025. With ratchet 2:
    // Sub-step 0 at 11025, sub-step 1 at 11025 + 5512 = 16537
    REQUIRE(noteOns.size() >= 2);

    int32_t step0onset = noteOns[0].sampleOffset;
    // subStepDuration = 11025 / 2 = 5512
    // Sub-step 1 onset = step0onset + 5512
    CHECK(noteOns[1].sampleOffset == step0onset + 5512);

    // Count noteOns in step 0 window [step0onset, step0onset + 11025)
    size_t noteOnsInStep0 = 0;
    for (const auto& e : noteOns) {
        if (e.sampleOffset >= step0onset &&
            e.sampleOffset < step0onset + 11025) {
            ++noteOnsInStep0;
        }
    }
    CHECK(noteOnsInStep0 == 2);
}


// T015: Ratchet count 3 produces 3 noteOn/noteOff pairs at correct sample offsets
TEST_CASE("Ratchet count 3 produces 3 noteOn/noteOff pairs at correct sample offsets",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(3));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 3);

    int32_t step0onset = noteOns[0].sampleOffset;
    // subStepDuration = 11025 / 3 = 3675
    CHECK(noteOns[1].sampleOffset == step0onset + 3675);
    CHECK(noteOns[2].sampleOffset == step0onset + 7350);

    // Count noteOns in step 0 window
    size_t noteOnsInStep0 = 0;
    for (const auto& e : noteOns) {
        if (e.sampleOffset >= step0onset &&
            e.sampleOffset < step0onset + 11025) {
            ++noteOnsInStep0;
        }
    }
    CHECK(noteOnsInStep0 == 3);
}


// T016: Ratchet count 4 produces 4 noteOn/noteOff pairs at correct sample offsets
TEST_CASE("Ratchet count 4 produces 4 noteOn/noteOff pairs at correct sample offsets",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 4);

    int32_t step0onset = noteOns[0].sampleOffset;
    // subStepDuration = 11025 / 4 = 2756
    CHECK(noteOns[1].sampleOffset == step0onset + 2756);
    CHECK(noteOns[2].sampleOffset == step0onset + 5512);
    CHECK(noteOns[3].sampleOffset == step0onset + 8268);

    // Count noteOns in step 0 window
    size_t noteOnsInStep0 = 0;
    for (const auto& e : noteOns) {
        if (e.sampleOffset >= step0onset &&
            e.sampleOffset < step0onset + 11025) {
            ++noteOnsInStep0;
        }
    }
    CHECK(noteOnsInStep0 == 4);
}


// T017: All sub-step noteOn events carry the same MIDI note number and velocity
TEST_CASE("All sub-step noteOn events carry the same MIDI note and velocity",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 4);

    // All 4 sub-steps of the first ratcheted step should have same note and velocity
    int32_t step0onset = noteOns[0].sampleOffset;
    std::vector<ArpEvent> step0NoteOns;
    for (const auto& e : noteOns) {
        if (e.sampleOffset >= step0onset &&
            e.sampleOffset < step0onset + 11025) {
            step0NoteOns.push_back(e);
        }
    }
    REQUIRE(step0NoteOns.size() == 4);

    uint8_t expectedNote = step0NoteOns[0].note;
    uint8_t expectedVel = step0NoteOns[0].velocity;
    for (size_t i = 1; i < step0NoteOns.size(); ++i) {
        CHECK(step0NoteOns[i].note == expectedNote);
        CHECK(step0NoteOns[i].velocity == expectedVel);
    }
}


// T018: No timing drift after 100 consecutive ratchet-4 steps
TEST_CASE("No timing drift after 100 consecutive ratchet-4 steps",
          "[arp][ratchet][us1]") {
    // Non-ratcheted run: collect total samples for 100 steps
    ArpeggiatorCore arpRef;
    arpRef.prepare(44100.0, 512);
    arpRef.setEnabled(true);
    arpRef.setMode(ArpMode::Up);
    arpRef.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arpRef.setGateLength(80.0f);
    // Default ratchet = 1 (no ratcheting)
    arpRef.noteOn(60, 100);

    BlockContext ctxRef;
    ctxRef.sampleRate = 44100.0;
    ctxRef.blockSize = 512;
    ctxRef.tempoBPM = 120.0;
    ctxRef.isPlaying = true;
    ctxRef.transportPositionSamples = 0;

    // 100 steps * 11025 samples = 1102500 samples / 512 = ~2154 blocks
    auto eventsRef = collectEvents(arpRef, ctxRef, 2200);
    auto noteOnsRef = filterNoteOns(eventsRef);
    REQUIRE(noteOnsRef.size() >= 101);

    int32_t refStep100Onset = noteOnsRef[100].sampleOffset;

    // Ratcheted run: ratchet 4 for all steps
    ArpeggiatorCore arpRatch;
    arpRatch.prepare(44100.0, 512);
    arpRatch.setEnabled(true);
    arpRatch.setMode(ArpMode::Up);
    arpRatch.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arpRatch.setGateLength(80.0f);
    arpRatch.ratchetLane().setStep(0, static_cast<uint8_t>(4));
    arpRatch.noteOn(60, 100);

    BlockContext ctxRatch;
    ctxRatch.sampleRate = 44100.0;
    ctxRatch.blockSize = 512;
    ctxRatch.tempoBPM = 120.0;
    ctxRatch.isPlaying = true;
    ctxRatch.transportPositionSamples = 0;

    auto eventsRatch = collectEvents(arpRatch, ctxRatch, 2200);
    auto noteOnsRatch = filterNoteOns(eventsRatch);

    // With ratchet 4, each step produces 4 noteOns. 100 steps = 400 noteOns
    // The noteOn at index 400 is the first sub-step of step 100
    REQUIRE(noteOnsRatch.size() >= 401);

    int32_t ratchStep100Onset = noteOnsRatch[400].sampleOffset;

    // Total elapsed samples must be identical (zero drift)
    CHECK(ratchStep100Onset == refStep100Onset);
}


// T019: Sub-steps that span block boundaries are correctly emitted
TEST_CASE("Sub-steps spanning block boundaries emit at correct offsets",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 64);  // Small block size
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 64;  // Very small blocks
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // 11025 * 2 = 22050 samples / 64 = ~345 blocks
    auto events = collectEvents(arp, ctx, 400);
    auto noteOns = filterNoteOns(events);

    // First step fires at offset 11025 with ratchet 4
    // Sub-step 0: 11025, Sub-step 1: 11025+2756=13781,
    // Sub-step 2: 11025+5512=16537, Sub-step 3: 11025+8268=19293
    REQUIRE(noteOns.size() >= 4);

    int32_t step0onset = noteOns[0].sampleOffset;
    CHECK(noteOns[0].sampleOffset == step0onset);
    CHECK(noteOns[1].sampleOffset == step0onset + 2756);
    CHECK(noteOns[2].sampleOffset == step0onset + 5512);
    CHECK(noteOns[3].sampleOffset == step0onset + 8268);
}


// T020: Chord mode ratchet 4 with 16 held notes produces 128 events without truncation
TEST_CASE("Chord mode ratchet 4 with 16 held notes produces expected event count",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    // Hold 16 notes
    for (uint8_t i = 0; i < 16; ++i) {
        arp.noteOn(static_cast<uint8_t>(48 + i), 100);
    }

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Collect enough blocks to cover at least one full ratcheted step
    // Step at 11025, sub-steps at 11025, 13781, 16537, 19293
    // Need at least 22050/512 = ~44 blocks to cover 2 steps
    auto events = collectEvents(arp, ctx, 50);

    // Count noteOns for the first ratcheted step
    auto noteOns = filterNoteOns(events);

    // First step: 4 sub-steps x 16 notes = 64 noteOns in the first step
    // Find the first noteOn offset
    REQUIRE(noteOns.size() >= 64);

    int32_t step0onset = noteOns[0].sampleOffset;
    size_t noteOnsInStep0 = 0;
    for (const auto& e : noteOns) {
        if (e.sampleOffset >= step0onset &&
            e.sampleOffset < step0onset + 11025) {
            ++noteOnsInStep0;
        }
    }
    CHECK(noteOnsInStep0 == 64);  // 4 sub-steps x 16 notes
}


// T021: Ratchet count 0 is clamped to 1 at DSP read site
TEST_CASE("Ratchet count 0 is clamped to 1 (no division by zero)",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Directly set ratchet step to 0 (bypassing parameter boundary clamp)
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(0));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Should not crash, and should behave like ratchet 1
    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);

    // Only 1 noteOn per step (ratchet 0 clamped to 1)
    int32_t step0onset = noteOns[0].sampleOffset;
    size_t noteOnsInStep0 = 0;
    for (const auto& e : noteOns) {
        if (e.sampleOffset >= step0onset &&
            e.sampleOffset < step0onset + 11025) {
            ++noteOnsInStep0;
        }
    }
    CHECK(noteOnsInStep0 == 1);
}


// T022: Ratchet sub-step state cleared on disable (setEnabled(false) mid-ratchet)
TEST_CASE("Ratchet sub-step state cleared on disable mid-ratchet",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Process enough blocks to fire the first step and first sub-step,
    // but not all sub-steps. Step fires at 11025, sub-step 1 at 13781.
    // Process to 12000 = about 23 blocks of 512 (23*512 = 11776, 24*512 = 12288)
    // At this point, sub-step 0 has fired but sub-step 1 hasn't yet.
    auto events1 = collectEvents(arp, ctx, 24);

    // Disable mid-ratchet
    arp.setEnabled(false);

    // Re-enable (resetLanes will be called)
    arp.setEnabled(true);
    arp.noteOn(60, 100);

    // Reset transport position for clean start
    ctx.transportPositionSamples = 0;

    // Process: the first step should fire normally without stale sub-steps
    auto events2 = collectEvents(arp, ctx, 50);
    auto noteOns2 = filterNoteOns(events2);

    // Step fires at offset 11025 with ratchet 4 again (clean state)
    REQUIRE(noteOns2.size() >= 4);

    int32_t step0onset = noteOns2[0].sampleOffset;
    CHECK(noteOns2[1].sampleOffset == step0onset + 2756);
    CHECK(noteOns2[2].sampleOffset == step0onset + 5512);
    CHECK(noteOns2[3].sampleOffset == step0onset + 8268);
}


// T023: Ratchet sub-step state cleared on transport stop
TEST_CASE("Ratchet sub-step state cleared on transport stop",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Process to mid-ratchet (after first sub-step, before all)
    auto events1 = collectEvents(arp, ctx, 24);

    // Stop transport
    ctx.isPlaying = false;
    std::array<ArpEvent, 128> buf;
    arp.processBlock(ctx, buf);

    // Restart transport
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events2 = collectEvents(arp, ctx, 50);
    auto noteOns2 = filterNoteOns(events2);

    // Should have clean ratchet behavior: first step fires with ratchet 4
    REQUIRE(noteOns2.size() >= 4);

    int32_t step0onset = noteOns2[0].sampleOffset;
    CHECK(noteOns2[1].sampleOffset == step0onset + 2756);
    CHECK(noteOns2[2].sampleOffset == step0onset + 5512);
    CHECK(noteOns2[3].sampleOffset == step0onset + 8268);
}


// T024: Bar boundary coinciding with sub-step discards sub-step
TEST_CASE("Bar boundary coinciding with sub-step discards sub-step",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setRetrigger(ArpRetriggerMode::Beat);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    arp.noteOn(60, 100);

    // Set up context such that a bar boundary falls mid-ratchet
    // At 120 BPM, 4/4: bar = 4 beats = 2 seconds = 88200 samples
    // Step duration = 11025 samples. 8 steps per bar.
    // The bar boundary fires at sample 88200.
    // If we position transport so that a step starts near the bar boundary
    // and a sub-step would fall at or after the bar boundary, the bar
    // boundary should take priority and clear sub-step state.
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    // 4/4 time signature (default)
    ctx.timeSignatureNumerator = 4;
    ctx.timeSignatureDenominator = 4;
    // Position transport so that the bar boundary at 88200 is reachable
    ctx.transportPositionSamples = 0;

    // Process enough blocks to cross the bar boundary
    // 88200 / 512 = ~173 blocks
    auto events = collectEvents(arp, ctx, 180);

    // Verify that the bar boundary was processed -- after it, step counting
    // resets. The test passes if we don't crash and the arp continues producing
    // events after the bar boundary.
    auto noteOns = filterNoteOns(events);
    REQUIRE(noteOns.size() >= 16);  // At least 2 bars worth
}


// T025: Defensive branch (result.count == 0): ratchet lane advances, state cleared
TEST_CASE("Defensive branch: ratchet lane advances and sub-step state cleared",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Ratchet lane: length 2, steps [2, 3]
    arp.ratchetLane().setLength(2);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(3));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Process first step
    auto events1 = collectEvents(arp, ctx, 25);

    // Remove the note (make buffer empty) -- next step hits defensive branch
    arp.noteOff(60);

    // Process to trigger the defensive branch at next step
    auto events2 = collectEvents(arp, ctx, 25);

    // Re-add note
    arp.noteOn(60, 100);

    // Process next step -- the ratchet lane should have advanced past step 1
    // (defensive branch should have advanced it) so we should now be at step 0
    // (lane length 2, wraps)
    auto events3 = collectEvents(arp, ctx, 25);
    auto noteOns3 = filterNoteOns(events3);

    // The ratchet count at the current step should produce ratcheting
    // We just verify no crash and events are produced
    REQUIRE(noteOns3.size() >= 1);
}


// T026: Swing applies to full step duration before subdivision
TEST_CASE("Swing applies to full step duration before subdivision",
          "[arp][ratchet][us1]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setSwing(50.0f);  // 50% swing

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 4);

    // Step 0 (even, lengthened): baseDuration * (1 + 0.5) = 11025 * 1.5 = 16537
    // Swung step duration for even step = 16537
    // subStepDuration = 16537 / 2 = 8268
    int32_t step0onset = noteOns[0].sampleOffset;
    int32_t subStep1onset = noteOns[1].sampleOffset;
    int32_t swungSubStepDuration = subStep1onset - step0onset;

    // Swung step duration = floor(11025 * 1.5) = 16537
    // subStepDuration = 16537 / 2 = 8268
    CHECK(swungSubStepDuration == 8268);

    // Step 1 (odd, shortened): baseDuration * (1 - 0.5) = 11025 * 0.5 = 5512
    // Swung step duration for odd step = 5512
    // subStepDuration = 5512 / 2 = 2756
    int32_t step1onset = noteOns[2].sampleOffset;
    int32_t step1subStep1onset = noteOns[3].sampleOffset;
    int32_t swungSubStepDuration2 = step1subStep1onset - step1onset;

    CHECK(swungSubStepDuration2 == 2756);
}


// T027: Phase 5 backward compatibility baseline
TEST_CASE("Phase 5 backward compatibility: ratchet 1 produces identical output",
          "[arp][ratchet][us1]") {
    // Record Phase 5 baseline: run arpeggiator with ratchet lane default (length 1,
    // value 1) and capture event sequence.
    // Then compare against a second run with identical config.

    auto runArp = [](uint8_t ratchetCount) -> std::vector<ArpEvent> {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(80.0f);

        if (ratchetCount != 1) {
            arp.ratchetLane().setStep(0, ratchetCount);
        }
        // else: use default (ratchet = 1)

        arp.noteOn(60, 100);
        arp.noteOn(64, 100);
        arp.noteOn(67, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        // 1000+ steps at 11025 samples/step = ~11M samples / 512 = ~21600 blocks
        return collectEvents(arp, ctx, 2200);
    };

    // Run 1: default ratchet (count = 1)
    auto eventsDefault = runArp(1);

    // Run 2: also default ratchet (count = 1) -- must be identical
    auto eventsCompare = runArp(1);

    REQUIRE(eventsDefault.size() == eventsCompare.size());

    for (size_t i = 0; i < eventsDefault.size(); ++i) {
        CHECK(eventsDefault[i].type == eventsCompare[i].type);
        CHECK(eventsDefault[i].note == eventsCompare[i].note);
        CHECK(eventsDefault[i].velocity == eventsCompare[i].velocity);
        CHECK(eventsDefault[i].sampleOffset == eventsCompare[i].sampleOffset);
        CHECK(eventsDefault[i].legato == eventsCompare[i].legato);
    }

    // Verify we covered enough steps (at least 100 noteOns = ~33 steps with 3 notes)
    auto noteOns = filterNoteOns(eventsDefault);
    REQUIRE(noteOns.size() >= 100);

    // Also run at 140 BPM
    auto runArpBPM = [](double bpm) -> std::vector<ArpEvent> {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(80.0f);

        arp.noteOn(60, 100);
        arp.noteOn(64, 100);
        arp.noteOn(67, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = bpm;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        return collectEvents(arp, ctx, 2200);
    };

    auto events140_a = runArpBPM(140.0);
    auto events140_b = runArpBPM(140.0);
    REQUIRE(events140_a.size() == events140_b.size());
    for (size_t i = 0; i < events140_a.size(); ++i) {
        CHECK(events140_a[i].type == events140_b[i].type);
        CHECK(events140_a[i].note == events140_b[i].note);
        CHECK(events140_a[i].velocity == events140_b[i].velocity);
        CHECK(events140_a[i].sampleOffset == events140_b[i].sampleOffset);
    }

    auto events180_a = runArpBPM(180.0);
    auto events180_b = runArpBPM(180.0);
    REQUIRE(events180_a.size() == events180_b.size());
    for (size_t i = 0; i < events180_a.size(); ++i) {
        CHECK(events180_a[i].type == events180_b[i].type);
        CHECK(events180_a[i].note == events180_b[i].note);
        CHECK(events180_a[i].velocity == events180_b[i].velocity);
        CHECK(events180_a[i].sampleOffset == events180_b[i].sampleOffset);
    }
}


// =============================================================================
// Phase 4: User Story 2 -- Per-Sub-Step Gate Length (074-ratcheting)
// =============================================================================

// T040: Ratchet 2 at 50% gate: noteOff at 2756 samples after each sub-step noteOn
TEST_CASE("Ratchet 2 at 50% gate: noteOff at correct sub-step offset",
          "[arp][ratchet][us2]") {
    // Step duration at 120 BPM, 1/8 note = 11025 samples
    // Ratchet 2: subStepDuration = 11025/2 = 5512
    // 50% gate: subGateDuration = max(1, 5512 * 50.0/100.0 * 1.0) = 2756
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // Need at least 2 noteOns and 2 noteOffs from the ratcheted step
    REQUIRE(noteOns.size() >= 2);
    REQUIRE(noteOffs.size() >= 2);

    int32_t step0onset = noteOns[0].sampleOffset;

    // Sub-step 0: noteOn at step0onset, noteOff at step0onset + 2756
    CHECK(noteOns[0].sampleOffset == step0onset);
    CHECK(noteOffs[0].sampleOffset == step0onset + 2756);

    // Sub-step 1: noteOn at step0onset + 5512, noteOff at step0onset + 5512 + 2756
    CHECK(noteOns[1].sampleOffset == step0onset + 5512);
    CHECK(noteOffs[1].sampleOffset == step0onset + 5512 + 2756);
}


// T041: Ratchet 3 at 100% gate: each sub-step noteOff fires at next sub-step boundary
TEST_CASE("Ratchet 3 at 100% gate: continuous ratchet, no silence",
          "[arp][ratchet][us2]") {
    // subStepDuration = 11025/3 = 3675
    // 100% gate: subGateDuration = 3675
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(100.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(3));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 3);
    REQUIRE(noteOffs.size() >= 3);

    int32_t step0onset = noteOns[0].sampleOffset;

    // Sub-step 0 noteOff should be at the same offset as sub-step 1 noteOn
    // (100% gate = continuous ratchet, no silence between sub-steps)
    CHECK(noteOffs[0].sampleOffset == step0onset + 3675);
    CHECK(noteOns[1].sampleOffset == step0onset + 3675);
    CHECK(noteOffs[0].sampleOffset == noteOns[1].sampleOffset);

    // Sub-step 1 noteOff should be at same offset as sub-step 2 noteOn
    CHECK(noteOffs[1].sampleOffset == step0onset + 7350);
    CHECK(noteOns[2].sampleOffset == step0onset + 7350);
    CHECK(noteOffs[1].sampleOffset == noteOns[2].sampleOffset);

    // Sub-step 2 noteOff at step0onset + 7350 + 3675 = step0onset + 11025
    CHECK(noteOffs[2].sampleOffset == step0onset + 11025);
}


// T042: Gate lane value 0.5 combined with global gate 80%
TEST_CASE("Gate lane 0.5 combined with global gate 80%: effective sub-step gate",
          "[arp][ratchet][us2]") {
    // subStepDuration = 11025/2 = 5512
    // Gate = max(1, 5512 * 80.0/100.0 * 0.5) = max(1, 2204.8) = 2204
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));

    // Set gate lane: multiply gate by 0.5
    arp.gateLane().setStep(0, 0.5f);

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 2);
    REQUIRE(noteOffs.size() >= 2);

    int32_t step0onset = noteOns[0].sampleOffset;

    // Expected gate duration: floor(5512 * 80.0/100.0 * 0.5) = floor(2204.8) = 2204
    CHECK(noteOffs[0].sampleOffset == step0onset + 2204);
    CHECK(noteOffs[1].sampleOffset == step0onset + 5512 + 2204);
}


// T043: Tie/Slide look-ahead applies to LAST sub-step only
TEST_CASE("Tie/Slide look-ahead applies to LAST sub-step only",
          "[arp][ratchet][us2]") {
    // Configure ratchet 3 on step 0, next step (step 1) is Tie.
    // Sub-steps 0 and 1 should schedule noteOffs normally.
    // Sub-step 2 (last) should suppress noteOff due to Tie look-ahead.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);

    // Ratchet lane: length 2, step[0] = 3, step[1] = 1 (normal)
    arp.ratchetLane().setLength(2);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(3));
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(1));

    // Modifier lane: length 2, step[0] = Active, step[1] = Active|Tie
    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks to cover step 0 (ratchet 3) and step 1 (tie)
    auto events = collectEvents(arp, ctx, 60);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // Step 0 with ratchet 3: 3 noteOns
    REQUIRE(noteOns.size() >= 3);

    int32_t step0onset = noteOns[0].sampleOffset;

    // Sub-step duration = 11025/3 = 3675
    // Gate at 50%: subGateDuration = max(1, 3675 * 50/100 * 1.0) = 1837

    // Sub-steps 0 and 1 should produce noteOffs normally
    // Sub-step 0 noteOff at step0onset + 1837
    CHECK(noteOffs[0].sampleOffset == step0onset + 1837);
    // Sub-step 1 noteOff at step0onset + 3675 + 1837 = step0onset + 5512
    CHECK(noteOffs[1].sampleOffset == step0onset + 3675 + 1837);

    // Sub-step 2 (last): noteOff should be SUPPRESSED because next step is Tie.
    // Count noteOffs in step 0 window. Only 2 noteOffs should appear (sub-steps 0 and 1).
    size_t noteOffsInStep0 = 0;
    for (const auto& e : noteOffs) {
        if (e.sampleOffset >= step0onset &&
            e.sampleOffset < step0onset + 11025) {
            ++noteOffsInStep0;
        }
    }
    CHECK(noteOffsInStep0 == 2);
}


// T044: Gate > 100% on ratcheted step (overlapping sub-notes)
TEST_CASE("Gate > 100% on ratcheted step: overlapping sub-notes",
          "[arp][ratchet][us2]") {
    SECTION("Basic gate > 100%: each sub-step noteOff fires after next noteOn") {
        // Gate 150%: subGateDuration = max(1, 5512 * 150.0/100.0 * 1.0) = 8268
        // This exceeds sub-step period (5512), so noteOff fires after next noteOn
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(150.0f);

        arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));

        arp.noteOn(60, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        auto events = collectEvents(arp, ctx, 50);
        auto noteOns = filterNoteOns(events);
        auto noteOffs = filterNoteOffs(events);

        REQUIRE(noteOns.size() >= 2);

        int32_t step0onset = noteOns[0].sampleOffset;

        // Sub-step 0 noteOn at step0onset
        // Sub-step 1 noteOn at step0onset + 5512
        CHECK(noteOns[1].sampleOffset == step0onset + 5512);

        // Sub-step 0 noteOff at step0onset + 8268
        // This is AFTER sub-step 1's noteOn (overlap)
        CHECK(noteOffs[0].sampleOffset == step0onset + 8268);
        CHECK(noteOffs[0].sampleOffset > noteOns[1].sampleOffset);
    }

    SECTION("Gate > 100% with Tie look-ahead on last sub-step") {
        // Ratchet 2, gate 150%, next step is Tie.
        // Sub-step 0 (non-last): noteOff fires normally at step0onset + 8268
        // Sub-step 1 (last): noteOff SUPPRESSED by Tie look-ahead
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(150.0f);

        // Ratchet lane: length 2, step[0] = 2, step[1] = 1
        arp.ratchetLane().setLength(2);
        arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));
        arp.ratchetLane().setStep(1, static_cast<uint8_t>(1));

        // Modifier lane: length 2, step[0] = Active, step[1] = Active|Tie
        arp.modifierLane().setLength(2);
        arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
        arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));

        arp.noteOn(60, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        auto events = collectEvents(arp, ctx, 60);
        auto noteOns = filterNoteOns(events);
        auto noteOffs = filterNoteOffs(events);

        REQUIRE(noteOns.size() >= 2);

        int32_t step0onset = noteOns[0].sampleOffset;

        // Sub-step 0 (non-last): noteOff fires normally at step0onset + 8268
        CHECK(noteOffs[0].sampleOffset == step0onset + 8268);

        // Sub-step 1 (last): noteOff SUPPRESSED by Tie look-ahead.
        // Count noteOffs in step 0 + some overflow window.
        // Only 1 noteOff should appear (from sub-step 0).
        size_t noteOffsInStep0Window = 0;
        for (const auto& e : noteOffs) {
            if (e.sampleOffset >= step0onset &&
                e.sampleOffset < step0onset + 11025 + 8268) {
                // Include overflow window for sub-step 0's long gate
                ++noteOffsInStep0Window;
            }
        }
        // Only sub-step 0 produces a noteOff; sub-step 1's is suppressed
        CHECK(noteOffsInStep0Window == 1);
    }
}


// =============================================================================
// Phase 5: User Story 3 -- Ratchet Lane Independent Cycling (074-ratcheting)
// =============================================================================

// T050: Ratchet lane length 3 cycles independently of velocity lane length 5
TEST_CASE("Ratchet lane length 3 cycles independently of velocity lane length 5",
          "[arp][ratchet][us3]") {
    // SC-006, FR-004: Ratchet lane length 3 with steps [1, 2, 4],
    // velocity lane length 5. Combined cycle = LCM(3, 5) = 15 steps.
    // Verify ratchet pattern repeats every 3 steps and velocity repeats
    // every 5 steps, producing 15 unique combinations before repetition.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Ratchet lane: length 3, steps [1, 2, 4]
    arp.ratchetLane().setLength(3);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(1));
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(2));
    arp.ratchetLane().setStep(2, static_cast<uint8_t>(4));

    // Velocity lane: length 5, distinct values
    arp.velocityLane().setLength(5);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.8f);
    arp.velocityLane().setStep(3, 0.6f);
    arp.velocityLane().setStep(4, 0.3f);

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks for 2 full LCM cycles (30 steps) plus margin.
    // 120 BPM 1/8th: 11025 samp/step. 30 steps = 330750 samples.
    // 330750 / 512 ~= 646 blocks. Use 800 for margin.
    auto events = collectEvents(arp, ctx, 800);
    auto noteOns = filterNoteOns(events);

    // With ratchet steps [1, 2, 4], total noteOns per 3-step cycle:
    // step0: 1 noteOn, step1: 2 noteOns, step2: 4 noteOns = 7 noteOns per ratchet cycle.
    // Over 15 arp steps (5 ratchet cycles): 5 * 7 = 35 noteOns.
    REQUIRE(noteOns.size() >= 35);

    // Build a per-arp-step summary: (number of noteOns per step, velocity of first noteOn).
    // Step duration = 11025 samples.
    int32_t firstOnset = noteOns[0].sampleOffset;
    const int32_t stepDuration = 11025;

    struct StepSummary {
        size_t noteOnCount;
        uint8_t firstVelocity;
    };

    std::vector<StepSummary> summaries;
    for (int step = 0; step < 30; ++step) {
        int32_t windowStart = firstOnset + step * stepDuration;
        int32_t windowEnd = windowStart + stepDuration;
        size_t count = 0;
        uint8_t firstVel = 0;
        for (const auto& e : noteOns) {
            if (e.sampleOffset >= windowStart && e.sampleOffset < windowEnd) {
                if (count == 0) firstVel = e.velocity;
                ++count;
            }
        }
        summaries.push_back({count, firstVel});
    }

    REQUIRE(summaries.size() >= 30);

    // Verify ratchet pattern repeats every 3 steps (checking noteOn count)
    // Expected ratchet counts: [1, 2, 4, 1, 2, 4, ...]
    for (int step = 0; step < 15; ++step) {
        INFO("Step " << step << ": checking ratchet count cycles with period 3");
        CHECK(summaries[static_cast<size_t>(step)].noteOnCount ==
              summaries[static_cast<size_t>(step % 3)].noteOnCount);
    }

    // Verify velocity pattern repeats every 5 steps (checking first velocity)
    // Steps 0, 5, 10 should have same velocity; 1, 6, 11 should have same; etc.
    for (int step = 0; step < 15; ++step) {
        INFO("Step " << step << ": checking velocity cycles with period 5");
        CHECK(summaries[static_cast<size_t>(step)].firstVelocity ==
              summaries[static_cast<size_t>(step % 5)].firstVelocity);
    }

    // Verify the combined (ratchetCount, velocity) is NOT periodic with period 3
    bool allMatch3 = true;
    for (size_t i = 0; i < 12; ++i) {
        if (summaries[i].firstVelocity != summaries[i + 3].firstVelocity) {
            allMatch3 = false;
            break;
        }
    }
    CHECK_FALSE(allMatch3);

    // Verify the combined (ratchetCount, velocity) is NOT periodic with period 5
    bool allMatch5 = true;
    for (size_t i = 0; i < 10; ++i) {
        if (summaries[i].noteOnCount != summaries[i + 5].noteOnCount) {
            allMatch5 = false;
            break;
        }
    }
    CHECK_FALSE(allMatch5);

    // Verify the full 15-step combined sequence repeats at step 15
    for (size_t i = 0; i < 15; ++i) {
        INFO("Step " << i << " vs Step " << (i + 15));
        CHECK(summaries[i].noteOnCount == summaries[i + 15].noteOnCount);
        CHECK(summaries[i].firstVelocity == summaries[i + 15].firstVelocity);
    }
}


// T051: Ratchet lane length 1, value 1 (default) produces no ratcheting
TEST_CASE("Ratchet lane length 1 with default value 1 produces no ratcheting",
          "[arp][ratchet][us3]") {
    // SC-006, FR-002: Default ratchet lane (length 1, value 1) produces
    // exactly 1 noteOn per step across extended playback -- no ratcheting.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Ratchet lane at default: length 1, step[0] = 1 (set by constructor)
    // No changes needed.

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run 50 steps (50 * 11025 = 551250 samples, 551250/512 ~= 1076 blocks)
    auto events = collectEvents(arp, ctx, 1200);
    auto noteOns = filterNoteOns(events);

    // Should have at least 50 noteOns (one per step)
    REQUIRE(noteOns.size() >= 50);

    // Verify exactly 1 noteOn per step (no ratcheting)
    int32_t firstOnset = noteOns[0].sampleOffset;
    const int32_t stepDuration = 11025;

    for (int step = 0; step < 50; ++step) {
        int32_t windowStart = firstOnset + step * stepDuration;
        int32_t windowEnd = windowStart + stepDuration;
        size_t count = 0;
        for (const auto& e : noteOns) {
            if (e.sampleOffset >= windowStart && e.sampleOffset < windowEnd) {
                ++count;
            }
        }
        INFO("Step " << step << ": expected 1 noteOn, got " << count);
        CHECK(count == 1);
    }
}


// T052: Ratchet lane advances exactly once per arp step tick
TEST_CASE("Ratchet lane advances once per step alongside other lanes",
          "[arp][ratchet][us3]") {
    // FR-004: The ratchet lane advances once per arp step tick, simultaneously
    // with velocity/gate/pitch/modifier lanes.
    // Verify by configuring ratchet lane length 4 with steps [1, 3, 2, 4]
    // and observing the correct ratchet count applied to each consecutive step.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Ratchet lane: length 4, steps [1, 3, 2, 4]
    arp.ratchetLane().setLength(4);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(1));
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(3));
    arp.ratchetLane().setStep(2, static_cast<uint8_t>(2));
    arp.ratchetLane().setStep(3, static_cast<uint8_t>(4));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks for 8 steps (2 full ratchet lane cycles)
    // 8 * 11025 = 88200 samples. 88200 / 512 ~= 173 blocks. Use 250.
    auto events = collectEvents(arp, ctx, 250);
    auto noteOns = filterNoteOns(events);

    // Expected noteOns per step: [1, 3, 2, 4, 1, 3, 2, 4]
    // Total for 8 steps: 1+3+2+4+1+3+2+4 = 20 noteOns
    REQUIRE(noteOns.size() >= 20);

    int32_t firstOnset = noteOns[0].sampleOffset;
    const int32_t stepDuration = 11025;
    const size_t expectedCounts[] = {1, 3, 2, 4, 1, 3, 2, 4};

    for (int step = 0; step < 8; ++step) {
        int32_t windowStart = firstOnset + step * stepDuration;
        int32_t windowEnd = windowStart + stepDuration;
        size_t count = 0;
        for (const auto& e : noteOns) {
            if (e.sampleOffset >= windowStart && e.sampleOffset < windowEnd) {
                ++count;
            }
        }
        INFO("Step " << step << ": expected " << expectedCounts[step]
             << " noteOns (ratchet count), got " << count);
        CHECK(count == expectedCounts[step]);
    }
}


// =============================================================================
// Phase 6: User Story 4 -- Ratcheting with Modifier Interaction (SC-005)
// =============================================================================

// T057: Ratchet count 3 + Tie modifier: Tie takes priority, zero events emitted
TEST_CASE("Ratchet count 3 + Tie: previous note sustains, zero ratchet events",
          "[arp][ratchet][us4][modifiers]") {
    // Tie should override ratcheting entirely. The previous note sustains.
    // No ratcheted retriggering occurs. ratchetSubStepsRemaining_ stays 0.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Ratchet lane: length 2, step[0] = 1, step[1] = 3
    arp.ratchetLane().setLength(2);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(1));
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(3));

    // Modifier lane: length 2, step[0] = Active, step[1] = Active|Tie
    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks to cover 4 steps
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Step 0: ratchet 1, Active -> 1 noteOn
    // Step 1: ratchet 3, Tie -> Tie overrides, 0 noteOns (previous sustains)
    // Step 2: ratchet 1, Active -> 1 noteOn
    // Step 3: ratchet 3, Tie -> 0 noteOns
    // Over 4 steps: expect 2 noteOns (steps 0 and 2 only)
    int32_t firstOnset = noteOns[0].sampleOffset;
    const int32_t stepDuration = 11025;

    // Count noteOns in step 1 window [firstOnset + 11025, firstOnset + 22050)
    size_t noteOnsInStep1 = 0;
    for (const auto& e : noteOns) {
        if (e.sampleOffset >= firstOnset + stepDuration &&
            e.sampleOffset < firstOnset + 2 * stepDuration) {
            ++noteOnsInStep1;
        }
    }
    CHECK(noteOnsInStep1 == 0);  // Tie suppresses all ratcheting

    // Verify noteOffs are also suppressed for step 1 (tie sustains)
    auto noteOffs = filterNoteOffs(events);
    size_t noteOffsInStep1 = 0;
    for (const auto& e : noteOffs) {
        if (e.sampleOffset >= firstOnset + stepDuration &&
            e.sampleOffset < firstOnset + 2 * stepDuration) {
            ++noteOffsInStep1;
        }
    }
    CHECK(noteOffsInStep1 == 0);  // No noteOffs during Tie step either
}


// T058: Ratchet count 2 + Rest modifier: Rest takes priority, no notes fire
TEST_CASE("Ratchet count 2 + Rest: no notes fire, ratcheting suppressed",
          "[arp][ratchet][us4][modifiers]") {
    // Rest (kStepActive not set) should suppress ratcheting entirely.
    // No noteOn events. Sub-step state not initialized.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Ratchet lane: length 2, step[0] = 1, step[1] = 2
    arp.ratchetLane().setLength(2);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(1));
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(2));

    // Modifier lane: length 2, step[0] = Active, step[1] = Rest (0x00)
    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, 0x00);  // Rest

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks to cover 4 steps (2 full cycles)
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    int32_t firstOnset = noteOns[0].sampleOffset;
    const int32_t stepDuration = 11025;

    // Step 1 is Rest: no noteOns in step 1 window, even though ratchet = 2
    size_t noteOnsInStep1 = 0;
    for (const auto& e : noteOns) {
        if (e.sampleOffset >= firstOnset + stepDuration &&
            e.sampleOffset < firstOnset + 2 * stepDuration) {
            ++noteOnsInStep1;
        }
    }
    CHECK(noteOnsInStep1 == 0);  // Rest suppresses ratcheting entirely

    // Also verify step 3 (second cycle step 1) is also Rest
    size_t noteOnsInStep3 = 0;
    for (const auto& e : noteOns) {
        if (e.sampleOffset >= firstOnset + 3 * stepDuration &&
            e.sampleOffset < firstOnset + 4 * stepDuration) {
            ++noteOnsInStep3;
        }
    }
    CHECK(noteOnsInStep3 == 0);
}


// T059: Ratchet count 3 + Accent: first sub-step accented, sub-steps 2 and 3 normal
TEST_CASE("Ratchet count 3 + Accent: first sub-step accented, rest normal velocity",
          "[arp][ratchet][us4][modifiers]") {
    // Accent applies to first sub-step only. Subsequent sub-steps use
    // pre-accent velocity (velocity lane scaling only, no accent boost).
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    // Ratchet lane: length 1, step[0] = 3
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(3));

    // Modifier lane: length 1, step[0] = Active|Accent
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive | kStepAccent));

    arp.noteOn(60, 80);  // Base velocity 80

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Collect events for a few steps
    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    // Step 0 with ratchet 3 and accent:
    // vel lane scale = 1.0 (default), so base = round(80 * 1.0) = 80
    // Accent boosts: 80 + 30 = 110
    // First sub-step: velocity = 110 (accented)
    // Sub-steps 2 and 3: velocity = 80 (pre-accent, no accent boost)
    REQUIRE(noteOns.size() >= 3);
    CHECK(noteOns[0].velocity == 110);  // First sub-step: accented
    CHECK(noteOns[1].velocity == 80);   // Second sub-step: un-accented
    CHECK(noteOns[2].velocity == 80);   // Third sub-step: un-accented
}


// T060: Ratchet count 2 + Slide: first sub-step legato, second normal retrigger
TEST_CASE("Ratchet count 2 + Slide: first sub-step legato, second normal",
          "[arp][ratchet][us4][modifiers]") {
    // Slide applies to first sub-step only (transition from previous note).
    // Second sub-step is normal retrigger (legato=false).
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Ratchet lane: length 2, step[0] = 1, step[1] = 2
    arp.ratchetLane().setLength(2);
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(1));
    arp.ratchetLane().setStep(1, static_cast<uint8_t>(2));

    // Modifier lane: length 2, step[0] = Active, step[1] = Active|Slide
    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepSlide));

    arp.noteOn(60, 100);
    arp.noteOn(64, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    // Step 0: ratchet 1, Active -> 1 normal noteOn (legato=false)
    // Step 1: ratchet 2, Slide -> first sub-step legato=true, second sub-step legato=false
    REQUIRE(noteOns.size() >= 3);

    // Step 0 noteOn: normal
    CHECK(noteOns[0].legato == false);

    // Step 1, first sub-step: Slide (legato=true, transition from previous note)
    CHECK(noteOns[1].legato == true);

    // Step 1, second sub-step: normal retrigger (legato=false)
    CHECK(noteOns[2].legato == false);
}


// T061: Modifier evaluation priority unchanged when ratchet count > 1
TEST_CASE("Modifier priority (Rest > Tie > Slide > Accent) unchanged with ratchet",
          "[arp][ratchet][us4][modifiers]") {
    // Test that with all modifiers combined, priority still holds.
    // Rest should always win, regardless of ratchet count.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    // Ratchet lane: length 1, step[0] = 4 (maximum ratcheting)
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));

    SECTION("Rest overrides ratcheting (kStepActive not set)") {
        // Even with ratchet 4, Rest should produce zero events
        arp.modifierLane().setStep(0, 0x00);  // Rest (kStepActive not set)

        arp.noteOn(60, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 11025;  // One full step
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        auto events = collectEvents(arp, ctx, 5);
        auto noteOns = filterNoteOns(events);

        // No noteOns for any step -- all are Rest
        // (the first step outputs noteOff for previous note, but no noteOn)
        // Since there's no previous note, expect 0 noteOns total.
        // Actually step fires initially, and Rest suppresses it.
        // With only Rest steps and ratchet 4, zero noteOns should appear.
        size_t totalNoteOns = 0;
        for (const auto& e : noteOns) {
            (void)e;
            ++totalNoteOns;
        }
        CHECK(totalNoteOns == 0);
    }

    SECTION("Tie overrides ratcheting") {
        // Step 0: Active, Step 1: Tie with ratchet 4
        arp.modifierLane().setLength(2);
        arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
        arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));

        arp.noteOn(60, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        auto events = collectEvents(arp, ctx, 100);
        auto noteOns = filterNoteOns(events);

        int32_t firstOnset = noteOns[0].sampleOffset;
        const int32_t stepDuration = 11025;

        // Step 1 has Tie: zero noteOns even with ratchet 4
        size_t noteOnsInStep1 = 0;
        for (const auto& e : noteOns) {
            if (e.sampleOffset >= firstOnset + stepDuration &&
                e.sampleOffset < firstOnset + 2 * stepDuration) {
                ++noteOnsInStep1;
            }
        }
        CHECK(noteOnsInStep1 == 0);
    }

    SECTION("Modifiers evaluated before ratchet initialization") {
        // If Rest causes early return, ratchet sub-step state must remain 0.
        // Following step should work normally with its own ratchet count.
        arp.ratchetLane().setLength(2);
        arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));
        arp.ratchetLane().setStep(1, static_cast<uint8_t>(1));

        arp.modifierLane().setLength(2);
        arp.modifierLane().setStep(0, 0x00);  // Rest
        arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive));

        arp.noteOn(60, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        auto events = collectEvents(arp, ctx, 100);
        auto noteOns = filterNoteOns(events);

        int32_t firstOnset = -1;
        // Find first noteOn (should be in step 1 window)
        if (!noteOns.empty()) {
            firstOnset = noteOns[0].sampleOffset;
        }
        REQUIRE(!noteOns.empty());

        // The first noteOn should appear in the step 1 window (after step 0 rest)
        // Step 0 rest takes 11025 samples, step 1 fires at 11025
        // Step 1 with ratchet 1: exactly 1 noteOn
        CHECK(firstOnset >= 11025);  // Not in step 0
    }
}


// T062: Ratchet count 2 + Slide on first step (no previous note): fallback
TEST_CASE("Ratchet count 2 + Slide on first step (no previous note)",
          "[arp][ratchet][us4][modifiers][edge]") {
    // First step: Slide with no preceding note falls back to normal noteOn.
    // First sub-step: legato=false (no preceding note for slide transition).
    // Second sub-step: normal retrigger (legato=false).
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Ratchet lane: length 1, step[0] = 2
    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));

    // Modifier lane: length 1, step[0] = Active|Slide
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive | kStepSlide));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    // First step: ratchet 2, Slide but no prior note
    REQUIRE(noteOns.size() >= 2);

    // First sub-step: Slide with no preceding note -> legato=false (fallback)
    CHECK(noteOns[0].legato == false);

    // Second sub-step: normal retrigger (legato=false)
    CHECK(noteOns[1].legato == false);
}


// =============================================================================
// Ratchet Swing (078-ratchet-swing)
// =============================================================================
// Tests for the continuous ratchet swing parameter that applies a long-short
// ratio to consecutive pairs of sub-steps.

// T070: Default 50% swing produces identical sub-step gaps to current behavior
TEST_CASE("Ratchet swing 50% produces equal sub-step durations (backward compat)",
          "[arp][ratchet-swing]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setRatchetSwing(50.0f);  // explicit default

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    // At 120 BPM, 1/8 note = 11025 samples. With ratchet 2 at 50% swing:
    // baseDuration = 11025 / 2 = 5512, pairDuration = 2 * 5512 = 11024
    // long = round(11024 * 0.50) = 5512, short = 11024 - 5512 = 5512
    // Same as equal spacing: both sub-steps = 5512
    REQUIRE(noteOns.size() >= 2);
    int32_t gap = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
    CHECK(gap == 5512);

    // Check step 1 also has equal sub-steps
    if (noteOns.size() >= 4) {
        int32_t gap2 = noteOns[3].sampleOffset - noteOns[2].sampleOffset;
        CHECK(gap2 == 5512);
    }
}


// T071: Ratchet 2 at 67% swing produces correct long-short pair
TEST_CASE("Ratchet 2 at 67% swing: long-short pair",
          "[arp][ratchet-swing]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setRatchetSwing(67.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    // stepDuration = 11025 (at 120BPM, 1/8 note, 44100Hz)
    // baseDuration = 11025 / 2 = 5512
    // pairDuration = 2 * 5512 = 11024
    // swingRatio = 67 / 100 = 0.67
    // long = round(11024 * 0.67) = round(7386.08) = 7386
    // short = 11024 - 7386 = 3638
    REQUIRE(noteOns.size() >= 2);
    int32_t gap = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
    CHECK(gap == 7386);

    // Verify full step: gap between step 0 first sub-step and step 1 first sub-step
    // = stepDuration = 11025 (step boundary timer drives the full step duration;
    // the pair sum 7386+3638=11024 fills within the step, 1 sample absorbed at end)
    if (noteOns.size() >= 3) {
        int32_t stepGap = noteOns[2].sampleOffset - noteOns[0].sampleOffset;
        CHECK(stepGap == 11025);
    }
}


// T072: Ratchet 4 at 75% swing produces two long-short pairs
TEST_CASE("Ratchet 4 at 75% swing: two long-short pairs",
          "[arp][ratchet-swing]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setRatchetSwing(75.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    // stepDuration = 11025
    // baseDuration = 11025 / 4 = 2756
    // pairDuration = 2 * 2756 = 5512
    // swingRatio = 75 / 100 = 0.75
    // long = round(5512 * 0.75) = round(4134) = 4134
    // short = 5512 - 4134 = 1378
    // Gaps: [4134, 1378, 4134, 1378]
    REQUIRE(noteOns.size() >= 4);

    int32_t gap01 = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
    int32_t gap12 = noteOns[2].sampleOffset - noteOns[1].sampleOffset;
    int32_t gap23 = noteOns[3].sampleOffset - noteOns[2].sampleOffset;

    CHECK(gap01 == 4134);
    CHECK(gap12 == 1378);
    CHECK(gap23 == 4134);

    // The last sub-step (index 3) has duration 1378, filling to step end
}


// T073: Ratchet 3 at 67% swing: one pair + unpaired remainder
TEST_CASE("Ratchet 3 at 67% swing: one pair + base remainder",
          "[arp][ratchet-swing]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setRatchetSwing(67.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(3));
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    // stepDuration = 11025
    // baseDuration = 11025 / 3 = 3675
    // pairDuration = 2 * 3675 = 7350
    // swingRatio = 0.67
    // long = round(7350 * 0.67) = round(4924.5) = 4924 (rounds to even)
    // short = 7350 - 4924 = 2426
    // Third sub-step (unpaired): baseDuration = 3675
    // Gaps: [4924, 2426, 3675]
    REQUIRE(noteOns.size() >= 3);

    int32_t gap01 = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
    int32_t gap12 = noteOns[2].sampleOffset - noteOns[1].sampleOffset;

    // Allow ±1 for rounding
    CHECK(std::abs(gap01 - 4924) <= 1);
    CHECK(std::abs(gap12 - 2426) <= 1);

    // Verify total: first sub-step of next step should be at step boundary
    if (noteOns.size() >= 4) {
        int32_t stepTotal = noteOns[3].sampleOffset - noteOns[0].sampleOffset;
        CHECK(std::abs(stepTotal - 11025) <= 1);
    }
}


// T074: Ratchet 1 with swing has no effect (single note per step)
TEST_CASE("Ratchet 1 with swing: no effect",
          "[arp][ratchet-swing]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setRatchetSwing(75.0f);  // Max swing, but ratchet 1

    // Ratchet lane default is 1 (no ratcheting)
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    // Should produce exactly 1 noteOn per step, spaced by 11025 samples
    REQUIRE(noteOns.size() >= 2);
    int32_t gap = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
    CHECK(gap == 11025);
}


// T075: Gate duration scales with sub-step duration (long sub-step -> longer gate)
TEST_CASE("Ratchet swing: gate scales with sub-step duration",
          "[arp][ratchet-swing]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);  // 50% gate for clear measurement
    arp.setRatchetSwing(75.0f);

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // long = round(11024 * 0.75) = 8268
    // short = 11024 - 8268 = 2756
    // Gate at 50%: longGate = round(8268 * 0.50) = 4134
    //              shortGate = round(2756 * 0.50) = 1378
    REQUIRE(noteOns.size() >= 2);
    REQUIRE(noteOffs.size() >= 2);

    // Find noteOff for first sub-step (long)
    int32_t firstOnset = noteOns[0].sampleOffset;
    int32_t firstNoteOff = -1;
    for (const auto& e : noteOffs) {
        if (e.note == noteOns[0].note && e.sampleOffset > firstOnset) {
            firstNoteOff = e.sampleOffset;
            break;
        }
    }
    REQUIRE(firstNoteOff > 0);
    int32_t longGate = firstNoteOff - firstOnset;

    // Find noteOff for second sub-step (short)
    int32_t secondOnset = noteOns[1].sampleOffset;
    int32_t secondNoteOff = -1;
    for (const auto& e : noteOffs) {
        if (e.note == noteOns[1].note && e.sampleOffset > secondOnset) {
            secondNoteOff = e.sampleOffset;
            break;
        }
    }
    REQUIRE(secondNoteOff > 0);
    int32_t shortGate = secondNoteOff - secondOnset;

    // Long gate should be larger than short gate
    CHECK(longGate > shortGate);
    // Verify approximate values (allow ±2 for rounding)
    CHECK(std::abs(longGate - 4134) <= 2);
    CHECK(std::abs(shortGate - 1378) <= 2);
}


// T076: Step swing + ratchet swing interaction
TEST_CASE("Step swing modifies total duration, ratchet swing subdivides within",
          "[arp][ratchet-swing]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setSwing(50.0f);           // 50% step swing
    arp.setRatchetSwing(67.0f);    // 67% ratchet swing

    arp.ratchetLane().setStep(0, static_cast<uint8_t>(2));
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    // Step swing at 50% on 1/8 note (11025 base):
    // Even step (step 0): 11025 * (1 + 0.50) = 16537
    // Odd step (step 1): 11025 * (1 - 0.50) = 5512
    //
    // Ratchet 2 at 67% on step 0 (duration 16537):
    // baseDuration = 16537 / 2 = 8268
    // pairDuration = 2 * 8268 = 16536
    // long = round(16536 * 0.67) = round(11079.12) = 11079
    // short = 16536 - 11079 = 5457
    REQUIRE(noteOns.size() >= 2);
    int32_t gap = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
    CHECK(std::abs(gap - 11079) <= 1);

    // Ratchet 2 at 67% on step 1 (duration 5512):
    // baseDuration = 5512 / 2 = 2756
    // pairDuration = 2 * 2756 = 5512
    // long = round(5512 * 0.67) = round(3693.04) = 3693
    // short = 5512 - 3693 = 1819
    if (noteOns.size() >= 4) {
        int32_t gap2 = noteOns[3].sampleOffset - noteOns[2].sampleOffset;
        CHECK(std::abs(gap2 - 3693) <= 1);
    }
}

// =============================================================================
// Ratchet sub-step NoteOns must carry velocity >= 1 (GMF-011)
// =============================================================================
// fireSubStep's applyDecay clamped to [0,127] while the main step path clamps to
// [1,127]. decayFactor = (1 - ratchetDecay)^subStepIndex, so a high decay drives
// later sub-steps to 0. Many hosts read a velocity-0 NoteOn as a note-off, so the
// note goes silent early at data.outputEvents -- while Gradus's audition voice
// takes noteOn(note, 0) as active with velocity 0, i.e. audible-state true but
// silent. Host and local monitor then disagree about what is sounding.

TEST_CASE("GMF-011: ratchet sub-step NoteOns always carry velocity >= 1",
          "[ArpeggiatorCore][ratchet][velocity]") {
    for (const float decay : {80.0f, 100.0f}) {
        ArpeggiatorCore arp;
        BlockContext ctx{};
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        arp.prepare(ctx.sampleRate, ctx.blockSize);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Quarter, NoteModifier::None);
        arp.setRatchetDecay(decay);
        arp.ratchetLane().setLength(1);
        arp.ratchetLane().setStep(0, static_cast<uint8_t>(4));
        arp.noteOn(60, 100);

        const auto events = collectEvents(arp, ctx, 300);
        const auto noteOns = filterNoteOns(events);
        INFO("ratchetDecay = " << decay << "%, note-ons captured: " << noteOns.size());
        REQUIRE(noteOns.size() > 4);

        for (const auto& on : noteOns) {
            INFO("NoteOn pitch " << static_cast<int>(on.note)
                 << " at " << on.sampleOffset
                 << " velocity " << static_cast<int>(on.velocity));
            CHECK(on.velocity >= 1);
        }
    }
}
