// Layer 0: Core Utility Tests - Pitch Conversion
// Part of Granular Delay feature (spec 034)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/core/pitch_utils.h"

using namespace Iterum::DSP;
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
