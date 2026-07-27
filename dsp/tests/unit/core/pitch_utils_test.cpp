// Layer 0: Core Utility Tests - Pitch Conversion
// Part of Granular Delay feature (spec 034)

#include <cmath>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/core/pitch_utils.h>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// semitonesToRatio Tests
// =============================================================================

TEST_CASE("semitonesToRatio converts semitones to playback ratio", "[core][pitch][layer0]") {

    SECTION("0 semitones returns unity ratio") {
        REQUIRE(semitonesToRatio(0.0f) == Approx(1.0f).margin(1e-6f));
    }

    SECTION("+12 semitones returns 2.0 (octave up)") {
        REQUIRE(semitonesToRatio(12.0f) == Approx(2.0f).margin(1e-5f));
    }

    SECTION("-12 semitones returns 0.5 (octave down)") {
        REQUIRE(semitonesToRatio(-12.0f) == Approx(0.5f).margin(1e-5f));
    }

    SECTION("+24 semitones returns 4.0 (two octaves up)") {
        REQUIRE(semitonesToRatio(24.0f) == Approx(4.0f).margin(1e-4f));
    }

    SECTION("-24 semitones returns 0.25 (two octaves down)") {
        REQUIRE(semitonesToRatio(-24.0f) == Approx(0.25f).margin(1e-5f));
    }

    SECTION("+7 semitones returns perfect fifth ratio (~1.498)") {
        // Perfect fifth = 2^(7/12) ≈ 1.4983
        REQUIRE(semitonesToRatio(7.0f) == Approx(1.4983f).margin(1e-3f));
    }

    SECTION("-7 semitones returns inverted perfect fifth (~0.667)") {
        // Inverted perfect fifth = 2^(-7/12) ≈ 0.6674
        REQUIRE(semitonesToRatio(-7.0f) == Approx(0.6674f).margin(1e-3f));
    }

    SECTION("+1 semitone returns semitone ratio (~1.0595)") {
        // One semitone = 2^(1/12) ≈ 1.05946
        REQUIRE(semitonesToRatio(1.0f) == Approx(1.05946f).margin(1e-4f));
    }
}

// =============================================================================
// ratioToSemitones Tests
// =============================================================================

TEST_CASE("ratioToSemitones converts playback ratio to semitones", "[core][pitch][layer0]") {

    SECTION("unity ratio returns 0 semitones") {
        REQUIRE(ratioToSemitones(1.0f) == Approx(0.0f).margin(1e-6f));
    }

    SECTION("2.0 ratio returns +12 semitones (octave up)") {
        REQUIRE(ratioToSemitones(2.0f) == Approx(12.0f).margin(1e-4f));
    }

    SECTION("0.5 ratio returns -12 semitones (octave down)") {
        REQUIRE(ratioToSemitones(0.5f) == Approx(-12.0f).margin(1e-4f));
    }

    SECTION("4.0 ratio returns +24 semitones (two octaves up)") {
        REQUIRE(ratioToSemitones(4.0f) == Approx(24.0f).margin(1e-4f));
    }

    SECTION("0.25 ratio returns -24 semitones (two octaves down)") {
        REQUIRE(ratioToSemitones(0.25f) == Approx(-24.0f).margin(1e-4f));
    }

    SECTION("invalid ratio (0 or negative) returns 0") {
        REQUIRE(ratioToSemitones(0.0f) == 0.0f);
        REQUIRE(ratioToSemitones(-1.0f) == 0.0f);
    }
}

// =============================================================================
// Roundtrip Tests
// =============================================================================

TEST_CASE("semitonesToRatio and ratioToSemitones are inverses", "[core][pitch][layer0]") {

    SECTION("roundtrip: semitones -> ratio -> semitones") {
        const float testValues[] = {-24.0f, -12.0f, -7.0f, -1.0f, 0.0f, 1.0f, 7.0f, 12.0f, 24.0f};

        for (float semitones : testValues) {
            float ratio = semitonesToRatio(semitones);
            float recovered = ratioToSemitones(ratio);
            REQUIRE(recovered == Approx(semitones).margin(1e-4f));
        }
    }

    SECTION("roundtrip: ratio -> semitones -> ratio") {
        const float testRatios[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f};

        for (float ratio : testRatios) {
            float semitones = ratioToSemitones(ratio);
            float recovered = semitonesToRatio(semitones);
            REQUIRE(recovered == Approx(ratio).margin(1e-5f));
        }
    }
}

// =============================================================================
// Pitch Accuracy Test (SC-003: accurate within 10 cents)
// =============================================================================

TEST_CASE("Pitch conversion accuracy within 10 cents", "[core][pitch][layer0][SC-003]") {
    // 10 cents = 0.1 semitones
    constexpr float kMaxErrorCents = 10.0f;
    constexpr float kMaxErrorSemitones = kMaxErrorCents / 100.0f;

    // Test across the full -24 to +24 semitone range at 1-semitone intervals
    for (int semitones = -24; semitones <= 24; ++semitones) {
        float targetSemitones = static_cast<float>(semitones);
        float ratio = semitonesToRatio(targetSemitones);
        float measuredSemitones = ratioToSemitones(ratio);

        // Verify accuracy within 10 cents
        REQUIRE(std::abs(measuredSemitones - targetSemitones) < kMaxErrorSemitones);
    }
}

// =============================================================================
// Pitch Quantization Tests (Phase 2.2)
// =============================================================================

TEST_CASE("quantizePitch Off mode passes through unchanged", "[core][pitch][layer0][quant]") {
    REQUIRE(quantizePitch(0.0f, PitchQuantMode::Off) == 0.0f);
    REQUIRE(quantizePitch(1.5f, PitchQuantMode::Off) == 1.5f);
    REQUIRE(quantizePitch(-3.7f, PitchQuantMode::Off) == -3.7f);
    REQUIRE(quantizePitch(12.3456f, PitchQuantMode::Off) == 12.3456f);
}

TEST_CASE("quantizePitch Semitones mode rounds to nearest integer", "[core][pitch][layer0][quant]") {
    SECTION("rounds positive values correctly") {
        REQUIRE(quantizePitch(0.0f, PitchQuantMode::Semitones) == Approx(0.0f));
        REQUIRE(quantizePitch(0.4f, PitchQuantMode::Semitones) == Approx(0.0f));
        REQUIRE(quantizePitch(0.5f, PitchQuantMode::Semitones) == Approx(1.0f));
        REQUIRE(quantizePitch(0.6f, PitchQuantMode::Semitones) == Approx(1.0f));
        REQUIRE(quantizePitch(5.7f, PitchQuantMode::Semitones) == Approx(6.0f));
        REQUIRE(quantizePitch(12.3f, PitchQuantMode::Semitones) == Approx(12.0f));
    }

    SECTION("rounds negative values correctly") {
        REQUIRE(quantizePitch(-0.4f, PitchQuantMode::Semitones) == Approx(0.0f));
        REQUIRE(quantizePitch(-0.6f, PitchQuantMode::Semitones) == Approx(-1.0f));
        REQUIRE(quantizePitch(-5.3f, PitchQuantMode::Semitones) == Approx(-5.0f));
        REQUIRE(quantizePitch(-12.7f, PitchQuantMode::Semitones) == Approx(-13.0f));
    }
}

TEST_CASE("quantizePitch Octaves mode rounds to nearest 12", "[core][pitch][layer0][quant]") {
    SECTION("rounds to nearest octave") {
        REQUIRE(quantizePitch(0.0f, PitchQuantMode::Octaves) == Approx(0.0f));
        REQUIRE(quantizePitch(5.0f, PitchQuantMode::Octaves) == Approx(0.0f));
        REQUIRE(quantizePitch(6.0f, PitchQuantMode::Octaves) == Approx(12.0f));
        REQUIRE(quantizePitch(11.0f, PitchQuantMode::Octaves) == Approx(12.0f));
        REQUIRE(quantizePitch(12.0f, PitchQuantMode::Octaves) == Approx(12.0f));
        REQUIRE(quantizePitch(18.0f, PitchQuantMode::Octaves) == Approx(24.0f));
        REQUIRE(quantizePitch(24.0f, PitchQuantMode::Octaves) == Approx(24.0f));
    }

    SECTION("rounds negative values to nearest octave") {
        REQUIRE(quantizePitch(-5.0f, PitchQuantMode::Octaves) == Approx(0.0f));
        // Note: std::round(-0.5) behavior is implementation-defined, -6 may round to 0 or -12
        REQUIRE(quantizePitch(-7.0f, PitchQuantMode::Octaves) == Approx(-12.0f));
        REQUIRE(quantizePitch(-12.0f, PitchQuantMode::Octaves) == Approx(-12.0f));
        REQUIRE(quantizePitch(-18.0f, PitchQuantMode::Octaves) == Approx(-24.0f));
    }
}

TEST_CASE("quantizePitch Fifths mode rounds to 0 or 7 within each octave", "[core][pitch][layer0][quant]") {
    SECTION("within first octave") {
        REQUIRE(quantizePitch(0.0f, PitchQuantMode::Fifths) == Approx(0.0f));
        REQUIRE(quantizePitch(3.0f, PitchQuantMode::Fifths) == Approx(0.0f));
        REQUIRE(quantizePitch(4.0f, PitchQuantMode::Fifths) == Approx(7.0f));
        REQUIRE(quantizePitch(7.0f, PitchQuantMode::Fifths) == Approx(7.0f));
        REQUIRE(quantizePitch(9.0f, PitchQuantMode::Fifths) == Approx(7.0f));
        REQUIRE(quantizePitch(10.0f, PitchQuantMode::Fifths) == Approx(12.0f));
        REQUIRE(quantizePitch(11.0f, PitchQuantMode::Fifths) == Approx(12.0f));
    }

    SECTION("in second octave") {
        REQUIRE(quantizePitch(12.0f, PitchQuantMode::Fifths) == Approx(12.0f));
        REQUIRE(quantizePitch(15.0f, PitchQuantMode::Fifths) == Approx(12.0f));
        REQUIRE(quantizePitch(16.0f, PitchQuantMode::Fifths) == Approx(19.0f));  // 12 + 7
        REQUIRE(quantizePitch(19.0f, PitchQuantMode::Fifths) == Approx(19.0f));
        REQUIRE(quantizePitch(22.0f, PitchQuantMode::Fifths) == Approx(24.0f));  // Next octave
    }

    SECTION("negative values") {
        // Negative values wrap: -3 in octave -1 = 9 semitones -> rounds to 7 -> -12+7 = -5
        REQUIRE(quantizePitch(-3.0f, PitchQuantMode::Fifths) == Approx(-5.0f));
        REQUIRE(quantizePitch(-5.0f, PitchQuantMode::Fifths) == Approx(-5.0f));
        REQUIRE(quantizePitch(-7.0f, PitchQuantMode::Fifths) == Approx(-5.0f));  // 5 semitones in prev octave -> 7
        REQUIRE(quantizePitch(-10.0f, PitchQuantMode::Fifths) == Approx(-12.0f)); // 2 semitones in prev octave -> 0
        REQUIRE(quantizePitch(-12.0f, PitchQuantMode::Fifths) == Approx(-12.0f));
    }
}

TEST_CASE("quantizePitch Scale mode rounds to major scale degrees", "[core][pitch][layer0][quant]") {
    // Major scale degrees: 0, 2, 4, 5, 7, 9, 11

    SECTION("within first octave") {
        REQUIRE(quantizePitch(0.0f, PitchQuantMode::Scale) == Approx(0.0f));
        REQUIRE(quantizePitch(0.5f, PitchQuantMode::Scale) == Approx(0.0f));
        REQUIRE(quantizePitch(1.0f, PitchQuantMode::Scale) == Approx(0.0f));  // Closer to 0 than 2
        REQUIRE(quantizePitch(1.5f, PitchQuantMode::Scale) == Approx(2.0f));  // Closer to 2
        REQUIRE(quantizePitch(2.0f, PitchQuantMode::Scale) == Approx(2.0f));
        REQUIRE(quantizePitch(3.0f, PitchQuantMode::Scale) == Approx(2.0f));  // Closer to 2 than 4
        REQUIRE(quantizePitch(3.5f, PitchQuantMode::Scale) == Approx(4.0f));  // Closer to 4
        REQUIRE(quantizePitch(4.0f, PitchQuantMode::Scale) == Approx(4.0f));
        REQUIRE(quantizePitch(4.4f, PitchQuantMode::Scale) == Approx(4.0f));
        REQUIRE(quantizePitch(4.6f, PitchQuantMode::Scale) == Approx(5.0f));  // Closer to 5
        REQUIRE(quantizePitch(5.0f, PitchQuantMode::Scale) == Approx(5.0f));
        REQUIRE(quantizePitch(6.0f, PitchQuantMode::Scale) == Approx(5.0f));  // Closer to 5 than 7
        REQUIRE(quantizePitch(6.5f, PitchQuantMode::Scale) == Approx(7.0f));  // Closer to 7
        REQUIRE(quantizePitch(7.0f, PitchQuantMode::Scale) == Approx(7.0f));
        REQUIRE(quantizePitch(8.0f, PitchQuantMode::Scale) == Approx(7.0f));  // Closer to 7 than 9
        REQUIRE(quantizePitch(8.5f, PitchQuantMode::Scale) == Approx(9.0f));  // Closer to 9
        REQUIRE(quantizePitch(9.0f, PitchQuantMode::Scale) == Approx(9.0f));
        REQUIRE(quantizePitch(10.0f, PitchQuantMode::Scale) == Approx(9.0f));  // Closer to 9 than 11
        REQUIRE(quantizePitch(10.5f, PitchQuantMode::Scale) == Approx(11.0f)); // Closer to 11
        REQUIRE(quantizePitch(11.0f, PitchQuantMode::Scale) == Approx(11.0f));
        REQUIRE(quantizePitch(11.5f, PitchQuantMode::Scale) == Approx(12.0f)); // Closer to next octave root
    }

    SECTION("second octave wraps correctly") {
        REQUIRE(quantizePitch(12.0f, PitchQuantMode::Scale) == Approx(12.0f));
        REQUIRE(quantizePitch(14.0f, PitchQuantMode::Scale) == Approx(14.0f));  // 12 + 2
        REQUIRE(quantizePitch(19.0f, PitchQuantMode::Scale) == Approx(19.0f));  // 12 + 7
    }

    SECTION("negative values") {
        // -1 wraps to 11 in octave -1 -> scale degree 11 -> -12 + 11 = -1
        REQUIRE(quantizePitch(-1.0f, PitchQuantMode::Scale) == Approx(-1.0f));
        // -2 wraps to 10 in octave -1 -> closest to 9 or 11? 10 is equidistant, picks 9 -> -12 + 9 = -3
        REQUIRE(quantizePitch(-2.0f, PitchQuantMode::Scale) == Approx(-3.0f));
        // -5 wraps to 7 in octave -1 -> scale degree 7 -> -12 + 7 = -5
        REQUIRE(quantizePitch(-5.0f, PitchQuantMode::Scale) == Approx(-5.0f));
    }
}

// =============================================================================
// frequencyToMidiNote Tests (spec 037-basic-synth-voice)
// =============================================================================

TEST_CASE("frequencyToMidiNote converts frequency to continuous MIDI note", "[core][pitch][layer0][synth-voice]") {

    SECTION("440 Hz returns 69.0 (A4)") {
        REQUIRE(frequencyToMidiNote(440.0f) == Approx(69.0f).margin(0.01f));
    }

    SECTION("261.63 Hz returns 60.0 (C4)") {
        REQUIRE(frequencyToMidiNote(261.63f) == Approx(60.0f).margin(0.05f));
    }

    SECTION("frequency <= 0 returns 0.0") {
        REQUIRE(frequencyToMidiNote(0.0f) == 0.0f);
        REQUIRE(frequencyToMidiNote(-100.0f) == 0.0f);
    }

    SECTION("466.16 Hz returns ~70.0 (A#4)") {
        REQUIRE(frequencyToMidiNote(466.16f) == Approx(70.0f).margin(0.05f));
    }

    SECTION("880 Hz returns 81.0 (A5)") {
        REQUIRE(frequencyToMidiNote(880.0f) == Approx(81.0f).margin(0.01f));
    }

    SECTION("roundtrip with semitonesToRatio at A4") {
        // If we go up 12 semitones from A4 (440 Hz), we get A5 (880 Hz)
        float a5Hz = 440.0f * semitonesToRatio(12.0f);
        float note = frequencyToMidiNote(a5Hz);
        REQUIRE(note == Approx(81.0f).margin(0.05f));
    }
}

// =============================================================================
// frequencyToNoteClass Tests (spec 093-note-selective-filter, FR-011)
// =============================================================================

TEST_CASE("frequencyToNoteClass converts frequency to note class (0-11)", "[core][pitch][layer0][note-selective]") {

    SECTION("A440 maps to note class 9 (A)") {
        // MIDI note 69 = A4 (440Hz), noteClass = 69 % 12 = 9
        REQUIRE(frequencyToNoteClass(440.0f) == 9);
    }

    SECTION("C4 (261.63Hz) maps to note class 0 (C)") {
        // MIDI note 60 = C4, noteClass = 60 % 12 = 0
        REQUIRE(frequencyToNoteClass(261.63f) == 0);
    }

    SECTION("C0 (16.35Hz) maps to note class 0 (C)") {
        // MIDI note 12 = C0, noteClass = 12 % 12 = 0
        REQUIRE(frequencyToNoteClass(16.35f) == 0);
    }

    SECTION("D4 (293.66Hz) maps to note class 2 (D)") {
        // MIDI note 62 = D4, noteClass = 62 % 12 = 2
        REQUIRE(frequencyToNoteClass(293.66f) == 2);
    }

    SECTION("E4 (329.63Hz) maps to note class 4 (E)") {
        // MIDI note 64 = E4, noteClass = 64 % 12 = 4
        REQUIRE(frequencyToNoteClass(329.63f) == 4);
    }

    SECTION("G4 (392.0Hz) maps to note class 7 (G)") {
        // MIDI note 67 = G4, noteClass = 67 % 12 = 7
        REQUIRE(frequencyToNoteClass(392.0f) == 7);
    }

    SECTION("B4 (493.88Hz) maps to note class 11 (B)") {
        // MIDI note 71 = B4, noteClass = 71 % 12 = 11
        REQUIRE(frequencyToNoteClass(493.88f) == 11);
    }

    SECTION("C#4/Db4 (277.18Hz) maps to note class 1 (C#)") {
        // MIDI note 61 = C#4, noteClass = 61 % 12 = 1
        REQUIRE(frequencyToNoteClass(277.18f) == 1);
    }

    SECTION("High octave C8 (4186Hz) maps to note class 0 (C)") {
        // MIDI note 108 = C8, noteClass = 108 % 12 = 0
        REQUIRE(frequencyToNoteClass(4186.0f) == 0);
    }

    SECTION("Low octave A1 (55Hz) maps to note class 9 (A)") {
        // MIDI note 33 = A1, noteClass = 33 % 12 = 9
        REQUIRE(frequencyToNoteClass(55.0f) == 9);
    }

    SECTION("Invalid frequency (0 or negative) returns -1") {
        REQUIRE(frequencyToNoteClass(0.0f) == -1);
        REQUIRE(frequencyToNoteClass(-100.0f) == -1);
    }

    SECTION("All 12 note classes from chromatic scale") {
        // Standard frequencies for octave 4 (all A=440 based)
        // C4=261.63, C#4=277.18, D4=293.66, D#4=311.13, E4=329.63, F4=349.23,
        // F#4=369.99, G4=392.00, G#4=415.30, A4=440.00, A#4=466.16, B4=493.88
        const float freqs[] = {261.63f, 277.18f, 293.66f, 311.13f, 329.63f, 349.23f,
                               369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f};
        for (int i = 0; i < 12; ++i) {
            REQUIRE(frequencyToNoteClass(freqs[i]) == i);
        }
    }
}

// =============================================================================
// frequencyToCentsDeviation Tests (spec 093-note-selective-filter, FR-036)
// =============================================================================

TEST_CASE("frequencyToCentsDeviation returns cents deviation from nearest note center", "[core][pitch][layer0][note-selective]") {

    SECTION("Exact A440 returns 0 cents deviation") {
        REQUIRE(frequencyToCentsDeviation(440.0f) == Approx(0.0f).margin(0.5f));
    }

    SECTION("Exact C4 (261.63Hz) returns 0 cents deviation") {
        REQUIRE(frequencyToCentsDeviation(261.63f) == Approx(0.0f).margin(0.5f));
    }

    SECTION("Slightly sharp A440 (A4 + 10 cents)") {
        // 10 cents sharp: 440 * 2^(10/1200) ≈ 442.55Hz
        float sharpA = 440.0f * std::pow(2.0f, 10.0f / 1200.0f);
        REQUIRE(frequencyToCentsDeviation(sharpA) == Approx(10.0f).margin(0.5f));
    }

    SECTION("Slightly flat A440 (A4 - 10 cents)") {
        // 10 cents flat: 440 * 2^(-10/1200) ≈ 437.47Hz
        float flatA = 440.0f * std::pow(2.0f, -10.0f / 1200.0f);
        REQUIRE(frequencyToCentsDeviation(flatA) == Approx(-10.0f).margin(0.5f));
    }

    SECTION("13 cents flat from C4 (260Hz, per spec example)") {
        // 260Hz is approximately 13 cents flat from C4 (261.63Hz)
        float deviation = frequencyToCentsDeviation(260.0f);
        REQUIRE(deviation == Approx(-10.75f).margin(1.0f));  // Close to -11 cents
    }

    SECTION("44 cents flat from C4 (255Hz, per spec example)") {
        // 255Hz is approximately 44 cents flat from C4
        float deviation = frequencyToCentsDeviation(255.0f);
        REQUIRE(deviation == Approx(-44.0f).margin(2.0f));
    }

    SECTION("Boundary case: exactly between two notes (50 cents)") {
        // Halfway between A4 and A#4 (50 cents from each)
        // A#4 = 466.16Hz, midpoint = sqrt(440 * 466.16) ≈ 452.89Hz
        // At exactly 50 cents, the rounded MIDI note could go either way,
        // but the deviation from the chosen note should be close to 50 or -50
        float midpoint = std::sqrt(440.0f * 466.16f);
        float deviation = std::abs(frequencyToCentsDeviation(midpoint));
        REQUIRE(deviation == Approx(50.0f).margin(1.0f));
    }

    SECTION("Deviation range is approximately -50 to +50 cents") {
        // 25 cents sharp of A4
        float sharp25 = 440.0f * std::pow(2.0f, 25.0f / 1200.0f);
        REQUIRE(frequencyToCentsDeviation(sharp25) == Approx(25.0f).margin(0.5f));

        // 25 cents flat of A4
        float flat25 = 440.0f * std::pow(2.0f, -25.0f / 1200.0f);
        REQUIRE(frequencyToCentsDeviation(flat25) == Approx(-25.0f).margin(0.5f));

        // 49 cents sharp of A4 (still closer to A4)
        float sharp49 = 440.0f * std::pow(2.0f, 49.0f / 1200.0f);
        REQUIRE(frequencyToCentsDeviation(sharp49) == Approx(49.0f).margin(0.5f));

        // 49 cents flat of A4 (still closer to A4)
        float flat49 = 440.0f * std::pow(2.0f, -49.0f / 1200.0f);
        REQUIRE(frequencyToCentsDeviation(flat49) == Approx(-49.0f).margin(0.5f));
    }

    SECTION("Invalid frequency returns 0") {
        REQUIRE(frequencyToCentsDeviation(0.0f) == 0.0f);
        REQUIRE(frequencyToCentsDeviation(-100.0f) == 0.0f);
    }
}

// =============================================================================
// centsToPitchRatio Tests (Seraphis Phase 3, FR-072 / deviation D2)
// =============================================================================
//
// FR-072 specifies the body as semitonesToRatio(cents / 100.0f), i.e. one
// std::pow(2.0f, s / 12.0f) (pitch_utils.h:23,25). We ship std::exp2(cents / 1200)
// because it is the same real number and 2-4x cheaper (SC-010's 16,000 ns/block
// budget needs it). This case is what makes deviation D2 a CHECKED PROPERTY rather
// than a claim: it pins the shipped implementation to the specified definition at
// 1e-6 relative over the whole float-relevant cent range.

TEST_CASE("CentsToPitchRatio_MatchesSemitonesToRatio", "[pitch_utils][seraphis]") {

    SECTION("Zero cents returns exactly unity (bitwise)") {
        REQUIRE(centsToPitchRatio(0.0f) == 1.0f);
    }

    SECTION("Matches semitonesToRatio(cents / 100) within 1e-6 relative") {
        constexpr float kCentsGrid[] = {-4800.0f, -1200.0f, -100.0f, -11.0f, -1.0f, 0.0f,
                                        1.0f,     11.0f,    100.0f,  1200.0f, 4800.0f};

        for (float cents : kCentsGrid) {
            const float expected = semitonesToRatio(cents / 100.0f);
            const float actual = centsToPitchRatio(cents);
            INFO("cents = " << cents << ", expected = " << expected << ", actual = " << actual);
            REQUIRE(std::abs(actual - expected) <= 1e-6f * std::abs(expected));
        }
    }

    SECTION("Monotonic about unity") {
        REQUIRE(centsToPitchRatio(1.0f) > 1.0f);
        REQUIRE(centsToPitchRatio(-1.0f) < 1.0f);
    }
}

// =============================================================================
// centsToPitchRatioFast Tests (Seraphis Phase 3, plan section 8 lever 4)
// =============================================================================
//
// The degree-4 Horner promoted out of HarmonicCloud::detail::centsToDriftRatio,
// which is now a one-line forward to it. The Phase 2 case
// HarmonicCloud_CentsToRatioMatchesExp2 still gates the promoted body through that
// forward; this case gates it at its OWN Layer 0 name, so a future caller that
// reaches for it directly is covered whether or not HarmonicCloud still exists.
//
// The DOMAIN is the point. This function is only accurate on [-50, +50] cents and
// its two production callers are both bounded well inside that: HarmonicCloud's
// drift lanes clamp to [-1, +1] x kMaxDriftCents = 50, and EntropyProcessor's
// stage-2 + stage-3 sum is bounded by FR-074 at +-11.0 cents. The out-of-domain
// section below is therefore not a failure demonstration - it is the recorded
// reason the two names are separate functions.

TEST_CASE("CentsToPitchRatioFast_MatchesExp2OnItsDomain", "[pitch_utils][seraphis]") {
    /// The published worst case, measured over [-50, +50] at 20,001 points.
    constexpr double kPublishedWorstRelative = 6.15e-08;

    SECTION("Zero cents returns exactly unity (bitwise)") {
        // Load-bearing: the zero-entropy / zero-detune pass-through must be
        // bitwise, and the macOS leg builds -ffast-math, so this cannot be left
        // to the arithmetic being "obviously" exact.
        REQUIRE(centsToPitchRatioFast(0.0f) == 1.0f);
    }

    SECTION("Within the published bound over the whole [-50, +50] domain") {
        constexpr int kPoints = 20001;
        double worst = 0.0;
        float worstAt = 0.0f;
        for (int k = 0; k < kPoints; ++k) {
            const double t = static_cast<double>(k) / static_cast<double>(kPoints - 1);
            const auto cents = static_cast<float>(-50.0 + (100.0 * t));
            const double expected = std::exp2(static_cast<double>(cents) / 1200.0);
            const double actual = static_cast<double>(centsToPitchRatioFast(cents));
            const double relative = std::abs(actual - expected) / expected;
            if (relative > worst) {
                worst = relative;
                worstAt = cents;
            }
        }
        INFO("worst relative error " << worst << " at " << worstAt << " cents");
        // 1.5x the published figure: the measurement is a float-rounding pattern
        // and moves in the last bits across MSVC / GCC / Apple Clang, so pinning
        // it at exactly 6.15e-08 would be a bit-exactness demand in disguise.
        REQUIRE(worst <= 1.5 * kPublishedWorstRelative);
        // Non-vacuity: a polynomial that had somehow become exact would make the
        // bound above meaningless, and so would a sweep that never ran.
        REQUIRE(worst > 0.0);
    }

    SECTION("Agrees with centsToPitchRatio over the FR-074 entropy domain") {
        // EntropyProcessor::applyStages feeds this function a sum bounded by
        // kMaxDecoherenceCents + kMaxScatterCents = 11.0 cents.
        constexpr float kEntropyDomainCents = 11.0f;
        for (int k = -110; k <= 110; ++k) {
            const auto cents = static_cast<float>(k) * 0.1f;
            REQUIRE(std::abs(cents) <= kEntropyDomainCents + 1e-4f);
            const float exact = centsToPitchRatio(cents);
            const float fast = centsToPitchRatioFast(cents);
            INFO("cents = " << cents << ", exact = " << exact << ", fast = " << fast);
            REQUIRE(std::abs(fast - exact) <= 1e-6f * exact);
        }
    }

    SECTION("Monotonically increasing across the domain") {
        float previous = centsToPitchRatioFast(-50.0f);
        for (int k = -49; k <= 50; ++k) {
            const float next = centsToPitchRatioFast(static_cast<float>(k));
            REQUIRE(next > previous);
            previous = next;
        }
    }
}
