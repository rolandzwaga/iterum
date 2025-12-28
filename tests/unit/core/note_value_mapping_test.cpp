// ==============================================================================
// NoteValue Dropdown Mapping Unit Tests
// ==============================================================================
// Tests for the dropdown index to NoteValue+NoteModifier mapping.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/core/note_value.h"

using namespace Iterum::DSP;

TEST_CASE("NoteValue dropdown mapping produces correct values", "[dsp][core]") {
    SECTION("Index 0 maps to 1/32 note") {
        auto mapping = getNoteValueFromDropdown(0);
        REQUIRE(mapping.note == NoteValue::ThirtySecond);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 1 maps to 1/16 triplet") {
        auto mapping = getNoteValueFromDropdown(1);
        REQUIRE(mapping.note == NoteValue::Sixteenth);
        REQUIRE(mapping.modifier == NoteModifier::Triplet);
    }

    SECTION("Index 2 maps to 1/16 note") {
        auto mapping = getNoteValueFromDropdown(2);
        REQUIRE(mapping.note == NoteValue::Sixteenth);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 3 maps to 1/8 triplet") {
        auto mapping = getNoteValueFromDropdown(3);
        REQUIRE(mapping.note == NoteValue::Eighth);
        REQUIRE(mapping.modifier == NoteModifier::Triplet);
    }

    SECTION("Index 4 maps to 1/8 note") {
        auto mapping = getNoteValueFromDropdown(4);
        REQUIRE(mapping.note == NoteValue::Eighth);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 5 maps to 1/4 triplet") {
        auto mapping = getNoteValueFromDropdown(5);
        REQUIRE(mapping.note == NoteValue::Quarter);
        REQUIRE(mapping.modifier == NoteModifier::Triplet);
    }

    SECTION("Index 6 maps to 1/4 note") {
        auto mapping = getNoteValueFromDropdown(6);
        REQUIRE(mapping.note == NoteValue::Quarter);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 7 maps to 1/2 triplet") {
        auto mapping = getNoteValueFromDropdown(7);
        REQUIRE(mapping.note == NoteValue::Half);
        REQUIRE(mapping.modifier == NoteModifier::Triplet);
    }

    SECTION("Index 8 maps to 1/2 note") {
        auto mapping = getNoteValueFromDropdown(8);
        REQUIRE(mapping.note == NoteValue::Half);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 9 maps to whole note") {
        auto mapping = getNoteValueFromDropdown(9);
        REQUIRE(mapping.note == NoteValue::Whole);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }
}

TEST_CASE("NoteValue dropdown mapping handles out of range", "[dsp][core]") {
    SECTION("Negative index defaults to 1/8") {
        auto mapping = getNoteValueFromDropdown(-1);
        REQUIRE(mapping.note == NoteValue::Eighth);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index > 9 defaults to 1/8") {
        auto mapping = getNoteValueFromDropdown(10);
        REQUIRE(mapping.note == NoteValue::Eighth);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }
}

TEST_CASE("NoteValue dropdown produces correct beat durations", "[dsp][core]") {
    // Verify that the mapping produces correct beat durations at 120 BPM
    // At 120 BPM, a quarter note = 0.5 seconds = 22050 samples at 44100Hz

    SECTION("1/32 note is 0.125 beats") {
        auto mapping = getNoteValueFromDropdown(0);  // 1/32
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == 0.125f);
    }

    SECTION("1/8 triplet is 0.5 * 2/3 = 0.333 beats") {
        auto mapping = getNoteValueFromDropdown(3);  // 1/8T
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == Catch::Approx(0.333333f).margin(0.0001f));
    }

    SECTION("1/4 note is 1 beat") {
        auto mapping = getNoteValueFromDropdown(6);  // 1/4
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == 1.0f);
    }

    SECTION("1/4 triplet is 2/3 beat") {
        auto mapping = getNoteValueFromDropdown(5);  // 1/4T
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == Catch::Approx(0.666666f).margin(0.0001f));
    }

    SECTION("Whole note is 4 beats") {
        auto mapping = getNoteValueFromDropdown(9);  // 1/1
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == 4.0f);
    }
}

// =============================================================================
// Tempo Sync Utility Tests
// =============================================================================

TEST_CASE("noteToDelayMs calculates correct delay times at 120 BPM", "[dsp][core][tempo]") {
    // At 120 BPM, 1 beat = 500ms
    constexpr double bpm = 120.0;

    SECTION("Quarter note = 500ms (1 beat)") {
        float delayMs = noteToDelayMs(NoteValue::Quarter, NoteModifier::None, bpm);
        REQUIRE(delayMs == Catch::Approx(500.0f).margin(0.01f));
    }

    SECTION("Eighth note = 250ms (0.5 beats)") {
        float delayMs = noteToDelayMs(NoteValue::Eighth, NoteModifier::None, bpm);
        REQUIRE(delayMs == Catch::Approx(250.0f).margin(0.01f));
    }

    SECTION("Sixteenth note = 125ms (0.25 beats)") {
        float delayMs = noteToDelayMs(NoteValue::Sixteenth, NoteModifier::None, bpm);
        REQUIRE(delayMs == Catch::Approx(125.0f).margin(0.01f));
    }

    SECTION("ThirtySecond note = 62.5ms (0.125 beats)") {
        float delayMs = noteToDelayMs(NoteValue::ThirtySecond, NoteModifier::None, bpm);
        REQUIRE(delayMs == Catch::Approx(62.5f).margin(0.01f));
    }

    SECTION("Half note = 1000ms (2 beats)") {
        float delayMs = noteToDelayMs(NoteValue::Half, NoteModifier::None, bpm);
        REQUIRE(delayMs == Catch::Approx(1000.0f).margin(0.01f));
    }

    SECTION("Whole note = 2000ms (4 beats)") {
        float delayMs = noteToDelayMs(NoteValue::Whole, NoteModifier::None, bpm);
        REQUIRE(delayMs == Catch::Approx(2000.0f).margin(0.01f));
    }
}

TEST_CASE("noteToDelayMs handles dotted notes correctly", "[dsp][core][tempo]") {
    constexpr double bpm = 120.0;

    SECTION("Dotted quarter = 750ms (1.5 beats)") {
        float delayMs = noteToDelayMs(NoteValue::Quarter, NoteModifier::Dotted, bpm);
        REQUIRE(delayMs == Catch::Approx(750.0f).margin(0.01f));
    }

    SECTION("Dotted eighth = 375ms (0.75 beats)") {
        float delayMs = noteToDelayMs(NoteValue::Eighth, NoteModifier::Dotted, bpm);
        REQUIRE(delayMs == Catch::Approx(375.0f).margin(0.01f));
    }
}

TEST_CASE("noteToDelayMs handles triplet notes correctly", "[dsp][core][tempo]") {
    constexpr double bpm = 120.0;

    SECTION("Quarter triplet = 333.33ms (2/3 beat)") {
        float delayMs = noteToDelayMs(NoteValue::Quarter, NoteModifier::Triplet, bpm);
        REQUIRE(delayMs == Catch::Approx(333.333f).margin(0.1f));
    }

    SECTION("Eighth triplet = 166.67ms (1/3 beat)") {
        float delayMs = noteToDelayMs(NoteValue::Eighth, NoteModifier::Triplet, bpm);
        REQUIRE(delayMs == Catch::Approx(166.667f).margin(0.1f));
    }
}

TEST_CASE("noteToDelayMs calculates correctly at 100 BPM", "[dsp][core][tempo]") {
    // At 100 BPM, 1 beat = 600ms
    constexpr double bpm = 100.0;

    SECTION("Quarter note = 600ms") {
        float delayMs = noteToDelayMs(NoteValue::Quarter, NoteModifier::None, bpm);
        REQUIRE(delayMs == Catch::Approx(600.0f).margin(0.01f));
    }

    SECTION("1/32 note = 75ms (at 100 BPM)") {
        // 0.125 beats * 600ms/beat = 75ms
        float delayMs = noteToDelayMs(NoteValue::ThirtySecond, NoteModifier::None, bpm);
        REQUIRE(delayMs == Catch::Approx(75.0f).margin(0.01f));
    }

    SECTION("1/8 triplet = 200ms (at 100 BPM)") {
        // 0.5 * 2/3 beats * 600ms/beat = 200ms
        float delayMs = noteToDelayMs(NoteValue::Eighth, NoteModifier::Triplet, bpm);
        REQUIRE(delayMs == Catch::Approx(200.0f).margin(0.1f));
    }
}

TEST_CASE("noteToDelayMs clamps tempo to safe range", "[dsp][core][tempo]") {
    SECTION("Tempo below 20 BPM is clamped to 20 BPM") {
        // At 20 BPM, quarter = 3000ms
        float delayMs = noteToDelayMs(NoteValue::Quarter, NoteModifier::None, 5.0);
        REQUIRE(delayMs == Catch::Approx(3000.0f).margin(0.1f));
    }

    SECTION("Tempo above 300 BPM is clamped to 300 BPM") {
        // At 300 BPM, quarter = 200ms
        float delayMs = noteToDelayMs(NoteValue::Quarter, NoteModifier::None, 500.0);
        REQUIRE(delayMs == Catch::Approx(200.0f).margin(0.1f));
    }

    SECTION("Zero tempo is clamped to minimum") {
        float delayMs = noteToDelayMs(NoteValue::Quarter, NoteModifier::None, 0.0);
        REQUIRE(delayMs == Catch::Approx(3000.0f).margin(0.1f));
    }

    SECTION("Negative tempo is clamped to minimum") {
        float delayMs = noteToDelayMs(NoteValue::Quarter, NoteModifier::None, -100.0);
        REQUIRE(delayMs == Catch::Approx(3000.0f).margin(0.1f));
    }
}

TEST_CASE("dropdownToDelayMs convenience function works correctly", "[dsp][core][tempo]") {
    constexpr double bpm = 100.0;

    SECTION("Index 0 (1/32) at 100 BPM = 75ms") {
        float delayMs = dropdownToDelayMs(0, bpm);
        REQUIRE(delayMs == Catch::Approx(75.0f).margin(0.1f));
    }

    SECTION("Index 3 (1/8T) at 100 BPM = 200ms") {
        float delayMs = dropdownToDelayMs(3, bpm);
        REQUIRE(delayMs == Catch::Approx(200.0f).margin(0.1f));
    }

    SECTION("Index 6 (1/4) at 100 BPM = 600ms") {
        float delayMs = dropdownToDelayMs(6, bpm);
        REQUIRE(delayMs == Catch::Approx(600.0f).margin(0.1f));
    }

    SECTION("Index 9 (whole) at 100 BPM = 2400ms") {
        float delayMs = dropdownToDelayMs(9, bpm);
        REQUIRE(delayMs == Catch::Approx(2400.0f).margin(0.1f));
    }

    SECTION("Out of range index defaults to 1/8 = 300ms at 100 BPM") {
        float delayMs = dropdownToDelayMs(99, bpm);
        REQUIRE(delayMs == Catch::Approx(300.0f).margin(0.1f));
    }
}
