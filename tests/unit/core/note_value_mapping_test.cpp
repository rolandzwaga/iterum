// ==============================================================================
// NoteValue Dropdown Mapping Unit Tests
// ==============================================================================
// Tests for the dropdown index to NoteValue+NoteModifier mapping.
// Updated for 21-value dropdown (1/64T through 1/1D, grouped by note value).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/core/note_value.h"

using namespace Iterum::DSP;

TEST_CASE("NoteValue dropdown mapping produces correct values", "[dsp][core]") {
    // New dropdown order (21 entries, grouped by note value: T, normal, D):
    // 0: 1/64T, 1: 1/64, 2: 1/64D,
    // 3: 1/32T, 4: 1/32, 5: 1/32D,
    // 6: 1/16T, 7: 1/16, 8: 1/16D,
    // 9: 1/8T, 10: 1/8 (DEFAULT), 11: 1/8D,
    // 12: 1/4T, 13: 1/4, 14: 1/4D,
    // 15: 1/2T, 16: 1/2, 17: 1/2D,
    // 18: 1/1T, 19: 1/1, 20: 1/1D

    SECTION("Index 0 maps to 1/64 triplet") {
        auto mapping = getNoteValueFromDropdown(0);
        REQUIRE(mapping.note == NoteValue::SixtyFourth);
        REQUIRE(mapping.modifier == NoteModifier::Triplet);
    }

    SECTION("Index 1 maps to 1/64 note") {
        auto mapping = getNoteValueFromDropdown(1);
        REQUIRE(mapping.note == NoteValue::SixtyFourth);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 2 maps to 1/64 dotted") {
        auto mapping = getNoteValueFromDropdown(2);
        REQUIRE(mapping.note == NoteValue::SixtyFourth);
        REQUIRE(mapping.modifier == NoteModifier::Dotted);
    }

    SECTION("Index 3 maps to 1/32 triplet") {
        auto mapping = getNoteValueFromDropdown(3);
        REQUIRE(mapping.note == NoteValue::ThirtySecond);
        REQUIRE(mapping.modifier == NoteModifier::Triplet);
    }

    SECTION("Index 4 maps to 1/32 note") {
        auto mapping = getNoteValueFromDropdown(4);
        REQUIRE(mapping.note == NoteValue::ThirtySecond);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 7 maps to 1/16 note") {
        auto mapping = getNoteValueFromDropdown(7);
        REQUIRE(mapping.note == NoteValue::Sixteenth);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 10 maps to 1/8 note (default)") {
        auto mapping = getNoteValueFromDropdown(10);
        REQUIRE(mapping.note == NoteValue::Eighth);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 13 maps to 1/4 note") {
        auto mapping = getNoteValueFromDropdown(13);
        REQUIRE(mapping.note == NoteValue::Quarter);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 16 maps to 1/2 note") {
        auto mapping = getNoteValueFromDropdown(16);
        REQUIRE(mapping.note == NoteValue::Half);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 18 maps to whole note triplet") {
        auto mapping = getNoteValueFromDropdown(18);
        REQUIRE(mapping.note == NoteValue::Whole);
        REQUIRE(mapping.modifier == NoteModifier::Triplet);
    }

    SECTION("Index 19 maps to whole note") {
        auto mapping = getNoteValueFromDropdown(19);
        REQUIRE(mapping.note == NoteValue::Whole);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index 20 maps to dotted whole note") {
        auto mapping = getNoteValueFromDropdown(20);
        REQUIRE(mapping.note == NoteValue::Whole);
        REQUIRE(mapping.modifier == NoteModifier::Dotted);
    }
}

TEST_CASE("NoteValue dropdown mapping handles out of range", "[dsp][core]") {
    SECTION("Negative index defaults to 1/8") {
        auto mapping = getNoteValueFromDropdown(-1);
        REQUIRE(mapping.note == NoteValue::Eighth);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }

    SECTION("Index >= 21 defaults to 1/8") {
        auto mapping = getNoteValueFromDropdown(21);
        REQUIRE(mapping.note == NoteValue::Eighth);
        REQUIRE(mapping.modifier == NoteModifier::None);
    }
}

TEST_CASE("NoteValue dropdown produces correct beat durations", "[dsp][core]") {
    SECTION("1/64 triplet is 0.0625 * 2/3 beats") {
        auto mapping = getNoteValueFromDropdown(0);  // 1/64T
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == Catch::Approx(0.041667f).margin(0.0001f));
    }

    SECTION("1/64 note is 0.0625 beats") {
        auto mapping = getNoteValueFromDropdown(1);  // 1/64
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == 0.0625f);
    }

    SECTION("1/32 note is 0.125 beats") {
        auto mapping = getNoteValueFromDropdown(4);  // 1/32
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == 0.125f);
    }

    SECTION("1/8 triplet is 0.5 * 2/3 = 0.333 beats") {
        auto mapping = getNoteValueFromDropdown(9);  // 1/8T (new index)
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == Catch::Approx(0.333333f).margin(0.0001f));
    }

    SECTION("1/8 note is 0.5 beats") {
        auto mapping = getNoteValueFromDropdown(10);  // 1/8 (default)
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == 0.5f);
    }

    SECTION("1/4 note is 1 beat") {
        auto mapping = getNoteValueFromDropdown(13);  // 1/4
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == 1.0f);
    }

    SECTION("1/4 triplet is 2/3 beat") {
        auto mapping = getNoteValueFromDropdown(12);  // 1/4T (new index)
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == Catch::Approx(0.666666f).margin(0.0001f));
    }

    SECTION("Dotted quarter is 1.5 beats") {
        auto mapping = getNoteValueFromDropdown(14);  // 1/4D (new index)
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == 1.5f);
    }

    SECTION("Whole note triplet is 2.667 beats") {
        auto mapping = getNoteValueFromDropdown(18);  // 1/1T
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == Catch::Approx(2.666666f).margin(0.0001f));
    }

    SECTION("Whole note is 4 beats") {
        auto mapping = getNoteValueFromDropdown(19);  // 1/1 (new index)
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == 4.0f);
    }

    SECTION("Dotted whole note is 6 beats") {
        auto mapping = getNoteValueFromDropdown(20);  // 1/1D (new index)
        float beats = getBeatsForNote(mapping.note, mapping.modifier);
        REQUIRE(beats == 6.0f);
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

    SECTION("SixtyFourth note = 31.25ms (0.0625 beats)") {
        float delayMs = noteToDelayMs(NoteValue::SixtyFourth, NoteModifier::None, bpm);
        REQUIRE(delayMs == Catch::Approx(31.25f).margin(0.01f));
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

    SECTION("Dotted whole = 3000ms (6 beats)") {
        float delayMs = noteToDelayMs(NoteValue::Whole, NoteModifier::Dotted, bpm);
        REQUIRE(delayMs == Catch::Approx(3000.0f).margin(0.01f));
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
    // At 100 BPM, 1 beat = 600ms

    SECTION("Index 1 (1/64) at 100 BPM = 37.5ms") {
        // 0.0625 beats * 600ms/beat = 37.5ms
        float delayMs = dropdownToDelayMs(1, bpm);
        REQUIRE(delayMs == Catch::Approx(37.5f).margin(0.1f));
    }

    SECTION("Index 4 (1/32) at 100 BPM = 75ms") {
        // 0.125 beats * 600ms/beat = 75ms
        float delayMs = dropdownToDelayMs(4, bpm);
        REQUIRE(delayMs == Catch::Approx(75.0f).margin(0.1f));
    }

    SECTION("Index 9 (1/8T) at 100 BPM = 200ms") {
        // 0.333 beats * 600ms/beat = 200ms
        float delayMs = dropdownToDelayMs(9, bpm);  // new index
        REQUIRE(delayMs == Catch::Approx(200.0f).margin(0.1f));
    }

    SECTION("Index 10 (1/8) at 100 BPM = 300ms") {
        // 0.5 beats * 600ms/beat = 300ms
        float delayMs = dropdownToDelayMs(10, bpm);
        REQUIRE(delayMs == Catch::Approx(300.0f).margin(0.1f));
    }

    SECTION("Index 13 (1/4) at 100 BPM = 600ms") {
        float delayMs = dropdownToDelayMs(13, bpm);
        REQUIRE(delayMs == Catch::Approx(600.0f).margin(0.1f));
    }

    SECTION("Index 18 (whole triplet) at 100 BPM = 1600ms") {
        // 2.667 beats * 600ms/beat = 1600ms
        float delayMs = dropdownToDelayMs(18, bpm);
        REQUIRE(delayMs == Catch::Approx(1600.0f).margin(0.1f));
    }

    SECTION("Index 19 (whole) at 100 BPM = 2400ms") {
        // 4 beats * 600ms/beat = 2400ms
        float delayMs = dropdownToDelayMs(19, bpm);
        REQUIRE(delayMs == Catch::Approx(2400.0f).margin(0.1f));
    }

    SECTION("Index 20 (dotted whole) at 100 BPM = 3600ms") {
        // 6 beats * 600ms/beat = 3600ms
        float delayMs = dropdownToDelayMs(20, bpm);
        REQUIRE(delayMs == Catch::Approx(3600.0f).margin(0.1f));
    }

    SECTION("Out of range index defaults to 1/8 = 300ms at 100 BPM") {
        float delayMs = dropdownToDelayMs(99, bpm);
        REQUIRE(delayMs == Catch::Approx(300.0f).margin(0.1f));
    }
}
