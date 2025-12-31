// Layer 4: User Feature Tests - Granular Delay
// Part of Granular Delay feature (spec 034)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/features/granular_delay.h"

#include <array>
#include <cmath>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// GranularDelay Lifecycle Tests
// =============================================================================

TEST_CASE("GranularDelay prepare and reset lifecycle", "[features][granular-delay][layer4]") {
    GranularDelay delay;

    SECTION("prepare initializes effect") {
        delay.prepare(44100.0);
        REQUIRE(delay.activeGrainCount() == 0);
        REQUIRE_FALSE(delay.isFrozen());
    }

    SECTION("reset clears all state") {
        delay.prepare(44100.0);
        delay.setDensity(100.0f);
        delay.seed(42);

        std::array<float, 512> inL{}, inR{}, outL{}, outR{};
        std::fill(inL.begin(), inL.end(), 0.5f);
        std::fill(inR.begin(), inR.end(), 0.5f);

        // Process to trigger grains
        for (int i = 0; i < 10; ++i) {
            delay.process(inL.data(), inR.data(), outL.data(), outR.data(), 512);
        }

        size_t activeBeforeReset = delay.activeGrainCount();
        REQUIRE(activeBeforeReset > 0);

        delay.reset();
        REQUIRE(delay.activeGrainCount() == 0);
    }

    SECTION("getLatencySamples returns zero") {
        delay.prepare(44100.0);
        REQUIRE(delay.getLatencySamples() == 0);
    }
}

// =============================================================================
// Parameter Control Tests
// =============================================================================

TEST_CASE("GranularDelay parameter controls", "[features][granular-delay][layer4]") {
    GranularDelay delay;
    delay.prepare(44100.0);

    SECTION("grain size control") {
        delay.setGrainSize(50.0f);
        delay.setGrainSize(200.0f);
        // Should not crash; effect should use these values
    }

    SECTION("density control") {
        delay.setDensity(10.0f);
        delay.setDensity(50.0f);
        delay.setDensity(100.0f);
    }

    SECTION("delay time control") {
        delay.setDelayTime(100.0f);
        delay.setDelayTime(500.0f);
        delay.setDelayTime(1000.0f);
    }

    SECTION("position spray control") {
        delay.setPositionSpray(0.0f);
        delay.setPositionSpray(0.5f);
        delay.setPositionSpray(1.0f);
    }

    SECTION("pitch control") {
        delay.setPitch(-12.0f);
        delay.setPitch(0.0f);
        delay.setPitch(12.0f);
    }

    SECTION("pitch spray control") {
        delay.setPitchSpray(0.0f);
        delay.setPitchSpray(0.5f);
        delay.setPitchSpray(1.0f);
    }

    SECTION("reverse probability control") {
        delay.setReverseProbability(0.0f);
        delay.setReverseProbability(0.5f);
        delay.setReverseProbability(1.0f);
    }

    SECTION("pan spray control") {
        delay.setPanSpray(0.0f);
        delay.setPanSpray(0.5f);
        delay.setPanSpray(1.0f);
    }

    SECTION("envelope type control") {
        delay.setEnvelopeType(GrainEnvelopeType::Hann);
        delay.setEnvelopeType(GrainEnvelopeType::Trapezoid);
        delay.setEnvelopeType(GrainEnvelopeType::Blackman);
        delay.setEnvelopeType(GrainEnvelopeType::Sine);
    }
}

// =============================================================================
// Global Control Tests
// =============================================================================

TEST_CASE("GranularDelay global controls", "[features][granular-delay][layer4]") {
    GranularDelay delay;
    delay.prepare(44100.0);

    SECTION("freeze mode control") {
        REQUIRE_FALSE(delay.isFrozen());

        delay.setFreeze(true);
        REQUIRE(delay.isFrozen());

        delay.setFreeze(false);
        REQUIRE_FALSE(delay.isFrozen());
    }

    SECTION("feedback control") {
        delay.setFeedback(0.0f);
        delay.setFeedback(0.5f);
        delay.setFeedback(1.0f);
        delay.setFeedback(1.2f);  // Self-oscillation range
    }

    SECTION("dry/wet mix control") {
        delay.setDryWet(0.0f);
        delay.setDryWet(0.5f);
        delay.setDryWet(1.0f);
    }
}

// =============================================================================
// Audio Processing Tests
// =============================================================================

TEST_CASE("GranularDelay audio processing", "[features][granular-delay][layer4]") {
    GranularDelay delay;
    delay.prepare(44100.0);
    delay.seed(42);

    SECTION("100% dry outputs input unchanged") {
        delay.setDryWet(0.0f);  // 100% dry
        delay.reset();  // Snap smoothers to new target values

        std::array<float, 512> inL{}, inR{}, outL{}, outR{};
        for (size_t i = 0; i < 512; ++i) {
            inL[i] = 0.5f;
            inR[i] = -0.3f;
        }

        delay.process(inL.data(), inR.data(), outL.data(), outR.data(), 512);

        for (size_t i = 0; i < 512; ++i) {
            REQUIRE(outL[i] == Approx(inL[i]).margin(0.01f));
            REQUIRE(outR[i] == Approx(inR[i]).margin(0.01f));
        }
    }

    SECTION("100% wet with no signal produces silence after buffer clear") {
        delay.setDryWet(1.0f);  // 100% wet
        delay.reset();

        std::array<float, 512> zeros{};
        std::array<float, 512> outL{}, outR{};

        // Process silence to clear any buffer contents
        for (int i = 0; i < 100; ++i) {
            delay.process(zeros.data(), zeros.data(), outL.data(), outR.data(), 512);
        }

        // Output should be near zero (grains reading from silent buffer)
        float sumL = 0.0f, sumR = 0.0f;
        for (size_t i = 0; i < 512; ++i) {
            sumL += std::abs(outL[i]);
            sumR += std::abs(outR[i]);
        }

        REQUIRE(sumL < 0.1f);
        REQUIRE(sumR < 0.1f);
    }

    SECTION("produces output with signal and grains") {
        delay.setDensity(50.0f);
        delay.setDelayTime(50.0f);
        delay.setDryWet(1.0f);  // 100% wet
        delay.reset();

        std::array<float, 512> inL{}, inR{}, outL{}, outR{};
        std::fill(inL.begin(), inL.end(), 0.5f);
        std::fill(inR.begin(), inR.end(), 0.5f);

        // First fill the buffer
        for (int i = 0; i < 10; ++i) {
            delay.process(inL.data(), inR.data(), outL.data(), outR.data(), 512);
        }

        // Now check for output
        bool anyOutput = false;
        for (size_t i = 0; i < 512; ++i) {
            if (std::abs(outL[i]) > 0.001f || std::abs(outR[i]) > 0.001f) {
                anyOutput = true;
                break;
            }
        }

        REQUIRE(anyOutput);
    }
}

// =============================================================================
// Dry/Wet Mix Tests
// =============================================================================

TEST_CASE("GranularDelay dry/wet mixing", "[features][granular-delay][layer4]") {
    GranularDelay delay;
    delay.prepare(44100.0);
    delay.seed(12345);

    SECTION("50% mix blends dry and wet") {
        delay.setDryWet(0.5f);
        delay.setDensity(50.0f);
        delay.setDelayTime(50.0f);
        delay.reset();

        std::array<float, 512> inL{}, inR{}, outL{}, outR{};
        std::fill(inL.begin(), inL.end(), 0.8f);
        std::fill(inR.begin(), inR.end(), 0.8f);

        // Process to fill buffer and generate output
        for (int i = 0; i < 20; ++i) {
            delay.process(inL.data(), inR.data(), outL.data(), outR.data(), 512);
        }

        // At 50% mix, output should be non-zero but different from pure input
        // (if there's any wet signal)
        bool hasDryComponent = false;
        for (size_t i = 0; i < 512; ++i) {
            // Dry would contribute 0.4f (0.8 * 0.5)
            if (std::abs(outL[i]) > 0.1f) {
                hasDryComponent = true;
                break;
            }
        }

        REQUIRE(hasDryComponent);
    }
}

// =============================================================================
// Feedback Tests
// =============================================================================

TEST_CASE("GranularDelay feedback behavior", "[features][granular-delay][layer4]") {
    GranularDelay delay;
    delay.prepare(44100.0);
    delay.seed(42);

    SECTION("zero feedback produces no buildup") {
        delay.setFeedback(0.0f);
        delay.setDryWet(1.0f);
        delay.setDensity(50.0f);
        delay.setDelayTime(100.0f);
        delay.reset();

        std::array<float, 512> impulse{};
        impulse[0] = 1.0f;  // Single impulse
        std::array<float, 512> outL{}, outR{};

        // Process impulse
        delay.process(impulse.data(), impulse.data(), outL.data(), outR.data(), 512);

        // Then process silence
        std::array<float, 512> zeros{};
        float maxOutput = 0.0f;
        for (int i = 0; i < 20; ++i) {
            delay.process(zeros.data(), zeros.data(), outL.data(), outR.data(), 512);
            for (size_t j = 0; j < 512; ++j) {
                maxOutput = std::max(maxOutput, std::abs(outL[j]));
                maxOutput = std::max(maxOutput, std::abs(outR[j]));
            }
        }

        // With no feedback, output should decay to near zero
        REQUIRE(maxOutput < 0.5f);  // Impulse was 1.0, should decay significantly
    }

    SECTION("high feedback maintains signal") {
        delay.setFeedback(0.9f);
        delay.setDryWet(1.0f);
        delay.setDensity(50.0f);
        delay.setDelayTime(100.0f);
        delay.reset();

        std::array<float, 512> inL{}, inR{}, outL{}, outR{};
        std::fill(inL.begin(), inL.end(), 0.5f);
        std::fill(inR.begin(), inR.end(), 0.5f);

        // Fill buffer with signal
        for (int i = 0; i < 20; ++i) {
            delay.process(inL.data(), inR.data(), outL.data(), outR.data(), 512);
        }

        // Now process silence with high feedback
        std::array<float, 512> zeros{};
        float sumOutput = 0.0f;
        for (int i = 0; i < 10; ++i) {
            delay.process(zeros.data(), zeros.data(), outL.data(), outR.data(), 512);
            for (size_t j = 0; j < 512; ++j) {
                sumOutput += std::abs(outL[j]) + std::abs(outR[j]);
            }
        }

        // With high feedback, should still have some output
        REQUIRE(sumOutput > 0.1f);
    }
}

// =============================================================================
// Freeze Mode Tests
// =============================================================================

TEST_CASE("GranularDelay freeze mode", "[features][granular-delay][layer4][freeze]") {
    GranularDelay delay;
    delay.prepare(44100.0);
    delay.seed(42);

    SECTION("freeze preserves buffer content") {
        delay.setDensity(50.0f);
        delay.setDelayTime(50.0f);
        delay.setDryWet(1.0f);
        delay.reset();

        // Fill buffer with signal
        std::array<float, 512> signal{}, outL{}, outR{};
        std::fill(signal.begin(), signal.end(), 0.7f);

        for (int i = 0; i < 20; ++i) {
            delay.process(signal.data(), signal.data(), outL.data(), outR.data(), 512);
        }

        // Enable freeze
        delay.setFreeze(true);

        // Process silence - buffer should still contain the signal
        std::array<float, 512> zeros{};
        bool hasOutput = false;

        for (int i = 0; i < 10; ++i) {
            delay.process(zeros.data(), zeros.data(), outL.data(), outR.data(), 512);
            for (size_t j = 0; j < 512; ++j) {
                if (std::abs(outL[j]) > 0.01f) {
                    hasOutput = true;
                }
            }
        }

        REQUIRE(hasOutput);  // Grains should still read from frozen buffer
    }
}

// =============================================================================
// Stability Tests (Phase 1.2/1.3 - Feedback and Output Limiting)
// =============================================================================

TEST_CASE("GranularDelay feedback stability", "[features][granular-delay][layer4][stability]") {
    GranularDelay delay;
    delay.prepare(44100.0);
    delay.seed(42);

    SECTION("moderate feedback with high overlap stays bounded") {
        // Configure for maximum stress: high density + large grains + feedback
        delay.setDensity(100.0f);     // Maximum density
        delay.setGrainSize(500.0f);   // Maximum grain size for overlap
        delay.setDelayTime(100.0f);   // Short delay
        delay.setFeedback(0.5f);      // 50% feedback
        delay.setDryWet(1.0f);        // 100% wet
        delay.reset();

        std::array<float, 512> inL{}, inR{}, outL{}, outR{};
        std::fill(inL.begin(), inL.end(), 1.0f);  // Unity amplitude input
        std::fill(inR.begin(), inR.end(), 1.0f);

        float maxAbsOutput = 0.0f;

        // Process 2 seconds of audio to allow feedback to accumulate
        for (int block = 0; block < 172; ++block) {  // ~2 seconds at 44100Hz
            delay.process(inL.data(), inR.data(), outL.data(), outR.data(), 512);

            for (size_t i = 0; i < 512; ++i) {
                maxAbsOutput = std::max(maxAbsOutput, std::max(std::abs(outL[i]), std::abs(outR[i])));
            }
        }

        // With proper feedback limiting, output should stay bounded
        // Without limiting: 50% feedback with 3x output would accumulate to infinity
        REQUIRE(maxAbsOutput <= 5.0f);
        REQUIRE(std::isfinite(maxAbsOutput));
    }

    SECTION("high feedback does not produce NaN or infinity") {
        delay.setDensity(100.0f);
        delay.setGrainSize(500.0f);
        delay.setDelayTime(100.0f);
        delay.setFeedback(1.0f);      // 100% feedback
        delay.setDryWet(1.0f);
        delay.reset();

        std::array<float, 512> inL{}, inR{}, outL{}, outR{};
        std::fill(inL.begin(), inL.end(), 0.5f);
        std::fill(inR.begin(), inR.end(), 0.5f);

        bool hasNaN = false;
        bool hasInf = false;

        // Process 3 seconds to stress test
        for (int block = 0; block < 258; ++block) {
            delay.process(inL.data(), inR.data(), outL.data(), outR.data(), 512);

            for (size_t i = 0; i < 512; ++i) {
                if (std::isnan(outL[i]) || std::isnan(outR[i])) hasNaN = true;
                if (std::isinf(outL[i]) || std::isinf(outR[i])) hasInf = true;
            }
        }

        REQUIRE_FALSE(hasNaN);
        REQUIRE_FALSE(hasInf);
    }
}

// =============================================================================
// Reproducibility Tests
// =============================================================================

TEST_CASE("GranularDelay seed produces reproducible output", "[features][granular-delay][layer4]") {
    SECTION("same seed produces same output") {
        GranularDelay delay1, delay2;

        delay1.prepare(44100.0);
        delay2.prepare(44100.0);

        delay1.setDensity(25.0f);
        delay2.setDensity(25.0f);
        delay1.setDryWet(1.0f);
        delay2.setDryWet(1.0f);

        delay1.seed(12345);
        delay2.seed(12345);
        delay1.reset();
        delay2.reset();

        std::array<float, 512> inL{}, inR{};
        std::array<float, 512> out1L{}, out1R{}, out2L{}, out2R{};
        std::fill(inL.begin(), inL.end(), 0.5f);
        std::fill(inR.begin(), inR.end(), 0.5f);

        bool allMatch = true;
        for (int block = 0; block < 20; ++block) {
            delay1.process(inL.data(), inR.data(), out1L.data(), out1R.data(), 512);
            delay2.process(inL.data(), inR.data(), out2L.data(), out2R.data(), 512);

            for (size_t i = 0; i < 512; ++i) {
                if (std::abs(out1L[i] - out2L[i]) > 0.0001f ||
                    std::abs(out1R[i] - out2R[i]) > 0.0001f) {
                    allMatch = false;
                    break;
                }
            }
            if (!allMatch) break;
        }

        REQUIRE(allMatch);
    }
}

// =============================================================================
// Stereo Width Tests (Phase 2.4)
// =============================================================================

TEST_CASE("GranularDelay stereo width control", "[features][granular-delay][layer4][stereo]") {
    GranularDelay delay;
    delay.prepare(44100.0);

    SECTION("default stereo width is 1.0 (full stereo)") {
        REQUIRE(delay.getStereoWidth() == Approx(1.0f));
    }

    SECTION("setStereoWidth/getStereoWidth work") {
        delay.setStereoWidth(0.5f);
        REQUIRE(delay.getStereoWidth() == Approx(0.5f));

        delay.setStereoWidth(0.0f);
        REQUIRE(delay.getStereoWidth() == Approx(0.0f));

        delay.setStereoWidth(1.0f);
        REQUIRE(delay.getStereoWidth() == Approx(1.0f));
    }

    SECTION("stereo width is clamped to 0-1") {
        delay.setStereoWidth(-0.5f);
        REQUIRE(delay.getStereoWidth() == 0.0f);

        delay.setStereoWidth(1.5f);
        REQUIRE(delay.getStereoWidth() == 1.0f);
    }
}

TEST_CASE("GranularDelay stereo width affects output stereo image", "[features][granular-delay][layer4][stereo]") {
    GranularDelay delay;
    delay.prepare(44100.0);
    delay.seed(42);

    SECTION("stereo width 0 produces mono output (L == R)") {
        delay.setDensity(50.0f);
        delay.setDelayTime(50.0f);
        delay.setPanSpray(1.0f);  // Full pan spray to create stereo difference
        delay.setDryWet(1.0f);    // Full wet for clearer test
        delay.setStereoWidth(0.0f);  // Mono output
        delay.reset();

        std::array<float, 512> inL{}, inR{}, outL{}, outR{};
        std::fill(inL.begin(), inL.end(), 0.5f);
        std::fill(inR.begin(), inR.end(), 0.5f);

        // Fill delay buffer
        for (int i = 0; i < 10; ++i) {
            delay.process(inL.data(), inR.data(), outL.data(), outR.data(), 512);
        }

        // Check that L and R are identical (mono)
        float maxDiff = 0.0f;
        for (size_t i = 0; i < 512; ++i) {
            maxDiff = std::max(maxDiff, std::abs(outL[i] - outR[i]));
        }

        // At width=0, L and R should be identical (or very close due to floating point)
        REQUIRE(maxDiff < 0.001f);
    }

    SECTION("stereo width 1 produces stereo output (L != R with pan spray)") {
        delay.setDensity(50.0f);
        delay.setDelayTime(50.0f);
        delay.setPanSpray(1.0f);  // Full pan spray
        delay.setDryWet(1.0f);
        delay.setStereoWidth(1.0f);  // Full stereo
        delay.reset();

        std::array<float, 512> inL{}, inR{}, outL{}, outR{};
        std::fill(inL.begin(), inL.end(), 0.5f);
        std::fill(inR.begin(), inR.end(), 0.5f);

        // Fill delay buffer
        for (int i = 0; i < 10; ++i) {
            delay.process(inL.data(), inR.data(), outL.data(), outR.data(), 512);
        }

        // Check that L and R differ (stereo)
        bool anyDifferent = false;
        for (size_t i = 0; i < 512; ++i) {
            if (std::abs(outL[i] - outR[i]) > 0.01f) {
                anyDifferent = true;
                break;
            }
        }

        // With pan spray and full stereo width, L and R should differ
        REQUIRE(anyDifferent);
    }
}

