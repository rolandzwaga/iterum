// ==============================================================================
// DSP Utilities Unit Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// - Unit tests MUST cover all DSP algorithms with known input/output pairs
// - DSP algorithms MUST be pure functions testable without VST infrastructure
// ==============================================================================

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// Provide main for Catch2
int main(int argc, char* argv[]) {
    return Catch::Session().run(argc, argv);
}

#include "dsp/dsp_utils.h"

#include <array>
#include <cmath>

using namespace VSTWork::DSP;
using namespace Iterum::DSP;  // New dB/linear conversion API
using Catch::Approx;

// ==============================================================================
// Gain Conversion Tests (using Iterum::DSP API)
// ==============================================================================

TEST_CASE("dbToGain converts correctly", "[dsp][gain]") {
    SECTION("0 dB equals unity gain") {
        REQUIRE(dbToGain(0.0f) == Approx(1.0f));
    }

    SECTION("-6 dB is approximately half") {
        REQUIRE(dbToGain(-6.0206f) == Approx(0.5f).margin(0.001f));
    }

    SECTION("+6 dB is approximately double") {
        REQUIRE(dbToGain(6.0206f) == Approx(2.0f).margin(0.001f));
    }

    SECTION("-20 dB equals 0.1") {
        REQUIRE(dbToGain(-20.0f) == Approx(0.1f));
    }

    SECTION("+20 dB equals 10") {
        REQUIRE(dbToGain(20.0f) == Approx(10.0f));
    }
}

TEST_CASE("gainToDb converts correctly", "[dsp][gain]") {
    SECTION("Unity gain equals 0 dB") {
        REQUIRE(gainToDb(1.0f) == Approx(0.0f));
    }

    SECTION("Half gain is approximately -6 dB") {
        REQUIRE(gainToDb(0.5f) == Approx(-6.0206f).margin(0.01f));
    }

    SECTION("Double gain is approximately +6 dB") {
        REQUIRE(gainToDb(2.0f) == Approx(6.0206f).margin(0.01f));
    }

    SECTION("Zero/silence returns floor value") {
        REQUIRE(gainToDb(0.0f) == kSilenceFloorDb);
        REQUIRE(gainToDb(1e-10f) == kSilenceFloorDb);
    }
}

TEST_CASE("dB and gain are inverse operations", "[dsp][gain]") {
    const float testValues[] = {0.01f, 0.1f, 0.5f, 1.0f, 2.0f, 10.0f};

    for (float gain : testValues) {
        float dB = gainToDb(gain);
        float backToGain = dbToGain(dB);
        REQUIRE(backToGain == Approx(gain).margin(0.0001f));
    }
}

// ==============================================================================
// Buffer Operations Tests
// ==============================================================================

TEST_CASE("applyGain modifies buffer correctly", "[dsp][buffer]") {
    std::array<float, 4> buffer = {1.0f, 0.5f, -0.5f, -1.0f};

    SECTION("Unity gain leaves buffer unchanged") {
        applyGain(buffer.data(), buffer.size(), 1.0f);
        REQUIRE(buffer[0] == Approx(1.0f));
        REQUIRE(buffer[1] == Approx(0.5f));
        REQUIRE(buffer[2] == Approx(-0.5f));
        REQUIRE(buffer[3] == Approx(-1.0f));
    }

    SECTION("Half gain halves all samples") {
        applyGain(buffer.data(), buffer.size(), 0.5f);
        REQUIRE(buffer[0] == Approx(0.5f));
        REQUIRE(buffer[1] == Approx(0.25f));
        REQUIRE(buffer[2] == Approx(-0.25f));
        REQUIRE(buffer[3] == Approx(-0.5f));
    }

    SECTION("Zero gain silences buffer") {
        applyGain(buffer.data(), buffer.size(), 0.0f);
        for (float sample : buffer) {
            REQUIRE(sample == 0.0f);
        }
    }
}

TEST_CASE("copyWithGain copies and scales correctly", "[dsp][buffer]") {
    const std::array<float, 4> input = {1.0f, 0.5f, -0.5f, -1.0f};
    std::array<float, 4> output = {};

    SECTION("Unity gain copies exactly") {
        copyWithGain(input.data(), output.data(), input.size(), 1.0f);
        REQUIRE(output[0] == input[0]);
        REQUIRE(output[1] == input[1]);
        REQUIRE(output[2] == input[2]);
        REQUIRE(output[3] == input[3]);
    }

    SECTION("Double gain doubles samples") {
        copyWithGain(input.data(), output.data(), input.size(), 2.0f);
        REQUIRE(output[0] == Approx(2.0f));
        REQUIRE(output[1] == Approx(1.0f));
        REQUIRE(output[2] == Approx(-1.0f));
        REQUIRE(output[3] == Approx(-2.0f));
    }
}

TEST_CASE("mix combines buffers correctly", "[dsp][buffer]") {
    const std::array<float, 4> a = {1.0f, 0.0f, 1.0f, 0.0f};
    const std::array<float, 4> b = {0.0f, 1.0f, 0.0f, 1.0f};
    std::array<float, 4> output = {};

    SECTION("Equal mix of complementary signals") {
        mix(a.data(), 0.5f, b.data(), 0.5f, output.data(), 4);
        for (float sample : output) {
            REQUIRE(sample == Approx(0.5f));
        }
    }

    SECTION("Full A, zero B") {
        mix(a.data(), 1.0f, b.data(), 0.0f, output.data(), 4);
        REQUIRE(output[0] == a[0]);
        REQUIRE(output[1] == a[1]);
    }
}

TEST_CASE("clear zeroes buffer", "[dsp][buffer]") {
    std::array<float, 4> buffer = {1.0f, 0.5f, -0.5f, -1.0f};

    clear(buffer.data(), buffer.size());

    for (float sample : buffer) {
        REQUIRE(sample == 0.0f);
    }
}

// ==============================================================================
// Smoother Tests
// ==============================================================================

TEST_CASE("OnePoleSmoother smooths values", "[dsp][smoother]") {
    OnePoleSmoother smoother;

    SECTION("Immediate response with zero time") {
        smoother.setTime(0.0f, 44100.0f);
        smoother.reset(0.0f);

        float result = smoother.process(1.0f);
        REQUIRE(result == Approx(1.0f));
    }

    SECTION("Gradual response with non-zero time") {
        smoother.setTime(0.01f, 44100.0f);  // 10ms smoothing
        smoother.reset(0.0f);

        // First sample should be less than target
        float result = smoother.process(1.0f);
        REQUIRE(result > 0.0f);
        REQUIRE(result < 1.0f);

        // After many samples, should approach target
        // 10ms at 44.1kHz = 441 samples per time constant
        // Need ~5 time constants for 99% convergence = ~2200 samples
        for (int i = 0; i < 3000; ++i) {
            result = smoother.process(1.0f);
        }
        REQUIRE(result == Approx(1.0f).margin(0.01f));
    }

    SECTION("Reset immediately sets value") {
        smoother.reset(0.5f);
        REQUIRE(smoother.getValue() == 0.5f);
    }
}

// ==============================================================================
// Clipping Tests
// ==============================================================================

TEST_CASE("hardClip clamps to [-1, 1]", "[dsp][clip]") {
    REQUIRE(hardClip(0.0f) == 0.0f);
    REQUIRE(hardClip(0.5f) == 0.5f);
    REQUIRE(hardClip(-0.5f) == -0.5f);
    REQUIRE(hardClip(1.0f) == 1.0f);
    REQUIRE(hardClip(-1.0f) == -1.0f);
    REQUIRE(hardClip(2.0f) == 1.0f);
    REQUIRE(hardClip(-2.0f) == -1.0f);
    REQUIRE(hardClip(100.0f) == 1.0f);
}

TEST_CASE("softClip provides smooth saturation", "[dsp][clip]") {
    SECTION("Zero passes through") {
        REQUIRE(softClip(0.0f) == Approx(0.0f));
    }

    SECTION("Small values are nearly linear") {
        REQUIRE(softClip(0.1f) == Approx(0.1f).margin(0.01f));
    }

    SECTION("Large values saturate") {
        REQUIRE(softClip(10.0f) == Approx(1.0f).margin(0.01f));
        REQUIRE(softClip(-10.0f) == Approx(-1.0f).margin(0.01f));
    }

    SECTION("Symmetry around zero") {
        REQUIRE(softClip(0.5f) == Approx(-softClip(-0.5f)));
        REQUIRE(softClip(1.0f) == Approx(-softClip(-1.0f)));
    }
}

// ==============================================================================
// Analysis Tests
// ==============================================================================

TEST_CASE("calculateRMS computes correctly", "[dsp][analysis]") {
    SECTION("Silence has zero RMS") {
        std::array<float, 4> silence = {0.0f, 0.0f, 0.0f, 0.0f};
        REQUIRE(calculateRMS(silence.data(), silence.size()) == 0.0f);
    }

    SECTION("DC signal has RMS equal to level") {
        std::array<float, 4> dc = {0.5f, 0.5f, 0.5f, 0.5f};
        REQUIRE(calculateRMS(dc.data(), dc.size()) == Approx(0.5f));
    }

    SECTION("Full scale sine wave has RMS of ~0.707") {
        // Simplified: just test with known values
        std::array<float, 4> signal = {1.0f, 0.0f, -1.0f, 0.0f};
        float rms = calculateRMS(signal.data(), signal.size());
        REQUIRE(rms == Approx(std::sqrt(0.5f)));
    }

    SECTION("Empty buffer returns zero") {
        REQUIRE(calculateRMS(nullptr, 0) == 0.0f);
    }
}

TEST_CASE("findPeak finds maximum absolute value", "[dsp][analysis]") {
    SECTION("Positive peak") {
        std::array<float, 4> buffer = {0.1f, 0.5f, 0.3f, 0.2f};
        REQUIRE(findPeak(buffer.data(), buffer.size()) == 0.5f);
    }

    SECTION("Negative peak") {
        std::array<float, 4> buffer = {0.1f, -0.7f, 0.3f, 0.2f};
        REQUIRE(findPeak(buffer.data(), buffer.size()) == 0.7f);
    }

    SECTION("Silence") {
        std::array<float, 4> buffer = {0.0f, 0.0f, 0.0f, 0.0f};
        REQUIRE(findPeak(buffer.data(), buffer.size()) == 0.0f);
    }
}
