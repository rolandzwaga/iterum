// Layer 2: DSP Processor Tests - Grain Processor
// Part of Granular Delay feature (spec 034)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/grain_processor.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/core/pitch_utils.h>

#include <cmath>
#include <array>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// GrainProcessor Lifecycle Tests
// =============================================================================

TEST_CASE("GrainProcessor prepare and reset lifecycle", "[processors][grain-processor][layer2]") {
    GrainProcessor processor;

    SECTION("prepare initializes processor") {
        processor.prepare(44100.0);
        REQUIRE(processor.getEnvelopeType() == GrainEnvelopeType::Hann);
    }

    SECTION("prepare with custom envelope size") {
        processor.prepare(44100.0, 1024);
        // Should not crash, envelope table is internal
    }

    SECTION("reset is safe to call") {
        processor.prepare(44100.0);
        processor.reset();
        // Reset is a no-op for stateless processor
    }
}

// =============================================================================
// Envelope Type Tests
// =============================================================================

TEST_CASE("GrainProcessor envelope type control", "[processors][grain-processor][layer2]") {
    GrainProcessor processor;
    processor.prepare(44100.0);

    SECTION("default envelope is Hann") {
        REQUIRE(processor.getEnvelopeType() == GrainEnvelopeType::Hann);
    }

    SECTION("setEnvelopeType changes type") {
        processor.setEnvelopeType(GrainEnvelopeType::Trapezoid);
        REQUIRE(processor.getEnvelopeType() == GrainEnvelopeType::Trapezoid);

        processor.setEnvelopeType(GrainEnvelopeType::Blackman);
        REQUIRE(processor.getEnvelopeType() == GrainEnvelopeType::Blackman);
    }
}

// =============================================================================
// Grain Initialization Tests
// =============================================================================

TEST_CASE("GrainProcessor initializeGrain", "[processors][grain-processor][layer2]") {
    GrainProcessor processor;
    processor.prepare(44100.0);

    SECTION("initializes envelope phase and increment") {
        Grain grain{};
        GrainParams params{
            .grainSizeMs = 100.0f,  // 100ms grain
            .pitchSemitones = 0.0f,
            .positionSamples = 1000.0f,
            .pan = 0.0f,
            .reverse = false
        };

        processor.initializeGrain(grain, params);

        // Envelope should start at 0
        REQUIRE(grain.envelopePhase == 0.0f);

        // Increment should be 1.0 / (grain size in samples)
        // 100ms at 44100 Hz = 4410 samples
        // increment = 1.0 / 4410 ≈ 0.000227
        REQUIRE(grain.envelopeIncrement == Approx(1.0f / 4410.0f).margin(1e-6f));

        // Grain should be active
        REQUIRE(grain.active == true);
    }

    SECTION("calculates playback rate from pitch") {
        Grain grain{};
        GrainParams params{
            .grainSizeMs = 100.0f,
            .pitchSemitones = 12.0f,  // Octave up
            .positionSamples = 0.0f,
            .pan = 0.0f,
            .reverse = false
        };

        processor.initializeGrain(grain, params);

        // +12 semitones = 2.0x playback rate
        REQUIRE(grain.playbackRate == Approx(2.0f).margin(0.001f));
    }

    SECTION("sets read position from params") {
        Grain grain{};
        GrainParams params{
            .grainSizeMs = 100.0f,
            .pitchSemitones = 0.0f,
            .positionSamples = 2000.0f,
            .pan = 0.0f,
            .reverse = false
        };

        processor.initializeGrain(grain, params);

        REQUIRE(grain.readPosition == 2000.0f);
    }

    SECTION("calculates pan gains using constant power pan law") {
        Grain grain{};

        // Center pan (0.0)
        GrainParams paramsCenter{.grainSizeMs = 100.0f, .pan = 0.0f};
        processor.initializeGrain(grain, paramsCenter);
        REQUIRE(grain.panL == Approx(grain.panR).margin(0.01f));  // Equal

        // Full left (-1.0)
        GrainParams paramsLeft{.grainSizeMs = 100.0f, .pan = -1.0f};
        processor.initializeGrain(grain, paramsLeft);
        REQUIRE(grain.panL == Approx(1.0f).margin(0.01f));
        REQUIRE(grain.panR == Approx(0.0f).margin(0.01f));

        // Full right (+1.0)
        GrainParams paramsRight{.grainSizeMs = 100.0f, .pan = 1.0f};
        processor.initializeGrain(grain, paramsRight);
        REQUIRE(grain.panL == Approx(0.0f).margin(0.01f));
        REQUIRE(grain.panR == Approx(1.0f).margin(0.01f));
    }

    SECTION("handles reverse playback") {
        Grain grain{};
        GrainParams params{
            .grainSizeMs = 100.0f,
            .pitchSemitones = 0.0f,
            .positionSamples = 1000.0f,
            .pan = 0.0f,
            .reverse = true
        };

        processor.initializeGrain(grain, params);

        REQUIRE(grain.reverse == true);
        // Playback rate should be negative for reverse
        REQUIRE(grain.playbackRate < 0.0f);
    }
}

// =============================================================================
// Pitch Accuracy Tests (SC-003: accurate within 10 cents)
// =============================================================================

TEST_CASE("GrainProcessor pitch accuracy (SC-003)", "[processors][grain-processor][layer2][SC-003]") {
    GrainProcessor processor;
    processor.prepare(44100.0);

    SECTION("+12 semitones = exactly 2.0x playback rate") {
        Grain grain{};
        GrainParams params{.grainSizeMs = 100.0f, .pitchSemitones = 12.0f};

        processor.initializeGrain(grain, params);

        // 10 cents = 0.06% error, so 2.0 +/- 0.0012
        REQUIRE(grain.playbackRate == Approx(2.0f).margin(0.002f));
    }

    SECTION("-12 semitones = exactly 0.5x playback rate") {
        Grain grain{};
        GrainParams params{.grainSizeMs = 100.0f, .pitchSemitones = -12.0f};

        processor.initializeGrain(grain, params);

        REQUIRE(grain.playbackRate == Approx(0.5f).margin(0.001f));
    }

    SECTION("0 semitones = exactly 1.0x playback rate") {
        Grain grain{};
        GrainParams params{.grainSizeMs = 100.0f, .pitchSemitones = 0.0f};

        processor.initializeGrain(grain, params);

        REQUIRE(grain.playbackRate == Approx(1.0f).margin(0.0001f));
    }
}

// =============================================================================
// Grain Processing Tests
// =============================================================================

TEST_CASE("GrainProcessor processGrain", "[processors][grain-processor][layer2]") {
    GrainProcessor processor;
    processor.prepare(44100.0);

    // Create and prepare delay buffers with test signal
    DelayLine delayL, delayR;
    delayL.prepare(44100.0, 1.0f);  // 1 second buffer
    delayR.prepare(44100.0, 1.0f);
    delayL.reset();
    delayR.reset();

    // Fill buffer with known values
    for (int i = 0; i < 44100; ++i) {
        delayL.write(0.5f);
        delayR.write(0.5f);
    }

    SECTION("returns output based on envelope and input") {
        Grain grain{};
        GrainParams params{
            .grainSizeMs = 100.0f,
            .pitchSemitones = 0.0f,
            .positionSamples = 100.0f,  // Read from 100 samples delay
            .pan = 0.0f,
            .reverse = false
        };

        processor.initializeGrain(grain, params);

        // Process first sample
        auto [outL, outR] = processor.processGrain(grain, delayL, delayR);

        // Output should be small at start (envelope just starting)
        // The envelope starts at phase 0, which for Hann is 0
        REQUIRE(std::abs(outL) < 0.01f);
        REQUIRE(std::abs(outR) < 0.01f);

        // Process many samples to reach envelope peak
        for (int i = 0; i < 2200; ++i) {  // Half of 4410 samples
            (void)processor.processGrain(grain, delayL, delayR);
        }

        // At envelope peak, output should be close to input * envelope peak (1.0)
        auto [peakL, peakR] = processor.processGrain(grain, delayL, delayR);
        REQUIRE(std::abs(peakL) > 0.2f);  // Should have significant output
    }

    SECTION("advances envelope phase") {
        Grain grain{};
        GrainParams params{.grainSizeMs = 100.0f, .pitchSemitones = 0.0f};

        processor.initializeGrain(grain, params);
        float initialPhase = grain.envelopePhase;

        (void)processor.processGrain(grain, delayL, delayR);

        REQUIRE(grain.envelopePhase > initialPhase);
    }

    SECTION("advances read position") {
        Grain grain{};
        GrainParams params{
            .grainSizeMs = 100.0f,
            .pitchSemitones = 0.0f,
            .positionSamples = 100.0f,
            .reverse = false
        };

        processor.initializeGrain(grain, params);
        float initialPos = grain.readPosition;

        (void)processor.processGrain(grain, delayL, delayR);

        REQUIRE(grain.readPosition > initialPos);
    }

    SECTION("inactive grain returns zero") {
        Grain grain{};
        grain.active = false;

        auto [outL, outR] = processor.processGrain(grain, delayL, delayR);

        REQUIRE(outL == 0.0f);
        REQUIRE(outR == 0.0f);
    }
}

// =============================================================================
// Grain Completion Tests
// =============================================================================

TEST_CASE("GrainProcessor isGrainComplete", "[processors][grain-processor][layer2]") {
    GrainProcessor processor;
    processor.prepare(44100.0);

    DelayLine delayL, delayR;
    delayL.prepare(44100.0, 1.0f);
    delayR.prepare(44100.0, 1.0f);

    // Fill with test signal
    for (int i = 0; i < 44100; ++i) {
        delayL.write(0.5f);
        delayR.write(0.5f);
    }

    SECTION("newly initialized grain is not complete") {
        Grain grain{};
        GrainParams params{.grainSizeMs = 100.0f};

        processor.initializeGrain(grain, params);

        REQUIRE_FALSE(processor.isGrainComplete(grain));
    }

    SECTION("grain becomes complete after full envelope cycle") {
        Grain grain{};
        GrainParams params{.grainSizeMs = 10.0f};  // Short grain for fast test

        processor.initializeGrain(grain, params);

        // 10ms at 44100 Hz = 441 samples
        for (int i = 0; i < 450; ++i) {
            (void)processor.processGrain(grain, delayL, delayR);
        }

        REQUIRE(processor.isGrainComplete(grain));
    }

    SECTION("envelope phase >= 1.0 means complete") {
        Grain grain{};
        grain.envelopePhase = 1.0f;

        REQUIRE(processor.isGrainComplete(grain));

        grain.envelopePhase = 1.5f;  // Over complete
        REQUIRE(processor.isGrainComplete(grain));
    }
}

// =============================================================================
// Pan Law Tests
// =============================================================================

TEST_CASE("GrainProcessor pan law produces correct gains", "[processors][grain-processor][layer2]") {
    GrainProcessor processor;
    processor.prepare(44100.0);

    SECTION("constant power: L^2 + R^2 ≈ 1 for all pan values") {
        for (float pan = -1.0f; pan <= 1.0f; pan += 0.1f) {
            Grain grain{};
            GrainParams params{.grainSizeMs = 100.0f, .pan = pan};

            processor.initializeGrain(grain, params);

            float power = grain.panL * grain.panL + grain.panR * grain.panR;
            REQUIRE(power == Approx(1.0f).margin(0.01f));
        }
    }
}
