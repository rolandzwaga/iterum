// ==============================================================================
// Unit Tests: MidiNoteDelay (Layer 2 Processor)
// ==============================================================================
// Tests for the MIDI echo post-processor for arpeggiator output.
// Reference: Gradus MIDI Delay Lane plan
// ==============================================================================

#include <krate/dsp/processors/midi_note_delay.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <vector>

using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

static BlockContext makeCtx(double sampleRate = 44100.0, size_t blockSize = 512,
                            double tempo = 120.0)
{
    BlockContext ctx{};
    ctx.sampleRate = sampleRate;
    ctx.blockSize = blockSize;
    ctx.tempoBPM = tempo;
    ctx.isPlaying = true;
    return ctx;
}

/// Create a single NoteOn ArpEvent at given sample offset.
static ArpEvent makeNoteOn(uint8_t note, uint8_t velocity, int32_t sampleOffset = 0)
{
    return ArpEvent{ArpEvent::Type::NoteOn, note, velocity, sampleOffset, false};
}

/// Collect all events across multiple blocks, adjusting offsets to absolute positions.
static std::vector<ArpEvent> collectEchoes(MidiNoteDelay& delay, BlockContext& ctx,
                                           size_t numBlocks,
                                           const ArpEvent* initialEvent = nullptr,
                                           size_t currentStep = 0)
{
    std::vector<ArpEvent> allEvents;
    std::array<ArpEvent, 512> output;

    for (size_t b = 0; b < numBlocks; ++b) {
        std::array<ArpEvent, 1> input;
        size_t inputCount = 0;

        // Only provide the initial event in block 0
        if (b == 0 && initialEvent != nullptr) {
            input[0] = *initialEvent;
            inputCount = 1;
        }

        size_t count = delay.process(ctx,
            std::span<const ArpEvent>(input.data(), inputCount),
            inputCount,
            std::span<ArpEvent>(output.data(), output.size()),
            currentStep);

        for (size_t i = 0; i < count; ++i) {
            ArpEvent evt = output[i];
            evt.sampleOffset += static_cast<int32_t>(b * ctx.blockSize);
            allEvents.push_back(evt);
        }
    }
    return allEvents;
}

/// Filter events by type.
static std::vector<ArpEvent> filterByType(const std::vector<ArpEvent>& events,
                                          ArpEvent::Type type)
{
    std::vector<ArpEvent> filtered;
    for (const auto& e : events) {
        if (e.type == type) filtered.push_back(e);
    }
    return filtered;
}

// =============================================================================
// Basic Functionality
// =============================================================================

TEST_CASE("MidiNoteDelay: no echoes when feedback is 0", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx();

    // Default config has feedbackCount = 0
    auto noteOn = makeNoteOn(60, 100, 0);
    std::array<ArpEvent, 64> output;

    size_t count = delay.process(ctx,
        std::span<const ArpEvent>(&noteOn, 1), 1,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    // Only the pass-through event
    REQUIRE(count == 1);
    CHECK(output[0].type == ArpEvent::Type::NoteOn);
    CHECK(output[0].note == 60);
    CHECK(output[0].velocity == 100);
    CHECK(delay.pendingCount() == 0);
}

TEST_CASE("MidiNoteDelay: pass-through preserves all input events", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx();

    std::array<ArpEvent, 3> input = {{
        {ArpEvent::Type::NoteOn, 60, 100, 0, false},
        {ArpEvent::Type::NoteOff, 60, 0, 100, false},
        {ArpEvent::Type::NoteOn, 64, 80, 200, false},
    }};
    std::array<ArpEvent, 64> output;

    size_t count = delay.process(ctx,
        std::span<const ArpEvent>(input.data(), input.size()), input.size(),
        std::span<ArpEvent>(output.data(), output.size()), 0);

    REQUIRE(count >= 3);
    CHECK(output[0].type == ArpEvent::Type::NoteOn);
    CHECK(output[0].note == 60);
    CHECK(output[1].type == ArpEvent::Type::NoteOff);
    CHECK(output[1].note == 60);
    CHECK(output[2].type == ArpEvent::Type::NoteOn);
    CHECK(output[2].note == 64);
}

TEST_CASE("MidiNoteDelay: NoteOff events do not generate echoes", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx();

    MidiDelayStepConfig config;
    config.active = true;
    config.feedbackCount = 3;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 100.0f;
    delay.setStepConfig(0, config);

    ArpEvent noteOff{ArpEvent::Type::NoteOff, 60, 0, 0, false};
    std::array<ArpEvent, 64> output;

    size_t count = delay.process(ctx,
        std::span<const ArpEvent>(&noteOff, 1), 1,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    CHECK(count == 1); // Only pass-through
    CHECK(delay.pendingCount() == 0);
}

// =============================================================================
// Echo Timing (Free Mode)
// =============================================================================

TEST_CASE("MidiNoteDelay: single echo at correct time (free mode)", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 100.0f;  // 100ms = 4410 samples at 44100Hz
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;  // No decay — echo same velocity
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);

    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    // Should have 2 NoteOns: original + 1 echo
    REQUIRE(noteOns.size() == 2);
    CHECK(noteOns[0].sampleOffset == 0);        // Original
    CHECK(noteOns[1].sampleOffset == 4410);      // Echo at 100ms
    CHECK(noteOns[1].note == 60);
    CHECK(noteOns[1].velocity == 100);           // No decay
}

TEST_CASE("MidiNoteDelay: multiple echoes with correct spacing", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;   // 50ms = 2205 samples
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4); // Original + 3 echoes
    CHECK(noteOns[0].sampleOffset == 0);
    CHECK(noteOns[1].sampleOffset == 2205);  // 50ms
    CHECK(noteOns[2].sampleOffset == 4410);  // 100ms
    CHECK(noteOns[3].sampleOffset == 6615);  // 150ms
}

TEST_CASE("MidiNoteDelay: echo from non-zero source offset", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;  // 2205 samples
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 256); // Source at sample 256
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 2);
    CHECK(noteOns[0].sampleOffset == 256);
    CHECK(noteOns[1].sampleOffset == 256 + 2205); // 256 + 50ms
}

// =============================================================================
// Echo Timing (Synced Mode)
// =============================================================================

TEST_CASE("MidiNoteDelay: tempo-synced echo timing", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512, 120.0); // 120 BPM

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Synced;
    config.noteValueIndex = 10;  // 1/8 note = 250ms at 120 BPM = 11025 samples
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 40, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 2);
    CHECK(noteOns[0].sampleOffset == 0);
    CHECK(noteOns[1].sampleOffset == 11025); // 1/8 at 120 BPM
}

TEST_CASE("MidiNoteDelay: dotted note value timing", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512, 120.0);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Synced;
    config.noteValueIndex = 11;  // 1/8D = 375ms at 120 BPM = 16537 samples
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 60, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 2);
    CHECK(noteOns[0].sampleOffset == 0);
    // 1/8 dotted = 0.75 beats = 375ms = 16537.5 → 16537 samples
    CHECK(noteOns[1].sampleOffset == Catch::Approx(16537).margin(1));
}

// =============================================================================
// Velocity Decay
// =============================================================================

TEST_CASE("MidiNoteDelay: velocity decay per echo", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 4;
    config.velocityDecay = 0.5f;  // Each echo = prev * 0.5
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 5); // Original + 4 echoes
    CHECK(noteOns[0].velocity == 100);  // Original
    CHECK(noteOns[1].velocity == 50);   // 100 * 0.5^1
    CHECK(noteOns[2].velocity == 25);   // 100 * 0.5^2
    CHECK(noteOns[3].velocity == 13);   // 100 * 0.5^3 ≈ 12.5 → 13
    CHECK(noteOns[4].velocity == 6);    // 100 * 0.5^4 = 6.25 → 6
}

TEST_CASE("MidiNoteDelay: zero decay means no velocity change", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 80, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);
    for (const auto& e : noteOns) {
        CHECK(e.velocity == 80);
    }
}

TEST_CASE("MidiNoteDelay: full decay stops echoes early", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 16;
    config.velocityDecay = 1.0f;  // (1 - 1.0)^N = 0 for N >= 1
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    // Only the original — all echoes would have velocity 0
    REQUIRE(noteOns.size() == 1);
}

// =============================================================================
// Pitch Shift
// =============================================================================

TEST_CASE("MidiNoteDelay: pitch shift per echo", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    config.pitchShiftPerRepeat = 7;  // Perfect fifth per echo
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);
    CHECK(noteOns[0].note == 60);
    CHECK(noteOns[1].note == 67);  // +7
    CHECK(noteOns[2].note == 74);  // +14
    CHECK(noteOns[3].note == 81);  // +21
}

TEST_CASE("MidiNoteDelay: negative pitch shift", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    config.pitchShiftPerRepeat = -12;  // Octave down per echo
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);
    CHECK(noteOns[0].note == 60);
    CHECK(noteOns[1].note == 48);  // -12
    CHECK(noteOns[2].note == 36);  // -24
    CHECK(noteOns[3].note == 24);  // -36
}

TEST_CASE("MidiNoteDelay: pitch clamped to MIDI range", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    config.pitchShiftPerRepeat = 24;  // +24 per echo
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(100, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);
    CHECK(noteOns[0].note == 100);
    CHECK(noteOns[1].note == 124); // 100 + 24
    CHECK(noteOns[2].note == 127); // Clamped (100 + 48 = 148 → 127)
    CHECK(noteOns[3].note == 127); // Clamped
}

TEST_CASE("MidiNoteDelay: pitch clamped at bottom", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.0f;
    config.pitchShiftPerRepeat = -24;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(20, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);
    CHECK(noteOns[0].note == 20);
    CHECK(noteOns[1].note == 0);   // 20 - 24 → clamped to 0
    CHECK(noteOns[2].note == 0);
    CHECK(noteOns[3].note == 0);
}

// =============================================================================
// Gate Scaling
// =============================================================================

TEST_CASE("MidiNoteDelay: NoteOff emitted after gate duration", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;  // 2205 samples
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    config.gateScaling = 1.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOffs = filterByType(events, ArpEvent::Type::NoteOff);

    // Should have at least 1 NoteOff for the echo
    REQUIRE(noteOffs.size() >= 1);

    // Find the echo's NoteOff (not the original's)
    // Echo NoteOn at sample 2205, gate = 80% of 2205 = 1764
    // Echo NoteOff at 2205 + 1764 = 3969
    bool foundEchoNoteOff = false;
    for (const auto& e : noteOffs) {
        if (e.sampleOffset > 2000) {
            foundEchoNoteOff = true;
            CHECK(e.note == 60);
            CHECK(e.sampleOffset == Catch::Approx(2205 + 1764).margin(2));
        }
    }
    CHECK(foundEchoNoteOff);
}

TEST_CASE("MidiNoteDelay: gate scaling shrinks echo gates", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 100.0f;  // 4410 samples
    config.feedbackCount = 2;
    config.velocityDecay = 0.0f;
    config.gateScaling = 0.5f;  // Each echo's gate halves
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 40, &noteOn, 0);
    auto noteOffs = filterByType(events, ArpEvent::Type::NoteOff);

    // Base gate = 80% of 4410 = 3528
    // Echo 1 gate = 3528 * 0.5 = 1764 → NoteOff at 4410 + 1764 = 6174
    // Echo 2 gate = 3528 * 0.25 = 882 → NoteOff at 8820 + 882 = 9702
    int echoNoteOffCount = 0;
    for (const auto& e : noteOffs) {
        if (e.sampleOffset > 4000 && e.sampleOffset < 7000) {
            CHECK(e.sampleOffset == Catch::Approx(6174).margin(2));
            ++echoNoteOffCount;
        }
        if (e.sampleOffset > 9000 && e.sampleOffset < 10500) {
            CHECK(e.sampleOffset == Catch::Approx(9702).margin(2));
            ++echoNoteOffCount;
        }
    }
    CHECK(echoNoteOffCount == 2);
}

// =============================================================================
// Per-Step Configuration
// =============================================================================

TEST_CASE("MidiNoteDelay: different steps use different configs", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    // Step 0: 2 echoes
    MidiDelayStepConfig config0;
    config0.active = true;
    config0.timeMode = TimeMode::Free;
    config0.delayTimeMs = 50.0f;
    config0.feedbackCount = 2;
    config0.velocityDecay = 0.0f;
    delay.setStepConfig(0, config0);

    // Step 1: no echoes (bypass)
    MidiDelayStepConfig config1;
    config1.feedbackCount = 0;
    delay.setStepConfig(1, config1);

    // Process with step 0
    auto noteOn0 = makeNoteOn(60, 100, 0);
    auto events0 = collectEchoes(delay, ctx, 20, &noteOn0, 0);
    auto noteOns0 = filterByType(events0, ArpEvent::Type::NoteOn);
    CHECK(noteOns0.size() == 3); // Original + 2 echoes

    // Reset and process with step 1
    delay.reset();
    auto noteOn1 = makeNoteOn(64, 100, 0);
    auto events1 = collectEchoes(delay, ctx, 20, &noteOn1, 1);
    auto noteOns1 = filterByType(events1, ArpEvent::Type::NoteOn);
    CHECK(noteOns1.size() == 1); // Only original (no echoes)
}

// =============================================================================
// Reset
// =============================================================================

TEST_CASE("MidiNoteDelay: reset clears pending echoes", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 500.0f;  // Long delay
    config.feedbackCount = 8;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    // Schedule echoes
    auto noteOn = makeNoteOn(60, 100, 0);
    std::array<ArpEvent, 64> output;
    delay.process(ctx, std::span<const ArpEvent>(&noteOn, 1), 1,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    CHECK(delay.pendingCount() > 0);

    delay.reset();
    CHECK(delay.pendingCount() == 0);
}

// =============================================================================
// Overflow / Capacity
// =============================================================================

TEST_CASE("MidiNoteDelay: overflow steals oldest echoes", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 1000.0f;  // Long delay to keep echoes pending
    config.feedbackCount = 16;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    // Send many notes to overwhelm the buffer (256 max)
    std::array<ArpEvent, 64> output;
    for (int i = 0; i < 20; ++i) {
        auto noteOn = makeNoteOn(60 + static_cast<uint8_t>(i % 12), 100, 0);
        delay.process(ctx, std::span<const ArpEvent>(&noteOn, 1), 1,
            std::span<ArpEvent>(output.data(), output.size()), 0);
    }

    // Should not exceed max capacity
    CHECK(delay.pendingCount() <= MidiNoteDelay::kMaxPendingEchoes);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("MidiNoteDelay: minimum delay time clamped", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 1.0f;  // Below minimum (10ms)
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 10, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 2);
    // 10ms minimum = 441 samples at 44100
    CHECK(noteOns[1].sampleOffset == 441);
}

TEST_CASE("MidiNoteDelay: empty input produces no output initially", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx();
    std::array<ArpEvent, 64> output;

    size_t count = delay.process(ctx,
        std::span<const ArpEvent>{}, 0,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    CHECK(count == 0);
}

TEST_CASE("MidiNoteDelay: max feedback count is 16", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 10.0f;  // 441 samples — short enough to fit in ~20 blocks
    config.feedbackCount = 100;  // Way above max — should clamp to 16
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 40, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    // Original + 16 echoes = 17
    CHECK(noteOns.size() == 17);
}

TEST_CASE("MidiNoteDelay: step config out of range uses clamped step", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.feedbackCount = 1;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(31, config);  // Last valid step

    // Access step 100 (out of range) — should clamp to step 31
    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 100);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    CHECK(noteOns.size() == 2); // Original + 1 echo (from step 31's config)
}

TEST_CASE("MidiNoteDelay: kSkip events pass through without echoes", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx();

    MidiDelayStepConfig config;
    config.active = true;
    config.feedbackCount = 3;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    delay.setStepConfig(0, config);

    ArpEvent skipEvent{ArpEvent::Type::kSkip, 5, 0, 0, false};
    std::array<ArpEvent, 64> output;

    size_t count = delay.process(ctx,
        std::span<const ArpEvent>(&skipEvent, 1), 1,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    CHECK(count == 1);
    CHECK(output[0].type == ArpEvent::Type::kSkip);
    CHECK(delay.pendingCount() == 0);
}

// =============================================================================
// Combined Parameters
// =============================================================================

TEST_CASE("MidiNoteDelay: combined pitch shift + velocity decay", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 50.0f;
    config.feedbackCount = 3;
    config.velocityDecay = 0.3f;
    config.pitchShiftPerRepeat = 12;  // Octave up per echo
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(48, 100, 0);
    auto events = collectEchoes(delay, ctx, 20, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 4);

    // Check pitch progression
    CHECK(noteOns[0].note == 48);
    CHECK(noteOns[1].note == 60);  // +12
    CHECK(noteOns[2].note == 72);  // +24
    CHECK(noteOns[3].note == 84);  // +36

    // Check velocity decay: 100 * 0.7^N
    CHECK(noteOns[0].velocity == 100);
    CHECK(noteOns[1].velocity == 70);   // 100 * 0.7
    CHECK(noteOns[2].velocity == 49);   // 100 * 0.49
    CHECK(noteOns[3].velocity == 34);   // 100 * 0.343 ≈ 34
}

// =============================================================================
// Multi-Block Spanning
// =============================================================================

TEST_CASE("MidiNoteDelay: echoes span multiple process blocks", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 256); // Small blocks

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 100.0f;  // 4410 samples = ~17 blocks of 256
    config.feedbackCount = 2;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    auto noteOn = makeNoteOn(60, 100, 0);
    auto events = collectEchoes(delay, ctx, 50, &noteOn, 0);
    auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);

    REQUIRE(noteOns.size() == 3);
    CHECK(noteOns[0].sampleOffset == 0);
    CHECK(noteOns[1].sampleOffset == Catch::Approx(4410).margin(1));
    CHECK(noteOns[2].sampleOffset == Catch::Approx(8820).margin(1));
}

// =============================================================================
// Tempo Changes (Synced Mode)
// =============================================================================

TEST_CASE("MidiNoteDelay: different tempo changes synced delay time", "[MidiNoteDelay]")
{
    MidiNoteDelay delay;

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Synced;
    config.noteValueIndex = 13;  // 1/4 note
    config.feedbackCount = 1;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    // At 120 BPM: 1/4 = 500ms = 22050 samples
    {
        auto ctx = makeCtx(44100.0, 512, 120.0);
        auto noteOn = makeNoteOn(60, 100, 0);
        auto events = collectEchoes(delay, ctx, 80, &noteOn, 0);
        auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);
        REQUIRE(noteOns.size() == 2);
        CHECK(noteOns[1].sampleOffset == 22050);
    }

    delay.reset();

    // At 60 BPM: 1/4 = 1000ms = 44100 samples
    {
        auto ctx = makeCtx(44100.0, 512, 60.0);
        auto noteOn = makeNoteOn(60, 100, 0);
        auto events = collectEchoes(delay, ctx, 120, &noteOn, 0);
        auto noteOns = filterByType(events, ArpEvent::Type::NoteOn);
        REQUIRE(noteOns.size() == 2);
        CHECK(noteOns[1].sampleOffset == 44100);
    }
}

// =============================================================================
// Output-span saturation (Gradus audit F6)
// =============================================================================
// The Gradus processor hands MidiNoteDelay a fixed 512-slot output span shared
// with the arp's own events. When that span fills, `emitDueEchoes` stops
// emitting -- including NoteOffs for echoes whose NoteOn already reached the
// host. `advanceAndCompact`'s safety rule then discards those echoes outright
// once their NoteOff is more than one block overdue, so the NoteOn is never
// balanced and the note hangs downstream forever.
//
// This drives that path directly by shrinking the output span for a stretch of
// blocks (no input events in those blocks, so only echo emission is starved),
// then draining with a generous span. Every echo NoteOn that reached the output
// must eventually be matched by a NoteOff.

TEST_CASE("MidiNoteDelay: output-span saturation never strands an echo NoteOff",
          "[MidiNoteDelay][saturation][F6]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 10.0f;   // 441 samples; gate = 0.8 * 441 = 352 samples
    config.feedbackCount = 8;
    config.velocityDecay = 0.0f;  // keep every echo at full velocity
    delay.setStepConfig(0, config);

    // Tally per-pitch NoteOn/NoteOff counts over the whole run.
    std::array<int, 128> onCount{};
    std::array<int, 128> offCount{};

    std::array<ArpEvent, 512> output{};

    auto runBlock = [&](std::span<const ArpEvent> input, size_t spanSize) {
        size_t count = delay.process(ctx, input, input.size(),
                                     std::span<ArpEvent>(output.data(), spanSize), 0);
        for (size_t i = 0; i < count; ++i) {
            if (output[i].type == ArpEvent::Type::NoteOn)  ++onCount[output[i].note];
            else if (output[i].type == ArpEvent::Type::NoteOff) ++offCount[output[i].note];
        }
    };

    // Block 0: 16 source notes at offset 0, each as a NoteOn/NoteOff pair so the
    // pass-through stream is self-balancing (MidiNoteDelay only owes NoteOffs for
    // the echoes it generates; balancing the source note is the arp core's job).
    // Generous span so every source is passed through and schedules its 8 echoes
    // (128 pending < kMaxPendingEchoes, so the oldest-stealing overflow path is
    // NOT what we are testing here).
    std::array<ArpEvent, 32> sources{};
    for (size_t i = 0; i < 16; ++i) {
        const auto pitch = static_cast<uint8_t>(60 + i);
        sources[i * 2]     = makeNoteOn(pitch, 100, 0);
        sources[i * 2 + 1] = ArpEvent{ArpEvent::Type::NoteOff, pitch, 0, 0, false};
    }
    runBlock(std::span<const ArpEvent>(sources.data(), sources.size()), 512);

    // Blocks 1-6: output span throttled to 4 events and no new input. Many
    // echo NoteOffs are due each block but only a handful fit, so the rest are
    // starved for more than one block -- exactly the condition the safety rule
    // in advanceAndCompact discards.
    for (int b = 0; b < 6; ++b) {
        runBlock(std::span<const ArpEvent>(), 4);
    }

    // Drain: generous span, plenty of blocks for every outstanding NoteOff.
    for (int b = 0; b < 200; ++b) {
        runBlock(std::span<const ArpEvent>(), 512);
    }

    for (int pitch = 0; pitch < 128; ++pitch) {
        INFO("pitch=" << pitch << " on=" << onCount[pitch] << " off=" << offCount[pitch]);
        REQUIRE(offCount[pitch] >= onCount[pitch]);
    }
}

// =============================================================================
// Output event ordering (Gradus audit F5)
// =============================================================================
// process() appends pass-through arp events first, then emergency NoteOffs at
// sampleOffset 0, then due echoes in pending-array order -- and the Gradus
// processor forwards the combined span to the host verbatim, with no sort. A
// due echo emitted after a later pass-through arp event therefore produces a
// backwards step in sampleOffset within one block.
//
// Verify-first: this asserts the ordering claim itself, NOT stuck notes. Both
// audit verifiers agreed an echo's own NoteOff can never precede its NoteOn, so
// the hanging-note consequence is unreachable; what remains is cross-event
// timing order, which strict hosts are entitled to expect.

TEST_CASE("MidiNoteDelay: output events are non-decreasing in sampleOffset",
          "[MidiNoteDelay][ordering][F5]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 512);

    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 10.0f;   // 441 samples -- due inside the next block
    config.feedbackCount = 4;
    config.velocityDecay = 0.0f;
    delay.setStepConfig(0, config);

    std::array<ArpEvent, 512> output{};

    // Block 0: a note at offset 0 schedules echoes at 441, 882, ...
    std::array<ArpEvent, 1> first{makeNoteOn(60, 100, 0)};
    (void)delay.process(ctx, std::span<const ArpEvent>(first.data(), 1), 1,
                        std::span<ArpEvent>(output.data(), output.size()), 0);

    // Block 1: a pass-through arp event LATE in the block (offset 500) is
    // appended before the echo that is due EARLY in the same block (offset ~441
    // - 512 -> clamped to 0), so the raw stream steps backwards.
    std::array<ArpEvent, 1> second{makeNoteOn(64, 100, 500)};
    const size_t count = delay.process(
        ctx, std::span<const ArpEvent>(second.data(), 1), 1,
        std::span<ArpEvent>(output.data(), output.size()), 0);

    REQUIRE(count >= 2);
    for (size_t i = 1; i < count; ++i) {
        INFO("event " << i << " offset " << output[i].sampleOffset
             << " follows offset " << output[i - 1].sampleOffset);
        CHECK(output[i].sampleOffset >= output[i - 1].sampleOffset);
    }
}

// =============================================================================
// Overflow eviction must never drop a NoteOff obligation (GMF-003)
// =============================================================================
// Eviction of the oldest slot on a full 256-entry ring is unconditional, but the
// emergency-NoteOff enqueue for an already-sounded evicted echo was gated on
// `emergencyNoteOffCount_ < kMaxEmergencyNoteOffs` (16). Evictions all happen in
// pass 1 of process() (scheduleEchoes) while emitEmergencyNoteOffs runs in pass
// 2, so within a single block the queue only grows: the 17th and later sounded
// evictions had their NoteOff obligation discarded outright, hanging those notes.

TEST_CASE("GMF-003: overflow eviction of >16 sounded echoes never drops a NoteOff",
          "[MidiNoteDelay][overflow][stuck]")
{
    MidiNoteDelay delay;
    auto ctx = makeCtx(44100.0, 4096);

    // 10 ms delay: all 16 echoes of a source note sound within ~7k samples (two
    // blocks). Max gate scaling compounds geometrically, so none of them close
    // again for minutes -- every pending echo sits sounded-but-unclosed, which is
    // exactly the state that makes an eviction strand a NoteOff.
    MidiDelayStepConfig config;
    config.active = true;
    config.timeMode = TimeMode::Free;
    config.delayTimeMs = 10.0f;
    config.feedbackCount = 16;
    config.velocityDecay = 0.0f;
    config.pitchShiftPerRepeat = 0;
    config.gateScaling = 2.0f;
    delay.setStepConfig(0, config);

    std::array<int, 128> ons{};
    std::array<int, 128> offs{};
    std::array<ArpEvent, 512> output{};

    auto runBlock = [&](std::span<const ArpEvent> input) {
        const size_t count = delay.process(
            ctx, input, input.size(),
            std::span<ArpEvent>(output.data(), output.size()), 0);
        REQUIRE(delay.pendingCount() <= 256);
        for (size_t i = 0; i < count; ++i) {
            const auto& e = output[i];
            if (e.type == ArpEvent::Type::NoteOn) ons[e.note]++;
            else if (e.type == ArpEvent::Type::NoteOff) offs[e.note]++;
        }
    };

    // Feed each source note with its own NoteOff so pass-through traffic is
    // self-balancing and any imbalance below is attributable to echoes alone.
    auto sourcePair = [](uint8_t pitch) {
        return std::array<ArpEvent, 2>{
            ArpEvent{ArpEvent::Type::NoteOn, pitch, 100, 0, false},
            ArpEvent{ArpEvent::Type::NoteOff, pitch, 0, 1, false}};
    };

    // Fill the 256-slot ring. Short-gate early echoes still close and compact
    // out, so keep feeding one source note per block until the ring saturates;
    // by then the surviving front slots are long-gated echoes that sounded
    // blocks ago and cannot close for minutes.
    // 2 source notes per block = 32 new echoes per block, well past the ring's
    // headroom, so once it saturates every further add evicts a front slot that
    // sounded blocks ago. (pendingCount() reads below 256 between blocks: the
    // end-of-block compaction retires the short-gated early echoes.)
    for (int n = 0; n < 40; ++n) {
        std::array<ArpEvent, 4> fill{};
        for (int k = 0; k < 2; ++k) {
            const auto pitch = static_cast<uint8_t>(40 + ((n * 2 + k) % 32));
            fill[static_cast<size_t>(k) * 2] =
                ArpEvent{ArpEvent::Type::NoteOn, pitch, 100, 0, false};
            fill[static_cast<size_t>(k) * 2 + 1] =
                ArpEvent{ArpEvent::Type::NoteOff, pitch, 0, 1, false};
        }
        runBlock(std::span<const ArpEvent>(fill.data(), fill.size()));
    }
    REQUIRE(delay.pendingCount() >= 200);

    // Dense burst in ONE block: 3 sources x 16 echoes = 48 evictions of
    // sounded-but-unclosed front slots, far past the 16-entry emergency queue.
    std::array<ArpEvent, 6> burst{};
    for (int n = 0; n < 3; ++n) {
        burst[static_cast<size_t>(n) * 2] =
            ArpEvent{ArpEvent::Type::NoteOn, static_cast<uint8_t>(100 + n), 100, 0, false};
        burst[static_cast<size_t>(n) * 2 + 1] =
            ArpEvent{ArpEvent::Type::NoteOff, static_cast<uint8_t>(100 + n), 0, 1, false};
    }
    runBlock(std::span<const ArpEvent>(burst.data(), burst.size()));

    // Discharge every remaining obligation, then drain the emergency queue.
    delay.flushWithNoteOffs();
    for (int b = 0; b < 40; ++b) runBlock({});

    for (int p = 0; p < 128; ++p) {
        INFO("pitch " << p << ": on=" << ons[static_cast<size_t>(p)]
                      << " off=" << offs[static_cast<size_t>(p)]);
        CHECK(offs[static_cast<size_t>(p)] >= ons[static_cast<size_t>(p)]);
    }
}
