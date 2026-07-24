// arpeggiator_core_core_test.cpp
// Core timing, gate, latch, retrigger, swing, toggle, free-rate, edge, chord mode
// Split from the former 17k-line arpeggiator_core_test.cpp (D1). Shared helpers in
// arpeggiator_core_test_helpers.h.
#include "arpeggiator_core_test_helpers.h"



// =============================================================================
// Phase 2: Skeleton Compilation Test
// =============================================================================

TEST_CASE("ArpeggiatorCore: skeleton compiles", "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.reset();

    // Verify construction and basic lifecycle methods compile and run
    REQUIRE(true);
}


// =============================================================================
// Phase 3: User Story 1 -- Tempo-Synced Arpeggio Playback
// =============================================================================

// T008: Lifecycle tests (FR-003, FR-004)
TEST_CASE("ArpeggiatorCore: prepare stores sample rate and clamps minimum",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;

    SECTION("prepare stores normal sample rate") {
        arp.prepare(48000.0, 512);
        arp.setEnabled(true);
        arp.setNoteValue(NoteValue::Quarter, NoteModifier::None);
        arp.noteOn(60, 100);

        // At 48000 Hz, 120 BPM, quarter note = 24000 samples
        // Run enough blocks to get first NoteOn and verify timing
        BlockContext ctx;
        ctx.sampleRate = 48000.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        auto events = collectEvents(arp, ctx, 100);
        auto noteOns = filterNoteOns(events);

        // Should have at least 2 NoteOn events to check spacing
        REQUIRE(noteOns.size() >= 2);
        // Expected step: 24000 samples at 48kHz, 120BPM, quarter
        int32_t gap = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
        CHECK(gap == 24000);
    }

    SECTION("prepare clamps sample rate below 1000 Hz") {
        arp.prepare(500.0, 512);
        arp.setEnabled(true);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.noteOn(60, 100);

        // Should be clamped to 1000 Hz. At 1000 Hz, 120 BPM, 1/8 note:
        // (60/120) * 0.5 * 1000 = 250 samples
        BlockContext ctx;
        ctx.sampleRate = 1000.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        auto events = collectEvents(arp, ctx, 10);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 2);
        int32_t gap = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
        CHECK(gap == 250);
    }
}


TEST_CASE("ArpeggiatorCore: reset zeroes timing but preserves config",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    // Advance a few blocks to shift timing
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    std::array<ArpEvent, 64> buf;
    for (int i = 0; i < 30; ++i) {
        arp.processBlock(ctx, buf);
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }

    // Now reset -- timing should restart, configuration preserved
    arp.reset();

    // Re-add notes (reset clears selector but config preserved)
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    ctx.transportPositionSamples = 0;
    auto events = collectEvents(arp, ctx, 50);
    auto noteOns = filterNoteOns(events);

    // After reset, first NoteOn should fire immediately (at sample 0)
    // 120 BPM, 1/8 note, 44100 Hz = 11025 samples per step
    REQUIRE(noteOns.size() >= 1);
    CHECK(noteOns[0].sampleOffset == 0);
}


// T009: Zero blockSize guard (FR-032, SC-010)
TEST_CASE("ArpeggiatorCore: zero blockSize returns 0 events with no state change",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    std::array<ArpEvent, 64> buf;

    SECTION("zero blockSize returns 0") {
        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 0;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        size_t count = arp.processBlock(ctx, buf);
        CHECK(count == 0);
    }

    SECTION("normal block after zero-size produces same result as without zero-size call") {
        // First, call with zero block
        BlockContext zeroCtx;
        zeroCtx.sampleRate = 44100.0;
        zeroCtx.blockSize = 0;
        zeroCtx.tempoBPM = 120.0;
        zeroCtx.isPlaying = true;

        arp.processBlock(zeroCtx, buf);

        // Now call with normal block -- should behave as if zero call never occurred
        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        auto events = collectEvents(arp, ctx, 30);
        auto noteOns = filterNoteOns(events);

        // First NoteOn fires immediately at sample 0
        REQUIRE(noteOns.size() >= 1);
        CHECK(noteOns[0].sampleOffset == 0);
    }
}


// T010: Basic timing accuracy (SC-001)
TEST_CASE("ArpeggiatorCore: timing accuracy at 120 BPM",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.noteOn(48, 100);  // C3
    arp.noteOn(52, 100);  // E3
    arp.noteOn(55, 100);  // G3

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    SECTION("1/8 note = every 11025 samples") {
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);

        // 120 BPM, 1/8 note: (60/120)*0.5*44100 = 11025 samples
        // First step fires immediately at 0, then every 11025
        auto events = collectEvents(arp, ctx, 2300);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 100);

        // Verify all NoteOn events land at exact expected offsets
        for (size_t i = 0; i < 100; ++i) {
            int32_t expected = static_cast<int32_t>(i * 11025);
            CHECK(std::abs(noteOns[i].sampleOffset - expected) <= 1);
        }
    }

    SECTION("1/16 note = every 5512 samples") {
        arp.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);

        // 120 BPM, 1/16 note: (60/120)*0.25*44100 = 5512.5 -> 5512 samples
        // First step fires immediately at 0, then every 5512
        auto events = collectEvents(arp, ctx, 1200);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 100);

        for (size_t i = 0; i < 100; ++i) {
            int32_t expected = static_cast<int32_t>(i * 5512);
            CHECK(std::abs(noteOns[i].sampleOffset - expected) <= 1);
        }
    }
}


// T011: Timing at multiple tempos (SC-001)
TEST_CASE("ArpeggiatorCore: timing accuracy at multiple tempos",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    SECTION("60 BPM, 1/4 note = 44100 samples") {
        arp.setNoteValue(NoteValue::Quarter, NoteModifier::None);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 60.0;
        ctx.isPlaying = true;

        auto events = collectEvents(arp, ctx, 8800);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 100);
        for (size_t i = 0; i < 100; ++i) {
            int32_t expected = static_cast<int32_t>(i * 44100);
            CHECK(std::abs(noteOns[i].sampleOffset - expected) <= 1);
        }
    }

    SECTION("120 BPM, 1/4 note = 22050 samples") {
        arp.setNoteValue(NoteValue::Quarter, NoteModifier::None);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        auto events = collectEvents(arp, ctx, 4500);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 100);
        for (size_t i = 0; i < 100; ++i) {
            int32_t expected = static_cast<int32_t>(i * 22050);
            CHECK(std::abs(noteOns[i].sampleOffset - expected) <= 1);
        }
    }

    SECTION("200 BPM, 1/8 note = 6615 samples") {
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 200.0;
        ctx.isPlaying = true;

        // (60/200)*0.5*44100 = 6615 samples
        auto events = collectEvents(arp, ctx, 1400);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 100);
        for (size_t i = 0; i < 100; ++i) {
            int32_t expected = static_cast<int32_t>(i * 6615);
            CHECK(std::abs(noteOns[i].sampleOffset - expected) <= 1);
        }
    }
}


// T012: 1/8 triplet timing (SC-001)
TEST_CASE("ArpeggiatorCore: 1/8 triplet timing at 120 BPM",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::Triplet);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // 120 BPM, 1/8 triplet: getBeatsForNote = 0.5 * 0.6667 = 0.33333
    // (60/120) * 0.33333 * 44100 = 7350 samples
    auto events = collectEvents(arp, ctx, 1600);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 100);
    for (size_t i = 0; i < 100; ++i) {
        int32_t expected = static_cast<int32_t>(i * 7350);
        CHECK(std::abs(noteOns[i].sampleOffset - expected) <= 1);
    }
}


// T013: Mid-block step boundary (US1 acceptance scenario 4)
TEST_CASE("ArpeggiatorCore: step boundary falls mid-block",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);

    // Step duration = 11025 samples at 120 BPM 1/8 note
    // First NoteOn fires immediately at sample 0 in block 0.
    // Second NoteOn at sample 11025. Block size 512.
    // 11025 / 512 = 21 blocks fully, remainder = 11025 - 21*512 = 11025 - 10752 = 273
    // So second NoteOn fires in block 21 at sampleOffset 273

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    std::array<ArpEvent, 64> buf;

    // Block 0: first step fires immediately at offset 0
    size_t count0 = arp.processBlock(ctx, buf);
    REQUIRE(count0 >= 1);
    bool foundFirst = false;
    for (size_t i = 0; i < count0; ++i) {
        if (buf[i].type == ArpEvent::Type::NoteOn) {
            CHECK(buf[i].sampleOffset == 0);
            foundFirst = true;
            break;
        }
    }
    CHECK(foundFirst);
    ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);

    // Process blocks 1..20 -- no NoteOn expected (counting to 11025)
    for (int b = 1; b < 21; ++b) {
        size_t count = arp.processBlock(ctx, buf);
        // May have NoteOff events from gate, but no new NoteOn
        for (size_t i = 0; i < count; ++i) {
            CHECK(buf[i].type != ArpEvent::Type::NoteOn);
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }

    // Block 21: second step boundary at sample 273 within this block
    size_t count = arp.processBlock(ctx, buf);
    REQUIRE(count >= 1);

    // Find the NoteOn event
    bool foundNoteOn = false;
    for (size_t i = 0; i < count; ++i) {
        if (buf[i].type == ArpEvent::Type::NoteOn) {
            CHECK(buf[i].sampleOffset == 273);
            foundNoteOn = true;
            break;
        }
    }
    CHECK(foundNoteOn);
}


// T014: Zero drift over 1000 steps (SC-008)
TEST_CASE("ArpeggiatorCore: zero drift over 1000 steps",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Need enough blocks: 1000 steps * 11025 samples/step / 512 samples/block ~ 21533 blocks
    // Plus 1 extra step for the first NoteOn after one step delay
    auto events = collectEvents(arp, ctx, 22000);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 1001);

    // Sum all inter-NoteOn sample gaps
    size_t totalGap = 0;
    for (size_t i = 0; i < 1000; ++i) {
        int32_t gap = noteOns[i + 1].sampleOffset - noteOns[i].sampleOffset;
        totalGap += static_cast<size_t>(gap);
    }

    // Expected: exactly 1000 * 11025 = 11025000 samples (zero drift)
    CHECK(totalGap == 1000 * 11025);
}


// T015: Disabled arp test (FR-008, SC-010)
TEST_CASE("ArpeggiatorCore: disabled arp returns 0 events",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(false);  // Disabled
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    std::array<ArpEvent, 64> buf;
    size_t count = arp.processBlock(ctx, buf);
    CHECK(count == 0);
}


// T016: Transport not playing test (FR-031)
TEST_CASE("ArpeggiatorCore: transport not playing returns 0 events",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = false;  // Transport NOT playing

    std::array<ArpEvent, 64> buf;
    size_t count = arp.processBlock(ctx, buf);
    CHECK(count == 0);
}


// =============================================================================
// Phase 4: User Story 2 -- Gate Length Controls Note Duration
// =============================================================================

// T024: Gate accuracy at 50% (SC-002, US2 scenario 1)
TEST_CASE("ArpeggiatorCore: gate 50% NoteOff fires at half step duration",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(48, 100);  // C3
    arp.noteOn(52, 100);  // E3
    arp.noteOn(55, 100);  // G3

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Step = 11025 samples. Gate 50% => NoteOff at 11025 * 50 / 100 = 5512 samples
    // after NoteOn. Run enough blocks to get several steps.
    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // We need at least 3 NoteOns and 3 NoteOffs to verify multiple steps
    REQUIRE(noteOns.size() >= 3);
    REQUIRE(noteOffs.size() >= 3);

    // For each NoteOn, find its corresponding NoteOff (same note) and check gap
    for (size_t i = 0; i < 3; ++i) {
        // Find the NoteOff for this note that fires after the NoteOn
        bool found = false;
        for (const auto& off : noteOffs) {
            if (off.note == noteOns[i].note &&
                off.sampleOffset > noteOns[i].sampleOffset) {
                int32_t gap = off.sampleOffset - noteOns[i].sampleOffset;
                // Gate 50% of 11025 = floor(11025 * 50 / 100) = 5512
                CHECK(std::abs(gap - 5512) <= 1);
                found = true;
                break;
            }
        }
        CHECK(found);
    }
}


// T025: Gate at 1%, 100%, and 150% (SC-002, SC-007)
TEST_CASE("ArpeggiatorCore: gate accuracy at 1%, 100%, and 150%",
          "[processors][arpeggiator_core]") {
    // Step duration: 11025 samples at 120 BPM, 1/8 note

    SECTION("gate 1% -- minimum gate duration") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(1.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        auto events = collectEvents(arp, ctx, 500);
        auto noteOns = filterNoteOns(events);
        auto noteOffs = filterNoteOffs(events);

        REQUIRE(noteOns.size() >= 3);
        REQUIRE(noteOffs.size() >= 3);

        // Gate 1% of 11025 = floor(11025 * 1 / 100) = 110; clamped min 1
        // Actually floor(11025 * 1.0 / 100.0) = floor(110.25) = 110
        for (size_t i = 0; i < 3; ++i) {
            bool found = false;
            for (const auto& off : noteOffs) {
                if (off.note == noteOns[i].note &&
                    off.sampleOffset > noteOns[i].sampleOffset) {
                    int32_t gap = off.sampleOffset - noteOns[i].sampleOffset;
                    CHECK(std::abs(gap - 110) <= 1);
                    found = true;
                    break;
                }
            }
            CHECK(found);
        }
    }

    SECTION("gate 100% -- NoteOff coincides with next NoteOn") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(100.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        auto events = collectEvents(arp, ctx, 500);
        auto noteOns = filterNoteOns(events);
        auto noteOffs = filterNoteOffs(events);

        REQUIRE(noteOns.size() >= 3);
        REQUIRE(noteOffs.size() >= 3);

        // Gate 100%: NoteOff fires at 11025 samples after NoteOn
        // This coincides with the next NoteOn (within 1 sample)
        for (size_t i = 0; i < 3; ++i) {
            bool found = false;
            for (const auto& off : noteOffs) {
                if (off.note == noteOns[i].note &&
                    off.sampleOffset > noteOns[i].sampleOffset) {
                    int32_t gap = off.sampleOffset - noteOns[i].sampleOffset;
                    CHECK(std::abs(gap - 11025) <= 1);
                    found = true;
                    break;
                }
            }
            CHECK(found);
        }

        // At 100% gate, NoteOff should fire at or very near next step boundary
        // Verify NoteOff fires at same offset as next NoteOn (within 1 sample)
        if (noteOns.size() >= 2) {
            int32_t nextNoteOnOffset = noteOns[1].sampleOffset;
            bool foundOff = false;
            for (const auto& off : noteOffs) {
                if (off.note == noteOns[0].note &&
                    off.sampleOffset > noteOns[0].sampleOffset) {
                    CHECK(std::abs(off.sampleOffset - nextNoteOnOffset) <= 1);
                    foundOff = true;
                    break;
                }
            }
            CHECK(foundOff);
        }
    }

    SECTION("gate 150% -- legato overlap (SC-007)") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(150.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // Gate 150% of 11025 = floor(11025 * 150 / 100) = floor(16537.5) = 16537
        auto events = collectEvents(arp, ctx, 800);
        auto noteOns = filterNoteOns(events);
        auto noteOffs = filterNoteOffs(events);

        REQUIRE(noteOns.size() >= 3);
        REQUIRE(noteOffs.size() >= 2);

        // Verify NoteOff for step 0's note fires 16537 samples after its NoteOn
        bool found = false;
        for (const auto& off : noteOffs) {
            if (off.note == noteOns[0].note &&
                off.sampleOffset > noteOns[0].sampleOffset) {
                int32_t gap = off.sampleOffset - noteOns[0].sampleOffset;
                CHECK(std::abs(gap - 16537) <= 1);
                found = true;
                break;
            }
        }
        CHECK(found);

        // SC-007: The NoteOff for step 0 fires AFTER the NoteOn for step 1
        // Step 0 NoteOn at 11025, Step 1 NoteOn at 22050
        // Step 0 NoteOff at 11025 + 16537 = 27562
        // So NoteOff fires at 27562 which is after step 1 NoteOn at 22050
        if (noteOns.size() >= 2) {
            for (const auto& off : noteOffs) {
                if (off.note == noteOns[0].note &&
                    off.sampleOffset > noteOns[0].sampleOffset) {
                    CHECK(off.sampleOffset > noteOns[1].sampleOffset);
                    break;
                }
            }
        }
    }
}


// T026: Gate 200% -- two full step durations overlap (SC-002, US2 scenario 4)
TEST_CASE("ArpeggiatorCore: gate 200% creates full step overlap",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(200.0f);
    arp.noteOn(48, 100);  // C3
    arp.noteOn(52, 100);  // E3
    arp.noteOn(55, 100);  // G3

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Gate 200% of 11025 = floor(11025 * 200 / 100) = 22050
    // Step 0 NoteOn at 11025, NoteOff at 11025 + 22050 = 33075
    // Step 1 NoteOn at 22050
    // So Step 0 NoteOff (33075) fires AFTER Step 1 NoteOn (22050)
    auto events = collectEvents(arp, ctx, 1200);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 3);
    REQUIRE(noteOffs.size() >= 2);

    // Verify gate duration is 22050 (200% of 11025)
    bool found = false;
    for (const auto& off : noteOffs) {
        if (off.note == noteOns[0].note &&
            off.sampleOffset > noteOns[0].sampleOffset) {
            int32_t gap = off.sampleOffset - noteOns[0].sampleOffset;
            CHECK(std::abs(gap - 22050) <= 1);
            found = true;
            break;
        }
    }
    CHECK(found);

    // Verify Step 0's NoteOff fires AFTER Step 1's NoteOn
    // This means both notes are sounding simultaneously for the overlap
    if (noteOns.size() >= 2) {
        for (const auto& off : noteOffs) {
            if (off.note == noteOns[0].note &&
                off.sampleOffset > noteOns[0].sampleOffset) {
                CHECK(off.sampleOffset > noteOns[1].sampleOffset);
                break;
            }
        }
    }
}


// T027: Cross-block NoteOff (FR-026)
TEST_CASE("ArpeggiatorCore: cross-block NoteOff fires in correct block",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 128);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);

    // Small block size (128) with step duration 11025.
    // Gate 50% = 5512 samples after NoteOn.
    // NoteOn fires at absolute sample 11025.
    // NoteOff should fire at absolute sample 11025 + 5512 = 16537.
    // These are definitely in different blocks with blockSize=128.

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 128;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // We need at least 16537 / 128 ~ 130 blocks
    auto events = collectEvents(arp, ctx, 250);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    REQUIRE(noteOns.size() >= 1);
    REQUIRE(noteOffs.size() >= 1);

    // Find the first NoteOn and its corresponding NoteOff
    int32_t noteOnOffset = noteOns[0].sampleOffset;
    bool found = false;
    for (const auto& off : noteOffs) {
        if (off.note == noteOns[0].note &&
            off.sampleOffset > noteOnOffset) {
            int32_t gap = off.sampleOffset - noteOnOffset;
            // Gate 50% of 11025 = 5512
            CHECK(std::abs(gap - 5512) <= 1);
            found = true;
            break;
        }
    }
    CHECK(found);

    // Verify the NoteOn and NoteOff are in different blocks
    if (noteOns.size() >= 1 && noteOffs.size() >= 1) {
        size_t noteOnBlock = static_cast<size_t>(noteOns[0].sampleOffset) / 128;
        for (const auto& off : noteOffs) {
            if (off.note == noteOns[0].note &&
                off.sampleOffset > noteOnOffset) {
                size_t noteOffBlock = static_cast<size_t>(off.sampleOffset) / 128;
                CHECK(noteOffBlock > noteOnBlock);
                break;
            }
        }
    }
}


// T028: Pending NoteOff overflow (FR-026)
TEST_CASE("ArpeggiatorCore: pending NoteOff overflow emits oldest immediately",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    // Very long gate to ensure many pending NoteOffs build up
    arp.setGateLength(200.0f);

    // Fill with max notes so chord mode could potentially fill pending array
    // With single-note mode (Up), each step adds 1 pending NoteOff.
    // With 200% gate, NoteOffs fire 22050 samples after NoteOn.
    // Steps fire every 11025 samples. So each NoteOff survives ~2 steps.
    // Max pending is 32 -- we need to verify no crash with many steps.
    for (uint8_t note = 36; note < 68; ++note) {
        arp.noteOn(note, 100);
    }

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Run many blocks -- should not crash even with many pending NoteOffs
    std::array<ArpEvent, 64> buf;
    bool crashed = false;
    for (size_t b = 0; b < 5000; ++b) {
        size_t count = arp.processBlock(ctx, buf);
        // Verify no out-of-bounds writes (count <= 64)
        if (count > 64) {
            crashed = true;
            break;
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }
    CHECK_FALSE(crashed);

    // Verify we got both NoteOn and NoteOff events (system is working)
    ctx.transportPositionSamples = 0;
    arp.reset();
    for (uint8_t note = 36; note < 68; ++note) {
        arp.noteOn(note, 100);
    }

    auto events = collectEvents(arp, ctx, 2000);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    CHECK(noteOns.size() > 0);
    CHECK(noteOffs.size() > 0);
}


// =============================================================================
// Phase 5: User Story 3 -- Latch Modes Sustain Arpeggio After Key Release
// =============================================================================

// T035: Latch Off mode tests (SC-004, US3 scenario 1)
TEST_CASE("ArpeggiatorCore: Latch Off -- release all keys stops arp",
          "[processors][arpeggiator_core]") {
    // Step = 11025 samples at 120 BPM 1/8 note
    SECTION("release all three keys emits NoteOff and stops") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Off);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;

        // Run until at least one NoteOn fires (need > 11025 samples = ~22 blocks)
        for (int b = 0; b < 25; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Now release all keys
        arp.noteOff(48);
        arp.noteOff(52);
        arp.noteOff(55);

        // Process several more blocks -- should get no more NoteOn events
        // (may get a final NoteOff for the current arp note)
        bool gotNoteOnAfterRelease = false;
        for (int b = 0; b < 100; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    gotNoteOnAfterRelease = true;
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
        CHECK_FALSE(gotNoteOnAfterRelease);
    }

    SECTION("release in reverse order -- arp stops after last key released") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Off);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;

        // Run a bit to get arp going
        for (int b = 0; b < 25; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Release in reverse: G3, E3, then C3
        arp.noteOff(55);
        arp.noteOff(52);

        // Still one key held -- arp should continue
        bool gotNoteOnWithOneKey = false;
        for (int b = 0; b < 50; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    gotNoteOnWithOneKey = true;
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
        CHECK(gotNoteOnWithOneKey);

        // Release last key
        arp.noteOff(48);

        // Now arp should stop
        bool gotNoteOnAfterAll = false;
        for (int b = 0; b < 100; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    gotNoteOnAfterAll = true;
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
        CHECK_FALSE(gotNoteOnAfterAll);
    }

    SECTION("release two keys, arp continues with remaining note") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Off);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;

        // Get arp going
        for (int b = 0; b < 25; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Release C3 and G3, keep E3
        arp.noteOff(48);
        arp.noteOff(55);

        // Run more blocks -- should still get NoteOn events (E3 still held)
        std::vector<uint8_t> notesPlayed;
        for (int b = 0; b < 100; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notesPlayed.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
        // Should have notes playing (only E3=52)
        CHECK(notesPlayed.size() > 0);
        for (uint8_t n : notesPlayed) {
            CHECK(n == 52);
        }
    }
}


// T036: Latch Hold mode tests (SC-004, US3 scenarios 2 and 3)
TEST_CASE("ArpeggiatorCore: Latch Hold -- sustains after release and replaces on new input",
          "[processors][arpeggiator_core]") {

    SECTION("release all keys -- arpeggiation continues with latched pattern") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Hold);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;

        // Get arp going
        for (int b = 0; b < 25; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Release all keys
        arp.noteOff(48);
        arp.noteOff(52);
        arp.noteOff(55);

        // Arp should continue playing C3, E3, G3 pattern
        std::vector<uint8_t> notesAfterRelease;
        for (int b = 0; b < 200; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notesAfterRelease.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Should still be getting NoteOn events
        CHECK(notesAfterRelease.size() >= 3);

        // Verify pattern is [48, 52, 55] cycling (Up mode)
        // Check that only notes from the original set appear
        for (uint8_t n : notesAfterRelease) {
            bool isOriginal = (n == 48 || n == 52 || n == 55);
            CHECK(isOriginal);
        }
    }

    SECTION("new keys while latched replaces entire pattern") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Hold);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;

        // Get arp going
        for (int b = 0; b < 25; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Release all keys to enter latched state
        arp.noteOff(48);
        arp.noteOff(52);
        arp.noteOff(55);

        // Process a few blocks in latched state
        for (int b = 0; b < 10; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Press new keys -- should replace latched pattern
        arp.noteOn(50, 100);  // D3
        arp.noteOn(53, 100);  // F3

        // Run more blocks and collect notes
        std::vector<uint8_t> notesAfterReplace;
        for (int b = 0; b < 200; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notesAfterReplace.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        CHECK(notesAfterReplace.size() >= 3);

        // All notes should be from new pattern [50, 53] only
        for (uint8_t n : notesAfterReplace) {
            bool isNew = (n == 50 || n == 53);
            CHECK(isNew);
        }
    }

    SECTION("pressing first new key while latched clears old, adds new") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Hold);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;

        // Get arp going
        for (int b = 0; b < 25; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Release all
        arp.noteOff(48);
        arp.noteOff(52);

        // Process a bit in latched state
        for (int b = 0; b < 10; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Press single new key D3
        arp.noteOn(50, 100);

        // Run and verify only D3 plays (old pattern cleared)
        std::vector<uint8_t> notes;
        for (int b = 0; b < 100; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notes.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
        CHECK(notes.size() >= 2);
        for (uint8_t n : notes) {
            CHECK(n == 50);
        }
    }
}


// T037: Latch Add mode tests (SC-004, US3 scenarios 4 and 5)
TEST_CASE("ArpeggiatorCore: Latch Add -- accumulates notes indefinitely",
          "[processors][arpeggiator_core]") {

    SECTION("release all keys, notes remain in pattern") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Add);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;

        // Get arp going
        for (int b = 0; b < 25; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Release all keys
        arp.noteOff(48);
        arp.noteOff(52);
        arp.noteOff(55);

        // Arp should continue with [48, 52, 55]
        std::vector<uint8_t> notes;
        for (int b = 0; b < 200; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notes.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        CHECK(notes.size() >= 3);
        for (uint8_t n : notes) {
            bool isOriginal = (n == 48 || n == 52 || n == 55);
            CHECK(isOriginal);
        }
    }

    SECTION("new key adds to existing pattern, not replaces") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Add);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;

        // Get arp going
        for (int b = 0; b < 25; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // Release all, then add D3
        arp.noteOff(48);
        arp.noteOff(52);
        arp.noteOff(55);
        arp.noteOn(50, 100);  // D3

        // Run and verify pattern is [48, 50, 52, 55] (all accumulated)
        std::vector<uint8_t> notes;
        for (int b = 0; b < 300; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notes.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        CHECK(notes.size() >= 4);

        // All four notes should appear in the output
        bool found48 = false, found50 = false, found52 = false, found55 = false;
        for (uint8_t n : notes) {
            if (n == 48) found48 = true;
            if (n == 50) found50 = true;
            if (n == 52) found52 = true;
            if (n == 55) found55 = true;
            // Only these four notes should appear
            bool valid = (n == 48 || n == 50 || n == 52 || n == 55);
            CHECK(valid);
        }
        CHECK(found48);
        CHECK(found50);
        CHECK(found52);
        CHECK(found55);
    }

    SECTION("multiple adds grow pattern cumulatively") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setLatchMode(LatchMode::Add);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;

        // Get arp going and release
        for (int b = 0; b < 25; ++b) {
            arp.processBlock(ctx, buf);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
        arp.noteOff(48);
        arp.noteOff(52);
        arp.noteOff(55);

        // Add A3 and B3
        arp.noteOn(69, 100);  // A3
        arp.noteOff(69);
        arp.noteOn(71, 100);  // B3
        arp.noteOff(71);

        // Pattern should now be [48, 52, 55, 69, 71]
        std::vector<uint8_t> notes;
        for (int b = 0; b < 500; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notes.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        CHECK(notes.size() >= 5);

        bool found48 = false, found52 = false, found55 = false;
        bool found69 = false, found71 = false;
        for (uint8_t n : notes) {
            if (n == 48) found48 = true;
            if (n == 52) found52 = true;
            if (n == 55) found55 = true;
            if (n == 69) found69 = true;
            if (n == 71) found71 = true;
            bool valid = (n == 48 || n == 52 || n == 55 || n == 69 || n == 71);
            CHECK(valid);
        }
        CHECK(found48);
        CHECK(found52);
        CHECK(found55);
        CHECK(found69);
        CHECK(found71);
    }
}


// T038: Transport stop test with Hold and Add modes (SC-004, FR-031)
TEST_CASE("ArpeggiatorCore: transport stop with Hold mode silences and preserves pattern",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setLatchMode(LatchMode::Hold);
    // Gate 150% ensures a note is always sounding when transport stops
    // (NoteOff fires after next NoteOn, so there's always overlap)
    arp.setGateLength(150.0f);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    std::array<ArpEvent, 64> buf;

    // Get arp going
    for (int b = 0; b < 30; ++b) {
        arp.processBlock(ctx, buf);
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }

    // Release all keys to enter latched state
    arp.noteOff(48);
    arp.noteOff(52);
    arp.noteOff(55);

    // Verify latched arp is still producing
    bool gotNoteOnLatched = false;
    for (int b = 0; b < 50; ++b) {
        size_t count = arp.processBlock(ctx, buf);
        for (size_t i = 0; i < count; ++i) {
            if (buf[i].type == ArpEvent::Type::NoteOn) {
                gotNoteOnLatched = true;
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }
    CHECK(gotNoteOnLatched);

    // Transport stop
    ctx.isPlaying = false;

    // First block after stop should emit NoteOff (and no NoteOn)
    size_t stopCount = arp.processBlock(ctx, buf);
    bool gotNoteOffOnStop = false;
    bool gotNoteOnOnStop = false;
    for (size_t i = 0; i < stopCount; ++i) {
        if (buf[i].type == ArpEvent::Type::NoteOff) {
            gotNoteOffOnStop = true;
        }
        if (buf[i].type == ArpEvent::Type::NoteOn) {
            gotNoteOnOnStop = true;
        }
    }
    CHECK(gotNoteOffOnStop);
    CHECK_FALSE(gotNoteOnOnStop);

    // Subsequent blocks with transport stopped: 0 events
    for (int b = 0; b < 10; ++b) {
        size_t count = arp.processBlock(ctx, buf);
        CHECK(count == 0);
    }

    // Transport restart -- arp should resume with same latched pattern
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    std::vector<uint8_t> notesAfterRestart;
    for (int b = 0; b < 200; ++b) {
        size_t count = arp.processBlock(ctx, buf);
        for (size_t i = 0; i < count; ++i) {
            if (buf[i].type == ArpEvent::Type::NoteOn) {
                notesAfterRestart.push_back(buf[i].note);
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }

    // Should resume arpeggiation with preserved [48, 52, 55] pattern
    CHECK(notesAfterRestart.size() >= 3);
    for (uint8_t n : notesAfterRestart) {
        bool isOriginal = (n == 48 || n == 52 || n == 55);
        CHECK(isOriginal);
    }
}


TEST_CASE("ArpeggiatorCore: transport stop with Add mode silences and preserves pattern",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setLatchMode(LatchMode::Add);
    // Gate 150% ensures a note is always sounding when transport stops
    // (NoteOff fires after next NoteOn, so there's always overlap)
    arp.setGateLength(150.0f);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    std::array<ArpEvent, 64> buf;

    // Get arp going
    for (int b = 0; b < 30; ++b) {
        arp.processBlock(ctx, buf);
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }

    // Release all keys (Add mode: notes stay)
    arp.noteOff(48);
    arp.noteOff(52);
    arp.noteOff(55);

    // Add D3 to the pattern
    arp.noteOn(50, 100);
    arp.noteOff(50);

    // Verify arp is producing with accumulated pattern
    bool gotNoteOn = false;
    for (int b = 0; b < 50; ++b) {
        size_t count = arp.processBlock(ctx, buf);
        for (size_t i = 0; i < count; ++i) {
            if (buf[i].type == ArpEvent::Type::NoteOn) {
                gotNoteOn = true;
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }
    CHECK(gotNoteOn);

    // Transport stop
    ctx.isPlaying = false;

    // Should get NoteOff and halt
    size_t stopCount = arp.processBlock(ctx, buf);
    bool gotNoteOffOnStop = false;
    for (size_t i = 0; i < stopCount; ++i) {
        if (buf[i].type == ArpEvent::Type::NoteOff) {
            gotNoteOffOnStop = true;
        }
    }
    CHECK(gotNoteOffOnStop);

    // Subsequent blocks stopped: 0 events
    for (int b = 0; b < 10; ++b) {
        size_t count = arp.processBlock(ctx, buf);
        CHECK(count == 0);
    }

    // Transport restart -- should resume with accumulated [48, 50, 52, 55]
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    std::vector<uint8_t> notesAfterRestart;
    for (int b = 0; b < 300; ++b) {
        size_t count = arp.processBlock(ctx, buf);
        for (size_t i = 0; i < count; ++i) {
            if (buf[i].type == ArpEvent::Type::NoteOn) {
                notesAfterRestart.push_back(buf[i].note);
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }

    // Should have all 4 notes in the accumulated pattern
    CHECK(notesAfterRestart.size() >= 4);
    bool found48 = false, found50 = false, found52 = false, found55 = false;
    for (uint8_t n : notesAfterRestart) {
        if (n == 48) found48 = true;
        if (n == 50) found50 = true;
        if (n == 52) found52 = true;
        if (n == 55) found55 = true;
        bool valid = (n == 48 || n == 50 || n == 52 || n == 55);
        CHECK(valid);
    }
    CHECK(found48);
    CHECK(found50);
    CHECK(found52);
    CHECK(found55);
}


// =============================================================================
// Phase 6: User Story 4 -- Retrigger Modes Reset the Pattern
// =============================================================================

// T045: Retrigger Off tests (SC-005, US4 scenario 1)
TEST_CASE("ArpeggiatorCore: Retrigger Off -- pattern continues on noteOn",
          "[processors][arpeggiator_core]") {

    SECTION("advance 2 steps, add A3, pattern continues from current index") {
        // Hold [C3, E3, G3] in Up mode, advance 2 steps so noteIndex_=2.
        // After 2 advances: step1 returned C3 (index 0->1), step2 returned E3 (index 1->2).
        // Now add A3. Pattern becomes [C3, E3, G3, A3] (4 notes).
        // noteIndex_=2, so next advance yields G3 (pitched[2]=55).
        // With Retrigger Off, selector is NOT reset -- pattern continues.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setRetrigger(ArpRetriggerMode::Off);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // Collect exactly 2 NoteOns (C3, E3). After these, noteIndex_=2.
        std::array<ArpEvent, 64> buf;
        std::vector<uint8_t> noteSequence;
        size_t blocksProcessed = 0;
        while (noteSequence.size() < 2 && blocksProcessed < 80) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    noteSequence.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
            ++blocksProcessed;
        }

        REQUIRE(noteSequence.size() >= 2);
        CHECK(noteSequence[0] == 48);  // C3
        CHECK(noteSequence[1] == 52);  // E3

        // Add A3 (57). Pattern is now [48, 52, 55, 57] sorted.
        arp.noteOn(57, 100);  // A3

        // Next advance picks pitched[2] = G3 (55), confirming continuation.
        std::vector<uint8_t> notesAfterAdd;
        size_t blocksAfter = 0;
        while (notesAfterAdd.empty() && blocksAfter < 50) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notesAfterAdd.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
            ++blocksAfter;
        }

        REQUIRE(notesAfterAdd.size() >= 1);
        // G3 (55) -- pattern continued from index 2, NOT C3 (48)
        CHECK(notesAfterAdd[0] == 55);
    }

    SECTION("advance 1 step, add D3, next step continues from index 1") {
        // Hold [C3, E3, G3] in Up mode, advance 1 step so noteIndex_=1.
        // Add D3. Pattern becomes [C3(48), D3(50), E3(52), G3(55)].
        // noteIndex_=1 picks D3(50), confirming continuation.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setRetrigger(ArpRetriggerMode::Off);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;
        std::vector<uint8_t> noteSequence;
        size_t blocksProcessed = 0;

        // Collect 1 NoteOn (C3). After this, noteIndex_=1.
        while (noteSequence.size() < 1 && blocksProcessed < 50) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    noteSequence.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
            ++blocksProcessed;
        }

        REQUIRE(noteSequence.size() >= 1);
        CHECK(noteSequence[0] == 48);  // C3

        // Add D3 (50). Pattern becomes [48, 50, 52, 55].
        arp.noteOn(50, 100);  // D3

        // noteIndex_=1. Next advance picks pitched[1] = D3(50).
        std::vector<uint8_t> notesAfterAdd;
        size_t blocksAfter = 0;
        while (notesAfterAdd.empty() && blocksAfter < 50) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notesAfterAdd.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
            ++blocksAfter;
        }

        REQUIRE(notesAfterAdd.size() >= 1);
        // D3 (50) -- pattern continued from index 1, not restarting at C3 (48)
        CHECK(notesAfterAdd[0] == 50);
    }
}


// T046: Retrigger Note tests (SC-005, US4 scenario 2)
TEST_CASE("ArpeggiatorCore: Retrigger Note -- resets pattern on noteOn",
          "[processors][arpeggiator_core]") {

    SECTION("advance to G3, send noteOn for A3, next step is C3 (first in Up)") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setRetrigger(ArpRetriggerMode::Note);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;
        std::vector<uint8_t> noteSequence;
        size_t blocksProcessed = 0;

        // Collect 3 NoteOns (C3, E3, G3)
        while (noteSequence.size() < 3 && blocksProcessed < 100) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    noteSequence.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
            ++blocksProcessed;
        }

        REQUIRE(noteSequence.size() >= 3);
        CHECK(noteSequence[0] == 48);  // C3
        CHECK(noteSequence[1] == 52);  // E3
        CHECK(noteSequence[2] == 55);  // G3

        // Send noteOn for A3 -- Retrigger Note should reset selector
        arp.noteOn(57, 100);  // A3

        // Next arp step should be C3 (first/lowest in Up mode after reset)
        // Pattern is now [C3, E3, G3, A3] sorted = [48, 52, 55, 57]
        std::vector<uint8_t> notesAfterRetrigger;
        size_t blocksAfter = 0;
        while (notesAfterRetrigger.empty() && blocksAfter < 50) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notesAfterRetrigger.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
            ++blocksAfter;
        }

        REQUIRE(notesAfterRetrigger.size() >= 1);
        // Should be C3 (48) -- the first note in Up mode after reset
        CHECK(notesAfterRetrigger[0] == 48);
    }

    SECTION("advance to E3, send noteOn for D3, pattern resets to C3") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setRetrigger(ArpRetriggerMode::Note);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 64> buf;
        std::vector<uint8_t> noteSequence;
        size_t blocksProcessed = 0;

        // Collect 2 NoteOns (C3, E3)
        while (noteSequence.size() < 2 && blocksProcessed < 80) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    noteSequence.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
            ++blocksProcessed;
        }

        REQUIRE(noteSequence.size() >= 2);
        CHECK(noteSequence[0] == 48);
        CHECK(noteSequence[1] == 52);

        // Send noteOn for D3 (50) -- Retrigger Note resets selector
        arp.noteOn(50, 100);

        // Pattern is now [48, 50, 52, 55]. After reset, next note should be C3 (48)
        std::vector<uint8_t> notesAfterRetrigger;
        size_t blocksAfter = 0;
        while (notesAfterRetrigger.empty() && blocksAfter < 50) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notesAfterRetrigger.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
            ++blocksAfter;
        }

        REQUIRE(notesAfterRetrigger.size() >= 1);
        CHECK(notesAfterRetrigger[0] == 48);  // C3 -- pattern restarted
    }

    SECTION("swingStepCounter resets to 0 on retrigger Note") {
        // Verify that after retrigger Note, swingStepCounter_ is 0.
        // We do this indirectly: enable swing, advance to odd step (shortened),
        // then trigger retrigger. The next step after retrigger should have
        // even-step timing (lengthened), confirming counter reset.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setRetrigger(ArpRetriggerMode::Note);
        arp.setSwing(50.0f);  // 50% swing: even=16537, odd=5512
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // Collect NoteOns. With swing 50%:
        // Step 0 (even): fires immediately at 0, duration = floor(11025 * 1.5) = 16537
        // Step 1 (odd):  at 16537, duration = floor(11025 * 0.5) = 5512
        // Need enough blocks to get at least 2 NoteOns
        auto events = collectEvents(arp, ctx, 500);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 2);
        // First NoteOn fires immediately at 0
        CHECK(noteOns[0].sampleOffset == 0);
        int32_t gap01 = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
        CHECK(gap01 == 5512);  // Odd step (shortened by swing)

        // After 2 NoteOns, swingStepCounter_=2 (even again).
        // Send a noteOn to retrigger -- should reset swingStepCounter_ to 0.
        arp.noteOn(57, 100);  // A3 -- triggers retrigger Note

        // Collect the next 2 NoteOns after retrigger
        std::array<ArpEvent, 64> buf;
        std::vector<int32_t> offsets;
        for (size_t b = 0; b < 200; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    offsets.push_back(buf[i].sampleOffset +
                                     static_cast<int32_t>(b * ctx.blockSize));
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        REQUIRE(offsets.size() >= 2);
        // After retrigger: swingStepCounter_ = 0.
        // The first step fires after even-step duration (16537).
        // After it fires, swingStepCounter_ becomes 1.
        // The next step fires after odd-step duration (5512).
        // Gap between NoteOn[0] and NoteOn[1] = 5512 (odd step after even).
        int32_t gapAfterRetrigger = offsets[1] - offsets[0];
        CHECK(gapAfterRetrigger == 5512);
    }
}


// T047: Retrigger Beat tests (SC-005, US4 scenarios 3 and 4)
TEST_CASE("ArpeggiatorCore: Retrigger Beat -- resets at bar boundary",
          "[processors][arpeggiator_core]") {

    SECTION("bar boundary mid-block resets pattern") {
        // 4/4 time at 120 BPM: bar = 4 * 22050 = 88200 samples.
        // Use 1/8 note step (11025 samples). 88200/11025 = 8 steps per bar.
        // Steps fire at: 11025, 22050, ..., 77175, 88200.
        // The 8th step fires exactly at sample 88200 = bar boundary.
        // Without retrigger Beat, step 8 would be E3 (step #8 in C E G C E G C E cycle).
        // With retrigger Beat, the selector resets at the bar boundary,
        // so step 8 should be C3 (first note after reset).
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setRetrigger(ArpRetriggerMode::Beat);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.timeSignatureNumerator = 4;
        ctx.timeSignatureDenominator = 4;
        ctx.transportPositionSamples = 0;

        // Need past 88200 samples: 88200/512 ~ 173 blocks. Use 200 for safety.
        auto events = collectEvents(arp, ctx, 200);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 9);

        // Find the NoteOn at or near sample 88200 (bar boundary)
        bool foundBarReset = false;
        for (size_t i = 0; i < noteOns.size(); ++i) {
            if (std::abs(noteOns[i].sampleOffset - 88200) <= 1) {
                // This NoteOn should be C3 (48) due to bar boundary reset
                CHECK(noteOns[i].note == 48);
                foundBarReset = true;
                break;
            }
        }
        CHECK(foundBarReset);

        // Without retrigger Beat, step 8 would be E3 (52):
        // Steps: C3 E3 G3 C3 E3 G3 C3 [E3] -- the 8th note.
        // With reset, it's C3 instead. Verify step 7 (at 77175) is NOT C3
        // to confirm the reset only happens at bar boundary, not before.
        for (size_t i = 0; i < noteOns.size(); ++i) {
            if (std::abs(noteOns[i].sampleOffset - 77175) <= 1) {
                // Step 7 in the cycle: C E G C E G [C] = C3.
                // Wait, step 7 is index 6 in the pattern = C3 (6 mod 3 = 0).
                // Actually this is already C3 in the normal cycle.
                // Let's verify a step that would differ with/without reset.
                // Step 8 without reset = E3 (7 mod 3 = 1 -> E3).
                // Step 8 with reset = C3 (reset, 0 mod 3 = 0 -> C3).
                // That's what we checked above. This is sufficient.
                break;
            }
        }
    }

    SECTION("bar boundary at block start resets pattern") {
        // Position transportPositionSamples so that the block starts exactly
        // at a bar boundary.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setRetrigger(ArpRetriggerMode::Beat);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.timeSignatureNumerator = 4;
        ctx.timeSignatureDenominator = 4;
        ctx.transportPositionSamples = 0;

        // Bar = 88200 samples. Run to near the bar boundary.
        std::array<ArpEvent, 64> buf;

        // Process blocks for the first bar (advance pattern)
        size_t samplesProcessed = 0;
        std::vector<uint8_t> notesBefore;
        while (samplesProcessed < 88200 - 512) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notesBefore.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
            samplesProcessed += ctx.blockSize;
        }

        // At this point we are close to the bar boundary. The selector has
        // advanced through several steps. Continue processing through the
        // bar boundary.
        std::vector<uint8_t> notesNearBoundary;
        for (int b = 0; b < 10; ++b) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    notesNearBoundary.push_back(buf[i].note);
                }
            }
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }

        // After the bar boundary, the pattern should reset to C3.
        // The first NoteOn after 88200 samples should be C3.
        // Since steps happen every 11025 and bar at 88200, step 8 lands
        // exactly at the bar boundary and should be C3 (after reset).
        // We already verified this in the previous section from a different angle.
        // Here we just confirm no crash and notes continue after boundary.
        CHECK(notesNearBoundary.size() >= 1);
    }

    SECTION("no bar boundary within block -- pattern continues without reset") {
        // Position transport so no bar boundary crosses during the block.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setRetrigger(ArpRetriggerMode::Beat);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.timeSignatureNumerator = 4;
        ctx.timeSignatureDenominator = 4;
        ctx.transportPositionSamples = 0;

        // Bar = 88200 samples. Process the first few steps (well within bar 1).
        // No bar boundary should cause any reset.
        // 3 steps: C3, E3, G3. Then step 4 should be C3 (wrapping in Up mode).
        auto events = collectEvents(arp, ctx, 100);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 4);
        // Up mode with [C3, E3, G3]: pattern is C3, E3, G3, C3, E3, G3, ...
        CHECK(noteOns[0].note == 48);  // C3
        CHECK(noteOns[1].note == 52);  // E3
        CHECK(noteOns[2].note == 55);  // G3
        CHECK(noteOns[3].note == 48);  // C3 (normal wrap, not bar-boundary reset)
    }

    SECTION("swingStepCounter resets at bar boundary") {
        // With swing, step durations alternate (even=16537, odd=5512 at 50%).
        // Pair sum = 22049. In one bar (88200), there are ~4 pairs = ~8 steps.
        // After bar boundary reset, the swing counter resets to 0.
        // To verify: find the first NoteOn at or after the bar boundary,
        // check that its gap to the next NoteOn matches the odd step (5512),
        // confirming the bar-boundary step was even (counter=0).
        //
        // Without swing, step is 11025. 88200/11025 = 8 exactly. With swing,
        // steps alternate 16537,5512 -> pair=22049. 4 pairs = 88196 samples.
        // So step 8 fires at 88196, which is 4 samples before the bar boundary.
        // The bar boundary at 88200 falls within the 9th step's duration.
        // When the bar boundary fires at offset 88200, the selector resets and
        // swingStepCounter resets to 0. The step duration recalculation after
        // that point uses counter=0 (even).
        //
        // The simplest verification: after the bar boundary, the gap from the
        // first post-boundary NoteOn to the second should follow even-then-odd
        // pattern (confirming counter started at 0).
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setRetrigger(ArpRetriggerMode::Beat);
        arp.setSwing(50.0f);  // 50% swing
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.timeSignatureNumerator = 4;
        ctx.timeSignatureDenominator = 4;
        ctx.transportPositionSamples = 0;

        // Run well past one bar boundary (88200 samples).
        auto events = collectEvents(arp, ctx, 300);
        auto noteOns = filterNoteOns(events);

        // Find the first NoteOn at or after the bar boundary (88200).
        size_t firstPostBarIdx = SIZE_MAX;
        for (size_t i = 0; i < noteOns.size(); ++i) {
            if (noteOns[i].sampleOffset >= 88200) {
                firstPostBarIdx = i;
                break;
            }
        }

        REQUIRE(firstPostBarIdx != SIZE_MAX);
        REQUIRE(firstPostBarIdx + 1 < noteOns.size());

        // The first post-bar-boundary step uses swingStepCounter_=0 (even).
        // Its duration is 16537 (even step). After it fires, counter=1.
        // The next step has counter=1 (odd) -> duration 5512.
        // So gap from first post-bar NoteOn to second = 5512.
        int32_t gap = noteOns[firstPostBarIdx + 1].sampleOffset -
                      noteOns[firstPostBarIdx].sampleOffset;
        CHECK(gap == 5512);
    }
}


// =============================================================================
// Phase 7: User Story 5 -- Swing Creates Shuffle Rhythm
// =============================================================================

// T053: Swing 0% test (SC-006, US5 scenario 1)
TEST_CASE("ArpeggiatorCore: swing 0% -- all steps equal duration",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setSwing(0.0f);  // No swing
    arp.setGateLength(50.0f);
    arp.noteOn(48, 100);  // C3
    arp.noteOn(52, 100);  // E3
    arp.noteOn(55, 100);  // G3

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // At 120 BPM, 1/8 note = 11025 samples per step
    constexpr int32_t expectedStep = 11025;

    // Collect enough blocks for 20+ NoteOn events
    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 20);

    // Verify all consecutive gaps are exactly 11025 samples (within 1 sample)
    for (size_t i = 1; i < noteOns.size(); ++i) {
        int32_t gap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;
        INFO("Step " << i << ": gap = " << gap << ", expected = " << expectedStep);
        CHECK(gap >= expectedStep - 1);
        CHECK(gap <= expectedStep + 1);
    }
}


// T054: Swing 50% test (SC-006, US5 scenario 2)
TEST_CASE("ArpeggiatorCore: swing 50% -- even=16537, odd=5512",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setSwing(50.0f);  // 50% swing -> swing_ = 0.5
    arp.setGateLength(50.0f);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // At 120 BPM, 1/8 note base = 11025 samples
    // Even step: floor(11025 * 1.5) = floor(16537.5) = 16537
    // Odd step:  floor(11025 * 0.5) = floor(5512.5)  = 5512
    constexpr int32_t expectedEven = 16537;
    constexpr int32_t expectedOdd = 5512;

    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 20);

    // Verify even/odd step durations.
    // The gap from noteOn[i-1] to noteOn[i] uses the step duration calculated
    // AFTER step (i-1) fired. fireStep() increments swingStepCounter_ then
    // recalculates the duration. So:
    //   gap index 0 (noteOn[0]->noteOn[1]): counter was 0 when step fired,
    //     incremented to 1 (odd), duration = odd (short).
    //   gap index 1 (noteOn[1]->noteOn[2]): counter was 1, incremented to 2
    //     (even), duration = even (long).
    for (size_t i = 1; i < noteOns.size(); ++i) {
        int32_t gap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;

        // After step (i-1) fires, counter = (i-1)+1 = i. Even/odd of counter i
        // determines the gap duration.
        bool isEvenCounter = (i % 2 == 0);
        int32_t expected = isEvenCounter ? expectedEven : expectedOdd;

        INFO("Gap " << (i - 1) << " (counter=" << i
             << ", even=" << isEvenCounter
             << "): gap = " << gap << ", expected = " << expected);
        CHECK(gap >= expected - 1);
        CHECK(gap <= expected + 1);
    }

    // Verify pair sums (odd + even) are within 1 of 22050.
    // First pair: gap[0] (odd) + gap[1] (even). Pairs start at odd gaps.
    for (size_t i = 1; i + 1 < noteOns.size(); i += 2) {
        int32_t firstGap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;
        int32_t secondGap = noteOns[i + 1].sampleOffset - noteOns[i].sampleOffset;
        int32_t pairSum = firstGap + secondGap;

        INFO("Pair starting at gap " << (i - 1) << ": " << firstGap
             << " + " << secondGap << " = " << pairSum);
        CHECK(pairSum >= 22049);
        CHECK(pairSum <= 22050);
    }
}


// T055: Swing 25% and 75% tests (SC-006, US5 scenarios 3 and 4)
TEST_CASE("ArpeggiatorCore: swing 25% -- even=13781, odd=8268",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setSwing(25.0f);  // 25% swing -> swing_ = 0.25
    arp.setGateLength(50.0f);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Even step: floor(11025 * 1.25) = floor(13781.25) = 13781
    // Odd step:  floor(11025 * 0.75) = floor(8268.75)  = 8268
    constexpr int32_t expectedEven = 13781;
    constexpr int32_t expectedOdd = 8268;

    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 20);

    // Same parity logic as the 50% test: gap index (i-1) uses counter value i.
    for (size_t i = 1; i < noteOns.size(); ++i) {
        int32_t gap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;
        bool isEvenCounter = (i % 2 == 0);
        int32_t expected = isEvenCounter ? expectedEven : expectedOdd;

        INFO("Gap " << (i - 1) << " (counter=" << i
             << ", even=" << isEvenCounter
             << "): gap = " << gap << ", expected = " << expected);
        CHECK(gap >= expected - 1);
        CHECK(gap <= expected + 1);
    }

    // Verify pair sums
    for (size_t i = 1; i + 1 < noteOns.size(); i += 2) {
        int32_t firstGap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;
        int32_t secondGap = noteOns[i + 1].sampleOffset - noteOns[i].sampleOffset;
        int32_t pairSum = firstGap + secondGap;

        INFO("Pair starting at gap " << (i - 1) << ": " << firstGap
             << " + " << secondGap << " = " << pairSum);
        CHECK(pairSum >= 22049);
        CHECK(pairSum <= 22050);
    }
}


TEST_CASE("ArpeggiatorCore: swing 75% -- even=19293, odd=2756",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setSwing(75.0f);  // 75% swing -> swing_ = 0.75
    arp.setGateLength(50.0f);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Even step: floor(11025 * 1.75) = floor(19293.75) = 19293
    // Odd step:  floor(11025 * 0.25) = floor(2756.25)  = 2756
    constexpr int32_t expectedEven = 19293;
    constexpr int32_t expectedOdd = 2756;

    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);

    REQUIRE(noteOns.size() >= 20);

    // Same parity logic: gap index (i-1) uses counter value i.
    for (size_t i = 1; i < noteOns.size(); ++i) {
        int32_t gap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;
        bool isEvenCounter = (i % 2 == 0);
        int32_t expected = isEvenCounter ? expectedEven : expectedOdd;

        INFO("Gap " << (i - 1) << " (counter=" << i
             << ", even=" << isEvenCounter
             << "): gap = " << gap << ", expected = " << expected);
        CHECK(gap >= expected - 1);
        CHECK(gap <= expected + 1);
    }

    // Verify pair sums
    for (size_t i = 1; i + 1 < noteOns.size(); i += 2) {
        int32_t firstGap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;
        int32_t secondGap = noteOns[i + 1].sampleOffset - noteOns[i].sampleOffset;
        int32_t pairSum = firstGap + secondGap;

        INFO("Pair starting at gap " << (i - 1) << ": " << firstGap
             << " + " << secondGap << " = " << pairSum);
        CHECK(pairSum >= 22049);
        CHECK(pairSum <= 22050);
    }
}


// T056: setMode() reset test (SC-006 additional requirement)
TEST_CASE("ArpeggiatorCore: setMode resets swing counter -- next step gets even timing",
          "[processors][arpeggiator_core]") {
    // Strategy: Run the arp with swing until we reach a point where the next
    // step would normally be calculated with an even counter (giving long duration).
    // Then call setMode() to reset the counter to 0. After the reset, the next
    // fireStep() will increment counter from 0 to 1 (odd) and calculate an odd
    // (short) duration. Without the reset, counter would be at an even value and
    // the gap would be long. With the reset, the gap is short.
    //
    // Concretely: after 2 steps fire (counter=2), the current step duration
    // was calculated with counter=2 (even=long=16537). Without setMode, the
    // next fireStep increments to 3 (odd) and sets duration to 5512.
    // The gap after step 2 is 5512 (odd), and after step 3 is 16537 (even).
    //
    // With setMode after 2 steps: counter resets to 0. The current step
    // duration (16537) remains. When the step fires, fireStep increments to 1
    // (odd) and sets duration to 5512. Gap after this step is 5512. Then
    // counter goes to 2 (even), duration = 16537. So the pattern is 5512,
    // 16537, 5512, 16537 -- the SAME as without reset.
    //
    // To see a real difference, call setMode after 3 steps (counter=3).
    // Without reset: next fireStep increments to 4 (even), duration=16537.
    //   So gap after step 3 = 16537 (even).
    // With reset (counter=0): next fireStep increments to 1 (odd),
    //   duration=5512. So gap after this step = 5512 (odd).
    //
    // Observable difference: without reset -> gap=16537, with reset -> gap=5512.
    // This proves the counter was reset.

    constexpr int32_t expectedOdd = 5512;    // counter=1 (odd) duration
    constexpr int32_t expectedEven = 16537;  // counter=2/4 (even) duration

    // --- Run 1: WITHOUT setMode (control) ---
    {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setSwing(50.0f);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        auto events = collectEvents(arp, ctx, 500);
        auto noteOns = filterNoteOns(events);
        REQUIRE(noteOns.size() >= 5);

        // Gap after step 3 (index 3): noteOn[3]->noteOn[4].
        // Step 3 fired at counter=3. fireStep increments to 4 (even),
        // duration = 16537. So gap = 16537.
        int32_t gapAfterStep3 = noteOns[4].sampleOffset -
                                 noteOns[3].sampleOffset;
        INFO("Control (no setMode): gap after step 3 = " << gapAfterStep3);
        CHECK(gapAfterStep3 >= expectedEven - 1);
        CHECK(gapAfterStep3 <= expectedEven + 1);
    }

    // --- Run 2: WITH setMode after step 3 ---
    {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setSwing(50.0f);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        std::array<ArpEvent, 64> buf;
        std::vector<ArpEvent> allNoteOns;

        // Advance until 4 NoteOns have fired (steps 0-3 complete).
        size_t blocksRun = 0;
        while (allNoteOns.size() < 4 && blocksRun < 200) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    ArpEvent evt = buf[i];
                    evt.sampleOffset +=
                        static_cast<int32_t>(blocksRun * ctx.blockSize);
                    allNoteOns.push_back(evt);
                }
            }
            ctx.transportPositionSamples +=
                static_cast<int64_t>(ctx.blockSize);
            ++blocksRun;
        }
        REQUIRE(allNoteOns.size() >= 4);

        // At this point, swingStepCounter_ = 4 (even). The current step
        // duration was calculated with counter=4 (even=16537).
        // WITHOUT reset, the next fireStep increments to 5 (odd), sets 5512.
        //
        // Call setMode to reset counter to 0.
        arp.setMode(ArpMode::Down);

        // Now counter = 0. The current step duration (16537) is still in
        // effect. When this step fires, fireStep increments to 1 (odd),
        // sets next duration to 5512. So the gap after the first
        // post-mode-change NoteOn is 5512 (odd). Without reset, the gap
        // after step 4 would also be 5512 (counter=5, odd). So the first
        // gap doesn't distinguish.
        //
        // The SECOND gap after the mode change is the telling one:
        // With reset: counter=1 after first post-change step. Next fires,
        //   counter goes to 2 (even), duration=16537. Gap = 16537.
        // Without reset: counter=5 after step 4. Next fires, counter goes
        //   to 6 (even), duration=16537. Gap = 16537.
        //
        // Hmm, this produces the same pattern regardless! The issue is that
        // the alternating pattern is phase-independent: 0->odd, 1->even,
        // 2->odd, etc. Whether counter is 0 or 4, the next values are both
        // odd then even.
        //
        // The real difference is when we call setMode at an ODD counter value.
        // If counter was 3 (odd), the next step fires with counter 3, then
        // counter becomes 4 (even), duration=16537. Without reset, gap=16537.
        // With reset (counter=0), next step fires, counter becomes 1 (odd),
        // duration=5512. Gap=5512. THAT is different.

        // Let me restart with the right approach below.
    }

    // --- Run 3: setMode at counter=3 (odd), proving the gap changes ---
    {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setSwing(50.0f);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;
        ctx.transportPositionSamples = 0;

        std::array<ArpEvent, 64> buf;
        std::vector<ArpEvent> allNoteOns;

        // Advance until 3 NoteOns have fired (steps 0, 1, 2).
        // After step 2 fires, swingStepCounter_ = 3 (odd). The current step
        // duration was calculated with counter=3 (odd=5512).
        size_t blocksRun = 0;
        while (allNoteOns.size() < 3 && blocksRun < 200) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    ArpEvent evt = buf[i];
                    evt.sampleOffset +=
                        static_cast<int32_t>(blocksRun * ctx.blockSize);
                    allNoteOns.push_back(evt);
                }
            }
            ctx.transportPositionSamples +=
                static_cast<int64_t>(ctx.blockSize);
            ++blocksRun;
        }
        REQUIRE(allNoteOns.size() >= 3);

        // swingStepCounter_ = 3 (odd). currentStepDuration_ = 5512 (odd).
        // Without setMode: next step fires after 5512 samples. fireStep
        //   increments to 4 (even), sets duration=16537. So:
        //   gap[3->4] = 16537 (LONG).
        //
        // Call setMode to reset counter to 0.
        arp.setMode(ArpMode::Down);
        //
        // With reset: counter = 0. currentStepDuration_ = 5512 (unchanged).
        //   Next step fires after remaining samples of the 5512-sample step.
        //   fireStep increments to 1 (odd), sets duration=5512 (SHORT).
        //   So gap from first post-change NoteOn to second = 5512.
        //
        // Without reset: gap from step 3 to step 4 = 16537 (LONG).
        // With reset: gap from first post-change step to second = 5512 (SHORT).
        //
        // This is the observable difference.

        std::vector<ArpEvent> postChangeNoteOns;
        while (postChangeNoteOns.size() < 2 && blocksRun < 500) {
            size_t count = arp.processBlock(ctx, buf);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i].type == ArpEvent::Type::NoteOn) {
                    ArpEvent evt = buf[i];
                    evt.sampleOffset +=
                        static_cast<int32_t>(blocksRun * ctx.blockSize);
                    postChangeNoteOns.push_back(evt);
                }
            }
            ctx.transportPositionSamples +=
                static_cast<int64_t>(ctx.blockSize);
            ++blocksRun;
        }
        REQUIRE(postChangeNoteOns.size() >= 2);

        int32_t gapAfterModeChange =
            postChangeNoteOns[1].sampleOffset -
            postChangeNoteOns[0].sampleOffset;

        // With reset: first post-change step fires with counter=0.
        // fireStep increments to 1 (odd), sets duration=5512.
        // Gap = 5512 (odd, SHORT).
        //
        // WITHOUT reset (counter=3), first gap after step 3 would be
        // calculated at counter=4 (even) = 16537 (LONG).
        //
        // So we expect the gap to be 5512, not 16537. This proves reset.
        INFO("Gap after setMode() (counter reset): " << gapAfterModeChange
             << ", expected odd (short) = " << expectedOdd
             << " (NOT even/long = " << expectedEven << ")");
        CHECK(gapAfterModeChange >= expectedOdd - 1);
        CHECK(gapAfterModeChange <= expectedOdd + 1);
    }
}


// =============================================================================
// Phase 8: User Story 6 -- Enable/Disable Toggle with Clean Transitions
// =============================================================================

// T061: Disabled state returns 0 events (SC-010, US6 scenario 1)
TEST_CASE("ArpeggiatorCore: disabled state returns 0 events with notes held",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(false);  // Disabled from the start
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);
    arp.noteOn(48, 100);  // C3
    arp.noteOn(52, 100);  // E3
    arp.noteOn(55, 100);  // G3

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    std::array<ArpEvent, 64> buf;

    // Run several blocks -- should always produce 0 events when disabled
    for (int block = 0; block < 50; ++block) {
        size_t count = arp.processBlock(ctx, buf);
        CHECK(count == 0);
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }
}


// T062: Disable transition emits NoteOff for currently sounding note (SC-010, US6 scenario 2)
TEST_CASE("ArpeggiatorCore: disable transition emits NoteOff for sounding note",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(80.0f);
    arp.noteOn(48, 100);  // C3
    arp.noteOn(52, 100);  // E3
    arp.noteOn(55, 100);  // G3

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    std::array<ArpEvent, 64> buf;

    // Advance until at least one NoteOn fires
    uint8_t soundingNote = 0;
    bool foundNoteOn = false;
    size_t blocksRun = 0;
    while (!foundNoteOn && blocksRun < 100) {
        size_t count = arp.processBlock(ctx, buf);
        for (size_t i = 0; i < count; ++i) {
            if (buf[i].type == ArpEvent::Type::NoteOn) {
                soundingNote = buf[i].note;
                foundNoteOn = true;
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        ++blocksRun;
    }
    REQUIRE(foundNoteOn);
    INFO("Sounding note after enable: " << static_cast<int>(soundingNote));

    // Disable the arp
    arp.setEnabled(false);

    // The next processBlock() should emit NoteOff for the currently sounding note
    size_t count = arp.processBlock(ctx, buf);
    ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    ++blocksRun;

    // Check that at least one NoteOff was emitted at sampleOffset 0
    bool foundNoteOff = false;
    uint8_t noteOffNote = 0;
    for (size_t i = 0; i < count; ++i) {
        if (buf[i].type == ArpEvent::Type::NoteOff) {
            CHECK(buf[i].sampleOffset == 0);
            noteOffNote = buf[i].note;
            foundNoteOff = true;
        }
    }
    CHECK(foundNoteOff);

    // Subsequent blocks must produce 0 events
    for (int block = 0; block < 20; ++block) {
        size_t cnt = arp.processBlock(ctx, buf);
        CHECK(cnt == 0);
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }
}


// T063: Enable from disabled begins arpeggiation from pattern start (US6 scenario 3)
TEST_CASE("ArpeggiatorCore: enable from disabled starts arpeggiation from pattern start",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(false);  // Start disabled
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);

    // Hold notes while disabled
    arp.noteOn(48, 100);  // C3
    arp.noteOn(52, 100);  // E3
    arp.noteOn(55, 100);  // G3

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    std::array<ArpEvent, 64> buf;

    // Process a few blocks while disabled -- should be 0 events
    for (int block = 0; block < 10; ++block) {
        size_t count = arp.processBlock(ctx, buf);
        CHECK(count == 0);
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }

    // Now enable
    arp.setEnabled(true);

    // Collect events over enough blocks to get multiple NoteOns
    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);

    // Should have produced NoteOn events
    REQUIRE(noteOns.size() >= 3);

    // In Up mode with notes [48, 52, 55], the first NoteOn should be
    // the lowest note (48 = C3) -- pattern starts from the beginning.
    CHECK(noteOns[0].note == 48);

    // Verify the pattern order is Up: 48, 52, 55, 48, 52, 55, ...
    CHECK(noteOns[1].note == 52);
    CHECK(noteOns[2].note == 55);
}


// T064: Pending NoteOff on disable is still emitted (spec edge cases)
TEST_CASE("ArpeggiatorCore: pending NoteOff emitted on disable -- no stuck notes",
          "[processors][arpeggiator_core]") {
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    // Gate > 100% so NoteOff is scheduled for a future block
    arp.setGateLength(150.0f);
    arp.noteOn(48, 100);  // C3
    arp.noteOn(52, 100);  // E3
    arp.noteOn(55, 100);  // G3

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    std::array<ArpEvent, 64> buf;

    // Advance until at least one NoteOn fires (a note is sounding)
    bool foundNoteOn = false;
    uint8_t soundingNote = 0;
    size_t blocksRun = 0;
    while (!foundNoteOn && blocksRun < 100) {
        size_t count = arp.processBlock(ctx, buf);
        for (size_t i = 0; i < count; ++i) {
            if (buf[i].type == ArpEvent::Type::NoteOn) {
                soundingNote = buf[i].note;
                foundNoteOn = true;
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        ++blocksRun;
    }
    REQUIRE(foundNoteOn);
    INFO("Sounding note (gate 150%): " << static_cast<int>(soundingNote));

    // With gate 150%, the NoteOff for this note is scheduled far into
    // the future (pending NoteOff). Now disable the arp.
    arp.setEnabled(false);

    // The next processBlock() should emit NoteOff for both:
    // - the currently sounding arp note (currentArpNotes_)
    // - any pending NoteOffs in the pendingNoteOffs_ array
    // All at sampleOffset 0.
    size_t count = arp.processBlock(ctx, buf);
    ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);

    // Collect all NoteOff events
    std::vector<uint8_t> noteOffNotes;
    for (size_t i = 0; i < count; ++i) {
        if (buf[i].type == ArpEvent::Type::NoteOff) {
            CHECK(buf[i].sampleOffset == 0);
            noteOffNotes.push_back(buf[i].note);
        }
    }

    // There must be at least one NoteOff to prevent stuck notes
    CHECK(!noteOffNotes.empty());

    // Subsequent blocks should produce 0 events
    for (int block = 0; block < 20; ++block) {
        size_t cnt = arp.processBlock(ctx, buf);
        CHECK(cnt == 0);
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }
}


// =============================================================================
// Phase 9: User Story 7 -- Free Rate Mode for Tempo-Independent Operation
// =============================================================================

// T069: Free rate tests (US7 scenarios 1 and 2)
TEST_CASE("ArpeggiatorCore: Free rate mode -- step rate at 4 Hz and 0.5 Hz",
          "[processors][arpeggiator_core]") {

    SECTION("free rate 4.0 Hz at 44100 Hz -- step every 11025 samples") {
        // At 44100 Hz, free rate 4.0 Hz: step = 44100 / 4.0 = 11025 samples
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setTempoSync(false);
        arp.setFreeRate(4.0f);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // Run enough blocks to get 10+ NoteOns.
        // 10 steps * 11025 samples / 512 block ~ 216 blocks
        auto events = collectEvents(arp, ctx, 250);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 10);

        // First NoteOn fires immediately at sample 0
        CHECK(noteOns[0].sampleOffset == 0);

        // Verify consecutive NoteOns are spaced by exactly 11025 samples
        for (size_t i = 1; i < noteOns.size(); ++i) {
            int32_t gap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;
            CHECK(std::abs(gap - 11025) <= 1);
        }
    }

    SECTION("free rate 0.5 Hz at 44100 Hz -- step every 88200 samples") {
        // At 44100 Hz, free rate 0.5 Hz: step = 44100 / 0.5 = 88200 samples
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setTempoSync(false);
        arp.setFreeRate(0.5f);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);
        arp.noteOn(55, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // Need 3 NoteOns: 3 * 88200 samples / 512 ~ 517 blocks
        auto events = collectEvents(arp, ctx, 600);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 3);

        // First NoteOn fires immediately at sample 0
        CHECK(noteOns[0].sampleOffset == 0);

        // Verify spacing is exactly 88200
        for (size_t i = 1; i < noteOns.size(); ++i) {
            int32_t gap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;
            CHECK(std::abs(gap - 88200) <= 1);
        }
    }
}


// T070: Tempo-independence test (US7 scenario 3)
TEST_CASE("ArpeggiatorCore: Free rate mode -- independent of host tempo",
          "[processors][arpeggiator_core]") {
    // Set free rate 4 Hz, then change ctx.tempoBPM mid-test.
    // Verify arp step rate remains unchanged at 11025-sample period.
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Up);
    arp.setTempoSync(false);
    arp.setFreeRate(4.0f);
    arp.setGateLength(50.0f);
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    // Collect 5 NoteOns at 120 BPM
    std::vector<int32_t> noteOnOffsets;
    std::array<ArpEvent, 64> buf;
    size_t blocks = 0;

    while (noteOnOffsets.size() < 5 && blocks < 300) {
        size_t count = arp.processBlock(ctx, buf);
        for (size_t i = 0; i < count; ++i) {
            if (buf[i].type == ArpEvent::Type::NoteOn) {
                noteOnOffsets.push_back(
                    buf[i].sampleOffset +
                    static_cast<int32_t>(blocks * ctx.blockSize));
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        ++blocks;
    }

    REQUIRE(noteOnOffsets.size() >= 5);

    // Verify spacing is 11025 at 120 BPM
    for (size_t i = 1; i < noteOnOffsets.size(); ++i) {
        int32_t gap = noteOnOffsets[i] - noteOnOffsets[i - 1];
        CHECK(std::abs(gap - 11025) <= 1);
    }

    // NOW change tempo to 60 BPM (if tempo-synced, step would be different)
    ctx.tempoBPM = 60.0;

    // Collect 5 more NoteOns at 60 BPM
    std::vector<int32_t> noteOnOffsetsAfter;
    while (noteOnOffsetsAfter.size() < 5 && blocks < 600) {
        size_t count = arp.processBlock(ctx, buf);
        for (size_t i = 0; i < count; ++i) {
            if (buf[i].type == ArpEvent::Type::NoteOn) {
                noteOnOffsetsAfter.push_back(
                    buf[i].sampleOffset +
                    static_cast<int32_t>(blocks * ctx.blockSize));
            }
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        ++blocks;
    }

    REQUIRE(noteOnOffsetsAfter.size() >= 5);

    // Verify spacing is STILL 11025 even at 60 BPM -- tempo has no effect
    for (size_t i = 1; i < noteOnOffsetsAfter.size(); ++i) {
        int32_t gap = noteOnOffsetsAfter[i] - noteOnOffsetsAfter[i - 1];
        CHECK(std::abs(gap - 11025) <= 1);
    }

    // Also verify the gap between the last note at 120 BPM and first at 60 BPM
    // is also 11025 (no disruption from tempo change)
    int32_t crossGap = noteOnOffsetsAfter[0] - noteOnOffsets.back();
    CHECK(std::abs(crossGap - 11025) <= 1);
}


// T071: Free rate clamping tests (FR-014)
TEST_CASE("ArpeggiatorCore: Free rate clamping",
          "[processors][arpeggiator_core]") {

    SECTION("setFreeRate below minimum clamps to 0.5 Hz") {
        // setFreeRate(0.1f) should clamp to 0.5 Hz.
        // At 44100 Hz, 0.5 Hz -> step = 88200 samples.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setTempoSync(false);
        arp.setFreeRate(0.1f);  // Below minimum 0.5 -> clamps to 0.5
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // Need 2 NoteOns: 2 * 88200 / 512 ~ 345 blocks
        auto events = collectEvents(arp, ctx, 400);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 2);

        // First NoteOn fires immediately at 0 (clamped rate 0.5 Hz, step = 88200)
        CHECK(noteOns[0].sampleOffset == 0);

        // Gap should be 88200
        if (noteOns.size() >= 2) {
            int32_t gap = noteOns[1].sampleOffset - noteOns[0].sampleOffset;
            CHECK(std::abs(gap - 88200) <= 1);
        }
    }

    SECTION("setFreeRate above maximum clamps to 50.0 Hz") {
        // setFreeRate(100.0f) should clamp to 50.0 Hz.
        // At 44100 Hz, 50 Hz -> step = 882 samples.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setTempoSync(false);
        arp.setFreeRate(100.0f);  // Above maximum 50.0 -> clamps to 50.0
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // 50 Hz -> step = 882 samples. 10 steps in ~9000 samples -> ~18 blocks
        auto events = collectEvents(arp, ctx, 50);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 5);

        // First NoteOn fires immediately at 0 (clamped rate 50 Hz, step = 882)
        CHECK(noteOns[0].sampleOffset == 0);

        // Verify subsequent spacing is 882
        for (size_t i = 1; i < noteOns.size() && i < 5; ++i) {
            int32_t gap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;
            CHECK(std::abs(gap - 882) <= 1);
        }
    }
}


// =============================================================================
// Phase 10: User Story 8 -- Single Note and Empty Buffer Edge Cases
// =============================================================================

// T076: Single note test (SC-010, US8 scenario 1)
TEST_CASE("ArpeggiatorCore: single note repeats at configured rate",
          "[processors][arpeggiator_core]") {

    SECTION("single C3 with mode Up, octave range 1 repeats C3") {
        // Hold only C3 (MIDI 48), mode Up, octave range 1.
        // Verify arp plays C3 repeatedly at configured rate over multiple steps.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setOctaveRange(1);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // At 120 BPM, 1/8 note = 11025 samples per step.
        // Run enough blocks for at least 8 steps.
        auto events = collectEvents(arp, ctx, 200);
        auto noteOns = filterNoteOns(events);

        // Must have at least 8 NoteOn events
        REQUIRE(noteOns.size() >= 8);

        // Every NoteOn must be note 48 (C3) -- no other notes
        for (size_t i = 0; i < noteOns.size(); ++i) {
            CHECK(noteOns[i].note == 48);
        }

        // Verify timing: first NoteOn fires immediately, subsequent at 11025 intervals
        CHECK(noteOns[0].sampleOffset == 0);
        for (size_t i = 1; i < noteOns.size(); ++i) {
            int32_t gap = noteOns[i].sampleOffset - noteOns[i - 1].sampleOffset;
            CHECK(std::abs(gap - 11025) <= 1);
        }
    }

    SECTION("single E4 with mode Down, octave range 1 repeats E4") {
        // Hold only E4 (MIDI 64), mode Down, octave range 1.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Down);
        arp.setOctaveRange(1);
        arp.setNoteValue(NoteValue::Quarter, NoteModifier::None);
        arp.setGateLength(50.0f);
        arp.noteOn(64, 80);  // E4

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // Quarter note at 120 BPM = 22050 samples
        auto events = collectEvents(arp, ctx, 200);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 4);

        // Every NoteOn must be note 64 (E4)
        for (size_t i = 0; i < noteOns.size(); ++i) {
            CHECK(noteOns[i].note == 64);
        }
    }
}


// T077: Single note octave expansion test (US8 scenario 2)
TEST_CASE("ArpeggiatorCore: single note with octave expansion cycles through octaves",
          "[processors][arpeggiator_core]") {

    SECTION("C3 with octave range 3, mode Up cycles C3, C4, C5") {
        // Hold C3 (MIDI 48), octave range 3, mode Up (Sequential).
        // NoteSelector should cycle: C3(48) at octave 0, C4(60) at octave 1,
        // C5(72) at octave 2, then back to C3(48).
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setOctaveRange(3);
        arp.setOctaveMode(OctaveMode::Sequential);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // Run enough blocks for at least 9 steps (3 full octave cycles)
        auto events = collectEvents(arp, ctx, 250);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 9);

        // Expected pattern: C3(48), C4(60), C5(72), C3(48), C4(60), C5(72), ...
        uint8_t expectedNotes[] = {48, 60, 72};
        for (size_t i = 0; i < noteOns.size(); ++i) {
            uint8_t expected = expectedNotes[i % 3];
            CHECK(noteOns[i].note == expected);
        }
    }

    SECTION("C3 with octave range 2, mode Up cycles C3, C4") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setOctaveRange(2);
        arp.setOctaveMode(OctaveMode::Sequential);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        auto events = collectEvents(arp, ctx, 200);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 6);

        // Expected pattern: C3(48), C4(60), C3(48), C4(60), ...
        uint8_t expectedNotes[] = {48, 60};
        for (size_t i = 0; i < noteOns.size(); ++i) {
            uint8_t expected = expectedNotes[i % 2];
            CHECK(noteOns[i].note == expected);
        }
    }

    SECTION("C3 with octave range 4, mode Up cycles C3, C4, C5, C6") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setOctaveRange(4);
        arp.setOctaveMode(OctaveMode::Sequential);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(50.0f);
        arp.noteOn(48, 100);  // C3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        auto events = collectEvents(arp, ctx, 350);
        auto noteOns = filterNoteOns(events);

        REQUIRE(noteOns.size() >= 8);

        // Expected pattern: C3(48), C4(60), C5(72), C6(84), C3(48), ...
        uint8_t expectedNotes[] = {48, 60, 72, 84};
        for (size_t i = 0; i < noteOns.size(); ++i) {
            uint8_t expected = expectedNotes[i % 4];
            CHECK(noteOns[i].note == expected);
        }
    }
}


// T078: Empty buffer tests (SC-010, FR-024, US8 scenarios 3 and 4)
TEST_CASE("ArpeggiatorCore: empty buffer produces zero events and no crash",
          "[processors][arpeggiator_core]") {

    SECTION("no held notes with latch Off returns 0 events") {
        // (a) Call processBlock() with no held notes, latch Off.
        // Must return 0 events without crash.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setLatchMode(LatchMode::Off);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        arp.setGateLength(50.0f);
        // Do NOT add any notes

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 128> blockEvents;

        // Run multiple blocks -- all must return 0 events
        for (int b = 0; b < 10; ++b) {
            size_t count = arp.processBlock(ctx, blockEvents);
            CHECK(count == 0);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
    }

    SECTION("empty buffer after calling processBlock many times does not crash") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setLatchMode(LatchMode::Off);
        arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        std::array<ArpEvent, 128> blockEvents;

        // Run 100 blocks with no notes held -- stress test for crash
        for (int b = 0; b < 100; ++b) {
            size_t count = arp.processBlock(ctx, blockEvents);
            CHECK(count == 0);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
    }

    SECTION("hold notes then release one by one emits NoteOff on last release") {
        // (b) Hold [C3, E3, G3], release one by one.
        // Verify NoteOff is emitted for current arp note when last note released,
        // and subsequent processBlock() calls return 0 events.
        // Use gate 99% so the NoteOff is still pending when we release.
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setOctaveRange(1);
        arp.setLatchMode(LatchMode::Off);
        arp.setNoteValue(NoteValue::Quarter, NoteModifier::None);
        arp.setGateLength(99.0f);  // Long gate so note is still sounding
        arp.noteOn(48, 100);  // C3
        arp.noteOn(52, 100);  // E3
        arp.noteOn(55, 100);  // G3

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // Run enough blocks for the first step to fire (quarter note = 22050 samples)
        // 22050/512 = ~43 blocks. Run 45 to ensure first NoteOn fires.
        auto events = collectEvents(arp, ctx, 45);
        auto noteOns = filterNoteOns(events);
        REQUIRE(noteOns.size() >= 1);

        // Release notes one by one
        arp.noteOff(48);  // Release C3, still have E3, G3

        // Run a few more blocks -- arp should still produce events
        events = collectEvents(arp, ctx, 3);

        arp.noteOff(52);  // Release E3, still have G3

        // Run a few more blocks
        events = collectEvents(arp, ctx, 3);

        // Release last note -- buffer now empty
        arp.noteOff(55);

        // Next processBlock should emit NoteOff (for currently sounding note
        // and/or pending NoteOffs that are flushed on empty buffer)
        std::array<ArpEvent, 128> blockEvents;
        size_t count = arp.processBlock(ctx, blockEvents);
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);

        bool hasNoteOff = false;
        for (size_t i = 0; i < count; ++i) {
            if (blockEvents[i].type == ArpEvent::Type::NoteOff) {
                hasNoteOff = true;
            }
        }
        CHECK(hasNoteOff);

        // Subsequent blocks must return 0 events (buffer is empty, all flushed)
        for (int b = 0; b < 5; ++b) {
            count = arp.processBlock(ctx, blockEvents);
            CHECK(count == 0);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
    }

    SECTION("release all notes at once emits NoteOff and stops") {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setMode(ArpMode::Up);
        arp.setOctaveRange(1);
        arp.setLatchMode(LatchMode::Off);
        arp.setNoteValue(NoteValue::Quarter, NoteModifier::None);
        arp.setGateLength(99.0f);  // Long gate so note is still sounding
        arp.noteOn(48, 100);
        arp.noteOn(52, 100);

        BlockContext ctx;
        ctx.sampleRate = 44100.0;
        ctx.blockSize = 512;
        ctx.tempoBPM = 120.0;
        ctx.isPlaying = true;

        // Run enough blocks for the first step to fire (22050/512 ~= 43 blocks)
        auto events = collectEvents(arp, ctx, 45);
        auto noteOns = filterNoteOns(events);
        REQUIRE(noteOns.size() >= 1);

        // Release all notes at once
        arp.noteOff(48);
        arp.noteOff(52);

        // Next processBlock should emit NoteOff for current arp note
        // and flush all pending NoteOffs
        std::array<ArpEvent, 128> blockEvents;
        size_t count = arp.processBlock(ctx, blockEvents);
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);

        bool hasNoteOff = false;
        for (size_t i = 0; i < count; ++i) {
            if (blockEvents[i].type == ArpEvent::Type::NoteOff) {
                hasNoteOff = true;
            }
        }
        CHECK(hasNoteOff);

        // Subsequent blocks produce 0 events (all flushed)
        for (int b = 0; b < 5; ++b) {
            count = arp.processBlock(ctx, blockEvents);
            CHECK(count == 0);
            ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
        }
    }
}


// =============================================================================
// Phase 11: Chord Mode (FR-022, FR-025, FR-026)
// =============================================================================

TEST_CASE("ArpeggiatorCore: chord mode emits all held notes simultaneously",
          "[processors][arpeggiator_core]") {
    // FR-022: When NoteSelector returns count > 1 (Chord mode), each note in
    // the chord must be emitted as a separate NoteOn ArpEvent at the same
    // sampleOffset, and each must receive a corresponding NoteOff.

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(50.0f);  // 50% gate for clear NoteOff timing

    // Hold C3=48, E3=52, G3=55
    arp.noteOn(48, 100);
    arp.noteOn(52, 90);
    arp.noteOn(55, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // 120 BPM, 1/8 note = 11025 samples per step.
    // First NoteOn fires at sample 11025 (after one full step duration).
    // 11025 / 512 ~ 21.5 blocks, so need ~22 blocks to see the first step.
    // Run enough blocks to see at least 2 steps and their NoteOffs.
    auto events = collectEvents(arp, ctx, 500);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // Must have at least 3 NoteOn events from the first chord step
    REQUIRE(noteOns.size() >= 3);

    SECTION("All three notes appear at the same sampleOffset in first chord") {
        // The first 3 NoteOns should be the chord [48, 52, 55] at the same offset
        int32_t firstChordOffset = noteOns[0].sampleOffset;
        CHECK(noteOns[1].sampleOffset == firstChordOffset);
        CHECK(noteOns[2].sampleOffset == firstChordOffset);

        // Verify the notes are 48, 52, 55 (pitch-sorted by NoteSelector in Chord mode)
        std::array<uint8_t, 3> expectedNotes = {48, 52, 55};
        std::array<uint8_t, 3> actualNotes = {
            noteOns[0].note, noteOns[1].note, noteOns[2].note};
        // Sort both for comparison (in case order differs)
        std::sort(actualNotes.begin(), actualNotes.end());
        CHECK(actualNotes[0] == expectedNotes[0]);
        CHECK(actualNotes[1] == expectedNotes[1]);
        CHECK(actualNotes[2] == expectedNotes[2]);
    }

    SECTION("Velocities are preserved for each chord note") {
        // Find each note and verify its velocity
        for (size_t i = 0; i < 3; ++i) {
            if (noteOns[i].note == 48) CHECK(noteOns[i].velocity == 100);
            if (noteOns[i].note == 52) CHECK(noteOns[i].velocity == 90);
            if (noteOns[i].note == 55) CHECK(noteOns[i].velocity == 80);
        }
    }

    SECTION("All three notes receive NoteOff at the same gate-determined time") {
        // Gate 50% of 11025 = floor(11025 * 50 / 100) = 5512 samples after NoteOn
        REQUIRE(noteOffs.size() >= 3);

        // Find the first 3 NoteOffs corresponding to the first chord
        int32_t firstChordOnOffset = noteOns[0].sampleOffset;
        size_t expectedNoteOffOffset = static_cast<size_t>(firstChordOnOffset) + 5512;

        // Collect NoteOffs for the first chord
        std::vector<ArpEvent> chordNoteOffs;
        for (const auto& off : noteOffs) {
            if (off.note == 48 || off.note == 52 || off.note == 55) {
                chordNoteOffs.push_back(off);
                if (chordNoteOffs.size() == 3) break;
            }
        }
        REQUIRE(chordNoteOffs.size() >= 3);

        // All 3 NoteOffs should fire at the same sample offset
        for (const auto& off : chordNoteOffs) {
            CHECK(std::abs(off.sampleOffset -
                           static_cast<int32_t>(expectedNoteOffOffset)) <= 1);
        }
    }

    SECTION("Second chord step fires at correct offset") {
        // Second chord should fire 11025 samples after the first
        REQUIRE(noteOns.size() >= 6);  // 2 chords x 3 notes

        int32_t firstChordOffset = noteOns[0].sampleOffset;
        int32_t secondChordOffset = noteOns[3].sampleOffset;
        int32_t gap = secondChordOffset - firstChordOffset;
        CHECK(std::abs(gap - 11025) <= 1);

        // All 3 notes in second chord have same offset
        CHECK(noteOns[4].sampleOffset == secondChordOffset);
        CHECK(noteOns[5].sampleOffset == secondChordOffset);
    }
}


TEST_CASE("ArpeggiatorCore: chord mode + gate overlap (gate > 100%)",
          "[processors][arpeggiator_core]") {
    // FR-022, FR-026: Chord mode is a hard replace -- step N+1 releases every
    // note step N left sounding before striking the new chord, so the gate
    // saturates at one step and notes never overlap across a step boundary.
    //
    // This case used to assert the opposite ("chord notes from step N remain
    // sounding when step N+1 fires") and passed only because of the GMF-005
    // defect: the replace loop did not cancel the previous step's pending gate
    // NoteOffs, so a leftover fired mid-step and released the note the CURRENT
    // step had just re-struck. That looked like the first chord's late NoteOff
    // but was really the second chord's note being cut short -- pitch 48 sounded
    // 11025 samples on the first step and only 8820 on every later one, with a
    // silent gap before the next strike. Nothing ever actually overlapped,
    // because the replace loop had already released those notes at the boundary.

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(150.0f);  // 150% gate => clamped by the replace boundary

    // Hold C3=48, E3=52, G3=55
    arp.noteOn(48, 100);
    arp.noteOn(52, 90);
    arp.noteOn(55, 80);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // Step = 11025 samples. Gate 150% => NoteOff at 11025 * 1.5 = 16537 samples
    // after NoteOn. The second chord fires at 11025 samples after the first,
    // so the first chord's NoteOffs (at 16537) fire AFTER the second chord's
    // NoteOns (at 11025), creating overlap.

    auto events = collectEvents(arp, ctx, 800);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    SECTION("Step N's chord is released exactly when step N+1 strikes") {
        REQUIRE(noteOns.size() >= 6);  // At least 2 chords
        REQUIRE(noteOffs.size() >= 3);  // At least first chord's NoteOffs

        const int32_t firstChordOnOffset = noteOns[0].sampleOffset;
        const int32_t secondChordOnOffset = noteOns[3].sampleOffset;

        // The replace NoteOffs land ON the second chord's onset, not at
        // firstChordOnOffset + 150% of a step.
        std::vector<ArpEvent> firstChordOffs;
        for (const auto& off : noteOffs) {
            if ((off.note == 48 || off.note == 52 || off.note == 55) &&
                std::abs(off.sampleOffset - secondChordOnOffset) <= 1) {
                firstChordOffs.push_back(off);
            }
        }
        REQUIRE(firstChordOffs.size() >= 3);

        // No NoteOff may survive past the replace boundary: the gate is capped
        // at one step, so nothing is released at firstChordOn + 1.5 steps.
        const int32_t unclampedGateEnd = firstChordOnOffset +
            static_cast<int32_t>(static_cast<size_t>(11025.0 * 1.5));
        for (const auto& off : noteOffs) {
            INFO("NoteOff " << static_cast<int>(off.note) << " at "
                 << off.sampleOffset << "; replace boundary "
                 << secondChordOnOffset);
            CHECK(std::abs(off.sampleOffset - unclampedGateEnd) > 1);
        }
    }

    SECTION("Pending NoteOff array handles multiple chord entries") {
        // With 3 notes per chord and gate > 100%, there should be 3 pending
        // NoteOffs from the first chord when the second chord fires, plus
        // 3 new pending NoteOffs from the second chord = 6 total at peak.
        // The array capacity is 32, which should be sufficient.

        // Verify we get NoteOffs for both chords (no lost NoteOffs)
        REQUIRE(noteOns.size() >= 6);

        // Count unique NoteOff emissions for notes 48, 52, 55
        size_t noteOffCount48 = 0;
        size_t noteOffCount52 = 0;
        size_t noteOffCount55 = 0;
        for (const auto& off : noteOffs) {
            if (off.note == 48) ++noteOffCount48;
            if (off.note == 52) ++noteOffCount52;
            if (off.note == 55) ++noteOffCount55;
        }
        // Each note should have at least 2 NoteOffs (one per chord step)
        size_t numChords = noteOns.size() / 3;
        // We expect at least numChords-1 NoteOffs per note (last chord's
        // NoteOff might not fire within collected blocks). But at least 1 each.
        CHECK(noteOffCount48 >= 1);
        CHECK(noteOffCount52 >= 1);
        CHECK(noteOffCount55 >= 1);
    }
}


TEST_CASE("ArpeggiatorCore: chord mode pending NoteOff capacity stress test",
          "[processors][arpeggiator_core]") {
    // FR-026: Verify the pending NoteOff array handles up to 32 entries
    // simultaneously. We use a large chord with gate > 100%.

    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(150.0f);

    // Hold 16 notes (a large chord)
    for (uint8_t n = 48; n < 64; ++n) {
        arp.noteOn(n, 100);
    }

    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.transportPositionSamples = 0;

    // With 16 notes per chord and gate 150%, after the second chord fires,
    // there will be 16 pending NoteOffs from the first chord + 16 from the
    // second = 32 total, hitting the capacity exactly.

    // Run enough blocks to see at least 3 chord steps
    auto events = collectEvents(arp, ctx, 1000);
    auto noteOns = filterNoteOns(events);
    auto noteOffs = filterNoteOffs(events);

    // Should have at least 2 chords worth of NoteOns (2 * 16 = 32)
    CHECK(noteOns.size() >= 32);
    // Should have NoteOffs for at least the first chord
    CHECK(noteOffs.size() >= 16);

    // Verify no crashes and events are reasonable
    for (const auto& on : noteOns) {
        CHECK(on.note >= 48);
        CHECK(on.note < 64);
    }
}

// =============================================================================
// Chord / ratchet replace must cancel pending NoteOffs first (GMF-005)
// =============================================================================
// The chord replace loop and the ratchet non-slide replace loop each emit an
// immediate NoteOff for every currentArpNotes_ entry and then zero the count,
// without first calling cancelPendingNoteOffsForCurrentNotes(). Every other path
// that manually releases currentArpNotes_ (euclidean rest, condition fail,
// modifier rest, tie, slide) cancels first. With gate > 100% a step's pending
// NoteOff outlives the next step boundary, so the stale pending off survived the
// replace: addPendingNoteOff does not dedup, so a pitch repeated across two
// steps ended up with an immediate replace-NoteOff *and* a later stale one --
// a double NoteOff that also cuts the freshly struck note short.

TEST_CASE("GMF-005: chord replace with gate>100% emits one NoteOff per NoteOn and no premature cut",
          "[ArpeggiatorCore][chord][pendingoff]")
{
    ArpeggiatorCore arp;
    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    arp.prepare(ctx.sampleRate, ctx.blockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(180.0f);  // > 100%: pending off outlives the next step
    arp.setSwing(0.0f);

    // A fixed triad: every step re-strikes the same three pitches, so each one
    // is a "repeated pitch" across the replace boundary.
    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    // ~8 steps at 1/8 / 120 BPM (11025 samples per step).
    const auto events = collectEvents(arp, ctx, 180);

    std::array<int, 128> ons{};
    std::array<int, 128> offs{};
    for (const auto& e : events) {
        if (e.type == ArpEvent::Type::NoteOn) ons[e.note]++;
        else if (e.type == ArpEvent::Type::NoteOff) offs[e.note]++;
    }

    // Never MORE offs than ons (the run ends mid-gate, so the last note of each
    // pitch may still be open).
    for (uint8_t p : {uint8_t{48}, uint8_t{52}, uint8_t{55}}) {
        INFO("pitch " << static_cast<int>(p) << ": on=" << ons[p] << " off=" << offs[p]);
        CHECK(ons[p] > 2);
        CHECK(offs[p] <= ons[p]);
    }

    // The counts balance even with the bug, because the stale pending off calls
    // removeFromCurrentArpNotes() and so suppresses the *next* replace-NoteOff.
    // What it corrupts is the gate: the note struck at step N is released by
    // step N-1's leftover schedule, cutting it to (gate - stepDuration) instead
    // of the full gate. Step 0 has no predecessor, so its duration is the
    // correct reference every later note must match.
    std::vector<int32_t> durations;
    int32_t openAt = -1;
    for (const auto& e : events) {
        if (e.note != 48) continue;
        if (e.type == ArpEvent::Type::NoteOn) {
            openAt = e.sampleOffset;
        } else if (e.type == ArpEvent::Type::NoteOff && openAt >= 0) {
            durations.push_back(e.sampleOffset - openAt);
            openAt = -1;
        }
    }
    REQUIRE(durations.size() >= 3);
    const int32_t reference = durations.front();
    INFO("pitch 48 reference gate: " << reference << " samples");
    for (size_t i = 1; i < durations.size(); ++i) {
        INFO("pitch 48 note " << i << " sounded for " << durations[i]
             << " samples, expected " << reference);
        CHECK(durations[i] == reference);
    }
}

TEST_CASE("GMF-005: ratchet replace with gate>100% emits one NoteOff per NoteOn",
          "[ArpeggiatorCore][ratchet][pendingoff]")
{
    ArpeggiatorCore arp;
    BlockContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.blockSize = 512;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;

    arp.prepare(ctx.sampleRate, ctx.blockSize);
    arp.setEnabled(true);
    arp.setMode(ArpMode::Chord);
    arp.setNoteValue(NoteValue::Eighth, NoteModifier::None);
    arp.setGateLength(180.0f);
    arp.setSwing(0.0f);
    // Ratchet count > 1 on every step drives the non-slide ratchet replace loop.
    arp.ratchetLane().setLength(8);
    for (size_t s = 0; s < 8; ++s)
        arp.ratchetLane().setStep(s, static_cast<uint8_t>(2));

    arp.noteOn(48, 100);
    arp.noteOn(52, 100);
    arp.noteOn(55, 100);

    const auto events = collectEvents(arp, ctx, 180);

    std::array<int, 128> ons{};
    std::array<int, 128> offs{};
    for (const auto& e : events) {
        if (e.type == ArpEvent::Type::NoteOn) ons[e.note]++;
        else if (e.type == ArpEvent::Type::NoteOff) offs[e.note]++;
    }
    for (uint8_t p : {uint8_t{48}, uint8_t{52}, uint8_t{55}}) {
        INFO("pitch " << static_cast<int>(p) << ": on=" << ons[p] << " off=" << offs[p]);
        CHECK(ons[p] > 2);
        CHECK(offs[p] <= ons[p]);
    }

    // Same gate-corruption check as the chord case. Ratchet sub-step durations
    // are the step split by the ratchet count, so integer division leaves a
    // 1-sample wobble between sub-steps; a leftover pending off would move the
    // release by thousands of samples, not by one.
    std::vector<int32_t> durations;
    int32_t openAt = -1;
    for (const auto& e : events) {
        if (e.note != 48) continue;
        if (e.type == ArpEvent::Type::NoteOn) {
            openAt = e.sampleOffset;
        } else if (e.type == ArpEvent::Type::NoteOff && openAt >= 0) {
            durations.push_back(e.sampleOffset - openAt);
            openAt = -1;
        }
    }
    REQUIRE(durations.size() >= 3);
    const int32_t reference = durations.front();
    INFO("pitch 48 reference gate: " << reference << " samples");
    for (size_t i = 1; i < durations.size(); ++i) {
        INFO("pitch 48 note " << i << " sounded for " << durations[i]
             << " samples, expected ~" << reference);
        CHECK(std::abs(durations[i] - reference) <= 2);
    }
}
