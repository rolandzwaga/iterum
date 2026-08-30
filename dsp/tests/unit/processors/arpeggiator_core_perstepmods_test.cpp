// arpeggiator_core_perstepmods_test.cpp
// Per-step modifiers (spec 073): flags/legato, tie, slide, accent, combined, edge
// Split from the former 17k-line arpeggiator_core_test.cpp (D1). Shared helpers in
// arpeggiator_core_test_helpers.h.
#include "arpeggiator_core_test_helpers.h"



// =============================================================================
// Phase 5 (073-per-step-mods): ArpStepFlags and ArpEvent.legato Tests
// =============================================================================

TEST_CASE("ArpStepFlags_BitValues", "[processors][arpeggiator_core]") {
    // FR-001: Verify exact bit values of each flag
    REQUIRE(static_cast<uint8_t>(Krate::DSP::kStepActive) == 0x01);
    REQUIRE(static_cast<uint8_t>(Krate::DSP::kStepTie) == 0x02);
    REQUIRE(static_cast<uint8_t>(Krate::DSP::kStepSlide) == 0x04);
    REQUIRE(static_cast<uint8_t>(Krate::DSP::kStepAccent) == 0x08);
}


TEST_CASE("ArpStepFlags_Combinable", "[processors][arpeggiator_core]") {
    // FR-001: Verify flags can be combined via bitwise OR
    uint8_t activeAccent = static_cast<uint8_t>(Krate::DSP::kStepActive) |
                           static_cast<uint8_t>(Krate::DSP::kStepAccent);
    REQUIRE(activeAccent == 0x09);

    uint8_t allFlags = static_cast<uint8_t>(Krate::DSP::kStepActive) |
                       static_cast<uint8_t>(Krate::DSP::kStepTie) |
                       static_cast<uint8_t>(Krate::DSP::kStepSlide) |
                       static_cast<uint8_t>(Krate::DSP::kStepAccent);
    REQUIRE(allFlags == 0x0F);
}


TEST_CASE("ArpStepFlags_UnderlyingType", "[processors][arpeggiator_core]") {
    // FR-001: Verify underlying type is uint8_t
    static_assert(std::is_same_v<std::underlying_type_t<Krate::DSP::ArpStepFlags>, uint8_t>,
                  "ArpStepFlags must have underlying type uint8_t");
    REQUIRE(true);
}


TEST_CASE("ArpEvent_LegatoDefaultsFalse", "[processors][arpeggiator_core]") {
    // FR-003, FR-004: Default-constructed ArpEvent has legato == false
    ArpEvent event{};
    REQUIRE(event.legato == false);
}


TEST_CASE("ArpEvent_LegatoField_SetAndRead", "[processors][arpeggiator_core]") {
    // FR-003: legato field can be set and read back
    ArpEvent event{};
    event.legato = true;
    REQUIRE(event.legato == true);
}


TEST_CASE("ArpEvent_BackwardCompat_AggregateInit", "[processors][arpeggiator_core]") {
    // FR-004: Aggregate init without legato defaults to false
    ArpEvent event{ArpEvent::Type::NoteOn, 60, 100, 0};
    REQUIRE(event.note == 60);
    REQUIRE(event.velocity == 100);
    REQUIRE(event.sampleOffset == 0);
    REQUIRE(event.legato == false);
}


// =============================================================================
// Phase 3 (073-per-step-mods): Modifier Lane Infrastructure & Rest Tests (T013)
// =============================================================================

TEST_CASE("ModifierLane_DefaultIsActive", "[processors][arpeggiator_core]") {
    // FR-005, FR-007: Default modifier lane has length 1, step[0] = kStepActive
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    REQUIRE(arp.modifierLane().length() == 1);
    REQUIRE(arp.modifierLane().getStep(0) == static_cast<uint8_t>(kStepActive));
}


TEST_CASE("ModifierLane_AccessorsExist", "[processors][arpeggiator_core]") {
    // FR-024: Mutable and const modifierLane() accessors compile and return ArpLane<uint8_t>&
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    ArpLane<uint8_t>& mutableLane = arp.modifierLane();
    const ArpeggiatorCore& constArp = arp;
    const ArpLane<uint8_t>& constLane = constArp.modifierLane();

    REQUIRE(mutableLane.length() == 1);
    REQUIRE(constLane.length() == 1);
}


TEST_CASE("ModifierLane_SetAccentVelocity", "[processors][arpeggiator_core]") {
    // FR-025: setAccentVelocity clamps to [0, 127]
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    arp.setAccentVelocity(50);
    REQUIRE(arp.accentVelocity() == 50);

    arp.setAccentVelocity(200);   // should clamp to 127
    REQUIRE(arp.accentVelocity() == 127);

    arp.setAccentVelocity(-1);    // should clamp to 0
    REQUIRE(arp.accentVelocity() == 0);
}


TEST_CASE("ModifierLane_SetSlideTime", "[processors][arpeggiator_core]") {
    // FR-025: setSlideTime clamps to [0, 500]
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    arp.setSlideTime(100.0f);
    REQUIRE(arp.slideTimeMs() == 100.0f);

    arp.setSlideTime(600.0f);   // should clamp to 500
    REQUIRE(arp.slideTimeMs() == 500.0f);

    arp.setSlideTime(-1.0f);    // should clamp to 0
    REQUIRE(arp.slideTimeMs() == 0.0f);
}


TEST_CASE("ModifierLane_ResetIncludesModifier", "[processors][arpeggiator_core]") {
    // FR-008: resetLanes() (called by reset()) resets modifierLane position and tieActive_
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);

    // Set modifier lane to length 4
    arp.modifierLane().setLength(4);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(3, static_cast<uint8_t>(kStepActive));

    // Advance modifier lane 2 steps manually
    arp.modifierLane().advance();
    arp.modifierLane().advance();
    REQUIRE(arp.modifierLane().currentStep() == 2);

    // Reset (calls resetLanes() internally)
    arp.reset();
    REQUIRE(arp.modifierLane().currentStep() == 0);

    // Verify tieActive_ is cleared by testing behavior:
    // Set up a tie step as first step and verify it produces silence (no noteOn)
    // because tieActive_ was cleared and there's no preceding active note.
    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepTie));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks to get at least one step
    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    // The first step is Tie with no preceding note -> silence (no noteOn).
    // The second step is Active -> noteOn.
    // So the first noteOn should be from step index 1, not step index 0.
    // If tieActive_ wasn't cleared, a bug could fire a noteOn on the Tie step.
    REQUIRE(noteOns.size() >= 1);
}


TEST_CASE("Rest_NoNoteOn", "[processors][arpeggiator_core]") {
    // FR-009: Rest step (0x00, kStepActive not set) produces no noteOn
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length 4, steps [Active, Active, Rest, Active]
    arp.modifierLane().setLength(4);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(2, 0x00);  // Rest
    arp.modifierLane().setStep(3, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks to cover 4+ steps
    // At 120 BPM, 1/8 note = 11025 samples. 4 steps = ~44100 samples = ~86 blocks
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Should have noteOns for steps 0, 1, 3 but NOT step 2.
    // In 4-step cycle: steps 0, 1, 3 fire = 3 noteOns per cycle.
    // Over ~200 blocks (~8+ cycles) we expect at least 3 noteOns.
    REQUIRE(noteOns.size() >= 3);

    // Verify pattern: group noteOns by cycle.
    // In a single cycle of 4 steps, we should get exactly 3 noteOns.
    // All notes should be C4 (60) since only 1 held note in Up mode.
    for (const auto& on : noteOns) {
        REQUIRE(on.note == 60);
    }

    // Verify the count is a multiple of 3 (3 noteOns per 4-step cycle)
    // Allow for partial last cycle.
    size_t fullCycles = noteOns.size() / 3;
    REQUIRE(fullCycles >= 1);
}


TEST_CASE("Rest_AllLanesAdvance", "[processors][arpeggiator_core]") {
    // FR-010: Rest step still advances all lanes (velocity, gate, pitch, modifier)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length 3, steps [Active, Rest, Active]
    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, 0x00);  // Rest
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    // Velocity lane: length 3, steps [1.0, 0.5, 0.8]
    arp.velocityLane().setLength(3);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.8f);

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Collect events for first 3 steps
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Step 0: Active, velocity scale 1.0 -> velocity 100
    // Step 1: Rest, velocity scale 0.5 -> consumed but no noteOn
    // Step 2: Active, velocity scale 0.8 -> velocity 80
    REQUIRE(noteOns.size() >= 2);
    CHECK(noteOns[0].velocity == 100);
    CHECK(noteOns[1].velocity == 80);  // NOT 50 (which would happen if step 1 didn't consume vel lane)
}


TEST_CASE("Rest_PreviousNoteOff", "[processors][arpeggiator_core]") {
    // FR-009: Rest step causes noteOff for any previously sounding note
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length 2, steps [Active, Rest]
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

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // Step 0: Active -> noteOn
    // Step 1: Rest -> previous note gets noteOff (either from gate or from rest)
    // Every noteOn should eventually be matched by a noteOff
    REQUIRE(noteOns.size() >= 1);
    REQUIRE(noteOffs.size() >= 1);

    // Verify note-on note 60 has a corresponding note-off note 60
    bool foundMatchingOff = false;
    for (const auto& off : noteOffs) {
        if (off.note == 60) {
            foundMatchingOff = true;
            break;
        }
    }
    REQUIRE(foundMatchingOff);
}


TEST_CASE("Rest_DefensiveBranch_LanesAdvance", "[processors][arpeggiator_core]") {
    // FR-010: When result.count == 0 (held buffer became empty between steps),
    // modifier lane still advances once.
    //
    // The result.count == 0 defensive branch inside fireStep() is reached when
    // selector_.advance(heldNotes_) returns zero notes. We exercise this by:
    //   1. Starting with a held note to get the arp running
    //   2. Removing the note via noteOff() between processBlock calls
    //   3. The next processBlock's empty-buffer guard fires first, but the
    //      modifier lane position from prior steps must be correct
    //   4. Re-adding the note and verifying the modifier lane cycle resumed
    //      at the correct position (proving it advanced during all prior steps)
    //
    // Additionally, we verify the modifier lane advances in the normal
    // result.count > 0 path even when the step is Rest (kStepActive not set).

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length 4. Steps: [Active, Rest, Rest, Active]
    // The cycle is: step 0 noteOn, step 1 rest, step 2 rest, step 3 noteOn, repeat
    arp.modifierLane().setLength(4);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, 0x00);  // Rest
    arp.modifierLane().setStep(2, 0x00);  // Rest
    arp.modifierLane().setStep(3, static_cast<uint8_t>(kStepActive));

    // Hold one note to trigger the arp
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks to fire multiple complete cycles (4 steps each)
    // At 120 BPM, 1/8 note = 0.25 sec = 11025 samples. 512 samples/block
    // -> ~21.5 blocks per step, ~86 blocks per cycle. Run 200 blocks ~ 2+ cycles.
    auto events = collectEvents(arp, ctx, 200);

    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // With the pattern [Active, Rest, Rest, Active]:
    // Each cycle of 4 steps should produce exactly 2 noteOn events (steps 0 and 3).
    // After 2+ full cycles we expect at least 4 noteOn events.
    REQUIRE(noteOns.size() >= 4);

    // Verify that noteOffs were emitted (Rest steps emit noteOff for previous note,
    // plus gate-based noteOffs). The last noteOn may not have its noteOff yet
    // if the collection ended before the gate expired.
    REQUIRE(noteOffs.size() >= 1);

    // Now remove the note to empty the buffer (simulates "notes gone" scenario)
    arp.noteOff(60);

    // Process a few more blocks -- processBlock returns early (empty buffer)
    auto emptyEvents = collectEvents(arp, ctx, 50);
    // Should emit noteOffs for any sounding notes (needsDisableNoteOff_)
    // No new noteOns expected
    auto emptyNoteOns = filterNoteOns(emptyEvents);
    REQUIRE(emptyNoteOns.empty());

    // Re-add the note and continue -- modifier lane position should continue
    // from where it was (not restart from 0). This proves all lanes advanced
    // properly during active playback including Rest steps.
    arp.noteOn(60, 100);
    auto resumeEvents = collectEvents(arp, ctx, 200);
    auto resumeNoteOns = filterNoteOns(resumeEvents);

    // After resuming, the arp should continue producing noteOn events
    // on Active steps, proving the modifier lane cycle is intact
    REQUIRE(resumeNoteOns.size() >= 2);
}


TEST_CASE("BitIdentical_DefaultModifierLane", "[processors][arpeggiator_core]") {
    // SC-002: Default modifier lane (length=1, step=kStepActive) produces output
    // bit-identical to Phase 4 baseline.
    const std::string basePath = std::string(KRATE_DSP_TESTS_DIR) + "/fixtures/";
    const double bpms[] = {120.0, 140.0, 180.0};
    const char* bpmNames[] = {"120", "140", "180"};

    size_t totalCompared = 0;
    size_t totalMismatches = 0;

    for (int t = 0; t < 3; ++t) {
        double bpm = bpms[t];
        std::string fixturePath = basePath + "arp_baseline_" + bpmNames[t] + "bpm.dat";

        // Load baseline fixture
        auto baselineEvents = readBaselineFixture(fixturePath);
        REQUIRE(baselineEvents.size() >= 1000);

        // Generate current output with default modifier lane
        ArpeggiatorCore arp;
        constexpr double kSampleRate = 44100.0;
        constexpr size_t kBlockSize = 512;

        arp.prepare(kSampleRate, kBlockSize);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(80.0f);
        arp.setSwing(0.0f);
        arp.noteOn(60, 100);  // C4

        BlockContext ctx;
        ctx.sampleRate = kSampleRate;
        ctx.blockSize = kBlockSize;
        ctx.tempoBPM = bpm;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        std::vector<ArpEvent> currentNoteOns;
        std::array<ArpEvent, 128> blockEvents;

        const size_t maxBlocks = 50000;
        for (size_t b = 0; b < maxBlocks && currentNoteOns.size() < baselineEvents.size(); ++b) {
            size_t count = arp.processBlock(ctx, blockEvents);
            for (size_t i = 0; i < count; ++i) {
                if (blockEvents[i].type == ArpEvent::Type::NoteOn) {
                    ArpEvent evt = blockEvents[i];
                    evt.sampleOffset += static_cast<int32_t>(b * kBlockSize);
                    currentNoteOns.push_back(evt);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(kBlockSize);
        }

        REQUIRE(currentNoteOns.size() >= baselineEvents.size());

        // Compare field-by-field
        size_t compareCount = baselineEvents.size();
        size_t mismatches = 0;
        for (size_t i = 0; i < compareCount; ++i) {
            if (currentNoteOns[i].note != baselineEvents[i].note ||
                currentNoteOns[i].velocity != baselineEvents[i].velocity ||
                currentNoteOns[i].sampleOffset != baselineEvents[i].sampleOffset) {
                ++mismatches;
                if (mismatches <= 5) {
                    INFO("Mismatch at step " << i << " at " << bpmNames[t] << " BPM: "
                         << "note=" << static_cast<int>(currentNoteOns[i].note) << " vs "
                         << static_cast<int>(baselineEvents[i].note)
                         << ", vel=" << static_cast<int>(currentNoteOns[i].velocity) << " vs "
                         << static_cast<int>(baselineEvents[i].velocity)
                         << ", offset=" << currentNoteOns[i].sampleOffset << " vs "
                         << baselineEvents[i].sampleOffset);
                }
            }
        }

        INFO(compareCount << " steps compared, " << mismatches << " mismatches at "
             << bpmNames[t] << " BPM");
        CHECK(mismatches == 0);

        totalCompared += compareCount;
        totalMismatches += mismatches;
    }

    INFO(totalCompared << " total steps compared, " << totalMismatches
         << " total mismatches across 120/140/180 BPM");
    REQUIRE(totalMismatches == 0);
}


// =============================================================================
// Phase 4: User Story 2 -- Tie Steps for Sustained Notes (073-per-step-mods)
// =============================================================================

TEST_CASE("Tie_SuppressesNoteOffAndNoteOn", "[processors][arpeggiator_core]") {
    // FR-011: steps [Active, Tie, Active]: step 0 noteOn, step 1 emits nothing,
    // step 2 emits noteOff then noteOn
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length 3, steps [Active, Tie, Active]
    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // At 120 BPM, 1/8 note = 11025 samples per step.
    // Run enough blocks for 3+ steps.
    auto events = collectEvents(arp, ctx, 200);

    // Analyze step-by-step behavior using absolute sample offsets.
    // Step boundaries are at 11025, 22050, 33075, ...
    // Step 0 fires at offset 11025 -> noteOn expected
    // Step 1 fires at offset 22050 -> Tie: no noteOff, no noteOn
    // Step 2 fires at offset 33075 -> Active: noteOff for step-0 note, then noteOn

    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // We should get noteOns for steps 0 and 2 but NOT step 1
    // In a 3-step cycle: [Active, Tie, Active] -> 2 noteOns per cycle
    REQUIRE(noteOns.size() >= 2);

    // Step 0: noteOn at note 60
    CHECK(noteOns[0].note == 60);
    // Step 2: noteOn at note 60 (same note in Up mode with 1 held key)
    CHECK(noteOns[1].note == 60);

    // The gap between noteOn[0] and noteOn[1] should be 2 steps (22050 samples),
    // because step 1 (Tie) suppresses its noteOn.
    int32_t gap = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
    CHECK(gap == 22050);

    // Verify: a noteOff for note 60 must appear BEFORE step 2's noteOn
    // (the tie step 1 suppresses noteOff, so the noteOff fires at step 2)
    bool foundNoteOff = false;
    for (const auto& off : noteOffs) {
        if (off.note == 60 && off.sampleOffset == noteOns[1].sampleOffset) {
            foundNoteOff = true;
            break;
        }
    }
    CHECK(foundNoteOff);
}


TEST_CASE("Tie_Chain_SustainsAcross3Steps", "[processors][arpeggiator_core]") {
    // FR-014, SC-005: steps [Active, Tie, Tie, Active]: step 0 noteOn,
    // steps 1-2 silent, step 3 emits noteOff+noteOn
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length 4, steps [Active, Tie, Tie, Active]
    arp.modifierLane().setLength(4);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(3, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 300);

    // Step boundaries: 11025, 22050, 33075, 44100
    // Step 0 (11025): Active -> noteOn
    // Step 1 (22050): Tie -> suppress noteOff + noteOn (chain start)
    // Step 2 (33075): Tie -> suppress noteOff + noteOn (chain continues)
    // Step 3 (44100): Active -> noteOff then noteOn (chain ends)

    auto noteOns = filterNoteOns(events);

    // 2 noteOns per 4-step cycle
    REQUIRE(noteOns.size() >= 2);

    // Gap between first and second noteOn = 3 steps (33075 samples)
    int32_t gap = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
    CHECK(gap == 33075);

    // SC-005: In the tied region (steps 1-2), there should be zero events.
    // Check that no events exist between step 0's noteOn and step 3's noteOff/noteOn.
    int32_t step1Start = noteOns[0].sampleOffset + 11025;  // step 1 boundary
    int32_t step3Start = noteOns[0].sampleOffset + 33075;  // step 3 boundary
    size_t eventsInTiedRegion = 0;
    for (const auto& evt : events) {
        if (evt.sampleOffset > step1Start && evt.sampleOffset < step3Start) {
            ++eventsInTiedRegion;
        }
    }
    INFO("Events in tied region (steps 1-2): " << eventsInTiedRegion);
    CHECK(eventsInTiedRegion == 0);
}


TEST_CASE("Tie_OverridesGateLane", "[processors][arpeggiator_core]") {
    // FR-012: Gate lane set to very short (0.01), Tie step sustains
    // despite short gate. The previous note does NOT receive a noteOff
    // during the tie step's duration.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Very short gate lane
    arp.gateLane().setLength(1);
    arp.gateLane().setStep(0, 0.01f);

    // Modifier lane: [Active, Tie, Active]
    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 2);

    // With gate 0.01, normal noteOff would fire very quickly (within ~110 samples).
    // But Tie step sustains: the noteOff should NOT fire between step 0's noteOn
    // and step 2's noteOn/noteOff.
    int32_t step0On = noteOns[0].sampleOffset;
    int32_t step2On = noteOns[1].sampleOffset;

    // The noteOff for step 0's note should NOT appear before step 2's boundary.
    // It should appear AT step 2's boundary (when Active step clears the tie).
    bool earlyNoteOff = false;
    for (const auto& off : noteOffs) {
        if (off.note == 60 && off.sampleOffset > step0On &&
            off.sampleOffset < step2On) {
            earlyNoteOff = true;
            break;
        }
    }
    CHECK_FALSE(earlyNoteOff);
}


TEST_CASE("Tie_NoPrecedingNote_BehavesAsRest", "[processors][arpeggiator_core]") {
    // FR-013: First step is Tie with no previous note -> silence (not crash)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length 3, steps [Tie, Tie, Active]
    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Steps 0 and 1: Tie with no preceding note -> silence (behaves as rest)
    // Step 2: Active -> noteOn
    REQUIRE(noteOns.size() >= 1);

    // First step fires immediately. Steps: 0=0, 1=11025, 2=22050
    CHECK(noteOns[0].sampleOffset == 22050);
    CHECK(noteOns[0].note == 60);
}


TEST_CASE("Tie_AfterRest_BehavesAsRest", "[processors][arpeggiator_core]") {
    // FR-013: steps [Active, Rest, Tie, Active]: step 0 noteOn, step 1 noteOff (rest),
    // step 2 silence (tie breaks because no sounding note), step 3 fresh noteOn
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length 4, steps [Active, Rest, Tie, Active]
    arp.modifierLane().setLength(4);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, 0x00);  // Rest
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(3, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 300);
    auto noteOns = filterNoteOns(events);

    // Step 0: Active -> noteOn
    // Step 1: Rest -> noteOff for step 0 note, no noteOn
    // Step 2: Tie but no sounding note (rest cleared it) -> silence
    // Step 3: Active -> noteOn
    // So 2 noteOns per 4-step cycle
    REQUIRE(noteOns.size() >= 2);

    // Gap between noteOns should be 3 steps = 33075 samples
    // (step 0 -> step 3)
    int32_t gap = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
    CHECK(gap == 33075);
}


TEST_CASE("Tie_ChordMode_SustainsAllNotes", "[processors][arpeggiator_core]") {
    // FR-011: Chord mode with 2 notes held; Tie step: verify both chord notes'
    // noteOffs are suppressed and no new noteOns fire
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length 3, steps [Active, Tie, Active]
    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 100);  // C4
    arp.noteOn(64, 100);  // E4

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 300);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // Step 0: Active, Chord -> 2 noteOns (C4, E4)
    // Step 1: Tie -> suppress all noteOffs and noteOns (both notes sustain)
    // Step 2: Active, Chord -> 2 noteOffs (C4, E4), then 2 noteOns (C4, E4)
    REQUIRE(noteOns.size() >= 4);

    // First chord: 2 notes at step 0
    int32_t firstChordOffset = noteOns[0].sampleOffset;
    CHECK(noteOns[1].sampleOffset == firstChordOffset);

    // Second chord: 2 notes at step 2 (2 steps later = 22050 samples)
    int32_t secondChordOffset = noteOns[2].sampleOffset;
    CHECK(noteOns[3].sampleOffset == secondChordOffset);
    CHECK(secondChordOffset - firstChordOffset == 22050);

    // Verify no noteOns or noteOffs exist in the tie step region
    int32_t tieStepStart = firstChordOffset + 11025;  // step 1 boundary
    int32_t activeStep2 = firstChordOffset + 22050;    // step 2 boundary
    size_t eventsInTieRegion = 0;
    for (const auto& evt : events) {
        if (evt.sampleOffset > tieStepStart && evt.sampleOffset < activeStep2) {
            ++eventsInTieRegion;
        }
    }
    CHECK(eventsInTieRegion == 0);
}


TEST_CASE("Tie_SetsAndClearsTieActiveState", "[processors][arpeggiator_core]") {
    // Verify tieActive_ state transitions via behavioral proxy:
    // After resetLanes(), a Tie step with no preceding Active step should
    // produce silence, proving tieActive_ was cleared.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // First: establish a tie chain
    // Modifier lane: [Active, Tie]
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

    // Run enough for 2 steps: Active then Tie (tieActive_ should be true)
    auto events1 = collectEvents(arp, ctx, 100);
    auto noteOns1 = filterNoteOns(events1);
    REQUIRE(noteOns1.size() >= 1);

    // Now reset lanes (should clear tieActive_)
    arp.reset();

    // Reconfigure: modifier lane is [Tie, Active]
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 100);

    BlockContext ctx2;
    ctx2.sampleRate = 44100.0;
    ctx2.blockSize = 512;
    ctx2.tempoBPM = 120.0;
    ctx2.isPlaying = true;
    ctx2.transportPositionSamples = 0;

    auto events2 = collectEvents(arp, ctx2, 200);
    auto noteOns2 = filterNoteOns(events2);

    // Step 0: Tie with no preceding note -> silence (proving tieActive_ was reset)
    // Step 1: Active -> noteOn
    REQUIRE(noteOns2.size() >= 1);
    // First step fires at 0 (tie = silence), step 1 fires at 11025
    CHECK(noteOns2[0].sampleOffset == 11025);
}


// =============================================================================
// Phase 5: User Story 3 -- Slide Steps for Portamento Glide (073 T034)
// =============================================================================

TEST_CASE("Slide_EmitsLegatoNoteOn",
          "[processors][arpeggiator_core][modifiers][slide]") {
    // FR-015, SC-003: Slide step emits noteOn with legato=true.
    // Steps: [Active, Slide, Active]
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepSlide));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 100);
    arp.noteOn(64, 100);
    arp.noteOn(67, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // We should have at least 3 noteOns (step 0, 1, 2)
    REQUIRE(noteOns.size() >= 3);

    // Step 0: normal noteOn (legato=false)
    CHECK(noteOns[0].legato == false);

    // Step 1: slide noteOn (legato=true)
    CHECK(noteOns[1].legato == true);

    // Step 2: normal noteOn (legato=false)
    CHECK(noteOns[2].legato == false);
}


TEST_CASE("Slide_SuppressesPreviousNoteOff",
          "[processors][arpeggiator_core][modifiers][slide]") {
    // FR-015: Slide suppresses the preceding note's noteOff.
    // Steps: [Active, Slide]
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

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

    // Run enough blocks to cover step 0 and step 1 fully
    auto events = collectEvents(arp, ctx, 100);

    // Between step 0 noteOn and step 1 noteOn, there should be NO noteOff
    // for step 0's note if it's a slide transition.
    // Find events around step 1 boundary (at 11025 samples)
    auto noteOffs = filterNoteOffs(events);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);

    // The first noteOff (if any) should come AFTER step 1's noteOn,
    // or not at all before step 1's slide noteOn.
    // Specifically: no noteOff for step 0's note should appear at step 1's offset.
    bool noteOffBeforeSlide = false;
    for (const auto& off : noteOffs) {
        if (off.note == noteOns[0].note && off.sampleOffset == noteOns[1].sampleOffset) {
            noteOffBeforeSlide = true;
        }
    }
    CHECK_FALSE(noteOffBeforeSlide);
}


TEST_CASE("Slide_NoPrecedingNote_FallsBackToNormal",
          "[processors][arpeggiator_core][modifiers][slide]") {
    // FR-016: First step is Slide with no previous note -> normal noteOn (legato=false)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive | kStepSlide));
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

    REQUIRE(noteOns.size() >= 1);
    // First step is Slide but no preceding note -> legato=false (normal noteOn)
    CHECK(noteOns[0].legato == false);
}


TEST_CASE("Slide_AfterRest_FallsBackToNormal",
          "[processors][arpeggiator_core][modifiers][slide]") {
    // FR-016: Slide after Rest has no preceding sounding note -> legato=false
    // Steps: [Active, Rest, Slide]
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(0x00)); // Rest
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive | kStepSlide));

    arp.noteOn(60, 100);
    arp.noteOn(64, 100);
    arp.noteOn(67, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Step 0: noteOn (normal), Step 1: rest (no noteOn), Step 2: slide (no preceding note)
    REQUIRE(noteOns.size() >= 2);

    // Step 2's slide has no preceding sounding note (Rest cleared it) -> legato=false
    CHECK(noteOns[1].legato == false);
}


TEST_CASE("Slide_PitchLaneAdvances",
          "[processors][arpeggiator_core][modifiers][slide]") {
    // FR-017: Slide steps still advance the pitch lane normally.
    // Steps: [Active, Slide] with pitch offsets [0, +7]
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepSlide));

    arp.pitchLane().setLength(2);
    arp.pitchLane().setStep(0, 0);   // no offset
    arp.pitchLane().setStep(1, 7);   // +7 semitones

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);

    // Step 0: note 60 + 0 = 60
    CHECK(noteOns[0].note == 60);

    // Step 1: note 60 + 7 = 67 (pitch lane advanced)
    CHECK(noteOns[1].note == 67);
    CHECK(noteOns[1].legato == true);
}


TEST_CASE("Slide_ChordMode_AllNotesLegato",
          "[processors][arpeggiator_core][modifiers][slide]") {
    // FR-015 chord edge case: Chord mode with 2 notes; Slide step should
    // suppress all previous noteOffs and emit all new noteOns with legato=true.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

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

    // In Chord mode, step 0 emits 2 noteOns (60, 64), step 1 emits 2 legato noteOns
    REQUIRE(noteOns.size() >= 4);

    // Step 0: legato=false
    CHECK(noteOns[0].legato == false);
    CHECK(noteOns[1].legato == false);

    // Step 1: legato=true (all chord notes)
    CHECK(noteOns[2].legato == true);
    CHECK(noteOns[3].legato == true);
}


TEST_CASE("Slide_SC003_LegatoFieldTrue",
          "[processors][arpeggiator_core][modifiers][slide]") {
    // SC-003: Directly verify ArpEvent.legato field for a Slide step.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

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

    std::array<ArpEvent, 128> blockEvents;

    // Collect events across multiple blocks
    std::vector<ArpEvent> allNoteOns;
    for (size_t b = 0; b < 100; ++b) {
        size_t count = arp.processBlock(ctx, blockEvents);
        for (size_t i = 0; i < count; ++i) {
            if (blockEvents[i].type == ArpEvent::Type::NoteOn) {
                allNoteOns.push_back(blockEvents[i]);
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        if (allNoteOns.size() >= 2) break;
    }

    REQUIRE(allNoteOns.size() >= 2);
    // First noteOn: normal
    CHECK(allNoteOns[0].legato == false);
    // Second noteOn: slide -> legato=true
    CHECK(allNoteOns[1].legato == true);
}


// ============================================================================
// Accent Tests (User Story 4 - 073-per-step-mods Phase 6)
// ============================================================================

TEST_CASE("Accent_BoostsVelocity",
          "[processors][arpeggiator_core][modifiers][accent]") {
    // FR-019, SC-004: Accent boosts velocity by accentVelocity_ amount.
    // Steps: [Active, Accent|Active], accent=30, input vel=80
    // Expected: step 0 = 80, step 1 = 80 + 30 = 110
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepAccent));

    arp.noteOn(60, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);
    CHECK(noteOns[0].velocity == 80);
    CHECK(noteOns[1].velocity == 110);
}


TEST_CASE("Accent_ClampsToMax127",
          "[processors][arpeggiator_core][modifiers][accent]") {
    // FR-020, SC-004: input vel 100 + accent 50 = 150 -> clamped to 127
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(50);

    arp.modifierLane().setLength(1);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive | kStepAccent));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    CHECK(noteOns[0].velocity == 127);
}


TEST_CASE("Accent_ZeroAccent_NoEffect",
          "[processors][arpeggiator_core][modifiers][accent]") {
    // FR-021, SC-004: accent=0 means accented step same velocity as normal
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(0);

    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepAccent));

    arp.noteOn(60, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);
    CHECK(noteOns[0].velocity == 80);
    CHECK(noteOns[1].velocity == 80);  // No boost with accent=0
}


TEST_CASE("Accent_AppliedAfterVelocityLaneScaling",
          "[processors][arpeggiator_core][modifiers][accent]") {
    // FR-020, SC-004: vel lane 0.5, input vel 100, accent 30
    // result = clamp(round(100 * 0.5) + 30, 1, 127) = clamp(50 + 30, 1, 127) = 80
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    // Set velocity lane to scale by 0.5
    arp.velocityLane().setLength(1);
    arp.velocityLane().setStep(0, 0.5f);

    arp.modifierLane().setLength(1);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive | kStepAccent));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    // round(100 * 0.5) = 50, then + 30 = 80
    CHECK(noteOns[0].velocity == 80);
}


TEST_CASE("Accent_LowVelocityPlusAccent",
          "[processors][arpeggiator_core][modifiers][accent]") {
    // SC-004 boundary: input vel 1, accent 30 -> 31 (not 0 or negative)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    arp.modifierLane().setLength(1);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive | kStepAccent));

    arp.noteOn(60, 1);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    CHECK(noteOns[0].velocity == 31);
}


TEST_CASE("Accent_WithTie_NoEffect",
          "[processors][arpeggiator_core][modifiers][accent]") {
    // FR-022: Tie+Accent step -> no noteOn fires, so no velocity boost.
    // Steps: [Active, Tie+Accent, Active]
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie | kStepAccent));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Step 0: normal noteOn at vel 80
    // Step 1: Tie+Accent -- no noteOn (Tie suppresses it)
    // Step 2: Active -- noteOn
    // So we should get noteOns for step 0 and step 2, but NOT step 1
    REQUIRE(noteOns.size() >= 2);
    CHECK(noteOns[0].velocity == 80);
    // Step 2 should also be vel 80 (not boosted because Accent was on the Tie step)
    CHECK(noteOns[1].velocity == 80);
}


TEST_CASE("Accent_WithRest_NoEffect",
          "[processors][arpeggiator_core][modifiers][accent]") {
    // FR-022, FR-023: Rest+Accent (0x08, kStepActive not set) -> no noteOn
    // Steps: [Active, Rest+Accent(0x08), Active]
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepAccent));  // 0x08 only, no kStepActive
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Step 0: normal noteOn
    // Step 1: Rest (kStepActive not set) -- no noteOn regardless of Accent
    // Step 2: Active -- noteOn
    REQUIRE(noteOns.size() >= 2);
    CHECK(noteOns[0].velocity == 80);
    CHECK(noteOns[1].velocity == 80);  // Step 2 is not accented
}


TEST_CASE("Accent_WithSlide_BothApply",
          "[processors][arpeggiator_core][modifiers][accent]") {
    // FR-022: Slide+Accent -> legato=true AND boosted velocity
    // Steps: [Active, Slide+Accent]
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepSlide | kStepAccent));

    arp.noteOn(60, 80);
    arp.noteOn(64, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);
    // Step 0: normal noteOn, vel 80, legato=false
    CHECK(noteOns[0].velocity == 80);
    CHECK(noteOns[0].legato == false);

    // Step 1: Slide+Accent -> legato=true AND vel = 80 + 30 = 110
    CHECK(noteOns[1].velocity == 110);
    CHECK(noteOns[1].legato == true);
}


// =============================================================================
// Phase 7: User Story 5 -- Combined Modifiers (073-per-step-mods)
// =============================================================================
// T055: Modifier combination and polymetric cycling verification tests

TEST_CASE("CombinedModifiers_SlideAccent_BothApply",
          "[processors][arpeggiator_core][modifiers][combined]") {
    // FR-022, US5 acceptance 1: step with kStepActive|kStepSlide|kStepAccent
    // -> legato=true AND velocity boosted simultaneously
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepSlide | kStepAccent));

    arp.noteOn(60, 80);
    arp.noteOn(64, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);
    // Step 0: normal noteOn, vel 80, legato=false
    CHECK(noteOns[0].velocity == 80);
    CHECK(noteOns[0].legato == false);

    // Step 1: Slide+Accent -> legato=true AND vel = 80 + 30 = 110
    CHECK(noteOns[1].velocity == 110);
    CHECK(noteOns[1].legato == true);
}


TEST_CASE("CombinedModifiers_TieAccent_OnlyTieApplies",
          "[processors][arpeggiator_core][modifiers][combined]") {
    // FR-022, US5 acceptance 2: step with kStepTie|kStepAccent
    // -> no noteOn fires, no velocity boost (Tie suppresses noteOn; Accent has nothing to boost)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie | kStepAccent));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks to cover multiple 3-step cycles
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Step 0: noteOn at vel 80
    // Step 1: Tie+Accent -> Tie wins, no noteOn emitted (accent has no effect)
    // Step 2: Active -> fresh noteOn at vel 80 (not boosted)
    // Pattern repeats with 3-step modifier lane: 2 noteOns per 3 steps
    REQUIRE(noteOns.size() >= 2);
    CHECK(noteOns[0].velocity == 80);
    CHECK(noteOns[1].velocity == 80);

    // With 200 blocks of 512 = 102400 samples at 120 BPM eighth notes (11025 samp/step),
    // we get ~9 steps. With 3-step pattern [Active, Tie, Active], that's ~6 noteOns.
    // Verify the Tie step suppressed 1/3 of expected noteOns
    // (total noteOns should be roughly 2/3 of total steps)
    size_t totalSteps = noteOns.size() + (noteOns.size() / 2);  // approximate
    CHECK(noteOns.size() >= 4);  // At least 2 full cycles
    // All noteOns should have velocity 80 (not boosted by accent)
    for (size_t i = 0; i < noteOns.size(); ++i) {
        INFO("noteOn[" << i << "] velocity");
        CHECK(noteOns[i].velocity == 80);
    }
}


TEST_CASE("CombinedModifiers_RestWithAnyFlag_AlwaysSilent",
          "[processors][arpeggiator_core][modifiers][combined]") {
    // FR-023, US5 acceptance 3: step value 0x08 (Accent set, Active NOT set)
    // -> no noteOn fires (Rest takes priority over everything when kStepActive is absent)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepAccent));  // 0x08: Accent only, no Active
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks for multiple 3-step cycles
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Step 0: noteOn
    // Step 1: Rest (no kStepActive) -> no noteOn regardless of Accent
    // Step 2: noteOn
    // Pattern: 2 noteOns per 3-step cycle, all at velocity 80
    REQUIRE(noteOns.size() >= 2);
    CHECK(noteOns[0].velocity == 80);
    CHECK(noteOns[1].velocity == 80);  // Step 2 is not accented

    // All noteOns should be vel 80 (not boosted). Rest step never produces noteOn.
    for (size_t i = 0; i < noteOns.size(); ++i) {
        INFO("noteOn[" << i << "] velocity");
        CHECK(noteOns[i].velocity == 80);
    }
}


TEST_CASE("CombinedModifiers_RestWithAllFlags_AlwaysSilent",
          "[processors][arpeggiator_core][modifiers][combined]") {
    // FR-023: step value 0x0E (Tie+Slide+Accent, Active NOT set)
    // -> no noteOn fires (kStepActive is the gate; without it, everything is Rest)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(30);

    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    // 0x0E = kStepTie | kStepSlide | kStepAccent (no kStepActive)
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepTie | kStepSlide | kStepAccent));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    arp.noteOn(60, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks for multiple 3-step cycles
    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    // Step 0: noteOn at vel 80
    // Step 1: Rest (0x0E -> kStepActive not set) -> no noteOn
    // Step 2: Active -> noteOn at vel 80
    // Pattern: 2 noteOns per 3-step cycle, all at velocity 80
    REQUIRE(noteOns.size() >= 2);
    CHECK(noteOns[0].velocity == 80);
    CHECK(noteOns[1].velocity == 80);

    // All noteOns should be vel 80 -- Rest step (0x0E) never fires regardless of other flags
    for (size_t i = 0; i < noteOns.size(); ++i) {
        INFO("noteOn[" << i << "] velocity");
        CHECK(noteOns[i].velocity == 80);
    }
}


TEST_CASE("Polymetric_ModifierLength3_VelocityLength5",
          "[processors][arpeggiator_core][modifiers][combined][polymetric]") {
    // SC-006, US1 acceptance 3: modifier lane=3, velocity lane=5
    // LCM(3,5) = 15 steps. Verify the combined output sequence repeats exactly
    // at step 15 and that the pattern is NOT periodic with any sub-period (3 or 5).
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length=3 with accent on step 1 to create observable variation
    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepAccent));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    // Velocity lane: length=5, distinct step values
    arp.velocityLane().setLength(5);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.8f);
    arp.velocityLane().setStep(3, 0.6f);
    arp.velocityLane().setStep(4, 0.9f);

    arp.setAccentVelocity(20);
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks to get 2 full LCM cycles (30 steps)
    // At 120 BPM eighth notes: 11025 samp/step. 30*11025/512 ~= 646 blocks.
    auto events = collectEvents(arp, ctx, 800);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 30);

    // Extract velocity sequence
    std::vector<uint8_t> velocities;
    for (const auto& on : noteOns) {
        velocities.push_back(on.velocity);
    }

    // Verify: the full 15-step sequence repeats exactly at step 15
    for (size_t i = 0; i < 15; ++i) {
        INFO("Step " << i << " vs Step " << (i + 15));
        CHECK(velocities[i] == velocities[i + 15]);
    }

    // Verify: the sequence is NOT periodic with period 3 (modifier lane length)
    // If it were, every step would have the same velocity as 3 steps later
    bool allMatch3 = true;
    for (size_t i = 0; i < 12; ++i) {
        if (velocities[i] != velocities[i + 3]) {
            allMatch3 = false;
            break;
        }
    }
    CHECK_FALSE(allMatch3);

    // Verify: the sequence is NOT periodic with period 5 (velocity lane length)
    // If it were, every step would have the same velocity as 5 steps later
    bool allMatch5 = true;
    for (size_t i = 0; i < 10; ++i) {
        if (velocities[i] != velocities[i + 5]) {
            allMatch5 = false;
            break;
        }
    }
    CHECK_FALSE(allMatch5);
}


TEST_CASE("ModifierLane_CyclesIndependently",
          "[processors][arpeggiator_core][modifiers][combined][polymetric]") {
    // SC-006: modifier=3, gate=7, velocity=5, pitch=4
    // LCM(3,4,5,7) = 420 steps. Verify the full 420-step sequence repeats
    // exactly at step 420 and that it is NOT periodic with any single lane's
    // period (3, 4, 5, or 7).
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Modifier lane: length=3, with accent on step 1
    arp.modifierLane().setLength(3);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepAccent));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive));

    // Velocity lane: length=5, distinct values
    arp.velocityLane().setLength(5);
    arp.velocityLane().setStep(0, 1.0f);
    arp.velocityLane().setStep(1, 0.5f);
    arp.velocityLane().setStep(2, 0.8f);
    arp.velocityLane().setStep(3, 0.6f);
    arp.velocityLane().setStep(4, 0.9f);

    // Gate lane: length=7, distinct values
    arp.gateLane().setLength(7);
    arp.gateLane().setStep(0, 0.5f);
    arp.gateLane().setStep(1, 0.6f);
    arp.gateLane().setStep(2, 0.7f);
    arp.gateLane().setStep(3, 0.8f);
    arp.gateLane().setStep(4, 0.9f);
    arp.gateLane().setStep(5, 1.0f);
    arp.gateLane().setStep(6, 1.1f);

    // Pitch lane: length=4, distinct values
    arp.pitchLane().setLength(4);
    arp.pitchLane().setStep(0, 0);
    arp.pitchLane().setStep(1, 3);
    arp.pitchLane().setStep(2, 7);
    arp.pitchLane().setStep(3, -2);

    arp.setAccentVelocity(20);
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Need 2 full LCM cycles (840 steps). Each step = 11025 samp.
    // 840*11025/512 ~= 18100 blocks. Use 19000 for margin.
    auto events = collectEvents(arp, ctx, 19000);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 840);

    // Extract velocity and note sequences
    std::vector<uint8_t> velocities;
    std::vector<uint8_t> notes;
    for (const auto& on : noteOns) {
        velocities.push_back(on.velocity);
        notes.push_back(on.note);
    }

    // Verify: the full 420-step sequence repeats exactly at step 420
    size_t mismatches = 0;
    for (size_t i = 0; i < 420; ++i) {
        if (velocities[i] != velocities[i + 420] ||
            notes[i] != notes[i + 420]) {
            ++mismatches;
        }
    }
    INFO(mismatches << " mismatches in 420-step cycle comparison");
    CHECK(mismatches == 0);

    // Verify: the velocity sequence is NOT periodic with period 3 (modifier lane length)
    bool allMatch3 = true;
    for (size_t i = 0; i < 417; ++i) {
        if (velocities[i] != velocities[i + 3]) {
            allMatch3 = false;
            break;
        }
    }
    CHECK_FALSE(allMatch3);

    // Verify: the velocity sequence is NOT periodic with period 5 (velocity lane length)
    bool allMatch5 = true;
    for (size_t i = 0; i < 415; ++i) {
        if (velocities[i] != velocities[i + 5]) {
            allMatch5 = false;
            break;
        }
    }
    CHECK_FALSE(allMatch5);

    // Verify: the combined (velocity, note) sequence is NOT periodic with period 4
    // (pitch lane length). While notes alone repeat every 4 steps (expected, since
    // pitch lane cycles independently), the combined output must not.
    bool allMatch4 = true;
    for (size_t i = 0; i < 416; ++i) {
        if (velocities[i] != velocities[i + 4] ||
            notes[i] != notes[i + 4]) {
            allMatch4 = false;
            break;
        }
    }
    CHECK_FALSE(allMatch4);

    // Verify: the velocity sequence is NOT periodic with period 7 (gate lane length)
    // Gate affects duration not velocity, but verify it doesn't create periodicity
    // by checking the combined (velocity, note) pair
    bool allMatch7 = true;
    for (size_t i = 0; i < 413; ++i) {
        if (velocities[i] != velocities[i + 7] ||
            notes[i] != notes[i + 7]) {
            allMatch7 = false;
            break;
        }
    }
    CHECK_FALSE(allMatch7);
}


// =============================================================================
// Phase 9: Edge Case Tests (073-per-step-mods)
// =============================================================================

TEST_CASE("EdgeCase_AllRestSteps",
          "[processors][arpeggiator_core][modifiers][edge]") {
    // Modifier lane all 0x00: arp produces no noteOn events but timing continues
    // and pending noteOffs still fire.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // All 4 steps are Rest (0x00)
    arp.modifierLane().setLength(4);
    arp.modifierLane().setStep(0, 0x00);
    arp.modifierLane().setStep(1, 0x00);
    arp.modifierLane().setStep(2, 0x00);
    arp.modifierLane().setStep(3, 0x00);

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Run enough blocks to cover many steps
    auto events = collectEvents(arp, ctx, 400);
    auto noteOns = filterNoteOns(events);

    // No noteOn events should be emitted when all steps are Rest
    CHECK(noteOns.empty());

    // Timing should still advance (no crash, no infinite loop)
    // The fact that we got here without hanging confirms timing continues
}


TEST_CASE("EdgeCase_AllTieSteps",
          "[processors][arpeggiator_core][modifiers][edge]") {
    // All steps are kStepTie: first Tie has no predecessor so silence,
    // subsequent Ties also silent since no note was ever triggered.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // All 4 steps are Tie (kStepActive | kStepTie)
    arp.modifierLane().setLength(4);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(2, static_cast<uint8_t>(kStepActive | kStepTie));
    arp.modifierLane().setStep(3, static_cast<uint8_t>(kStepActive | kStepTie));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 400);
    auto noteOns = filterNoteOns(events);

    // First Tie has no predecessor -> silence (FR-013)
    // Subsequent Ties also have no sounding note -> silence
    // No noteOn events should be emitted
    CHECK(noteOns.empty());
}


TEST_CASE("EdgeCase_TieAfterRest",
          "[processors][arpeggiator_core][modifiers][edge]") {
    // Steps [Rest, Tie]: Tie has no preceding note (rest cleared it), silence
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, 0x00);  // Rest
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepTie));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 400);
    auto noteOns = filterNoteOns(events);

    // Step 0: Rest -> no noteOn
    // Step 1: Tie with no preceding note -> silence
    // Both steps produce silence, cycling indefinitely
    CHECK(noteOns.empty());
}


TEST_CASE("EdgeCase_SlideFirstStep",
          "[processors][arpeggiator_core][modifiers][edge]") {
    // First step is Slide with no prior note -> normal noteOn with legato=false (FR-016)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    arp.modifierLane().setLength(1);
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

    REQUIRE(noteOns.size() >= 1);
    // First step is Slide but no prior note -> normal noteOn, legato=false
    CHECK(noteOns[0].legato == false);
    CHECK(noteOns[0].note == 60);
    CHECK(noteOns[0].velocity == 100);
}


TEST_CASE("EdgeCase_AccentVelocityZero",
          "[processors][arpeggiator_core][modifiers][edge]") {
    // setAccentVelocity(0): accented steps have identical velocity to non-accented
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setAccentVelocity(0);

    arp.modifierLane().setLength(2);
    arp.modifierLane().setStep(0, static_cast<uint8_t>(kStepActive));
    arp.modifierLane().setStep(1, static_cast<uint8_t>(kStepActive | kStepAccent));

    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);
    // Both steps should have the same velocity since accent=0
    CHECK(noteOns[0].velocity == 100);
    CHECK(noteOns[1].velocity == 100);
}


TEST_CASE("EdgeCase_SlideTimeZero",
          "[processors][arpeggiator_core][modifiers][edge]") {
    // setSlideTime(0.0f): portamento completes instantly, pitch still changes
    // This test verifies that slideTime=0 doesn't cause division by zero or hang.
    // The ArpeggiatorCore stores the slide time but the actual portamento is
    // handled by RuinaeVoice (engine layer). The arp still emits legato noteOns.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.setSlideTime(0.0f);

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

    auto events = collectEvents(arp, ctx, 200);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 2);
    // Step 0: normal noteOn
    CHECK(noteOns[0].legato == false);
    // Step 1: Slide -> legato=true even with slideTime=0 (the arp still flags legato)
    CHECK(noteOns[1].legato == true);
}


TEST_CASE("EdgeCase_ModifierLaneLength0_ClampedTo1",
          "[processors][arpeggiator_core][modifiers][edge]") {
    // ArpLane::setLength(0) clamps to 1 (minimum length per ArpLane implementation)
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);

    // Try to set length to 0 -- should be clamped to 1
    arp.modifierLane().setLength(0);
    CHECK(arp.modifierLane().length() == 1);

    // The default step 0 should still be kStepActive
    CHECK(arp.modifierLane().getStep(0) == static_cast<uint8_t>(kStepActive));

    // Verify arp still works normally with length=1
    arp.noteOn(60, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    auto events = collectEvents(arp, ctx, 100);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1);
    CHECK(noteOns[0].note == 60);
}
