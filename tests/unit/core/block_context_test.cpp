// ==============================================================================
// Layer 0: Core Utility Tests - BlockContext
// ==============================================================================
// Tests for per-block processing context.
//
// Constitution Compliance:
// - Principle XII: Test-First Development
//
// Reference: specs/017-layer0-utilities/spec.md (Phase 3 - US1)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "dsp/core/block_context.h"

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// Default Value Tests (T014 - FR-007)
// =============================================================================

TEST_CASE("BlockContext default construction", "[block_context][US1]") {
    BlockContext ctx;

    SECTION("sampleRate defaults to 44100 Hz") {
        REQUIRE(ctx.sampleRate == 44100.0);
    }

    SECTION("blockSize defaults to 512 samples") {
        REQUIRE(ctx.blockSize == 512);
    }

    SECTION("tempoBPM defaults to 120 BPM") {
        REQUIRE(ctx.tempoBPM == 120.0);
    }

    SECTION("time signature defaults to 4/4") {
        REQUIRE(ctx.timeSignatureNumerator == 4);
        REQUIRE(ctx.timeSignatureDenominator == 4);
    }

    SECTION("isPlaying defaults to false (stopped)") {
        REQUIRE(ctx.isPlaying == false);
    }

    SECTION("transportPositionSamples defaults to 0") {
        REQUIRE(ctx.transportPositionSamples == 0);
    }
}

// =============================================================================
// Member Access Tests (T015 - FR-001 to FR-006)
// =============================================================================

TEST_CASE("BlockContext member access", "[block_context][US1]") {
    BlockContext ctx;

    SECTION("sampleRate is modifiable (FR-001)") {
        ctx.sampleRate = 48000.0;
        REQUIRE(ctx.sampleRate == 48000.0);

        ctx.sampleRate = 192000.0;
        REQUIRE(ctx.sampleRate == 192000.0);
    }

    SECTION("blockSize is modifiable (FR-002)") {
        ctx.blockSize = 256;
        REQUIRE(ctx.blockSize == 256);

        ctx.blockSize = 1024;
        REQUIRE(ctx.blockSize == 1024);
    }

    SECTION("tempoBPM is modifiable (FR-003)") {
        ctx.tempoBPM = 90.0;
        REQUIRE(ctx.tempoBPM == 90.0);

        ctx.tempoBPM = 180.0;
        REQUIRE(ctx.tempoBPM == 180.0);
    }

    SECTION("time signature is modifiable (FR-004)") {
        ctx.timeSignatureNumerator = 3;
        ctx.timeSignatureDenominator = 4;
        REQUIRE(ctx.timeSignatureNumerator == 3);
        REQUIRE(ctx.timeSignatureDenominator == 4);

        ctx.timeSignatureNumerator = 6;
        ctx.timeSignatureDenominator = 8;
        REQUIRE(ctx.timeSignatureNumerator == 6);
        REQUIRE(ctx.timeSignatureDenominator == 8);
    }

    SECTION("isPlaying is modifiable (FR-005)") {
        ctx.isPlaying = true;
        REQUIRE(ctx.isPlaying == true);

        ctx.isPlaying = false;
        REQUIRE(ctx.isPlaying == false);
    }

    SECTION("transportPositionSamples is modifiable (FR-006)") {
        ctx.transportPositionSamples = 44100;  // 1 second at 44.1kHz
        REQUIRE(ctx.transportPositionSamples == 44100);

        ctx.transportPositionSamples = -1000;  // Pre-roll
        REQUIRE(ctx.transportPositionSamples == -1000);
    }
}

// =============================================================================
// tempoToSamples() Basic Tests (T016 - FR-008)
// =============================================================================

TEST_CASE("tempoToSamples basic calculations", "[block_context][US1]") {
    BlockContext ctx;

    SECTION("Quarter note at 120 BPM, 44100 Hz = 22050 samples") {
        // From spec.md US1 acceptance scenario 1:
        // At 120 BPM, one beat = 0.5 seconds
        // 0.5 sec * 44100 Hz = 22050 samples
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 120.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 22050);
    }

    SECTION("Whole note at 120 BPM, 44100 Hz = 88200 samples") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 120.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Whole);
        REQUIRE(samples == 88200);  // 4 beats * 22050
    }

    SECTION("Eighth note at 120 BPM, 44100 Hz = 11025 samples") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 120.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Eighth);
        REQUIRE(samples == 11025);  // 0.5 beats * 22050
    }

    SECTION("Quarter note at 60 BPM, 44100 Hz = 44100 samples") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 60.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 44100);  // 1 second = 1 beat at 60 BPM
    }

    SECTION("Quarter note at 48000 Hz sample rate") {
        ctx.sampleRate = 48000.0;
        ctx.tempoBPM = 120.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 24000);  // 0.5 sec * 48000 Hz
    }

    SECTION("All note values at 120 BPM, 44100 Hz") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 120.0;

        REQUIRE(ctx.tempoToSamples(NoteValue::Whole) == 88200);         // 4 beats
        REQUIRE(ctx.tempoToSamples(NoteValue::Half) == 44100);          // 2 beats
        REQUIRE(ctx.tempoToSamples(NoteValue::Quarter) == 22050);       // 1 beat
        REQUIRE(ctx.tempoToSamples(NoteValue::Eighth) == 11025);        // 0.5 beats
        REQUIRE(ctx.tempoToSamples(NoteValue::Sixteenth) == 5512);      // 0.25 beats (truncated)
        REQUIRE(ctx.tempoToSamples(NoteValue::ThirtySecond) == 2756);   // 0.125 beats (truncated)
    }
}

// =============================================================================
// tempoToSamples() with Modifiers Tests (T017)
// =============================================================================

TEST_CASE("tempoToSamples with modifiers", "[block_context][US1]") {
    BlockContext ctx;
    ctx.sampleRate = 48000.0;
    ctx.tempoBPM = 90.0;

    SECTION("Dotted eighth at 90 BPM, 48000 Hz = 24000 samples") {
        // From spec.md US1 acceptance scenario 2:
        // dotted eighth = 0.75 beats
        // At 90 BPM: 0.75 beats * (60/90 sec/beat) * 48000 Hz = 24000 samples
        size_t samples = ctx.tempoToSamples(NoteValue::Eighth, NoteModifier::Dotted);
        REQUIRE(samples == 24000);
    }

    SECTION("Dotted quarter at 120 BPM = 1.5x quarter note") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 120.0;

        size_t quarterSamples = ctx.tempoToSamples(NoteValue::Quarter);
        size_t dottedQuarterSamples = ctx.tempoToSamples(NoteValue::Quarter, NoteModifier::Dotted);

        REQUIRE(dottedQuarterSamples == 33075);  // 1.5 * 22050
    }

    SECTION("Triplet quarter at 120 BPM = 2/3x quarter note") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 120.0;

        size_t quarterSamples = ctx.tempoToSamples(NoteValue::Quarter);  // 22050
        size_t tripletQuarterSamples = ctx.tempoToSamples(NoteValue::Quarter, NoteModifier::Triplet);

        // 22050 * (2/3) = 14700
        REQUIRE(tripletQuarterSamples == 14700);
    }

    SECTION("None modifier has no effect") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 120.0;

        size_t withNone = ctx.tempoToSamples(NoteValue::Quarter, NoteModifier::None);
        size_t withoutModifier = ctx.tempoToSamples(NoteValue::Quarter);

        REQUIRE(withNone == withoutModifier);
    }

    SECTION("Triplet relationships hold") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 120.0;

        size_t triplet = ctx.tempoToSamples(NoteValue::Quarter, NoteModifier::Triplet);
        size_t quarter = ctx.tempoToSamples(NoteValue::Quarter);

        // 3 triplet quarters should equal 2 regular quarters
        // Due to integer truncation, we check approximate relationship
        double ratio = static_cast<double>(triplet) / static_cast<double>(quarter);
        REQUIRE(ratio == Approx(2.0 / 3.0).margin(0.001));
    }
}

// =============================================================================
// Edge Case Tests (T018)
// =============================================================================

TEST_CASE("tempoToSamples edge cases", "[block_context][US1]") {
    BlockContext ctx;

    SECTION("Zero sample rate returns 0 samples") {
        ctx.sampleRate = 0.0;
        ctx.tempoBPM = 120.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 0);
    }

    SECTION("Negative sample rate returns 0 samples") {
        ctx.sampleRate = -44100.0;
        ctx.tempoBPM = 120.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 0);
    }

    SECTION("Zero tempo is clamped to minimum (20 BPM)") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 0.0;

        // At 20 BPM minimum: 1 beat = 3 seconds = 132300 samples
        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 132300);
    }

    SECTION("Negative tempo is clamped to minimum (20 BPM)") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = -60.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 132300);  // Same as 20 BPM
    }

    SECTION("Tempo above 300 BPM is clamped to maximum") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 500.0;

        // At 300 BPM max: 1 beat = 0.2 seconds = 8820 samples
        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 8820);
    }

    SECTION("Extreme tempo just within bounds (20 BPM)") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 20.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 132300);  // 3 seconds * 44100
    }

    SECTION("Extreme tempo just within bounds (300 BPM)") {
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 300.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 8820);  // 0.2 seconds * 44100
    }

    SECTION("High sample rate (192000 Hz)") {
        ctx.sampleRate = 192000.0;
        ctx.tempoBPM = 120.0;

        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 96000);  // 0.5 seconds * 192000
    }
}

// =============================================================================
// samplesPerBeat() and samplesPerBar() Tests (T019)
// =============================================================================

TEST_CASE("samplesPerBeat helper", "[block_context][US1]") {
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    SECTION("Equals tempoToSamples for quarter note") {
        REQUIRE(ctx.samplesPerBeat() == ctx.tempoToSamples(NoteValue::Quarter));
    }

    SECTION("Correct at various tempos") {
        ctx.tempoBPM = 60.0;
        REQUIRE(ctx.samplesPerBeat() == 44100);  // 1 second

        ctx.tempoBPM = 120.0;
        REQUIRE(ctx.samplesPerBeat() == 22050);  // 0.5 seconds

        ctx.tempoBPM = 180.0;
        REQUIRE(ctx.samplesPerBeat() == 14700);  // 1/3 second
    }
}

TEST_CASE("samplesPerBar helper", "[block_context][US1]") {
    BlockContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.tempoBPM = 120.0;

    SECTION("4/4 time signature = 4 beats per bar") {
        ctx.timeSignatureNumerator = 4;
        ctx.timeSignatureDenominator = 4;

        size_t beatSamples = ctx.samplesPerBeat();
        REQUIRE(ctx.samplesPerBar() == beatSamples * 4);
    }

    SECTION("3/4 time signature = 3 beats per bar") {
        ctx.timeSignatureNumerator = 3;
        ctx.timeSignatureDenominator = 4;

        size_t beatSamples = ctx.samplesPerBeat();
        REQUIRE(ctx.samplesPerBar() == beatSamples * 3);
    }

    SECTION("6/8 time signature") {
        ctx.timeSignatureNumerator = 6;
        ctx.timeSignatureDenominator = 8;

        // 6/8 = 6 eighth notes per bar
        // An eighth note is half a quarter note (beat)
        // So bar = 6 * 0.5 * beat = 3 beats worth
        size_t beatSamples = ctx.samplesPerBeat();
        REQUIRE(ctx.samplesPerBar() == beatSamples * 3);
    }

    SECTION("2/4 time signature = 2 beats per bar") {
        ctx.timeSignatureNumerator = 2;
        ctx.timeSignatureDenominator = 4;

        size_t beatSamples = ctx.samplesPerBeat();
        REQUIRE(ctx.samplesPerBar() == beatSamples * 2);
    }
}

// =============================================================================
// Constexpr Tests (T020 - US4)
// =============================================================================

TEST_CASE("BlockContext tempoToSamples is constexpr", "[block_context][constexpr][US4]") {
    SECTION("Can be used in constexpr context") {
        constexpr BlockContext ctx{};  // Uses default values
        constexpr size_t samples = ctx.tempoToSamples(NoteValue::Quarter);
        REQUIRE(samples == 22050);  // 120 BPM, 44100 Hz
    }

    SECTION("Constexpr with initialized values") {
        // Note: Aggregate initialization
        constexpr BlockContext ctx{48000.0, 512, 90.0, 4, 4, false, 0};
        constexpr size_t samples = ctx.tempoToSamples(NoteValue::Eighth, NoteModifier::Dotted);
        REQUIRE(samples == 24000);
    }

    SECTION("samplesPerBeat is constexpr") {
        constexpr BlockContext ctx{};
        constexpr size_t beatSamples = ctx.samplesPerBeat();
        REQUIRE(beatSamples == 22050);
    }

    SECTION("samplesPerBar is constexpr") {
        constexpr BlockContext ctx{};
        constexpr size_t barSamples = ctx.samplesPerBar();
        REQUIRE(barSamples == 88200);  // 4 beats at 4/4
    }
}

// =============================================================================
// Real-Time Safety Tests (noexcept)
// =============================================================================

TEST_CASE("BlockContext methods are noexcept", "[block_context][realtime][US1]") {
    BlockContext ctx;

    SECTION("tempoToSamples is noexcept") {
        REQUIRE(noexcept(ctx.tempoToSamples(NoteValue::Quarter)));
        REQUIRE(noexcept(ctx.tempoToSamples(NoteValue::Eighth, NoteModifier::Dotted)));
    }

    SECTION("samplesPerBeat is noexcept") {
        REQUIRE(noexcept(ctx.samplesPerBeat()));
    }

    SECTION("samplesPerBar is noexcept") {
        REQUIRE(noexcept(ctx.samplesPerBar()));
    }
}

// =============================================================================
// Practical Use Case Tests (From spec.md acceptance scenarios)
// =============================================================================

TEST_CASE("Practical tempo sync scenarios from spec", "[block_context][US1]") {
    BlockContext ctx;

    SECTION("Spec US1 Scenario 1: 120 BPM, 44100 Hz, quarter note") {
        // Given a BlockContext with 120 BPM tempo and 44100 Hz sample rate
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 120.0;

        // When I calculate delay samples for a quarter note
        size_t samples = ctx.tempoToSamples(NoteValue::Quarter);

        // Then I get 22050 samples (0.5 seconds)
        REQUIRE(samples == 22050);
    }

    SECTION("Spec US1 Scenario 2: 90 BPM, 48000 Hz, dotted eighth") {
        // Given a BlockContext with 90 BPM and 48000 Hz sample rate
        ctx.sampleRate = 48000.0;
        ctx.tempoBPM = 90.0;

        // When I calculate delay samples for a dotted eighth note
        size_t samples = ctx.tempoToSamples(NoteValue::Eighth, NoteModifier::Dotted);

        // Then I get 24000 samples
        // dotted eighth = 0.75 beats * (60/90 sec/beat) * 48000 Hz = 24000
        REQUIRE(samples == 24000);
    }

    SECTION("Spec US1 Scenario 3: Transport playing state query") {
        // Given a BlockContext with transport playing
        ctx.isPlaying = true;

        // When I query the transport state
        // Then I can determine if playback is active for LFO sync decisions
        REQUIRE(ctx.isPlaying == true);

        ctx.isPlaying = false;
        REQUIRE(ctx.isPlaying == false);
    }
}

// =============================================================================
// SC-004: Accuracy Test (Within 1 sample for tempos 20-300 BPM)
// =============================================================================

TEST_CASE("tempoToSamples accuracy (SC-004)", "[block_context][US1]") {
    BlockContext ctx;

    SECTION("Accuracy across tempo range at 44100 Hz") {
        ctx.sampleRate = 44100.0;

        // Test various tempos within the valid range
        for (double tempo : {20.0, 60.0, 90.0, 120.0, 150.0, 180.0, 240.0, 300.0}) {
            ctx.tempoBPM = tempo;

            // Calculate expected samples using the same formula
            double secondsPerBeat = 60.0 / tempo;
            double expectedSamples = secondsPerBeat * ctx.sampleRate;

            size_t actualSamples = ctx.tempoToSamples(NoteValue::Quarter);

            // Should be accurate to within 1 sample (due to integer truncation)
            REQUIRE(std::abs(static_cast<double>(actualSamples) - expectedSamples) < 1.0);
        }
    }

    SECTION("Accuracy across sample rate range") {
        ctx.tempoBPM = 120.0;

        for (double sampleRate : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
            ctx.sampleRate = sampleRate;

            double secondsPerBeat = 60.0 / 120.0;  // 0.5 seconds at 120 BPM
            double expectedSamples = secondsPerBeat * sampleRate;

            size_t actualSamples = ctx.tempoToSamples(NoteValue::Quarter);

            REQUIRE(std::abs(static_cast<double>(actualSamples) - expectedSamples) < 1.0);
        }
    }
}
